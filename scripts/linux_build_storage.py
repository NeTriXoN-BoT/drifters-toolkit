"""Check Qt build capacity before growth, including the host volume under WSL.

The WSL record is an explicit local operator attestation, not hardware-backed
proof. Native Linux builds need local capacity checks and no Windows metadata.
No files are created, downloaded, moved or removed by these checks.
"""
from pathlib import Path, PureWindowsPath
from contextlib import contextmanager
import datetime
import hashlib
import json
import os
import platform
import re
import shutil
import stat
import subprocess
import time

MINIMUM_FREE=64*1024**3


def is_wsl():
    return ('microsoft' in platform.release().casefold()
            or bool(os.environ.get('WSL_DISTRO_NAME') or os.environ.get('WSL_INTEROP')))


def backing_path(value):
    if not isinstance(value,str):raise RuntimeError('The Windows backing path is missing.')
    value=value.replace('/','\\')
    if value.startswith('\\\\?\\'):value=value[4:]
    result=PureWindowsPath(value)
    if (not result.is_absolute() or not re.fullmatch(r'[A-Za-z]:',result.drive)
            or '..' in result.parts or any(':' in part for part in result.parts[1:])):
        raise RuntimeError('The backing path must name a local Windows drive.')
    return str(result).casefold()


def plain_directory_target(path):
    if '..' in Path(path).parts:
        raise RuntimeError('A Linux build destination must not contain parent traversal.')
    path=Path(path).absolute()
    for parent in (path,*path.parents):
        if parent.is_symlink() or parent.exists() and not parent.is_dir():
            raise RuntimeError('A Linux build destination is redirected or is not a directory.')
    existing=path
    while not existing.exists():existing=existing.parent
    return path,existing


def read_attestation(path):
    path=Path(path).absolute()
    for parent in (path,*path.parents):
        if parent.is_symlink():raise RuntimeError('The host-storage attestation is redirected.')
    descriptor=os.open(path,os.O_RDONLY|getattr(os,'O_NOFOLLOW',0)|getattr(os,'O_NONBLOCK',0))
    with os.fdopen(descriptor,'rb') as stream:
        before=os.fstat(stream.fileno())
        if not stat.S_ISREG(before.st_mode) or before.st_nlink!=1 or not 0<before.st_size<=65536:
            raise RuntimeError('The host-storage attestation is not a bounded plain file.')
        body=stream.read(65537);after=os.fstat(stream.fileno())
    if len(body)!=before.st_size or any(getattr(before,key)!=getattr(after,key) for key in ('st_dev','st_ino','st_size','st_mtime_ns','st_ctime_ns')):
        raise RuntimeError('The host-storage attestation changed while being checked.')
    return json.loads(body),{'sha256':hashlib.sha256(body).hexdigest(),'size':len(body)}


def verify_wsl_attestation(path,expected_host_drive,now=None):
    if path is None or not isinstance(expected_host_drive,str) or not re.fullmatch(r'[A-Z]:',expected_host_drive):
        raise RuntimeError('WSL builds require --storage-attestation and an explicit canonical --expected-host-drive such as D:.')
    attestation,identity=read_attestation(path)
    now=now or datetime.datetime.now(datetime.timezone.utc)
    try:checked=datetime.datetime.fromisoformat(attestation.get('checkedAtUtc','').replace('Z','+00:00'))
    except (TypeError,ValueError):raise RuntimeError('The host backing-volume check time is invalid.')
    if checked.tzinfo is None or not -300<=(now-checked).total_seconds()<=7200:
        raise RuntimeError('The host backing-volume storage check is missing or stale.')
    backing=backing_path(attestation.get('windowsBackingPath'))
    registry=backing_path(attestation.get('registryBackingPath'))
    registry_base=backing_path(attestation.get('registryBasePath'))
    available=attestation.get('hostAvailableBytes')
    if (attestation.get('schemaVersion')!=1 or not isinstance(attestation.get('distro'),str) or not attestation['distro']
            or attestation['distro']!=os.environ.get('WSL_DISTRO_NAME')
            or attestation.get('approvedHostDrive')!=expected_host_drive
            or PureWindowsPath(backing).drive.upper()!=expected_host_drive or registry!=backing
            or str(PureWindowsPath(registry_base)/'ext4.vhdx').casefold()!=registry
            or type(available) is not int or available<MINIMUM_FREE
            or type(attestation.get('maxProjectBytes')) is not int or attestation['maxProjectBytes']<=0
            or type(attestation.get('baselineVhdBytes')) is not int or attestation['baselineVhdBytes']<=0
            or type(attestation.get('baselineLinuxUsedBytes')) is not int or attestation['baselineLinuxUsedBytes']<=0
            or not isinstance(attestation.get('rootFilesystemSource'),str) or not attestation['rootFilesystemSource'].startswith('/dev/')):
        raise RuntimeError('The approved host drive, WSL registry VHD path and at least 64 GiB host free must match the operator attestation.')
    project=PureWindowsPath(backing_path(attestation.get('projectRootPath')))
    if (len(project.parts)<2 or project.drive.upper()!=expected_host_drive
            or not PureWindowsPath(backing).is_relative_to(project)):
        raise RuntimeError('The budget needs a dedicated project root containing its VHD.')
    return attestation,identity


def drvfs_path(value,drive):
    backing_path(value)
    value=value.replace('/','\\')
    if value.startswith('\\\\?\\'):value=value[4:]
    return Path('/mnt')/drive[0].lower()/Path(*PureWindowsPath(value).parts[1:])


def project_usage(project,vhd):
    """Read only the approved project tree's metadata, never other game folders."""
    project,vhd=Path(project),Path(vhd)
    plain_directory_target(project)
    total=0;files=0;vhd_size=None;vhd_counted=None;pending=[project]
    while pending:
        folder=pending.pop()
        with os.scandir(folder) as entries:
            for entry in entries:
                if entry.is_symlink():raise RuntimeError('A budgeted project path is redirected.')
                info=entry.stat(follow_symlinks=False)
                files+=1
                if files>10000:raise RuntimeError('The project budget inventory exceeds its metadata bound.')
                if stat.S_ISDIR(info.st_mode):pending.append(Path(entry.path));continue
                if not stat.S_ISREG(info.st_mode):raise RuntimeError('A budgeted project entry is not a plain file.')
                counted=max(info.st_size,max(0,getattr(info,'st_blocks',0))*512)
                total+=counted
                if str(Path(entry.path)).casefold()==str(vhd).casefold():
                    if vhd_size is not None:raise RuntimeError('The project has duplicate VHD paths.')
                    vhd_size=info.st_size;vhd_counted=counted
    if vhd_size is None or vhd_size<=0:raise RuntimeError('Live backing VHD length metadata is unavailable.')
    return {'projectBytes':total,'vhdFileBytes':vhd_size,'otherProjectBytes':total-vhd_counted,'entries':files}


class BudgetStop(RuntimeError):
    def __init__(self,reason,snapshot):
        super().__init__(reason);self.snapshot=snapshot


def budget_snapshot(targets,attestation=None,peak_vhd=0):
    roots=[shutil.disk_usage(plain_directory_target(path)[1]) for path in targets]
    snapshot={'checkedAtUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'linuxFreeBytes':min(row.free for row in roots),'linuxUsedBytes':max(row.used for row in roots)}
    reason=None
    if snapshot['linuxFreeBytes']<MINIMUM_FREE:reason='Linux filesystem reserve fell below 64 GiB.'
    if attestation is not None:
        drive=attestation['approvedHostDrive'];host=shutil.disk_usage(Path('/mnt')/drive[0].lower())
        usage=project_usage(drvfs_path(attestation['projectRootPath'],drive),drvfs_path(attestation['windowsBackingPath'],drive))
        estimate=max(peak_vhd,usage['vhdFileBytes'],attestation['baselineVhdBytes']+max(0,snapshot['linuxUsedBytes']-attestation['baselineLinuxUsedBytes']))
        estimate_project=estimate+usage['otherProjectBytes']
        snapshot.update(usage,hostFreeBytes=host.free,estimatedVhdBytes=estimate,estimatedProjectBytes=estimate_project,
                        maxProjectBytes=attestation['maxProjectBytes'],minimumFreeBytes=MINIMUM_FREE)
        if host.free<MINIMUM_FREE:reason='Actual host-drive reserve fell below 64 GiB.'
        if max(usage['projectBytes'],estimate_project)>attestation['maxProjectBytes']:
            reason='The approved single-project storage budget was exceeded.'
    snapshot['status']='within-budget' if reason is None else 'budget-stopped'
    if reason is not None:
        snapshot['reason']=reason;raise BudgetStop(reason,snapshot)
    return snapshot


@contextmanager
def project_build_lock(receipt,lock_directory=None):
    """One build owner per project; this never locks unrelated Docker projects."""
    import fcntl
    name=hashlib.sha256(receipt['projectScope'].encode()).hexdigest()[:24]
    path=Path(lock_directory or '/tmp')/('.drifters-toolkit-project-'+name+'.lock')
    descriptor=os.open(path,os.O_CREAT|os.O_RDWR|getattr(os,'O_NOFOLLOW',0),0o600)
    with os.fdopen(descriptor,'w') as guard:
        info=os.fstat(guard.fileno())
        if not stat.S_ISREG(info.st_mode) or info.st_nlink!=1:raise RuntimeError('The project build lock is redirected.')
        try:fcntl.flock(guard,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError:raise RuntimeError('Another compiler group already owns this project.')
        guard.write(str(os.getpid())+'\n');guard.flush()
        try:yield
        finally:fcntl.flock(guard,fcntl.LOCK_UN)


class RuntimeBudget:
    def __init__(self,targets,attestation_path=None,expected_host_drive=None,receipt_path=None):
        self.targets=tuple(targets);self.receipt_path=Path(receipt_path) if receipt_path else None
        self.attestation=verify_wsl_attestation(attestation_path,expected_host_drive)[0] if is_wsl() else None
        self.peak_vhd=0;self.history=[]

    def check(self):
        try:
            snapshot=budget_snapshot(self.targets,self.attestation,self.peak_vhd)
        except BudgetStop as error:
            self.record(error.snapshot);raise
        except (OSError,RuntimeError,ValueError) as error:
            snapshot={'status':'budget-check-unavailable','reason':str(error),
                      'checkedAtUtc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
            self.record(snapshot);raise BudgetStop('Live budget metadata is unavailable; compilation stopped.',snapshot) from error
        self.peak_vhd=snapshot.get('estimatedVhdBytes',self.peak_vhd)
        self.record(snapshot)
        return snapshot

    def record(self,snapshot):
        self.history=(self.history+[snapshot])[-12:]
        if self.receipt_path is not None:
            self.receipt_path.write_text(json.dumps({'latest':snapshot,'recent':self.history},indent=2)+'\n',encoding='utf-8')


def check_build_storage(targets,attestation_path=None,expected_host_drive=None,now=None):
    paths=[plain_directory_target(path) for path in targets]
    attestation=None
    receipt={'mode':'native-linux','minimumFreeBytes':MINIMUM_FREE}
    if is_wsl():
        attestation,identity=verify_wsl_attestation(attestation_path,expected_host_drive,now)
        receipt={'mode':'operator-attested-wsl','minimumFreeBytes':MINIMUM_FREE,'attestation':identity,
                 'approvedHostDrive':expected_host_drive,'maxProjectBytes':attestation['maxProjectBytes']}
    for path,existing in paths:
        if attestation is not None:
            if str(path).startswith(('/mnt/','/run/desktop/')):
                raise RuntimeError('The large Linux build cannot write to a Windows/shared mount.')
            source=subprocess.check_output(['findmnt','--noheadings','--output','SOURCE','--target',str(existing)],text=True).strip()
            if source!=attestation['rootFilesystemSource']:
                raise RuntimeError('A build destination is not on the attested Linux root filesystem.')
        if shutil.disk_usage(existing).free<MINIMUM_FREE:
            raise RuntimeError('The Linux build filesystem needs at least 64 GiB free before growth.')
    receipt['projectScope']=backing_path(attestation['projectRootPath']) if attestation else '|'.join(str(path) for path,_ in paths)
    receipt['initialBudget']=budget_snapshot([path for path,_ in paths],attestation)
    return receipt
