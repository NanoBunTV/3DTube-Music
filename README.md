# 3DTube Music

A work-in-progress homebrew YouTube Music client for the Nintendo 3DS.

3DTube Music is based on [FourthTube](https://github.com/erievs/FourthTube), which continued
[ThirdTube](https://github.com/windows-server-2003/ThirdTube) after it stopped working. It plays music
natively, without running any JavaScript or rendering web pages.

## Status

Alpha. It hasn't been tested on hardware yet, so expect crashes and bugs. If you find one, please open an issue.

## Features

 - Audio-only playback
 - No ads. Streams are fetched directly, so ads are never requested
 - [SponsorBlock](https://sponsor.ajay.app/) auto-skip for non-music sections, sponsors, self-promotion and
   interaction reminders (can be turned off in Settings > Playback)
 - YouTube Music search with Songs / Albums / Artists / Playlists filters
 - Album, playlist and artist pages
 - Now-playing screen with album art, play queue, shuffle and repeat (off / all / one)
 - Radio: keeps playing similar songs when the queue ends (can be turned off in Settings > Playback)
 - Optional Google account sign-in
 - Local history and subscriptions

### Planned

 - YouTube Music home feed
 - YouTube Music library when signed in (liked songs, saved albums, playlists)
 - Lyrics

## Controls

 - B button : go back to the previous screen
 - C-pad up/down : scroll
 - L/R : switch between tabs (search filters on the search screen)
 - Select + Start : blackout the bottom screen
 - Tap a song : play it (the rest of the list is queued after it)
 - Hold a song : play it next
 - In the now-playing screen
    - A : play/pause
    - ZL/ZR : previous/next song
    - Arrow left/right : 10 s seek
    - Y : toggle shuffle
    - X : cycle repeat (off / all / one)
    - Hold a song in the Queue tab : remove it from the queue

Below are for debug purposes

 - Select + X : toggle debug log
 - Select + Y : toggle memory usage monitor
 - Select + R + A : toggle FPS monitor

## Requirements

A 3DS (including 2DS) with [Luma3DS](https://github.com/LumaTeam/Luma3DS) installed and with the DSP firmware
dumped. On modern Luma3DS versions, you can
[do this using a built-in feature](https://3ds.hacks.guide/finalizing-setup.html#section-iii-rtc-and-dsp-setup:~:text=dump%20the%20sound%20firmware).
Otherwise, you can use [DSP1](https://github.com/zoogie/DSP1).
The minimum system version hasn't been tested, but at least 8.1.0-0 is needed.

App data is stored in `/3ds/3DTubeMusic/`.

## A note on signing in

Signing in is optional and gives access to your own account's content. This app is an unofficial client that
identifies itself to YouTube as other official apps, which is against YouTube's Terms of Service. Accounts used
this way can be flagged, so consider using a secondary Google account. Never share the `oauth_tokens` file.

## Building

[Click here for instructions](/Docs/BUILDING.md)

## Credits

3DTube Music would not exist without the work of the ThirdTube and FourthTube communities.

### FourthTube

  [NCP 3.0 / erievs](https://github.com/erievs) - Maintainer of FourthTube; fixed the app after it stopped working in 2024.

  [Smu1zel](https://github.com/Smu1zel) - Figured out the line of code that needed to be changed and tested the change out, fixing the app after it stopped working in 2024.

  [5GBurrito](https://github.com/5GBurrito) - Minor changes.

  [2b-zipper](https://github.com/2b-zipper) - Banner, watch history fix, icon, 480p support, and many other fixes.

  [ItsFrocat](https://github.com/ItsFrocat) & [Dragontwo14](https://github.com/Dragontwo14) - German translation strings.

  [cooolgamer](https://github.com/cooolgamer) - French translation strings and a custom boot screen.

  [Dxni](https://github.com/Icee666) - Italian translation strings.

  Misino - Spanish translation strings.

  [returndislike](https://returnyoutubedislike.com/install) - Used for dislikes.

### ThirdTube

  [WindowsServer2003](https://github.com/windows-server-2003) - The creator of ThirdTube.

* Core 2 Extreme
  For [Video player for 3DS](https://github.com/Core-2-Extreme/Video_player_for_3DS), which the playback code is based on.
  The playback functionality is essential to this app, including his optimization work (sometimes in assembly) and research into hardware decoding on the New 3DS.
* dixy52-beep
  For in-app textures
* [PokéTube](https://github.com/Poketubepoggu)
  For the icon and the banner
* The contributors of [youtube-dl](https://github.com/ytdl-org/youtube-dl)
  As a reference for YouTube webpage parsing, especially the deobfuscation of ciphered signatures.
* The contributors of [pytube](https://github.com/pytube/pytube)
  As a reference for YouTube webpage parsing.

### Other

 - [SponsorBlock](https://sponsor.ajay.app/) by Ajay Ramachandran and contributors - skip segment data.
 - [Pear Desktop](https://github.com/pear-devs/pear-desktop) - inspiration for this project.

## License

You can use the code under the terms of the GNU General Public License GPL v3 or under the terms of any later
revisions of the GPL. Refer to the provided LICENSE file for further information.

## Third-party licenses

### [FFmpeg](https://ffmpeg.org/)
by the FFmpeg developers under GNU Lesser General Public License (LGPL) version 2.1
The modified source code can be found at https://github.com/windows-server-2003/FFmpeg/tree/3ds.
### [rapidjson](https://github.com/Tencent/rapidjson)
by Tencent and Milo Yip under MIT License
### [libctru](https://github.com/devkitPro/libctru)
by devkitPro under zlib License
### [libcurl](https://curl.se/)
by Daniel Stenberg and many contributors under the curl License
### [libbrotli](https://github.com/google/brotli)
by the Brotli Authors under MIT license
### [stb](https://github.com/nothings/stb/)
by Sean Barrett under MIT License and Public Domain

3DTube Music is not affiliated with, endorsed by, or associated with Google or YouTube.
