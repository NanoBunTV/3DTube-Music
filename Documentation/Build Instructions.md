You need:

 - devkitPro with devkitARM r68 or higher (older versions are untested)
 - ```3ds-zlib``` and ```3ds-mbedtls``` installed in portlibs of devkitPro
   You can install them by opening devkitPro msys2 and typing ```pacman -S [package name]```.

Simply run ```make``` in a terminal in the root directory to build it.
This produces ```3DTubeMusic.3dsx``` and ```3DTubeMusic_dev.cia```.

 - Windows: GNU make can't build from a path containing spaces. Run ```.\build.ps1``` from PowerShell instead;
   it temporarily maps the project folder to a free drive letter and runs make there. Arguments are passed to make
   (e.g. ```.\build.ps1 clean```).

 - Building of dependency libraries (optional)
   For ffmpeg, libbrotli, and libcurl, follow built.txt in each directory
   For libctru, just type ```make``` in library\libctru\source\libctru
