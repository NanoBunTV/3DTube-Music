"""Checks how to make a liked song appear in Liked Music (VLLM), not only in Liked videos (VLLL).

Signs in like the app (TV device flow, you enter a code at google.com/device), finds a song (ATV) with an anonymous
YouTube Music search, then (skipped if you already liked it) :
 1. `like/like` with each client; music clients (WEB_REMIX, ANDROID_MUSIC, IOS_MUSIC) reject the TV token with 400
 2. ANDROID_VR `like/like`, wait, then search all of VLLM and VLLL for the song
 3. ANDROID_VR `browse/edit_playlist` ACTION_ADD_VIDEO on playlist "LM" directly, and search VLLM again
Every change is undone (remove from LM, removelike). The token is kept in memory only.

Usage : python Docs/tools/probe_like_music.py ["search query"]
"""
import json
import os
import sys
import time
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe_library as lib  # noqa: E402  (sign-in and request helpers)

lib.CLIENTS.update({
    "IOS_MUSIC": ("https://music.youtube.com/youtubei/v1/",
                  {"clientName": "IOS_MUSIC", "clientVersion": "7.27.0", "deviceMake": "Apple",
                   "deviceModel": "iPhone16,2", "osName": "iOS", "osVersion": "18.1.0.22B83"}, {}),
})
MUSIC_CLIENTS = ["WEB_REMIX", "ANDROID_MUSIC", "IOS_MUSIC"]
SONGS_FILTER = "EgWKAQIIAWoKEAkQBRAKEAMQBA%3D%3D"


def anonymous_search(query, params):
    body = {"query": query, "params": params,
            "context": {"client": {"clientName": "WEB_REMIX", "clientVersion": "1.20251006.03.00", "hl": "en"}}}
    req = urllib.request.Request("https://music.youtube.com/youtubei/v1/search?prettyPrint=false",
                                 json.dumps(body).encode(),
                                 {"Content-Type": "application/json", "User-Agent": lib.UA,
                                  "Origin": "https://music.youtube.com", "Referer": "https://music.youtube.com/"})
    with urllib.request.urlopen(req) as r:
        s = r.read().decode()
    i = s.find('"videoId":"')
    return s[i + 11:].split('"')[0] if i != -1 else ""


def position_in_playlist(token, browse_id, video_id, max_pages=40):
    """1-based position of video_id in the whole playlist, 0 if absent; also returns the number of items seen"""
    status, data = lib.call(token, "ANDROID_VR", "browse", {"browseId": browse_id})
    seen = 0
    for _ in range(max_pages):
        s = json.dumps(data)
        ids = [part.split('"')[0] for part in s.split('"playlistVideoRenderer": {"videoId": "')[1:]]
        if video_id in ids:
            return seen + ids.index(video_id) + 1, seen + len(ids)
        seen += len(ids)
        cont = lib.find_continuation(data)
        if not cont or not ids:
            break
        status, data = lib.call(token, "ANDROID_VR", "browse", {"continuation": cont})
    return 0, seen


def like_status(token, video_id):
    s = json.dumps(lib.call(token, "ANDROID_VR", "next", {"videoId": video_id})[1])
    return "LIKE" if '"likeStatus": "LIKE"' in s else "INDIFFERENT" if '"likeStatus": "INDIFFERENT"' in s else "?"


def report(token, video_id, label):
    lm, lm_n = position_in_playlist(token, "VLLM", video_id)
    ll, ll_n = position_in_playlist(token, "VLLL", video_id)
    print("   %-38s Liked Music : %s (of %d seen) | Liked videos : %s (of %d seen)" % (
        label, "#%d" % lm if lm else "absent", lm_n, "#%d" % ll if ll else "absent", ll_n), flush=True)


def show(label, status, data):
    err = data.get("error", {}).get("message", "") if isinstance(data, dict) else ""
    print("   %s -> HTTP %d %s" % (label, status, err[:100]), flush=True)


def main():
    query = sys.argv[1] if len(sys.argv) > 1 else "Earth Wind Fire September"
    video_id = anonymous_search(query, SONGS_FILTER)
    print("song :", video_id)
    token = lib.sign_in()
    initial = like_status(token, video_id)
    print("initial like status :", initial)
    report(token, video_id, "before")
    if initial != "INDIFFERENT":
        sys.exit("already liked or unknown status : pick another song with a query argument")

    print("=" * 80)
    for client in MUSIC_CLIENTS:
        show(client + " like/like", *lib.call(token, client, "like/like", {"target": {"videoId": video_id}}))

    print("=" * 80)
    show("ANDROID_VR like/like", *lib.call(token, "ANDROID_VR", "like/like", {"target": {"videoId": video_id}}))
    report(token, video_id, "right after like")
    time.sleep(20)
    report(token, video_id, "20 s after like")

    print("=" * 80)
    show("edit_playlist LM ACTION_ADD_VIDEO", *lib.call(token, "ANDROID_VR", "browse/edit_playlist", {
        "playlistId": "LM", "actions": [{"action": "ACTION_ADD_VIDEO", "addedVideoId": video_id}]}))
    time.sleep(5)
    report(token, video_id, "after adding to LM")

    print("=" * 80)
    show("edit_playlist LM ACTION_REMOVE_VIDEO_BY_VIDEO_ID", *lib.call(token, "ANDROID_VR", "browse/edit_playlist", {
        "playlistId": "LM", "actions": [{"action": "ACTION_REMOVE_VIDEO_BY_VIDEO_ID", "removedVideoId": video_id}]}))
    show("ANDROID_VR like/removelike", *lib.call(token, "ANDROID_VR", "like/removelike",
                                                 {"target": {"videoId": video_id}}))
    time.sleep(5)
    print("final like status :", like_status(token, video_id))
    report(token, video_id, "after undo")
    print("=" * 80)
    print("Done. The token is discarded now.")


if __name__ == "__main__":
    main()
