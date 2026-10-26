###################################################################################################
# OrbisAFR for PS5 -- built as an etaHEN plugin (see ../README.md and ./README.md)
# Structure borrowed from etaHEN-Plugins' Plugin_samples/Game_Plugin_Loader/CMakeLists.txt
###################################################################################################

cmake_minimum_required (VERSION 3.20)

set(basename "OrbisAFR_PS5")
project(${basename} C CXX)

# Shown in the etaHEN toolbox's plugin list. This plugin isn't tied to one
# game -- it watches whatever title is in the foreground -- so the id is
# just a label, not a filter.
set(PLUGIN_TITLE_ID "AFRP00001")
set(PLUGIN_VERSION "0.1.0")

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)

if (NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	set(IS_SUBPROJECT TRUE)
else()
	set(IS_SUBPROJECT FALSE)
endif()

# Path to a checkout of https://github.com/etaHEN/etaHEN-Plugins (the Plugin
# SDK). Override with: cmake -DETAHEN_PLUGINS_SDK=/path/to/etaHEN-Plugins
if (NOT DEFINED ETAHEN_PLUGINS_SDK)
	set(ETAHEN_PLUGINS_SDK "$ENV{ETAHEN_PLUGINS_SDK}")
endif()
if (NOT ETAHEN_PLUGINS_SDK)
	message(FATAL_ERROR "Set ETAHEN_PLUGINS_SDK to a checkout of https://github.com/etaHEN/etaHEN-Plugins "
	                     "(e.g. -DETAHEN_PLUGINS_SDK=/path/to/etaHEN-Plugins)")
endif()

set(D_CWD "${CMAKE_CURRENT_SOURCE_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${D_CWD}/bin)

add_executable(${PROJECT_NAME})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}.elf")

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "[Cc]lang")
	set(IS_CLANG 1)
else()
	message(FATAL_ERROR "${PROJECT_NAME} is meant to be built with clang! CompilerID: ${CMAKE_CXX_COMPILER_ID}")
endif()

set(D_SRC ${D_CWD}/source)
file(GLOB SrcFiles ${D_SRC}/*.c ${D_SRC}/*.cpp ${D_SRC}/*.h ${D_SRC}/*.hpp)

set(CMAKE_C_FLAGS "--target=x86_64-sie-ps5 -DPS5")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-keyword-macro -fPIC -fPIE -march=znver2 -Wall -Wextra -fstack-protector-all")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -gfull -gdwarf-2 -O1")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O1")

add_custom_command(TARGET ${basename}
	POST_BUILD
	COMMAND python3 "${ETAHEN_PLUGINS_SDK}/lib/make_plugin.py" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${basename}.elf" "${PLUGIN_TITLE_ID}" "${PLUGIN_VERSION}"
	COMMAND ${CMAKE_COMMAND} -E remove "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${basename}.elf"
	COMMENT "Packaging ${basename} as an etaHEN plugin"
	WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

target_sources(${PROJECT_NAME} PRIVATE ${SrcFiles})
target_include_directories(${PROJECT_NAME} PRIVATE "${ETAHEN_PLUGINS_SDK}/include")
target_link_directories(${PROJECT_NAME} PUBLIC "${ETAHEN_PLUGINS_SDK}/lib")
target_link_libraries(${PROJECT_NAME} PUBLIC hijacker SceSystemService SceUserService SceSysCore)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
