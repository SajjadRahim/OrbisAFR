// OrbisAFR for PS5 -- OnionHEN plugin edition
//
// Unlike the original PS4 OrbisAFR, this does NOT patch the kernel's sys_open
// syscall table. PS5 jailbreaks don't expose a stable, documented kernel
// hook API the way the PS4 ones used (hardcoded firmware offsets), so this
// instead attaches to each game's own process (via libhijacker) and patches
// its PLT entry for open()/sceKernelOpen(), the same way etaHEN-Plugins' own
// Game_Plugin_Loader sample patches scePadReadState(). See ../README.md for
// the full writeup and hook.cpp for the patch itself.
//
// Why libhijacker instead of OnionHEN's own plugin SDK for this part:
// OnionHEN's plugin ABI (include/onion/plugin.h) declares
// ONION_PLUGIN_CAP_PROCESS/CAP_INJECT/CAP_KERNEL capability bits, but as of
// this writing its host-side query_interface only implements the UI service
// (ONION_UI_SERVICE_NAME) -- there's no process-injection service to call
// into yet. The plugin descriptor below (ONION_PLUGIN_DEFINE) is still what
// lets OnionHEN discover/validate/start this as a plugin; the actual file
// hook uses libhijacker directly, the same open-source library OnionHEN
// itself is built on (see ../README.md).
//
// Redirect convention (same spirit as the original OrbisAFR):
//   a game's own file   -> read normally
//   /data/afr/<TITLEID>/<same relative path>  -> tried FIRST, if present
// So dropping a translated file at
//   /data/afr/CUSA12345/content/paks/pakchunk0-PS5_ar.pak
// overrides that file for title CUSA12345 without touching the disc/app0.

#include "hook.hpp"
#include <hijacker/hijacker.hpp>
#include <onion/plugin.h>
#include <ps5/payload.h>
#include <signal.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <string>

#define AFR_LOG_PATH "/data/afr/afr_plugin.log"
#define AFR_DATA_ROOT "/data/afr/"

// Lets OnionHEN's plugin manager discover and validate this ELF (the
// ".onion_plugin" ELF section onion_add_plugin's post-build step checks
// for). It does not open an IPC session with the host -- see note above.
//
// No ONION_PLUGIN_FLAG_AUTO_START here: OnionHEN's real plugin manager
// (onion::plugin::kKnownFlags, in onionHEN's own plugin_manager.hpp) only
// recognizes LONG_RUNNING and STOP_SUPPORTED -- AUTO_START isn't in that
// set, so a descriptor carrying it gets rejected outright during discovery
// ("plugin descriptor contains unknown flags"), before the file is even
// added to the plugin list. OnionHEN controls auto-start via a sibling
// "<plugin>.elf.auto_start" marker file instead (see ps5/README.md).
ONION_PLUGIN_DEFINE("AFRP00001", "0.10", "OrbisAFR",
                    ONION_PLUGIN_CAP_PROCESS | ONION_PLUGIN_CAP_INJECT | ONION_PLUGIN_CAP_KERNEL,
                    ONION_PLUGIN_FLAG_LONG_RUNNING | ONION_PLUGIN_FLAG_STOP_SUPPORTED);

extern "C" {
	int sceSystemServiceGetAppIdOfRunningBigApp();
	int sceSystemServiceGetAppTitleId(int app_id, char *title_id);
	int _sceApplicationGetAppId(int pid, int *appId);
}

static void write_log(const char *text) {
	int fd = open(AFR_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0777);
	if (fd < 0) {
		return;
	}
	write(fd, text, strlen(text));
	close(fd);
}

void plugin_log(const char *fmt, ...) {
	char msg[0x1000]{};
	va_list args;
	va_start(args, fmt);
	int len = vsnprintf(msg, sizeof(msg), fmt, args);
	va_end(args);
	if (len <= 0) {
		return;
	}
	if (msg[len - 1] != '\n' && (size_t)len + 1 < sizeof(msg)) {
		msg[len] = '\n';
		msg[len + 1] = '\0';
	}
	write_log(msg);
}

static void sig_handler(int signo) {
	plugin_log("AFR plugin crashed with signal %d", signo);
}

// Returns the pid of the running foreground app, and its title id, or false
// if nothing is running (e.g. still on the home screen).
static bool GetForegroundGame(int &pid, std::string &title_id) {
	int app_id = sceSystemServiceGetAppIdOfRunningBigApp();
	if (app_id < 0) {
		return false;
	}

	char tid[32]{};
	if (sceSystemServiceGetAppTitleId(app_id, tid) != 0) {
		return false;
	}
	title_id = tid;

	for (int j = 0; j <= 9999; j++) {
		int bigappid = 0;
		if (_sceApplicationGetAppId(j, &bigappid) < 0) {
			continue;
		}
		if (bigappid == app_id) {
			pid = j;
			return true;
		}
	}
	return false;
}

uintptr_t kernel_base = 0;

int main() {
	plugin_log("AFR plugin starting...");

	// libhijacker walks the kernel's process list (getAllProcs(), via
	// kernel_base + offsets::allproc()) to find a game's KProc by pid --
	// see Hijacker::getHijacker(int). Without this, kernel_base stays 0 and
	// every lookup silently fails to match any pid, which is why attach
	// kept failing (confirmed against etaHEN-Plugins' own samples, e.g.
	// Game_Plugin_Loader/source/main.cpp, which all set this the same way
	// before doing anything else).
	payload_args_t *args = payload_get_args();
	if (args) {
		kernel_base = args->kdata_base_addr;
	}
	plugin_log("kernel_base = 0x%llx", (unsigned long long)kernel_base);

	struct sigaction act{};
	act.sa_handler = sig_handler;
	sigemptyset(&act.sa_mask);
	for (int i = 0; i < 12; i++) {
		sigaction(i, &act, nullptr);
	}

	std::string last_hooked_title;

	while (true) {
		int pid = 0;
		std::string title_id;

		if (!GetForegroundGame(pid, title_id)) {
			usleep(500 * 1000);
			continue;
		}

		if (title_id == last_hooked_title) {
			// Already hooked this run of this title; just keep watching for
			// it to exit / a different title to take over.
			usleep(500 * 1000);
			continue;
		}

		plugin_log("Detected foreground title %s (pid %d)", title_id.c_str(), pid);

		UniquePtr<Hijacker> hijacker = Hijacker::getHijacker(pid);
		if (!hijacker) {
			plugin_log("Failed to attach to pid %d, will retry", pid);
			usleep(1000 * 1000);
			continue;
		}

		std::string redirect_prefix = std::string(AFR_DATA_ROOT) + title_id + "/";

		if (InstallOpenHook(hijacker, redirect_prefix.c_str())) {
			plugin_log("AFR: redirect active for %s (%s)", title_id.c_str(), redirect_prefix.c_str());
			last_hooked_title = title_id;
		} else {
			plugin_log("InstallOpenHook failed for %s -- will retry on next poll", title_id.c_str());
			// Don't spin forever retrying a title we can't hook; wait for it
			// to be replaced by something else before trying again.
			last_hooked_title = title_id;
		}

		usleep(500 * 1000);
	}

	return 0;
}
