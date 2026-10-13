#include <regex>
#include "internal_common.hpp"
#include "parser.hpp"
#include "../variables.hpp"
#include "../oauth/oauth.hpp"

// Resolves the audio stream of a track with a single InnerTube `player` request (plus SponsorBlock, in parallel).
// Title and channel come from the player response's videoDetails, so no `next` request is needed.
// See Docs/YOUTUBE_API.md for which clients work and why.

#define VISITOR_DATA_MAX_AGE_MS (30 * 60 * 1000)

// `client` : the "client" object of the request context, with %0 (hl), %1 (gl), %2 (visitorData) placeholders
static const char *PLAYER_CLIENT_ANDROID =
    R"({"hl": "%0","gl": "%1","clientName": "ANDROID","clientVersion": "20.10.38","deviceMake": "Apple","deviceModel": "iPhone9,1","osName": "iPhone","userAgent": "com.google.ios.youtube/19.01.1 (iPhone9,1; U; CPU iOS 15_0_0 like Mac OS X;)\"","osVersion": "15.0.0.19A346", "visitorData": "%2"})";
static const char *PLAYER_CLIENT_ANDROID_VR =
    R"({"hl": "%0","gl": "%1","clientName": "ANDROID_VR","clientVersion": "1.65.10","deviceMake": "Oculus","deviceModel": "Quest 3","androidSdkVersion": "34","osName": "Android", "userAgent": "com.google.android.apps.youtube.vr.oculus/1.62.27 (Linux; U; Android 12L; eureka-user Build/SQ3A.220605.009.A1) gzip\"","osVersion": "12L", "visitorData": "%2"})";
static const char *PLAYER_CLIENT_VISIONOS =
    R"({"hl": "%0","gl": "%1","clientName": "VISIONOS","clientVersion": "0.1","deviceMake": "Apple","deviceModel": "iPhone16,2","osName": "iPhone","userAgent": "com.google.ios.youtube/20.10.4 (iPhone16,2; U; CPU iOS 18_3_2 like Mac OS X;)\"","osVersion": "2.2.22N842", "visitorData": "%2"})";
// used with the signed-in account's token, only as a fallback (see youtube_load_video_page())
static const char *PLAYER_CLIENT_ANDROID_VR_SIGNED_IN =
    R"({"hl": "%0","gl": "%1","clientName": "ANDROID_VR","clientVersion": "1.65.10","deviceMake": "Oculus","deviceModel": "Quest 3","androidSdkVersion": "34","osName": "Android","osVersion": "14","visitorData": "%2"})";

static std::string make_player_request_body(const char *client, const std::string &video_id,
                                            const std::string &visitor_data, bool content_check_ok) {
	std::string client_str = client;
	client_str = std::regex_replace(client_str, std::regex("%0"), language_code);
	client_str = std::regex_replace(client_str, std::regex("%1"), country_code);
	client_str = std::regex_replace(client_str, std::regex("%2"), visitor_data);
	return R"({"videoId": ")" + video_id + R"(", "context": {"client": )" + client_str +
	       R"(}, "playbackContext": {"contentPlaybackContext": {"signatureTimestamp": "0"}})" +
	       (content_check_ok ? R"(, "contentCheckOk": true, "racyCheckOk": true)" : "") + "}";
}

static void extract_player_data(Document &json_root, RJson player_response, YouTubeVideoDetail &res) {
	res.playability_status = player_response["playabilityStatus"]["status"].string_value();
	res.playability_reason = player_response["playabilityStatus"]["reason"].string_value();

	RJson details = player_response["videoDetails"];
	if (details.is_valid()) {
		res.title = details["title"].string_value();
		res.author.name = details["author"].string_value();
		// auto-generated artist channels are named "<artist> - Topic"
		const std::string topic_suffix = " - Topic";
		if (ends_with(res.author.name, topic_suffix)) {
			res.author.name = res.author.name.substr(0, res.author.name.size() - topic_suffix.size());
		}
		res.author.id = details["channelId"].string_value();
		res.is_upcoming = details["isUpcoming"].bool_value();
		res.livestream_type = details["isLiveContent"].bool_value() ? YouTubeVideoDetail::LivestreamType::LIVESTREAM
		                                                            : YouTubeVideoDetail::LivestreamType::PREMIERE;
		if (details["lengthSeconds"].string_value() != "") {
			res.duration_ms = atoi(details["lengthSeconds"].string_value().c_str()) * 1000;
		}
	}

	std::vector<RJson> formats;
	for (auto i : player_response["streamingData"]["formats"].array_items()) {
		formats.push_back(i);
	}
	for (auto i : player_response["streamingData"]["adaptiveFormats"].array_items()) {
		formats.push_back(i);
	}
	for (auto &i : formats) {
		// something like %2C still appears in the url, so decode them back
		i.set_str(json_root, "url", url_decode(i["url"].string_value()).c_str());
	}

	res.stream_fragment_len = -1;
	res.is_livestream = false;
	std::vector<RJson> audio_formats;
	for (auto i : formats) {
		if (i.has_key("targetDurationSec")) {
			int new_stream_fragment_len = i["targetDurationSec"].int_value();
			if (res.stream_fragment_len != -1 && res.stream_fragment_len != new_stream_fragment_len) {
				debug_warning("[unexp] diff targetDurationSec for diff streams");
			}
			res.stream_fragment_len = new_stream_fragment_len;
			res.is_livestream = true;
		}
		if (i["approxDurationMs"].string_value() != "") {
			res.duration_ms = stoll(i["approxDurationMs"].string_value());
		}
		if (i["type"].string_value() == "FORMAT_STREAM_TYPE_OTF") {
			continue;
		}
		auto mime_type = i["mimeType"].string_value();
		// AAC only (FFmpeg is built without opus)
		if (mime_type.substr(0, 5) == "audio" && mime_type.find("mp4a") != std::string::npos) {
			audio_formats.push_back(i);
		}
	}
	res.audio_stream_url = "";
	for (auto i : audio_formats) {
		if (i["itag"].int_value() == 140) { // AAC 128 kbps
			res.audio_stream_url = i["url"].string_value();
		}
	}
	if (res.audio_stream_url == "" && audio_formats.size()) {
		res.audio_stream_url = audio_formats[0]["url"].string_value();
	}
}

static void parse_player_response(std::vector<u8> &data, YouTubeVideoDetail &res, const std::string &tag) {
	if (data.empty()) {
		res.error = "[" + tag + "] Empty response";
		debug_error(res.error);
		return;
	}
	data.push_back('\0');
	parse_json_destructive(
	    (char *)&data[0], [&](Document &json_root, RJson json) { extract_player_data(json_root, json, res); },
	    [&](const std::string &error) {
		    res.error = "[" + tag + "] " + error;
		    debug_error(res.error);
	    });
}

static std::string fetch_visitor_data() {
	std::string url = "https://www.youtube.com/sw.js_data";
	std::map<std::string, std::string> headers = {{"Origin", "https://www.youtube.com"},
	                                              {"Referer", "https://www.youtube.com/"}};
	auto response = http_get(url, headers);
	if (!response.first) {
		logger.error("Visitor Data", "Failed to fetch visitor data");
		return "";
	}
	std::string json_content = response.second;
	const std::string unwanted_prefix = ")]}'\n";
	if (json_content.find(unwanted_prefix) == 0) {
		json_content = json_content.substr(unwanted_prefix.size());
	}
	rapidjson::Document document;
	std::string error;
	RJson data = RJson::parse(document, json_content.c_str(), error);
	if (!data.is_valid()) {
		logger.error("Visitor Data", "JSON Parsing Error: " + error);
		return "";
	}
	std::string visitor_data = data[(size_t)0][(size_t)2][(size_t)0][(size_t)0][(size_t)13].string_value();
	logger.info("Visitor Data", "Fetched new visitor data: " + visitor_data);
	return visitor_data;
}

// visitor data is reused for a while instead of being fetched for every track (saves a request per track)
// only called from the async task thread
static std::string get_visitor_data() {
	static std::string cached;
	static u64 fetched_at = 0;
	u64 now = osGetTime();
	if (cached.empty() || now - fetched_at > VISITOR_DATA_MAX_AGE_MS) {
		std::string fetched = fetch_visitor_data();
		if (!fetched.empty()) {
			cached = fetched;
			fetched_at = now;
		}
	}
	return cached;
}

YouTubeVideoDetail youtube_load_video_page(std::string url) {
	YouTubeVideoDetail res;

	res.id = youtube_get_video_id_by_url(url);
	if (res.id.empty()) {
		res.error = "Not a video URL: " + url;
		return res;
	}
	std::string visitor_data = get_visitor_data();

	// The player request uses the selected client without authentication : as of 2026-10, authenticated
	// Android VR (and plain Android) stream urls stop working after ~1 minute (HTTP 403, PO token enforcement),
	// while visionOS serves full streams. Authenticated Android VR is only used as a fallback (see below).
	const char *client;
	if (var_player_response == 0) {
		client = PLAYER_CLIENT_ANDROID;
	} else if (var_player_response == 1) {
		client = PLAYER_CLIENT_ANDROID_VR;
	} else if (var_player_response == 2) {
		client = PLAYER_CLIENT_VISIONOS;
	} else {
		logger.error("appdata", "Invalid value for player_response has been set. Falling back to 2 (visionOS)!");
		client = PLAYER_CLIENT_VISIONOS;
		var_player_response = 2;
		misc_tasks_request(TASK_SAVE_SETTINGS);
	}

	// the player and SponsorBlock requests are sent in parallel
	std::vector<HttpRequest> requests;
	requests.push_back(http_post_json_request(get_innertube_api_url("player"),
	                                          make_player_request_body(client, res.id, visitor_data, false)));
	bool use_sponsorblock = var_sponsorblock_enabled;
	if (use_sponsorblock) {
		requests.push_back(http_get_request(youtube_sponsorblock_url(res.id)));
	}
	debug_info("accessing(multi)...");
	std::vector<NetworkResult> results = thread_network_session_list.perform(requests);

	bool success = !results[0].fail;
	if (success) {
		parse_player_response(results[0].data, res, "v-player");
	} else {
		res.error = "[v-player] Network request failed : " + results[0].error;
		debug_error(res.error);
	}

	// Fallback : if the song needs a signed-in account (age restriction etc.), retry with authenticated Android VR.
	// Its stream may be cut off after ~1 minute, but the player then reloads the url and resumes.
	std::string token = success && res.playability_status != "OK" ? OAuth::get_valid_access_token() : "";
	if (token != "") {
		debug_info("Fallback to authenticated Android VR client");
		std::map<std::string, std::string> headers = {{"Authorization", "Bearer " + token}};
		auto fallback_result = thread_network_session_list.perform(http_post_json_request(
		    get_innertube_api_url("player"),
		    make_player_request_body(PLAYER_CLIENT_ANDROID_VR_SIGNED_IN, res.id, visitor_data, true), headers));
		if (!fallback_result.fail) {
			YouTubeVideoDetail fallback = res;
			parse_player_response(fallback_result.data, fallback, "v-fallback");
			if (fallback.is_audio_playable()) {
				res = fallback;
				res.error = "";
			}
		} else {
			debug_error("[v-fallback] Network request failed");
		}
	}

	res.succinct_thumbnail_url = youtube_get_video_thumbnail_url_by_id(res.id);
	if (use_sponsorblock && res.is_audio_playable() && !results[1].fail) {
		youtube_parse_sponsor_segments(res, std::string(results[1].data.begin(), results[1].data.end()));
	}
	if (res.title != "") {
		HistoryVideo video;
		video.id = res.id;
		video.title = res.title;
		video.author_name = res.author.name;
		video.length_text = Util_convert_seconds_to_time((double)res.duration_ms / 1000);
		video.my_view_count = 1;
		video.last_watch_time = time(NULL);
		add_watched_video(video);
		misc_tasks_request(TASK_SAVE_HISTORY);
	}
	debug_info(res.title.empty() ? "preason: " + res.playability_reason : res.title);
	return res;
}
