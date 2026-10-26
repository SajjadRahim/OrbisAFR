#pragma once

#include <hijacker/hijacker.hpp>
#include <cstdint>

/**
 * Installs the open()-redirect hook (see ../shellcode/open_hook.c) into the
 * given game process, so that any file it opens under `redirect_prefix` is
 * tried first, falling back to the game's own copy on failure.
 *
 * @param hijacker       Attached handle to the target game process.
 * @param redirect_prefix Absolute path the hook should prepend to the
 *                         game's own (relative) open() path, e.g.
 *                         "/data/afr/CUSA12345/". Must end in '/'.
 * @return true on success, false if the PLT entry for open()/sceKernelOpen
 *         could not be found or the injection failed.
 */
bool InstallOpenHook(UniquePtr<Hijacker> &hijacker, const char *redirect_prefix);
