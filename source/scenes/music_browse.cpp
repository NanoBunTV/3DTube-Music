#include "headers.hpp"
#include <vector>
#include <string>
#include <map>

#include "scenes/music_browse.hpp"
#include "scenes/video_player.hpp"
#include "music/music_parser.hpp"
#include "music/queue.hpp"
#include "ui/overlay.hpp"
#include "ui/colors.hpp"
#include "ui/ui.hpp"
#include "network_decoder/thumbnail_loader.hpp"
#include "util/async_task.hpp"

#define MAX_THUMBNAIL_LOAD_REQUEST 12
#define HEADER_HEIGHT 76
#define HEADER_ART_SIZE 68
#define ITEM_HEIGHT 44
#define BUTTON_HEIGHT 24
#define COLLECTION_CACHE_MAX 8
#define ACCENT_COLOR 0xFF3333D0

namespace MusicBrowse {
bool already_init = false;
bool exiting = false;

Mutex resource_lock;
std::string cur_browse_id;
MusicCollection collection;
std::map<std::string, MusicCollection> collection_cache;

int header_art_handle = -1;
// set by view callbacks, processed in MusicBrowse_draw()
int clicked_track = -1;
int long_held_track = -1;
int clicked_release = -1;
int pressed_button = -1; // 0 : play, 1 : shuffle, 2 : all songs

int CONTENT_Y_HIGH = 240;
ScrollView *main_view = NULL;
VerticalListView *track_list_view = NULL;
VerticalListView *release_list_view = NULL;
View *bottom_view = NULL;
TextView *toast_view = NULL;
int toast_frames_left = 0;
} // namespace MusicBrowse
using namespace MusicBrowse;

static void load_collection(void *);
static void load_more_tracks(void *);

static void show_toast(const std::string &text) {
	toast_view->set_text(text);
	toast_view->set_is_visible(true);
	toast_frames_left = 100;
	var_need_refresh = true;
}

void MusicBrowse_init(void) {
	logger.info("browse/init", "Initializing...");
	toast_view = (new TextView((320 - 160) / 2, 180, 160, DEFAULT_FONT_INTERVAL + SMALL_MARGIN))
	                 ->set_x_alignment(TextView::XAlign::CENTER)
	                 ->set_text_offset(0, -1)
	                 ->set_get_text_color([]() { return (u32)-1; });
	toast_view->set_get_background_color([](const View &) { return 0x50000000; });
	toast_view->set_is_visible(false);
	main_view = new ScrollView(0, 0, 320, 240);
	already_init = true;
}

void MusicBrowse_exit(void) {
	already_init = false;
	exiting = true;
	resource_lock.lock();
	if (header_art_handle != -1) {
		thumbnail_cancel_request(header_art_handle);
		header_art_handle = -1;
	}
	main_view->recursive_delete_subviews();
	delete main_view;
	main_view = NULL;
	delete toast_view;
	toast_view = NULL;
	resource_lock.unlock();
	logger.info("browse/exit", "Exited.");
}

void MusicBrowse_suspend(void) {}

static void show_loading_wo_lock() {
	if (header_art_handle != -1) {
		thumbnail_cancel_request(header_art_handle);
		header_art_handle = -1;
	}
	main_view->recursive_delete_subviews();
	track_list_view = release_list_view = NULL;
	bottom_view = NULL;
	main_view->set_views({(new TextView(0, 0, 320, 40))
	                          ->set_text((std::function<std::string()>)[]() { return LOCALIZED(LOADING); })
	                          ->set_x_alignment(TextView::XAlign::CENTER)});
	main_view->reset();
}

void MusicBrowse_resume(std::string arg) {
	overlay_menu_on_resume();
	if (arg != "" && arg != cur_browse_id) {
		resource_lock.lock();
		cur_browse_id = arg;
		show_loading_wo_lock();
		remove_all_async_tasks_with_type(load_collection);
		remove_all_async_tasks_with_type(load_more_tracks);
		queue_async_task(load_collection, NULL);
		resource_lock.unlock();
	}
	var_need_refresh = true;
}

/* ------------------------------------------------------------------------------------------------------------------ */
// views

static TextView *make_button(std::function<std::string()> text, double width, int id, bool accent) {
	TextView *res = (new TextView(0, 0, width, BUTTON_HEIGHT))
	                    ->set_text(text)
	                    ->set_x_alignment(TextView::XAlign::CENTER)
	                    ->set_text_offset(0, -1);
	res->set_get_text_color([accent]() { return accent ? DEF_DRAW_WHITE : DEFAULT_TEXT_COLOR; });
	res->set_get_background_color([accent](const View &view) -> u32 {
		if (accent) {
			return view.touch_darkness > 0.5 ? 0xFF2828A8 : ACCENT_COLOR;
		}
		u32 standard = View::STANDARD_BACKGROUND(view);
		return standard == DEFAULT_BACK_COLOR ? LIGHT0_BACK_COLOR : standard;
	});
	res->set_on_view_released([id](View &) { pressed_button = id; });
	return res;
}

static View *track_to_view(const MusicTrack &track, int index, bool show_album) {
	constexpr int TITLE_WIDTH = 320 - ITEM_HEIGHT - 3 - SMALL_MARGIN;
	std::string subtitle = track.artist;
	if (show_album && !track.album.empty()) {
		subtitle += " • " + track.album;
	}
	std::string thumbnail = track.thumbnail_url.empty() ? music_video_thumbnail_url(track.video_id)
	                                                    : music_thumbnail_url(track.thumbnail_url, 120);
	SuccinctVideoView *view = (new SuccinctVideoView(0, 0, 320, ITEM_HEIGHT))
	                              ->set_square_thumbnail(true)
	                              ->set_title_lines(truncate_str(track.title, TITLE_WIDTH, 1, 0.5, 0.5))
	                              ->set_auxiliary_lines(truncate_str(subtitle, TITLE_WIDTH, 1, 0.5, 0.5))
	                              ->set_bottom_right_overlay(track.duration_text)
	                              ->set_thumbnail_url(thumbnail);
	view->set_get_background_color(View::STANDARD_BACKGROUND);
	view->add_on_long_hold(40, [index](View &) { long_held_track = index; });
	view->set_on_view_released([index](View &) { clicked_track = index; });
	return view;
}

static View *release_to_view(const MusicItem &item, int index) {
	constexpr int TITLE_WIDTH = 320 - ITEM_HEIGHT - 3 - SMALL_MARGIN;
	SuccinctVideoView *view = (new SuccinctVideoView(0, 0, 320, ITEM_HEIGHT))
	                              ->set_square_thumbnail(true)
	                              ->set_title_lines(truncate_str(item.title, TITLE_WIDTH, 1, 0.5, 0.5))
	                              ->set_auxiliary_lines(truncate_str(item.subtitle, TITLE_WIDTH, 1, 0.5, 0.5))
	                              ->set_thumbnail_url(music_thumbnail_url(item.thumbnail_url, 120));
	view->set_get_background_color(View::STANDARD_BACKGROUND);
	view->set_on_view_released([index](View &) { clicked_release = index; });
	return view;
}

static View *section_title(const std::string &text) {
	return (new TextView(SMALL_MARGIN, 0, 320 - SMALL_MARGIN, DEFAULT_FONT_INTERVAL + SMALL_MARGIN * 2))
	    ->set_text(text)
	    ->set_font_size(0.55, DEFAULT_FONT_INTERVAL + SMALL_MARGIN * 2);
}

static View *make_bottom_view() {
	if (collection.error != "" && collection.tracks.size()) {
		return (new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL * 2))
		    ->set_text(collection.error)
		    ->set_x_alignment(TextView::XAlign::CENTER);
	}
	if (collection.has_more()) {
		return (new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL * 2))
		    ->set_text((std::function<std::string()>)[]() { return LOCALIZED(LOADING); })
		    ->set_x_alignment(TextView::XAlign::CENTER);
	}
	return new EmptyView(0, 0, 320, SMALL_MARGIN);
}

// rebuilds every view from `collection`; should be called while `resource_lock` is locked
static void build_views_wo_lock() {
	main_view->recursive_delete_subviews();
	if (header_art_handle != -1) {
		thumbnail_cancel_request(header_art_handle);
		header_art_handle = -1;
	}
	if (collection.error != "" && collection.tracks.empty() && collection.releases.empty()) {
		main_view->set_views({(new TextView(0, 0, 320, 40))
		                          ->set_text(collection.error)
		                          ->set_x_alignment(TextView::XAlign::CENTER)});
		track_list_view = release_list_view = NULL;
		bottom_view = NULL;
		return;
	}
	if (collection.thumbnail_url != "") {
		header_art_handle = thumbnail_request(music_thumbnail_url(collection.thumbnail_url, 120),
		                                      SceneType::MUSIC_BROWSE, PRIORITY_FOREGROUND * 2,
		                                      collection.kind == MusicCollection::Kind::ARTIST ? ThumbnailType::ICON
		                                                                                       : ThumbnailType::DEFAULT);
	}
	auto title_lines = truncate_str(collection.title, 320 - HEADER_ART_SIZE - SMALL_MARGIN * 4, 2, 0.6, 0.6);
	auto subtitle_lines = truncate_str(collection.subtitle, 320 - HEADER_ART_SIZE - SMALL_MARGIN * 4, 2, 0.45, 0.45);
	View *header = (new CustomView(0, 0, 320, HEADER_HEIGHT))->set_draw([title_lines, subtitle_lines](const CustomView &view) {
		float x = SMALL_MARGIN * 2;
		float y = view.y0 + (HEADER_HEIGHT - HEADER_ART_SIZE) / 2;
		Draw_texture(var_square_image[0], LIGHT0_BACK_COLOR, x, y, HEADER_ART_SIZE, HEADER_ART_SIZE);
		thumbnail_draw(header_art_handle, x, y, HEADER_ART_SIZE, HEADER_ART_SIZE);
		float tx = x + HEADER_ART_SIZE + SMALL_MARGIN * 2;
		float ty = y + 2;
		for (auto &line : title_lines) {
			Draw(line, tx, ty, 0.6, 0.6, DEFAULT_TEXT_COLOR);
			ty += 17;
		}
		ty += 2;
		for (auto &line : subtitle_lines) {
			Draw(line, tx, ty, 0.45, 0.45, LIGHT1_TEXT_COLOR);
			ty += 13;
		}
	});

	std::vector<View *> views = {header};
	if (collection.tracks.size()) {
		views.push_back((new HorizontalListView(0, 0, BUTTON_HEIGHT))
		                    ->set_views({new EmptyView(0, 0, SMALL_MARGIN * 2, BUTTON_HEIGHT),
		                                 make_button([]() { return LOCALIZED(PLAY_ALL); }, 145, 0, true),
		                                 new EmptyView(0, 0, SMALL_MARGIN * 2, BUTTON_HEIGHT),
		                                 make_button([]() { return LOCALIZED(SHUFFLE); }, 145, 1, false)}));
		views.push_back(new EmptyView(0, 0, 320, SMALL_MARGIN * 2));
	}

	bool is_artist = collection.kind == MusicCollection::Kind::ARTIST;
	if (is_artist && collection.tracks.size()) {
		views.push_back(section_title(LOCALIZED(TOP_SONGS)));
	}
	track_list_view = (new VerticalListView(0, 0, 320))
	                      ->set_margin(SMALL_MARGIN)
	                      ->enable_thumbnail_request_update(MAX_THUMBNAIL_LOAD_REQUEST, SceneType::MUSIC_BROWSE);
	for (size_t i = 0; i < collection.tracks.size(); i++) {
		track_list_view->views.push_back(
		    track_to_view(collection.tracks[i], i, collection.kind != MusicCollection::Kind::ALBUM));
	}
	views.push_back(track_list_view);

	release_list_view = NULL;
	if (is_artist) {
		if (collection.all_songs_browse_id != "") {
			views.push_back(new EmptyView(0, 0, 320, SMALL_MARGIN));
			views.push_back((new HorizontalListView(0, 0, BUTTON_HEIGHT))
			                    ->set_views({new EmptyView(0, 0, SMALL_MARGIN * 2, BUTTON_HEIGHT),
			                                 make_button([]() { return LOCALIZED(ALL_SONGS); }, 145, 2, false)}));
		}
		if (collection.releases.size()) {
			views.push_back(section_title(LOCALIZED(ALBUMS_AND_SINGLES)));
			release_list_view = (new VerticalListView(0, 0, 320))
			                        ->set_margin(SMALL_MARGIN)
			                        ->enable_thumbnail_request_update(MAX_THUMBNAIL_LOAD_REQUEST, SceneType::MUSIC_BROWSE);
			for (size_t i = 0; i < collection.releases.size(); i++) {
				release_list_view->views.push_back(release_to_view(collection.releases[i], i));
			}
			views.push_back(release_list_view);
		}
	}
	bottom_view = make_bottom_view();
	views.push_back(bottom_view);
	main_view->set_views(views);
	main_view->set_on_child_drawn(views.size() - 1, [](const ScrollView &, int) {
		if (collection.has_more() && collection.error == "" && !is_async_task_running(load_collection) &&
		    !is_async_task_running(load_more_tracks)) {
			queue_async_task(load_more_tracks, NULL);
		}
	});
}

/* ------------------------------------------------------------------------------------------------------------------ */
// async tasks

static void load_collection(void *) {
	resource_lock.lock();
	std::string browse_id = cur_browse_id;
	bool cached = collection_cache.count(browse_id);
	MusicCollection result;
	if (cached) {
		result = collection_cache[browse_id];
	}
	resource_lock.unlock();

	if (!cached) {
		add_cpu_limit(ADDITIONAL_CPU_LIMIT);
		const std::string account_prefix = MUSIC_ACCOUNT_PREFIX;
		if (browse_id.compare(0, account_prefix.size(), account_prefix) == 0) { // the user's own playlist
			result = music_load_account_playlist(browse_id.substr(account_prefix.size()));
		} else {
			result = music_load_collection(browse_id);
		}
		remove_cpu_limit(ADDITIONAL_CPU_LIMIT);
	}

	resource_lock.lock();
	if (exiting || browse_id != cur_browse_id) {
		resource_lock.unlock();
		return;
	}
	collection = result;
	if (result.error == "" && !result.from_account) { // account playlists change (liked songs...) : not cached
		if (collection_cache.size() >= COLLECTION_CACHE_MAX) {
			collection_cache.clear();
		}
		collection_cache[browse_id] = result;
	}
	build_views_wo_lock();
	main_view->reset();
	var_need_refresh = true;
	resource_lock.unlock();
}

static void load_more_tracks(void *) {
	resource_lock.lock();
	MusicCollection result = collection;
	resource_lock.unlock();

	add_cpu_limit(ADDITIONAL_CPU_LIMIT);
	result.load_more();
	remove_cpu_limit(ADDITIONAL_CPU_LIMIT);

	resource_lock.lock();
	if (exiting || result.browse_id != cur_browse_id || !track_list_view) {
		resource_lock.unlock();
		return;
	}
	for (size_t i = collection.tracks.size(); i < result.tracks.size(); i++) {
		track_list_view->views.push_back(
		    track_to_view(result.tracks[i], i, result.kind != MusicCollection::Kind::ALBUM));
	}
	collection = result;
	if (!collection.from_account) {
		collection_cache[collection.browse_id] = collection;
	}
	// replace the bottom view (always the last one)
	View *new_bottom_view = make_bottom_view();
	for (auto &view : main_view->views) {
		if (view == bottom_view) {
			delete view;
			view = new_bottom_view;
		}
	}
	bottom_view = new_bottom_view;
	var_need_refresh = true;
	resource_lock.unlock();
}

/* ------------------------------------------------------------------------------------------------------------------ */

// should be called while `resource_lock` is locked
static void process_actions_wo_lock() {
	if (long_held_track != -1) {
		int index = long_held_track;
		long_held_track = clicked_track = -1;
		main_view->reset_holding_status(); // the long hold must not turn into a click
		if (index < (int)collection.tracks.size()) {
			bool was_empty = !MusicQueue::has_current();
			MusicQueue::add_next(collection.tracks[index]);
			if (was_empty) {
				music_player_play_queue();
			}
			show_toast(LOCALIZED(ADDED_TO_QUEUE));
		}
	}
	if (clicked_track != -1) {
		int index = clicked_track;
		clicked_track = -1;
		if (index < (int)collection.tracks.size()) {
			MusicQueue::set(collection.tracks, index);
			music_player_play_queue();
			global_intent.next_scene = SceneType::VIDEO_PLAYER;
			global_intent.arg = "";
		}
	}
	if (pressed_button == 0 || pressed_button == 1) {
		bool shuffle = pressed_button == 1;
		pressed_button = -1;
		if (collection.tracks.size()) {
			MusicQueue::set_shuffle(shuffle);
			int start = shuffle ? rand() % collection.tracks.size() : 0;
			MusicQueue::set(collection.tracks, start);
			music_player_play_queue();
			global_intent.next_scene = SceneType::VIDEO_PLAYER;
			global_intent.arg = "";
		}
	} else if (pressed_button == 2) {
		pressed_button = -1;
		if (collection.all_songs_browse_id != "") {
			global_intent.next_scene = SceneType::MUSIC_BROWSE;
			global_intent.arg = collection.all_songs_browse_id;
		}
	}
	if (clicked_release != -1) {
		int index = clicked_release;
		clicked_release = -1;
		if (index < (int)collection.releases.size()) {
			global_intent.next_scene = SceneType::MUSIC_BROWSE;
			global_intent.arg = collection.releases[index].browse_id;
		}
	}
}

void MusicBrowse_draw(void) {
	Hid_info key;
	Util_hid_query_key_state(&key);

	thumbnail_set_active_scene(SceneType::MUSIC_BROWSE);

	bool video_playing_bar_show = video_is_playing();
	CONTENT_Y_HIGH = video_playing_bar_show ? 240 - VIDEO_PLAYING_BAR_HEIGHT : 240;
	main_view->update_y_range(0, CONTENT_Y_HIGH);

	if (toast_frames_left > 0 && --toast_frames_left == 0) {
		toast_view->set_is_visible(false);
		var_need_refresh = true;
	}

	if (var_need_refresh || !var_eco_mode) {
		var_need_refresh = false;
		Draw_frame_ready();
		video_draw_top_screen();

		Draw_screen_ready(2, DEFAULT_BACK_COLOR);

		resource_lock.lock();
		main_view->draw();
		if (toast_frames_left > 0) {
			toast_view->draw();
		}
		resource_lock.unlock();

		if (video_playing_bar_show) {
			video_draw_playing_bar();
		}
		draw_overlay_menu(video_playing_bar_show ? 240 - OVERLAY_MENU_ICON_SIZE - VIDEO_PLAYING_BAR_HEIGHT
		                                         : 240 - OVERLAY_MENU_ICON_SIZE);

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

		resource_lock.lock();
		main_view->update(key);
		process_actions_wo_lock();
		resource_lock.unlock();

		if (video_playing_bar_show) {
			video_update_playing_bar(key);
		}
		if (key.p_b) {
			global_intent.next_scene = SceneType::BACK;
		}
	}
}
