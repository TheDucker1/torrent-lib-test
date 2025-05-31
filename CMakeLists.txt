# CMakeList.txt : CMake project for torrentThumbnailGenerator, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.17)
set(CMAKE_CXX_STANDARD 20)  

project ("torrentThumbnailGenerator" LANGUAGES C CXX)

find_package(PkgConfig REQUIRED)
find_package(LibtorrentRasterbar CONFIG REQUIRED)

pkg_check_modules( LIBAV REQUIRED IMPORTED_TARGET
    libavcodec
    libavformat
    libavutil
    libswscale
)
pkg_check_modules( LIBMATROSKA REQUIRED IMPORTED_TARGET
    libebml 
    libmatroska
)

# Add source to this project's executable.
add_executable (torrentThumbnailGenerator "src/torrentQuickMediaManager.cpp" 
    "src/ram_storage.cpp" 
    "src/ram_disk_io.cpp" 
    "src/media_base.cpp" 
    "src/media_mkv.cpp" 
    "src/media_send_socket.cpp"
    "src/media_torrent_manager.cpp"
)

set_property(TARGET torrentThumbnailGenerator PROPERTY CXX_STANDARD 20)

target_include_directories(torrentThumbnailGenerator PRIVATE "include")
target_include_directories(torrentThumbnailGenerator PRIVATE "lodepng")

target_link_libraries(torrentThumbnailGenerator PRIVATE LibtorrentRasterbar::torrent-rasterbar)
target_link_libraries(torrentThumbnailGenerator PRIVATE PkgConfig::LIBAV)
target_link_libraries(torrentThumbnailGenerator PRIVATE PkgConfig::LIBMATROSKA)

# TODO: Add tests and install targets if needed.
