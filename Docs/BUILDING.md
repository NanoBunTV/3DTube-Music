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
 - Emulator : Azahar can load the `.3dsx` directly (`azahar.exe path\to\3DTubeMusic.3dsx`). It needs system
   files dumped from a real 3DS (shared fonts etc.) to run this app properly.
 - YouTube-side checks from a PC : `python Docs/tools/probe_player_clients.py <videoId>` (see YOUTUBE_API.md).
