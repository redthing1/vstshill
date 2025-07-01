# vstshill

the vst inspector.

the goal is to be an ultra-lightweight cross-platform vst host for analyzing plugins.
this will support cli, gui, and offline loading and inspection of plugins.

## build

## windows

use vcpkg to install sdl2:
```sh
.\vcpkg install sdl2:x64-windows
```

using a developer command prompt:
```sh
cmake -G Ninja -B build-windows -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build-windows --parallel
```

## acknowledgements

thanks to the projects:
+ [easyvst](https://github.com/iffyloop/EasyVst) - sdl2 wrapping vst3
+ [plugalyzer](https://github.com/CrushedPixel/Plugalyzer) - inspiration
