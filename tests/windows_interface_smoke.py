"""Probe this app's own hidden WebView; no desktop input or live account access."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
import ctypes
from ctypes import wintypes
import shutil
import time


def browser_children(root_pid, handles):
    class Entry(ctypes.Structure):
        _fields_ = [("size", wintypes.DWORD), ("usage", wintypes.DWORD), ("pid", wintypes.DWORD),
                    ("heap", ctypes.c_size_t), ("module", wintypes.DWORD), ("threads", wintypes.DWORD),
                    ("parent", wintypes.DWORD), ("priority", wintypes.LONG), ("flags", wintypes.DWORD),
                    ("exe", wintypes.WCHAR * 260)]
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.Process32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(Entry)]
    kernel.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(Entry)]
    snapshot = kernel.CreateToolhelp32Snapshot(2, 0)
    if snapshot == ctypes.c_void_p(-1).value: return
    rows = []; entry = Entry(); entry.size = ctypes.sizeof(entry)
    more = kernel.Process32FirstW(snapshot, ctypes.byref(entry))
    while more:
        rows.append((entry.pid, entry.parent, entry.exe))
        more = kernel.Process32NextW(snapshot, ctypes.byref(entry))
    kernel.CloseHandle(snapshot)
    owned = {root_pid, *handles}
    while True:
        found = {pid for pid, parent, exe in rows if parent in owned and pid not in owned
                 and exe.lower() == "msedgewebview2.exe"}
        if not found: break
        owned.update(found)
        for pid in found:
            handle = kernel.OpenProcess(0x100000, False, pid)
            if handle: handles[pid] = handle


def main():
    if sys.platform != 'win32':
        raise RuntimeError('The WebView2 native probe requires Windows.')
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary',type=Path)
    parser.add_argument('--report',type=Path,help='Retain the native result outside the disposable probe folder.')
    arguments=parser.parse_args()
    binary=arguments.binary.resolve()
    if not binary.is_file():
        raise RuntimeError('Supply the separately compiled -InterfaceSmoke executable.')
    with tempfile.TemporaryDirectory(prefix='DrifterInterfaceTest-') as temporary:
        output=Path(temporary)/'interface.json'
        # Exercise a disposable, complete runtime so folder locks are observable.
        sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
        from release_layout import WINDOWS_RUNTIME, SAVE_CODEC_WINDOWS_FILES
        runtime = Path(temporary) / 'runtime'
        runtime.mkdir()
        shutil.copyfile(binary, runtime / binary.name)
        for name in (*WINDOWS_RUNTIME, *SAVE_CODEC_WINDOWS_FILES):
            source = binary.parent / name
            if source == binary or not source.is_file(): continue
            target = runtime / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
        handles = {}
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        process = subprocess.Popen([str(runtime / binary.name),'--interface-test-only',str(output)],
                                   creationflags=subprocess.CREATE_NO_WINDOW)
        deadline = time.monotonic() + 55
        try:
            while process.poll() is None and time.monotonic() < deadline:
                browser_children(process.pid, handles)
                time.sleep(.1)
            process.wait(timeout=3)
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
            kernel.CloseHandle.argtypes = [wintypes.HANDLE]
            alive = [pid for pid, handle in handles.items() if kernel.WaitForSingleObject(handle, 3000) != 0]
            assert handles and not alive, 'Owned browser helpers survived application exit: ' + str(alive)
            moved = runtime.with_name('runtime-moved')
            runtime.rename(moved)
            shutil.rmtree(moved)
        finally:
            if process.poll() is None: process.kill(); process.wait(timeout=5)
            for handle in handles.values(): kernel.CloseHandle(handle)
        if not output.is_file():
            raise RuntimeError('The native interface probe produced no result; exit '+str(process.returncode))
        result=json.loads(output.read_text(encoding='utf-8'))
        result['testExecutableSha256']=hashlib.sha256(binary.read_bytes()).hexdigest()
        result['testExitCode']=process.returncode
        result['shutdown']={'observedBrowserHelpers':len(handles),'allOwnedHelpersExited':True,'runtimeMovedAndDeleted':True}
        if arguments.report:
            report=arguments.report.resolve()
            report.parent.mkdir(parents=True,exist_ok=True)
            report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        assert process.returncode==0 and result['ready'],result
        assert result['windowWasVisible'] is False,'The probe must never open a visible desktop window.'
        assert result['navigationCompleted'] is True and result['mainCharacters']>80,result
        # Ten main pages, including Workshop and Saves, plus Settings.
        assert result['navigationItems']==11 and not result['errorVisible'],result
        scale=result['scaling']
        assert scale['source']=='native monitor' and scale['automatic'] is True,result
        metrics=result['nativeMetrics']
        assert metrics['monitorPixelWidth']>0 and metrics['monitorPixelHeight']>0,result
        expected=max(100,min(200,int(min(metrics['monitorPixelWidth'],metrics['monitorPixelHeight'])/
                                     1080/metrics['devicePixelRatio']*20+0.5)*5))
        assert scale['interfaceScale']==min(expected,scale['windowLimit']),result
        assert result['seasonal']['artwork'] == 'local-svg-sprite',result
        assert result['seasonal']['glyphs'] == 18,result
        assert result['bridgeDuplicateRejected'] is True,result
        assert result['profileWorkflow']['passed'] is True and not result['profileWorkflow']['running'],result
        assert result['profileWorkflow']['checks'] and all(result['profileWorkflow']['checks'].values()),result
        assert result['profileFiles'] == {'ready': True, 'manifests': 2, 'owned': 2, 'empty': 2},result
        assert result['bridgeChecks']['alive'] is True and result['bridgeChecks']['rejectedReplies']==0 and result['bridgeChecks']['internalRefused'] is True,result
        expected_policy = 'local-edition-denial' if result['localEdition'] else 'native-key-validation'
        assert result['keyCheck'] == expected_policy, result
        assert result['bridgeChecks']['keyRouted'] is True and result['bridgeChecks']['keyEchoed'] is False,'The native edition policy must refuse the synthetic key offline and never echo it.'
        assert result['seasonal']['paused'] is True,'A hidden window must pause seasonal motion.'
        assert abs(scale['textScale']-scale['interfaceScale']*scale['fontPreference'])<.01,result
        print(json.dumps(result,indent=2))
        print('Native Windows hidden WebView startup, content, monitor Auto, bundled emoji, bounded bridge and actual profile creation/rename checks passed.')

if __name__=='__main__':main()
