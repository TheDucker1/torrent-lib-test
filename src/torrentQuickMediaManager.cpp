// torrentQuickMediaManager.cpp : Defines the entry point for the application.
//


#include"torrentQuickMediaManager.h"
#include"media_torrent_manager.hpp"
#include"ram_disk_io.hpp"
#include<iostream>
#include<atomic>
#include<csignal>

#include<boost/asio.hpp>
using boost::asio::ip::tcp;

std::atomic<bool> shouldStop{false};
void sighandler(int){
    shouldStop=true;
}

int main(int argc, char* argv[]) try
{
    std::signal(SIGINT, &sighandler);
    lt::session_params ses_params;
    ses_params.disk_io_constructor = ram_disk_constructor;

    media_torrent_manager manager(ses_params);

    auto tcp_thread_job = [&]() {
        try {
            boost::asio::io_context m_context;
            tcp::acceptor m_acceptor(m_context, tcp::endpoint(tcp::v4(), 1205));
            for (; !shouldStop;) {
                tcp::socket m_socket(m_context);
                m_acceptor.accept(m_socket); // this block
                char is_upload;
                std::uint32_t path_length;
                boost::asio::read(m_socket, boost::asio::buffer(&is_upload, 1));
                boost::asio::read(m_socket, boost::asio::buffer(&path_length, 4));
                path_length = ntohl(path_length);
                std::cerr << path_length << " LEN\n";
                std::vector<char> path_(path_length, '\0');
                boost::asio::read(m_socket, boost::asio::buffer(path_.data(), path_length));
                int bufferSize = MultiByteToWideChar(CP_UTF8, 0, path_.data(), -1, NULL, 0);
                if (bufferSize == 0) return;
                std::wstring wpath(bufferSize, L'\0');
                int result = MultiByteToWideChar(CP_UTF8, 0, path_.data(), -1, wpath.data(), bufferSize);
                if (result == 0) return;
                if (wpath[wpath.size()-1] == '\0') wpath.resize(wpath.size()-1);
                manager.add_torrent_download(wpath, is_upload);
            }
        } catch (std::exception& e) {
            shouldStop = true;
            std::cerr << "[CANNOT CREATE INPUT THREAD]: " << e.what() << std::endl;
        }
    };
    std::thread thread_input(tcp_thread_job);
    thread_input.detach();

	for (; !shouldStop ;) {
        manager.handle_loop();
		std::this_thread::sleep_for(std::chrono::milliseconds(200));
	}
	
	std::cout << "Done, shutting down..." << std::endl;
	return 0;
}
catch (std::exception const& e) {
	std::cerr << "ERROR: " << e.what() << std::endl;
}
