#include"media_torrent_manager.hpp"
#include"ram_disk_io.hpp"
#include<iostream>
#include<atomic>
#include<csignal>
#include<string>
#include<thread>
#include<chrono>
#include<mutex>
#include<cstdlib>
#include<filesystem>

#include<fcntl.h>
#include<unistd.h>
#include<poll.h>
#include<sys/file.h>
#include<sys/socket.h>
#include<sys/stat.h>
#include<sys/un.h>
#include<sys/types.h>
#include<pwd.h>
#include<limits.h>

namespace fs = std::filesystem;
static int lock_fd = -1;
static int server_fd = -1;
constexpr auto TIMEOUT = std::chrono::minutes(30);
fs::path lock_path;
fs::path sock_path;

std::atomic<bool> shouldStop{false};
void cleanup() {
    if (server_fd != -1) {
        close(server_fd);
        fs::remove(sock_path);
        server_fd = -1;
    }
    if (lock_fd != -1) {
        close(lock_fd);
        fs::remove(lock_path);
        lock_fd = -1;
    }
}

void sighandler(int){
    cleanup();
    shouldStop=true;
}

bool validate_client(int client_fd) {
    struct ucred cred{};
    socklen_t len = sizeof(cred);
    if (getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == -1) {
        perror("getsockopt");
        return false;
    }
    return cred.uid == getuid();
}

void run(std::string fp="") {
    // Acquire file lock
    lock_fd = open(lock_path.c_str(), O_CREAT|O_RDWR, 0600);
    if (lock_fd < 0) { perror("open"); exit(EXIT_FAILURE); }
    if (flock(lock_fd, LOCK_EX | LOCK_NB) < 0) {
        if (fp.size() == 0) return;
        int client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (client_fd < 0) { perror("socket"); exit( EXIT_FAILURE ); }
        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path)-1);
        if (connect(client_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("connect"); close(client_fd); exit( EXIT_FAILURE );
        }
        write(client_fd, fp.data(), fp.size());
        close(client_fd);
        return;
    }

    struct sockaddr_un addr{};
    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        cleanup();
        exit(EXIT_FAILURE);
    }

    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path) - 1);
    fs::remove(sock_path);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        cleanup();
        exit(EXIT_FAILURE);
    }
    chmod(sock_path.c_str(), 0600);

    if (listen(server_fd, 32) < 0) {
        perror("listen");
        cleanup();
        exit(EXIT_FAILURE);
    }

    std::signal(SIGINT, &sighandler);
    std::signal(SIGTERM, &sighandler);
    std::signal(SIGSTOP, &sighandler);
    std::signal(SIGABRT, &sighandler);

    lt::session_params ses_params;
    ses_params.disk_io_constructor = ram_disk_constructor;
    media_torrent_manager manager(ses_params);

    pollfd pfd[1];
    pfd[0].fd = server_fd;
    pfd[0].events = POLLIN;

    for (;;) {
        int ret = poll(pfd, 1, 200);
        if (ret < 0) {
            if (errno == EINTR) /* interrupt */;
            else std::cerr << "SERIOUS ERROR (poll)\n";
            break;
        }   
        else if (ret) {
            int client_fd = accept(server_fd, NULL, NULL);
            if (client_fd < 0) {
                std::cerr << "SERIOUS ERROR (accept)\n";
                break;
            }
            std::thread([&manager, client_fd]() {
                if (!validate_client(client_fd)) {
                    std::cerr << "Rejected unauthorized client" << std::endl;
                    close(client_fd);
                    return;
                }
                char buf[1024];
                int total_read = 0, n = 0;
                while (n = read(client_fd, buf+total_read, sizeof(buf) - total_read)) {
                    if (n < 0) {
                        std::cerr << "SERIOUS ERROR IN CHILD\n";
                        close(client_fd);
                        return;
                    }
                    else if (n == 0) {
                        buf[total_read] = '\0';
                        break;
                    }
                    total_read += n;
                }
                manager.add_torrent_download(std::string(buf));
                close(client_fd);
            }).detach();
        }
        manager.handle_loop();

        auto tm = std::chrono::steady_clock::now();
        if (tm - manager.get_last_upd() >= TIMEOUT) {
            std::cerr << "INACTIVE, EXIT...\n";
            break;
        }
    }
    cleanup();
}

int main(int argc, char* argv[]) {
    char exe_real[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_real, sizeof(exe_real)-1);
    std::string appname = (len > 0
        ? fs::path(std::string(exe_real, len)).filename().stem().string()
        : fs::path(argv[0]).filename().stem().string());

    fs::path base = fs::path("/run/user") / std::to_string(getuid());
    fs::create_directories(base);

    lock_path = base / (appname + ".lock");
    sock_path = base / (appname + ".sock");

    if (argc == 1) { run(); }
    else if (argc == 2) { run(std::string(argv[1])); }
    else {
        std::cerr << argv[0] << " [file_name]" << '\n';
        return 1;
    }

	return 0;
}
