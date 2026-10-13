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

	bool from_account = false; // loaded with music_load_account_playlist()

	bool has_more() const { return continuation != ""; }
	void load_more();
};
MusicCollection music_load_collection(const std::string &browse_id);

// The signed-in user's library. YouTube Music's own API (WEB_REMIX) rejects the app's TV sign-in tokens
// (HTTP 400), so this uses the regular YouTube API with the ANDROID_VR client, which accepts them.
// Playlists here are YouTube playlists, the same ones YouTube Music shows.
struct MusicLibraryResult {
	std::string error;
	std::vector<MusicItem> playlists; // type PLAYLIST, browse_id "VL<playlist id>"; doesn't include Liked Music
};
MusicLibraryResult music_load_library();
// browse ids of the user's playlists are passed around (e.g. as the MUSIC_BROWSE scene arg) with this prefix
#define MUSIC_ACCOUNT_PREFIX "account:"
#define MUSIC_LIKED_MUSIC_BROWSE_ID "VLLM"
// `browse_id` : "VL<playlist id>", without MUSIC_ACCOUNT_PREFIX
MusicCollection music_load_account_playlist(const std::string &browse_id);

// Signed-in actions (same API as the library). All return an error message, "" on success.
// Liking a song adds it to Liked Music.
std::string music_get_like_status(const std::string &video_id, bool &liked);
std::string music_set_like(const std::string &video_id, bool like);

struct MusicPlaylistOption {
	std::string playlist_id; // without "VL"
	std::string title;
	std::string privacy; // PUBLIC, UNLISTED or PRIVATE
	bool contains_video = false;
};
// the user's playlists the video can be added to (Watch later excluded), with whether they already contain it
std::vector<MusicPlaylistOption> music_get_add_to_playlist(const std::string &video_id, std::string &error);
std::string music_edit_playlist(const std::string &playlist_id, const std::string &video_id, bool add);
// creates a private playlist containing the video
std::string music_create_playlist(const std::string &title, const std::string &video_id, std::string &playlist_id);

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

// plain (unsynced) lyrics of a track
struct MusicLyrics {
	std::string error;     // network / API error
	bool available = false; // false : the track has no lyrics (e.g. most music videos), `message` says why
	std::string text;      // lines separated by '\n'
	std::string source;    // e.g. "Source: Musixmatch"
	std::string message;
};
MusicLyrics music_load_lyrics(const std::string &video_id);

// rewrites a googleusercontent / ytimg thumbnail url to the requested square size (jpeg)
std::string music_thumbnail_url(const std::string &url, int size);
// thumbnail for tracks played from regular YouTube links (16:9)
std::string music_video_thumbnail_url(const std::string &video_id);
