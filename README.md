# Batch Installer Plugin for MO2

Installs multiple downloaded archives in Mod Organizer 2 one at a time — no more clicking Install, waiting for the mod dialog, naming the mod, and repeating fifty times. Select every archive you downloaded, right-click, and install them all in sequence with one action.

## What it does

- Adds an **"Install selected downloads"** option to the Downloads tab context menu when two or more downloads are selected
- Installs each selected archive sequentially through MO2's own install pipeline — FOMODs still open their installers, texture packs still prompt for options
- Uses the same naming as stock MO2: mods are named from the download's Nexus metadata, falling back to the archive filename without the extension
- **Deferred directory refresh (in memory only):** on supported 64-bit MO2 2.4.4 / 2.5.0 / 2.5.2 release builds, a MinHook detour on `OrganizerCore::refreshDirectoryStructure()` defers virtual-directory work until one final `refresh(false)` after the batch. Unknown `ModOrganizer.exe` hashes disable fast mode with a log line.
- Runs entirely as a plugin — **it does not modify ModOrganizer.exe on disk**, requires no core patches, and leaves your stock installation untouched

## Why this exists

Stock MO2 has no batch-install option. If you download thirty mods before starting a playthrough, installing them one by one takes forever. This plugin removes that grind — you select everything in the Downloads tab and walk away while MO2 handles them in order.

The plugin is 100% additive: uninstall it and your MO2 behaves exactly as before. There is no persistent state, no save-file impact, and nothing written outside the normal install process.

## Requirements

- Mod Organizer 2 — version 2.4.4, 2.5.0, or 2.5.2
- No SKSE, no DLL frameworks, no dependencies beyond MO2 itself

## Installation

1. Download the archive matching your MO2 version from the [Releases](https://github.com/nwn900/MO2-Batch-Download-Installer/releases) page — each MO2 version has its own DLL
2. Open the folder where ModOrganizer.exe lives
3. Navigate to the **plugins** folder
4. Extract the DLL into `plugins\` — it must sit directly in the plugins folder, next to the other plugin DLLs
5. Start Mod Organizer 2 — done. The plugin loads automatically

To verify it loaded: run MO2, open the Downloads tab, select two or more finished downloads with Ctrl+click, right-click, and confirm **"Install selected downloads"** appears in the context menu.

## Usage

1. In the **Downloads** tab, select the archives you want to install. Use Ctrl+click for individual picks or Shift+click for a range
2. Right-click any selected download
3. Click **"Install selected downloads"**
4. MO2 installs each archive in order. FOMOD installers, texture option prompts, and any other installer dialogs appear as usual — confirm each one as it comes up
5. When the queue finishes, your mods appear in the left pane, enabled, ready to sort

## Behavior details

- Files are installed in the order they appear in the Downloads list (top to bottom)
- Mod names come from the download's Nexus metadata (`.meta` file), with the archive name as fallback — identical to installing the same download normally
- If an archive is already installed, the normal reinstall/update prompt behavior applies
- You can cancel the queue between items; the currently-running installation completes first

## Notes & troubleshooting

- The batch option only appears when **two or more** downloads are selected. With one selected, use the stock Install option
- Downloads that are still downloading or paused are ignored
- If the context menu option doesn't appear, confirm the DLL is in the **plugins** folder directly (not in a subfolder) and that MO2 was restarted after placing it
- This plugin coexists with every other plugin — it only adds one context menu entry

## Building from source

Qt 6 (MO2 2.5.x): use **current** [modorganizer-uibase](https://github.com/ModOrganizer2/modorganizer-uibase) master (`include/uibase/` layout).

```
cmake -S . -B build-qt6 -DBUILD_QT6=ON -DCMAKE_PREFIX_PATH=C:\Qt\6.7.1\msvc2019_64 -DMO2_UIBASE_PATH=<uibase source root>
cmake --build build-qt6 --config Release --target BatchDownloadInstaller
```

Qt 5 (MO2 2.4.4): checkout uibase tag **v2.5.2** (still `src/*.h` layout — see `cmake/SelectMo2Sdk.cmake`).

```
cmake -S . -B build-qt5 -DBUILD_QT6=OFF -DBATCH_CXX_STANDARD=17 -DCMAKE_PREFIX_PATH=C:\Qt\5.15.2\msvc2019_64 -DMO2_UIBASE_PATH=<uibase at tag v2.5.2>
cmake --build build-qt5 --config Release --target BatchDownloadInstaller
```

- Output DLL names: `BatchDownloadInstaller-qt6.dll` / `BatchDownloadInstaller-qt5.dll`
- With no `UIBASE_IMPORT_LIB` / `UIBASE_STUB_IMPORT_LIB`, CMake links a minimal `VersionInfo` stub (`cmake/stub_versioninfo.cpp`) so the plugin DLL links standalone; MO2 still provides real uibase at runtime.
- Optional: `UIBASE_IMPORT_LIB` or `UIBASE_STUB_IMPORT_LIB` if you have a uibase import lib.

## Third-party code

- [MinHook](https://github.com/TsudaKageyu/minhook) (BSD 2-clause) — see `third_party/minhook/LICENSE.txt`

## License

MIT — see [LICENSE](LICENSE).

Mod Organizer 2 is created by Tannin and the MO2 team. This is an independent add-on, not affiliated with the Mod Organizer project.
