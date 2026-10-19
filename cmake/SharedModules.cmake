include_guard(GLOBAL)
get_filename_component(FURINA_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

add_library(furina_protocol STATIC
    ${FURINA_ROOT}/core/protocol/furina_protocol.c
    ${FURINA_ROOT}/core/protocol/video_policy.c
    ${FURINA_ROOT}/core/video/cursor_shape.c
)
set_target_properties(furina_protocol PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(furina_protocol PUBLIC
    ${FURINA_ROOT}/core/protocol ${FURINA_ROOT}/core/video
)

add_library(furina_diagnostics STATIC
    ${FURINA_ROOT}/core/diagnostics/debug_log.cpp
    ${FURINA_ROOT}/core/diagnostics/debug_text.cpp
)
target_include_directories(furina_diagnostics PUBLIC ${FURINA_ROOT}/core/diagnostics)
target_link_libraries(furina_diagnostics PUBLIC Qt6::Core)

add_library(furina_transport STATIC
    ${FURINA_ROOT}/core/transport/peer.cpp
    ${FURINA_ROOT}/core/transport/peer.h
    ${FURINA_ROOT}/core/transport/network_discovery.cpp
)
target_include_directories(furina_transport PUBLIC ${FURINA_ROOT}/core/transport)
target_link_libraries(furina_transport PUBLIC furina_protocol Qt6::Network Qt6::Gui)

add_library(furina_widgets STATIC
    ${FURINA_ROOT}/ui/widgets/screenview.cpp
    ${FURINA_ROOT}/ui/widgets/screenview.h
    ${FURINA_ROOT}/ui/widgets/settings_support.cpp
    ${FURINA_ROOT}/ui/widgets/settings_support.h
    ${FURINA_ROOT}/ui/support/interface_support.h
    ${FURINA_ROOT}/ui/support/selection_controls.cpp
    ${FURINA_ROOT}/ui/support/tool_icons.cpp
    ${FURINA_ROOT}/ui/support/theme.cpp
    ${FURINA_ROOT}/ui/support/combo_box.cpp
    ${FURINA_ROOT}/ui/support/presentation.cpp
)
target_include_directories(furina_widgets PUBLIC ${FURINA_ROOT}/ui/widgets ${FURINA_ROOT}/ui/support)
target_link_libraries(furina_widgets PUBLIC furina_protocol furina_diagnostics Qt6::Widgets)

add_library(furina_session STATIC
    ${FURINA_ROOT}/core/session/session_consent.cpp
    ${FURINA_ROOT}/core/session/session_consent.h
    ${FURINA_ROOT}/core/session/cast_ui_state.cpp
    ${FURINA_ROOT}/core/session/cast_ui_state.h
    ${FURINA_ROOT}/core/session/cast_control.cpp
    ${FURINA_ROOT}/core/session/cast_control.h
    ${FURINA_ROOT}/core/session/presentation_sync.cpp
    ${FURINA_ROOT}/core/session/presentation_sync.h
)
target_include_directories(furina_session PUBLIC ${FURINA_ROOT}/core/session)
target_link_libraries(furina_session PUBLIC furina_transport furina_widgets)

add_library(furina_settings STATIC
    ${FURINA_ROOT}/core/settings/pointer_settings.cpp
    ${FURINA_ROOT}/core/settings/pointer_settings.h
)
target_include_directories(furina_settings PUBLIC ${FURINA_ROOT}/core/settings)
target_link_libraries(furina_settings PUBLIC furina_transport Qt6::Widgets)

add_library(furina_video STATIC
    ${FURINA_ROOT}/core/video/desktop_video.cpp
    ${FURINA_ROOT}/core/video/desktop_video.h
    ${FURINA_ROOT}/core/video/windows/backend_types.h
    ${FURINA_ROOT}/core/video/windows/gpu_capture.h
    ${FURINA_ROOT}/core/video/windows/nvenc_encoder.h
    ${FURINA_ROOT}/core/video/windows/mf_encoder.h
    ${FURINA_ROOT}/core/video/windows/h264_decoder.h
)
target_include_directories(furina_video PUBLIC ${FURINA_ROOT}/core/video)
target_include_directories(furina_video PRIVATE ${FURINA_ROOT}/third_party ${FURINA_ROOT}/third_party/ffmpeg)
target_link_libraries(furina_video PUBLIC furina_protocol furina_diagnostics Qt6::Gui)
if(WIN32)
    target_link_libraries(furina_video PUBLIC d3d11 dxgi ole32 winmm d3dcompiler runtimeobject gdi32)
endif()
