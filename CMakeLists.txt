cmake_minimum_required(VERSION 3.22...3.30)

# This plugin CMakeLists is intended to be built *inside* the OBS Studio source tree
# (i.e. added under obs-studio/plugins/ and referenced from obs-studio/plugins/CMakeLists.txt).
#
# It relies on OBS Studio's CMake helpers/targets such as:
#   - OBS::libobs
#   - OBS::frontend-api
#   - Qt6::Widgets
#   - set_target_properties_obs()
#   - legacy_check() (optional, safe if present)
#
# If you see errors about those, you are likely trying to build this standalone.

project(rpg-map-effects LANGUAGES C CXX)

if (COMMAND legacy_check)
  legacy_check()
endif()

add_library(rpg-map-effects MODULE)
add_library(OBS::rpg-map-effects ALIAS rpg-map-effects)

target_sources(rpg-map-effects PRIVATE
  src/plugin-main.cpp
  src/rpg_window.cpp
  src/obs_display_widget.mm
)

target_include_directories(rpg-map-effects PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(rpg-map-effects PRIVATE cxx_std_17)

target_link_libraries(rpg-map-effects PRIVATE
  OBS::libobs
  OBS::frontend-api
  Qt6::Widgets
)

# Match built-in OBS plugin conventions (no 'lib' prefix on macOS)
if (COMMAND set_target_properties_obs)
  set_target_properties_obs(rpg-map-effects PROPERTIES
    FOLDER plugins
    PREFIX ""
  )
else()
  set_target_properties(rpg-map-effects PROPERTIES PREFIX "")
endif()
set_target_properties(rpg-map-effects PROPERTIES AUTOMOC ON)
