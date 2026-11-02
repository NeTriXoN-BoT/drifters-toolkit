// Private malicious-worker fixture. Never ship or call from the application.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include "../src/save_picture.hpp"
#include <array>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#include <fcntl.h>
#include <io.h>
int main() {
    _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
    const std::string input{std::istreambuf_iterator<char>(std::cin), {}};
    if (input == "H") { Sleep(20000);return 2; }
    if (input == "O") { std::string bytes(5*1024*1024,'x');std::cout.write(bytes.data(),bytes.size());return 0; }
    if (input.size()<2 || input[0]!='P') return 2;
    const auto separator=input.rfind('|');if(separator==std::string::npos)return 2;
    const auto port=std::stoi(input.substr(separator+1));
    const auto text = input.substr(1,separator-1);
    const auto count = MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),text.size(),nullptr,0);
    if (!count) return 2;
    std::wstring path(count,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),text.size(),path.data(),count);
    HANDLE file = CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    if (file!=INVALID_HANDLE_VALUE) { CloseHandle(file);return 2; }
    if (GetLastError()!=ERROR_ACCESS_DENIED) return 2;
    file=CreateFileW(L"must-not-be-written",GENERIC_WRITE,0,nullptr,CREATE_NEW,0,nullptr);
    if (file!=INVALID_HANDLE_VALUE) { CloseHandle(file);return 2; }
    if (GetLastError()!=ERROR_ACCESS_DENIED) return 2;
    wchar_t environment[128];
    if (GetEnvironmentVariableW(L"DRIFTER_PREVIEW_TEST_SECRET",environment,128)) return 2;
    WSADATA sockets{};if(WSAStartup(MAKEWORD(2,2),&sockets))return 2;
    const auto connection=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if (connection==INVALID_SOCKET) { if (WSAGetLastError()!=WSAEACCES)return 2; }
    else {
        u_long nonblocking=1;if(ioctlsocket(connection,FIONBIO,&nonblocking))return 2;
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        const auto result=connect(connection,reinterpret_cast<sockaddr*>(&address),sizeof(address));
        const auto error=WSAGetLastError();
        if(result!=SOCKET_ERROR)return 2;
        if(error!=WSAEACCES) {
            if(error!=WSAEWOULDBLOCK)return 2;
            fd_set ready,failed;FD_ZERO(&ready);FD_ZERO(&failed);FD_SET(connection,&ready);FD_SET(connection,&failed);
            timeval wait{0,750000};const auto observed=select(0,nullptr,&ready,&failed,&wait);
            if(observed==SOCKET_ERROR)return 2;
            if(observed>0) {int reason=0;int length=sizeof(reason);if(getsockopt(connection,SOL_SOCKET,SO_ERROR,reinterpret_cast<char*>(&reason),&length)||!reason)return 2;}
        }
        closesocket(connection);
    }
    WSACleanup();
    std::array<wchar_t,32768> image{};
    if (!GetModuleFileNameW(nullptr,image.data(),image.size()))return 2;
    std::wstring command=L"\""+std::wstring(image.data())+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(CreateProcessW(image.data(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,
                      nullptr,nullptr,&startup,&process)) {
        TerminateProcess(process.hProcess,2);CloseHandle(process.hThread);CloseHandle(process.hProcess);return 2;
    }
    std::string png="\x89PNG\r\n\x1a\n";
    const auto be=[](std::string& output,std::uint32_t n){for(int i=24;i>=0;i-=8)output+=char((n>>i)&255);};
    const auto chunk=[&](const std::string& kind,const std::string& data){
        be(png,data.size());auto bytes=kind+data;png+=bytes;
        be(png,drifter::save_picture::crc(bytes.data(),bytes.size()));
    };
    chunk("IHDR",std::string("\0\0\0\1\0\0\0\1\10\6\0\0\0",13));
    chunk("IDAT",std::string("\x78\x9c\x63\x60\0\2\0\0\5\0\1",11));chunk("IEND",{});
    std::cout.write(png.data(),png.size());return 0;
}
