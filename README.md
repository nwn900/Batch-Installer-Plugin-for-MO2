# Batch Download Installer for Mod Organizer 2

Installs multiple downloaded archives in Mod Organizer 2 one at a time — no more clicking Install, waiting for the mod dialog, naming the mod, and repeating fifty times. Select every archive you downloaded, right-click, and install them all in sequence with one action.

## What it does

- Adds an **"Install selected downloads"** option to the Downloads tab context menu when two or more downloads are selected
- Installs each selected archive sequentially through MO2's own install pipeline — FOMODs still open their installers, texture packs still prompt for options
- Uses the same naming as stock MO2: mods are named from the download's Nexus metadata, falling back to the archive filename without the extension
- Runs entirely as a plugin — **it does not modify ModOrganizer.exe**, requires no core patches, and leaves your stock installation untouched

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

```
cmake -S . -B build -DBUILD_QT6=ON -DCMAKE_PREFIX_PATH=C:\Qt\6.7.1\msvc2019_64 -DMO2_UIBASE_PATH=<uibase source root> -DUIBASE_STUB_IMPORT_LIB=<path to stub lib>
cmake --build build --config Release --target BatchDownloadInstaller
```

- `BUILD_QT6=ON` — Qt 6 build for MO2 2.5.0 / 2.5.2
- `BUILD_QT6=OFF` — Qt 5 build for MO2 2.4.4
- `MO2_UIBASE_PATH` — the MO2 uibase source tree matching the target MO2 version (see `cmake/SelectMo2Sdk.cmake`)
- `UIBASE_STUB_IMPORT_LIB` — a stub import library exporting the uibase symbols; at runtime MO2's own uibase.dll provides them

## License

MIT — see [LICENSE](LICENSE).

Mod Organizer 2 is created by Tannin and the MO2 team. This is an independent add-on, not affiliated with the Mod Organizer project.
