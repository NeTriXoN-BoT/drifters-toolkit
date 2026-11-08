"""HTTPS-only pinned build downloads; no credential headers or executable launch."""
from pathlib import Path
import hashlib
import os
import re
import tempfile
import urllib.parse
import urllib.request
from release_layout import read_plain

HOSTS={'github.com','raw.githubusercontent.com','release-assets.githubusercontent.com','www.apache.org',
       'objects.githubusercontent.com','www.nuget.org','api.nuget.org','globalcdn.nuget.org','deb.debian.org'}
MAX_DOWNLOAD=512*1024*1024


def require_https(url):
    try:
        target=urllib.parse.urlsplit(url)
        valid=(target.scheme=='https' and target.hostname in HOSTS and target.port in (None,443)
               and '@' not in target.netloc and not target.fragment
               and not any(ord(char)<33 or ord(char)>126 for char in url))
    except ValueError:
        valid=False
    if not valid:raise RuntimeError('Unexpected build-dependency URL.')
    return url


class Redirects(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,request,fp,code,message,headers,newurl):
        require_https(newurl)
        return super().redirect_request(request,fp,code,message,headers,newurl)


def fetch_pinned(url,target,expected,size=None,opener=None):
    require_https(url)
    if not isinstance(expected,str) or not re.fullmatch('[a-f0-9]{64}',expected):raise RuntimeError('Invalid build-dependency hash.')
    if size is not None and (type(size) is not int or not 0<size<=MAX_DOWNLOAD):raise RuntimeError('Invalid build-dependency size.')
    target=Path(target).absolute()
    for parent in (target.parent,*target.parent.parents):
        if parent.is_symlink() or parent.exists() and getattr(parent.lstat(),'st_file_attributes',0)&0x400:
            raise RuntimeError('The dependency folder contains a redirected path.')
    target.parent.mkdir(parents=True,exist_ok=True)
    if target.exists() or target.is_symlink():
        data=read_plain(target,size if size is not None else MAX_DOWNLOAD)
        if (size is not None and len(data)!=size) or hashlib.sha256(data).hexdigest()!=expected:
            raise RuntimeError('An existing dependency differs and was preserved: '+target.name)
        return target
    temporary=None
    try:
        with tempfile.NamedTemporaryFile(prefix='drifter-build-',dir=target.parent,delete=False) as output:
            temporary=Path(output.name);count=0;digest=hashlib.sha256()
            request=urllib.request.Request(url,headers={'User-Agent':'DriftersToolkit-Build/0.1'})
            with (opener or urllib.request.build_opener(Redirects())).open(request,timeout=90) as response:
                while chunk:=response.read(65536):
                    count+=len(chunk)
                    if count>(size if size is not None else MAX_DOWNLOAD):raise RuntimeError('The dependency response is too large.')
                    digest.update(chunk);output.write(chunk)
            output.flush();os.fsync(output.fileno())
        if size is not None and count!=size or digest.hexdigest()!=expected:raise RuntimeError('The dependency checksum or size did not match: '+target.name)
        os.link(temporary,target)
        temporary.unlink();temporary=None
    finally:
        if temporary is not None:temporary.unlink(missing_ok=True)
    return target
