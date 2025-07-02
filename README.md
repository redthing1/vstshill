
# vstshill

the vst inspector.

an ultra-lightweight cross-platform vst3 host for analyzing plugins.

## features

+ load plugins
+ gui host with sdl3
+ offline processing of video files

## build

## macos

make sure xcode cli tools are set up, then build:
```sh
cmake -G Ninja -B build-macos
cmake --build build-macos --parallel
```

## windows

use vcpkg to install sdl3:
```sh
.\vcpkg install sdl3:x64-windows
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
