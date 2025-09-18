cmake_minimum_required(VERSION 3.15)

include(CMakeFindDependencyMacro)

# Compute the installation prefix relative to this file.
get_filename_component(_IMPORT_PREFIX "${CMAKE_CURRENT_LIST_FILE}" PATH)
get_filename_component(_IMPORT_PREFIX "${_IMPORT_PREFIX}" PATH)
get_filename_component(_IMPORT_PREFIX "${_IMPORT_PREFIX}" PATH)
if(_IMPORT_PREFIX STREQUAL "/")
  set(_IMPORT_PREFIX "")
endif()

add_library(lexbor STATIC IMPORTED)

set(lexbor_INCLUDE_DIR "${_IMPORT_PREFIX}/include")
set(lexbor_LIBRARY "${_IMPORT_PREFIX}/lib")

#message(lexbor_INCLUDE_DIR="${lexbor_INCLUDE_DIR}")

include("${CMAKE_CURRENT_LIST_DIR}/lexborConfigVersion.cmake")
