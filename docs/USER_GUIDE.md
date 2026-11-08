# Using Drifter’s Toolkit

[GitHub downloads](https://github.com/NeTriXoN-BoT/drifters-toolkit/releases/latest) · [Nexus Mods](https://www.nexusmods.com/daysgone/mods/1022?tab=files) · [CLI guide](CLI.md) · [Report a problem](https://github.com/NeTriXoN-BoT/drifters-toolkit/issues/new/choose)

This guide covers the **1.1.0 Local Edition**. Download mods from their original pages using **Manual download**, then import the completed files locally. The public toolkit needs no Nexus sign-in or API key. It does not query Nexus for online mod-version checks.

## First run

Extract the complete package into a local folder. Windows uses `DriftersToolkit.exe` and needs Windows 10/11 x64 with Microsoft Edge WebView2 Runtime. Linux uses `Launch-DriftersToolkit.sh`; preserve executable permissions when extracting.

Follow the Overview checklist to select the game and prepare mod loading. Check the selected folder when more than one installation is found. On Linux, start Days Gone through the selected Steam installation once so its Proton prefix exists, then scan again. Native and Flatpak Steam libraries are detected.

Close Days Gone before changing active mods, editing saves or restoring progress. The toolkit refuses changes when it cannot establish that the game has stopped. A queued restart waits for normal closure.

## Bring in your mods

Download from each mod’s original page, then open **My mods** and drag its PAK or supported archive onto the page or use the import button. You can select several files, choose a folder or use **Review & import** when the toolkit notices a download. Detection offers a review; it does not silently activate a newly downloaded package.

Supported archive readers include ZIP, 7z, RAR, TAR and several other formats. Support also depends on the compression method and contents. Encrypted archives, split volumes, links and executable installers are not mod-import targets. Read the mod’s requirements before choosing optional PAKs or tiers.

If an archive contains several PAKs, tick the ones you want. In the desktop interface, one selected PAK enables after import when the checks allow it; several stay stored until you choose what to enable. The CLI stores imports without enabling them. Conflicts, changed files and a running game can still block activation.

Drops follow the page you are using: **My mods** reviews packages for installation, **Workshop** opens PAKs as drafts, and **Saves** imports `.sav` files as a separate backup. Finish any pending review before dropping more files. Workshop asks before replacing a draft with unsaved changes.

## Read the library

- **Enabled** counts managed packages currently switched on.
- **Stored only** counts inactive local library packages, including packages you disabled. Keeping a package here does not load it in game.
- **Available mods** lists catalogue entries you have not added locally. **Needs review** identifies entries that need attention, such as a missing or changed installed copy.

Search and category filters narrow the visible rows; tab counts describe the packages in each collection before those filters. Details explain the selected package, its source, requirements and overlapping assets. **Enable** activates an intact library package after checks. **Disable** removes its managed active copy while keeping the library copy. Removing a package from the library is a separate action after disabling it.

The catalogue covers OkayDude1337’s releases. Supported PAK mods from other authors work as local imports; their authorship and update support stay with the original authors. Downloading a file locally does not verify its origin or make it gameplay-compatible.

### PAKs already installed by hand

The toolkit scans installed PAKs for conflicts. **Manage hand-installed PAKs automatically** is on by default in Settings. Eligible files directly in the game’s Saved/Paks folder can be brought under management while staying enabled, with an independent copy kept in the library. Turn this setting off if another tool manages those files.

Files in nested locations or the game’s Content/Paks folder may only support adding a library copy. Their installed originals stay active until a separate removal review allows you to remove them. The review explains which action applies; protected game archives cannot be removed as mods.

**Stop managing all** hands installed files back without uninstalling them. To remove an external file, use its own reviewed removal action and check the exact file shown. A missing or changed installation entry can be forgotten without deleting its current file, cached package, profiles or saves. Recover a missing library copy from an intact installed PAK or import the original download again.

### Conflicts and combinations

The toolkit reads package indexes and groups mods that replace the same asset paths. **Keep this one** switches off conflicting managed choices so you can keep the selected mod. An overlap may be deliberate, but it deserves review. A clear scan cannot prove that two mods behave correctly together; unreadable packages remain unknown.

ASI/DLL plugins, ReShade and other native components are outside these PAK checks. Some mods need both a PAK and a native plugin. Follow each author’s instructions and the Field guide’s ASI chapter for Windows or Proton setup.

## Save a collection as a profile

Create a profile from **Current enabled mods** to capture independent copies of your managed PAKs, or choose **Blank profile** for a session without profile PAKs. Creating a blank profile does not switch your current collection off. Profiles can be renamed without copying their packages or losing linked backup information.

Before launch, review which PAKs will be set aside and which profile copies will be used. Keep the toolkit open while the game runs. It restores the previous collection after a verified exit. If a file changes, launch remains uncertain or restoration is interrupted, follow the recovery message with the game closed. Keep the recovery files until the session is resolved.

Profiles swap PAKs. **Game saves are shared across profiles.** ASI/DLL plugins and injectors remain active across profile launches; a blank PAK profile does not disable them. Optional automatic save backups can be linked to the profile before or after a session. Switching profiles never replaces active saves.

## Look after your saves

**Saves → Current saves** shows the actual files, with names, dates, sizes and embedded pictures when supported. Some saves contain no picture; that does not make the save unusable. Refresh to retry a picture read. If the game has just created its first saves or you changed accounts, restart the toolkit before making a backup. Multiple possible account folders need a selection rather than a guess.

**Back up now** makes a dated copy with the game closed. Backups can have names, private notes and a custom picture. Protect important copies from automatic cleanup, or filter profile-linked backups to find a session’s progress. A custom backup picture is separate from the picture stored inside a game save.

Select a current slot to export a separate file, import a replacement or delete it. Replacing or deleting a current save requires a complete safety backup even when automatic backups are off. **Import saves as backup…** stores selected files separately without replacing current progress.

Before restoring a backup, review its files. Current saves receive a safety copy first; unrelated current saves remain in place, and restoring saves does not restore mods. Keep the game closed for backup, replacement and restoration.

### Supported save editing

**Inspect save…** opens a read-only snapshot, including while the game runs. Reopen it after the game saves to see fresh data. **Edit save…** starts a separate editing session with the game closed and the correct installation selected.

Supported saves allow editing **unspent skill points**. Review the before/after values and choose **Back up and apply changes**. The complete current save set is backed up before only the chosen slot is replaced. Use a save where the Skills menu is already unlocked; changing points does not unlock that menu. Start the game and use **Load Game** to select the exact edited slot, since Continue may choose another one.

The inspector offers Editable, Read-only and All values views with local search. Unknown, ambiguous and protected progress is not rewritten. **Advanced fields** is off by default. It exposes supported `UseEffectLevels` entries and controller booleans where present, with checked stored types and ranges; their in-game effects are unverified. Each infusion entry has a numbered label and its raw name. Brackets such as `[0]` and `[1]` identify separate positions in one list, rather than duplicate fields or stat levels. Their exact gameplay roles have not been mapped. The names do not promise current or maximum health, stamina or focus. Turning the option off preserves the draft but prevents applying advanced changes until you enable it again or revert them.

## Work on a local package

**Workshop** opens PAKs as a draft, leaving the source packages intact. Inspect files and supported properties, start an empty package, add or replace a file, remove it from the draft with a restore option, or export one file separately. **Use opened bytes** resets an edit to that file’s opened version.

For combinations, open the source PAKs and review shared asset families. Identical files, differences and unresolved choices are listed separately. Use the next-unresolved shortcut to work through them. You can choose a complete family from one source, including its companion files, or compare supported cooked properties against the same matching original exported from your game.

Separate property changes can be combined. If sources change the same value differently, choose what to keep. Supported edits include verified numeric types, existing enum values, vector/colour components, scalar array items and supported text that keeps its encoded length. Unknown layouts, meshes, Blueprint instructions and opaque companion data are not automatically merged.

Choosing a complete family can discard another source’s changes. **Rename path…** moves the family’s file paths together but does not rewrite references inside the assets. Review unresolved items before export and test the resulting PAK in game; a structurally valid package is not a gameplay test.

Save the project to keep checked source copies, additions, removals, renames and unfinished choices for later. Export creates a separate PAK. Keep combinations private unless the original authors permit redistribution.

Open **What does this do?** beside a supported property for a short explanation of its type and any known use. Exact weapon-property mappings are identified; other name clues are labelled as suggestions. A generic Bool or Float type alone does not establish its gameplay meaning. Supported tagged DataTable and CurveTable rows, quaternion components and transforms can also be edited. Unknown native data stays read-only; use a suitable external editor and **Replace file** when necessary.

## Diagnose a problem

Run **Diagnostics** for installation paths, mod loading, package integrity, conflicts and incomplete file changes. Review offered fixes. It cannot repair an unknown mod or explain every game crash.

Crash summaries show the available crash type, location, time and active-mod evidence without opening the memory stored in a dump. **Find the bad mod** tests managed PAKs in rounds: you run the game after each round and report the result. Finish or stop the test through its review so the toolkit can restore the verified originals it still has. Native plugins remain outside this test.

Logs and support reports have redacted previews. Review them before sharing and remove any remaining account details, personal paths or authorized download links. Reports are not uploaded automatically; saves and memory dumps are not automatically bundled. Include the toolkit version, operating system, relevant mod links and reproduction steps in a [bug report](https://github.com/NeTriXoN-BoT/drifters-toolkit/issues/new/choose). Use [private reporting](../SECURITY.md) for vulnerabilities.

The sidebar’s **View source** button opens the toolkit’s public GitHub project.

## Settings and where your data lives

Automatic interface scaling accounts for monitor size, OS DPI and window space. Text follows interface size, with separate manual controls. Themes include dark, light and system appearance; Linux can follow supported desktop palettes. Seasonal colours, details and falling emoji are optional. Reduced motion and high contrast are respected, and automatic motion stops when hidden.

Settings generally save as you change them. Game-folder, Steam-launch and automatic-management changes show their own **Save** button. Use **Ctrl+F** or **/** for the current page’s search and **?** outside a text field for keyboard help. Recent activity retains this session’s last 50 messages after their visible countdown closes.

| Data | Default location |
| --- | --- |
| Windows toolkit data | `%LOCALAPPDATA%\DriftersToolkit` |
| Linux toolkit data | `$XDG_DATA_HOME/drifters-toolkit`, or `~/.local/share/drifters-toolkit` when no absolute XDG override is set |
| Windows managed PAKs | `%LOCALAPPDATA%\BendGame\Saved\Paks` |
| Linux managed PAKs and saves | The selected Steam installation’s actual Proton user folder |

Linux keeps installation-specific state under its toolkit data root. **Folders** and **Settings → Toolkit data** show the locations used by your setup. Extracting the program on another drive does not move its library or backups.

Use **Settings → Toolkit data → Move data** to choose a normal local destination. The toolkit copies and verifies its data at the next start, then cleans up verified originals. A small location record stays in the default user-data folder, and temporary work still uses OS temp. This does not move your game, active PAKs or current game saves.

**Back up library…** makes a private ZIP containing actual packages, profiles and library metadata such as notes and favourites. Save backups are separate. Restoring a library backup adds its checked contents without enabling mods. **Share my mod list** creates a text list with links rather than distributing package files.

## Linux startup

The AppImage needs an x86-64-v2 CPU, glibc 2.35 or newer and working Chromium sandbox support. X11 and Wayland work on tested configurations. Start it as your normal user; the launcher supports a temporary extraction fallback when FUSE is unavailable.

If startup reports a sandbox problem, follow the specific explanation or run:

```sh
./Launch-DriftersToolkit.sh --check-sandbox
```

Some systems need an AppImage-specific AppArmor rule; the check provides guidance for the detected setup. Keep Chromium’s sandbox enabled. Steam/Proton discovery is supported; an arbitrary Wine or Heroic prefix is not interchangeable with a verified Steam installation.

## Update or remove the toolkit

Track the [Nexus page](https://www.nexusmods.com/daysgone/mods/1022) for new releases. Download the correct signed ZIP and leave it in Downloads, or select it in **Settings → Toolkit updates**. The toolkit offers **Install and restart** after checking its release signature, platform, version and file checksums. This detects a ZIP you downloaded; Local Edition does not query Nexus for a newer upload. Complete pending recovery or profile sessions before updating.

Local source builds are development copies and do not carry the official release signature. Downloaded official release ZIPs remain the supported input for the signed updater.

To move or remove the program, finish profile and recovery sessions, close it and move or delete the extracted application folder. Closing waits for active file work and releases owned browser helpers. If you close during a profile session, read the recovery choice; the game itself keeps running. This leaves installed mods, game saves and toolkit data in place. Disable or remove managed mods in the toolkit first if you want them uninstalled. Keep any backups you need before removing toolkit data separately from its configured location.
