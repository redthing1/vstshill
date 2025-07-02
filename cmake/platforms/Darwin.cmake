# macOS-specific configuration
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "minimum macOS version")

option(VSTSHILL_UNIVERSAL_BINARY "build universal binary for macOS" OFF)
if(VSTSHILL_UNIVERSAL_BINARY)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()