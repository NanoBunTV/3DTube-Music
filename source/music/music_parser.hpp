#pragma once
#include <string>
#include <vector>

// YouTube Music (music.youtube.com, InnerTube client WEB_REMIX) metadata API.
// Only metadata comes from here; audio streams are still resolved through youtube_load_video_page(),
// since YouTube Music track ids are regular YouTube video ids.

struct MusicTrack {
	std::string video_id;
	std::string title;
	std::string artist;
	std::string album;
	std::string duration_text;
	std::string thumbnail_url; // may be empty (e.g. tracks on an album page share the album art)
};

struct MusicItem {
	enum class Type { SONG, ALBUM, ARTIST, PLAYLIST };
	Type type = Type::SONG;
	MusicTrack track;      // valid if type == SONG
	std::string browse_id; // valid otherwise (MPREb_..., UC..., VL...)
	std::string title;
	std::string subtitle;
	std::string thumbnail_url;
};

enum class MusicSearchFilter { SONGS, ALBUMS, ARTISTS, PLAYLISTS };

struct MusicSearchResult {
	std::string error;
	MusicSearchFilter filter = MusicSearchFilter::SONGS;
	std::vector<MusicItem> items;
	std::string continuation;

	bool has_more() const { return continuation != ""; }
	void load_more();
};
MusicSearchResult music_search(const std::string &query, MusicSearchFilter filter);

// album, playlist, or artist page
struct MusicCollection {
	enum class Kind { ALBUM, PLAYLIST, ARTIST };
	std::string error;
	Kind kind = Kind::PLAYLIST;
	std::string browse_id;
	std::string title;
	std::string subtitle;
	std::string thumbnail_url;
	std::vector<MusicTrack> tracks; // for artists : top songs
	std::string continuation;

	// artist only
	std::string all_songs_browse_id;  // playlist containing all of the artist's songs
	std::vector<MusicItem> releases;  // albums, singles & EPs

	bool has_more() const { return continuation != ""; }
	void load_more();
};
MusicCollection music_load_collection(const std::string &browse_id);

// home feed : shelves from the YouTube Music home and explore pages (works signed out)
struct MusicShelf {
	std::string title;
	std::vector<MusicItem> items;
};
struct MusicHomeResult {
	std::string error;
	std::vector<MusicShelf> shelves;
};
MusicHomeResult music_load_home();

// "radio" (automix) based on a track; the first item is usually the seed track itself
std::vector<MusicTrack> music_load_radio(const std::string &video_id, std::string &error);

// rewrites a googleusercontent / ytimg thumbnail url to the requested square size (jpeg)
std::string music_thumbnail_url(const std::string &url, int size);
// thumbnail for tracks played from regular YouTube links (16:9)
std::string music_video_thumbnail_url(const std::string &video_id);
