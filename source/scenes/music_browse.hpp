#pragma once
#include "ui/ui.hpp"

// Album / playlist / artist page (SceneType::MUSIC_BROWSE). The scene arg is a YouTube Music browse id.

void MusicBrowse_init(void);
void MusicBrowse_exit(void);
void MusicBrowse_suspend(void);
void MusicBrowse_resume(std::string arg);
void MusicBrowse_draw(void);
