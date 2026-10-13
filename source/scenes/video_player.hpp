#pragma once
#include "types.hpp"

// The music player : playback engine + the now-playing scene (SceneType::VIDEO_PLAYER).
// Plays the tracks in MusicQueue (music/queue.hpp), audio only.

void VideoPlayer_init(void);
void VideoPlayer_exit(void);
void VideoPlayer_suspend(void);
// arg : a YouTube video url to play on its own (replaces the queue), or "" to just open the now-playing screen
void VideoPlayer_resume(std::string arg);

#define VIDEO_PLAYING_BAR_HEIGHT 20
bool video_is_playing(void);
void video_draw_playing_bar();
void video_update_playing_bar(Hid_info key);
// auto-skips SponsorBlock segments of the playing track; call once per frame
void video_update_sponsorblock();

// draws the now-playing info (album art, title, progress) on the top screen; used by every scene
void video_draw_top_screen();

void video_set_should_suspend_decoding(bool should_suspend_decoding);
void video_set_skip_drawing(bool skip);

void VideoPlayer_draw(void);

// queue playback controls
void music_player_play_queue(); // start playing MusicQueue's current track
void music_player_next();
void music_player_prev();
// shows a short message above the playing bar
void music_player_show_notice(const std::string &text);
