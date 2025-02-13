// tcp client to send .torrent path from taiga to manager
// we disguise as transmission-remote
// g++ ../src/torrentAdder.cpp --static -o transmission-remote.exe -lws2_32

#include<string>
#include<boost/asio.hpp>
#include<windows.h>
using boost::asio::ip::tcp;

int WINAPI WinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR lpCmdLine,
    int nCmdShow
    ) {

    try {
    std::string filePath_ = std::string(lpCmdLine);
    std::string filePath = filePath_.substr(1, filePath_.size()-2); // the path is surrounded by " "
    boost::asio::io_context m_context;
    tcp::socket m_socket(m_context);
    m_socket.connect(
        tcp::endpoint(boost::asio::ip::address::from_string("127.0.0.1"),
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
