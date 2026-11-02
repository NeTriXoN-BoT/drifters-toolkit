"""Actual Qt monitor metadata and event checks on isolated Xvfb displays."""
from pathlib import Path
import json
import os
import select
import shlex
import signal
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
PROBE=r'''
#include "linux_display_metrics.hpp"
#include <QEventLoop>
#include <iostream>
#include <stdexcept>
using drifter::json;
int main(int argc,char** argv) {
 try {
  const int pixelWidth=std::stoi(argv[1]),pixelHeight=std::stoi(argv[2]);
  const double scale=std::stod(argv[3]); const bool move=argc>4;
  QApplication app(argc,argv); QWidget window; window.resize(1440,955);
  unsigned count=0,events=0;bool guiThread=true;json last;
  auto check=[&](bool value,const char* message){++count;if(!value)throw std::runtime_error(message);};
  drifter::LinuxDisplayTracker tracker(&window,[&](const json& data){++events;last=data;guiThread=guiThread&&QThread::currentThread()==app.thread();});
  auto settle=[] {QEventLoop loop;QTimer::singleShot(180,&loop,&QEventLoop::quit);loop.exec();};
  window.show();settle(); auto first=tracker.snapshot();
  check(first.at("source")=="qt-qscreen","native Qt source missing");
  check(first.at("available")==true,"actual screen missing");
  check(first.at("monitorPixelSizeDerived")==true,"derived pixels must be identified");
  check(std::abs(first.at("monitorPixelWidth").get<int>()-pixelWidth)<=2,"unexpected native monitor width");
  check(std::abs(first.at("monitorPixelHeight").get<int>()-pixelHeight)<=2,"unexpected native monitor height");
  check(std::abs(first.at("devicePixelRatio").get<double>()-scale)<.02,"window DPR differs from configured Qt scale");
  check(std::abs(first.at("monitorLogicalWidth").get<int>()-std::round(pixelWidth/scale))<=2,"logical monitor width applied scale twice");
  check(first.at("workLogicalWidth").get<int>()<=first.at("monitorLogicalWidth").get<int>(),"work area exceeds monitor");
  check(first.at("logicalDpiX").get<double>()>0&&first.at("logicalDpiY").get<double>()>0,"native logical DPI missing");
  check(first.at("windowLogicalWidth")==1440&&first.at("windowLogicalHeight")==955,"client geometry missing");
  check(first.at("windowHidden")==false,"shown window incorrectly hidden");
  check(events>0&&guiThread,"first native event missing or on worker thread");
  auto before=events;window.resize(928,680);settle();
  check(events>before&&last.at("windowLogicalWidth")==928&&last.at("windowLogicalHeight")==680,"resize did not update observer");
  before=events;settle();check(events==before,"idle observer should not poll or republish unchanged metrics");
  window.hide();settle();check(last.at("windowHidden")==true,"hide visibility signal missing");
  window.show();settle();check(last.at("windowHidden")==false,"show visibility signal missing");
  window.showMinimized();settle();check(last.at("windowHidden")==true,"minimize visibility signal missing");
  window.showNormal();settle();check(last.at("windowHidden")==false,"restore visibility signal missing");
  json fourK={{"devicePixelRatio",1.0},{"monitorPixelWidth",3840},{"monitorPixelHeight",2160},{"workLogicalWidth",3840},{"workLogicalHeight",2160}};
  check(drifter::linuxInitialWindowSize(fourK)==QSize(2880,1910),"4K unscaled startup should compensate");
  fourK["devicePixelRatio"]=2.0;fourK["workLogicalWidth"]=1920;fourK["workLogicalHeight"]=1080;
  check(drifter::linuxInitialWindowSize(fourK)==QSize(1440,955),"4K OS scaling must not apply twice");
  fourK["devicePixelRatio"]=1.5;fourK["workLogicalWidth"]=2560;fourK["workLogicalHeight"]=1440;
  check(drifter::linuxInitialWindowSize(fourK)==QSize(1944,1289),"fractional DPR startup target differs");
  check(drifter::linuxInitialWindowSize(fourK,{},false)==QSize(1440,955),"manual mode must preserve baseline size");
  fourK["workLogicalWidth"]=1000;fourK["workLogicalHeight"]=700;
  check(drifter::linuxInitialWindowSize(fourK,{20,40})==QSize(980,660),"initial outer frame must fit actual work area");
  if(move) {
   check(app.screens().size()>=2,"two actual native screens required");
   auto* second=app.screens().at(1);before=events;
   window.windowHandle()->setScreen(second);window.show();settle();auto selected=tracker.snapshot();
   check(window.windowHandle()->screen()==second,"native window screen selection failed");
   check(selected.at("monitorPixelWidth")==3840&&selected.at("monitorPixelHeight")==2160,"new monitor resolution was not selected");
   check(events>before&&last.at("monitorPixelWidth")==3840,"native screenChanged event did not refresh metrics");
  }
  std::cout<<json{{"checks",count},{"events",events},{"metrics",first},{"screens",app.screens().size()},{"moveTest",move}}.dump()<<std::endl;
  return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<std::endl;return 7;}
}
'''

def main():
    if sys.platform!='linux':raise RuntimeError('Run the actual Qt monitor checks on Linux.')
    total=0
    with tempfile.TemporaryDirectory(prefix='drifter-display-tests-') as temporary:
        root=Path(temporary);source=root/'probe.cpp';source.write_text(PROBE);probe=root/'probe'
        flags=shlex.split(subprocess.run(['pkg-config','--cflags','--libs','Qt6Widgets'],check=True,capture_output=True,text=True).stdout)
        if os.environ.get('DRIFTER_DISPLAY_ASAN')=='1':flags+=['-g','-fsanitize=address','-fno-omit-frame-pointer']
        subprocess.run(['g++','-std=c++17','-O1','-fPIC','-pthread','-I'+str(ROOT/'src'),str(source),'-o',str(probe),*flags],check=True)
        for width,height,scale,multiple in [(1920,1080,1,False),(3840,2160,1,False),(3840,2160,1.5,False),(3840,2160,2,False),(1920,1080,1,True)]:
            read,write=os.pipe();server=None
            try:
                args=['Xvfb','-displayfd',str(write),'-screen','0',f'{width}x{height}x24','-nolisten','tcp']
                if multiple:args+=['-screen','1','3840x2160x24','-extension','XINERAMA']
                server=subprocess.Popen(args,pass_fds=(write,),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,start_new_session=True)
                os.close(write);write=-1
                if not select.select([read],[],[],10)[0]:raise RuntimeError('The isolated X server did not start.')
                number=os.read(read,64).strip().decode('ascii');assert number.isdigit()
                runtime=root/('runtime-'+str(total));runtime.mkdir(mode=0o700)
                env={key:value for key,value in os.environ.items() if not key.startswith(('QT_','QTWEBENGINE_'))}
                env.update(DISPLAY=':'+number,QT_QPA_PLATFORM='xcb',QT_SCALE_FACTOR=str(scale),XDG_RUNTIME_DIR=str(runtime))
                result=subprocess.run([str(probe),str(width),str(height),str(scale)]+(['move'] if multiple else []),env=env,capture_output=True,text=True,timeout=15)
                if result.returncode:
                    raise RuntimeError('Native display probe returned '+str(result.returncode)+'\n'+result.stdout+result.stderr)
                data=json.loads(result.stdout);total+=data['checks'];print(json.dumps(data),flush=True)
            finally:
                os.close(read)
                if write>=0:os.close(write)
                if server and server.poll() is None:os.killpg(server.pid,signal.SIGKILL);server.wait(timeout=5)
    print(str(total)+' actual Qt display metadata/startup/signal checks passed; all displays and files isolated.')

if __name__=='__main__':main()
