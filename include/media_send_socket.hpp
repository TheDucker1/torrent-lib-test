#ifndef MEDIA_SOCKET_H_
#define MEDIA_SOCKET_H_

#include<libtorrent/libtorrent.hpp>
bool write_data_to_activation_socket(lt::string_view const& data_name, unsigned char * data, std::size_t data_size);

#endif //MEDIA_SOCKET_H_
