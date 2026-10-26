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
project(OrbisAFR_PS5 CXX)

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

list(APPEND CMAKE_MODULE_PATH "${ONION_PLUGIN_SDK}/cmake")
find_package(OnionHENPluginSDK CONFIG REQUIRED)

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
