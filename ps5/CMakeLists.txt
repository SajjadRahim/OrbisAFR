###################################################################################################
# OrbisAFR for PS5 -- built as an OnionHEN plugin (see ../README.md and ./README.md)
#
# Packaging/descriptor: OnionHEN Plugin SDK's onion_add_plugin() helper
#   https://github.com/OnionBuddies/onionHEN-plugin-sdk
# Actual file-hook mechanism: libhijacker (vendored via a checkout of
# etaHEN-Plugins, which ships prebuilt libhijacker.a + headers; see README.md
# for why OnionHEN's own plugin SDK doesn't cover this part yet)
###################################################################################################

cmake_minimum_required (VERSION 3.20)
# C *and* CXX: onionHEN-plugin-sdk's own CMakeLists.txt (pulled in below via
# add_subdirectory) declares "LANGUAGES C" itself, but enabling C only
# through that nested project() call -- instead of here, in the first
# project() CMake processes -- leaves target_compile_features(c_std_11)
# unable to find the C compiler's feature table at all (reproduced outside
# the PS5 toolchain too: plain host gcc hits the same "no known features
# for C compiler \"\"" error). Declaring both here avoids it.
project(OrbisAFR_PS5 C CXX)

# Path to a checkout of https://github.com/OnionBuddies/onionHEN-plugin-sdk
if (NOT DEFINED ONION_PLUGIN_SDK)
	set(ONION_PLUGIN_SDK "$ENV{ONION_PLUGIN_SDK}")
endif()
if (NOT ONION_PLUGIN_SDK)
	message(FATAL_ERROR "Set ONION_PLUGIN_SDK to a checkout of "
	                     "https://github.com/OnionBuddies/onionHEN-plugin-sdk "
	                     "(e.g. -DONION_PLUGIN_SDK=/path/to/onionHEN-plugin-sdk)")
endif()

# Path to a checkout of https://github.com/etaHEN/etaHEN-Plugins -- used only
# for its vendored libhijacker.a + headers (see README.md: "Why this isn't a
# straight port" / the OnionHEN section).
if (NOT DEFINED ETAHEN_PLUGINS_SDK)
	set(ETAHEN_PLUGINS_SDK "$ENV{ETAHEN_PLUGINS_SDK}")
endif()
if (NOT ETAHEN_PLUGINS_SDK)
	message(FATAL_ERROR "Set ETAHEN_PLUGINS_SDK to a checkout of "
	                     "https://github.com/etaHEN/etaHEN-Plugins (only its "
	                     "lib/libhijacker.a and include/ are used)")
endif()

# onion_add_plugin() only becomes available this way (via find_package) once
# the SDK has been `cmake --install`ed somewhere; consuming it straight from
# a git checkout -- the only thing CI (and most people) will have on hand --
# means building it the same way its own README builds its samples: as a
# subdirectory of our own project.
add_subdirectory("${ONION_PLUGIN_SDK}" onion-plugin-sdk-build)

set(D_SRC "${CMAKE_CURRENT_SOURCE_DIR}/source")
file(GLOB SrcFiles ${D_SRC}/*.cpp)

onion_add_plugin(
	NAME OrbisAFR_PS5
	TITLE_ID AFRP00001
	VERSION 0.1.0
	SOURCES ${SrcFiles}
	INCLUDE_DIRS "${ETAHEN_PLUGINS_SDK}/include"
	LIBRARIES hijacker SceSystemService SceUserService SceSysCore
	COMPILE_OPTIONS -std=c++20 -Wno-error)

target_link_directories(OrbisAFR_PS5 PRIVATE "${ETAHEN_PLUGINS_SDK}/lib")
