// torrentQuickMediaManager.cpp : Defines the entry point for the application.
//


#include"torrentQuickMediaManager.h"
#include"media_torrent_manager.hpp"
#include"ram_disk_io.hpp"
#include<iostream>
#include<atomic>
#include<csignal>

std::atomic<bool> shouldStop{false};

void sighandler(int){
    shouldStop=true;
}

int main(int argc, char* argv[]) try
{
	if (argc < 2) {
		std::cerr << "usage: [program] [list] [of] [torrent] [file] [or] [uri]" << std::endl;
		return 1;
	}
    std::signal(SIGINT, &sighandler);
    lt::session_params ses_params;
    ses_params.disk_io_constructor = ram_disk_constructor;

    media_torrent_manager manager(ses_params);
    for (int i = 1; i < argc; ++i) {
        std::string fn(argv[i]);
        manager.add_torrent_download(fn);
    }

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
