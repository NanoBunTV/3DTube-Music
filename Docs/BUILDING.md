# Building

## Requirements

 - devkitPro with devkitARM r68 or higher (older versions are untested)
 - ```3ds-zlib``` and ```3ds-mbedtls``` installed in portlibs of devkitPro
   You can install them by opening devkitPro msys2 and typing ```pacman -S [package name]```.

The other libraries (FFmpeg, libcurl, nghttp2, libbrotli, a patched libctru) are prebuilt in `library/`.

## Build

Run ```make``` in a terminal in the root directory.
This produces ```3DTubeMusic.3dsx``` and ```3DTubeMusic_dev.cia```.

 - Windows: GNU make can't build from a path containing spaces, and MSYS resolves junctions back to the real
   path. Run ```.\build.ps1``` from PowerShell instead; it temporarily maps the project folder to a free drive
   letter with `subst`, runs make there, and removes the mapping. Arguments are passed to make
   (e.g. ```.\build.ps1 clean```). It finds devkitPro's `bash.exe` next to `make.exe` on PATH
   (the owner's install : `F:\Programs\devkitPro`).

 - Building of dependency libraries (optional)
   For ffmpeg, libbrotli, and libcurl, follow built.txt in each directory
   For libctru, just type ```make``` in library\libctru\source\libctru

## Testing

 - Hardware : copy `3DTubeMusic.3dsx` to `sd:/3ds/` and start it from the Homebrew Launcher (or install the
   `.cia`). Needs Luma3DS and dumped DSP firmware. Logs are written under `/3ds/3DTubeMusic/`.
 - Emulator : not used. Azahar only showed white screens for this app, even with real system files.
 - YouTube-side checks from a PC : `python Docs/tools/probe_player_clients.py <videoId>` (see YOUTUBE_API.md).
 - Interface strings : `python Docs/tools/check_strings.py` lists missing / unused string ids per language.

## Releasing

Releases are published on https://github.com/NanoBunTV/3DTube-Music/releases. **The repository must be public** :
the in-app updater and downloads use GitHub's API without signing in. The owner does all commits, tags and pushes.

1. Bump the version in three places (they must agree) :
   - `DEF_CURRENT_APP_VER` in `source/definitions.hpp`, e.g. `"v1.1.0"`. The updater compares the numbers in it
     with the release tag; an update is offered when the tag is higher.
   - `APP_VER` in the `Makefile` : major * 1024 + minor * 16 + patch (v1.1.0 = 1040). The CIA's title version.
   - A new `## v1.1.0` section at the top of `CHANGELOG.md` (it becomes the release notes).
2. Build on the owner's PC : `.\build.ps1 release`. It produces `3DTubeMusic.3dsx` and `3DTubeMusic.cia` (the
   normal build names the CIA `3DTubeMusic_dev.cia`; the contents are the same). Test it on hardware.
3. Commit and push, then on GitHub create a release with tag `v1.1.0`, title "3DTube Music v1.1.0", the
   CHANGELOG section as the description, and `3DTubeMusic.3dsx` + `3DTubeMusic.cia` attached.

There are no GitHub Actions workflows : builds are always made on the owner's PC (owner's decision, 2026-10-13;
FourthTube's CI and CodeQL workflows and a tag-triggered release workflow were removed).

Don't mark releases as pre-releases : the updater uses `releases/latest`, which skips them. The updater picks
the first `.3dsx` and `.cia` assets of the latest release, so attach exactly one of each.

## Icon, banner and banner sound

`resource/icon.png` (48x48) and `resource/banner.png` (256x128, HOME menu banner) are original artwork generated
by a script, so they can be tweaked and regenerated :

 - `powershell -ExecutionPolicy Bypass -File resource\art\make_art.ps1` : icon, banner, and
   `resource/art/icon_large.png` (shown in the README). Windows only (System.Drawing).

`resource/banner.wav` (played when the app is selected) is a sound file supplied by the owner (replacing a
generated chime) : 16-bit PCM, 32 kHz stereo, 2.987 s. bannertool requires one and reads WAV or Vorbis OGG only
(not Opus); it must be under 3 seconds. To convert another file :
`ffmpeg -i <file> -t 2.9 -af "afade=t=out:st=2.7:d=0.2" -ar 32000 -c:a pcm_s16le resource/banner.wav`.

The HOME Menu can keep showing / playing a title's old banner after the CIA is reinstalled (banner cache).

The Makefile passes them to `bannertool` (`BANNER_IMAGE`, `BANNER_AUDIO`, `ICON`).
