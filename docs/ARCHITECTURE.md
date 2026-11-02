# Architecture

Drifter’s Toolkit has a shared C++17 backend, a local HTML/CSS/JavaScript interface and separate Windows and Linux native shells. The GUI and CLI use the same package, ownership, review and recovery rules. It manages files around the game; it does not inject code into Days Gone.

## Entry points and shared code

| Area | Main files | Responsibility |
| --- | --- | --- |
| Windows GUI | `src/main.cpp` | Win32/WebView2 lifetime, native dialogs, local bridge, process monitoring and platform actions |
| Linux GUI | `src/linux_main.cpp` | Qt WebEngine/WebChannel shell, native dialogs, Steam/Proton context and platform actions |
| Command line | `src/windows_cli_main.cpp`, `src/linux_cli_main.cpp`, `src/cli.cpp` | Native context, shared parser, reviewed command dispatch and terminal/JSON output |
| Interface | `ui/index.html`, `ui/app.js`, `ui/app.css` | Navigation, library views, previews, review dialogs and appearance |
| Package manager | `src/core.cpp`, `src/core.hpp` | Catalogue identities, managed ownership, library imports, scans, conflicts, settings and package transactions |
| Archive/PAK readers | `src/mod_archive.cpp`, `src/pak.cpp` | Bounded archive reads and actual Unreal package-index inspection |
| Profiles | `src/profiles.cpp`, `src/session_runtime.cpp` | Captured PAK collections, launch isolation, verified exit and restoration |
| Workshop | `src/pak_document.cpp`, `src/pak_workshop.cpp`, `src/workshop_projects.cpp` | Draft package files, supported cooked properties, combinations and durable authoring projects |
| Saves | `src/current_saves.cpp`, `src/save_backups.cpp`, `src/save_format.cpp`, `src/save_editor.cpp` | Current-file operations, reviewed backups/restores, typed inspection and supported edits |
| Diagnostics | `src/diagnostics.cpp`, `src/crash_logs.cpp`, `src/minidump.cpp` | Setup findings, redacted log previews and bounded crash summaries |
| Storage | `src/storage_location.cpp`, `src/library_backup.cpp`, `src/temp_workspace.cpp` | Verified data moves, portable library backups and disposable process workspaces |
| Updates | `src/self_update.cpp`, `src/catalog_pack.cpp` | Signed toolkit ZIPs and signed data-only catalogue packs |

Platform adapters handle filesystem, crypto, process observation, game discovery and launch. Windows uses native APIs; Linux uses POSIX helpers, Qt and validated Steam/Proton discovery. The separate save decoder is under `components/save-codec` with its own build and licence boundary.

## The interface requests; native code decides

The shipped interface is local. Windows hosts it in WebView2; Linux uses a restricted WebChannel in Qt WebEngine. Native shells validate incoming messages, bound their size and queue work away from the window thread. File/folder pickers resolve native selections rather than accepting arbitrary renderer authority.

Button availability is a convenience. Native code repeats ownership, path, identity, process and recovery checks before changing files. Reviews bind the exact action and current evidence; a changed file or stale context requires a new review. The CLI dispatches the same allowed operations and requires `--yes` for writes.

Renderer recovery can reload the interface, but it does not replay a previous mutation. Errors return a result the interface can present; a native crash can still require application restart and journal recovery.

## Packages and ownership

The library copy and installed active PAK are distinct records. Disabling a mod preserves its library copy. Finding a familiar filename or digest does not by itself grant permission to remove an external file.

External adoption and removal have separate review paths. Direct Saved/Paks adoption can retain the reviewed file as active while storing a library copy. Other locations can require copy-only import and a separately reviewed removal. Base game archives remain protected. Forgetting a stale installation removes its ownership record, not the current file.

Conflict detection compares canonical package asset paths. Unknown or unreadable indexes remain unknown, and disjoint paths do not prove gameplay compatibility. Workshop adds supported property comparison against a matching original; opaque layouts still require an explicit family choice rather than guessed merging.

## Transactions and recoverable state

Changes to active packages and current saves require a stopped, observable game and recheck relevant file evidence. Durable changes use transaction records and verified recovery copies. Recovery checks identities again; changed or unrelated files must not be overwritten to make a journal disappear. Editing a separate Workshop draft does not itself replace an active game file.

Profile launches capture independent PAK bytes, review isolation, hold displaced packages outside loading roots and restore after verified exit. Shared saves and ASI/DLL plugins are outside that isolation. Package, external-removal, profile, save-restore and library-restore workflows retain their own recovery evidence.

The CLI defers ordinary package recovery during construction, so inspection does not replay a pending transaction. An explicit recovery command or applicable confirmed mutation reviews that recovery before proceeding. The desktop can recover through its normal guarded startup flow.

## Data and subprocess boundaries

Persistent libraries, settings, profiles, backups and recovery records live in the configured toolkit data store. Windows starts from `%LOCALAPPDATA%\DriftersToolkit`; Linux starts from its XDG data root and keeps installation-specific state. A verified move can relocate the store while leaving a small location record at the default root.

Disposable archive work and partial operations use a private OS-temp workspace. Transaction backups and profile recovery stay in durable locations, including same-volume recovery areas where required; they are not ordinary scratch cleanup.

The separate save decoder reads an encoded container through stdin and returns decoded data through stdout. Native callers pin its bytes and impose isolation, time and output bounds. The helper does not choose a save path or write game saves; selected-file review, backups and replacement remain native application responsibilities. Typed save editing refuses protected or ambiguous records.

## Integrity and dependencies

Build-generated manifests bind the native application to its shipped interface, catalogue, configuration and optional helper. Public release updates additionally verify the author’s signature and exact ZIP contents. A source modification requires a consistent rebuilt application; an official signature is not a requirement for making your own local build.

Windows uses the separately serviced WebView2 Runtime. The Linux AppImage bundles its Qt/Chromium and other runtime dependencies, retaining their notices, matching sources and library-replacement instructions. Rebuilding a development executable against system Qt does not reproduce that portable runtime.

Nexus API code exists behind native platform adapters, but public **Local Edition** blocks API traffic while approval and live testing are pending. The current user workflow is manual website download followed by local import. Reports and diagnostic previews exclude common private data and require review before sharing; there is no automatic report-upload service.

See [BUILDING.md](../BUILDING.md) for build inputs, [CONTRIBUTING.md](../CONTRIBUTING.md) for change expectations and [THIRD_PARTY.md](../THIRD_PARTY.md) for component boundaries.
