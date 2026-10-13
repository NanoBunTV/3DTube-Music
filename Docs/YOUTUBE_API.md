# YouTube / YouTube Music APIs

All requests are POSTs to InnerTube (`/youtubei/v1/...`) with a JSON body containing `context.client`.
YouTube changes these without notice; when playback or browsing breaks, check here first, then compare with
what [yt-dlp](https://github.com/yt-dlp/yt-dlp) and [ytmusicapi](https://github.com/sigma67/ytmusicapi) do.

## Stream URLs (`source/youtube_parser/video.cpp`)

`youtube_load_video_page()` sends two requests to `m.youtube.com/youtubei/v1/` :
- `next` (metadata) : MWEB client, or authenticated ANDROID_VR when signed in
- `player` (stream URLs) : the client chosen by the `player_client` setting (Settings > Advanced). **Never
  authenticated.** If the song isn't playable and the user is signed in, it falls back to authenticated
  ANDROID_VR (e.g. age-restricted songs).

The app uses itag 140 (AAC, ~128 kbps). Opus formats are ignored.

### Client status (last checked 2026-10-13, from a PC in the US)

| Setting | Client | Result |
|---|---|---|
| 2 (default) | `VISIONOS` 0.1 | Full streams for all tested songs |
| 1 | `ANDROID_VR` 1.62.27 | Often `LOGIN_REQUIRED` ("Sign in to confirm you're not a bot") |
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

Search filter params : Songs `EgWKAQIIAWoMEA4QChADEAQQCRAF`, Albums `EgWKAQIYAWoMEA4QChADEAQQCRAF`,
Artists `EgWKAQIgAWoMEA4QChADEAQQCRAF`, community Playlists `EgeKAQQoAEABagwQDhAKEAMQBBAJEAU=`.

The old YouTube home (`FEhype_leaderboard` signed out) now returns nothing, which is why Home was switched to
YouTube Music.

## SponsorBlock

`GET https://sponsor.ajay.app/api/skipSegments/<first 4 hex chars of sha256(videoId)>?categories=[...]&actionType=skip`
(categories : sponsor, selfpromo, interaction, music_offtopic). 404 means no segments.
