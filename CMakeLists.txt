# CMakeList.txt : CMake project for torrentQuickMediaManager, include source and define
# project specific logic here.
#
cmake_minimum_required (VERSION 3.17)
set(CMAKE_CXX_STANDARD 20)  

project ("torrentQuickMediaManager" LANGUAGES C CXX)
project ("customStorage" LANGUAGES C CXX)

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
pkg_check_modules( LIBWEBP REQUIRED IMPORTED_TARGET
    libwebp
    libwebpmux
)

# Add source to this project's executable.
add_executable (torrentQuickMediaManager "src/torrentQuickMediaManager.cpp" 
    "src/ram_storage.cpp" 
    "src/ram_disk_io.cpp" 
    "src/media_base.cpp" 
    "src/media_test.cpp" 
    "src/media_mkv.cpp" 
    "src/media_torrent_manager.cpp"
)
add_executable (customStorage "src/custom_storage.cpp")

set_property(TARGET torrentQuickMediaManager PROPERTY CXX_STANDARD 20)
set_property(TARGET customStorage PROPERTY CXX_STANDARD 20)

target_include_directories(torrentQuickMediaManager PRIVATE "include")
target_include_directories(torrentQuickMediaManager PRIVATE "lodepng")

target_link_libraries(torrentQuickMediaManager PRIVATE PkgConfig::LIBWEBP)
target_link_libraries(torrentQuickMediaManager PRIVATE LibtorrentRasterbar::torrent-rasterbar)
target_link_libraries(torrentQuickMediaManager PRIVATE PkgConfig::LIBAV)
target_link_libraries(torrentQuickMediaManager PRIVATE PkgConfig::LIBMATROSKA)

target_link_libraries(customStorage PRIVATE LibtorrentRasterbar::torrent-rasterbar)


# TODO: Add tests and install targets if needed.
