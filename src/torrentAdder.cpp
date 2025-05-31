#include<string>
#include<utility>
#include<boost/asio.hpp>
using boost::asio::ip::tcp;

int main(int argc, const char* argv[]) {
    if (argc != 2) return 1;

    try {
    std::string filepath = std::string(argv[1]);
    boost::asio::io_context m_context;
    tcp::socket m_socket(m_context);
    m_socket.connect(
        tcp::endpoint(boost::asio::ip::make_address("127.0.0.1"),
            1205)
    );

    std::uint32_t filepathsize_ = filepath.size();
    std::uint32_t filepathsize = ntohl(filepathsize_);

    std::vector<char> payload(4 + filepathsize);
    std::memcpy(payload.data(), &filepathsize, 4);
    std::memcpy(payload.data()+4, filepath.data(), filepathsize_);

    boost::asio::write(m_socket, boost::asio::buffer(payload.data(), payload.size()));
    } catch (std::exception& e) {
        //ignore
    }


    return 0;

}
