"""Probe this app's own hidden WebView; no desktop input or live account access."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys
import tempfile

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
        process=subprocess.run([str(binary),'--interface-test-only',str(output)],timeout=55,
                               creationflags=subprocess.CREATE_NO_WINDOW)
        if not output.is_file():
            raise RuntimeError('The native interface probe produced no result; exit '+str(process.returncode))
        result=json.loads(output.read_text(encoding='utf-8'))
        result['testExecutableSha256']=hashlib.sha256(binary.read_bytes()).hexdigest()
        result['testExitCode']=process.returncode
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
