"""Validate and continue an existing pinned WebEngine build after storage migration.

No downloading, source extraction, SDK cloning or system installation occurs.
All writes stay in the explicit existing Linux work/prefix. A recent host-storage
attestation is required because a WSL virtual filesystem's free space alone does
not establish the capacity of its Windows backing volume.
"""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import os
import re
import signal
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode=True
import bootstrap_qt_webengine_security as security
from linux_build_storage import check_build_storage, project_build_lock, RuntimeBudget, BudgetStop

ROOT=Path(__file__).resolve().parents[1]
LOCK=Path(__file__).with_name('qt_webengine_resume_lock.json')
INVENTORY=Path(__file__).with_name('qt_webengine_patched_files.json')
FINAL_INVENTORY=Path(__file__).with_name('qt_webengine_final_files.json')
HARD_STOP=getattr(signal,'SIGKILL',9)
MIN_AVAILABLE_MEMORY=4*1024**3


class MemoryStop(RuntimeError):
    def __init__(self,snapshot):
        self.snapshot=snapshot
        super().__init__('Available Linux memory is below the 4 GiB build reserve.')


def check_memory():
    values={line.split(':',1)[0]:line.split(':',1)[1].strip() for line in Path('/proc/meminfo').read_text().splitlines() if ':' in line}
    match=re.fullmatch(r'([0-9]+) kB',values.get('MemAvailable',''))
    if not match:raise RuntimeError('Live Linux available memory cannot be established.')
    snapshot={'availableMemoryBytes':int(match[1])*1024,'minimumAvailableMemoryBytes':MIN_AVAILABLE_MEMORY}
    if snapshot['availableMemoryBytes']<MIN_AVAILABLE_MEMORY:raise MemoryStop(snapshot)
    return snapshot


def load_resume_inputs():
    data=json.loads(LOCK.read_text(encoding='utf-8'))
    if (data.get('schemaVersion')!=1 or data.get('resumeRevision')!=2
            or data.get('sourceInventory')!=security.file_record(INVENTORY)
            or data.get('finalSourceInventory')!=security.file_record(FINAL_INVENTORY)):
        raise RuntimeError('The pinned WebEngine continuation inputs are invalid.')
    if security.file_record(ROOT/data['configuredRecipeFile'])!=data['configuredRecipe']:
        raise RuntimeError('The retained original configure recipe source is missing or changed.')
    original=security.verify_inputs()
    inventory=json.loads(INVENTORY.read_text(encoding='utf-8'))
    vendor_files=inventory.get('files',{})
    expected={name for row in original['patches'] for name in row['files']}
    if (inventory.get('schemaVersion')!=1 or inventory.get('targetChromiumCommit')!=original['patchedChromiumCommit']
            or data.get('targetChromiumCommit')!=original['patchedChromiumCommit'] or set(vendor_files)!=expected or len(vendor_files)!=75):
        raise RuntimeError('The continuation inventory does not cover the exact official patch series.')
    final=json.loads(FINAL_INVENTORY.read_text(encoding='utf-8'));files=final.get('files',{})
    supplements=original['compatibilityBackports']
    expected|={name for row in supplements for name in row['files']}
    records=[{key:row[key] for key in ('name','sha256','size')} for row in supplements]
    if (final.get('schemaVersion')!=1 or final.get('vendorChromiumCommit')!=original['patchedChromiumCommit']
            or final.get('vendorInventory')!=security.file_record(INVENTORY) or final.get('compatibilityBackports')!=records
            or set(files)!=expected or len(files)!=81):
        raise RuntimeError('The final derived source inventory does not cover the exact vendor plus Aura supplement.')
    before={name:value['sha256'] for row in supplements for name,value in row['before'].items()}
    after={name:value['sha256'] for row in supplements for name,value in row['after'].items()}
    if (any(before.get(name,files[name])!=value for name,value in vendor_files.items())
            or any(files[name]!=value for name,value in after.items())):
        raise RuntimeError('The original vendor target and final derived source identities were conflated.')
    for name,digest in files.items():
        path=PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts or not re.fullmatch('[a-f0-9]{64}',digest):
            raise RuntimeError('A continuation source identity is invalid.')
    return data,original,files


def verify_source(source,files):
    source=Path(source).resolve(strict=True)
    for name,digest in files.items():
        target=source/'src/3rdparty'/name
        if not target.resolve(strict=True).is_relative_to(source) or security.file_record(target)['sha256']!=digest:
            raise RuntimeError('A retained patched source differs from the verified vendor target: '+name)
    return {'targetFiles':len(files),'inventory':security.file_record(FINAL_INVENTORY),'vendorInventory':security.file_record(INVENTORY)}


def cache_values(path):
    values={}
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        match=re.fullmatch(r'([^#/:][^:]*):[^=]+=(.*)',line)
        if match:
            key,value=match.groups()
            if key in values:raise RuntimeError('A CMake build cache key is duplicated.')
            values[key]=value
    return values


def verify_configuration(work,prefix):
    work,prefix=Path(work).resolve(strict=True),Path(prefix).resolve(strict=True)
    output=work/'build';cache=output/'CMakeCache.txt';values=cache_values(cache)
    flags='-march=x86-64-v2 -mtune=generic -fstack-protector-strong -ffile-prefix-map='+str(work)+'=/usr/src/drifters-qt-webengine'
    expected={'CMAKE_HOME_DIRECTORY':str(work/'source'),'CMAKE_INSTALL_PREFIX':str(prefix),'CMAKE_BUILD_TYPE':'Release',
              'CMAKE_C_COMPILER':'/usr/bin/clang-19','CMAKE_CXX_COMPILER':'/usr/bin/clang++-19',
              'CMAKE_C_FLAGS':flags,'CMAKE_CXX_FLAGS':flags,'CMAKE_EXE_LINKER_FLAGS':'-fuse-ld=lld-19',
              'CMAKE_SHARED_LINKER_FLAGS':'-fuse-ld=lld-19','QT_BUILD_EXAMPLES':'OFF','QT_BUILD_TESTS':'OFF',
              'QT_FEATURE_webengine_jumbo_build':'ON','QT_FEATURE_webengine_system_libwebp':'OFF'}
    expected['FEATURE_webengine_printing_and_pdf']='ON'
    for key,value in expected.items():
        if values.get(key)!=value:raise RuntimeError('The retained CMake setting differs: '+key)
    qt_dirs={key:value for key,value in values.items() if re.fullmatch(r'Qt6[A-Za-z0-9_]*_DIR',key) and not value.endswith('-NOTFOUND')}
    if not qt_dirs or any(not Path(value).resolve(strict=True).is_relative_to(prefix) for value in qt_dirs.values()):
        raise RuntimeError('A retained Qt module points outside the existing SDK prefix.')
    for key,value in values.items():
        if 'sandbox' in key.lower() and value.upper() in ('OFF','FALSE','0'):
            raise RuntimeError('A retained Qt configuration disables sandbox support.')
    args=output/'src/core/Release/x86_64/args.gn';gn={}
    for line in args.read_text(encoding='utf-8').splitlines():
        if '=' in line:
            key,value=line.split('=',1)
            if key.strip() in gn and gn[key.strip()]!=value.strip():
                raise RuntimeError('A Chromium GN argument has conflicting duplicate values.')
            gn[key.strip()]=value.strip()
    expected_gn={'is_debug':'false','is_official_build':'true','is_unsafe_developer_build':'false',
                 'is_clang':'true','enable_rust':'false','use_jumbo_build':'true','jumbo_file_merge_limit':'8',
                 'use_sysroot':'false','use_system_libwebp':'false','use_system_icu':'false','use_custom_libcxx':'false',
                 'proprietary_codecs':'false','symbol_level':'0',
                 'custom_toolchain':json.dumps(str(output/'src/core/target_toolchain')+':target'),
                 'host_toolchain':json.dumps(str(output/'src/core/host_toolchain')+':host')}
    for key,value in expected_gn.items():
        if gn.get(key)!=value:raise RuntimeError('The retained Chromium GN setting differs: '+key)
    for key,value in gn.items():
        if (('sandbox' in key or 'seccomp' in key) and value=='false') or key=='no_sandbox' and value=='true':
            raise RuntimeError('A retained Chromium configuration disables sandbox support.')
    ninja=output/'build.ninja';body=ninja.read_text(encoding='utf-8')
    nested=[line for line in body.splitlines() if re.search(r'\bninja\b',line) and 'QtWebEngineCore' in line and 'COMMAND =' in line]
    if not nested or any(not re.search(r'\bninja\s+-j\s+2\s+-C\s',line) for line in nested):
        raise RuntimeError('The configured nested Chromium Ninja is not bounded to two jobs.')
    return {'cmakeCache':security.file_record(cache),'gnArgs':security.file_record(args),
            'outerNinja':security.file_record(ninja),'settings':expected,'gnSettings':expected_gn}


def verify_storage(path,work,prefix,expected_host_drive,now=None):
    return check_build_storage((work,prefix),path,expected_host_drive,now)


def version(command):
    return subprocess.check_output(command,text=True,stderr=subprocess.STDOUT).splitlines()[0].strip()


def verify_tools(work,data):
    node=Path(work)/'node-v22.23.4-linux-x64/bin/node'
    actual={'clang':version(['/usr/bin/clang++-19','--version']),'node':version([str(node),'--version']),
            'cmake':version(['cmake','--version']),'ninja':version(['ninja','--version'])}
    if actual!=data['expectedToolVersions']:raise RuntimeError('The retained build tools changed during migration.')
    return actual


def verify_continuation(record):
    data,original,files=load_resume_inputs()
    override=record.get('coreBuildOverride') if isinstance(record,dict) else None
    if override not in (None,{'target':'QtWebEngineCore','jobs':3,'phase':'normal-ninja'}):
        raise RuntimeError('The WebEngine Core job override is invalid.')
    expected={'schemaVersion':1,'resumeRevision':data['resumeRevision'],'helper':security.file_record(__file__),
              'storageHelper':security.file_record(Path(__file__).with_name('linux_build_storage.py')),
              'resumeLock':security.file_record(LOCK),'sourceInventory':security.file_record(INVENTORY),
              'finalSourceInventory':security.file_record(FINAL_INVENTORY),
              'configuredRecipe':data['configuredRecipe'],'finalRecipe':security.file_record(security.__file__),
              'validatedSourceFiles':len(files),'vendorSourceFiles':75,'nestedJobs':3 if override else 2,'configuredNestedJobs':2,'outerJobs':1,
              'coreBuildOverride':override,'minimumAvailableMemoryBytes':MIN_AVAILABLE_MEMORY,'runtimeScope':security.runtime_scope()}
    if not isinstance(record,dict) or set(record)!=set(expected) or record!=expected:
        raise RuntimeError('The WebEngine build continuation identity is missing or stale.')
    return record


def continuation_record(data,core_jobs=2):
    if type(core_jobs) is not int or core_jobs not in (2,3):raise RuntimeError('The retained Core phase allows two or three jobs.')
    return verify_continuation({'schemaVersion':1,'resumeRevision':data['resumeRevision'],'helper':security.file_record(__file__),
              'storageHelper':security.file_record(Path(__file__).with_name('linux_build_storage.py')),
              'resumeLock':security.file_record(LOCK),'sourceInventory':security.file_record(INVENTORY),
              'finalSourceInventory':security.file_record(FINAL_INVENTORY),
              'configuredRecipe':data['configuredRecipe'],'finalRecipe':security.file_record(security.__file__),
              'validatedSourceFiles':81,'vendorSourceFiles':75,'nestedJobs':core_jobs,'configuredNestedJobs':2,'outerJobs':1,
              'coreBuildOverride':{'target':'QtWebEngineCore','jobs':3,'phase':'normal-ninja'} if core_jobs==3 else None,
              'minimumAvailableMemoryBytes':MIN_AVAILABLE_MEMORY,'runtimeScope':security.runtime_scope()})


def owned_descendants(root):
    """Track Qt tool wrappers that create process groups below the owner."""
    rows={}
    try:entries=list(Path('/proc').iterdir())
    except OSError:return rows
    for entry in entries:
        if not entry.name.isdigit():continue
        try:
            fields=(entry/'stat').read_text().split(') ',1)[1].split()
            rows[int(entry.name)]={'pid':int(entry.name),'parent':int(fields[1]),'start':fields[19],
                                   'uid':entry.stat().st_uid,'state':fields[0]}
        except (OSError,ValueError):pass
    selected={root}
    while True:
        added={pid for pid,row in rows.items() if row['parent'] in selected}
        if added<=selected:break
        selected|=added
    return {pid:rows[pid] for pid in selected if pid in rows and rows[pid]['uid']==os.getuid()}


def signal_owned(rows,number):
    for old in rows.values():
        entry=Path('/proc')/str(old['pid'])
        try:
            fields=(entry/'stat').read_text().split(') ',1)[1].split()
            if fields[19]==old['start'] and entry.stat().st_uid==old['uid']:
                os.kill(old['pid'],number)
        except (OSError,ValueError):pass


def run_phase(command,log_path,env,state_path,phase,budget=None,memory_guard=False):
    """Own the child process group so interruption cannot orphan a compiler tree."""
    with Path(log_path).open('a',encoding='utf-8') as log:
        child=None;interruption=[None];tracked={}
        previous={number:signal.getsignal(number) for number in (signal.SIGTERM,signal.SIGINT)}
        # Delay raising until Popen has returned an owned PID. Raising inside
        # process creation could leave a newly forked process without an owner.
        def interrupted(signum,frame):interruption[0]=signum
        for number in previous:signal.signal(number,interrupted)
        try:
            if budget is not None:budget.check()
            if memory_guard:check_memory()
            child=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
            tracked.update(owned_descendants(child.pid))
            if interruption[0] is not None:raise KeyboardInterrupt
            Path(state_path).write_text(json.dumps({'phase':phase,'pid':child.pid,'processGroup':child.pid,'command':command},indent=2)+'\n',encoding='utf-8')
            print(phase+': child process group '+str(child.pid),flush=True)
            next_check=time.monotonic()+5
            while True:
                tracked.update(owned_descendants(child.pid))
                if interruption[0] is not None:raise KeyboardInterrupt
                if (budget is not None or memory_guard) and time.monotonic()>=next_check:
                    if budget is not None:budget.check()
                    if memory_guard:check_memory()
                    next_check=time.monotonic()+5
                try:code=child.wait(timeout=0.5);break
                except subprocess.TimeoutExpired:pass
            if code:raise subprocess.CalledProcessError(code,command)
        except BaseException as error:
            if child is not None:
                tracked.update(owned_descendants(child.pid))
                signal_owned(tracked,signal.SIGTERM)
                try:os.killpg(child.pid,signal.SIGTERM)
                except ProcessLookupError:pass
                try:child.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    signal_owned(tracked,HARD_STOP)
                    try:os.killpg(child.pid,HARD_STOP)
                    except ProcessLookupError:pass
                    child.wait()
                signal_owned(tracked,HARD_STOP)
            if isinstance(error,BudgetStop):
                Path(state_path).write_text(json.dumps({'phase':'budget-stopped','reason':str(error),'snapshot':error.snapshot},indent=2)+'\n',encoding='utf-8')
                print('Build stopped by storage budget: '+str(error),file=sys.stderr,flush=True)
            elif isinstance(error,MemoryStop):
                Path(state_path).write_text(json.dumps({'phase':'memory-stopped','reason':str(error),'snapshot':error.snapshot},indent=2)+'\n',encoding='utf-8')
                print('Build stopped by memory reserve: '+str(error),file=sys.stderr,flush=True)
            raise
        finally:
            for number,handler in previous.items():signal.signal(number,handler)


def publish_proof(prefix,proof):
    """Publish exclusively and remove only this attempt's unique temporary file."""
    target=Path(prefix)/security.PROOF;temporary=None
    try:
        with tempfile.NamedTemporaryFile(mode='w',encoding='utf-8',prefix='.qtwebengine-security-',dir=prefix,delete=False) as stream:
            temporary=Path(stream.name);stream.write(json.dumps(proof,indent=2)+'\n');stream.flush();os.fsync(stream.fileno())
        os.link(temporary,target)
    finally:
        if temporary is not None:temporary.unlink(missing_ok=True)
    return target


def elf_floor(paths):
    result=[]
    for path in paths:
        with path.open('rb') as stream:magic=stream.read(4)
        if magic!=b'\x7fELF':continue
        text=subprocess.check_output(['readelf','--version-info',str(path)],text=True)
        versions={kind:sorted({tuple(map(int,value.split('.'))) for value in re.findall(kind+r'_([0-9]+(?:\.[0-9]+)+)',text)}) for kind in ('GLIBC','GLIBCXX')}
        if (versions['GLIBC'] and versions['GLIBC'][-1]>(2,35)) or (versions['GLIBCXX'] and versions['GLIBCXX'][-1]>(3,4,30)):
            raise RuntimeError('The rebuilt WebEngine exceeds the established Linux ABI floor: '+path.name)
        result.append({'file':path.name,'versions':versions})
    return result


def resume(work,prefix,source_archive,configured_recipe,storage_attestation=None,expected_host_drive=None,check_only=False,core_jobs=2):
    data,original,files=load_resume_inputs()
    work,prefix=Path(work).resolve(strict=True),Path(prefix).resolve(strict=True)
    if not (work/'build').is_dir() or not (prefix/'lib').is_dir():raise RuntimeError('The retained build/SDK is missing; no new preparation will be created.')
    storage=verify_storage(storage_attestation,work,prefix,expected_host_drive)
    if security.file_record(configured_recipe)!=data['configuredRecipe']:
        raise RuntimeError('The original configured recipe differs from its retained pin.')
    pin=original['sourceArchive']
    if security.file_record(source_archive)!={'sha256':pin['digest'],'size':pin['size']}:
        raise RuntimeError('The retained original WebEngine source archive differs.')
    source=verify_source(work/'source',files);configuration=verify_configuration(work,prefix);tools=verify_tools(work,data)
    record=continuation_record(data,core_jobs)
    receipt={'schemaVersion':1,'source':source,'configuration':configuration,'tools':tools,'storageAttestation':storage,'continuation':record}
    if check_only:
        print(json.dumps(receipt,indent=2));return receipt
    if (prefix/security.PROOF).exists():
        proof=security.verify_build(prefix)
        print('The updated SDK is already complete and verified; preserved without rebuilding.')
        return proof
    import fcntl
    descriptor=os.open(work/'continuation.lock',os.O_CREAT|os.O_RDWR|getattr(os,'O_NOFOLLOW',0),0o600)
    with os.fdopen(descriptor,'w') as guard, project_build_lock(storage):
        fcntl.flock(guard,fcntl.LOCK_EX|fcntl.LOCK_NB)
        (work/'continuation-validation.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
        original_path=work/'continuation-original-core.json'
        if original_path.exists():before=json.loads(original_path.read_text(encoding='utf-8'))
        else:
            before={path.name:security.file_record(path) for path in (prefix/'lib').glob('libQt6WebEngineCore.so.*') if path.is_file() and not path.is_symlink()}
            original_path.write_text(json.dumps(before,indent=2)+'\n',encoding='utf-8')
        if not before:raise RuntimeError('The original SDK core identity is missing.')
        env={key:value for key,value in os.environ.items() if not key.startswith(('LD_','QT_','QTWEBENGINE_'))}
        env['PATH']=str(work/'node-v22.23.4-linux-x64/bin')+os.pathsep+env['PATH']
        # Match the NODEJS_EXECUTABLE supplied by Qt's generated cmake -E env
        # wrapper when invoking its existing Core Ninja directory directly.
        env['NODEJS_EXECUTABLE']=str(work/'node-v22.23.4-linux-x64/bin/node')
        env['NINJAFLAGS']='-j 2';env['CMAKE_BUILD_PARALLEL_LEVEL']='1'
        budget=RuntimeBudget((work,prefix),storage_attestation,expected_host_drive,work/'continuation-budget.json')
        if core_jobs==3:
            run_phase(['ninja','-j','3','-C',str(work/'build/src/core/Release/x86_64'),'QtWebEngineCore'],work/'continuation-build.log',env,work/'continuation-process.json','core-override',budget,True)
        run_phase(['cmake','--build',str(work/'build'),'--target',*security.RUNTIME_TARGETS,'--parallel','1'],work/'continuation-build.log',env,work/'continuation-process.json','runtime-build',budget,True)
        # Verify the source again before installing or creating any success proof.
        verify_source(work/'source',files)
        for script in security.RUNTIME_INSTALL_SCRIPTS:
            run_phase(['cmake','-DCMAKE_INSTALL_PREFIX='+str(prefix),'-P',str(work/'build'/script)],work/'continuation-install.log',env,work/'continuation-process.json','runtime-install',budget,True)
        if not any(security.file_record(prefix/'lib'/name)!=value for name,value in before.items()):
            raise RuntimeError('The completed build did not replace the original engine core.')
        outputs=security.runtime_outputs(prefix)
        abi=elf_floor([prefix/row['file'] for row in outputs])
        proof={'schemaVersion':1,'recipeRevision':original['recipeRevision'],'qtVersion':original['qtVersion'],
               'securityLock':security.file_record(security.LOCK),'recipe':security.file_record(security.__file__),
               'storageHelper':security.file_record(Path(__file__).with_name('linux_build_storage.py')),
               'runtimeScope':security.runtime_scope(),
               'compatibilityBackports':original['compatibilityBackports'],'sourceInventories':security.source_inventory_identity(),
               'sourceArchive':security.file_record(source_archive),'baseChromiumCommit':original['baseChromiumCommit'],
               'patchedChromiumCommit':original['patchedChromiumCommit'],'declaredSecurityVersion':original['declaredSecurityVersion'],
               'patches':[{key:row[key] for key in ('commit','sha256','size')} for row in original['patches']],
               'criticalAdvisories':original['criticalAdvisories'],'outputs':outputs,'continuation':record,
               'continuationConfiguration':configuration,'abiFloor':abi}
        target=publish_proof(prefix,proof)
        security.verify_build(prefix)
        (work/'continuation-process.json').write_text(json.dumps({'phase':'complete','proof':security.file_record(target)},indent=2)+'\n',encoding='utf-8')
        print('Completed and verified the existing WebEngine build:',prefix,flush=True)
        return proof


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('work','prefix','source-archive','configured-recipe'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--storage-attestation',type=Path,help='Required on WSL: fresh local operator host-volume attestation.')
    parser.add_argument('--expected-host-drive',help='Required on WSL: explicitly approved Windows backing drive, for example D:.')
    parser.add_argument('--check-only',action='store_true')
    parser.add_argument('--core-jobs',type=int,choices=(2,3),default=2,help='Optional normal-Ninja Core phase at three jobs; retained CMake phases stay at two.')
    args=parser.parse_args()
    if sys.platform!='linux':parser.error('Continuation requires the attested Linux build environment.')
    resume(args.work,args.prefix,args.source_archive,args.configured_recipe,args.storage_attestation,args.expected_host_drive,args.check_only,args.core_jobs)
