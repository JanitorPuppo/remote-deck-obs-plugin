cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)
option(ENABLE_REMOTE_DECK_LOCAL_DEV "Show a local Remote Deck URL override (never enable for public releases)" OFF)

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
  find_package(Qt6 REQUIRED COMPONENTS Widgets Core Network)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/device-auth.cpp
    src/device-auth.hpp
    src/network-log.hpp
    src/obs-audio.cpp
    src/obs-audio.hpp
    src/plugin-controller.cpp
    src/plugin-controller.hpp
    src/plugin-main.cpp
    src/protocol.cpp
    src/protocol.hpp
    src/qt-tls-setup.cpp
    src/qt-tls-setup.hpp
    src/settings.cpp
    src/settings.hpp
    src/settings-dialog.cpp
    src/settings-dialog.hpp
    src/upstream-client.cpp
    src/upstream-client.hpp
    src/ws-transport.cpp
    src/ws-transport.hpp
)

if(ENABLE_REMOTE_DECK_LOCAL_DEV)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE REMOTE_DECK_LOCAL_DEV=1)
  message(WARNING "ENABLE_REMOTE_DECK_LOCAL_DEV is ON — this build is not a public release")
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
