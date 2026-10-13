"""Checks which InnerTube endpoints return the signed-in user's library with the app's sign-in token.

The app signs in with YouTube's TV device flow (same client id as source/oauth/oauth.cpp). This script does the
same sign-in (you enter a code at google.com/device), then tries several library requests and prints, for each,
the HTTP status and the playlists/items it found. The token is kept in memory only : it's never printed or saved.

Usage (from a PC, no dependencies) : python Docs/tools/probe_library.py
"""
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

CLIENT_ID = "652469312169-4lvs9bnhr9lpns9v451j5oivd81vjvu1.apps.googleusercontent.com"
CLIENT_SECRET = "3fTWrBJI5Uojm1TK7_iJCW5Z"
SCOPE = "https://www.googleapis.com/auth/youtube"
UA = "Mozilla/5.0 (Linux; Android 12; Quest 2) AppleWebKit/537.36"


def post_form(url, fields):
    req = urllib.request.Request(url, urllib.parse.urlencode(fields).encode(),
                                 {"Content-Type": "application/x-www-form-urlencoded", "User-Agent": UA})
    try:
        with urllib.request.urlopen(req) as r:
            return r.status, json.load(r)
    except urllib.error.HTTPError as e:
        return e.code, json.loads(e.read() or b"{}")


def sign_in():
    status, data = post_form("https://www.youtube.com/o/oauth2/device/code",
                             {"client_id": CLIENT_ID, "scope": SCOPE, "device_id": "probe-%d" % time.time(),
                              "device_model": "NINTENDO3DS"})
    if status != 200:
        sys.exit("device code request failed : %d %s" % (status, data))
    print("\n  Go to %s and enter the code :  %s\n" % (data["verification_url"], data["user_code"]), flush=True)
    interval = max(5, data.get("interval", 5))
    deadline = time.time() + data.get("expires_in", 1800)
    while time.time() < deadline:
        time.sleep(interval)
        status, tok = post_form("https://www.youtube.com/o/oauth2/token",
                                {"client_id": CLIENT_ID, "client_secret": CLIENT_SECRET, "code": data["device_code"],
                                 "grant_type": "http://oauth.net/grant_type/device/1.0"})
        if "access_token" in tok:
            print("  Signed in.\n", flush=True)
            return tok["access_token"]
        if tok.get("error") == "slow_down":
            interval += 5
        elif tok.get("error") not in ("authorization_pending", None):
            sys.exit("sign-in failed : %s" % tok.get("error"))
    sys.exit("sign-in timed out")


CLIENTS = {
    "WEB_REMIX": ("https://music.youtube.com/youtubei/v1/",
                  {"clientName": "WEB_REMIX", "clientVersion": "1.20251006.03.00"},
                  {"Origin": "https://music.youtube.com", "Referer": "https://music.youtube.com/"}),
    "ANDROID_VR": ("https://www.youtube.com/youtubei/v1/",
                   {"clientName": "ANDROID_VR", "clientVersion": "1.62.27"}, {}),
    "ANDROID_VR_165": ("https://www.youtube.com/youtubei/v1/",
                       {"clientName": "ANDROID_VR", "clientVersion": "1.65.10", "deviceMake": "Oculus",
                        "deviceModel": "Quest 3", "androidSdkVersion": "34", "osName": "Android", "osVersion": "14"},
                       {}),
    "TVHTML5": ("https://www.youtube.com/youtubei/v1/",
                {"clientName": "TVHTML5", "clientVersion": "7.20250923.13.00"}, {}),
    "ANDROID_MUSIC": ("https://music.youtube.com/youtubei/v1/",
                      {"clientName": "ANDROID_MUSIC", "clientVersion": "7.27.52", "androidSdkVersion": "30",
                       "osName": "Android", "osVersion": "11"}, {}),
}


def call(token, client, endpoint, body):
    base, ctx, extra = CLIENTS[client]
    body = dict(body, context={"client": dict(ctx, hl="en", gl="US")})
    headers = dict(extra, **{"Content-Type": "application/json", "Authorization": "Bearer " + token,
                             "User-Agent": UA})
    req = urllib.request.Request(base + endpoint + "?prettyPrint=false", json.dumps(body).encode(), headers)
    try:
        with urllib.request.urlopen(req) as r:
            return r.status, json.load(r)
    except urllib.error.HTTPError as e:
        try:
            return e.code, json.loads(e.read())
        except Exception:
            return e.code, {}


def text_of(obj):
    if isinstance(obj, dict):
        if "simpleText" in obj:
            return obj["simpleText"]
        if "runs" in obj:
            return "".join(r.get("text", "") for r in obj["runs"])
        if "content" in obj and isinstance(obj["content"], str):
            return obj["content"]
    return ""


def summarize(data):
    """renderer types containing playlist/browse ids, with a title, plus the top-level structure"""
    found, renderers = [], {}

    def walk(o, parent_key):
        if isinstance(o, dict):
            for k, v in o.items():
                if k.endswith("Renderer") or k.endswith("ViewModel"):
                    renderers[k] = renderers.get(k, 0) + 1
                walk(v, k)
            ids = []
            for key in ("playlistId", "browseId"):
                if isinstance(o.get(key), str):
                    ids.append(o[key])
            for k in ("title", "headline", "primaryText"):
                t = text_of(o.get(k)) if k in o else ""
                if t:
                    nav = json.dumps(o)
                    for prefix in ('"browseId": "VL', '"playlistId": "', '"browseId": "MPRE', '"browseId": "FE'):
                        i = nav.find(prefix)
                        if i != -1:
                            ident = nav[i + len(prefix) - (2 if "VL" in prefix else 0):].split('"')[0]
                            found.append((parent_key, t[:60], prefix.split('"')[1] + ":" + ident[:40]))
                            break
                    break
        elif isinstance(o, list):
            for v in o:
                walk(v, parent_key)

    walk(data, "")
    return found, renderers


PROBES = [
    ("WEB_REMIX", "browse", {"browseId": "FEmusic_liked_playlists"}),
    ("WEB_REMIX", "browse", {"browseId": "FEmusic_library_landing"}),
    ("WEB_REMIX", "browse", {"browseId": "VLLM"}),
    ("ANDROID_MUSIC", "browse", {"browseId": "FEmusic_liked_playlists"}),
    ("ANDROID_VR", "browse", {"browseId": "FElibrary"}),
    ("ANDROID_VR", "browse", {"browseId": "FEplaylist_aggregation"}),
    ("ANDROID_VR", "browse", {"browseId": "VLLM"}),
    ("ANDROID_VR", "browse", {"browseId": "VLLL"}),
    ("ANDROID_VR_165", "browse", {"browseId": "FElibrary"}),
    ("TVHTML5", "browse", {"browseId": "FEmusic_liked_playlists"}),
    ("TVHTML5", "browse", {"browseId": "FElibrary"}),
    ("TVHTML5", "browse", {"browseId": "FEplaylist_aggregation"}),
    ("TVHTML5", "browse", {"browseId": "VLLM"}),
]


def find_continuation(data):
    """first continuation token in a response (continuationItemRenderer or nextContinuationData)"""
    s = json.dumps(data)
    for marker in ('"continuationCommand": {"token": "', '"continuation": "'):
        i = s.find(marker)
        if i != -1:
            return s[i + len(marker):].split('"')[0]
    return ""


def dump(token, out_dir):
    """--dump DIR : saves raw ANDROID_VR responses (library, Liked Music + its next page, one playlist) to DIR"""
    import os
    os.makedirs(out_dir, exist_ok=True)

    def save(name, data):
        with open(os.path.join(out_dir, name + ".json"), "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False, indent=1)
        print("  saved", name)

    status, library = call(token, "ANDROID_VR", "browse", {"browseId": "FElibrary"})
    save("library", library)
    status, liked = call(token, "ANDROID_VR", "browse", {"browseId": "VLLM"})
    save("liked_music", liked)
    token_next = find_continuation(liked)
    if token_next:
        status, liked2 = call(token, "ANDROID_VR", "browse", {"continuation": token_next})
        print("  liked music continuation -> HTTP", status)
        save("liked_music_continuation", liked2)
    s = json.dumps(library)
    i = s.find('"browseId": "VLPL')
    if i != -1:
        browse_id = s[i + len('"browseId": "'):].split('"')[0]
        status, playlist = call(token, "ANDROID_VR", "browse", {"browseId": browse_id})
        save("playlist", playlist)


def main():
    token = sign_in()
    if len(sys.argv) >= 3 and sys.argv[1] == "--dump":
        dump(token, sys.argv[2])
        print("Done. The token is discarded now.")
        return
    for client, endpoint, body in PROBES:
        status, data = call(token, client, endpoint, body)
        print("=" * 100)
        print("%s %s %s -> HTTP %d" % (client, endpoint, json.dumps(body), status))
        if "error" in data:
            print("  error :", data["error"].get("message"), data["error"].get("status"))
            continue
        print("  top-level keys :", list(data.keys()))
        contents = data.get("contents", {})
        print("  contents keys  :", list(contents.keys()) if isinstance(contents, dict) else type(contents).__name__)
        found, renderers = summarize(data)
        top = sorted(renderers.items(), key=lambda x: -x[1])[:12]
        print("  renderers      :", ", ".join("%s x%d" % r for r in top))
        print("  items with ids : %d" % len(found))
        for parent, title, ident in found[:15]:
            print("    - [%s] %s  (%s)" % (parent, title, ident))
        sys.stdout.flush()
    print("=" * 100)
    print("Done. The token is discarded now.")


if __name__ == "__main__":
    main()
