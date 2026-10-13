#include "internal_common.hpp"
#include "parser.hpp"
#include <algorithm>
#include <mbedtls/sha256.h>

// SponsorBlock (https://sponsor.ajay.app) skip segments.
// Uses the k-anonymity endpoint: only the first 4 hex chars of sha256(video id) are sent, and the
// response contains every video sharing that prefix, so the server never learns which video is playing.

#define SPONSORBLOCK_API_URL "https://sponsor.ajay.app/api/skipSegments/"
// ["sponsor","selfpromo","interaction","music_offtopic"], URL-encoded
#define SPONSORBLOCK_CATEGORIES                                                                                        \
	"%5B%22sponsor%22%2C%22selfpromo%22%2C%22interaction%22%2C%22music_offtopic%22%5D"

static std::string sha256_hex_prefix(const std::string &input, size_t prefix_len) {
	unsigned char hash[32];
	mbedtls_sha256_ret((const unsigned char *)input.c_str(), input.size(), hash, 0);
	static const char *hex = "0123456789abcdef";
	std::string res;
	for (size_t i = 0; i < 32 && res.size() < prefix_len; i++) {
		res.push_back(hex[hash[i] >> 4]);
		res.push_back(hex[hash[i] & 0xF]);
	}
	return res.substr(0, prefix_len);
}

std::string youtube_sponsorblock_url(const std::string &video_id) {
	return SPONSORBLOCK_API_URL + sha256_hex_prefix(video_id, 4) +
	       "?categories=" SPONSORBLOCK_CATEGORIES "&actionType=skip";
}

void youtube_parse_sponsor_segments(YouTubeVideoDetail &res, const std::string &response) {
	res.sponsor_segments.clear();
	// SponsorBlock answers 404 (with a non-JSON body) when no segments exist
	if (res.id.empty() || res.is_livestream || response.empty() || response[0] != '[') {
		return;
	}

	std::vector<char> data(response.begin(), response.end());
	data.push_back('\0');
	parse_json_destructive(
	    &data[0],
	    [&](Document &, RJson json) {
		    for (auto video : json.array_items()) {
			    if (video["videoID"].string_value() != res.id) {
				    continue;
			    }
			    for (auto segment : video["segments"].array_items()) {
				    auto range = segment["segment"].array_items();
				    if (range.size() != 2) {
					    continue;
				    }
				    YouTubeVideoDetail::SponsorSegment cur;
				    cur.start = range[0].double_value();
				    cur.end = range[1].double_value();
				    cur.category = segment["category"].string_value();
				    if (cur.end - cur.start >= 1.0) { // ignore zero-length/point segments
					    res.sponsor_segments.push_back(cur);
				    }
			    }
		    }
		    std::sort(res.sponsor_segments.begin(), res.sponsor_segments.end(),
		              [](const auto &a, const auto &b) { return a.start < b.start; });
		    debug_info("sponsorblock: " + std::to_string(res.sponsor_segments.size()) + " segment(s)");
	    },
	    [&](const std::string &error) { debug_warning("sponsorblock: " + error); });
}
