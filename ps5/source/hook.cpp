#include "hook.hpp"
#include "../shellcode/open_hook_stuff.h"
#include "../shellcode/open_hook_bytes.h"
#include <nid.hpp>
#include <cstdio>
#include <cstring>

extern void plugin_log(const char *fmt, ...);

namespace {

/*
 * open()/sceKernelOpen() take 3 real arguments (path, flags, mode), so they
 * land in rdi, rsi, rdx. The compiled shellcode (open_hook.c) expects a 4th
 * argument -- the OpenHookStuff* -- in rcx, the next SysV integer register.
 * The game's own call site only ever passes 3 arguments, so we inject that
 * 4th one ourselves with a 10-byte prefix ("mov rcx, imm64") immediately
 * before jumping into the compiled hook body. Same trick etaHEN-Plugins'
 * Game_Plugin_Loader sample uses for scePadReadState, just RCX instead of
 * RDX because open() has one more real parameter.
 */
constexpr size_t TRAMPOLINE_PREFIX_SIZE = 10; // 48 b9 + imm64
constexpr size_t STUFF_ADDR_OFFSET = 2;

struct Trampoline {
	uint8_t prefix[TRAMPOLINE_PREFIX_SIZE];
	uint8_t hook_body[AFR_OPEN_HOOK_SIZE];
};

// Not constexpr: it copies from afr_open_hook_code, a plain `static const`
// array (not itself constexpr), so this can only ever run at runtime.
Trampoline makeTrampoline() {
	Trampoline t{};
	t.prefix[0] = 0x48;
	t.prefix[1] = 0xb9; // mov rcx, imm64
	for (size_t i = 0; i < AFR_OPEN_HOOK_SIZE; i++) {
		t.hook_body[i] = afr_open_hook_code[i];
	}
	return t;
}

void setTrampolineStuffAddr(Trampoline &t, uintptr_t addr) {
	*reinterpret_cast<uintptr_t *>(t.prefix + STUFF_ADDR_OFFSET) = addr;
}

} // namespace

bool InstallOpenHook(UniquePtr<Hijacker> &hijacker, const char *redirect_prefix) {
	size_t prefix_len = strlen(redirect_prefix);
	if (prefix_len == 0 || prefix_len >= sizeof(OpenHookStuff{}.prefix)) {
		plugin_log("InstallOpenHook: redirect_prefix too long/empty (%zu bytes)", prefix_len);
		return false;
	}
	if (redirect_prefix[prefix_len - 1] != '/') {
		plugin_log("InstallOpenHook: redirect_prefix must end in '/': %s", redirect_prefix);
		return false;
	}

	UniquePtr<SharedLib> libkernel = hijacker->getLib("libkernel.sprx");
	if (!libkernel) {
		libkernel = hijacker->getLib("libSceLibcInternal.sprx");
	}
	if (!libkernel) {
		plugin_log("InstallOpenHook: could not find libkernel in the process");
		return false;
	}

	// Try sceKernelOpen first (the native syscall wrapper every title links),
	// fall back to the POSIX `open` alias some titles call directly instead.
	uintptr_t original_open = hijacker->getFunctionAddress(libkernel.get(), nid::sceKernelOpen);
	if (original_open == 0) {
		original_open = hijacker->getFunctionAddress(libkernel.get(), nid::open);
	}
	if (original_open == 0) {
		plugin_log("InstallOpenHook: failed to resolve open()/sceKernelOpen() in libkernel");
		return false;
	}
	plugin_log("InstallOpenHook: resolved original open at 0x%llx", (unsigned long long)original_open);

	auto meta = hijacker->getEboot()->getMetaData();
	if (!meta) {
		plugin_log("InstallOpenHook: eboot has no rtld metadata");
		return false;
	}

	const auto &plttab = meta->getPltTable();
	long open_index = meta->getSymbolTable().getSymbolIndex(nid::sceKernelOpen);
	bool want_sceKernelOpen = true;
	if (open_index < 0) {
		open_index = meta->getSymbolTable().getSymbolIndex(nid::open);
		want_sceKernelOpen = false;
	}
	if (open_index < 0) {
		plugin_log("InstallOpenHook: eboot does not import open()/sceKernelOpen() directly "
		           "(it may only be called from an .sprx the game ships -- not supported yet)");
		return false;
	}
	(void)want_sceKernelOpen;

	uintptr_t plt_slot = 0;
	for (const auto &plt : plttab) {
		if (ELF64_R_SYM(plt.r_info) == static_cast<uint32_t>(open_index)) {
			plt_slot = hijacker->getEboot()->imagebase() + plt.r_offset;
			break;
		}
	}
	if (plt_slot == 0) {
		plugin_log("InstallOpenHook: open() import index found but no matching PLT relocation");
		return false;
	}

	// Build the stuff struct + trampoline, write both into the game's own
	// data/text regions, then flip the PLT slot to point at our trampoline.
	OpenHookStuff stuff{};
	stuff.original_open = reinterpret_cast<long (*)(const char *, long, long)>(original_open);
	stuff.prefix_len = static_cast<int>(prefix_len);
	memcpy(stuff.prefix, redirect_prefix, prefix_len + 1);

	uintptr_t stuff_addr = hijacker->getDataAllocator().allocate(sizeof(OpenHookStuff));
	uintptr_t code_addr = hijacker->getTextAllocator().allocate(sizeof(Trampoline));
	if (!stuff_addr || !code_addr) {
		plugin_log("InstallOpenHook: failed to allocate space in the game process");
		return false;
	}

	Trampoline trampoline = makeTrampoline();
	setTrampolineStuffAddr(trampoline, stuff_addr);

	if (!hijacker->write(stuff_addr, stuff) || !hijacker->write(code_addr, trampoline)) {
		plugin_log("InstallOpenHook: failed to write hook payload into the game process");
		return false;
	}

	if (!hijacker->write<uintptr_t>(plt_slot, code_addr)) {
		plugin_log("InstallOpenHook: failed to patch the PLT slot");
		return false;
	}

	plugin_log("InstallOpenHook: installed. redirect_prefix=%s plt_slot=0x%llx code=0x%llx",
	           redirect_prefix, (unsigned long long)plt_slot, (unsigned long long)code_addr);
	return true;
}
