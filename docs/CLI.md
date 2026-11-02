# Command-line guide

The CLI uses the same native package, process, ownership and recovery checks as the desktop app. It runs without opening the desktop interface.

On Windows, run `DriftersToolkitCLI.exe` from the complete extracted package. On Linux, use `./Launch-DriftersToolkit.sh --cli`. The examples below show the Windows command; substitute the Linux launcher prefix for the same commands.

```powershell
.\DriftersToolkitCLI.exe --help
.\DriftersToolkitCLI.exe --version
.\DriftersToolkitCLI.exe status
.\DriftersToolkitCLI.exe --json mods list
```

`--help` is the complete command reference for your build. No arguments also prints help. Help and version do not initialize game or account state.

## Arguments and results

Changes require literal `--yes`; there are no interactive input prompts. Commands still perform their native checks and can refuse a confirmed operation. Put `--` before a literal argument beginning with `--`. Use exact IDs returned by the corresponding list command rather than guessing from a title or filename.

`--json` returns one structured envelope containing `schemaVersion`, `command`, `ok`, `exitCode` and `data` or `error`. A confirmed action can include a redacted review. A review may also be written to stderr before a change. Redirected output and JSON stay plain; `--no-color` or `NO_COLOR` disables terminal colours and the banner.

Native `--sandbox` and `--game <installation>` options go before the command. A game selection must pass platform validation. Sandbox mode is for disposable fixtures and refuses profile game launch.

| Exit | Meaning |
| --- | --- |
| 0 | Completed |
| 2 | Invalid command or argument |
| 3 | A change needs `--yes` |
| 4 | Native refusal, failed precondition or unavailable backend |
| 5 | Diagnostics completed with findings, including warnings or unknown checks |
| 6 | The native adapter does not support the requested lifecycle |
| 130 | Profile monitoring was interrupted; read the recovery message |

Diagnostic export can succeed with exit 0 while its report contains findings. `diagnostics run` uses exit 5 for those findings.

## Packages and library

```powershell
.\DriftersToolkitCLI.exe mods list
.\DriftersToolkitCLI.exe mods external
.\DriftersToolkitCLI.exe import "Example_P.pak" --yes
.\DriftersToolkitCLI.exe import-batch "mods-folder" "another-mod.zip" --yes
.\DriftersToolkitCLI.exe enable "MOD_ID" --yes
.\DriftersToolkitCLI.exe disable "MOD_ID" --yes
.\DriftersToolkitCLI.exe remove "MOD_ID" --yes
```

Replace `MOD_ID` with an exact ID from `mods list`. Both import commands review and **store packages without enabling them**. `enable` or its alias `install` activates a stored package after checks. Disable keeps the library copy; remove deletes a disabled owned library package. Batch imports report individual failures or attention items; read the result even if some packages were added successfully.

Installed external PAKs use the exact opaque IDs from `mods external`:

| Command | Result after review |
| --- | --- |
| `adopt-external <id> --yes` | Manage an eligible direct Saved/Paks file while keeping it enabled, or store a separate copy when that is the permitted mode |
| `remove-external <id> --yes` | Remove only the exact selected, unchanged external PAK |
| `forget-installation <id> --yes` | Forget a missing or changed installed-file record; keep files, library copies, profiles and saves |
| `library backup <new.zip> --yes` | Save a private backup of library packages and metadata |
| `library restore <backup.zip> --yes` | Review and add checked backup contents without enabling mods |
| `library recover --yes` | Recover an interrupted library restore |

Nested/game-folder originals can remain active after a copy-only adoption. Review their separate removal before enabling the library copy. Base game archives stay protected. Local imports do not verify authorship or create Nexus update support.

## Profiles and recovery

```powershell
.\DriftersToolkitCLI.exe profiles list
.\DriftersToolkitCLI.exe profiles create "My setup" --yes
.\DriftersToolkitCLI.exe profiles empty "Blank PAK setup" --yes
.\DriftersToolkitCLI.exe profiles review "My setup"
.\DriftersToolkitCLI.exe profiles launch "My setup" --yes
```

`profiles capture` aliases `profiles create`. Create captures your current managed PAKs. Empty creates a separate blank profile without disabling your current collection. Rename with `profiles rename <id|name> <new-name> --yes`; delete with `profiles delete <id|name> --yes`. Ambiguous names or IDs are refused.

Launch reviews a fresh plan, starts through the validated platform launcher and keeps the terminal attached until the game exits and the original PAKs are restored. Saves remain shared; ASI/DLL plugins stay outside profiles. Keep the terminal open. Ctrl+C stops monitoring without stopping Days Gone, and uncertainty preserves the recovery record.

With the game and any pending launcher closed, use `profiles restore --yes` for a profile session needing recovery. `recover --yes` reviews an interrupted ordinary package transaction or external removal. `saves recover --yes` and `library recover --yes` address their separate workflows. Use the command named by the reported recovery state.

Status, lists and diagnostics inspect pending package journals without replaying them. Confirmed package/settings/capture commands review relevant package recovery before continuing. Inspection can still create normal private state or lock files; it is not a guarantee of zero filesystem activity.

## Saves

Start with the listing commands and use their current IDs:

```powershell
.\DriftersToolkitCLI.exe saves current
.\DriftersToolkitCLI.exe saves list
.\DriftersToolkitCLI.exe saves files
.\DriftersToolkitCLI.exe saves fields "CURRENT_SAVE_ID"
.\DriftersToolkitCLI.exe saves backup --yes
```

| Command | Purpose |
| --- | --- |
| `saves files <backup-id>` | List files in one stored backup |
| `saves import <file.sav...> --yes` | Store selected save files as a separate backup |
| `saves export-current <id> <new-file> --yes` | Export one unchanged current slot |
| `saves import-current <id> <file> --yes` | Make a safety backup and replace only that slot |
| `saves delete-current <id> --yes` | Make a safety backup and delete only that current save |
| `saves restore <backup-id> --yes` | Review, safety-backup and restore matching save files |
| `saves delete <backup-id> --yes` | Delete a stored backup, leaving current saves alone |
| `saves settings on\|off <1..30> --yes` | Set automatic backups and retained-copy count |
| `saves edit <id> <field-id> <JSON-value> --yes` | Review and safety-backup one supported typed edit |

Use field IDs from `saves fields`; do not invent names or offsets. Editing is limited to supported fields, including verified unspent skill points. `--advanced` explicitly enables supported advanced fields whose in-game effects remain unverified. Unknown or protected progress stays read-only. Save changes require the correct installation and a stopped game. The [user guide](USER_GUIDE.md#supported-save-editing) explains slot selection and the editor’s limits.

## Workshop

`workshop inspect <pak>` lists a draft; `workshop fields <pak> <asset>` shows supported property IDs. A one-field edit uses `workshop set <pak> <asset> <field> <JSON-value> <new.pak> --yes`. Values use JSON syntax and native types; shell quoting must preserve that JSON.

Save a self-contained draft with `workshop save-project <pak> <name> --yes`. List it with `workshop projects` and inspect it with `workshop resume <id>`. `workshop project-set <id> <asset> <field> <JSON-value> --yes` edits and saves it; `workshop project-export <id> <new.pak> --yes` reviews and exports a new package.

`workshop combine <new.pak> <source.pak> <source.pak>... --yes` reviews a combination for immediate export. `workshop combine-project <name> <source.pak> <source.pak>... --yes` saves it as a project, including unresolved choices. Resolve supported project families using `project-choose`, `project-compare` and `project-resolve`; `--help` gives their arguments. Unsupported layouts need an explicit complete-family choice. An unresolved draft cannot be treated as a finished export.

File operations include `new-project`, `add`, `replace`, `remove`, `extract` and `rename`, with corresponding project operations. They use native-selected asset IDs and validated virtual paths. Export uses a new output file; the original PAKs remain intact. Renaming a path does not rewrite references inside an asset. See [Workshop in the user guide](USER_GUIDE.md#work-on-a-local-package) for format and permission limits.

## Diagnostics, settings and data

```powershell
.\DriftersToolkitCLI.exe diagnostics run
.\DriftersToolkitCLI.exe diagnostics export "support-report.json" --yes
.\DriftersToolkitCLI.exe crashlogs list
.\DriftersToolkitCLI.exe settings show
.\DriftersToolkitCLI.exe directories scan
.\DriftersToolkitCLI.exe data show
.\DriftersToolkitCLI.exe game build
```

Use `crashlogs preview <id>` for a fresh redacted text preview or `crashlogs summary <id>` for a bounded crash-dump summary without raw memory. IDs come from `crashlogs list`. Review reports and previews before sharing.

`settings set <key> <value> --yes` preserves native types: booleans use `true` or `false`, scale values use numbers and enum values remain validated. For example, `settings set reducedMotion true --yes` enables reduced motion. `data move <folder> --yes` reviews and queues a data move for the next start; `data cancel --yes` cancels a queued move. Read [data locations](USER_GUIDE.md#settings-and-where-your-data-lives) before moving or removing files.

`catalog status` shows the current revision. `catalog import <zip> --yes` verifies a signed catalogue pack without enabling mods. Local Edition does not contact the Nexus API. The CLI accepts no password, API-key, authorized download-link or raw native-action arguments. Official toolkit updates are installed through the desktop workflow from a downloaded signed ZIP.
