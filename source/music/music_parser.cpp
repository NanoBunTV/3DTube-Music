#include "youtube_parser/internal_common.hpp"
#include "music/music_parser.hpp"
#include <algorithm>

#define MUSIC_API_BASE "https://music.youtube.com/youtubei/v1/"
#define MUSIC_CLIENT_NAME "WEB_REMIX"
#define MUSIC_CLIENT_VERSION "1.20251006.03.00"

#define debug_music_info(s) logger.info("music", (s))
#define debug_music_error(s) logger.error("music", (s))

namespace {

std::string json_escape(const std::string &str) {
	std::string res;
	for (unsigned char c : str) {
		if (c == '"' || c == '\\') {
			res.push_back('\\'), res.push_back(c);
		} else if (c < 0x20) {
			char buf[8];
			snprintf(buf, sizeof(buf), "\\u%04x", c);
			res += buf;
		} else {
			res.push_back(c);
		}
	}
	return res;
}

std::string url_encode(const std::string &str) {
	std::string res;
	for (unsigned char c : str) {
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			res.push_back(c);
		} else {
			res.push_back('%');
			res.push_back("0123456789ABCDEF"[c >> 4]);
			res.push_back("0123456789ABCDEF"[c & 15]);
		}
	}
	return res;
}

// `body` : the request's top-level members other than "context", e.g. "\"query\":\"abc\"" (may be empty)
std::string make_request_body(const std::string &body) {
	return std::string(R"({"context":{"client":{"clientName":")") + MUSIC_CLIENT_NAME + R"(","clientVersion":")" +
	       MUSIC_CLIENT_VERSION + R"(","hl":")" + language_code + R"(","gl":")" + country_code + R"("}})" +
	       (body.empty() ? "" : "," + body) + "}";
}

// performs the request and calls on_success(RJson) with the parsed response, or returns an error message
template <class Func> std::string music_api_call(const std::string &endpoint, const std::string &body, Func on_success) {
	std::map<std::string, std::string> headers = {{"Origin", "https://music.youtube.com"},
	                                              {"Referer", "https://music.youtube.com/"}};
	auto response = http_post_json(MUSIC_API_BASE + endpoint, make_request_body(body), headers);
	if (!response.first) {
		return "Network error : " + response.second;
	}
	std::string error;
	std::vector<char> data(response.second.begin(), response.second.end());
	data.push_back('\0');
	parse_json_destructive(
	    &data[0],
	    [&](Document &, RJson json) {
		    if (json.has_key("error")) {
			    error = "API error : " + json["error"]["message"].string_value();
		    } else {
			    on_success(json);
		    }
	    },
	    [&](const std::string &parse_error) { error = "JSON error : " + parse_error; });
	return error;
}

std::string runs_text(RJson text) {
	std::string res;
	for (auto run : text["runs"].array_items()) {
		res += run["text"].string_value();
	}
	if (res.empty()) {
		res = text["simpleText"].string_value();
	}
	return res;
}

// splits runs into segments separated by " • "
std::vector<std::vector<RJson>> split_runs(RJson text) {
	std::vector<std::vector<RJson>> res(1);
	for (auto run : text["runs"].array_items()) {
		if (run["text"].string_value() == " • ") {
			res.emplace_back();
		} else {
			res.back().push_back(run);
		}
	}
	if (res.back().empty()) {
		res.pop_back();
	}
	return res;
}
std::string join_runs(const std::vector<RJson> &runs) {
	std::string res;
	for (auto run : runs) {
		res += run["text"].string_value();
	}
	return res;
}

std::string get_page_type(RJson run) {
	return run["navigationEndpoint"]["browseEndpoint"]["browseEndpointContextSupportedConfigs"]
	          ["browseEndpointContextMusicConfig"]["pageType"]
	              .string_value();
}

bool looks_like_duration(const std::string &str) {
	if (str.empty() || str.size() > 9 || str.find(':') == std::string::npos) {
		return false;
	}
	for (char c : str) {
		if (!isdigit((unsigned char)c) && c != ':') {
			return false;
		}
	}
	return true;
}

std::string get_thumbnail(RJson thumbnail_container) {
	// accepts {musicThumbnailRenderer:{thumbnail:{thumbnails:[...]}}} or {thumbnails:[...]}
	RJson thumbnails = thumbnail_container["musicThumbnailRenderer"]["thumbnail"]["thumbnails"];
	if (!thumbnails.is_valid()) {
		thumbnails = thumbnail_container["thumbnails"];
	}
	auto list = thumbnails.array_items();
	return list.size() ? list.back()["url"].string_value() : "";
}

RJson flex_column_text(RJson item, size_t index) {
	auto columns = item["flexColumns"].array_items();
	return index < columns.size() ? columns[index]["musicResponsiveListItemFlexColumnRenderer"]["text"] : RJson();
}

// musicResponsiveListItemRenderer -> track (returns false if it's not a playable track)
bool parse_track(RJson item, MusicTrack &res) {
	res.video_id = item["playlistItemData"]["videoId"].string_value();
	RJson title_text = flex_column_text(item, 0);
	if (res.video_id.empty()) {
		for (auto run : title_text["runs"].array_items()) {
			if (run["navigationEndpoint"]["watchEndpoint"]["videoId"].is_valid()) {
				res.video_id = run["navigationEndpoint"]["watchEndpoint"]["videoId"].string_value();
			}
		}
	}
	if (res.video_id.empty()) {
		return false;
	}
	res.title = runs_text(title_text);

	auto segments = split_runs(flex_column_text(item, 1));
	// unfiltered results start with a type label ("Song", "Video"), which has no endpoint
	size_t artist_segment = 0;
	if (segments.size() >= 2 && segments[0].size() == 1 && !segments[0][0]["navigationEndpoint"].is_valid() &&
	    (segments[0][0]["text"].string_value() == "Song" || segments[0][0]["text"].string_value() == "Video")) {
		artist_segment = 1;
	}
	if (artist_segment < segments.size()) {
		res.artist = join_runs(segments[artist_segment]);
	}
	if (segments.size() && segments.back().size() == 1 && looks_like_duration(join_runs(segments.back()))) {
		res.duration_text = join_runs(segments.back());
	}
	for (size_t col = 1; col < item["flexColumns"].array_items().size(); col++) {
		for (auto run : flex_column_text(item, col)["runs"].array_items()) {
			if (get_page_type(run) == "MUSIC_PAGE_TYPE_ALBUM") {
				res.album = run["text"].string_value();
			}
		}
	}
	auto fixed_columns = item["fixedColumns"].array_items();
	if (fixed_columns.size()) {
		std::string fixed = runs_text(fixed_columns[0]["musicResponsiveListItemFixedColumnRenderer"]["text"]);
		if (looks_like_duration(fixed)) {
			res.duration_text = fixed;
		}
	}
	res.thumbnail_url = get_thumbnail(item["thumbnail"]);
	return true;
}

// musicResponsiveListItemRenderer (search result of any type) -> item
bool parse_list_item(RJson item, MusicSearchFilter filter, MusicItem &res) {
	if (filter == MusicSearchFilter::SONGS) {
		res.type = MusicItem::Type::SONG;
		if (!parse_track(item, res.track)) {
			return false;
		}
		res.title = res.track.title;
		res.subtitle = res.track.artist;
		res.thumbnail_url = res.track.thumbnail_url;
		return true;
	}
	res.type = filter == MusicSearchFilter::ALBUMS    ? MusicItem::Type::ALBUM
	           : filter == MusicSearchFilter::ARTISTS ? MusicItem::Type::ARTIST
	                                                  : MusicItem::Type::PLAYLIST;
	res.browse_id = item["navigationEndpoint"]["browseEndpoint"]["browseId"].string_value();
	if (res.browse_id.empty()) {
		return false;
	}
	res.title = runs_text(flex_column_text(item, 0));
	res.subtitle = runs_text(flex_column_text(item, 1));
	// drop the redundant type label ("Album • ", "Artist • ", "Playlist • ")
	for (const char *label : {"Album • ", "Artist • ", "Playlist • "}) {
		if (starts_with(res.subtitle, label)) {
			res.subtitle = res.subtitle.substr(strlen(label));
		}
	}
	res.thumbnail_url = get_thumbnail(item["thumbnail"]);
	return true;
}

// musicTwoRowItemRenderer (carousel item) -> item
bool parse_two_row_item(RJson item, MusicItem &res) {
	std::string video_id = item["navigationEndpoint"]["watchEndpoint"]["videoId"].string_value();
	if (!video_id.empty()) { // a song / music video
		res.type = MusicItem::Type::SONG;
		res.track.video_id = video_id;
		res.track.title = res.title = runs_text(item["title"]);
		auto segments = split_runs(item["subtitle"]);
		size_t artist_segment = segments.size() >= 2 && segments[0].size() == 1 &&
		                                !segments[0][0]["navigationEndpoint"].is_valid()
		                            ? 1 // skip a type label like "Song" / "Video"
		                            : 0;
		if (artist_segment < segments.size()) {
			res.track.artist = join_runs(segments[artist_segment]);
		}
		res.subtitle = res.track.artist;
		res.track.thumbnail_url = res.thumbnail_url = get_thumbnail(item["thumbnailRenderer"]);
		return true;
	}
	res.browse_id = item["navigationEndpoint"]["browseEndpoint"]["browseId"].string_value();
	if (res.browse_id.empty()) {
		return false;
	}
	res.type = starts_with(res.browse_id, "MPRE")  ? MusicItem::Type::ALBUM
	           : starts_with(res.browse_id, "UC") ? MusicItem::Type::ARTIST
	                                              : MusicItem::Type::PLAYLIST;
	res.title = runs_text(item["title"]);
	res.subtitle = runs_text(item["subtitle"]);
	res.thumbnail_url = get_thumbnail(item["thumbnailRenderer"]);
	return true;
}

void parse_search_shelf_items(RJson contents, MusicSearchResult &res) {
	for (auto content : contents.array_items()) {
		MusicItem item;
		if (parse_list_item(content["musicResponsiveListItemRenderer"], res.filter, item)) {
			res.items.push_back(item);
		}
	}
}

const char *search_params(MusicSearchFilter filter) {
	switch (filter) {
	case MusicSearchFilter::SONGS:
		return "EgWKAQIIAWoMEA4QChADEAQQCRAF";
	case MusicSearchFilter::ALBUMS:
		return "EgWKAQIYAWoMEA4QChADEAQQCRAF";
	case MusicSearchFilter::ARTISTS:
		return "EgWKAQIgAWoMEA4QChADEAQQCRAF";
	case MusicSearchFilter::PLAYLISTS:
	default:
		return "EgeKAQQoAEABagwQDhAKEAMQBBAJEAU=";
	}
}

// appends tracks from a shelf's contents; picks up a trailing continuationItemRenderer token
void parse_track_list(RJson contents, std::vector<MusicTrack> &tracks, std::string &continuation) {
	continuation = "";
	for (auto content : contents.array_items()) {
		if (content.has_key("continuationItemRenderer")) {
			continuation = content["continuationItemRenderer"]["continuationEndpoint"]["continuationCommand"]["token"]
			                   .string_value();
			continue;
		}
		MusicTrack track;
		if (parse_track(content["musicResponsiveListItemRenderer"], track)) {
			tracks.push_back(track);
		}
	}
}

} // namespace

MusicSearchResult music_search(const std::string &query, MusicSearchFilter filter) {
	MusicSearchResult res;
	res.filter = filter;
	debug_music_info("search : " + query);
	res.error = music_api_call("search?prettyPrint=false",
	                           R"("query":")" + json_escape(query) + R"(","params":")" + search_params(filter) + "\"",
	                           [&](RJson json) {
		                           auto sections = json["contents"]["tabbedSearchResultsRenderer"]["tabs"][(size_t)0]
		                                               ["tabRenderer"]["content"]["sectionListRenderer"]["contents"];
		                           for (auto section : sections.array_items()) {
			                           RJson shelf = section["musicShelfRenderer"];
			                           if (!shelf.is_valid()) {
				                           continue;
			                           }
			                           parse_search_shelf_items(shelf["contents"], res);
			                           res.continuation =
			                               shelf["continuations"][(size_t)0]["nextContinuationData"]["continuation"].string_value();
		                           }
	                           });
	if (res.error.empty() && res.items.empty()) {
		res.error = "No results";
	}
	return res;
}

void MusicSearchResult::load_more() {
	if (continuation.empty()) {
		return;
	}
	std::string token = url_encode(continuation);
	std::string new_continuation;
	error = music_api_call("search?prettyPrint=false&type=next&ctoken=" + token + "&continuation=" + token, "",
	                       [&](RJson json) {
		                       RJson shelf = json["continuationContents"]["musicShelfContinuation"];
		                       parse_search_shelf_items(shelf["contents"], *this);
		                       new_continuation =
		                           shelf["continuations"][(size_t)0]["nextContinuationData"]["continuation"].string_value();
	                       });
	continuation = error.empty() ? new_continuation : "";
}

MusicCollection music_load_collection(const std::string &browse_id) {
	MusicCollection res;
	res.browse_id = browse_id;
	res.kind = starts_with(browse_id, "MPRE") ? MusicCollection::Kind::ALBUM
	           : starts_with(browse_id, "UC") ? MusicCollection::Kind::ARTIST
	                                          : MusicCollection::Kind::PLAYLIST;
	debug_music_info("browse : " + browse_id);
	res.error = music_api_call("browse?prettyPrint=false", R"("browseId":")" + json_escape(browse_id) + "\"",
	                           [&](RJson json) {
		                           if (res.kind == MusicCollection::Kind::ARTIST) {
			                           RJson header = json["header"]["musicImmersiveHeaderRenderer"];
			                           if (!header.is_valid()) {
				                           header = json["header"]["musicVisualHeaderRenderer"];
			                           }
			                           res.title = runs_text(header["title"]);
			                           res.subtitle = runs_text(header["monthlyListenerCount"]);
			                           res.thumbnail_url = get_thumbnail(header["thumbnail"]);
			                           auto sections = json["contents"]["singleColumnBrowseResultsRenderer"]["tabs"][(size_t)0]
			                                               ["tabRenderer"]["content"]["sectionListRenderer"]["contents"];
			                           for (auto section : sections.array_items()) {
				                           if (section.has_key("musicShelfRenderer")) { // top songs
					                           RJson shelf = section["musicShelfRenderer"];
					                           std::string unused;
					                           parse_track_list(shelf["contents"], res.tracks, unused);
					                           res.all_songs_browse_id =
					                               shelf["bottomEndpoint"]["browseEndpoint"]["browseId"].string_value();
				                           } else if (section.has_key("musicCarouselShelfRenderer")) {
					                           for (auto content :
					                                section["musicCarouselShelfRenderer"]["contents"].array_items()) {
						                           MusicItem item;
						                           if (parse_two_row_item(content["musicTwoRowItemRenderer"], item) &&
						                               item.type == MusicItem::Type::ALBUM) {
							                           res.releases.push_back(item);
						                           }
					                           }
				                           }
			                           }
			                           return;
		                           }
		                           // album / playlist
		                           RJson two_column = json["contents"]["twoColumnBrowseResultsRenderer"];
		                           RJson header = two_column["tabs"][(size_t)0]["tabRenderer"]["content"]["sectionListRenderer"]
		                                                    ["contents"][(size_t)0]["musicResponsiveHeaderRenderer"];
		                           if (!header.is_valid()) {
			                           header = two_column["tabs"][(size_t)0]["tabRenderer"]["content"]["sectionListRenderer"]
			                                              ["contents"][(size_t)0]["musicEditablePlaylistDetailHeaderRenderer"]
			                                              ["header"]["musicResponsiveHeaderRenderer"];
		                           }
		                           res.title = runs_text(header["title"]);
		                           std::string strapline = runs_text(header["straplineTextOne"]);
		                           res.subtitle = runs_text(header["subtitle"]);
		                           if (!strapline.empty()) {
			                           res.subtitle = strapline + " • " + res.subtitle;
		                           }
		                           res.thumbnail_url = get_thumbnail(header["thumbnail"]);
		                           RJson shelf = two_column["secondaryContents"]["sectionListRenderer"]["contents"][(size_t)0];
		                           RJson shelf_contents = shelf["musicShelfRenderer"]["contents"];
		                           if (!shelf_contents.is_valid()) {
			                           shelf_contents = shelf["musicPlaylistShelfRenderer"]["contents"];
		                           }
		                           parse_track_list(shelf_contents, res.tracks, res.continuation);
	                           });
	if (res.kind == MusicCollection::Kind::ALBUM) {
		for (auto &track : res.tracks) { // album pages don't repeat these per track
			if (track.album.empty()) {
				track.album = res.title;
			}
			if (track.thumbnail_url.empty()) {
				track.thumbnail_url = res.thumbnail_url;
			}
		}
	}
	if (res.error.empty() && res.title.empty() && res.tracks.empty()) {
		res.error = "Failed to load page";
	}
	return res;
}

void MusicCollection::load_more() {
	if (continuation.empty()) {
		return;
	}
	std::string new_continuation;
	error = music_api_call("browse?prettyPrint=false", R"("continuation":")" + json_escape(continuation) + "\"",
	                       [&](RJson json) {
		                       for (auto action : json["onResponseReceivedActions"].array_items()) {
			                       parse_track_list(action["appendContinuationItemsAction"]["continuationItems"], tracks,
			                                        new_continuation);
		                       }
	                       });
	continuation = error.empty() ? new_continuation : "";
}

namespace {
// musicResponsiveListItemRenderer of unknown type (song, album, artist...) -> item
bool parse_any_list_item(RJson item, MusicItem &res) {
	MusicTrack track;
	if (parse_track(item, track)) {
		res.type = MusicItem::Type::SONG;
		res.track = track;
		res.title = track.title;
		res.subtitle = track.artist;
		res.thumbnail_url = track.thumbnail_url;
		return true;
	}
	std::string browse_id = item["navigationEndpoint"]["browseEndpoint"]["browseId"].string_value();
	MusicSearchFilter filter = starts_with(browse_id, "MPRE") ? MusicSearchFilter::ALBUMS
	                           : starts_with(browse_id, "UC") ? MusicSearchFilter::ARTISTS
	                                                          : MusicSearchFilter::PLAYLISTS;
	return parse_list_item(item, filter, res);
}

void parse_home_sections(RJson sections, std::vector<MusicShelf> &shelves) {
	constexpr size_t MAX_ITEMS_PER_SHELF = 10;
	for (auto section : sections.array_items()) {
		RJson carousel = section["musicCarouselShelfRenderer"];
		if (!carousel.is_valid()) {
			continue;
		}
		MusicShelf shelf;
		shelf.title = runs_text(carousel["header"]["musicCarouselShelfBasicHeaderRenderer"]["title"]);
		for (auto content : carousel["contents"].array_items()) {
			if (shelf.items.size() >= MAX_ITEMS_PER_SHELF) {
				break;
			}
			MusicItem item;
			// other renderers (genre buttons, podcast episodes) are skipped
			if (content.has_key("musicTwoRowItemRenderer")) {
				if (parse_two_row_item(content["musicTwoRowItemRenderer"], item)) {
					shelf.items.push_back(item);
				}
			} else if (content.has_key("musicResponsiveListItemRenderer")) {
				if (parse_any_list_item(content["musicResponsiveListItemRenderer"], item)) {
					shelf.items.push_back(item);
				}
			}
		}
		// podcast items have browse ids starting with "MPSP" / "MPED" : not playable as music
		shelf.items.erase(std::remove_if(shelf.items.begin(), shelf.items.end(),
		                                 [](const MusicItem &item) {
			                                 return starts_with(item.browse_id, "MPSP") ||
			                                        starts_with(item.browse_id, "MPED");
		                                 }),
		                  shelf.items.end());
		if (!shelf.title.empty() && !shelf.items.empty()) {
			shelves.push_back(shelf);
		}
	}
}
} // namespace

MusicHomeResult music_load_home() {
	MusicHomeResult res;
	// the signed-out home has few shelves, so the explore page (new releases, trending) is appended
	for (const char *browse_id : {"FEmusic_home", "FEmusic_explore"}) {
		std::string error = music_api_call("browse?prettyPrint=false", R"("browseId":")" + std::string(browse_id) + "\"",
		                                   [&](RJson json) {
			                                   parse_home_sections(
			                                       json["contents"]["singleColumnBrowseResultsRenderer"]["tabs"]
			                                           [(size_t)0]["tabRenderer"]["content"]["sectionListRenderer"]
			                                           ["contents"],
			                                       res.shelves);
		                                   });
		if (error != "") {
			debug_music_error(std::string(browse_id) + " : " + error);
			if (res.error.empty()) {
				res.error = error;
			}
		}
	}
	if (!res.shelves.empty()) {
		res.error = ""; // partial success is fine
	} else if (res.error.empty()) {
		res.error = "Nothing to show";
	}
	return res;
}

std::vector<MusicTrack> music_load_radio(const std::string &video_id, std::string &error) {
	std::vector<MusicTrack> res;
	error = music_api_call("next?prettyPrint=false",
	                       R"("videoId":")" + json_escape(video_id) + R"(","playlistId":"RDAMVM)" +
	                           json_escape(video_id) + R"(","isAudioOnly":true)",
	                       [&](RJson json) {
		                       RJson panel = json["contents"]["singleColumnMusicWatchNextResultsRenderer"]
		                                         ["tabbedRenderer"]["watchNextTabbedResultsRenderer"]["tabs"][(size_t)0]
		                                         ["tabRenderer"]["content"]["musicQueueRenderer"]["content"]
		                                         ["playlistPanelRenderer"];
		                       for (auto content : panel["contents"].array_items()) {
			                       RJson video = content["playlistPanelVideoRenderer"];
			                       if (!video.is_valid()) { // e.g. playlistPanelVideoWrapperRenderer
				                       video = content["playlistPanelVideoWrapperRenderer"]["primaryRenderer"]
				                                      ["playlistPanelVideoRenderer"];
			                       }
			                       MusicTrack track;
			                       track.video_id = video["videoId"].string_value();
			                       if (track.video_id.empty()) {
				                       continue;
			                       }
			                       track.title = runs_text(video["title"]);
			                       auto segments = split_runs(video["longBylineText"]);
			                       if (segments.size() >= 1) {
				                       track.artist = join_runs(segments[0]);
			                       }
			                       if (segments.size() >= 3) { // artist • album • year
				                       track.album = join_runs(segments[1]);
			                       }
			                       track.duration_text = runs_text(video["lengthText"]);
			                       track.thumbnail_url = get_thumbnail(video["thumbnail"]);
			                       res.push_back(track);
		                       }
	                       });
	if (error.empty() && res.empty()) {
		error = "Empty radio";
	}
	return res;
}

std::string music_thumbnail_url(const std::string &url, int size) {
	if (url.find("googleusercontent.com") != std::string::npos || url.find("ggpht.com") != std::string::npos) {
		auto eq = url.rfind('=');
		std::string base = eq == std::string::npos ? url : url.substr(0, eq);
		return base + "=w" + std::to_string(size) + "-h" + std::to_string(size) + "-l90-rj";
	}
	return url;
}

std::string music_video_thumbnail_url(const std::string &video_id) {
	return "https://i.ytimg.com/vi/" + video_id + "/mqdefault.jpg";
}
