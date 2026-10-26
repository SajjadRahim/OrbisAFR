#pragma once
#include <stdint.h>

/* Shared between open_hook.c (compiled to raw shellcode, injected into the
 * game) and ../source/hook.cpp (host side, fills this struct and writes it
 * into the game's data segment before patching the PLT). Keep both sides in
 * sync — this is plain, fixed layout, no padding games, so it round-trips
 * as long as both are built for x86_64 LP64 (true for every PS5 toolchain). */
struct OpenHookStuff {
	long (*original_open)(const char *path, long flags, long mode);
	int prefix_len;   /* strlen(prefix), cached so the hook never calls strlen */
	char prefix[64];  /* e.g. "/data/afr/CUSA12345/" (must end in '/')        */
};
