#pragma once
#include <string>
#include <vector>
#include "music/music_parser.hpp"

// The play queue. Thread-safe: used by the UI thread and the player's decode thread.
namespace MusicQueue {

enum class RepeatMode { OFF, ALL, ONE };

// replaces the whole queue and makes `start_index` (index into `tracks`) the current track
void set(const std::vector<MusicTrack> &tracks, int start_index);
// insert right after the current track
void add_next(const MusicTrack &track);
void add_to_end(const std::vector<MusicTrack> &tracks);
void clear();

bool has_current();
MusicTrack get_current();       // empty track if none
MusicTrack peek_next();         // the track next() would move to (ignoring repeat-one); empty if none
int get_current_position();     // position in the play order, -1 if none
std::vector<MusicTrack> get_play_order(); // tracks in the order they will be played
int size();

// fills in metadata for the current track (e.g. after playing a plain YouTube link)
void update_current_metadata(const MusicTrack &track);

// advance to the next track. `automatic` : called because the current track ended
// (repeat-one then keeps the same track). Returns false if the end of the queue was reached.
bool next(bool automatic);
bool prev();
bool jump_to(int position);
// removes the track at `position` in the play order (not the current one)
void remove(int position);

bool get_shuffle();
void set_shuffle(bool shuffle);
RepeatMode get_repeat();
void set_repeat(RepeatMode mode);
RepeatMode cycle_repeat(); // OFF -> ALL -> ONE -> OFF

// incremented on every change, so the UI can tell when to rebuild its views
int get_version();

} // namespace MusicQueue
