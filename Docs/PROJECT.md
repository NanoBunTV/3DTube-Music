# Project overview and decisions

## What it is

3DTube Music is a homebrew YouTube Music client for the Nintendo 3DS (C++, devkitPro / libctru / citro2d).
It plays audio only, has no ads (streams are fetched directly), and supports SponsorBlock, YouTube Music
search / albums / playlists / artists, a play queue with shuffle and repeat, and radio.

It started from the idea of porting [Pear Desktop](https://github.com/pear-devs/pear-desktop) (an Electron
YouTube Music wrapper) to the 3DS. That isn't possible: Electron/Chromium can't run on a 3DS. Instead the
project is based on [FourthTube](https://github.com/erievs/FourthTube) (GPLv3), a native 3DS YouTube client
that continued [ThirdTube](https://github.com/windows-server-2003/ThirdTube).

## History

- **2026-10-13** : set up as a local fork of FourthTube (Beta 34.1, commit `6d8bcb2`), first named "Pear3DS".
  Added SponsorBlock, made audio-only the default, and disabled the in-app updater.
- **2026-10-13** : renamed to **3DTube Music**. Git history was restarted as a single initial commit and the
  `upstream` remote removed, so it can be a standalone (non-fork) GitHub repository. FourthTube and ThirdTube
  credits are kept in the README and the About screen.
- **2026-10-13** : video removed entirely. Added the YouTube Music parser, play queue, now-playing screen,
  music search, album/playlist/artist pages, and the YouTube Music home feed. Switched the default player
  client to visionOS after the "song skips after one minute" bug (see YOUTUBE_API.md).

## Decisions and why

| Decision | Reason |
|---|---|
| Base on FourthTube instead of writing from scratch | It already solves stream URLs, networking (libcurl + mbedtls), AAC decoding (FFmpeg) and the UI toolkit |
| Standalone repo, no shared history with FourthTube | The owner wants a separate, non-fork GitHub repo. Credits must stay in README / About |
| No video at all | Owner's call : "people can use the original FourthTube if they want video". Also frees ~4 MB of linear memory |
| Sign-in is optional, never required | Using a Google account with a client that impersonates official apps can get the account flagged |
| No ads, by design | Streams are fetched directly; ads are never requested. No Premium needed |
| SponsorBlock on by default, using the k-anonymity endpoint | Only a 4-character hash prefix of the video id is sent |
| In-app updater disabled (`UPDATE_RELEASES_API_URL` is empty) | It overwrites the running .3dsx with whatever the releases URL serves. Point it at this project's own GitHub releases once they exist, never at FourthTube's |
| Separate title ID / product code / data folder | Installs alongside FourthTube : unique ID `0xBEA12`, `CTR-3TM`, `/3ds/3DTubeMusic/` |
| Must stay GPLv3 | Derived from GPLv3 code. The FFmpeg submodule points at the modified FFmpeg source (LGPL requirement) |

## Owner preferences (for AI assistants)

- **Never run `git commit` or `git push`**, and don't stage changes. The owner handles all commits and pushes.
- The owner tests on a **New 3DS** (no Old 3DS available).
- Azahar emulator : `E:\ROMs and Emulators\_Emulators\Azahar (3DS)\azahar.exe`. Its data is in
  `%APPDATA%\Azahar` (log : `%APPDATA%\Azahar\log\azahar_log.txt`). Don't drive the owner's desktop
  (screenshots, input) while they're using the PC.
- Pressing HOME stops audio. That's normal 3DS behaviour for applications; don't treat it as a bug.
