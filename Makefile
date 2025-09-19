PROJ_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

$(shell cp ${PROJ_DIR}cmake/* ${PROJ_DIR}build/release/vcpkg_installed/x64-linux/share/lexbor/)
$(shell cp ${PROJ_DIR}cmake/* ${PROJ_DIR}build/debug/vcpkg_installed/x64-linux/share/lexbor/)

# Configuration of extension
EXT_NAME=webquery
EXT_CONFIG=${PROJ_DIR}extension_config.cmake

# Include the Makefile from extension-ci-tools
include extension-ci-tools/makefiles/duckdb_extension.Makefile