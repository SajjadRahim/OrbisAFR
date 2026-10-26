// OrbisAFR for PS5 -- etaHEN plugin edition
//
// Unlike the original PS4 OrbisAFR, this does NOT patch the kernel's sys_open
// syscall table. PS5 jailbreaks don't expose a stable, documented kernel
// hook API the way the PS4 ones used (hardcoded firmware offsets), so this
// instead attaches to each game's own process (via etaHEN-Plugins' Hijacker)
// and patches its PLT entry for open()/sceKernelOpen(), the same way the
// SDK's own Game_Plugin_Loader sample patches scePadReadState(). See
// ../README.md for the full writeup and ../source/hook.cpp for the patch
// itself.
//
// Redirect convention (same spirit as the original OrbisAFR):
//   a game's own file   -> read normally
//   /data/afr/<TITLEID>/<same relative path>  -> tried FIRST, if present
// So dropping a translated file at
//   /data/afr/CUSA12345/content/paks/pakchunk0-PS5_ar.pak
// overrides that file for title CUSA12345 without touching the disc/app0.

#include "hook.hpp"
#include <hijacker/hijacker.hpp>
#include <notify.hpp>
#include <backtrace.hpp>
#include <signal.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <string>

#define AFR_LOG_PATH "/data/afr/afr_plugin.log"
#define AFR_DATA_ROOT "/data/afr/"

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
	printf_notification("AFR plugin crashed with signal %d. Relaunch it from the\netaHEN toolbox if needed.", signo);
	printBacktraceForCrash();
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

	struct sigaction act{};
	act.sa_handler = sig_handler;
	sigemptyset(&act.sa_mask);
	for (int i = 0; i < 12; i++) {
		sigaction(i, &act, nullptr);
	}

	printf_notification("OrbisAFR (PS5) started.\nWaiting for a game to launch...");

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
			printf_notification("AFR: redirect active for %s\n(%s)", title_id.c_str(), redirect_prefix.c_str());
			last_hooked_title = title_id;
		} else {
			plugin_log("InstallOpenHook failed for %s -- will retry on next poll", title_id.c_str());
			printf_notification("AFR: failed to hook %s, see afr_plugin.log", title_id.c_str());
			// Don't spin forever retrying a title we can't hook; wait for it
			// to be replaced by something else before trying again.
			last_hooked_title = title_id;
		}

		usleep(500 * 1000);
	}

	return 0;
}
