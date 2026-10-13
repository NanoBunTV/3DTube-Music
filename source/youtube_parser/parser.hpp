#pragma once
#include <vector>
#include <string>
#include <map>
#include <functional>

// Regular YouTube (InnerTube) API : used only to resolve audio streams of a track (YouTube Music track ids are
// regular video ids). Music metadata (search, albums, home, lyrics...) comes from music/music_parser.

struct YouTubeChannelSuccinct {
	std::string name;
	std::string id;
	std::string icon_url;
	std::string subscribers;
	std::string video_num;
	bool is_collaboration = false;
};
struct YouTubeVideoSuccinct {
	std::string url;
	std::string title;
	std::string duration_text;
	std::string publish_date;
	std::string views_str;
	std::string author;
	std::string thumbnail_url;
};

struct YouTubeVideoDetail {
	std::string error;
	std::string title;
	YouTubeChannelSuccinct author; // only name and id are set
	std::string id;
	std::string succinct_thumbnail_url;
	std::string audio_stream_url;
	int duration_ms = 0;
	bool is_livestream = false;
	enum class LivestreamType {
		PREMIERE,
		LIVESTREAM,
	};
	LivestreamType livestream_type = LivestreamType::LIVESTREAM;
	bool is_upcoming = false;
	std::string playability_status;
	std::string playability_reason;
	int stream_fragment_len = -1; // used only for livestreams

	// SponsorBlock segments to be skipped automatically (empty if disabled or none submitted)
	struct SponsorSegment {
		double start;
		double end;
		std::string category; // e.g. "sponsor", "music_offtopic"
	};
	std::vector<SponsorSegment> sponsor_segments;

	bool needs_timestamp_adjusting() const { return is_livestream && livestream_type == LivestreamType::PREMIERE; }
	bool is_audio_playable() const { return playability_status == "OK" && audio_stream_url != ""; }
};
// resolves the audio stream and basic metadata (title, channel) of a video, plus SponsorBlock segments if enabled
YouTubeVideoDetail youtube_load_video_page(std::string url);

// SponsorBlock (sponsorblock.cpp)
std::string youtube_sponsorblock_url(const std::string &video_id);
// fills res.sponsor_segments from a response of youtube_sponsorblock_url(res.id)
void youtube_parse_sponsor_segments(YouTubeVideoDetail &res, const std::string &response);

void youtube_change_content_language(std::string language_code);

/* -------------------------------- utils.cpp -------------------------------- */
std::string youtube_get_video_id_by_url(const std::string &url);
std::string youtube_get_playlist_id_by_url(const std::string &url);
std::string youtube_get_video_thumbnail_url_by_id(const std::string &id);
std::string youtube_get_video_url_by_id(const std::string &id);
std::string get_video_id_from_thumbnail_url(const std::string &url);
std::string convert_webp_thumbnail_to_jpg(const std::string &url);
bool youtube_is_valid_video_id(const std::string &id);
bool is_youtube_url(const std::string &url);
bool is_youtube_thumbnail_url(const std::string &url);
int64_t extract_stream_length(const std::string &url);

enum class YouTubePageType { VIDEO, CHANNEL, SEARCH, INVALID };
YouTubePageType youtube_get_page_type(std::string url);
