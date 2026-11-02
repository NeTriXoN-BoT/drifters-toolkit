#include "../src/save_preview_process.hpp"
#include "../src/temp_workspace.hpp"
#include "../src/save_picture.hpp"
#include "../src/pak.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <chrono>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using Bytes = std::vector<unsigned char>;
unsigned checks = 0;
void check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
void le(Bytes& out, std::uint64_t n, unsigned count = 4) { while (count--) { out.push_back(n & 255); n >>= 8; } }
void text(Bytes& out, const std::string& value) { out.insert(out.end(), value.begin(), value.end()); }
void field(Bytes& out, const std::string& value) { le(out, value.size()+1);text(out,value);out.push_back(0); }
void be(Bytes& out, std::uint32_t value) { for (int i = 24; i >= 0; i -= 8) out.push_back((value >> i) & 255); }
Bytes image() {
    Bytes out{0x89,'P','N','G',13,10,26,10};
    const auto chunk = [&](const std::string& kind, const Bytes& data) {
        be(out, static_cast<std::uint32_t>(data.size()));
        Bytes crc; text(crc, kind);crc.insert(crc.end(),data.begin(),data.end());
        out.insert(out.end(),crc.begin(),crc.end());
        be(out,drifter::save_picture::crc(reinterpret_cast<const char*>(crc.data()),crc.size()));
    };
    chunk("IHDR",{0,0,0,1,0,0,0,1,8,6,0,0,0});
    chunk("IDAT",{0x78,0x9c,0x63,0x60,0,2,0,0,5,0,1});chunk("IEND",{});
    return out;
}
Bytes sample() {
    Bytes plain; text(plain,"GVAS");field(plain,"PNGData");field(plain,"ArrayProperty");
    const auto png = image();le(plain,png.size()+4);le(plain,0);field(plain,"ByteProperty");le(plain,png.size());
    plain.insert(plain.end(),png.begin(),png.end());
    Bytes out;text(out,"pmOc");le(out,plain.size());le(out,0x9e2a83c1,8);le(out,131072,8);
    le(out,plain.size(),8);le(out,plain.size(),8);le(out,plain.size(),8);le(out,plain.size(),8);
    out.insert(out.end(),plain.begin(),plain.end());return out;
}
int main(int argc, char** argv) {
    try {
        if (argc != 2 && argc != 3) throw std::runtime_error("Supply the private helper binary for this isolated fixture.");
        drifter::TempWorkspace fixture;
#ifdef _WIN32
        const auto helper = fixture.path()/"DriftersToolkitSavePreview.exe";
#else
        const auto helper = fixture.path()/"DriftersToolkitSavePreview";
#endif
        fs::copy_file(fs::u8path(argv[1]),helper);
        const auto hash = drifter::sha256File(helper);
        const auto input = sample();
        const auto good = drifter::save_preview_process::decode(fixture.path(),hash,input);
        if (!good.available) throw std::runtime_error("Sandboxed valid preview failed: "+good.reason);
        check(good.png==image(),"Sandboxed helper returns only the exact fixture PNG.");
        const auto absent = drifter::save_preview_process::decode(fixture.path(),hash,{'G','V','A','S'});
        check(!absent.available && absent.png.empty() && absent.reason.find("no supported")!=std::string::npos,
              "A real no-picture result is distinct from decoder failure.");
        const auto invalid = drifter::save_preview_process::decode(fixture.path(),hash,{'p','m','O','c'});
        check(!invalid.available && invalid.png.empty(),"Malformed input returns no unverified image.");
        check(!drifter::save_preview_process::decode(fixture.path(),std::string(64,'0'),input).available,
              "A helper with the wrong immutable pin is never run.");
        check(!drifter::save_preview_process::decode(fixture.path(),"",input).available,
              "Unconfigured builds do not start a decoder.");
        check(!drifter::save_preview_process::decode(fixture.path(),hash,Bytes(8*1024*1024+1)).available,
              "Oversized input is rejected before helper execution.");
#ifndef _WIN32
        int saved[3];
        for(int i=0;i<3;++i) {saved[i]=fcntl(i,F_DUPFD_CLOEXEC,30);check(saved[i]>=0,"Fixture stdio is available.");}
        for(int i=0;i<3;++i)close(i);
        const auto noStdio=drifter::save_preview_process::decode(fixture.path(),hash,input);
        for(int i=0;i<3;++i){dup2(saved[i],i);close(saved[i]);}
        check(noStdio.available && noStdio.png==image(),"Closed parent stdio cannot collide with the exec source.");
#endif
        fs::remove(helper);
        check(!drifter::save_preview_process::decode(fixture.path(),hash,input).available,
              "Missing helpers fail closed without another executable fallback.");
#ifdef _WIN32
        if (argc==3) {
            fs::copy_file(fs::u8path(argv[2]),helper);
            const auto probeHash=drifter::sha256File(helper);
            const auto outside=fixture.path()/"outside-private-marker.txt";
            {std::ofstream marker(outside);marker<<"dummy fixture content";}
            WSADATA sockets{};check(!WSAStartup(MAKEWORD(2,2),&sockets),"Network fixture initializes.");
            const auto listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
            sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
            check(listener!=INVALID_SOCKET && bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0 &&
                  listen(listener,1)==0,"Network fixture listens on a private ephemeral loopback port.");
            int length=sizeof(address);check(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length)==0,"Network fixture has a port.");
            const auto control=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
            check(control!=INVALID_SOCKET && connect(control,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,
                  "Ordinary parent loopback connectivity works before sandbox test.");
            const auto accepted=accept(listener,nullptr,nullptr);check(accepted!=INVALID_SOCKET,"Control connection is accepted.");
            closesocket(accepted);closesocket(control);
            SetEnvironmentVariableW(L"DRIFTER_PREVIEW_TEST_SECRET",L"dummy-not-a-secret");
            const auto command=std::string("P")+outside.u8string()+"|"+std::to_string(ntohs(address.sin_port));
            const auto confinement=drifter::save_preview_process::decode(fixture.path(),probeHash,Bytes(command.begin(),command.end()));
            SetEnvironmentVariableW(L"DRIFTER_PREVIEW_TEST_SECRET",nullptr);
            fd_set arrivals;FD_ZERO(&arrivals);FD_SET(listener,&arrivals);timeval immediate{};
            const auto connected=select(0,&arrivals,nullptr,nullptr,&immediate);
            closesocket(listener);WSACleanup();
            check(confinement.available && connected==0,"AppContainer denies external file reads, helper-folder writes, TCP, inherited secrets and child processes.");
            check(!drifter::save_preview_process::decode(fixture.path(),probeHash,{'O'}).available,"Excess stdout is bounded and rejected.");
            const auto started=std::chrono::steady_clock::now();
            check(!drifter::save_preview_process::decode(fixture.path(),probeHash,{'H'}).available &&
                  std::chrono::steady_clock::now()-started<std::chrono::seconds(12),"The native wall deadline kills a sleeping helper.");
        }
#endif
        std::cout << "Isolated save preview passed " << checks << " checks.\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n';return 1; }
}
