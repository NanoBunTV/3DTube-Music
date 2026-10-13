#include "headers.hpp"
#include "music/queue.hpp"
#include <numeric>
#include <random>

namespace MusicQueue {
namespace {
Mutex lock;
std::vector<MusicTrack> tracks; // in insertion order
std::vector<int> order;         // play order : indices into `tracks`
int pos = -1;                   // position in `order` of the current track
bool shuffle = false;
RepeatMode repeat = RepeatMode::OFF;
int version = 0;
std::mt19937 rng((unsigned)osGetTime());

// reorders everything after the current track (or the whole queue if there's no current track)
void shuffle_upcoming() {
	int from = pos + 1;
	if (from < (int)order.size()) {
		std::shuffle(order.begin() + from, order.end(), rng);
	}
}
void rebuild_order() {
	int current_track = pos >= 0 && pos < (int)order.size() ? order[pos] : -1;
	order.resize(tracks.size());
	std::iota(order.begin(), order.end(), 0);
	if (shuffle) {
		// the current track goes first, the rest is shuffled
		if (current_track >= 0) {
			std::swap(order[0], order[current_track]);
			pos = 0;
		} else {
			pos = -1;
		}
		shuffle_upcoming();
	} else {
		pos = current_track;
	}
	version++;
}
} // namespace

void set(const std::vector<MusicTrack> &new_tracks, int start_index) {
	lock.lock();
	tracks = new_tracks;
	order.resize(tracks.size());
	std::iota(order.begin(), order.end(), 0);
	pos = tracks.empty() ? -1 : std::max(0, std::min<int>(start_index, tracks.size() - 1));
	if (shuffle && pos >= 0) {
		std::swap(order[0], order[pos]);
		pos = 0;
		shuffle_upcoming();
	}
	version++;
	lock.unlock();
}

void add_next(const MusicTrack &track) {
	lock.lock();
	tracks.push_back(track);
	order.insert(order.begin() + (pos + 1), tracks.size() - 1);
	if (pos < 0) {
		pos = 0;
	}
	version++;
	lock.unlock();
}

void add_to_end(const std::vector<MusicTrack> &new_tracks) {
	lock.lock();
	for (auto &track : new_tracks) {
		tracks.push_back(track);
		order.push_back(tracks.size() - 1);
	}
	if (pos < 0 && !tracks.empty()) {
		pos = 0;
	}
	version++;
	lock.unlock();
}

void clear() {
	lock.lock();
	tracks.clear();
	order.clear();
	pos = -1;
	version++;
	lock.unlock();
}

bool has_current() {
	lock.lock();
	bool res = pos >= 0 && pos < (int)order.size();
	lock.unlock();
	return res;
}

MusicTrack get_current() {
	lock.lock();
	MusicTrack res;
	if (pos >= 0 && pos < (int)order.size()) {
		res = tracks[order[pos]];
	}
	lock.unlock();
	return res;
}

MusicTrack peek_next() {
	lock.lock();
	MusicTrack res;
	if (pos + 1 < (int)order.size()) {
		res = tracks[order[pos + 1]];
	} else if (repeat == RepeatMode::ALL && !order.empty() && !shuffle) {
		res = tracks[order[0]];
	}
	lock.unlock();
	return res;
}

int get_current_position() {
	lock.lock();
	int res = pos;
	lock.unlock();
	return res;
}

std::vector<MusicTrack> get_play_order() {
	lock.lock();
	std::vector<MusicTrack> res;
	res.reserve(order.size());
	for (int i : order) {
		res.push_back(tracks[i]);
	}
	lock.unlock();
	return res;
}

int size() {
	lock.lock();
	int res = order.size();
	lock.unlock();
	return res;
}

void update_current_metadata(const MusicTrack &track) {
	lock.lock();
	if (pos >= 0 && pos < (int)order.size() && tracks[order[pos]].video_id == track.video_id) {
		auto &cur = tracks[order[pos]];
		if (cur.title.empty()) {
			cur.title = track.title;
		}
		if (cur.artist.empty()) {
			cur.artist = track.artist;
		}
		if (cur.thumbnail_url.empty()) {
			cur.thumbnail_url = track.thumbnail_url;
		}
		if (cur.duration_text.empty()) {
			cur.duration_text = track.duration_text;
		}
		version++;
	}
	lock.unlock();
}

bool next(bool automatic) {
	lock.lock();
	bool res = true;
	if (order.empty()) {
		res = false;
	} else if (automatic && repeat == RepeatMode::ONE) {
		// stay on the same track
	} else if (pos + 1 < (int)order.size()) {
		pos++;
	} else if (repeat != RepeatMode::OFF) {
		pos = 0;
		if (shuffle) { // new shuffle for the next round
			std::shuffle(order.begin(), order.end(), rng);
		}
	} else {
		res = false;
	}
	if (res) {
		version++;
	}
	lock.unlock();
	return res;
}

bool prev() {
	lock.lock();
	bool res = true;
	if (order.empty()) {
		res = false;
	} else if (pos > 0) {
		pos--;
	} else if (repeat == RepeatMode::ALL) {
		pos = order.size() - 1;
	} else {
		res = false;
	}
	if (res) {
		version++;
	}
	lock.unlock();
	return res;
}

bool jump_to(int position) {
	lock.lock();
	bool res = position >= 0 && position < (int)order.size();
	if (res) {
		pos = position;
		version++;
	}
	lock.unlock();
	return res;
}

void remove(int position) {
	lock.lock();
	if (position >= 0 && position < (int)order.size() && position != pos) {
		order.erase(order.begin() + position);
		if (position < pos) {
			pos--;
		}
		version++;
	}
	lock.unlock();
}

bool get_shuffle() {
	lock.lock();
	bool res = shuffle;
	lock.unlock();
	return res;
}

void set_shuffle(bool new_shuffle) {
	lock.lock();
	if (shuffle != new_shuffle) {
		shuffle = new_shuffle;
		rebuild_order();
	}
	lock.unlock();
}

RepeatMode get_repeat() {
	lock.lock();
	RepeatMode res = repeat;
	lock.unlock();
	return res;
}

void set_repeat(RepeatMode mode) {
	lock.lock();
	repeat = mode;
	version++;
	lock.unlock();
}

RepeatMode cycle_repeat() {
	lock.lock();
	repeat = repeat == RepeatMode::OFF ? RepeatMode::ALL : repeat == RepeatMode::ALL ? RepeatMode::ONE : RepeatMode::OFF;
	RepeatMode res = repeat;
	version++;
	lock.unlock();
	return res;
}

int get_version() {
	lock.lock();
	int res = version;
	lock.unlock();
	return res;
}

} // namespace MusicQueue
