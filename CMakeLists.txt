cmake_minimum_required(VERSION 3.25)

set(BB_FLOPPY_BASE_FOLDER "${CMAKE_CURRENT_SOURCE_DIR}")

# Basic compiler and cmake configurations
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_INCLUDE_DIRECTORIES_BEFORE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include("${BB_FLOPPY_BASE_FOLDER}/cmake/build_helpers.cmake")

# Setup project
loadversion(BB_FLOPPY_VERSION BB_FLOPPY_VERSION_PLAIN)
set_variable_in_parent(BB_FLOPPY_VERSION ${BB_FLOPPY_VERSION})

configure_cmake()
project(
  bb-floppy
  LANGUAGES C CXX
  VERSION ${BB_FLOPPY_VERSION_PLAIN}
  DESCRIPTION "bb-floppy"
  HOMEPAGE_URL "http://jonarne.no")
configureproject()

add_subdirectory(src/caps_parser)
