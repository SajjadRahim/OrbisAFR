# OrbisAFR for PS5 (etaHEN plugin)

A port of OrbisAFR's idea -- redirect a game's file reads to a `/data`
folder at runtime, with the original game files on disc untouched -- to
PS5. The implementation is necessarily different from the PS4 version;
see **Why this isn't a straight port** below.

## What it does

Exactly what OrbisAFR did on PS4: while a game runs, any file it tries to
open is first looked up under a per-title redirect folder; if found there,
that copy is used instead; otherwise the game's own file loads as normal.
Nothing on the disc, in `app0`, or anywhere else belonging to the original
game is ever touched or copied over.

```
/data/afr/<TITLEID>/<same relative path the game itself would open>
```

Example, from the Arabic translation use case this was built for: to
override a Beast of Reincarnation (`<TITLEID>`) pak file, drop the
translated file at:

```
/data/afr/<TITLEID>/content/paks/pakchunk0-PS5_ar.pak
```

The next time the game opens `content/paks/pakchunk0-PS5_ar.pak`, it gets
your file instead -- the one on the disc is never modified.

## Why this isn't a straight port

OrbisAFR's PS4 payload patches the kernel's `sysent` syscall table directly,
using hardcoded offsets for one specific PS4 firmware's kernel (see
`../source/kernel_func.c`, `../include/kernel_func.h`). That approach needs:
- a kernel exploit for *that* firmware,
- and a full reverse-engineered offset table for every kernel structure and
  function it touches.

Neither exists for PS5 in a form equivalent to the PS4 scene's OrbisAFR. More
importantly, PS5 does **not** have a GoldHEN-equivalent *kernel syscall table*
hook. What it does have is **etaHEN** (<https://github.com/LightningMods/etaHEN>),
the PS5 scene's homebrew enabler/plugin host, together with its Plugin SDK
(<https://github.com/etaHEN/etaHEN-Plugins>), which exposes `libhijacker`: a
userland library that can attach to a *running game's own process* (not the
kernel) and patch its imports.

So instead of hooking the kernel's `open` syscall globally, this plugin:
1. Watches for whichever game is in the foreground.
2. Attaches to that game's process with `libhijacker`.
3. Finds the game executable's PLT entry for `open()`/`sceKernelOpen()`
   (the function it calls to open any file).
4. Resolves the *real* open() address in `libkernel.sprx` first, then
   overwrites the PLT entry to jump to a small injected hook instead.
5. The hook (`../shellcode/open_hook.c`) tries the redirected path first,
   falls back to calling the real open() with the original path otherwise.

This is the same technique etaHEN-Plugins' own `Plugin_samples/Game_Plugin_Loader`
sample uses to hook `scePadReadState()` for a different purpose (auto-loading
a `.prx` on controller connect) -- we're hooking a different function
(`open`) for a different purpose (file redirection), but the mechanism
(resolve real function -> allocate a stub in the game's own memory -> patch
one PLT entry) is identical and already proven by that sample.

## Layout

```
ps5/
  CMakeLists.txt        build file; needs ETAHEN_PLUGINS_SDK set (see Building)
  linker.x              standard etaHEN plugin linker script (copied from the SDK sample)
  source/
    main.cpp            plugin entry point: watches the foreground app, calls InstallOpenHook
    hook.hpp / hook.cpp  resolves open()/sceKernelOpen(), patches the PLT, installs the trampoline
  shellcode/
    open_hook.c          the actual redirect logic -- compiled standalone to raw machine code
    open_hook_stuff.h     struct shared between open_hook.c and hook.cpp
    open_hook_bytes.h     generated: open_hook.c compiled + extracted as a byte array
    generate_bytes.py     regenerates open_hook_bytes.h from open_hook.c (verifies no relocations)
```

### Why `shellcode/` is compiled separately

`open_hook.c` gets injected directly into the game's memory and jumped to
from the patched PLT entry -- there is no dynamic linker step at that point.
So it has to compile down to **pure, self-contained machine code**: no libc
calls, no string literals (those would live in `.rodata`, which never gets
copied over), no global state, zero relocations. `generate_bytes.py` compiles
it with clang targeting `x86_64-freebsd-pc-elf`, extracts the `.text`
section, and fails loudly if `readelf -r` finds anything that `objcopy -O
binary` would silently drop. The current `open_hook_bytes.h` (372 bytes) was
generated and verified this way -- re-run the script if you touch
`open_hook.c`.

## Building

You need:
- [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) (clang-18/lld-18 toolchain for PS5)
- A checkout of [etaHEN-Plugins](https://github.com/etaHEN/etaHEN-Plugins) (the Plugin SDK + `libhijacker`)

```sh
git clone https://github.com/etaHEN/etaHEN-Plugins
export ETAHEN_PLUGINS_SDK=$PWD/etaHEN-Plugins
# build etaHEN-Plugins itself first (builds libhijacker.a etc.) -- see its own README

cd ps5
cmake -B build -DETAHEN_PLUGINS_SDK=$ETAHEN_PLUGINS_SDK
cmake --build build
# produces build/bin/OrbisAFR_PS5.plugin (or similar, see make_plugin.py's output)
```

If you change `shellcode/open_hook.c`, regenerate the byte array first:
```sh
python3 shellcode/generate_bytes.py
```

## Installing / using it

1. Copy the built plugin to `/data/etaHEN/plugins/` (internal) or
   `/<usb>/etaHEN/plugins/` on your jailbroken PS5, the same way any other
   etaHEN plugin is installed.
2. Create `/data/afr/<TITLEID>/` and put your replacement files in it, at
   the same relative path the game itself uses (see the pak example above).
3. Launch the game. Check `/data/afr/afr_plugin.log` if the redirect
   doesn't seem to be taking effect.

## Known limitations / what still needs verifying on real hardware

This was written and compile-checked in a sandboxed CI-style environment
with **no PS5 to test against**, so treat this as a first draft to iterate
on against your actual console, not a finished tool:

- **Only catches opens the main executable (eboot) makes directly.** If a
  game only calls `open()`/`sceKernelOpen()` from inside one of its own
  `.sprx` modules (not the eboot itself), `InstallOpenHook` currently
  reports failure (logged) instead of hooking that module too -- that's a
  reasonable next step once we know whether your target game needs it.
- **Path format is assumed to be relative** to the game's own root (matching
  the `beastofreincarnation/content/paks/...`-style example). If the engine
  actually calls `open()` with an absolute sandbox path (e.g. something
  under `/app0/...`), the redirect-prefix logic in `open_hook.c` will need a
  small tweak to strip that prefix instead of just dropping a leading `/`.
- **No existing installed base to compare against** -- unlike OrbisAFR on
  PS4 (years of real-world use), this is new code. Please report back via
  the project thread what `afr_plugin.log` shows and whether the redirect
  actually took effect, so it can be fixed quickly.
