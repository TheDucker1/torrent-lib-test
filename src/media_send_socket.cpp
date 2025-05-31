#include"media_send_socket.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <arpa/inet.h>

auto socket_path_ = std::string(std::getenv("XDG_RUNTIME_DIR")) + "/torrent_thumbnail.socket";
const char * socket_path = socket_path_.c_str();

bool write_data_to_activation_socket(lt::string_view const& data_name, unsigned char * data, std::size_t data_size) {
    if (data_size > UINT32_MAX) {
        std::cerr << "[ERROR]: Max size exceed, max: " << UINT32_MAX << "bytes, got: " << data_size << " bytes\n";
        return false;
    }

    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) { 
        std::cerr << "[ERROR]: Cannot create socket\n";
        return false;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == -1) {
        std::cerr << "[ERROR]: Cannot connect to socket\n";
        close(sock);
        return false;
    }

    uint32_t len_name = htonl(static_cast<uint32_t>(data_name.size()));
    if (send(sock, &len_name, sizeof(len_name), 0) != sizeof(len_name)) {
        std::cerr << "[ERROR]: Send crashed\n";
        close(sock);
        return false;
    }
    ssize_t total_sent = 0;
    while (total_sent < static_cast<ssize_t>(data_name.size())) {
        ssize_t sent = send(sock, data_name.data() + total_sent, data_name.size() - total_sent, 0);
        if (sent < 0) {
            std::cerr << "[ERROR]: Send crashed\n";
            close(sock);
            return false;
        }
        total_sent += sent;
    }

    uint32_t len_net = htonl(static_cast<uint32_t>(data_size));
    if (send(sock, &len_net, sizeof(len_net), 0) != sizeof(len_net)) {
        std::cerr << "[ERROR]: Send crashed\n";
        close(sock);
        return false;
    }

    total_sent = 0;
    while (total_sent < static_cast<ssize_t>(data_size)) {
        ssize_t sent = send(sock, data + total_sent, data_size - total_sent, 0);
        if (sent < 0) {
            std::cerr << "[ERROR]: Send crashed\n";
            close(sock);
            return false;
        }
        total_sent += sent;
    }
    close(sock);
    return true;
}


