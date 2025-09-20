PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# VCPKG_TOOLCHAIN_PATH: /home/runner/work/webquery/webquery/vcpkg/scripts/buildsystems/vcpkg.cmake
# VCPKG_TARGET_TRIPLET: x64-linux-release
# VCPKG_HOST_TRIPLET: x64-linux-release
# cp: can't create '/duckdb_build_dir/build/release/vcpkg_installed/x64-linux/share/lexbor/lexborConfig.cmake': No such file or directory
# Successfully downloaded lexbor-lexbor-v2.4.0.tar.gz
# -- Extracting source /vcpkg/downloads/lexbor-lexbor-v2.4.0.tar.gz
# -- Using source at /vcpkg/buildtrees/lexbor/src/v2.4.0-54ec613b2b.clean
# -- Configuring x64-linux-release
# CMake Warning at /duckdb_build_dir/build/release/vcpkg_installed/x64-linux/share/vcpkg-cmake/vcpkg_cmake_configure.cmake:346 (message):
# /vcpkg/packages/lexbor_x64-linux-release/share/lexbor/copyright

$(shell cp ${PROJ_DIR}cmake/* /vcpkg/packages/lexbor_${VCPKG_TARGET_TRIPLET}/share/lexbor/)

$(shell cp ${PROJ_DIR}cmake/* ${PROJ_DIR}build/release/vcpkg_installed/x64-linux/share/lexbor/)
$(shell cp ${PROJ_DIR}cmake/* ${PROJ_DIR}build/debug/vcpkg_installed/x64-linux/share/lexbor/)

# Configuration of extension
EXT_NAME=webquery
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile