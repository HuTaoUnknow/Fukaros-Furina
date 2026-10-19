include_guard(GLOBAL)
function(furina_test target name)
    qt_add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE Qt6::Test)
    add_test(NAME ${name} COMMAND ${target})
endfunction()

function(furina_offscreen_test name)
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

# Hardware checks are built for deliberate execution, never registered in CTest.
function(furina_hardware_test target)
    qt_add_executable(${target} ${ARGN})
    target_include_directories(${target} PRIVATE ${FURINA_ROOT}/third_party ${FURINA_ROOT}/third_party/ffmpeg)
    target_link_libraries(${target} PRIVATE furina_video furina_widgets fukaros_display_native)
endfunction()
