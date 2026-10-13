#include "headers.hpp"
#include <vector>
#include <string>

#include "scenes/home.hpp"
#include "scenes/search.hpp"
#include "scenes/video_player.hpp"
#include "ui/ui.hpp"
#include "ui/overlay.hpp"
#include "network_decoder/thumbnail_loader.hpp"
#include "util/async_task.hpp"
#include "music/music_parser.hpp"
#include "music/queue.hpp"
#include "oauth/oauth.hpp"

#define MAX_THUMBNAIL_LOAD_REQUEST 12
#define TOP_HEIGHT 25
#define LIBRARY_ITEM_HEIGHT 44

namespace Home {
bool thread_suspend = false;
bool already_init = false;
bool exiting = false;

Mutex resource_lock;

MusicHomeResult home_info; // YouTube Music home shelves
// clicked home item, processed in Home_draw()
int clicked_shelf = -1;
int clicked_item = -1;
int long_held_shelf = -1;
int long_held_item = -1;

int CONTENT_Y_HIGH = 240; // changes according to whether the playing bar is drawn or not

VerticalListView *main_view = NULL;
TabView *main_tab_view = NULL;
ScrollView *home_tab_view = NULL;
VerticalListView *home_list_view = NULL;
View *home_bottom_view = NULL;

// Library tab : the signed-in user's playlists (loaded the first time the tab is shown)
MusicLibraryResult library_info;
bool library_loaded = false;    // library_info is up to date for the current sign-in state
bool library_signed_in = false; // sign-in state the Library tab was built for
int clicked_library = -1;       // 0 : Liked Music, i + 1 : library_info.playlists[i]
ScrollView *library_tab_view = NULL;
VerticalListView *library_list_view = NULL;
View *library_bottom_view = NULL;
}; // namespace Home
using namespace Home;

static void load_home_page(void *);
static void load_library(void *);
static void update_home_bottom_view(bool loading);
static void update_library_views(bool loading);

static void request_reload() {
	if (!is_async_task_running(load_home_page)) {
		queue_async_task(load_home_page, NULL);
	}
}
static void request_library_reload() {
	if (OAuth::is_authenticated() && !is_async_task_running(load_library)) {
		queue_async_task(load_library, NULL);
	} else if (library_tab_view) {
		library_tab_view->finish_pull_refresh();
	}
}

void Home_init(void) {
	logger.info("home/init", "Initializing...");

	exiting = false;
	home_list_view = (new VerticalListView(0, 0, 320))
	                     ->set_margin(SMALL_MARGIN)
	                     ->enable_thumbnail_request_update(MAX_THUMBNAIL_LOAD_REQUEST, SceneType::HOME);
	home_bottom_view = new EmptyView(0, 0, 320, 0);
	home_tab_view = (new ScrollView(0, 0, 320, 0))
	                    ->set_views({home_list_view, home_bottom_view})
	                    ->set_pull_to_refresh(!var_disable_pull_to_refresh, request_reload);
	library_list_view = (new VerticalListView(0, 0, 320))
	                        ->set_margin(SMALL_MARGIN)
	                        ->enable_thumbnail_request_update(MAX_THUMBNAIL_LOAD_REQUEST, SceneType::HOME);
	library_bottom_view = new EmptyView(0, 0, 320, 0);
	library_tab_view = (new ScrollView(0, 0, 320, 0))
	                       ->set_views({library_list_view, library_bottom_view})
	                       ->set_pull_to_refresh(!var_disable_pull_to_refresh, request_library_reload);
	library_loaded = false;
	library_signed_in = OAuth::is_authenticated();
	update_library_views(false);
	main_tab_view = (new TabView(0, 0, 320, CONTENT_Y_HIGH - TOP_HEIGHT))
	                    ->set_views({home_tab_view, library_tab_view})
	                    ->set_tab_texts<std::function<std::string()>>(
	                        {[]() { return LOCALIZED(HOME); }, []() { return LOCALIZED(LIBRARY); }});
	main_view = (new VerticalListView(0, 0, 320))
	                ->set_views({(new CustomView(0, 0, 320, TOP_HEIGHT))
	                                 ->set_draw([](const CustomView &) { return Search_get_search_bar_view()->draw(); })
	                                 ->set_update([](const CustomView &, Hid_info key) {
		                                 return Search_get_search_bar_view()->update(key);
	                                 }),
	                             main_tab_view})
	                ->set_draw_order({1, 0});

	queue_async_task(load_home_page, NULL);

	Home_resume("");
	already_init = true;
}
void Home_exit(void) {
	already_init = false;
	thread_suspend = false;
	exiting = true;

	resource_lock.lock();
	main_view->recursive_delete_subviews();
	delete main_view;
	main_view = NULL;
	main_tab_view = NULL;
	home_tab_view = NULL;
	home_list_view = NULL;
	home_bottom_view = NULL;
	library_tab_view = NULL;
	library_list_view = NULL;
	library_bottom_view = NULL;
	resource_lock.unlock();

	logger.info("home/exit", "Exited.");
}
void Home_suspend(void) { thread_suspend = true; }

void Home_update_pull_to_refresh(void) {
	if (!already_init) {
		return;
	}
	resource_lock.lock();
	home_tab_view->set_pull_to_refresh(!var_disable_pull_to_refresh, request_reload);
	library_tab_view->set_pull_to_refresh(!var_disable_pull_to_refresh, request_library_reload);
	update_home_bottom_view(is_async_task_running(load_home_page)); // shows/hides the reload buttons
	update_library_views(is_async_task_running(load_library));
	resource_lock.unlock();
	var_need_refresh = true;
}

void Home_resume(std::string arg) {
	(void)arg;
	overlay_menu_on_resume();
	thread_suspend = false;
	var_need_refresh = true;

	// signed in or out in the settings : the library is reloaded when the tab is shown
	if (already_init && OAuth::is_authenticated() != library_signed_in) {
		resource_lock.lock();
		library_signed_in = OAuth::is_authenticated();
		library_loaded = false;
		library_info = MusicLibraryResult();
		update_library_views(false);
		resource_lock.unlock();
	}
}

// should be called while `resource_lock` is locked
// shows "Loading", the error (tap to retry), or a reload button if pull-to-refresh is disabled
static void update_home_bottom_view(bool loading) {
	View *new_view;
	if (loading || home_info.error != "" || var_disable_pull_to_refresh) {
		TextView *text_view = (new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL + SMALL_MARGIN * 2))
		                          ->set_text(loading                    ? LOCALIZED(LOADING)
		                                     : home_info.error != "" ? home_info.error + " (" + LOCALIZED(RETRY) + ")"
		                                                            : LOCALIZED(RELOAD))
		                          ->set_x_alignment(TextView::XAlign::CENTER);
		if (!loading) {
			text_view->set_get_background_color(View::STANDARD_BACKGROUND);
			text_view->set_on_view_released([](View &) { request_reload(); });
		}
		new_view = text_view;
	} else {
		new_view = new EmptyView(0, 0, 320, SMALL_MARGIN);
	}
	home_tab_view->set_views({home_list_view, new_view});
	delete home_bottom_view;
	home_bottom_view = new_view;
}

static std::vector<View *> home_shelves_to_views(const MusicHomeResult &home) {
	constexpr int ITEM_HEIGHT = 44;
	constexpr int TITLE_WIDTH = 320 - ITEM_HEIGHT - 3 - SMALL_MARGIN;
	std::vector<View *> res;
	for (size_t s = 0; s < home.shelves.size(); s++) {
		const auto &shelf = home.shelves[s];
		res.push_back((new TextView(SMALL_MARGIN, 0, 320 - SMALL_MARGIN, DEFAULT_FONT_INTERVAL + SMALL_MARGIN * 2))
		                  ->set_text(shelf.title)
		                  ->set_font_size(0.55, DEFAULT_FONT_INTERVAL + SMALL_MARGIN * 2));
		for (size_t i = 0; i < shelf.items.size(); i++) {
			const auto &item = shelf.items[i];
			SuccinctVideoView *view = (new SuccinctVideoView(0, 0, 320, ITEM_HEIGHT))
			                              ->set_square_thumbnail(true)
			                              ->set_title_lines(truncate_str(item.title, TITLE_WIDTH, 1, 0.5, 0.5))
			                              ->set_auxiliary_lines(truncate_str(item.subtitle, TITLE_WIDTH, 1, 0.5, 0.5))
			                              ->set_thumbnail_url(music_thumbnail_url(item.thumbnail_url, 120));
			view->set_get_background_color(View::STANDARD_BACKGROUND);
			int shelf_index = s, item_index = i;
			if (item.type == MusicItem::Type::SONG) {
				view->add_on_long_hold(40, [shelf_index, item_index](View &) {
					long_held_shelf = shelf_index;
					long_held_item = item_index;
				});
			}
			view->set_on_view_released([shelf_index, item_index](View &) {
				clicked_shelf = shelf_index;
				clicked_item = item_index;
			});
			res.push_back(view);
		}
	}
	return res;
}

static void load_home_page(void *) {
	resource_lock.lock();
	if (exiting) {
		resource_lock.unlock();
		return;
	}
	home_info.error = "";
	update_home_bottom_view(true);
	var_need_refresh = true;
	resource_lock.unlock();

	add_cpu_limit(ADDITIONAL_CPU_LIMIT);
	auto results = music_load_home();
	remove_cpu_limit(ADDITIONAL_CPU_LIMIT);

	std::vector<View *> new_views = home_shelves_to_views(results);

	resource_lock.lock();
	if (exiting) {
		for (auto view : new_views) {
			delete view;
		}
		resource_lock.unlock();
		return;
	}
	home_info = results;
	home_list_view->recursive_delete_subviews();
	home_list_view->set_views(new_views);
	update_home_bottom_view(false);
	home_tab_view->finish_pull_refresh();

	var_need_refresh = true;
	resource_lock.unlock();
}

static SuccinctVideoView *library_item_view(const std::string &title, const std::string &subtitle,
                                            const std::string &thumbnail_url, int index) {
	constexpr int THUMBNAIL_WIDTH = LIBRARY_ITEM_HEIGHT * VIDEO_LIST_THUMBNAIL_WIDTH / VIDEO_LIST_THUMBNAIL_HEIGHT;
	constexpr int TITLE_WIDTH = 320 - THUMBNAIL_WIDTH - 3 - SMALL_MARGIN;
	SuccinctVideoView *view = (new SuccinctVideoView(0, 0, 320, LIBRARY_ITEM_HEIGHT))
	                              ->set_title_lines(truncate_str(title, TITLE_WIDTH, 1, 0.5, 0.5))
	                              ->set_auxiliary_lines(truncate_str(subtitle, TITLE_WIDTH, 1, 0.5, 0.5))
	                              ->set_thumbnail_url(thumbnail_url);
	view->set_get_background_color(View::STANDARD_BACKGROUND);
	view->set_on_view_released([index](View &) { clicked_library = index; });
	return view;
}

// rebuilds the Library tab from `library_info`; should be called while `resource_lock` is locked
static void update_library_views(bool loading) {
	std::vector<View *> items;
	std::vector<std::string> status_lines;
	bool status_tappable = false; // retry / reload
	if (!OAuth::is_authenticated()) {
		// "Sign in under %0 ..." with %0 = "Settings > Advanced"
		std::string hint = LOCALIZED(LIBRARY_SIGN_IN_HINT);
		auto pos = hint.find("%0");
		if (pos != std::string::npos) {
			hint.replace(pos, 2, LOCALIZED(SETTINGS) + " > " + LOCALIZED(SETTINGS_ADVANCED));
		}
		status_lines = truncate_str(hint, 320 - SMALL_MARGIN * 4, 4, 0.5, 0.5);
	} else if (loading) {
		status_lines = {LOCALIZED(LOADING)};
	} else if (library_info.error != "") {
		status_lines = truncate_str(library_info.error + " (" + LOCALIZED(RETRY) + ")", 320 - SMALL_MARGIN * 4, 3, 0.5,
		                            0.5);
		status_tappable = true;
	} else if (library_loaded) {
		items.push_back(library_item_view(LOCALIZED(LIKED_MUSIC), "", "", 0));
		for (size_t i = 0; i < library_info.playlists.size(); i++) {
			const auto &playlist = library_info.playlists[i];
			items.push_back(library_item_view(playlist.title, playlist.subtitle, playlist.thumbnail_url, i + 1));
		}
		if (var_disable_pull_to_refresh) {
			status_lines = {LOCALIZED(RELOAD)};
			status_tappable = true;
		}
	}
	library_list_view->recursive_delete_subviews();
	library_list_view->views = items;

	View *new_bottom_view;
	if (status_lines.size()) {
		TextView *text_view =
		    (new TextView(0, 0, 320, DEFAULT_FONT_INTERVAL * status_lines.size() + SMALL_MARGIN * 2))
		        ->set_text_lines(status_lines)
		        ->set_x_alignment(TextView::XAlign::CENTER);
		if (status_tappable) {
			text_view->set_get_background_color(View::STANDARD_BACKGROUND);
			text_view->set_on_view_released([](View &) { request_library_reload(); });
		}
		new_bottom_view = text_view;
	} else {
		new_bottom_view = new EmptyView(0, 0, 320, SMALL_MARGIN);
	}
	library_tab_view->set_views({library_list_view, new_bottom_view});
	delete library_bottom_view;
	library_bottom_view = new_bottom_view;
	var_need_refresh = true;
}

static void load_library(void *) {
	resource_lock.lock();
	if (exiting) {
		resource_lock.unlock();
		return;
	}
	library_info.error = "";
	update_library_views(true);
	resource_lock.unlock();

	add_cpu_limit(ADDITIONAL_CPU_LIMIT);
	MusicLibraryResult result = music_load_library();
	remove_cpu_limit(ADDITIONAL_CPU_LIMIT);
	if (result.error != "") {
		logger.warning("home/library", result.error);
	}

	resource_lock.lock();
	if (exiting) {
		resource_lock.unlock();
		return;
	}
	library_info = result;
	library_loaded = true;
	update_library_views(false);
	library_tab_view->finish_pull_refresh();
	resource_lock.unlock();
}

void Home_draw(void) {
	Hid_info key;
	Util_hid_query_key_state(&key);

	thumbnail_set_active_scene(SceneType::HOME);

	bool video_playing_bar_show = video_is_playing();
	CONTENT_Y_HIGH = video_playing_bar_show ? 240 - VIDEO_PLAYING_BAR_HEIGHT : 240;

	if (var_need_refresh || !var_eco_mode) {
		var_need_refresh = false;
		Draw_frame_ready();
		video_draw_top_screen();

		Draw_screen_ready(2, DEFAULT_BACK_COLOR);

		resource_lock.lock();
		main_view->draw();
		resource_lock.unlock();

		if (video_playing_bar_show) {
			video_draw_playing_bar();
		}
		draw_overlay_menu(CONTENT_Y_HIGH - OVERLAY_MENU_ICON_SIZE - main_tab_view->tab_selector_height);

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

	resource_lock.lock();

	if (Util_err_query_error_show_flag()) {
		Util_err_main(key);
	} else if (Util_expl_query_show_flag()) {
		Util_expl_main(key);
	} else {
		update_overlay_menu(&key);

		main_tab_view->update_y_range(0, CONTENT_Y_HIGH - TOP_HEIGHT);
		home_tab_view->update_y_range(0, CONTENT_Y_HIGH - TOP_HEIGHT - main_tab_view->tab_selector_height);
		library_tab_view->update_y_range(0, CONTENT_Y_HIGH - TOP_HEIGHT - main_tab_view->tab_selector_height);
		main_view->update(key);

		// the library is loaded the first time its tab is shown
		if (main_tab_view->selected_tab == 1 && !library_loaded && OAuth::is_authenticated() &&
		    !is_async_task_running(load_library)) {
			queue_async_task(load_library, NULL);
		}
		if (clicked_library != -1) {
			int index = clicked_library;
			clicked_library = -1;
			std::string browse_id = index == 0 ? MUSIC_LIKED_MUSIC_BROWSE_ID
			                        : index - 1 < (int)library_info.playlists.size()
			                            ? library_info.playlists[index - 1].browse_id
			                            : "";
			if (browse_id != "") {
				global_intent.next_scene = SceneType::MUSIC_BROWSE;
				global_intent.arg = MUSIC_ACCOUNT_PREFIX + browse_id;
			}
		}

		if (long_held_shelf != -1) {
			int s = long_held_shelf, i = long_held_item;
			long_held_shelf = long_held_item = clicked_shelf = clicked_item = -1;
			main_view->reset_holding_status(); // the long hold must not turn into a click
			if (s < (int)home_info.shelves.size() && i < (int)home_info.shelves[s].items.size()) {
				bool was_empty = !MusicQueue::has_current();
				MusicQueue::add_next(home_info.shelves[s].items[i].track);
				if (was_empty) {
					music_player_play_queue();
				}
				music_player_show_notice(LOCALIZED(ADDED_TO_QUEUE));
			}
		}
		if (clicked_shelf != -1) {
			int s = clicked_shelf, i = clicked_item;
			clicked_shelf = clicked_item = -1;
			if (s < (int)home_info.shelves.size() && i < (int)home_info.shelves[s].items.size()) {
				const auto &shelf = home_info.shelves[s];
				const auto &item = shelf.items[i];
				if (item.type == MusicItem::Type::SONG) {
					// queue the shelf's songs from the tapped one onwards
					std::vector<MusicTrack> tracks;
					for (size_t j = i; j < shelf.items.size(); j++) {
						if (shelf.items[j].type == MusicItem::Type::SONG) {
							tracks.push_back(shelf.items[j].track);
						}
					}
					MusicQueue::set(tracks, 0);
					music_player_play_queue();
					global_intent.next_scene = SceneType::VIDEO_PLAYER;
					global_intent.arg = "";
				} else {
					global_intent.next_scene = SceneType::MUSIC_BROWSE;
					global_intent.arg = item.browse_id;
				}
			}
		}
		if (video_playing_bar_show) {
			video_update_playing_bar(key);
		}

		if (key.p_a) {
			Search_show_search_keyboard();
		}
		if (key.p_b) {
			global_intent.next_scene = SceneType::BACK;
		}
	}
	resource_lock.unlock();
}
