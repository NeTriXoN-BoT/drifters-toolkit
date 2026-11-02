"""Build the private Nexus review ZIP from verified, separately built runtimes."""
from pathlib import Path
import argparse
import json
import tempfile
from package import package as package_windows
from package_check import check_archive,Inspection
from release_layout import app_version,read_plain,write_zip
from review_sources import collect,ROOT


def package(windows_build,linux_package,destination=None,plan=False,save_codec_source=None):
    sources=collect()
    config=json.loads(sources['config/nexus.json'])
    if config!={'applicationSlug':''}:raise RuntimeError('The review source must keep the empty public application reference.')
    inspection=Inspection(public=False)
    for name,body in sources.items():inspection.name(name);inspection.consume(len(body),name);inspection.content(name,body)
    if plan:
        print(json.dumps({'reviewSourceFiles':len(sources),'reviewSourceBytes':sum(map(len,sources.values())),
                          'windowsBuild':str(Path(windows_build).name),'linuxPackage':str(Path(linux_package).name),
                          'sourceContainsPrivateHandoff':False,'credentialsIncluded':False},indent=2))
        return None
    linux_package=Path(linux_package)
    if linux_package.name!='Drifters-Toolkit-NexusReview-Linux.zip':raise RuntimeError('Supply the separately built Linux reviewer AppImage ZIP, not the ordinary download.')
    identity=check_archive(linux_package,require_sidecar=True)
    if identity!={'platform':'linux','distribution':'portable','reviewer':True}:
        raise RuntimeError('Supply the universal Linux reviewer AppImage ZIP.')
    with tempfile.TemporaryDirectory(prefix='drifter-nexus-review-package-') as temporary:
        temporary=Path(temporary)
        windows=package_windows(Path(windows_build),temporary,reviewer=True,save_codec_source=save_codec_source)
        check_archive(windows,require_sidecar=True)
        source=write_zip(temporary/'Drifters-Toolkit-Review-Source.zip','DriftersToolkit-review-source',sources,{name for name in sources if name.endswith('.sh')})
        check_archive(source,source=True,require_sidecar=True)
        contents={windows.name:read_plain(windows),linux_package.name:read_plain(linux_package),source.name:read_plain(source)}
        if save_codec_source is not None:
            from save_codec_materials import package_source
            codec_source=package_source(save_codec_source,temporary)
            check_archive(codec_source,source=True,require_sidecar=True)
            contents[codec_source.name]=read_plain(codec_source)
        candidate=write_zip(temporary/'Drifters-Toolkit-Nexus-Review.zip','DriftersToolkit-NexusReview',contents)
        check_archive(candidate,require_sidecar=True)
        target=(Path(destination) if destination is not None else ROOT/'build-artifacts/packages'/app_version()/'nexus-review')/candidate.name
        # Rebuild the final envelope from the same in-memory snapshot only after
        # the complete candidate and every nested archive passed inspection.
        write_zip(target,'DriftersToolkit-NexusReview',contents)
    print('Prepared the Nexus review submission: '+str(target))
    return target


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--windows-build',type=Path,required=True)
    parser.add_argument('--linux-package',type=Path,required=True)
    parser.add_argument('--destination',type=Path)
    parser.add_argument('--save-codec-source',type=Path)
    parser.add_argument('--plan',action='store_true',help='Inspect and list the clean source scope without building a ZIP or running executables.')
    args=parser.parse_args();package(args.windows_build,args.linux_package,args.destination,args.plan,args.save_codec_source)
