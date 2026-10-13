# Architecture

## Source layout (`source/`)

| Path | Purpose |
|---|---|
| `main.cpp`, `scene_switcher.cpp/.hpp` | Main loop, service init, scene stack (`SceneType`, `Intent`). Every frame : `video_update_sponsorblock()`, then the current scene's `*_draw()` |
| `scenes/video_player.cpp` | **The music player.** Playback engine (decode + position threads), queue playback, SponsorBlock skipping, the playing bar shown in every scene, the top-screen now-playing view, and the now-playing scene (`SceneType::VIDEO_PLAYER`, name kept from FourthTube) |
| `scenes/search.cpp` | YouTube Music search with Songs / Albums / Artists / Playlists filters. Its search bar is also embedded in Home |
| `scenes/music_browse.cpp` | Album / playlist / artist page (`SceneType::MUSIC_BROWSE`, scene arg = browse id) |
| `scenes/home.cpp` | Home : YouTube Music shelves (first tab), plus FourthTube's channel subscriptions and new-videos tabs |
| `scenes/channel.cpp`, `watch_history.cpp`, `setting_menu.cpp`, `about.cpp` | Mostly unchanged from FourthTube |
| `music/music_parser.cpp/.hpp` | YouTube Music API (WEB_REMIX client) : search, browse (album/playlist/artist), home, radio. Types `MusicTrack`, `MusicItem`, `MusicCollection`, `MusicShelf` |
| `music/queue.cpp/.hpp` | `MusicQueue` : play order, shuffle, repeat (off/all/one). Thread-safe |
| `youtube_parser/` | FourthTube's YouTube parser. `video.cpp` resolves stream URLs (`youtube_load_video_page`), `sponsorblock.cpp` fetches skip segments |
| `network_decoder/` | Stream downloading and FFmpeg decoding (audio path only is used), thumbnail loader |
| `oauth/` | Google sign-in (TV device-code flow) |
| `ui/` | View toolkit (ScrollView, VerticalListView, TabView, TextView, CustomView, SuccinctVideoView...) |
| `data_io/` | Settings, history, subscriptions, string resources (`romfs/gfx/msg/string_resources_*.txt`) |

## Playback flow

1. A screen fills the queue (`MusicQueue::set(tracks, index)` or `add_next`) and calls `music_player_play_queue()`.
   A plain YouTube URL passed to `VideoPlayer_resume(url)` becomes a one-track queue.
2. `play_current_track_wo_lock()` stops the current track, sets `cur_playing_url`, requests the album art and
   queues the async task `load_video_page`.
3. `load_video_page` calls `youtube_load_video_page()` (player + next requests, SponsorBlock segments) and sets
   `vid_change_video_request`. Unplayable tracks are skipped (max 10 in a row).
4. `decode_thread` initializes `network_decoder` with the audio URL (itag 140 AAC), decodes and feeds NDSP via
   `Util_speaker_*`. `position_thread` updates `vid_current_pos` from the speaker and requests a redraw when the
   displayed second changes (scenes only redraw on `var_need_refresh` in eco mode).
5. End of stream :
   - ended more than 5 s before the duration → the URL was cut off (see YOUTUBE_API.md) : reload with
     `seek_at_init_request` and resume (max 3 per track)
   - repeat-one → seek to 0
   - otherwise `MusicQueue::next(true)`; at the end of the queue, if autoplay is on, `load_radio` appends a radio
     and continues.

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
- New user-facing strings go in **all 7** `romfs/gfx/msg/string_resources_*.txt` files (English if untranslated).
  A missing id shows `[SR Not Found]`. Each file must stay under 16 KB (the loader's buffer).
