[heading]Batch Download Installer for Mod Organizer 2[/heading]

Installs multiple downloaded archives in Mod Organizer 2 one at a time — no more clicking Install, waiting for the mod dialog, naming the mod, and repeating fifty times. Select every archive you downloaded, right-click, and install them all in sequence with one action.

[b]What it does[/b]

• Adds a [b]“Install selected downloads”[/b] option to the Downloads tab context menu when two or more downloads are selected
• Installs each selected archive sequentially through MO2's own install pipeline — FOMODs still open their installers, texture packs still prompt for options
• Uses the same naming as stock MO2: mods are named from the download's Nexus metadata, falling back to the archive filename without the extension
• Runs entirely as a plugin — [b]it does not modify ModOrganizer.exe[/b], requires no core patches, and leaves your stock installation untouched

[b]Why this exists[/b]

Stock MO2 has no batch-install option. If you download thirty mods before starting a playthrough, installing them one by one takes forever. This plugin removes that grind — you select everything in the Downloads tab and walk away while MO2 handles them in order.

The plugin is 100% additive: uninstall it and your MO2 behaves exactly as before. There is no persistent state, no save-file impact, and nothing written outside the normal install process.

[b]Requirements[/b]

[list]
[*] Mod Organizer 2 — version 2.4.4, 2.5.0, or 2.5.2
[*] No SKSE, no DLL frameworks, no dependencies beyond MO2 itself
[/list]

[b]Installation[/b]

[list=1]
[*] Download the file matching your MO2 version (see the Files tab below — each MO2 version has its own DLL)
[*] Open the folder where ModOrganizer.exe lives
[*] Navigate to the [b]plugins[/b] folder
[*] Extract the DLL into [b]plugins\[/b] — it must sit directly in the plugins folder, next to the other plugin DLLs
[*] Start Mod Organizer 2 — done. The plugin loads automatically
[/list]

To verify it loaded: run MO2, open the Downloads tab, select two or more finished downloads with Ctrl+click, right-click, and confirm [b]“Install selected downloads”[/b] appears in the context menu.

[b]Usage[/b]

[list=1]
[*] In the [b]Downloads[/b] tab, select the archives you want to install. Use Ctrl+click for individual picks or Shift+click for a range
[*] Right-click any selected download
[*] Click [b]“Install selected downloads”[/b]
[*] MO2 installs each archive in order. FOMOD installers, texture option prompts, and any other installer dialogs appear as usual — confirm each one as it comes up
[*] When the queue finishes, your mods appear in the left pane, enabled, ready to sort
[/list]

[b]Behavior details[/b]

[list]
[*] Files are installed in the order they appear in the Downloads list (top to bottom)
[*] Mod names come from the download's Nexus metadata ([i].meta[/i] file), with the archive name as fallback — identical to installing the same download normally
[*] If an archive is already installed, the normal reinstall/update prompt behavior applies
[*] You can cancel the queue between items; the currently-running installation completes first
[/list]

[b]Notes & troubleshooting[/b]

[list]
[*] The batch option only appears when [b]two or more[/b] downloads are selected. With one selected, use the stock Install option
[*] Downloads that are still downloading or paused are ignored
[*] If the context menu option doesn't appear, confirm the DLL is in the [b]plugins[/b] folder directly (not in a subfolder) and that MO2 was restarted after placing it
[*] This plugin coexists with every other plugin — it only adds one context menu entry
[/list]

[b]Credits[/b]

Built for the MO2 modding community. No assets from other mods are used; this is a standalone plugin written against the public MO2 plugin interface.

[hr]
[size=1]Mod Organizer 2 is created by Tannin and the MO2 team. This is an independent add-on, not affiliated with the Mod Organizer project.[/size]
