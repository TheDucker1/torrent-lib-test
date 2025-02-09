#include<libtorrent/libtorrent.hpp>
#include<functional>
#include"ram_disk_io.hpp"

lt::storage_holder ram_disk_io::new_torrent(
	lt::storage_params const& params,
	std::shared_ptr<void> const&) {

	auto pop = [&]() -> lt::storage_index_t {
		TORRENT_ASSERT(!m_free_slots.empty());
		lt::storage_index_t ret = m_free_slots.back();
		m_free_slots.pop_back();
		return ret;
		};

	lt::storage_index_t const idx = m_free_slots.empty() ?
		m_torrents.end_index() :
		pop();
	auto storage = std::make_unique<ram_storage>(params);

	if (idx == m_torrents.end_index()) m_torrents.emplace_back(std::move(storage));
	else m_torrents[idx] = std::move(storage);

	return lt::storage_holder(idx, *this);
}

void ram_disk_io::remove_torrent(lt::storage_index_t const idx) {
	m_torrents[idx].reset();
	m_free_slots.push_back(idx);
}

void ram_disk_io::abort(bool) {}

void ram_disk_io::async_read(lt::storage_index_t storage,
	lt::peer_request const& r,
	std::function<void(lt::disk_buffer_holder block, lt::storage_error const& se)> handler,
	lt::disk_job_flags_t) {

	lt::storage_error error;
	lt::span<char const> b = m_torrents[storage]->readv(r, error);

	boost::asio::post(m_ioc, [handler, error, b, this] {
		handler(lt::disk_buffer_holder(
			*this, const_cast<char*>(b.data()), int(b.size())
		),
			error);
		});
}

bool ram_disk_io::async_write(lt::storage_index_t storage,
	lt::peer_request const& r,
	char const* buf, std::shared_ptr<lt::disk_observer>,
	std::function<void(lt::storage_error const&)> handler,
	lt::disk_job_flags_t) {

	lt::span<char const> const b = { buf, r.length };

	m_torrents[storage]->writev(b, r.piece, r.start);

	boost::asio::post(m_ioc, [=] { handler(lt::storage_error()); });
	return false;
}

void ram_disk_io::async_hash(lt::storage_index_t storage,
	lt::piece_index_t const piece,
	lt::span<lt::sha256_hash> block_hashes,
	lt::disk_job_flags_t,
	std::function<void(lt::piece_index_t, lt::sha1_hash const&,
		lt::storage_error const&)> handler) {

	lt::storage_error error;
	lt::sha1_hash const hash = m_torrents[storage]->hash(piece, block_hashes, error);
	boost::asio::post(m_ioc, [=] {handler(piece, hash, error);});
}

void ram_disk_io::async_hash2(lt::storage_index_t storage,
	lt::piece_index_t const piece,
	int const offset,
	lt::disk_job_flags_t,
	std::function<void(lt::piece_index_t, lt::sha256_hash const&,
		lt::storage_error const&)> handler) {

	lt::storage_error error;
	lt::sha256_hash const hash = m_torrents[storage]->hash2(piece, offset, error);
	boost::asio::post(m_ioc, [=] {handler(piece, hash, error);});
}

void ram_disk_io::async_move_storage(lt::storage_index_t storage, std::string p,
	lt::move_flags_t const flags,
	std::function<void(lt::status_t, std::string const&,
		lt::storage_error const&)> handler) {

	ram_storage* st = m_torrents[storage].get();
	lt::storage_error ec;
	lt::status_t ret;

	std::tie(ret, p) = st->move_storage(p, flags, ec);
	boost::asio::post(m_ioc, [=] {handler(ret, p, ec);});
}

void ram_disk_io::async_release_files(lt::storage_index_t storage, std::function<void()> handler) {

	ram_storage* st = m_torrents[storage].get();
	st->release_files();
	if (!handler) return;
	boost::asio::post(m_ioc, [=] { handler(); });
}

void ram_disk_io::async_delete_files(lt::storage_index_t storage,
	lt::remove_flags_t const options,
	std::function<void(lt::storage_error const&)> handler) {

	boost::asio::post(m_ioc, [=] { handler(lt::storage_error()); });
}

void ram_disk_io::async_check_files(lt::storage_index_t storage, 
	lt::add_torrent_params const* resume_data,
	lt::aux::vector<std::string, lt::file_index_t> links,
	std::function<void(lt::status_t, lt::storage_error const&)> handler) {

	ram_storage* st = m_torrents[storage].get();

	lt::add_torrent_params tmp;
	lt::add_torrent_params const* rd = resume_data ? resume_data : &tmp;

	lt::storage_error error;
	lt::status_t const ret = [&] {
		st->initialize(error);
		
        return lt::status_t::no_error;
		}();

	boost::asio::post(m_ioc, [error, ret, handler] {handler(ret, error);});
}

void ram_disk_io::async_rename_file(lt::storage_index_t, lt::file_index_t const idx,
	std::string const name,
	std::function<void(std::string const&, lt::file_index_t,
		lt::storage_error const&)> handler) {

	boost::asio::post(m_ioc, [=] { handler(name, idx, lt::storage_error()); });
}

void ram_disk_io::async_stop_torrent(lt::storage_index_t storage
	, std::function<void()> handler) {

	if (!handler) return;
	boost::asio::post(m_ioc, std::move(handler));
}

void ram_disk_io::async_set_file_priority(lt::storage_index_t storage,
	lt::aux::vector<lt::download_priority_t, lt::file_index_t> prio,
	std::function<void(lt::storage_error const&,
		lt::aux::vector<lt::download_priority_t, lt::file_index_t>)> handler) {

	ram_storage* st = m_torrents[storage].get();
	lt::storage_error error;
	st->set_file_priority(prio, error);
	boost::asio::post(m_ioc, [p = std::move(prio), h = std::move(handler), error]() mutable
		{ h(error, std::move(p)); });
}

void ram_disk_io::async_clear_piece(lt::storage_index_t, lt::piece_index_t index,
	std::function<void(lt::piece_index_t)> handler) {

	boost::asio::post(m_ioc, [=] { handler(index); });
}

void ram_disk_io::free_disk_buffer(char*) {

}

void ram_disk_io::update_stats_counters(lt::counters&) const {

}

std::vector<lt::open_file_state> ram_disk_io::get_status(lt::storage_index_t) const {
	return {};
}

void ram_disk_io::submit_jobs() {

}

std::unique_ptr<lt::disk_interface> ram_disk_constructor(
	lt::io_context& ioc, 
	lt::settings_interface const&, 
	lt::counters&
) {
	return std::make_unique<ram_disk_io>(ioc);
}
