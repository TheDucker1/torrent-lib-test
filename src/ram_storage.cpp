#include<libtorrent/libtorrent.hpp>
#include"ram_storage.hpp"

ram_storage::~ram_storage() {
	release_files();
}

void ram_storage::clear_piece(
    lt::piece_index_t piece
) {
    std::cerr << "[CLEAR PIECE " << piece << "]\n";
    m_invalid.insert(piece);
}
lt::span<char const> ram_storage::readv(lt::peer_request const r, lt::storage_error& ec) const {
    auto const it2 = m_invalid.find(r.piece);
	auto const it = m_file_data.find(r.piece);
	if (it == m_file_data.end() || it2 != m_invalid.end()) {
		ec.operation = lt::operation_t::file_read;
		ec.ec = boost::asio::error::eof;
		return {};
	}
	if (static_cast<int>(it->second.size()) <= r.start) {
		ec.operation = lt::operation_t::file_read;
		ec.ec = boost::asio::error::eof;
		return {};
	}
	return {
		it->second.data() + r.start,
		std::min(r.length, static_cast<int>(it->second.size()) - r.start)
	};
}

void ram_storage::writev(lt::span<char const> const b,
	lt::piece_index_t const piece, int const offset) {

    has_changed_since = true;
    m_invalid.erase(piece);
	auto& data = m_file_data[piece];
	if (data.empty()) {
		// allocate all to avoid relocate data -> lost readv pointer
		int const size = m_files.piece_size(piece);
		data.resize(std::size_t(size), '\0');
	}
	TORRENT_ASSERT(offset + b.size() <= static_cast<int>(data.size()));
	std::memcpy(data.data() + offset, b.data(), std::size_t(b.size()));
}

lt::sha1_hash ram_storage::hash(lt::piece_index_t const piece,
	lt::span<lt::sha256_hash> const block_hashes, lt::storage_error& ec) const {

	auto const it = m_file_data.find(piece);
	if (it == m_file_data.end()) {
		ec.operation = lt::operation_t::file_read;
		ec.ec = boost::asio::error::eof;
		return {};
	}
	if (!block_hashes.empty()) { // not empty -> cal hash2 into blocks
		int const piece_size2 = m_files.piece_size2(piece);
		int const block_in_piece2 = m_files.blocks_in_piece2(piece);
		char const* buf = it->second.data();
		std::int64_t offset = 0;
		for (int blk = 0; blk < block_in_piece2; ++blk) {
			lt::hasher256 h2;
			std::ptrdiff_t const len2 = std::min(
				lt::default_block_size,
				static_cast<int>(piece_size2 - offset)
			);
			h2.update({ buf, len2 });
			buf += len2;
			offset += len2;
			block_hashes[blk] = h2.final();
		}
	}
	return lt::hasher(it->second).final();
}

lt::sha256_hash ram_storage::hash2(lt::piece_index_t const piece, int const offset,
	lt::storage_error& ec) const {

	auto const it = m_file_data.find(piece);
	if (it == m_file_data.end()) {
		ec.operation = lt::operation_t::file_read;
		ec.ec = boost::asio::error::eof;
		return {};
	}
	int const piece_size = m_files.piece_size2(piece);
	std::ptrdiff_t const len = std::min(
		lt::default_block_size,
		piece_size - offset
	);
	lt::span<char const> b = { it->second.data() + offset, len };
	return lt::hasher256(b).final();
}

lt::status_t ram_storage::initialize() {
    if (has_init_since) {
		return lt::status_t::no_error;
    } 
    has_init_since = true;
	lt::error_code error;
	auto f = open_file(open_mode::read_only, error);
	if (error) {
		// cannot read file -> assume having no data
		return lt::status_t::no_error;
	}

	//parse header
	std::vector<char> _header(header_size());
    f.read(_header.data(), _header.size());
	if (f.gcount() != _header.size()) {
		// cannot read header -> assume invalid file
		return lt::status_t::no_error;
	}

	char* ptr = _header.data();
	std::int32_t num_piece_ = lt::aux::read_int32(ptr);
	std::int32_t piece_size_ = lt::aux::read_int32(ptr);
	if (num_piece_ != m_files.num_pieces() || piece_size_ != m_files.piece_length()) {
		// mismatch numpiece / piecesize -> assume invalid file
		return lt::status_t::no_error;
	}

	std::vector<lt::piece_index_t> used_slots;
	for (lt::piece_index_t i(0); i < lt::piece_index_t(num_piece_); ++i) {
		std::int8_t const used(lt::aux::read_int8(ptr));
		if (used) used_slots.emplace_back(i);
	}

	bool invalid_file = false;
	std::vector<char> piece_data(piece_size_);
	for (lt::piece_index_t const i : used_slots) {
		std::int32_t this_piece_size = m_files.piece_size(i);

        f.read(piece_data.data(), static_cast<std::size_t>(piece_size_));
		if (f.gcount() != static_cast<std::size_t>(piece_size_)) {
			invalid_file = true;
			break;
		}

		auto& data = m_file_data[i];
		data.resize(this_piece_size);
		std::memcpy(data.data(), piece_data.data(), static_cast<std::size_t>(this_piece_size));
	}

	if (invalid_file) {
		m_file_data.clear();
	}
    has_changed_since = false;

	return lt::status_t::no_error;
}

std::pair<lt::status_t, std::string> ram_storage::move_storage(
	std::string const& sp,
	lt::move_flags_t const flags,
	lt::storage_error& ec) {

	lt::status_t ret;
	auto _move_storage = [&](
		lt::file_storage const& f,
		std::string save_path,
		std::string const& destination_save_path,
		lt::move_flags_t flags,
		lt::storage_error& ec
		) -> std::pair<lt::status_t, std::string> {

			std::string const new_save_path = complete_path(destination_save_path);
            
			if (flags == lt::move_flags_t::fail_if_exist) {
                std::error_code err;
                std::filesystem::path const fpth(new_save_path);
                std::filesystem::file_status s = 
                    std::filesystem::status(fpth, err);

				if (err != std::errc::no_such_file_or_directory) {
                    std::filesystem::path const fpth2 = fpth / std::filesystem::path(m_save_name);
                    std::filesystem::file_status s2 = 
                        std::filesystem::status(fpth2, err);
					if (err != std::errc::no_such_file_or_directory) {
						ec.ec = err;
						ec.file(lt::file_index_t(-1));
						ec.operation = lt::operation_t::file_stat;
						return { lt::status_t::file_exist, save_path };

					}
				}
			}

			{
                std::error_code err;
                std::filesystem::path fpth(new_save_path);
                std::filesystem::file_status s =
                    std::filesystem::status(fpth, err);
				if (err == std::errc::no_such_file_or_directory) {
					err.clear();
                    std::filesystem::create_directories(new_save_path, err);
					if (err) {
						ec.ec = err;
						ec.file(lt::file_index_t(-1));
						ec.operation = lt::operation_t::mkdir;
						return { lt::status_t::fatal_disk_error, save_path };
					}
				}
				else if (err) {
					ec.ec = err;
					ec.file(lt::file_index_t(-1));
					ec.operation = lt::operation_t::file_stat;
					return { lt::status_t::fatal_disk_error, save_path };
				}
			}

			return { lt::status_t::no_error, new_save_path };
		};

	std::tie(ret, m_save_path) = _move_storage(m_files,
		m_save_path, sp, flags, ec
	);

	return { ret, m_save_path };
}

void ram_storage::set_file_priority(
	lt::aux::vector<lt::download_priority_t, lt::file_index_t>& prio,
	lt::storage_error& ec
) {
	if (prio.size() > m_file_priority.size())
		m_file_priority.resize(prio.size(), lt::default_priority);
}

void ram_storage::release_files() {
	// flush all data to m_save_path+m_save_name;
    if (!has_changed_since) return; // no need to flush since we didn't download anything
	lt::error_code ec;
	auto f = open_file(open_mode::read_write, ec);
	if (ec) return;

	std::vector<char> header(header_size(), '\0');
	char* ptr = header.data();

	lt::aux::write_int32(m_files.num_pieces(), ptr);
	lt::aux::write_int32(m_files.piece_length(), ptr);
	lt::aux::vector<std::int8_t, lt::piece_index_t> used_vector;
	used_vector.resize(m_files.num_pieces(), std::int8_t(0));
	for (auto const& i : m_file_data) {
		used_vector[i.first] = std::int8_t(1);
	}
	for (lt::piece_index_t i(0); i < m_files.end_piece(); ++i) {
		lt::aux::write_int8(used_vector[i], ptr);
	}

    // assume write success
    f.write(header.data(), header.size());

	std::vector<char> p_data(m_files.piece_length());
	for (auto const& i : m_file_data) {
		std::int32_t this_piece_size = m_files.piece_size(i.first);
		std::memcpy(p_data.data(), i.second.data(), static_cast<std::size_t>(this_piece_size));
        f.write(p_data.data(), p_data.size());
	}

    has_changed_since = false; // reset file status
}
