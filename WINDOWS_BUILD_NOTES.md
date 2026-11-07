# Windows GUI Build — Debugging Notes

Notes from getting `Grabber.exe` (GUI target) cross-compiled from Linux for Windows
and hunting down why the resulting binary misbehaved. Kept here so the fixes and
their reasoning aren't lost — several of these bugs are non-obvious and could easily
be reintroduced.

## Context

Branch: `download-fix`. Two independent efforts happened in this session:

1. Reviewing/hardening the download-stall-detection code (the original ask).
2. Reconstructing a Linux→Windows cross-compilation pipeline for the GUI target,
   which surfaced several real, previously-latent bugs — including one serious one
   that made every mingw-built `Grabber.exe` silently run the wrong `main()`.

## Toolchain used

- Ubuntu system MinGW-w64 (`x86_64-w64-mingw32-g++`/`gcc`, GCC 10.3.0) for actual
  C/C++ compilation.
- Wine to run Qt's Windows-only host tool binaries (`moc.exe`, `rcc.exe`, `uic.exe`,
  etc.) from the aqt-installed Qt 6.6.3 `mingw_64` kit, via wrapper scripts
  (`wine-moc`, `wine-rcc`, ...) and a `CMAKE_PROJECT_INCLUDE` hook overriding the
  Qt6 tool targets' `IMPORTED_LOCATION*` properties.
- A CMake toolchain file setting `CMAKE_SYSTEM_NAME Windows`, pointing
  `CMAKE_PREFIX_PATH` at the Qt mingw kit, and manually setting `OPENSSL_*`
  variables (mingw OpenSSL isn't discoverable via normal system paths).

No toolchain file survived from the original working build session, so this was
reconstructed from scratch based on leftover artifacts (`wine-*` scripts, a stale
`src/build-windows/CMakeCache.txt`).

## Bugs found and fixed

### 1. Download stall/hang fix — double `saved()` emission

`src/lib/src/downloader/image-downloader.cpp`, `checkStall()`.

The user's existing stall-retry logic (stall timer + retry, immediate flush on
`readyRead` in `file-downloader.cpp`) was sound, but the give-up path emitted
`saved()` **without first aborting/disconnecting** the stalled `NetworkReply`. If
the connection blipped back to life right after giving up, `saved()` could fire
twice for the same image. Fixed by making the abort/disconnect/cleanup run
unconditionally before the retry-vs-give-up branch.

### 2. Header casing (`ShlObj.h` / `Windows.h`)

`src/gui/src/helpers.cpp`, `src/lib/src/functions.cpp`, `src/lib/src/windows-file-property.h`.

Ubuntu's mingw-w64 sysroot only ships lowercase header names
(`/usr/x86_64-w64-mingw32/include/`). Legacy PascalCase includes
(`<ShlObj.h>`, `<Windows.h>`) fail to resolve on a case-sensitive filesystem.
Fixed by switching to lowercase includes at the source level (there was a
pre-existing root-owned compat symlink for `Windows.h` on this machine from an
earlier session — deliberately not extended further, since relying on manual
system-wide symlinks isn't portable).

### 3. `windows-url-protocol.*` incorrectly gated behind `WIN_FILE_PROPS`

`src/lib/CMakeLists.txt`.

```cmake
if(WIN_FILE_PROPS)
    set(LIBS ${LIBS} propsys shlwapi)
else()
    listFilterRegex(SOURCES "windows-file-property.*")
endif()

if(NOT WIN32)
    listFilterRegex(SOURCES "windows-url-protocol.*")
endif()
```

`windows-url-protocol.cpp` (plain `QSettings`-based registry code for OAuth2
URL-protocol registration) has no MSVC-specific dependency, but was previously
excluded from the build unless the MSVC-only `WIN_FILE_PROPS` flag was set —
breaking the link (`undefined reference to protocolInstall/...`) for any non-MSVC
Windows build. Fixed by gating its exclusion on `NOT WIN32` instead.

### 4. `WIN_FILE_PROPS` not enabled for mingw

`src/CMakeLists.txt`.

```cmake
set(WIN_FILE_PROPS 0)
if(MSVC OR CMAKE_CXX_COMPILER MATCHES "cl\\.exe" OR (WIN32 AND MINGW))
    set(WIN_FILE_PROPS 1)
    add_definitions(-DWIN_FILE_PROPS=1)
    add_definitions(-DNTDDI_VERSION=NTDDI_VISTA -D_WIN32_WINNT=0x0601)
endif()
```

The original (pre-existing) successful build had `WIN_FILE_PROPS=1` for mingw too
(Windows Property System / file-metadata support via `propsys`), which a from-scratch
mingw CMake configure does not enable by default (it's MSVC-only upstream). Extended
the condition to cover `WIN32 AND MINGW`. This also required linking `shlwapi`
(`SHStrDupW`) alongside `propsys` in `src/lib/CMakeLists.txt` — without it the link
fails with `undefined reference to __imp_SHStrDupW`.

### 5. **Major bug**: `Grabber.exe` was silently running the wrong `main()`

This is the one worth remembering. Symptom: `Grabber.exe` immediately showed a
native "Grabber" message box containing the **CLI** `--help` usage text
(`-t, --tags`, `-s, --sources`, `--download`, ...) instead of opening the main
window — reproducible with zero arguments, via double-click *and* `cmd.exe`,
confirmed byte-identical binary via SHA256 and clean `argv` via
`Get-CimInstance Win32_Process`.

**Root cause**: `src/cli/CMakeLists.txt` builds a static library `cli-lib`
(shared CLI-parsing logic — `parseAndRunCliArgs`, `CliCommand` classes — reused by
both `Grabber.exe` and `Grabber-cli.exe`). Its sources came from:

```cmake
file(GLOB_RECURSE SOURCES "src/*.cpp" "vendor/*.cpp")
add_library(${PROJECT_NAME}-lib STATIC ${SOURCES})
```

This glob also swept up `src/cli/src/main.cpp` — the **CLI executable's own entry
point**, which hardcodes `parseAndRunCliArgs(&app, &profile, /*defaultToGui=*/false, ...)`.

Separately, `Grabber.exe` is built as a `WIN32`-subsystem Qt app
(`qt_add_executable(gui WIN32 ...)`), which renames the GUI's own `main()` to
`qMain` at compile time (`-Dmain=qMain`), expecting Qt's own entry-point wrapper
(`WinMain` → `qtEntryPoint()`) to call it after doing its own `argv` setup via
`CommandLineToArgvW`.

Because `gui`'s own `main.cpp` no longer provides a plain, unmangled `main` symbol,
and `gui` links against `cli-lib` (`target_link_libraries(gui ... lib "cli-lib")`
in `src/gui/CMakeLists.txt`), the linker silently satisfied the CRT's undefined
`main` reference by pulling in `cli-lib.a`'s copy of `main.cpp.obj` instead — the
**CLI's** entry point, with `defaultToGui` hardcoded `false`. So `Grabber.exe`'s
actual CRT-invoked `main()` was the CLI's, which — with `defaultToGui=false` and
no CLI action flag given — correctly falls through to `parser.showHelp()` in
`src/cli/src/cli.cpp`. The GUI's real `qMain` (in `src/gui/src/main/main.cpp`) was
present in the binary (confirmed via `nm`/`objdump`/`strings`) but **completely
unreferenced by anything** — dead code, never called. Confirmed by disassembling
`__tmainCRTStartup`: it called `main` at an address inside what turned out to be
`libcli-lib.a`'s object, not `gui`'s own `main.cpp.obj`.

This is why every debug statement added to `gui/src/main/main.cpp` — a `QMessageBox`,
a `fopen`-based log write, a raw `MessageBoxA`, even an unconditional `ExitProcess`
call at the very first line — silently had zero effect. The function containing
them was never invoked.

**Fix**: `src/cli/CMakeLists.txt` — exclude `main.cpp` from `cli-lib`'s sources so
the shared library only contains reusable logic, not a competing entry point:

```cmake
file(GLOB_RECURSE SOURCES "src/*.cpp" "vendor/*.cpp")
include(ListFilterRegex)
set(LIB_SOURCES ${SOURCES})
listFilterRegex(LIB_SOURCES "src/main\\.cpp$")
add_library(${PROJECT_NAME}-lib STATIC ${LIB_SOURCES})
...
add_executable(${PROJECT_NAME} ${SOURCES})   # unaffected — still has its own main.cpp
```

Verified fixed by disassembly: `libcli-lib.a` no longer exports a `main` symbol,
and `__tmainCRTStartup → main (mingw trampoline) → WinMain → qtEntryPoint() → qMain`
now correctly reaches the GUI's own compiled `main.cpp`.

**Why this was latent rather than always-broken**: this only bites when something
provides an *alternative* definition of the CRT's expected `main`/`WinMain` symbol
that the linker can silently substitute. It's plausible official/MSVC builds never
hit this (different linker duplicate-symbol resolution behavior, or a different
official build process that never links `cli-lib` into `gui` the same way) — this
was only discovered because of the from-scratch mingw toolchain reconstruction.

### 6. `sites/` folder missing generated `model.js` / `helper.js`

Symptom (after fixing #5): the GUI now actually launched, but immediately showed
"No source found — Grabber: No source found. Do you have a configuration problem?
Try to reinstall the program."

**Root cause**: `Profile::Profile()` (`src/lib/src/models/profile.cpp`) builds a
`Source` per subfolder of `sites/`. Each `Source` requires a `model.js` (per-site)
and a shared `sites/helper.js` — both of which are **generated build artifacts**,
not checked into git (see `.gitignore`: `src/sites/helper.js`, `src/sites/*/model.js`),
produced from TypeScript (`model.ts`/`helper.ts`) by `npm run build` (`tsc`) in
`src/sites/`. This step was never run as part of the from-scratch mingw
cross-compile pipeline, so every `Source::isValid()` was false and the sites map
was empty.

**Fix**: run `npm install && npm run build` inside `src/sites/`, then redeploy the
`sites/` folder into the release directory the same way
`scripts/package-windows.bat` does — `xcopy` with `src/sites/exclude_xcopy.txt`
(excludes `.ts` files, `node_modules`, `package.json`, tooling configs, and a few
specific site folders: `Gfycat`, `Hentai Cosplays`, `Sankaku`, `Slushe`, `Tumblr`,
`Twitter`).

## Known remaining gaps / uncommitted state

- **`src/lib/vendor/lexbor` submodule** has an uncommitted, load-bearing change
  (`feature.cmake`: `STRING(REGEX REPLACE ...)` → `list(REMOVE_ITEM ...)`) required
  for the mingw build to succeed at all. Left uncommitted per user's choice — will
  need to be reapplied if the submodule checkout is reset.
- **`release-windows/languages/`** is currently empty — only the `gui` CMake target
  was built, not `translations`. Left as-is per user's choice.
- Wine proved to be an unreliable proxy for testing the actual bug throughout this
  session (it ran builds successfully that failed identically, byte-for-byte, on
  real Windows, and separately crashes on real network/SSL code paths that
  presumably work fine on real Windows) — real-hardware testing was necessary to
  find and confirm bug #5.
