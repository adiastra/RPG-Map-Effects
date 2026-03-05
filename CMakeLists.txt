# Standalone build using OBS plugin template.
# See https://github.com/obsproject/obs-plugintemplate
cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version} LANGUAGES C CXX OBJC OBJCXX)

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<COMPILE_LANG_AND_ID:CXX,AppleClang,Clang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

# Embed cursor PNGs into C++ at build time
find_package(Python3 REQUIRED)
set(CURSOR_ASSETS_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/cursor_assets_generated.cpp")
file(GLOB CURSOR_PNGS "${CMAKE_CURRENT_SOURCE_DIR}/cursors/*.png")
add_custom_command(
  OUTPUT "${CURSOR_ASSETS_GENERATED}"
  COMMAND ${CMAKE_COMMAND} -E env PYTHONIOENCODING=utf-8
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_cursors.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/cursors"
    "${CURSOR_ASSETS_GENERATED}"
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_cursors.py" ${CURSOR_PNGS}
  COMMENT "Embedding cursor PNGs into C++"
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
  src/plugin-main.cpp
  src/rpg_window.cpp
  src/obs_display_widget.mm
  "${CURSOR_ASSETS_GENERATED}"
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# Allow plugin to load when placed inside OBS.app (PlugIns folder): use OBS's Frameworks.
# Runpath order: own bundle's Frameworks, then OBS.app/Contents/Frameworks (when inside PlugIns).
if(APPLE)
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES
      XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS
      "@executable_path/../Frameworks @executable_path/../../../../Frameworks"
  )
endif()
