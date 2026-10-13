# YouTube / YouTube Music APIs

All requests are POSTs to InnerTube (`/youtubei/v1/...`) with a JSON body containing `context.client`.
YouTube changes these without notice; when playback or browsing breaks, check here first, then compare with
what [yt-dlp](https://github.com/yt-dlp/yt-dlp) and [ytmusicapi](https://github.com/sigma67/ytmusicapi) do.

## Stream URLs (`source/youtube_parser/video.cpp`)

`youtube_load_video_page()` sends one `player` request to `m.youtube.com/youtubei/v1/`, with the client chosen
by the `player_client` setting (Settings > Advanced). **Never authenticated.** If the song isn't playable and the
user is signed in, it retries with authenticated ANDROID_VR (e.g. age-restricted songs).
- Title, channel name and channel id come from the player response's `videoDetails` (all clients return it;
  auto-generated artist channels are named `<artist> - Topic`, the suffix is stripped).
- `visitorData` comes from `https://www.youtube.com/sw.js_data` and is cached for 30 minutes.
- The SponsorBlock request is sent in parallel with the player request.

Until 2026-10-13 it also sent a `next` request (metadata, comments, suggestions), a second `player` request
(captions) and a returnyoutubedislike.com request for every track; all three were dropped as unused, which
makes tracks start faster.

The app uses itag 140 (AAC, ~128 kbps). Opus formats are ignored.

### Client status (last checked 2026-10-13, from a PC in the US)

| Setting | Client | Result |
|---|---|---|
| 2 (default) | `VISIONOS` 0.1 | Full streams for all tested songs |
| 1 | `ANDROID_VR` 1.62.27 | `LOGIN_REQUIRED` ("Sign in to confirm you're not a bot"), again on 2026-10-13 |
| — | `ANDROID_VR` 1.65.10 (signed-in fallback) | Playable, but cut off after ~1 minute |
| 0 | `ANDROID` 20.10.38 | Playable, but cut off after ~1 minute |

**"Cut off after ~1 minute"** : the first ~1 MB (~66 s of 128 kbps AAC) downloads normally, then every later
byte range returns HTTP 403. This is YouTube's PO-token enforcement. In the app it looked like "the song skips
to the next one after exactly one minute". The player now reloads the URL and resumes when a stream ends more
than 5 s before its duration, but the real fix is using a client that serves full streams.

To re-check clients : `python Docs/tools/probe_player_clients.py <videoId> ...`

### History of breakages

- **2024** : ThirdTube stopped working; fixed by FourthTube (Smu1zel / NCP 3.0).
- **2026-02** : PO token required on the default ANDROID client; FourthTube Beta 34.1 switched clients.
- **2026-10** : ANDROID and ANDROID_VR 1.65.10 streams cut off after ~1 minute; switched the default to
  visionOS (settings key renamed `player_response` → `player_client` so existing settings migrate).

## YouTube Music metadata (`source/music/music_parser.cpp`)

Base URL `https://music.youtube.com/youtubei/v1/`, client `WEB_REMIX` / `1.20251006.03.00`, with
`Origin`/`Referer: https://music.youtube.com`. Works signed out; no auth is sent.

| Feature | Request | Response shape |
|---|---|---|
| Search | `search` + `query` + filter `params` | `contents.tabbedSearchResultsRenderer.tabs[0]...sectionListRenderer.contents[].musicShelfRenderer.contents[].musicResponsiveListItemRenderer` |
| Search, more | `search?type=next&ctoken=T&continuation=T` (token in URL) | `continuationContents.musicShelfContinuation` |
| Album (`MPREb_...`) / playlist (`VL...`) | `browse` + `browseId` | header : `twoColumnBrowseResultsRenderer.tabs[0]...contents[0].musicResponsiveHeaderRenderer`; tracks : `secondaryContents.sectionListRenderer.contents[0].musicShelfRenderer` / `musicPlaylistShelfRenderer` |
| Playlist, more | `browse` + `continuation` (token in body, from a trailing `continuationItemRenderer`) | `onResponseReceivedActions[].appendContinuationItemsAction.continuationItems` |
| Artist (`UC...`) | `browse` + `browseId` | `header.musicImmersiveHeaderRenderer`; "Top songs" `musicShelfRenderer` (`bottomEndpoint` = all-songs playlist); carousels of `musicTwoRowItemRenderer` |
| Home | `browse` `FEmusic_home` + `FEmusic_explore` | `singleColumnBrowseResultsRenderer...sectionListRenderer.contents[].musicCarouselShelfRenderer`. Signed out, `FEmusic_home` has only ~2 shelves and its continuation just returns the same page, so explore's shelves are appended |
| Radio | `next` + `videoId` + `playlistId: "RDAMVM<videoId>"` | `...musicQueueRenderer.content.playlistPanelRenderer.contents[].playlistPanelVideoRenderer` (~50 tracks) |
| Lyrics | `next` + `videoId` → the tab whose `browseEndpoint...pageType` is `MUSIC_PAGE_TYPE_TRACK_LYRICS` gives a `browseId` (`MPLYt_...`, not derivable from the video id) → `browse` | `contents.sectionListRenderer.contents[].musicDescriptionShelfRenderer` : `description` (text, `\n`-separated), `footer` ("Source: Musixmatch"). No lyrics : `contents.messageRenderer` ("Lyrics not available"). Music videos usually have none; songs (audio tracks) usually do. Plain lyrics only (synced lyrics need another client) |

Search filter params : Songs `EgWKAQIIAWoMEA4QChADEAQQCRAF`, Albums `EgWKAQIYAWoMEA4QChADEAQQCRAF`,
Artists `EgWKAQIgAWoMEA4QChADEAQQCRAF`, community Playlists `EgeKAQQoAEABagwQDhAKEAMQBBAJEAU=`.

The old YouTube home (`FEhype_leaderboard` signed out) now returns nothing, which is why Home was switched to
YouTube Music.

## Signed-in library (`music_load_library()`, `music_load_account_playlist()` in `source/music/music_parser.cpp`)

The app signs in with YouTube's **TV device flow** (client id in `source/oauth/oauth.cpp`, scope
`https://www.googleapis.com/auth/youtube`). Checked 2026-10-13 with `Docs/tools/probe_library.py` :

| Request (with `Authorization: Bearer <token>`) | Result |
|---|---|
| WEB_REMIX (music.youtube.com) `FEmusic_liked_playlists`, `FEmusic_library_landing`, `VLLM` | **HTTP 400 INVALID_ARGUMENT** : YouTube Music's own API rejects TV tokens (same as ytmusicapi's TV-client OAuth since late 2024) |
| ANDROID_MUSIC `FEmusic_liked_playlists` | HTTP 400 |
| ANDROID_VR 1.62.27 (youtube.com) `FElibrary` | OK : `...sectionListRenderer.contents[].shelfRenderer.content.verticalListRenderer.items[].compactPlaylistRenderer` (`title`, `videoCountText`, `navigationEndpoint.browseEndpoint.browseId` = `VL<id>`, `thumbnail.thumbnails`). All playlists in one page (39 for the test account), owned and saved. No Liked Music entry (it has "Liked videos" `VLLL`) |
| ANDROID_VR `VLLM` (Liked Music), `VL<playlist id>` | OK : header `header.playlistHeaderRenderer` (`title`, `ownerText`, `numVideosText`); items `...sectionListRenderer.contents[].playlistVideoListRenderer.contents[].playlistVideoRenderer` (`videoId`, `title`, `shortBylineText` = channel, often `<artist> - Topic`, `lengthText`, `isPlayable`); 20 per page, next page token `playlistVideoListRenderer.continuations[0].nextContinuationData.continuation` |
| ANDROID_VR `browse` + `continuation` | OK : `continuationContents.playlistVideoListContinuation` (`contents`, `continuations`) |
| ANDROID_VR `FEplaylist_aggregation` | empty |
| TVHTML5 `FEmusic_liked_playlists`, `FElibrary`, `VLLM` | OK, but TV-style `tvBrowseRenderer` / `tileRenderer` shapes. `FEmusic_liked_playlists` here returns the actual YouTube Music library list (incl. Liked Music); a possible alternative if ANDROID_VR stops working |

Access tokens last 1 hour. `OAuth::get_valid_access_token()` refreshes them (when older than 50 minutes, on first
use after launch, or after an HTTP 401) and only signs the user out if the refresh token is rejected
(`invalid_grant`).

## SponsorBlock

`GET https://sponsor.ajay.app/api/skipSegments/<first 4 hex chars of sha256(videoId)>?categories=[...]&actionType=skip`
(categories : sponsor, selfpromo, interaction, music_offtopic). 404 means no segments. Built by
`youtube_sponsorblock_url()`, parsed by `youtube_parse_sponsor_segments()`.
