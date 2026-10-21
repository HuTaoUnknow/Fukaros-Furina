cmake_minimum_required(VERSION 3.22)
project(Fukaros VERSION 0.4.20 LANGUAGES C CXX)
include(../cmake/ProjectDefaults.cmake)

set(CMAKE_AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/ui/forms")
add_library(fukaros_display_native STATIC core/display/virtual_display.c core/display/display_scale.c)
target_link_libraries(fukaros_display_native PUBLIC furina_protocol)
set_target_properties(fukaros_display_native PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(fukaros_display_native PUBLIC core/display)
if(WIN32)
    target_link_libraries(fukaros_display_native PUBLIC user32 setupapi)
endif()

add_library(fukaros_platform STATIC
    core/app/desktop_tray.cpp core/app/desktop_tray.h
    core/app/single_instance.cpp core/app/single_instance.h
    core/display/cast_source.c
    core/display/virtual_display_controller.cpp core/display/virtual_display_controller.h
    core/display/display_position.cpp core/display/display_position.h ui/forms/position_dialog.ui
    core/input/desktop_pointer.cpp core/input/desktop_pointer.h core/input/pointer_gain.c
    core/transport/adb.cpp core/transport/adb.h
    core/transport/usb_network.cpp core/transport/usb_network.h
)
target_include_directories(fukaros_platform PUBLIC core/app core/display core/input core/transport)
target_link_libraries(fukaros_platform PUBLIC fukaros_display_native furina_session furina_settings)
if(WIN32)
    target_link_libraries(fukaros_platform PUBLIC shell32 iphlpapi ws2_32)
endif()

add_library(fukaros_app STATIC
    ui/mainwindow.cpp ui/mainwindow.h
    ui/mainwindow_settings.cpp ui/mainwindow_session.cpp ui/mainwindow_video.cpp
    ui/forms/mainwindow.ui ui/forms/settings.ui
)
# Make changed forms an explicit input: a first incremental build must not
# leave compiled window code using an older AUTOUIC header.
set_source_files_properties(
    ui/mainwindow.cpp ui/mainwindow_settings.cpp ui/mainwindow_session.cpp ui/mainwindow_video.cpp
    PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ui/forms/settings.ui;${CMAKE_CURRENT_SOURCE_DIR}/ui/forms/mainwindow.ui"
)

target_include_directories(fukaros_app PUBLIC ui)
target_link_libraries(fukaros_app PUBLIC fukaros_platform furina_video)

qt_add_executable(Fukaros main.cpp resources.qrc)
target_link_libraries(Fukaros PRIVATE fukaros_app)
if(MINGW)
    target_link_options(Fukaros PRIVATE -mwindows)
endif()
if(WIN32)
    target_sources(Fukaros PRIVATE packaging/version.rc)
    target_include_directories(Fukaros PRIVATE packaging)
    set_source_files_properties(packaging/version.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/app.ico")
    add_executable(virtual_display_helper core/display/virtual_display_helper.c)
    target_link_libraries(virtual_display_helper PRIVATE fukaros_display_native newdev advapi32 wintrust crypt32)
    if(MINGW)
        target_link_options(virtual_display_helper PRIVATE -municode -static)
    endif()
    add_dependencies(Fukaros virtual_display_helper)
    add_custom_command(TARGET Fukaros POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/core/display/virtual-display/driver" "$<TARGET_FILE_DIR:Fukaros>/virtual-display")
endif()
if(BUILD_TESTING)
    include(cmake/Tests.cmake)
endif()
