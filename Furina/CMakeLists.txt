cmake_minimum_required(VERSION 3.22)
project(Furina VERSION 0.4.20 LANGUAGES C CXX)
include(../cmake/ProjectDefaults.cmake)
set(CMAKE_AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/ui/forms")

add_library(furina_android_bridge STATIC core/platform/androidbridge.cpp core/platform/androidbridge.h)
target_include_directories(furina_android_bridge PUBLIC core/platform)
target_link_libraries(furina_android_bridge PUBLIC furina_protocol Qt6::Gui)

add_library(furina_app STATIC
    ui/mainwindow.cpp ui/mainwindow.h
    ui/mainwindow_settings.cpp ui/mainwindow_session.cpp ui/mainwindow_video.cpp
    ui/mobile_layout.cpp ui/mobile_layout.h
    ui/forms/mainwindow.ui ui/forms/settings.ui
)
# Make changed forms an explicit input: a first incremental build must not
# leave compiled window code using an older AUTOUIC header.
set_source_files_properties(
    ui/mainwindow.cpp ui/mainwindow_settings.cpp ui/mainwindow_session.cpp ui/mainwindow_video.cpp
    PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ui/forms/settings.ui;${CMAKE_CURRENT_SOURCE_DIR}/ui/forms/mainwindow.ui"
)

target_include_directories(furina_app PUBLIC ui)
target_link_libraries(furina_app PUBLIC furina_android_bridge furina_session furina_settings furina_video)
qt_add_executable(Furina main.cpp resources.qrc)
target_link_libraries(Furina PRIVATE furina_app)
if(ANDROID)
    include(cmake/AndroidPackage.cmake)
elseif(MINGW)
    target_link_options(Furina PRIVATE -mwindows)
endif()
if(NOT ANDROID AND BUILD_TESTING)
    include(cmake/Tests.cmake)
endif()
