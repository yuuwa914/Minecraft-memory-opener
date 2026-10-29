cmake_minimum_required(VERSION 3.22)
project(MemoryGuard LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
    preloader_android
    GIT_REPOSITORY https://github.com/LiteLDev/preloader-android.git
    GIT_TAG 0.2.2)
FetchContent_MakeAvailable(preloader_android)

find_library(LOG_LIB log)

add_library(MemoryGuard SHARED src/MemoryGuard.cpp)
target_compile_options(MemoryGuard PRIVATE
    -O2 -fvisibility=hidden -fvisibility-inlines-hidden
    -ffunction-sections -fdata-sections)
target_link_options(MemoryGuard PRIVATE -Wl,--gc-sections)
target_link_libraries(MemoryGuard PRIVATE preloader ${LOG_LIB})
