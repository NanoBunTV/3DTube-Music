# Architecture

## Source layout (`source/`)

| Path | Purpose |
|---|---|
| `main.cpp`, `scene_switcher.cpp/.hpp` | Main loop, service init, scene stack (`SceneType`, `Intent`). Every frame : `video_update_sponsorblock()`, then the current scene's `*_draw()` |
| `scenes/video_player.cpp` | **The music player.** Playback engine (decode + position threads), queue playback, SponsorBlock skipping, the playing bar shown in every scene, the top-screen now-playing view, and the now-playing scene (`SceneType::VIDEO_PLAYER`, name kept from FourthTube) with tabs Now Playing / Lyrics / Queue / Audio |
| `scenes/search.cpp` | YouTube Music search with Songs / Albums / Artists / Playlists filters. Its search bar is also embedded in Home |
| `scenes/music_browse.cpp` | Album / playlist / artist page (`SceneType::MUSIC_BROWSE`, scene arg = browse id; `account:` + browse id = the user's own playlist, loaded with the sign-in token and never cached) |
| `scenes/home.cpp` | Home : search bar + tabs Home (YouTube Music shelves) and Library (signed-in user's Liked Music and playlists; opens them in MUSIC_BROWSE with arg `account:VL...`) |
| `scenes/watch_history.cpp`, `setting_menu.cpp`, `about.cpp` | Mostly unchanged from FourthTube (history = songs played, recorded by `youtube_load_video_page`) |
| `music/music_parser.cpp/.hpp` | YouTube Music API (WEB_REMIX client) : search, browse (album/playlist/artist), home, radio, lyrics. Types `MusicTrack`, `MusicItem`, `MusicCollection`, `MusicShelf`, `MusicLyrics` |
| `music/queue.cpp/.hpp` | `MusicQueue` : play order, shuffle, repeat (off/all/one). Thread-safe |
| `youtube_parser/` | Regular YouTube API, trimmed to what an audio player needs. `video.cpp` resolves the audio stream, title and channel of a track (`youtube_load_video_page`), `sponsorblock.cpp` builds/parses SponsorBlock requests. FourthTube's channel, search, home, comments and captions parsers were removed |
| `network_decoder/` | Stream downloading and FFmpeg decoding (audio path only is used), thumbnail loader |
| `oauth/` | Google sign-in (TV device-code flow). Use `OAuth::get_valid_access_token()` for requests : it refreshes expired tokens |
| `ui/` | View toolkit (ScrollView, VerticalListView, TabView, TextView, CustomView, SuccinctVideoView...) |
| `data_io/` | Settings, history, string resources (`romfs/gfx/msg/string_resources_*.txt`) |

## Playback flow

1. A screen fills the queue (`MusicQueue::set(tracks, index)` or `add_next`) and calls `music_player_play_queue()`.
   A plain YouTube URL passed to `VideoPlayer_resume(url)` becomes a one-track queue.
2. `play_current_track_wo_lock()` stops the current track, sets `cur_playing_url`, requests the album art and
   queues the async task `load_video_page`.
3. `load_video_page` calls `youtube_load_video_page()` (one `player` request, sent in parallel with the
   SponsorBlock request; visitor data is cached for 30 min) and sets `vid_change_video_request`. Unplayable
   tracks are skipped (max 10 in a row).
4. `decode_thread` initializes `network_decoder` with the audio URL (itag 140 AAC), decodes and feeds NDSP via
   `Util_speaker_*`. `position_thread` updates `vid_current_pos` from the speaker and requests a redraw when the
   displayed second changes (scenes only redraw on `var_need_refresh` in eco mode).
5. End of stream :
   - ended more than 5 s before the duration → the URL was cut off (see YOUTUBE_API.md) : reload with
     `seek_at_init_request` and resume (max 3 per track)
   - repeat-one → seek to 0
   - otherwise `MusicQueue::next(true)`; at the end of the queue, if autoplay is on, `load_radio` appends a radio
     and continues.

Lyrics : while the Lyrics tab is open, `update_lyrics_views()` (UI thread) requests the current track's lyrics
(async task `load_lyrics` → `music_load_lyrics()`); results are matched by video id, so switching tracks while
loading is safe. A failed load is retried when the tab is opened again.

## Threads and locking

- UI thread : scenes, views. Views are only touched from the UI thread.
- `decode_thread`, `position_thread`, stream downloader, livestream initer (player), async task thread
  (`queue_async_task`), thumbnail downloader, misc tasks.
- `VideoPlayer::small_resource_lock` protects player state (`cur_playing_url`, `playing_video_info`, cache).
  It's a libctru `LightLock` and **not recursive** : never call `music_player_*()` / `toggle_pause()` while
  holding it. The now-playing scene draws/updates its views without it for that reason.
- `notice_lock` protects the notice text (set from any thread via `music_player_show_notice`).
- `MusicQueue` has its own internal lock.
- Each other scene has its own `resource_lock` around its views; calling player functions while holding a
  scene's lock is fine.

## UI conventions

- Lists of music items use `SuccinctVideoView` with `set_square_thumbnail(true)` (album art isn't cropped to 16:9).
- Album art URLs are resized with `music_thumbnail_url(url, size)` (googleusercontent `=wN-hN-l90-rj`, JPEG).
- Tap = play from here (the rest of the list is queued after it); long hold (40 frames) = play next.
  After handling a long hold, call `reset_holding_status()` so the release isn't treated as a tap.
- New user-facing strings go in **all 7** `romfs/gfx/msg/string_resources_*.txt` files (en, de, fr, es, it, br =
  Brazilian Portuguese, ja), translated. A missing id shows `[SR Not Found]`. Each file must stay under 16 KB (the
  loader's buffer). Check with `python Docs/tools/check_strings.py` (missing / unused ids, sizes). Files use CRLF;
  some have a UTF-8 BOM, keep it as is.
- Tab labels in the now-playing scene are 80 px wide at font size 0.4 : keep translations short.
