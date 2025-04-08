// tcp client to send .torrent path from taiga to manager
// g++ ../src/torrentAdder.cpp -mwindows --static -o transmission-remote.exe -lshell32 -lws2_32

#include<boost/asio.hpp>
#include<windows.h>
#include<shellapi.h>
#include<string>

using boost::asio::ip::tcp;

int WINAPI WinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR lpCmdLine,
    int nCmdShow
    ) {

    try {
    LPWSTR *szArgList;
    int nArgs;
    szArgList = CommandLineToArgvW(GetCommandLineW(), &nArgs);
    if (szArgList == 0) return 0;
    if (nArgs < 2) return 0;

    int bufferSize = WideCharToMultiByte(CP_UTF8, 0, szArgList[1], -1, NULL, 0, NULL, NULL);
    if (bufferSize == 0) {
        LocalFree(szArgList);
        return 0;
    }
    std::vector<char> filePath(bufferSize);
    WideCharToMultiByte(CP_UTF8, 0, szArgList[1], -1, filePath.data(), bufferSize, NULL, NULL);
    if (filePath.size() && filePath[filePath.size()-1] == '\0') filePath.resize(filePath.size()-1);

    boost::asio::io_context m_context;
    tcp::socket m_socket(m_context);
    m_socket.connect(
        tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"),
            1205)
    );

    std::uint32_t filePathSize_ = filePath.size();
    char isUpdate = 0;
    std::uint32_t filePathSize = ntohl(filePathSize_);

    std::vector<char> payload(1 + 4 + filePathSize);
    payload[0] = isUpdate;
    std::memcpy(payload.data()+1, &filePathSize, 4);
    std::memcpy(payload.data()+5, filePath.data(), filePathSize_);

    boost::asio::write(m_socket, boost::asio::buffer(payload.data(), payload.size()));
    } catch (std::exception& e) {
        //ignore
    }


    return 0;
}
