cmake_minimum_required(VERSION 3.15)

# Set extension name here
set(TARGET_NAME webquery)

#set(lexbor_DIR cmake)
set(lexbor_DIR /vcpkg/packages/lexbor_${VCPKG_TARGET_TRIPLET}/share/lexbor/)

# DuckDB's extension distribution supports vcpkg. As such, dependencies can be added in ./vcpkg.json and then
# used in cmake with find_package. Feel free to remove or replace with other dependencies.
# Note that it should also be removed from vcpkg.json to prevent needlessly installing it..
find_package(lexbor REQUIRED)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})
include_directories(src/include ${lexbor_INCLUDE_DIR})

set(EXTENSION_SOURCES src/webquery_extension.cpp)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

message(lexbor_LIBS="${lexbor_LIBS}")

# Link lexbor in both the static library as the loadable extension
target_link_libraries(${EXTENSION_NAME} ${lexbor_LIBRARY}/liblexbor_static.a)
target_link_libraries(${LOADABLE_EXTENSION_NAME} ${lexbor_LIBRARY}/liblexbor_static.a)

install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")