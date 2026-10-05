# SPDX-License-Identifier: Apache-2.0
# EVRe Studio: a register tool for EVRe devices (Qt 6, C++17).
#
#   EVReStudio       the program
#   evre_gui_test    the real window driven by QtTest against tests/fake_device.py
#   evre_map_test    the map files: load, save (only what changed), overlays, checks
#   evre             the command-line tool: validate, export, info, read, dump, watch, write
#   evre-sim         a device made from a map, over TCP
#   evre_probe       the protocol core without the window, for checking a device (reads only;
#                    the one write is the login token when EVRE_TOKEN is set)
#   evre_fake_fast   a fake EVRe device fast enough to measure the Studio
cmake_minimum_required(VERSION 3.21)
project(EVReStudio VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network SerialPort Test)

# Warnings on for all our code (GCC and Clang; MSVC keeps its defaults).
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
endif()

# The EVRe protocol and the map file, without any window: the Studio, the
# probe and the fast fake device are all built on it, and compile it once.
qt_add_library(evre_protocol STATIC
    src/evre/frame.h            src/evre/frame.cpp
    src/evre/link.h             src/evre/link.cpp
    src/evre/master.h           src/evre/master.cpp
    src/evre/registers.h
    src/model/device_map.h      src/model/device_map.cpp
    src/model/map_file.cpp      src/model/map_check.cpp
    src/model/json_doc.h        src/model/json_doc.cpp
    src/model/map_export.h      src/model/map_export.cpp
    src/model/bus_file.h        src/model/bus_file.cpp
)
target_include_directories(evre_protocol PUBLIC src)
target_link_libraries(evre_protocol PUBLIC Qt6::Core Qt6::Network Qt6::SerialPort)

# The rest of the Studio without main(): the program and the GUI test are both
# built on it, so a new source file is one more line here (or in
# evre_protocol above) and nowhere else.
qt_add_library(evre_studio_core STATIC
    src/model/expr.h            src/model/expr.cpp
    src/model/math_lines.h      src/model/math_lines.cpp
    src/model/recording_file.h  src/model/recording_file.cpp
    src/model/register_model.h  src/model/register_model.cpp
    src/model/map_document.h    src/model/map_document.cpp
    src/ui/bus_panel.h          src/ui/bus_panel.cpp
    src/ui/bus_device_dialog.h  src/ui/bus_device_dialog.cpp
    src/ui/bus_preset_dialog.h  src/ui/bus_preset_dialog.cpp
    src/io/reg_table.h
    src/io/engine.h             src/io/engine.cpp
    src/api/api_server.h        src/api/api_server.cpp
    src/ui/theme.h              src/ui/theme.cpp
    src/ui/ui_helpers.h         src/ui/ui_helpers.cpp
    src/ui/limit_spin_box.h     src/ui/limit_spin_box.cpp
    src/ui/elided_label.h       src/ui/elided_label.cpp
    src/ui/value_pace.h         src/ui/value_pace.cpp
    src/ui/value_delegate.h     src/ui/value_delegate.cpp
    src/ui/event_log.h          src/ui/event_log.cpp
    src/ui/bit_view.h           src/ui/bit_view.cpp
    src/ui/quick_write_panel.h  src/ui/quick_write_panel.cpp
    src/ui/chart_widget.h       src/ui/chart_widget.cpp
    src/ui/gpu_lines.h          src/ui/gpu_lines.cpp
    src/ui/math_line_dialog.h   src/ui/math_line_dialog.cpp
    src/ui/formula_completer.h  src/ui/formula_completer.cpp
    src/ui/chart_tab.h          src/ui/chart_tab.cpp
    src/ui/recording_window.h   src/ui/recording_window.cpp
    src/ui/monitor_tab.h        src/ui/monitor_tab.cpp
    src/ui/map_table_model.h    src/ui/map_table_model.cpp
    src/ui/name_table.h         src/ui/name_table.cpp
    src/ui/field_editor.h       src/ui/field_editor.cpp
    src/ui/register_editor.h    src/ui/register_editor.cpp
    src/ui/map_settings_dialog.h src/ui/map_settings_dialog.cpp
    src/ui/map_editor_tab.h     src/ui/map_editor_tab.cpp
    src/ui/registers_tab.h      src/ui/registers_tab.cpp
    src/ui/sidebar.h            src/ui/sidebar.cpp
    src/ui/help_dialog.h        src/ui/help_dialog.cpp
    src/ui/frame_clock.h        src/ui/frame_clock.cpp
    src/ui/main_window.h        src/ui/main_window.cpp
    src/ui/main_window_bus.cpp
)
target_include_directories(evre_studio_core PUBLIC src)
target_compile_definitions(evre_studio_core PUBLIC EVRE_STUDIO_VERSION="${PROJECT_VERSION}")
target_link_libraries(evre_studio_core PUBLIC evre_protocol Qt6::Widgets)
if(WIN32)
    # timeBeginPeriod: 1 ms timers; DwmFlush: frames at the display refresh; Direct3D 11 and DirectComposition:
    # the chart's plot on a GPU, a layer of the window
    target_link_libraries(evre_studio_core PRIVATE winmm dwmapi d3d11 dxgi d3dcompiler dcomp)
endif()

qt_add_executable(EVReStudio WIN32 src/main.cpp)
target_link_libraries(EVReStudio PRIVATE evre_studio_core)

# The maps travel with the programs: maps/ is copied beside them at every
# build, so an edited map is there even when no program had to be linked
# again. Every program that opens maps/ beside itself depends on it.
add_custom_target(evre_maps ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/maps $<TARGET_FILE_DIR:EVReStudio>/maps)
add_dependencies(EVReStudio evre_maps)

# The real window driven by QtTest against tests/fake_device.py (it writes: the
# fake device only). It opens the maps copied beside it.
qt_add_executable(evre_gui_test tests/gui_test.cpp)
target_link_libraries(evre_gui_test PRIVATE evre_studio_core Qt6::Test)
add_dependencies(evre_gui_test evre_maps)

# evre: the EVRe tools on the command line (validate, export, info, read, dump,
# watch, write), on the protocol core without a window.
qt_add_executable(evre cli/evre.cpp)
target_link_libraries(evre PRIVATE evre_protocol)

# evre-sim: an EVRe device made from a map, over TCP (defaults, write-only,
# action, write-1-to-clear, login, persist: as the map says).
qt_add_executable(evre-sim cli/evre_sim.cpp)
target_link_libraries(evre-sim PRIVATE evre_protocol)

# The map files without a window or a device: every map in maps/ (and the
# files named on its command line) must save back byte for byte.
qt_add_executable(evre_map_test tests/map_test.cpp)
target_link_libraries(evre_map_test PRIVATE evre_protocol Qt6::Test)
add_dependencies(evre_map_test evre_maps)

# The protocol core without the window, for checking it against a real device (reads only;
# the one write is the login token when EVRE_TOKEN is set).
qt_add_executable(evre_probe tests/evre_probe.cpp)
target_link_libraries(evre_probe PRIVATE evre_protocol)

# A fake EVRe device fast enough to measure the Studio at thousands of polls a
# second. Without a map argument it opens the example map copied beside it.
qt_add_executable(evre_fake_fast tests/fake_device_fast.cpp)
target_link_libraries(evre_fake_fast PRIVATE evre_protocol)
add_dependencies(evre_fake_fast evre_maps)
