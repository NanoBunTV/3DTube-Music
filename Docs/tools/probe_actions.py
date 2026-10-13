"""Checks the signed-in write actions (like a song, add it to a playlist) with the app's sign-in token.

Signs in like the app (TV device flow, you enter a code at google.com/device), then with the ANDROID_VR client :
 1. `next` : how the like status of a song is reported
 2. `playlist/get_add_to_playlist` : the playlists a song can be added to
 3. `like/like` then `like/removelike` on the test song (skipped if you already liked it), re-checking the status
 4. `playlist/create` a temporary private playlist, `browse/edit_playlist` add + remove the song,
    `playlist/delete` the playlist
Every change is undone. The token is kept in memory only.

Usage : python Docs/tools/probe_actions.py [videoId] [--dump DIR]   (--dump saves the raw `next` and
get_add_to_playlist responses to DIR; they contain your playlist names)
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe_library as lib  # noqa: E402  (sign-in and request helpers)


def find_key_paths(o, keys, path="", out=None):
    if out is None:
        out = []
    if isinstance(o, dict):
        for k, v in o.items():
            if k in keys and not isinstance(v, (dict, list)):
                out.append((path + "." + k, v))
            find_key_paths(v, keys, path + "." + k, out)
    elif isinstance(o, list):
        for i, v in enumerate(o):
            find_key_paths(v, keys, path + "[%d]" % i, out)
    return out


def like_status(token, video_id, label, dump_dir=None):
    status, data = lib.call(token, "ANDROID_VR", "next", {"videoId": video_id})
    print("next (%s) -> HTTP %d" % (label, status))
    if dump_dir:
        with open(os.path.join(dump_dir, "next_%s.json" % label), "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=1)
    found = find_key_paths(data, {"likeStatus", "isToggled", "likeCountIfLiked", "toggled"})
    for path, value in found[:12]:
        print("   %s = %s" % (path[-150:], value))
    s = json.dumps(data)
    return "LIKE" if '"likeStatus": "LIKE"' in s else "INDIFFERENT" if '"likeStatus": "INDIFFERENT"' in s else "?"


def show(label, status, data):
    err = data.get("error", {}).get("message") if isinstance(data, dict) else None
    print("%s -> HTTP %d%s" % (label, status, " : " + err if err else ""))


def main():
    args = [a for a in sys.argv[1:]]
    dump_dir = None
    if "--dump" in args:
        dump_dir = args[args.index("--dump") + 1]
        args = args[:args.index("--dump")]
        os.makedirs(dump_dir, exist_ok=True)
    video_id = args[0] if args else "BSTsnWoslP4"
    token = lib.sign_in()

    print("=" * 80)
    initial = like_status(token, video_id, "initial", dump_dir)
    print("initial like status :", initial)

    print("=" * 80)
    status, data = lib.call(token, "ANDROID_VR", "playlist/get_add_to_playlist", {"videoIds": [video_id]})
    show("get_add_to_playlist", status, data)
    if dump_dir:
        with open(os.path.join(dump_dir, "get_add_to_playlist.json"), "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=1)
    options = find_key_paths(data, {"playlistId", "containsSelectedVideos", "privacy"})
    print("   %d playlistId/containsSelectedVideos/privacy values, e.g. :" % len(options))
    for path, value in options[:6]:
        print("   %s = %s" % (path[-120:], value))

    print("=" * 80)
    if initial == "INDIFFERENT":
        status, data = lib.call(token, "ANDROID_VR", "like/like", {"target": {"videoId": video_id}})
        show("like/like", status, data)
        print("status after like :", like_status(token, video_id, "liked"))
        status, data = lib.call(token, "ANDROID_VR", "like/removelike", {"target": {"videoId": video_id}})
        show("like/removelike", status, data)
        print("status after removelike :", like_status(token, video_id, "unliked"))
    else:
        print("skipping the like test (status is %s)" % initial)

    print("=" * 80)
    status, data = lib.call(token, "ANDROID_VR", "playlist/create",
                            {"title": "3DTube Music probe (temporary)", "privacyStatus": "PRIVATE"})
    show("playlist/create", status, data)
    playlist_id = data.get("playlistId", "")
    print("   created playlist id :", playlist_id or "(none)", "| top-level keys :", list(data.keys()))
    if playlist_id:
        status, data = lib.call(token, "ANDROID_VR", "browse/edit_playlist", {
            "playlistId": playlist_id, "actions": [{"action": "ACTION_ADD_VIDEO", "addedVideoId": video_id}]})
        show("edit_playlist ACTION_ADD_VIDEO", status, data)
        print("   response keys :", list(data.keys()), "| status :", data.get("status"))
        status, data = lib.call(token, "ANDROID_VR", "playlist/get_add_to_playlist", {"videoIds": [video_id]})
        s = json.dumps(data)
        i = s.find(playlist_id)
        print("   get_add_to_playlist around the new playlist :", s[max(0, i - 200):i + 300] if i != -1 else "(not listed)")
        status, data = lib.call(token, "ANDROID_VR", "browse/edit_playlist", {
            "playlistId": playlist_id,
            "actions": [{"action": "ACTION_REMOVE_VIDEO_BY_VIDEO_ID", "removedVideoId": video_id}]})
        show("edit_playlist ACTION_REMOVE_VIDEO_BY_VIDEO_ID", status, data)
        status, data = lib.call(token, "ANDROID_VR", "playlist/delete", {"playlistId": playlist_id})
        show("playlist/delete", status, data)
    print("=" * 80)
    print("Done. The token is discarded now.")


if __name__ == "__main__":
    main()
