#include <stdint.h>
#include "open_hook_stuff.h"

/* Freestanding file-redirect hook for a game's `open`/`sceKernelOpen` PLT entry.
 * No libc is linked into this translation unit: every helper is local so the
 * compiled .text section can be extracted as raw position-independent shellcode
 * and injected into the target game's process (see ps5/source/hook.cpp).
 *
 * Layout mirrors etaHEN-Plugins' Game_Plugin_Loader sample: the struct below
 * is allocated in the game's data segment, filled in by the plugin (host side)
 * before the hook is installed, and passed as the hook's last argument.
 */

#define PATH_BUF_SIZE 512

__attribute__((used, noinline))
long afr_open_hook(const char *path, long flags, long mode, struct OpenHookStuff *restrict stuff) {
	char buf[PATH_BUF_SIZE];
	int i, j;
	const char *p = path;

	if (!path || !stuff || !stuff->original_open) {
		return -1;
	}

	/* Build candidate = prefix + path (path without its leading '/', since
	 * prefix already ends in '/'). This is the same two-step strategy
	 * OrbisAFR used on PS4: try the redirected copy first, fall back to the
	 * game's own file on any failure (missing redirect, read-only media, …). */
	if (*p == '/') {
		p++;
	}

	for (i = 0; i < stuff->prefix_len && i < PATH_BUF_SIZE - 1; i++) {
		buf[i] = stuff->prefix[i];
	}

	j = i;
	while (*p && j < PATH_BUF_SIZE - 1) {
		buf[j++] = *p++;
	}
	buf[j] = '\0';

	long ret = stuff->original_open(buf, flags, mode);
	if (ret >= 0) {
		return ret;
	}

	return stuff->original_open(path, flags, mode);
}
