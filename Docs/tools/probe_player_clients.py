"""Checks which InnerTube player clients return audio streams that can be downloaded in full.

YouTube sometimes serves only the first ~1 minute of a stream (HTTP 403 for later byte ranges) when a client
would need a PO token. This script requests the player response for each client the app supports, then
tries to download a small range at 50% and 95% of the itag 140 (AAC) audio stream.

Usage (from a PC, no dependencies):  python probe_player_clients.py <videoId> [<videoId> ...]
Output per client: OK 50%: ok 95%: ok  -> full streams work
                   OK 50%: 403 ...     -> stream is cut off (PO token enforcement)
                   LOGIN_REQUIRED      -> bot check / sign-in required for this client
Results can differ by IP address, so a pass here doesn't guarantee the 3DS gets the same result.
Keep the client definitions in sync with the player request in source/youtube_parser/video.cpp.
"""
import json
import sys
import urllib.request


def get_visitor_data():
    # same source as extractVisitorData() in source/youtube_parser/video.cpp
    req = urllib.request.Request("https://www.youtube.com/sw.js_data",
                                 headers={"Origin": "https://www.youtube.com", "Referer": "https://www.youtube.com/",
                                          "User-Agent": "Mozilla/5.0"})
    text = urllib.request.urlopen(req).read().decode()
    if text.startswith(")]}'"):
        text = text[len(")]}'\n"):]
    return json.loads(text)[0][2][0][0][13]


CLIENTS = {
    "ANDROID_VR (signed-in fallback)": {
        "clientName": "ANDROID_VR", "clientVersion": "1.65.10", "deviceMake": "Oculus", "deviceModel": "Quest 3",
        "androidSdkVersion": "34", "osName": "Android", "osVersion": "14",
        "userAgent": "com.google.android.apps.youtube.vr.oculus/1.65.10 (Linux; U; Android 14; eureka-user Build/UP1A.231005.007.A1) gzip"},
    "ANDROID_VR (setting 1)": {
        "clientName": "ANDROID_VR", "clientVersion": "1.62.27", "deviceMake": "Oculus", "deviceModel": "Quest 3",
        "androidSdkVersion": "32", "osName": "Android", "osVersion": "12L",
        "userAgent": "com.google.android.apps.youtube.vr.oculus/1.62.27 (Linux; U; Android 12L; eureka-user Build/SQ3A.220605.009.A1) gzip"},
    "VISIONOS (setting 2, default)": {
        "clientName": "VISIONOS", "clientVersion": "0.1", "deviceMake": "Apple", "deviceModel": "iPhone16,2",
        "osName": "iPhone", "osVersion": "2.2.22N842",
        "userAgent": "com.google.ios.youtube/20.10.4 (iPhone16,2; U; CPU iOS 18_3_2 like Mac OS X;)"},
    "ANDROID (setting 0)": {
        "clientName": "ANDROID", "clientVersion": "20.10.38", "deviceMake": "Apple", "deviceModel": "iPhone9,1",
        "osName": "iPhone", "osVersion": "15.0.0.19A346",
        "userAgent": "com.google.ios.youtube/19.01.1 (iPhone9,1; U; CPU iOS 15_0_0 like Mac OS X;)"},
}


def probe(video_id, name, client, visitor_data):
    client = dict(client, hl="en", gl="US", visitorData=visitor_data)
    body = json.dumps({"videoId": video_id, "context": {"client": client},
                       "playbackContext": {"contentPlaybackContext": {"signatureTimestamp": "0"}},
                       "contentCheckOk": True, "racyCheckOk": True}).encode()
    req = urllib.request.Request("https://m.youtube.com/youtubei/v1/player?prettyPrint=false", body,
                                 {"Content-Type": "application/json", "User-Agent": client["userAgent"],
                                  "X-Goog-Visitor-Id": visitor_data})
    data = json.load(urllib.request.urlopen(req))
    status = data.get("playabilityStatus", {})
    formats = [f for f in data.get("streamingData", {}).get("adaptiveFormats", []) if f.get("itag") == 140]
    if not formats:
        print(video_id, name, status.get("status"), status.get("reason", "")[:60])
        return
    url, length = formats[0]["url"], int(formats[0]["contentLength"])
    results = []
    for fraction in (0.5, 0.95):
        start = int(length * fraction)
        try:
            urllib.request.urlopen(urllib.request.Request(url + "&range=%d-%d" % (start, start + 1023)))
            results.append("ok")
        except Exception as e:  # noqa: BLE001
            results.append(str(e)[11:14])
    print(video_id, name, "OK", "50%:", results[0], "95%:", results[1])


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    visitor = get_visitor_data()
    for vid in sys.argv[1:]:
        for client_name, client_def in CLIENTS.items():
            probe(vid, client_name, client_def, visitor)
