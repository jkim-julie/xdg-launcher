cmake_minimum_required(VERSION 3.0)
project (runxdg)

set(PROJECT_PRETTY_NAME "Run XDG Application")
set(PROJECT_MAJOR_VERSION "1.0")
set(PROJECT_MINOR_VERSION "1")
set(PROJECT_VERSION "${PROJECT_MAJOR_VERSION}.${PROJECT_MINOR_VERSION}")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(GIO REQUIRED gio-2.0)

# No configuration
# configure_file (
#   "${PROJECT_SOURCE_DIR}/config.h.in"
#   "${PROJECT_BINARY_DIR}/config.h"
# )
#
# include_directories("${PROJECT_BINARY_DIR}")

include_directories(
  "include"
  "${GLIB_INCLUDE_DIRS}"
  "${GIO_INCLUDE_DIRS}"
  )

SET(SRC_FILES
    src/runxdg.cpp
)

SET(LIBRARIES
  windowmanager
  homescreen
  ${ILMCONTROL_LIBRARIES}
  afbwsc
  json-c
  pthread
  ${GLIB_LIBRARIES}
  ${GIO_LIBRARIES}
  appbridge
  )

add_executable (runxdg ${SRC_FILES})

TARGET_LINK_LIBRARIES (runxdg ${LIBRARIES})

install (TARGETS runxdg DESTINATION bin)

add_custom_command(TARGET runxdg POST_BUILD
  COMMAND cp -rf ${CMAKE_CURRENT_SOURCE_DIR}/package ${PROJECT_BINARY_DIR})

add_custom_target(widget
  # simple egl
  COMMAND wgtpkg-pack -f -o ${PROJECT_BINARY_DIR}/package/simple-egl.wgt ${PROJECT_BINARY_DIR}/package/simple-egl
  # override hvac to test runxdg
  COMMAND wgtpkg-pack -f -o ${PROJECT_BINARY_DIR}/package/hvac.wgt ${PROJECT_BINARY_DIR}/package/hvac
  # override navigation to test runxdg
  COMMAND wgtpkg-pack -f -o ${PROJECT_BINARY_DIR}/package/navi.wgt ${PROJECT_BINARY_DIR}/package/navi

  # test for video
  COMMAND wgtpkg-pack -f -o ${PROJECT_BINARY_DIR}/package/video.wgt ${PROJECT_BINARY_DIR}/package/video

  # test for webbrowser
  COMMAND wgtpkg-pack -f -o ${PROJECT_BINARY_DIR}/package/webbrowser.wgt ${PROJECT_BINARY_DIR}/package/webbrowser

)
