cmake_minimum_required(VERSION 3.22)
project(FukarosFurina VERSION 0.4.19 LANGUAGES C CXX)

# The desktop workspace builds both applications and their regression tests.
# For an Android APK, configure Furina/ with the Android Qt toolchain instead.
include(CTest)
add_subdirectory(Fukaros)
if(NOT ANDROID)
    add_subdirectory(Furina)
endif()
