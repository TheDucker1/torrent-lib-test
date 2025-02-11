#include<libtorrent/libtorrent.hpp>
#include<filesystem>
#include<fstream>

#ifndef RAM_STORAGE_H_
#define RAM_STORAGE_H_

struct ram_storage {
	explicit ram_storage(lt::storage_params const& p) :
        has_changed_since(false),
		m_files(p.files),
		m_save_path(p.path),
		m_save_name("." + lt::aux::to_hex(p.info_hash) + ".pieceset"),
		m_file_priority(p.priorities)
	{
	}

	~ram_storage();

	lt::span<char const> readv(lt::peer_request const r, lt::storage_error& ec) const;

	void writev(lt::span<char const> const b,
		lt::piece_index_t const piece, int const offset);

	lt::sha1_hash hash(lt::piece_index_t const piece,
		lt::span<lt::sha256_hash> const block_hashes, lt::storage_error& ec) const;

	lt::sha256_hash hash2(lt::piece_index_t const piece, int const offset,
		lt::storage_error& ec) const;

	std::pair<lt::status_t, std::string> move_storage(
		std::string const& sp,
		lt::move_flags_t const flags,
		lt::storage_error& ec);

	void set_file_priority(
		lt::aux::vector<lt::download_priority_t, lt::file_index_t>& prio,
		lt::storage_error& ec
	);
    void clear_piece(
        lt::piece_index_t piece
    );

	void release_files();

private:
	enum class open_mode : std::uint8_t
	{
		read_only, read_write
	};
	std::fstream open_file(
		enum open_mode const mode,
		lt::error_code& ec) const {

		std::filesystem::path fpth = std::filesystem::path(m_save_path) / 
            std::filesystem::path(m_save_name);

        std::ios::openmode mode_ = std::ios::binary | std::ios::in;
        if (mode == open_mode::read_write) {
            mode_ |= std::ios::out;
        }

        std::fstream ret(fpth, mode_);
        if (ret.fail()) {
            // create and close the file if not exists
            std::fstream f(fpth, std::ios::out);
            f.close();
            ret = std::fstream(fpth, mode_);
            if (ret.fail()) {
                ec.assign(errno, boost::system::generic_category());
            }
        }

		return ret;
	}
    std::string complete_path(std::string maybe_relative_path) const {
        std::filesystem::path cur_path(maybe_relative_path);
        return std::filesystem::absolute(cur_path).string();
    }

    std::size_t header_size() const {
        std::size_t _header_size = 0;
		_header_size += sizeof(std::int32_t) * 2; // num_piece, piece_size
		_header_size += sizeof(std::int8_t) * m_files.num_pieces(); // [array of piece index]
        std::size_t const round_factor = 1024-1; // (pow of 2) - 1
		_header_size = (_header_size + round_factor) & ~round_factor; 
		return _header_size;
	}

    bool has_changed_since;
	lt::file_storage const& m_files;
	std::map<lt::piece_index_t, std::vector<char>> m_file_data;
    std::set<lt::piece_index_t> m_invalid;
	std::string m_save_path;
	std::string m_save_name;
	lt::aux::vector<lt::download_priority_t, lt::file_index_t> m_file_priority;
};

#endif // RAM_STORAGE_H_
