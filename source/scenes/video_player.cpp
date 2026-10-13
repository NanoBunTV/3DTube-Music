#include "headers.hpp"
#include <vector>
#include <numeric>
#include <math.h>
#include <set>

#include "scenes/video_player.hpp"
#include "ui/overlay.hpp"
#include "ui/ui.hpp"
#include "youtube_parser/parser.hpp"
#include "network_decoder/network_io.hpp"
#include "network_decoder/network_decoder_multiple.hpp"
#include "network_decoder/thumbnail_loader.hpp"
#include "util/async_task.hpp"
#include "util/misc_tasks.hpp"
#include "util/util.hpp"
#include "music/music_parser.hpp"
#include "music/queue.hpp"

#define TAB_SELECTOR_HEIGHT 20
#define EQUALIZER_POPUP_WIDTH 280
#define EQUALIZER_POPUP_HEIGHT (240 - VIDEO_PLAYING_BAR_HEIGHT)

#define MAX_THUMBNAIL_LOAD_REQUEST 12
#define MAX_RETRY_CNT 2           // 3 trials as a total
#define VIDEO_INFO_CACHE_MAX 30   // stream urls expire anyway, so don't keep too many
#define MAX_CONSECUTIVE_SKIPS 10  // stop auto-skipping unplayable tracks after this many in a row
#define MAX_TRUNCATED_RELOADS 3   // per track
#define ALBUM_ART_SIZE 226        // requested album art resolution
#define QUEUE_ITEM_HEIGHT 40
#define NOTICE_DURATION_MS 3000
#define ACCENT_COLOR 0xFF3333D0

#define TAB_CONTROLS 0
#define TAB_QUEUE 1
#define TAB_AUDIO 2

namespace VideoPlayer {
bool vid_thread_run = false;
bool vid_already_init = false;
bool vid_thread_suspend = true;
volatile bool vid_play_request = false;
volatile bool vid_seek_request = false;
volatile bool vid_change_video_request = false;
volatile bool vid_pausing = false;
volatile bool vid_pausing_seek = false;
volatile bool eof_reached = false;
volatile bool video_skip_drawing = false; // for performance reason, enabled when opening keyboard
volatile double seek_at_init_request = -1;
int vid_sample_rate = 0;
double vid_duration = 0;
double vid_current_pos = 0;
volatile double vid_seek_pos = 0;
std::string vid_audio_format = "n/a";
std::string cur_playing_url = "";
Thread vid_decode_thread, vid_position_thread;

constexpr int CONTENT_Y_HIGH = 240 - TAB_SELECTOR_HEIGHT - VIDEO_PLAYING_BAR_HEIGHT;

Thread stream_downloader_thread;
NetworkStreamDownloader stream_downloader;

Thread livestream_initer_thread;
NetworkMultipleDecoder network_decoder;
Mutex network_decoder_critical_lock; // locked when seeking or deiniting

Mutex small_resource_lock; // locking basically all std::vector, std::string, etc
YouTubeVideoDetail playing_video_info;
std::map<std::string, YouTubeVideoDetail> video_info_cache;
int video_retry_left = 0;
int consecutive_skips = 0;
// reloads of a stream that ended before the track's duration (see the EOF handling in decode_thread)
std::string truncated_reload_url;
int truncated_reload_count = 0;

// album art of the current track
std::string album_art_url;
int album_art_handle = -1;

// short messages (SponsorBlock skips, skipped tracks...) shown above the playing bar; set from any thread
Mutex notice_lock;
std::string notice_text;
u64 notice_until = 0;

TabView *main_view = NULL;
VerticalListView *controls_tab_view = NULL;
ScrollView *queue_tab_view = NULL;
VerticalListView *queue_list_view = NULL;
TextView *queue_header_view = NULL;
int queue_view_version = -1;
int queue_clicked_position = -1;
int queue_remove_position = -1;
ScrollView *audio_tab_view = NULL;
CustomView *download_progress_view = NULL;
OverlayView *equalizer_popup_view = NULL;
}; // namespace VideoPlayer
using namespace VideoPlayer;

static const char *volatile network_waiting_status = NULL;
const char *get_network_waiting_status() {
	if (network_waiting_status) {
		return network_waiting_status;
	}
	return network_decoder.get_network_waiting_status();
}

// FourthTube suspended decoding on Old 3DS while the HOME menu is open (needed for video). Not needed for audio;
// the system itself stops application audio while the HOME menu is open anyway.
void video_set_should_suspend_decoding(bool should_suspend) { (void)should_suspend; }

static void load_video_page(void *);
static void load_radio(void *);
static void decode_thread(void *arg);
static void position_thread(void *arg);
static void send_seek_request_wo_lock(double pos);
static void send_seek_request(double pos);

void music_player_show_notice(const std::string &text) {
	notice_lock.lock();
	notice_text = text;
	notice_until = osGetTime() + NOTICE_DURATION_MS;
	notice_lock.unlock();
	var_need_refresh = true;
}

static std::string repeat_mode_label(MusicQueue::RepeatMode mode) {
	return mode == MusicQueue::RepeatMode::ALL   ? LOCALIZED(REPEAT_ALL)
	       : mode == MusicQueue::RepeatMode::ONE ? LOCALIZED(REPEAT_ONE)
	                                             : LOCALIZED(REPEAT_OFF);
}

// should be called while `small_resource_lock` is locked
static void update_album_art_wo_lock(const MusicTrack &track) {
	std::string url;
	if (!track.thumbnail_url.empty()) {
		url = music_thumbnail_url(track.thumbnail_url, ALBUM_ART_SIZE);
	} else if (!track.video_id.empty()) {
		url = music_video_thumbnail_url(track.video_id);
	}
	if (url == album_art_url) {
		return;
	}
	if (album_art_handle != -1) {
		thumbnail_cancel_request(album_art_handle);
	}
	album_art_url = url;
	album_art_handle = url.empty() ? -1
	                                : thumbnail_request(url, SceneType::VIDEO_PLAYER, PRIORITY_FOREGROUND * 2,
	                                                    ThumbnailType::DEFAULT);
}

// starts loading and playing MusicQueue's current track
// should be called while `small_resource_lock` is locked
static void play_current_track_wo_lock(bool force_load) {
	MusicTrack track = MusicQueue::get_current();
	if (track.video_id.empty()) {
		return;
	}
	std::string url = youtube_get_video_url_by_id(track.video_id);
	remove_all_async_tasks_with_type(load_video_page);
	if (force_load) {
		video_info_cache.erase(url);
	}
	vid_play_request = false; // stops the current track, if any
	cur_playing_url = url;
	video_retry_left = MAX_RETRY_CNT;
	update_album_art_wo_lock(track);
	queue_async_task(load_video_page, NULL);
	var_need_refresh = true;
}

void music_player_play_queue() {
	small_resource_lock.lock();
	consecutive_skips = 0;
	play_current_track_wo_lock(false);
	small_resource_lock.unlock();
}

void music_player_next() {
	small_resource_lock.lock();
	consecutive_skips = 0;
	if (MusicQueue::next(false)) {
		play_current_track_wo_lock(false);
	} else if (var_autoplay_level != 0 && MusicQueue::has_current()) {
		if (!is_async_task_running(load_radio)) {
			queue_async_task(load_radio, (void *)1);
		}
	}
	small_resource_lock.unlock();
}

void music_player_prev() {
	small_resource_lock.lock();
	consecutive_skips = 0;
	if (network_decoder.ready && vid_current_pos > 3) {
		send_seek_request_wo_lock(0); // like most players : restart the track first
	} else if (MusicQueue::prev()) {
		play_current_track_wo_lock(false);
	} else if (network_decoder.ready) {
		send_seek_request_wo_lock(0);
	}
	small_resource_lock.unlock();
}

static void toggle_pause() {
	small_resource_lock.lock();
	if (vid_play_request) {
		if (vid_pausing) {
			vid_pausing = false;
			if (eof_reached) {
				send_seek_request_wo_lock(0);
			}
			if (!vid_pausing_seek) {
				Util_speaker_resume(0);
			}
		} else {
			vid_pausing = true;
			if (!vid_pausing_seek) {
				Util_speaker_pause(0);
			}
		}
	} else if (MusicQueue::has_current() && !is_async_task_running(load_video_page)) {
		network_waiting_status = NULL;
		play_current_track_wo_lock(false);
	}
	var_need_refresh = true;
	small_resource_lock.unlock();
}

/* ------------------------------------------------------------------------------------------------------------------ */
// views

namespace {
// a touchable text button
TextView *make_button(std::function<std::string()> text, double width, double height,
                      std::function<bool()> is_highlighted, std::function<void()> on_press) {
	TextView *res = (new TextView(0, 0, width, height))
	                    ->set_text(text)
	                    ->set_x_alignment(TextView::XAlign::CENTER)
	                    ->set_text_offset(0, -1);
	res->set_get_background_color([is_highlighted](const View &view) -> u32 {
		if (is_highlighted && is_highlighted()) {
			return ACCENT_COLOR;
		}
		return View::STANDARD_BACKGROUND(view) == DEFAULT_BACK_COLOR ? LIGHT0_BACK_COLOR
		                                                              : View::STANDARD_BACKGROUND(view);
	});
	res->set_get_text_color([is_highlighted]() -> u32 {
		return is_highlighted && is_highlighted() ? DEF_DRAW_WHITE : DEFAULT_TEXT_COLOR;
	});
	res->set_on_view_released([on_press](View &) { on_press(); });
	return res;
}

// transport buttons : previous, play/pause, next
constexpr int TRANSPORT_HEIGHT = 62;
constexpr float TRANSPORT_X[3] = {85, 160, 235};
constexpr float PLAY_RADIUS = 25;
constexpr float SIDE_RADIUS = 20;
int transport_pressed = -1;

void draw_prev_icon(float cx, float cy, u32 color) {
	C2D_DrawRectSolid(cx - 9, cy - 8, 0, 3, 16, color);
	C2D_DrawTriangle(cx + 8, cy - 8, color, cx + 8, cy + 8, color, cx - 6, cy, color, 0);
}
void draw_next_icon(float cx, float cy, u32 color) {
	C2D_DrawRectSolid(cx + 6, cy - 8, 0, 3, 16, color);
	C2D_DrawTriangle(cx - 8, cy - 8, color, cx - 8, cy + 8, color, cx + 6, cy, color, 0);
}
void draw_play_pause_icon(float cx, float cy, bool playing, u32 color) {
	if (playing) {
		C2D_DrawRectSolid(cx - 8, cy - 10, 0, 6, 20, color);
		C2D_DrawRectSolid(cx + 2, cy - 10, 0, 6, 20, color);
	} else {
		C2D_DrawTriangle(cx - 6, cy - 11, color, cx - 6, cy + 11, color, cx + 11, cy, color, 0);
	}
}

CustomView *make_transport_view() {
	return (new CustomView(0, 0, 320, TRANSPORT_HEIGHT))
	    ->set_draw([](const CustomView &view) {
		    float cy = view.y0 + TRANSPORT_HEIGHT / 2;
		    bool playing = vid_play_request && !vid_pausing && !eof_reached;
		    C2D_DrawCircleSolid(TRANSPORT_X[1], cy, 0, PLAY_RADIUS + (transport_pressed == 1 ? 2 : 0),
		                        ACCENT_COLOR);
		    draw_play_pause_icon(TRANSPORT_X[1], cy, playing, DEF_DRAW_WHITE);
		    for (int i : {0, 2}) {
			    if (transport_pressed == i) {
				    C2D_DrawCircleSolid(TRANSPORT_X[i], cy, 0, SIDE_RADIUS, LIGHT0_BACK_COLOR);
			    }
		    }
		    draw_prev_icon(TRANSPORT_X[0], cy, DEFAULT_TEXT_COLOR);
		    draw_next_icon(TRANSPORT_X[2], cy, DEFAULT_TEXT_COLOR);
	    })
	    ->set_update([](CustomView &view, Hid_info key) {
		    float cy = view.y0 + TRANSPORT_HEIGHT / 2;
		    auto button_at = [&](int x, int y) {
			    for (int i = 0; i < 3; i++) {
				    float r = i == 1 ? PLAY_RADIUS : SIDE_RADIUS;
				    if (std::hypot(x - TRANSPORT_X[i], y - cy) <= r + 4) {
					    return i;
				    }
			    }
			    return -1;
		    };
		    if (key.p_touch) {
			    transport_pressed = button_at(key.touch_x, key.touch_y);
			    var_need_refresh = true;
		    } else if (transport_pressed != -1 && !key.h_touch) {
			    int pressed = transport_pressed;
			    transport_pressed = -1;
			    if (pressed == 0) {
				    music_player_prev();
			    } else if (pressed == 1) {
				    toggle_pause();
			    } else {
				    music_player_next();
			    }
			    var_need_refresh = true;
		    } else if (transport_pressed != -1 && button_at(key.touch_x, key.touch_y) != transport_pressed) {
			    transport_pressed = -1; // dragged away : cancel
			    var_need_refresh = true;
		    }
	    });
}

// rebuilds the queue list if the queue changed; UI thread only
void update_queue_views() {
	int version = MusicQueue::get_version();
	if (version == queue_view_version) {
		return;
	}
	queue_view_version = version;
	auto tracks = MusicQueue::get_play_order();
	int current = MusicQueue::get_current_position();

	std::vector<View *> views;
	for (size_t i = 0; i < tracks.size(); i++) {
		const auto &track = tracks[i];
		std::string thumbnail = track.thumbnail_url.empty() ? music_video_thumbnail_url(track.video_id)
		                                                    : music_thumbnail_url(track.thumbnail_url, 120);
		SuccinctVideoView *cur_view = (new SuccinctVideoView(0, 0, 320, QUEUE_ITEM_HEIGHT))
		                                  ->set_square_thumbnail(true)
		                                  ->set_title_lines(truncate_str(
		                                      track.title.empty() ? LOCALIZED(LOADING) : track.title,
		                                      320 - QUEUE_ITEM_HEIGHT - 3 - SMALL_MARGIN, 1, 0.5, 0.5))
		                                  ->set_auxiliary_lines({track.artist})
		                                  ->set_bottom_right_overlay(track.duration_text)
		                                  ->set_thumbnail_url(thumbnail);
		bool is_current = (int)i == current;
		cur_view->set_get_background_color([is_current](const View &view) -> u32 {
			return is_current ? (var_night_mode ? 0xFF502020 : COLOR_LIGHT_BLUE) : View::STANDARD_BACKGROUND(view);
		});
		cur_view->add_on_long_hold(40, [i](View &) { queue_remove_position = i; });
		cur_view->set_on_view_released([i](View &) { queue_clicked_position = i; });
		views.push_back(cur_view);
	}
	queue_list_view->recursive_delete_subviews();
	queue_list_view->views = views;
	queue_header_view->set_text(tracks.empty() ? LOCALIZED(QUEUE_EMPTY)
	                                           : std::to_string(tracks.size()) + " " + LOCALIZED(SONGS_LOWER) + " • " +
	                                                 LOCALIZED(QUEUE_HOLD_TO_REMOVE));
	// keep the current track in view
	if (current >= 0) {
		double target = current * (QUEUE_ITEM_HEIGHT + SMALL_MARGIN) - (CONTENT_Y_HIGH / 2);
		queue_tab_view->set_offset(std::max(0.0, target));
	}
	var_need_refresh = true;
}
} // namespace

void VideoPlayer_init(void) {
	logger.info(DEF_SAPP0_INIT_STR, "Initializing...");
	Result_with_string result;

	vid_thread_run = true;

	// controls tab
	controls_tab_view =
	    (new VerticalListView(0, 0, 320))
	        ->set_views(
	            {(new CustomView(0, 0, 320, 38))->set_draw([](const CustomView &view) {
		             MusicTrack track = MusicQueue::get_current();
		             if (track.video_id.empty()) {
			             Draw_x_centered(LOCALIZED(NOTHING_PLAYING), 0, 320, view.y0 + 10, 0.5, 0.5,
			                             LIGHT1_TEXT_COLOR);
			             return;
		             }
		             auto title = truncate_str(track.title.empty() ? LOCALIZED(LOADING) : track.title,
		                                       320 - SMALL_MARGIN * 4, 1, 0.55, 0.55);
		             auto artist = truncate_str(track.artist, 320 - SMALL_MARGIN * 4, 1, 0.45, 0.45);
		             Draw_x_centered(title.size() ? title[0] : "", 0, 320, view.y0 + 4, 0.55, 0.55,
		                             DEFAULT_TEXT_COLOR);
		             Draw_x_centered(artist.size() ? artist[0] : "", 0, 320, view.y0 + 21, 0.45, 0.45,
		                             LIGHT1_TEXT_COLOR);
	             }),
	             make_transport_view(),
	             (new HorizontalListView(0, 0, 26))
	                 ->set_views({new EmptyView(0, 0, 10, 26),
	                              make_button([]() { return LOCALIZED(SHUFFLE); }, 145, 26,
	                                          []() { return MusicQueue::get_shuffle(); },
	                                          []() {
		                                          MusicQueue::set_shuffle(!MusicQueue::get_shuffle());
		                                          var_need_refresh = true;
	                                          }),
	                              new EmptyView(0, 0, 10, 26),
	                              make_button([]() { return repeat_mode_label(MusicQueue::get_repeat()); }, 145, 26,
	                                          []() { return MusicQueue::get_repeat() != MusicQueue::RepeatMode::OFF; },
	                                          []() {
		                                          MusicQueue::cycle_repeat();
		                                          var_need_refresh = true;
	                                          })}),
	             new EmptyView(0, 0, 320, 8),
	             (new HorizontalListView(0, 0, 26))
	                 ->set_views({new EmptyView(0, 0, 10, 26),
	                              make_button([]() { return LOCALIZED(START_RADIO); }, 145, 26, nullptr,
	                                          []() {
		                                          if (MusicQueue::has_current() && !is_async_task_running(load_radio)) {
			                                          queue_async_task(load_radio, (void *)0);
			                                          music_player_show_notice(LOCALIZED(LOADING));
		                                          }
	                                          }),
	                              new EmptyView(0, 0, 10, 26),
	                              make_button([]() { return LOCALIZED(RELOAD); }, 145, 26, nullptr,
	                                          []() {
		                                          small_resource_lock.lock();
		                                          if (!is_async_task_running(load_video_page)) {
			                                          seek_at_init_request = vid_current_pos;
			                                          play_current_track_wo_lock(true);
		                                          }
		                                          small_resource_lock.unlock();
	                                          })})});

	// queue tab
	queue_header_view = (new TextView(SMALL_MARGIN, 0, 320 - SMALL_MARGIN, DEFAULT_FONT_INTERVAL + SMALL_MARGIN));
	queue_header_view->set_get_text_color([]() { return LIGHT1_TEXT_COLOR; });
	queue_list_view = (new VerticalListView(0, 0, 320))
	                      ->set_margin(SMALL_MARGIN)
	                      ->enable_thumbnail_request_update(MAX_THUMBNAIL_LOAD_REQUEST, SceneType::VIDEO_PLAYER);
	queue_tab_view = (new ScrollView(0, 0, 320, CONTENT_Y_HIGH))
	                     ->set_views({queue_header_view, queue_list_view, new EmptyView(0, 0, 320, SMALL_MARGIN)});

	// audio tab
	download_progress_view = (new CustomView(0, 0, 320, SMALL_MARGIN * 2))->set_draw([](const CustomView &view) {
		int y = view.y0;
		int sample = 50;
		auto progress_bars = network_decoder.get_buffering_progress_bars(sample);
		if (progress_bars.size() && progress_bars[0].second.size()) {
			auto &cur_bar = progress_bars[0].second;
			int bar_len = 310;
			for (int i = 0; i < sample; i++) {
				int a = 0xFF;
				int r = 0x80 * (1 - cur_bar[i] / 100);
				int g = 0x80 * (1 - cur_bar[i] / 100);
				int b = 0xA0 * cur_bar[i] / 100 + 0x80 * (1 - cur_bar[i] / 100);
				int xl = 5 + bar_len * i / sample;
				int xr = 5 + bar_len * (i + 1) / sample;
				Draw_texture(var_square_image[0], a << 24 | b << 16 | g << 8 | r, xl, y, xr - xl, SMALL_MARGIN);
			}
			Draw_texture(var_square_image[0], DEF_DRAW_RED, 5 + bar_len * progress_bars[0].first - 1, y, SMALL_MARGIN,
			             SMALL_MARGIN);
		}
	});
	static double equalizer_values[18];
	std::fill(equalizer_values, equalizer_values + 18, 1.0);
	audio_tab_view =
	    (new ScrollView(0, 0, 320, CONTENT_Y_HIGH))
	        ->set_margin(SMALL_MARGIN)
	        ->set_views(
	            {(new EmptyView(0, 0, 320, 0)),
	             (new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL))
	                 ->set_text((std::function<std::string()>)[]() { return LOCALIZED(BUFFERING_PROGRESS); }),
	             download_progress_view,
	             (new BarView(0, 0, 260, 30))                     // preamp
	                 ->set_values(std::log(0.25), std::log(4), 0) // exponential scale
	                 ->set_title([](const BarView &view) {
		                 return LOCALIZED(PREAMP) + " : " +
		                        std::to_string((int)std::round(std::exp(view.get_value()) * 100)) + "%";
	                 })
	                 ->set_while_holding([](BarView &view) {
		                 double volume = std::exp(view.get_value());
		                 if (volume >= 0.95 && volume <= 1.05) {
			                 volume = 1.0, view.set_value(0);
		                 }
		                 // volume change needs a reconstruction of the filter graph, so don't update volume too often
		                 static int cnt = 0;
		                 if (++cnt >= 15) {
			                 cnt = 0, network_decoder.set_preamp(volume);
		                 }
	                 })
	                 ->set_on_release([](BarView &view) {
		                 double volume = std::exp(view.get_value());
		                 if (volume >= 0.95 && volume <= 1.05) {
			                 volume = 1.0, view.set_value(0);
		                 }
		                 network_decoder.set_preamp(volume);
	                 }),
	             (new BarView(0, 0, 260, 30)) // speed
	                 ->set_values(0.25, 1.5, 1.0)
	                 ->set_title([](const BarView &view) {
		                 return LOCALIZED(SPEED) + " : " + std::to_string((int)std::round(view.get_value() * 100)) + "%";
	                 })
	                 ->set_while_holding([](BarView &view) {
		                 view.fix_close_values(0.97, 1.0, 1.03);
		                 static int cnt = 0;
		                 if (++cnt >= 15) {
			                 cnt = 0, network_decoder.set_tempo(view.get_value());
		                 }
	                 })
	                 ->set_on_release([](BarView &view) {
		                 view.fix_close_values(0.97, 1.0, 1.03);
		                 network_decoder.set_tempo(view.get_value());
	                 }),
	             (new BarView(0, 0, 260, 30)) // pitch
	                 ->set_values(0.5, 2.0, 1.0)
	                 ->set_title([](const BarView &view) {
		                 return LOCALIZED(PITCH) + " : " + std::to_string((int)std::round(view.get_value() * 100)) + "%";
	                 })
	                 ->set_while_holding([](BarView &view) {
		                 view.fix_close_values(0.97, 1.0, 1.03);
		                 static int cnt = 0;
		                 if (++cnt >= 15) {
			                 cnt = 0, network_decoder.set_pitch(view.get_value());
		                 }
	                 })
	                 ->set_on_release([](BarView &view) {
		                 view.fix_close_values(0.97, 1.0, 1.03);
		                 network_decoder.set_pitch(view.get_value());
	                 }),
	             make_button([]() { return LOCALIZED(OPEN_EQUALIZER); }, 140, 22, nullptr,
	                         []() { equalizer_popup_view->set_is_visible(true); }),
	             (new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL))->set_text((std::function<std::string()>)[]() {
		             return LOCALIZED(AUDIO_FORMAT) + " : " + vid_audio_format;
	             })});

	// equalizer popup
	equalizer_popup_view = (new OverlayView(0, 0, 320, 240 - VIDEO_PLAYING_BAR_HEIGHT));
	std::vector<View *> equalizer_views;
	constexpr int equalizer_hz_list[18] = {65,   92,   131,  185,  262,  370,  523,   740,   1047,
	                                       1480, 2093, 2960, 4186, 5920, 8372, 11840, 16744, 20000};
	for (int i = 0; i < 18; i++) {
		BarView *bar_view = (new BarView(0, 0, 180, DEFAULT_FONT_INTERVAL))
		                        ->set_values_sync(0.0, 2.0, &equalizer_values[i])
		                        ->set_while_holding([i](BarView &view) {
			                        static int cnt = 0;
			                        if (++cnt >= 30) {
				                        cnt = 0;
				                        network_decoder.set_equalizer_value(i, view.get_value());
			                        }
		                        })
		                        ->set_on_release([i](BarView &view) {
			                        network_decoder.set_equalizer_value(i, view.get_value());
		                        });
		equalizer_views.push_back(
		    (new HorizontalListView(0, 0, DEFAULT_FONT_INTERVAL))
		        ->set_views({(new TextView(0, 0, 60, DEFAULT_FONT_INTERVAL))
		                         ->set_text(std::to_string(equalizer_hz_list[i]) + " Hz"),
		                     bar_view}));
	}
	equalizer_popup_view->set_subview(
	    (new VerticalListView(0, 0, EQUALIZER_POPUP_WIDTH))
	        ->set_views({(new TextView(0, 0, EQUALIZER_POPUP_WIDTH, 19))
	                         ->set_text([]() { return LOCALIZED(RESET); })
	                         ->set_x_alignment(TextView::XAlign::CENTER)
	                         ->set_get_background_color(View::STANDARD_BACKGROUND)
	                         ->set_on_view_released([](View &) {
		                         std::fill(equalizer_values, equalizer_values + 18, 1.0);
		                         for (int i = 0; i < 18; i++) {
			                         network_decoder.set_equalizer_value(i, 1);
		                         }
	                         }),
	                     (new RuleView(0, 0, EQUALIZER_POPUP_WIDTH, 1))->set_margin(0),
	                     (new ScrollView(0, 0, EQUALIZER_POPUP_WIDTH, EQUALIZER_POPUP_HEIGHT - 40))
	                         ->set_views(equalizer_views)
	                         ->set_get_background_color([](const View &) { return DEFAULT_BACK_COLOR; }),
	                     (new TextView(0, 0, EQUALIZER_POPUP_WIDTH, 20))
	                         ->set_text([]() { return LOCALIZED(CLOSE); })
	                         ->set_x_alignment(TextView::XAlign::CENTER)
	                         ->set_on_view_released([](View &) {
		                         equalizer_popup_view->set_is_visible(false);
		                         var_need_refresh = true;
	                         })
	                         ->set_get_background_color(View::STANDARD_BACKGROUND)})
	        ->set_draw_order({2, 0, 1, 3}));
	equalizer_popup_view->set_is_visible(false);

	main_view = (new TabView(0, 0, 320, 240 - VIDEO_PLAYING_BAR_HEIGHT))
	                ->set_stretch_subview(true)
	                ->set_tab_font_size(0.4)
	                ->set_views({controls_tab_view, queue_tab_view, audio_tab_view}, TAB_CONTROLS)
	                ->set_tab_texts<std::function<std::string()>>({[]() { return LOCALIZED(NOW_PLAYING); },
	                                                               []() { return LOCALIZED(QUEUE); },
	                                                               []() { return LOCALIZED(AUDIO); }});

	add_cpu_limit(CPU_LIMIT);
	int core = var_is_new3ds ? 2 : 1;
	vid_decode_thread = threadCreate(decode_thread, (void *)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_HIGH, core, false);
	vid_position_thread =
	    threadCreate(position_thread, (void *)(""), DEF_STACKSIZE, DEF_THREAD_PRIORITY_NORMAL, 0, false);
	livestream_initer_thread = threadCreate(livestream_initer_thread_func, &network_decoder, DEF_STACKSIZE,
	                                        DEF_THREAD_PRIORITY_NORMAL, core, false);
	stream_downloader_thread = threadCreate(network_downloader_thread, &stream_downloader, DEF_STACKSIZE,
	                                        DEF_THREAD_PRIORITY_NORMAL, 0, false);

	// used by community posts in the channel scene
	result = Draw_load_texture("romfs:/gfx/draw/thumb_up.t3x", 63, var_texture_thumb_up, 0, 2);
	if (result.code != 0) {
		logger.error(DEF_SAPP0_INIT_STR, "Draw_load_texture()..." + result.string + result.error_description,
		             result.code);
	}
	result = Draw_load_texture("romfs:/gfx/draw/thumb_down.t3x", 64, var_texture_thumb_down, 0, 2);
	if (result.code != 0) {
		logger.error(DEF_SAPP0_INIT_STR, "Draw_load_texture()..." + result.string + result.error_description,
		             result.code);
	}

	vid_current_pos = 0;
	vid_duration = 0;
	vid_audio_format = "n/a";

	VideoPlayer_resume("");
	vid_already_init = true;
	logger.info(DEF_SAPP0_INIT_STR, "Initialized.");
}

void VideoPlayer_exit(void) {
	logger.info(DEF_SAPP0_EXIT_STR, "Exiting...");
	u64 time_out = 10000000000;

	vid_already_init = false;
	vid_thread_suspend = false;
	vid_thread_run = false;
	vid_play_request = false;

	stream_downloader.request_thread_exit();
	network_decoder.interrupt = true;
	network_decoder.request_thread_exit();
	logger.info(DEF_SAPP0_EXIT_STR, "threadJoin()...", threadJoin(vid_decode_thread, time_out));
	logger.info(DEF_SAPP0_EXIT_STR, "threadJoin()...", threadJoin(vid_position_thread, time_out));
	logger.info(DEF_SAPP0_EXIT_STR, "threadJoin()...", threadJoin(stream_downloader_thread, time_out));
	logger.info(DEF_SAPP0_EXIT_STR, "threadJoin()...", threadJoin(livestream_initer_thread, time_out));
	threadFree(vid_decode_thread);
	threadFree(vid_position_thread);
	threadFree(stream_downloader_thread);
	threadFree(livestream_initer_thread);
	stream_downloader.delete_all();

	small_resource_lock.lock();
	main_view->recursive_delete_subviews();
	delete main_view;
	main_view = NULL;
	equalizer_popup_view->recursive_delete_subviews();
	delete equalizer_popup_view;
	equalizer_popup_view = NULL;
	small_resource_lock.unlock();

	remove_cpu_limit(CPU_LIMIT);

	Draw_free_texture(63);
	Draw_free_texture(64);

	logger.info(DEF_SAPP0_EXIT_STR, "Exited.");
}

void VideoPlayer_suspend(void) { vid_thread_suspend = true; }

void VideoPlayer_resume(std::string arg) {
	if (arg != "") { // play a single YouTube link
		std::string video_id = youtube_get_video_id_by_url(arg);
		if (!video_id.empty() && (MusicQueue::get_current().video_id != video_id || !vid_play_request)) {
			MusicTrack track;
			track.video_id = video_id;
			MusicQueue::set({track}, 0);
			music_player_play_queue();
		}
	}
	main_view->reset_holding_status();
	queue_view_version = -1; // rebuild, which also scrolls to the current track

	overlay_menu_on_resume();
	vid_thread_suspend = false;
	var_need_refresh = true;
}

/* ------------------------------------------------------------------------------------------------------------------ */
// async tasks

static void load_video_page(void *) {
	small_resource_lock.lock();
	std::string url = cur_playing_url;
	YouTubeVideoDetail info;
	bool cached = video_info_cache.count(url);
	if (cached) {
		info = video_info_cache[url];
	}
	network_waiting_status = "Loading";
	var_need_refresh = true;
	small_resource_lock.unlock();

	if (!cached) {
		logger.info("player/load", "request : " + url);
		add_cpu_limit(ADDITIONAL_CPU_LIMIT);
		info = youtube_load_video_page(url);
		remove_cpu_limit(ADDITIONAL_CPU_LIMIT);
	}

	small_resource_lock.lock();
	if (network_waiting_status && std::string(network_waiting_status) == "Loading") {
		network_waiting_status = NULL;
	}
	if (!vid_already_init || url != cur_playing_url) { // app shut down or another track was requested meanwhile
		small_resource_lock.unlock();
		return;
	}
	// fill in metadata of tracks that were played from plain YouTube links
	MusicTrack metadata;
	metadata.video_id = info.id;
	metadata.title = info.title;
	metadata.artist = info.author.name;
	MusicQueue::update_current_metadata(metadata);

	if (info.is_audio_playable()) {
		if (video_info_cache.size() >= VIDEO_INFO_CACHE_MAX) {
			video_info_cache.clear();
		}
		video_info_cache[url] = info;
		playing_video_info = info;
		vid_change_video_request = true;
		if (network_decoder.ready) {
			network_decoder.interrupt = true;
		}
	} else {
		std::string reason = info.playability_reason != "" ? info.playability_reason
		                     : info.error != ""            ? info.error
		                                                   : "No audio stream";
		logger.warning("player/load", "unplayable : " + reason);
		consecutive_skips++;
		if (consecutive_skips < MAX_CONSECUTIVE_SKIPS && MusicQueue::next(false)) {
			music_player_show_notice(LOCALIZED(SKIPPED_UNPLAYABLE));
			play_current_track_wo_lock(false);
		} else {
			consecutive_skips = 0;
			music_player_show_notice(LOCALIZED(CANNOT_PLAY) + " : " + reason);
		}
	}
	var_need_refresh = true;
	small_resource_lock.unlock();
}

// arg : non-NULL to continue playback with the radio once it's loaded (used when the queue ended)
static void load_radio(void *arg) {
	bool continue_playback = arg != NULL;
	MusicTrack seed = MusicQueue::get_current();
	if (seed.video_id.empty()) {
		return;
	}
	std::string error;
	add_cpu_limit(ADDITIONAL_CPU_LIMIT);
	auto radio_tracks = music_load_radio(seed.video_id, error);
	remove_cpu_limit(ADDITIONAL_CPU_LIMIT);

	std::set<std::string> queued;
	for (auto &track : MusicQueue::get_play_order()) {
		queued.insert(track.video_id);
	}
	std::vector<MusicTrack> new_tracks;
	for (auto &track : radio_tracks) {
		if (!queued.count(track.video_id)) {
			queued.insert(track.video_id);
			new_tracks.push_back(track);
		}
	}

	small_resource_lock.lock();
	if (!vid_already_init) {
		small_resource_lock.unlock();
		return;
	}
	if (new_tracks.empty()) {
		music_player_show_notice(error != "" ? error : LOCALIZED(RADIO_NO_MORE_SONGS));
	} else {
		bool at_end = MusicQueue::get_current_position() == MusicQueue::size() - 1;
		MusicQueue::add_to_end(new_tracks);
		if (continue_playback && at_end && MusicQueue::next(false)) {
			play_current_track_wo_lock(false);
		} else {
			music_player_show_notice(LOCALIZED(RADIO_ADDED));
		}
	}
	var_need_refresh = true;
	small_resource_lock.unlock();
}

/* ------------------------------------------------------------------------------------------------------------------ */
// seeking, SponsorBlock

static void send_seek_request_wo_lock(double pos) {
	vid_seek_pos = pos;
	vid_current_pos = pos;
	vid_seek_request = true;
	if (network_decoder.ready) { // avoid locking while initing
		network_decoder.interrupt = true;
	}
}
static void send_seek_request(double pos) {
	small_resource_lock.lock();
	send_seek_request_wo_lock(pos);
	small_resource_lock.unlock();
}

namespace SponsorBlock {
std::string segments_video_id;
// a segment is auto-skipped once per pass, so the user can seek back into a segment that was just skipped
std::vector<bool> segment_skipped;

static std::string category_to_label(const std::string &category) {
	if (category == "music_offtopic") {
		return "Non-music section";
	}
	if (category == "sponsor") {
		return "Sponsor";
	}
	if (category == "selfpromo") {
		return "Self-promotion";
	}
	if (category == "interaction") {
		return "Interaction reminder";
	}
	return category;
}
} // namespace SponsorBlock

// called every frame from the main loop, regardless of the current scene
void video_update_sponsorblock() {
	using namespace SponsorBlock;
	if (!var_sponsorblock_enabled || !vid_play_request || !network_decoder.ready || vid_seek_request ||
	    vid_change_video_request || vid_pausing_seek || eof_reached) {
		return;
	}
	small_resource_lock.lock();
	const auto &segments = playing_video_info.sponsor_segments;
	if (segments_video_id != playing_video_info.id) {
		segments_video_id = playing_video_info.id;
		segment_skipped.assign(segments.size(), false);
	}
	double pos = vid_current_pos;
	for (size_t i = 0; i < segments.size(); i++) {
		if (pos < segments[i].start - 1) {
			segment_skipped[i] = false; // re-arm once playback is before the segment again (loop, seek back)
		}
		if (!segment_skipped[i] && pos >= segments[i].start && pos < segments[i].end - 0.5) {
			segment_skipped[i] = true;
			double target = segments[i].end;
			if (vid_duration > 1) {
				target = std::min(target, vid_duration - 0.5);
			}
			logger.info("sponsorblock", "skipping " + segments[i].category + " " + std::to_string(segments[i].start) +
			                                " -> " + std::to_string(target));
			send_seek_request_wo_lock(target);
			music_player_show_notice(LOCALIZED(SPONSORBLOCK_SKIPPED) + ": " + category_to_label(segments[i].category));
			break;
		}
	}
	small_resource_lock.unlock();
}

bool video_is_playing() {
	if (!vid_play_request && vid_pausing_seek) {
		small_resource_lock.lock();
		vid_pausing_seek = false;
		if (!vid_pausing) {
			Util_speaker_resume(0);
		}
		small_resource_lock.unlock();
	}
	return vid_play_request || MusicQueue::has_current();
}

/* ------------------------------------------------------------------------------------------------------------------ */
// the playing bar shown at the bottom of every scene

#define SMALL_FONT_SIZE 0.4

namespace Bar {
bool bar_grabbed = false;
double last_grab_timestamp = 0;
int last_touch_x = -1;
int last_touch_y = -1;
float bar_x_l, bar_x_r;
constexpr int MAXIMIZE_ICON_WIDTH = 28;
constexpr int TIME_STR_RIGHT_MARGIN = 2;
constexpr int TIME_STR_LEFT_MARGIN = 3;

void draw() {
	float y_l = 240 - VIDEO_PLAYING_BAR_HEIGHT;
	float y_center = 240 - (float)VIDEO_PLAYING_BAR_HEIGHT / 2;

	Draw_texture(var_square_image[0], DEF_DRAW_DARK_GRAY, 0, y_l, 320, VIDEO_PLAYING_BAR_HEIGHT);

	if (get_network_waiting_status() || is_async_task_running(load_video_page)) { // loading
		C2D_DrawCircleSolid(6, y_center, 0, 2, DEF_DRAW_WHITE);
		C2D_DrawCircleSolid(12, y_center, 0, 2, DEF_DRAW_WHITE);
		C2D_DrawCircleSolid(18, y_center, 0, 2, DEF_DRAW_WHITE);
	} else if (vid_pausing || vid_pausing_seek || !vid_play_request) { // pausing
		C2D_DrawTriangle(8, y_l + 3, DEF_DRAW_WHITE, 8, 240 - 4, DEF_DRAW_WHITE,
		                 8 + (VIDEO_PLAYING_BAR_HEIGHT - 6) * std::sqrt(3) / 2, y_center, DEF_DRAW_WHITE, 0);
	} else { // playing
		C2D_DrawRectSolid(8, y_l + 3, 0, 4, VIDEO_PLAYING_BAR_HEIGHT - 6, DEF_DRAW_WHITE);
		C2D_DrawRectSolid(16, y_l + 3, 0, 4, VIDEO_PLAYING_BAR_HEIGHT - 6, DEF_DRAW_WHITE);
	}
	// maximize icon
	Draw_texture(var_square_image[0], DEF_DRAW_WHITE, 320 - MAXIMIZE_ICON_WIDTH + 4, y_l + 3, 8, 3);
	Draw_texture(var_square_image[0], DEF_DRAW_WHITE, 320 - MAXIMIZE_ICON_WIDTH + 4, y_l + 3, 3, 5);
	Draw_texture(var_square_image[0], DEF_DRAW_WHITE, 320 - MAXIMIZE_ICON_WIDTH + 4, 240 - 6, 8, 3);
	Draw_texture(var_square_image[0], DEF_DRAW_WHITE, 320 - MAXIMIZE_ICON_WIDTH + 4, 240 - 8, 3, 5);
	Draw_texture(var_square_image[0], DEF_DRAW_WHITE, 320 - 12, y_l + 3, 8, 3);
	Draw_texture(var_square_image[0], DEF_DRAW_WHITE, 320 - 7, y_l + 3, 3, 5);
	Draw_texture(var_square_image[0], DEF_DRAW_WHITE, 320 - 12, 240 - 6, 8, 3);
	Draw_texture(var_square_image[0], DEF_DRAW_WHITE, 320 - 7, 240 - 8, 3, 5);

	double bar_timestamp = bar_grabbed ? last_grab_timestamp : vid_current_pos;
	double vid_progress = network_decoder.ready ? network_decoder.get_bar_pos_from_timestamp(bar_timestamp) : 0;

	std::string time_str0 = Util_convert_seconds_to_time(bar_timestamp);
	std::string time_str1 = "/ " + Util_convert_seconds_to_time(vid_duration);
	float time_str0_w = Draw_get_width(time_str0, SMALL_FONT_SIZE);
	float time_str1_w = Draw_get_width(time_str1, SMALL_FONT_SIZE);
	bar_x_l = 30;
	bar_x_r = 320 - std::max(time_str0_w, time_str1_w) - TIME_STR_RIGHT_MARGIN - TIME_STR_LEFT_MARGIN -
	          MAXIMIZE_ICON_WIDTH;

	float time_str_w = std::max(time_str0_w, time_str1_w);
	Draw(time_str0, bar_x_r + TIME_STR_LEFT_MARGIN + (time_str_w - time_str0_w) / 2, y_l - 1, SMALL_FONT_SIZE,
	     SMALL_FONT_SIZE, DEF_DRAW_WHITE);
	Draw(time_str1, bar_x_r + TIME_STR_LEFT_MARGIN + (time_str_w - time_str1_w) / 2, y_center - 2, SMALL_FONT_SIZE,
	     SMALL_FONT_SIZE, DEF_DRAW_WHITE);
	Draw_texture(var_square_image[0], DEF_DRAW_LIGHT_GRAY, bar_x_l, y_center - 2, bar_x_r - bar_x_l, 4);
	if (vid_duration != 0) {
		Draw_texture(var_square_image[0], ACCENT_COLOR, bar_x_l, y_center - 2, (bar_x_r - bar_x_l) * vid_progress, 4);
		C2D_DrawCircleSolid(bar_x_l + (bar_x_r - bar_x_l) * vid_progress, y_center, 0, bar_grabbed ? 6 : 4,
		                    ACCENT_COLOR);
	}

	// notice, just above the bar
	notice_lock.lock();
	bool show_notice = osGetTime() < notice_until;
	std::string text = notice_text;
	notice_lock.unlock();
	if (show_notice) {
		constexpr int NOTICE_HEIGHT = 14;
		float notice_w = std::min<float>(316, Draw_get_width(text, SMALL_FONT_SIZE) + 8);
		Draw_texture(var_square_image[0], 0xC0000000, 320 - notice_w - 2, y_l - NOTICE_HEIGHT - 2, notice_w,
		             NOTICE_HEIGHT);
		Draw(text, 320 - notice_w + 2, y_l - NOTICE_HEIGHT - 1, SMALL_FONT_SIZE, SMALL_FONT_SIZE, DEF_DRAW_WHITE);
		var_need_refresh = true; // keep redrawing so the notice disappears on time
	}
}

void update(Hid_info key) {
	float y_l = 240 - VIDEO_PLAYING_BAR_HEIGHT;
	float y_center = 240 - (float)VIDEO_PLAYING_BAR_HEIGHT / 2;

	constexpr int GRAB_TOLERANCE = 5;
	if (!bar_grabbed && network_decoder.ready && key.p_touch && bar_x_l - GRAB_TOLERANCE <= key.touch_x &&
	    key.touch_x <= bar_x_r + GRAB_TOLERANCE && y_center - 2 - GRAB_TOLERANCE <= key.touch_y &&
	    key.touch_y <= y_center + 2 + GRAB_TOLERANCE) {
		bar_grabbed = true;
		var_need_refresh = true;
	}
	if (bar_grabbed) {
		last_grab_timestamp = network_decoder.get_timestamp_from_bar_pos(
		    ((key.touch_x != -1 ? key.touch_x : last_touch_x) - bar_x_l) / (bar_x_r - bar_x_l));
	}
	if (bar_grabbed && key.touch_x == -1) {
		if (network_decoder.ready) {
			send_seek_request(last_grab_timestamp);
		}
		bar_grabbed = false;
		var_need_refresh = true;
	}
	if (key.p_touch && key.touch_x >= 320 - MAXIMIZE_ICON_WIDTH && key.touch_y >= 240 - VIDEO_PLAYING_BAR_HEIGHT) {
		global_intent.next_scene = SceneType::VIDEO_PLAYER;
		global_intent.arg = "";
	}
	small_resource_lock.lock();
	if (!vid_pausing_seek && bar_grabbed && !vid_pausing) {
		Util_speaker_pause(0);
	}
	if (vid_pausing_seek && !bar_grabbed && !vid_pausing) {
		Util_speaker_resume(0);
	}
	vid_pausing_seek = bar_grabbed;
	small_resource_lock.unlock();

	// start/stop
	if (!get_network_waiting_status() && !vid_pausing_seek && key.p_touch && key.touch_x < 22 && y_l <= key.touch_y) {
		toggle_pause();
	}

	last_touch_x = key.touch_x;
	last_touch_y = key.touch_y;
}
} // namespace Bar
void video_update_playing_bar(Hid_info key) { Bar::update(key); }
void video_draw_playing_bar() { Bar::draw(); }

void video_set_skip_drawing(bool skip) { video_skip_drawing = skip; }

/* ------------------------------------------------------------------------------------------------------------------ */
// top screen

static void draw_now_playing_top() {
	MusicTrack track = MusicQueue::get_current();
	if (track.video_id.empty()) {
		Draw_xy_centered("3DTube Music", 0, 400, 15, 130, 0.9, 0.9, DEFAULT_TEXT_COLOR);
		Draw_xy_centered(LOCALIZED(NOTHING_PLAYING), 0, 400, 130, 170, 0.5, 0.5, LIGHT1_TEXT_COLOR);
		return;
	}
	constexpr float ART_X = 14;
	constexpr float ART_Y = 28;
	constexpr float ART_BOX = 156;

	// album art, fitted into a square box
	Draw_texture(var_square_image[0], LIGHT0_BACK_COLOR, ART_X, ART_Y, ART_BOX, ART_BOX);
	small_resource_lock.lock();
	int handle = album_art_handle;
	small_resource_lock.unlock();
	if (handle != -1 && thumbnail_is_available(handle)) {
		int w = 0, h = 0;
		thumbnail_get_dimensions(handle, &w, &h);
		if (w > 0 && h > 0) {
			float scale = std::min(ART_BOX / w, ART_BOX / h);
			float dw = w * scale, dh = h * scale;
			thumbnail_draw(handle, ART_X + (ART_BOX - dw) / 2, ART_Y + (ART_BOX - dh) / 2, dw, dh);
		}
	}

	// text
	constexpr float TEXT_X = ART_X + ART_BOX + 14;
	constexpr float TEXT_W = 400 - TEXT_X - 10;
	float y = ART_Y + 2;
	for (auto &line : truncate_str(track.title.empty() ? LOCALIZED(LOADING) : track.title, TEXT_W, 3, 0.65, 0.65)) {
		Draw(line, TEXT_X, y, 0.65, 0.65, DEFAULT_TEXT_COLOR);
		y += 19;
	}
	y += 6;
	for (auto &line : truncate_str(track.artist, TEXT_W, 2, 0.5, 0.5)) {
		Draw(line, TEXT_X, y, 0.5, 0.5, LIGHT0_TEXT_COLOR);
		y += 15;
	}
	if (!track.album.empty()) {
		for (auto &line : truncate_str(track.album, TEXT_W, 1, 0.45, 0.45)) {
			Draw(line, TEXT_X, y, 0.45, 0.45, LIGHT1_TEXT_COLOR);
			y += 14;
		}
	}
	// playback state
	std::string state;
	if (get_network_waiting_status() || is_async_task_running(load_video_page)) {
		state = LOCALIZED(LOADING);
	} else if (vid_play_request && vid_pausing) {
		state = LOCALIZED(PAUSED);
	}
	std::string modes;
	if (MusicQueue::get_shuffle()) {
		modes = LOCALIZED(SHUFFLE);
	}
	if (MusicQueue::get_repeat() != MusicQueue::RepeatMode::OFF) {
		modes += (modes.empty() ? "" : " • ") + repeat_mode_label(MusicQueue::get_repeat());
	}
	if (!state.empty()) {
		Draw(state, TEXT_X, ART_Y + ART_BOX - 30, 0.45, 0.45, ACCENT_COLOR);
	}
	if (!modes.empty()) {
		Draw(modes, TEXT_X, ART_Y + ART_BOX - 14, 0.45, 0.45, LIGHT1_TEXT_COLOR);
	}

	// progress
	constexpr float BAR_X0 = ART_X;
	constexpr float BAR_X1 = 400 - ART_X;
	constexpr float BAR_Y = ART_Y + ART_BOX + 10;
	double progress = network_decoder.ready && vid_duration > 0 ? network_decoder.get_bar_pos_from_timestamp(vid_current_pos) : 0;
	progress = std::max(0.0, std::min(1.0, progress));
	Draw_texture(var_square_image[0], LIGHT0_BACK_COLOR, BAR_X0, BAR_Y, BAR_X1 - BAR_X0, 4);
	Draw_texture(var_square_image[0], ACCENT_COLOR, BAR_X0, BAR_Y, (BAR_X1 - BAR_X0) * progress, 4);
	Draw(Util_convert_seconds_to_time(vid_current_pos), BAR_X0, BAR_Y + 6, 0.4, 0.4, LIGHT1_TEXT_COLOR);
	Draw_right(Util_convert_seconds_to_time(vid_duration), BAR_X1, BAR_Y + 6, 0.4, 0.4, LIGHT1_TEXT_COLOR);

	MusicTrack next = MusicQueue::peek_next();
	if (!next.video_id.empty()) {
		std::string next_str = LOCALIZED(UP_NEXT) + " : " + (next.title.empty() ? next.video_id : next.title);
		if (!next.artist.empty()) {
			next_str += " • " + next.artist;
		}
		auto lines = truncate_str(next_str, 260, 1, 0.4, 0.4);
		Draw_x_centered(lines.size() ? lines[0] : "", 70, 330, BAR_Y + 6, 0.4, 0.4, LIGHT1_TEXT_COLOR);
	}
}

void video_draw_top_screen() {
	Draw_screen_ready(0, DEFAULT_BACK_COLOR);
	if (!video_skip_drawing) {
		draw_now_playing_top();
	}
	logger.draw();
	Draw_top_ui();
	if (var_debug_mode) {
		Draw_debug_info();
	}
}

/* ------------------------------------------------------------------------------------------------------------------ */
// threads

static void decode_thread(void *arg) {
	logger.info(DEF_SAPP0_DECODE_THREAD_STR, "Thread started.");

	Result_with_string result;
	int ch = 0;
	int audio_size = 0;

	while (vid_thread_run) {
		if (vid_play_request || vid_change_video_request) {
			vid_current_pos = 0;
			vid_duration = 0;
			vid_audio_format = "n/a";
			vid_change_video_request = false;
			vid_seek_request = false;
			eof_reached = false;
			vid_pausing = false;
			vid_play_request = true;

			network_waiting_status = "Reading Stream";
			small_resource_lock.lock();
			YouTubeVideoDetail info_to_play = playing_video_info;
			small_resource_lock.unlock();
			std::string url_to_play = youtube_get_video_url_by_id(info_to_play.id);
			result = network_decoder.init(info_to_play.audio_stream_url, stream_downloader,
			                              info_to_play.is_livestream ? info_to_play.stream_fragment_len : -1,
			                              info_to_play.needs_timestamp_adjusting(), var_is_new3ds);

			logger.info(DEF_SAPP0_DECODE_THREAD_STR,
			            "network_decoder.init()..." + result.string + result.error_description, result.code);
			if (result.code != 0) {
				small_resource_lock.lock();
				// ignore the failure if another track was requested in the meantime
				if (url_to_play == cur_playing_url && !vid_change_video_request) {
					if (video_retry_left > 0) {
						video_retry_left--;
						logger.caution("dec", "failed, retrying. retry cnt left:" + std::to_string(video_retry_left));
						int retry_left = video_retry_left;
						play_current_track_wo_lock(true);
						video_retry_left = retry_left;
					} else if (++consecutive_skips < MAX_CONSECUTIVE_SKIPS && MusicQueue::next(false)) {
						music_player_show_notice(LOCALIZED(SKIPPED_UNPLAYABLE));
						play_current_track_wo_lock(false);
					} else {
						consecutive_skips = 0;
						music_player_show_notice(LOCALIZED(CANNOT_PLAY) + " : " + result.string);
					}
				}
				small_resource_lock.unlock();
				vid_play_request = false;
			}
			network_waiting_status = NULL;

			if (vid_play_request) {
				consecutive_skips = 0; // the track actually started
				auto info = network_decoder.get_audio_info();
				vid_sample_rate = info.sample_rate;
				ch = info.ch;
				vid_audio_format = info.format_name;
				vid_duration = info.duration;
				Util_speaker_init(0, ch, vid_sample_rate);
			}

			if (seek_at_init_request >= 0) {
				vid_seek_request = true;
				vid_seek_pos = seek_at_init_request;
				seek_at_init_request = -1;
			}

			while (vid_play_request) {
				if (vid_seek_request && !vid_change_video_request) {
					network_waiting_status = "Seeking";
					Util_speaker_clear_buffer(0);
					network_decoder_critical_lock.lock();
					vid_current_pos = vid_seek_pos;
					while (vid_seek_request && !vid_change_video_request && vid_play_request) {
						small_resource_lock.lock();
						double seek_pos_bak = vid_seek_pos;
						vid_seek_request = false;
						network_decoder.interrupt = false;
						small_resource_lock.unlock();

						result = network_decoder.seek(seek_pos_bak * 1000 * 1000);
						// if seek failed because of the lock, it's probably another seek request or other requests
						// (change track etc...), so we can ignore it
						if (network_decoder.interrupt) {
							logger.info(DEF_SAPP0_DECODE_THREAD_STR, "seek interrupted");
							continue;
						}
						logger.info(DEF_SAPP0_DECODE_THREAD_STR,
						            "network_decoder.seek()..." + result.string + result.error_description,
						            result.code);
						if (result.code != 0) {
							vid_play_request = false;
						}
						break;
					}
					network_decoder_critical_lock.unlock();
					if (eof_reached) {
						vid_pausing = false; // if it was stuck at the end of the track, resume playing after seek
					}
					network_waiting_status = NULL;
					var_need_refresh = true;
				}
				if (vid_change_video_request || !vid_play_request) {
					break;
				}
				vid_duration = network_decoder.get_duration();

				auto type = network_decoder.next_decode_type();

				if (type == NetworkMultipleDecoder::PacketType::EoF) {
					// wait for the audio buffer to be empty
					while (!Util_speaker_is_buffer_empty(0) && vid_play_request && !vid_seek_request &&
					       !vid_change_video_request) {
						usleep(100000);
					}
					if (vid_seek_request || vid_change_video_request || !vid_play_request) {
						continue;
					}
					vid_pausing = true;
					bool break_flag = false;
					if (!eof_reached) { // the first time it reaches the end
						small_resource_lock.lock();
						// The stream ended well before the track's duration : YouTube stopped serving it (expired url,
						// or HTTP 403 after ~1 minute without a PO token). Reload the url and resume from here.
						bool truncated = !playing_video_info.is_livestream && vid_duration > 10 &&
						                 vid_current_pos < vid_duration - 5;
						if (truncated && truncated_reload_url != cur_playing_url) {
							truncated_reload_url = cur_playing_url;
							truncated_reload_count = 0;
						}
						if (truncated && truncated_reload_count < MAX_TRUNCATED_RELOADS) {
							truncated_reload_count++;
							logger.caution("dec", "stream ended early at " + std::to_string(vid_current_pos) + "/" +
							                          std::to_string(vid_duration) + ", reloading");
							seek_at_init_request = vid_current_pos;
							play_current_track_wo_lock(true);
							break_flag = true;
						} else if (MusicQueue::get_repeat() == MusicQueue::RepeatMode::ONE) {
							vid_seek_request = true;
							vid_seek_pos = 0;
						} else if (MusicQueue::next(true)) {
							play_current_track_wo_lock(false);
							break_flag = true;
						} else if (var_autoplay_level != 0 && !is_async_task_running(load_radio)) {
							queue_async_task(load_radio, (void *)1);
						}
						var_need_refresh = true;
						small_resource_lock.unlock();
					}
					eof_reached = true;
					if (break_flag) {
						break;
					}
					usleep(10000);
					continue;
				} else {
					eof_reached = false;
				}

				if (type == NetworkMultipleDecoder::PacketType::AUDIO) {
					double pos;
					u8 *audio = NULL;
					result = network_decoder.decode_audio(&audio_size, &audio, &pos);
					if (result.code == 0) {
						while (true) {
							result = Util_speaker_add_buffer(0, ch, audio, audio_size, pos);
							if (result.code == 0 || !vid_play_request || vid_seek_request ||
							    vid_change_video_request) {
								break;
							}
							usleep(10000);
						}
						free(audio);
						audio = NULL;
					} else if (result.code != DEF_ERR_NEED_MORE_INPUT) { // ignore NEED_MORE_INPUT error
						logger.error(DEF_SAPP0_DECODE_THREAD_STR,
						             "decode_audio()..." + result.string + result.error_description, result.code);
					}
				} else if (type == NetworkMultipleDecoder::PacketType::INTERRUPTED) {
					continue;
				} else if (type == NetworkMultipleDecoder::PacketType::VIDEO) {
					logger.error(DEF_SAPP0_DECODE_THREAD_STR, "unexpected video packet");
				} else {
					logger.error(DEF_SAPP0_DECODE_THREAD_STR, "unknown type of packet");
				}
			}

			network_waiting_status = NULL;

			while (Util_speaker_is_playing(0) && vid_play_request) {
				usleep(10000);
			}
			Util_speaker_exit(0);

			if (!vid_change_video_request) {
				vid_play_request = false;
			}

			network_decoder_critical_lock.lock();
			network_decoder.deinit();
			network_decoder_critical_lock.unlock();

			var_need_refresh = true;
			vid_pausing = false;
			vid_seek_request = false;
			logger.info(DEF_SAPP0_DECODE_THREAD_STR, "deinit complete");
		} else {
			usleep(DEF_ACTIVE_THREAD_SLEEP_TIME);
		}
	}

	logger.info(DEF_SAPP0_DECODE_THREAD_STR, "Thread exit.");
	threadExit(0);
}

// keeps vid_current_pos in sync with the audio actually being played
static void position_thread(void *arg) {
	while (vid_thread_run) {
		if (vid_play_request && !vid_seek_request && !vid_change_video_request && network_decoder.ready) {
			double pos = Util_speaker_get_current_timestamp(0, vid_sample_rate);
			if (pos != -1) {
				if ((int)pos != (int)vid_current_pos) {
					var_need_refresh = true; // the displayed time changed
				}
				vid_current_pos = pos;
			}
			usleep(50000);
		} else {
			usleep(DEF_ACTIVE_THREAD_SLEEP_TIME);
		}
	}
	threadExit(0);
}

/* ------------------------------------------------------------------------------------------------------------------ */
// the now-playing scene

void VideoPlayer_draw(void) {
	Hid_info key;
	Util_hid_query_key_state(&key);

	thumbnail_set_active_scene(SceneType::VIDEO_PLAYER);

	// note : the views of this scene are only touched from this (UI) thread, so no locking is needed for them.
	// Don't hold `small_resource_lock` while updating views : their callbacks (play/next...) lock it themselves.
	update_queue_views();

	if (var_need_refresh || !var_eco_mode) {
		var_need_refresh = false;
		Draw_frame_ready();
		video_draw_top_screen();

		Draw_screen_ready(2, DEFAULT_BACK_COLOR);

		main_view->draw();
		video_draw_playing_bar();
		draw_overlay_menu(240 - VIDEO_PLAYING_BAR_HEIGHT - TAB_SELECTOR_HEIGHT - OVERLAY_MENU_ICON_SIZE);
		equalizer_popup_view->draw();

		if (Util_expl_query_show_flag()) {
			Util_expl_draw();
		}
		if (Util_err_query_error_show_flag()) {
			Util_err_draw();
		}
		Draw_touch_pos();
		Draw_apply_draw();
	} else {
		gspWaitForVBlank();
	}

	if (Util_err_query_error_show_flag()) {
		Util_err_main(key);
	} else if (Util_expl_query_show_flag()) {
		Util_expl_main(key);
	} else {
		update_overlay_menu(&key);
		if (equalizer_popup_view->is_visible) {
			equalizer_popup_view->update(key);
		} else {
			main_view->update(key);
		}
		int clicked = queue_clicked_position;
		int remove = queue_remove_position;
		queue_clicked_position = queue_remove_position = -1;

		if (remove != -1) {
			main_view->reset_holding_status(); // the long hold must not turn into a click
			if (remove == MusicQueue::get_current_position()) {
				music_player_show_notice(LOCALIZED(CANNOT_REMOVE_PLAYING));
			} else {
				MusicQueue::remove(remove);
			}
		} else if (clicked != -1) {
			if (clicked != MusicQueue::get_current_position() && MusicQueue::jump_to(clicked)) {
				music_player_play_queue();
			}
		}

		video_update_playing_bar(key);

		if (key.p_a) {
			toggle_pause();
		} else if ((key.h_x && key.p_b) || (key.h_b && key.p_x)) { // stop
			small_resource_lock.lock();
			vid_play_request = false;
			small_resource_lock.unlock();
		} else if (key.p_b) {
			global_intent.next_scene = SceneType::BACK;
		} else if (key.p_y && !key.h_select) { // Select + Y / X are debug shortcuts
			MusicQueue::set_shuffle(!MusicQueue::get_shuffle());
			var_need_refresh = true;
		} else if (key.p_x && !key.h_select) {
			MusicQueue::cycle_repeat();
			var_need_refresh = true;
		} else if (key.p_zl) {
			music_player_prev();
		} else if (key.p_zr) {
			music_player_next();
		} else if (key.p_d_right || key.p_d_left) {
			if (network_decoder.ready) {
				double pos = vid_current_pos + (key.p_d_right ? 10 : -10);
				pos = std::max<double>(0, std::min<double>(vid_duration, pos));
				send_seek_request(pos);
			}
		}
	}
}
