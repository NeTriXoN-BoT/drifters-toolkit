"""Fetch pinned complete Qt/ICU sources and collect their intact licence notices."""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import os
import re
import shutil
import tarfile
import tempfile
import urllib.request
import urllib.parse
from portable_materials import identity, row, verify_lock
from release_layout import canonical_name, read_plain

QT_MODULES=('qtbase','qtdeclarative','qtsvg','qtwayland','qtwebchannel','qtwebsockets','qtimageformats','qtpositioning','qtwebengine')

def source_lock(path=None):
    path=Path(path) if path is not None else Path(__file__).with_name('qt_sources_lock.json')
    data=json.loads(read_plain(path,128*1024))
    if data.get('schemaVersion')!=1 or data.get('qtVersion')!='6.11.3' or not isinstance(data.get('archives'),list):
        raise RuntimeError('The pinned Qt source inventory is invalid.')
    seen=set()
    for entry in data['archives']:
        if not isinstance(entry,dict):raise RuntimeError('Invalid pinned Qt source entry.')
        component=entry.get('component');name=entry.get('name','');canonical_name(name)
        if component not in (*QT_MODULES,'icu') or component in seen or '/' in name:
            raise RuntimeError('A pinned Qt source is missing, duplicated or redirected.')
        seen.add(component)
        algorithm=entry.get('algorithm');digest=entry.get('digest','')
        if algorithm not in ('sha256','sha512') or not re.fullmatch('[a-f0-9]{'+str(64 if algorithm=='sha256' else 128)+'}',digest):
            raise RuntimeError('A pinned Qt source checksum is invalid.')
        if type(entry.get('size')) is not int or not 0<entry['size']<=4*1024*1024*1024:
            raise RuntimeError('A pinned Qt source size is invalid.')
        address=urllib.parse.urlsplit(entry.get('url',''))
        expected_host='github.com' if component=='icu' else 'download.qt.io'
        if address.scheme!='https' or address.hostname!=expected_host or address.username or address.password or address.port is not None or address.query or address.fragment or address.path.rsplit('/',1)[-1]!=name:
            raise RuntimeError('A pinned Qt source URL is invalid.')
        if entry.get('version')!=('73.2' if component=='icu' else data['qtVersion']):
            raise RuntimeError('The pinned Qt source version does not match its SDK.')
    if seen!=set((*QT_MODULES,'icu')):raise RuntimeError('The pinned Qt source inventory is incomplete.')
    return data

class HttpsRedirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, newurl):
        parsed=urllib.parse.urlsplit(newurl)
        if parsed.scheme!='https' or parsed.username or parsed.password:raise RuntimeError('An upstream source redirect was not plain HTTPS.')
        return super().redirect_request(request,fp,code,message,headers,newurl)

def download(url,target,expected,algorithm='sha256',expected_bytes=None):
    target=Path(target).absolute()
    for parent in (target.parent,*target.parent.parents):
        if parent.is_symlink():raise RuntimeError('Source download output is redirected.')
    target.parent.mkdir(parents=True,exist_ok=True)
    if target.exists() or target.is_symlink():
        identity(target)
        with target.open('rb') as source:actual=hashlib.file_digest(source,algorithm).hexdigest()
        if actual!=expected or expected_bytes is not None and target.stat().st_size!=expected_bytes:raise RuntimeError('Existing source bytes differ and were preserved: '+target.name)
        return
    with tempfile.TemporaryDirectory(prefix='drifter-qt-source-') as temporary:
        staged=Path(temporary)/'download.part';value=hashlib.new(algorithm);count=0
        with urllib.request.build_opener(HttpsRedirects()).open(url,timeout=90) as response,staged.open('xb') as output:
            while chunk:=response.read(1024*1024):
                count+=len(chunk)
                if count>4*1024*1024*1024 or expected_bytes is not None and count>expected_bytes:raise RuntimeError('A source archive exceeded its bound.')
                value.update(chunk);output.write(chunk)
            output.flush();os.fsync(output.fileno())
        if value.hexdigest()!=expected or expected_bytes is not None and count!=expected_bytes:raise RuntimeError('The upstream source checksum or size did not match: '+target.name)
        created=False
        try:
            with staged.open('rb') as source,target.open('xb') as output:
                created=True;shutil.copyfileobj(source,output,1024*1024);output.flush();os.fsync(output.fileno())
            if identity(target)!=identity(staged):raise RuntimeError('The verified source publication changed bytes.')
        except BaseException:
            if created:target.unlink(missing_ok=True)
            raise

def collect_notices(archive,component,notices):
    """Read selected regular text members; never extract paths or execute source."""
    records=[];count=0;total=0
    with tarfile.open(archive,'r:*') as source:
        for member in source:
            count+=1
            if count>1000000:raise RuntimeError('The source archive has too many members.')
            if not member.isfile():continue
            name=member.name;parts=PurePosixPath(name).parts
            if name.startswith('/') or '..' in parts:raise RuntimeError('Unsafe upstream source member path.')
            leaf=parts[-1]
            legal=bool(re.search(r'license|copying|notice|copyright|qt_attribution',leaf,re.I))
            third_party=any(part in ('3rdparty','third_party') for part in parts)
            if not legal and not (third_party and re.match(r'readme(?:\.|$)',leaf,re.I)):continue
            if member.size>16*1024*1024:raise RuntimeError('An upstream notice exceeds its bound.')
            body=source.extractfile(member).read(16*1024*1024+1)
            if len(body)!=member.size:raise RuntimeError('An upstream notice changed size.')
            try:body.decode('utf-8')
            except UnicodeError:continue
            total+=len(body)
            if total>128*1024*1024:raise RuntimeError('The upstream notice collection exceeds its bound.')
            safe=re.sub('[^A-Za-z0-9._-]','_',leaf)
            if re.match(r'readme(?:\.|$)',safe,re.I):safe='UPSTREAM-NOTICE.txt'
            relative=component+'/'+hashlib.sha256(name.encode()).hexdigest()[:16]+'-'+safe
            canonical_name(relative);target=Path(notices)/relative;target.parent.mkdir(parents=True,exist_ok=True)
            with target.open('xb') as output:output.write(body)
            records.append({'file':relative,'sourceMember':name,**identity(target)})
    if not records:raise RuntimeError('No upstream licence notices were found: '+component)
    return records

def fetch(destination,notices):
    pinned=source_lock()
    destination=Path(destination).absolute();notices=Path(notices).absolute()
    if notices.exists():raise RuntimeError('Use a fresh notice collection path.')
    destination.mkdir(parents=True,exist_ok=True);notices.mkdir(parents=True)
    entries=[];notice_rows=[]
    for entry in pinned['archives']:
        module=entry['component'];name=entry['name'];url=entry['url']
        target=destination/name;download(url,target,entry['digest'],entry['algorithm'],entry['size'])
        entries.append(row(destination,name,module,entry['version'],url))
        notice_rows.extend(collect_notices(target,module,notices));print('Verified complete source: '+name,flush=True)
    (notices/'NOTICE-SOURCES.json').write_text(json.dumps({'schemaVersion':1,'notices':notice_rows},indent=2)+'\n')
    lock=destination/'source-materials.json';lock.write_text(json.dumps({'schemaVersion':1,'kind':'qt-corresponding-source','qtVersion':pinned['qtVersion'],'files':entries},indent=2)+'\n')
    verify_lock(lock,(*QT_MODULES,'icu'))
    print('Actual Qt/ICU source material: '+str(lock),flush=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('destination',type=Path);parser.add_argument('notices',type=Path)
    args=parser.parse_args();fetch(args.destination,args.notices)
