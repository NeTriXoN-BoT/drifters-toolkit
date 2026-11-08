window.FIELD_GUIDE = [
  {
    id: 'pak-workshop',
    category: 'MOD FILES',
    icon: 'wrench',
    title: 'Build, edit or combine local PAKs',
    summary: 'Manage draft files, compare supported changes and save a project before exporting.',
    body: "<p>Open <strong>Workshop</strong> from the sidebar, <strong>Conflicts → Open workshop</strong> or <strong>My mods → Library tools → Open workshop</strong>. Choose local PAKs, open a saved project or choose <strong>New empty package</strong>. Work stays in a temporary draft; original downloads and installed mods are not edited.</p><p><strong>Add file…</strong> adds an ordinary file at a checked package path. Select an existing entry for <strong>Replace file…</strong>, <strong>Export file…</strong>, <strong>Rename path…</strong> or <strong>Remove from draft</strong>. A removed entry offers <strong>Restore file</strong>. <strong>Use opened bytes</strong> resets an entry to the version opened in this project. Export writes a separate new file, without overwriting an existing or protected location. Raw files are limited to 128 MiB each and the active retained-data budget is 256 MiB.</p><p>For cooked assets, renaming moves the whole asset family and its companion paths together. It does not rewrite object references inside cooked files. Use the correct game path; changing a filename alone does not make a different asset load correctly. Add all required companion files and test the resulting PAK in the game.</p><p>The combination summary separates shared, identical and different asset families. Start with <strong>Choices needed</strong> and <strong>Review next required choice</strong>; <strong>All assets</strong> keeps the full draft available. Package count is limited by the checked data and metadata budgets rather than a small fixed number.</p><p>If versions replace the same asset, <strong>Compare with original asset…</strong> compares supported property layouts against the same matching original exported from your game. More than two modded versions can be compared. Separate changes can be combined; conflicting values require your choice before <strong>Apply selected values</strong>. An opaque layout or incompatible companion data needs a whole-source-family choice. That keeps the chosen source and its companions, and can discard the other versions’ edits. The toolkit does not guess missing originals or merge arbitrary binary data.</p><p>The typed reader supports checked 8-, 16-, 32- and 64-bit numbers, float and double values, booleans, existing enum choices, vector, colour, quaternion and transform components, supported tagged DataTable and CurveTable rows, existing scalar-array items and text that keeps its encoded length. Large 64-bit integers stay exact. Open <strong>What does this do?</strong> beside a property for a short explanation of its type and context. Known weapon settings have specific descriptions; name-based clues are kept separate from confirmed behaviour. Field ranges and formats are shown. Arrays cannot be resized and new enum names cannot be invented. Invalid values stay beside the editor until corrected or reverted. Unknown layouts, unsupported compression, meshes and Blueprint instructions are not guessed or edited.</p><p><strong>Save project</strong> keeps checked source copies, file additions and replacements, removals, renamed paths, typed edits and unfinished merge choices. Moving or deleting the original downloads does not remove the saved project. <strong>Saved projects</strong> opens or deletes one, and <strong>Save as new project</strong> keeps the previous project separately. Older supported projects remain readable. Projects follow the toolkit’s configured data location.</p><p><strong>Review build</strong> checks the output before you choose where to export a separate PAK. Import it through <strong>My mods</strong> to review installation. A valid file structure is not a gameplay test. Keep combined files private unless the original authors’ permissions allow sharing them.</p>"
  },
  {
    id: 'batch-import',
    category: 'USING THE TOOLKIT',
    icon: 'plus',
    title: 'Import several mods together',
    summary: 'Review a group of downloads and add the packages you want.',
    body: '<p>Choose <strong>My mods → Import mods</strong> and select your downloads together, or use <strong>Import folder</strong>. You can also drop several supported files onto the window. The toolkit checks the packages before offering them in one review.</p><p>The review shows the source archive, package name, file size, duplicates and known overlaps. Packages already in your library are skipped. A damaged or unsupported file gets its own explanation so you can still add the others. Tick the packages you want and choose <strong>Add selected packages</strong>.</p><p><strong>Select one package to add and enable it together.</strong> Existing conflicts still open a review before any installed mod is switched off. If you select several packages, they are stored only; choose which ones to enable afterward. Importing does not merge edits or resolve conflicts automatically. The results screen lists what was added, skipped or could not be added.</p><p>Supported archive families include ZIP, 7z, RAR and TAR with common compression formats. Encryption, incomplete multipart archives, installers and unsupported compression methods are refused. The archive is checked for unsafe paths and file types; its contents are not executed. If a download cannot be read, obtain a supported complete archive or extract the PAK with a trusted archive tool and import that file.</p>'
  },
  {
    id: 'character-appearance',
    category: 'USING THE TOOLKIT',
    icon: 'grid',
    title: 'Character appearance and model previews',
    summary: 'Find character packages, view a supplied model image and review a skin switch.',
    body: '<p>Choose <strong>My mods → Character appearance</strong> or open it from <strong>Library tools</strong>. The page brings available releases and your local character library together, with Enabled and Stored only filters. You can also use the category filter on My mods. The list includes packages with supplied character information and recognised character game files. A matching name alone is not used to classify a mod.</p><p>Open <strong>Details</strong> to see the character and, where supplied, an <strong>Author model preview</strong>. Choose a supplied preview on the appearance page, or <strong>View model preview</strong> in Details, to enlarge that static reference image. Packages without a supplied picture say so instead of inventing a preview. Game lighting and animation are shown in Days Gone itself; the toolkit does not render your character in 3D.</p><p>Use the usual Enable and Disable controls. A declared full-character alternative or an actual package overlap opens a switch review showing which enabled mods would be turned off. Confirming keeps their library copies. Hats, hair and other partial appearance changes are not made mutually exclusive just because they affect the same character. Third-party packages still use the same native file and conflict checks.</p>'
  },
  {
    id: 'remove-mods',
    category: 'USING THE TOOLKIT',
    icon: 'trash',
    title: 'Remove a mod or an old library entry',
    summary: 'Remove an unwanted package, clear a missing copy or restore it from the installed PAK.',
    body: '<p>Use <strong>Remove</strong> on a mod row or open its <strong>Details</strong>. If the mod is enabled, the toolkit disables it first, then removes its library copy and saved entry. Your original download, other mods, portable profile copies and saves stay as they are.</p><p>If the library file was deleted elsewhere, <strong>Remove saved entry</strong> clears that old record without asking you to find the missing PAK. Import the original download again if you want to use the mod later. A changed installed file needs its own installation-record review, so another file is never removed using an old record.</p><p>If an enabled mod has lost its library copy, <strong>Restore library copy</strong> can rebuild it from the verified installed PAK. That leaves the installed file enabled. Download notices disappear once their checked contents match a package already in your library or mods folder; matching filenames alone are not enough.</p>'
  },
  {
    id: 'combination-review',
    category: 'USING THE TOOLKIT',
    icon: 'layers',
    title: 'Check a combination before using it',
    summary: 'Compare your selected local packages, including other authors’ mods.',
    body: '<p>Open <strong>Conflicts → Review combination</strong>. Choose packages from your library or installed mod folders. The report groups each cooked asset with its companion files, so a shared .uasset and .uexp appear as one asset family.</p><p>This is a read-only review. It does not merge, install or switch on anything. If both mods change one cooked asset, combining those edits needs an asset reader and the matching original asset; choosing one whole file can lose the other mod’s changes. Packages that cannot be read stay marked unchecked. Open <strong>Workshop</strong> to choose complete source families or work on a supported property merge in a separate draft.</p><p>No shared paths means those files do not replace one another. It does not guarantee that their gameplay changes or requirements work together. Check the authors’ notes too. Keep any combined files private unless their permissions allow sharing them.</p>'
  },
  {
    id: 'library-backup',
    category: 'USING THE TOOLKIT',
    icon: 'save',
    title: 'Back up the whole mod library',
    summary: 'Keep your actual mod versions, profiles and private notes in one ZIP.',
    body: '<p>Open <strong>My mods → Library tools → Back up library…</strong>, or <strong>Settings → Backups → Library backups</strong>. Choose where to save the ZIP. It contains the actual cached packages and copied profile packages, saved mod selections, favourites, personal notes and portable appearance preferences.</p><p><strong>This is a private backup.</strong> Keep it for yourself; personal notes are included. Account credentials, game saves, downloaded archives and machine-specific paths are excluded. Save backups remain a separate feature.</p><p>To bring a library back, choose <strong>Restore library…</strong> and select the ZIP. Review its package, profile and note counts before confirming. The toolkit checks the contents again, adds verified files to the library and leaves new mods disabled. Existing installed mods, profiles, saves and settings stay in place. Portable appearance preferences are included in the archive as reference.</p><p>Keep an additional copy on another drive if you want protection against a failed disk. Nothing is uploaded. If a restore is interrupted, use the recovery message in the toolkit before starting another restore.</p>'
  },
  {
    id: 'find-setting',
    category: 'USING THE TOOLKIT',
    icon: 'settings',
    title: 'Find a setting or fix an unsaved folder',
    summary: 'Search Settings without losing the choices you are editing.',
    body: '<p>Use the search at the top of Settings or choose a section such as <strong>Appearance</strong>, <strong>Game & mods</strong> or <strong>Backups</strong>. Filtering keeps the other controls and any unsaved changes intact.</p><p>Appearance and comfort choices save automatically. Changes that affect game files have their own Save button. If the game folder is rejected, the explanation stays beneath that field; <strong>Go to game folder</strong> takes you straight to it even when another Settings section is selected. Editing the folder does not save it—choose Save to check the new location.</p><p>A folder problem does not prevent valid theme, text-size or other loader preferences from saving.</p>'
  },
  {
    id: 'save-editor',
    category: 'USING THE TOOLKIT',
    icon: 'wrench',
    title: 'Inspect save data or edit supported values',
    summary: 'Search recorded values, edit skill points or choose advanced fields with a safety backup.',
    body: "<p>Open <strong>Saves → Current saves</strong> and select a slot. <strong>Inspect save…</strong> opens a read-only snapshot, including while Days Gone is running. Search it without changing the file; reopen the inspection after the game saves to read new data. Inspection cannot be turned into an editing session.</p><p>To make supported changes, close the inspection and Days Gone, check the game folder, then choose <strong>Edit save…</strong>. Edits always use a new checked session. You can still inspect a recognized file when it has no editable values.</p><p>The editor groups the identified values. Use <strong>Editable</strong>, <strong>Read-only</strong> or <strong>All values</strong>, then search by name, group, type or displayed value. The list shows one page at a time. Searching and filtering do not decode the file again. Read-only entries explain why they are protected. Some structures are shown as counts or summaries, while unknown bytes stay as summaries. Local strings and references appear as read-only text. Recognized credentials are masked.</p><p><strong>Unspent skill points</strong> is the normal verified edit. Use a save where Skills is already unlocked; changing points does not unlock that menu. Enter a value, wait for the draft check and choose <strong>Review changes…</strong>.</p><p><strong>Advanced fields</strong> is off until you choose it. It exposes supported typed <strong>Health / Stamina / Focus → UseEffectLevels</strong> entries and controller booleans where those records are present. Each infusion entry has a clearer label and its raw name, such as UseEffectLevels[0]. The brackets identify separate positions in a list, starting at zero. They are not duplicate fields. We have not mapped each position to a specific effect in the game, so the editor explains what is known instead of calling them current or maximum stats. Experience, level-linked progress, unknown native state, references and unsupported fields stay read-only.</p><p>Advanced changes are marked again in the review. Turning Advanced off does not throw those changes away: turn it back on to review them, or return those values to their saved values first. The toolkit does not rewrite an unknown field just because its name sounds familiar.</p><p>Read the before and after values, then choose <strong>Back up and apply changes</strong>. The complete current save set is backed up first, even when automatic backups are off. Only the selected slot is replaced. Other slots and protected progress stay unchanged. If the file format cannot be identified, editing stays unavailable.</p><p>Start the game and use <strong>Load Game</strong> to select that exact slot. Continue may choose a different save. Check any advanced changes in the game before keeping them. To undo an applied edit, use <strong>Show safety backup</strong> and review its restore with the game closed.</p><p><strong>Export save…</strong> copies one selected current save to a file you choose. <strong>Import into this slot…</strong> checks one selected file and shows which current slot will be replaced. Review it, then choose <strong>Back up and replace this slot</strong>. Shared saves may need the same game version, DLC or mods. Check their author’s notes before restoring them.</p><p><strong>Import saves as backup…</strong> stores selected files separately without replacing a current slot. Replacing or deleting a current save always needs review and a full safety backup.</p>"
  },
  {
    id: 'save-backups',
    category: 'TROUBLESHOOTING',
    icon: 'shield',
    title: 'Back up or restore your saves',
    summary: 'Keep a copy before changing your mods, and review a restore before it runs.',
    body: `<p>Open <strong>Saves in the sidebar</strong>. <strong>Back up now</strong> copies saves from the selected installation. Choose a backup card to see its date, reason, size and actions beside the gallery. Refreshing the list keeps that selection when the backup is still available.</p><p>Give a backup a name or private note, then choose <strong>Save details</strong>. These changes stay on this computer and do not change the save files. <strong>Keep this backup</strong> leaves that copy out of automatic cleanup. Unsaved details stay in the form while pictures or the list refresh.</p><p>Choose <strong>View save backups</strong> on a profile to show its linked copies. The Backups tab also has an <strong>All profiles</strong> filter. <strong>Before launching</strong> is a copy made before that profile starts, not proof that the save was played with those mods. <strong>After session</strong> is taken after the game closes and your usual mods have been restored. An unchanged copy can carry both links. Launching a profile never swaps or restores saves.</p><p><strong>Automatic backup preferences</strong> is one global option for copies before mod changes and before and after profile sessions, alongside the recent-backup limit. Automatic backups are off until you enable them. Before-change copies run before adding enabled mods or preparing a portable profile; another copy is checked after the session. Unchanged saves reuse the last matching copy. If a before-change backup fails, that change stops. An after-session failure shows a warning after your normal mods have been restored. Cleanup only removes the toolkit’s own verified, unpinned backups; changed or unfamiliar files are preserved.</p><p>The <strong>Current saves</strong> tab shows the actual game save files without making a backup first. It updates while the page is open. The in-game picture is read from the image embedded in a supported save and checked before display. No picture is downloaded or invented for that slot. Some saves contain no screenshot. Profile data and slots without a picture are labelled separately; an unreadable picture shows its reason and can be retried with Refresh. Filenames, sizes and modified times remain available. <strong>View files</strong> lists the selected backup. The file list does not guess a mission from a filename. <strong>Edit save…</strong> changes verified unspent skill points in a supported current save, after review and a full safety backup. Other progress stays protected. The Save location section shows the native path or explains why it could not be identified safely.</p><p>Use <strong>Add picture</strong> in the selected backup’s panel to choose your own PNG or JPEG. A small copy with image metadata removed is kept separately from the saves. You can change or remove the picture without changing the backup. These optional backup pictures stay on this computer and are separate from the in-game previews on the Current saves tab.</p><p>To restore, close Days Gone and choose <strong>Review restore…</strong>. The review shows the selected backup, the safety copy of your current saves and the actual file list when available. Read the warnings, then choose <strong>Restore backup</strong>. The toolkit checks the reviewed files again before writing and refuses a stale review if they changed. Other current saves, mods and profiles stay in place. If a restore was interrupted, use <strong>Review recovery</strong> first; do not remove recovery files by hand.</p><p>Backups stay on this computer. Keep a separate copy elsewhere if you need protection against a failed drive. Nothing is uploaded.</p>`
  },
  {
    id: 'library-tools',
    category: 'MODS & COMPATIBILITY',
    icon: 'layers',
    title: 'Library tools and duplicate files',
    summary: 'Compare a shared list, keep personal notes and review extra copies.',
    body: `<p>In <strong>My mods → Library tools</strong>, share your mod list or paste someone else’s list to make a checklist. The checklist shows what you have and which downloads need a look. It never installs or enables mods for you. Grouped downloads may contain several alternatives, so check the variants.</p><p>Open a mod’s Details to add a favourite or a personal note. These stay on this computer and are left out of shared lists and support reports. <strong>Recently changed</strong> shows recorded changes to your enabled collection.</p><p><strong>Diagnostics → Review duplicates</strong> checks exact file contents. Its review shows which verified copy stays and which extra copies would be removed. Unknown packages and base game files are left alone. If a file changes during cleanup, it stops and reports any removals already completed.</p><p><strong>Conflicts → Sort out conflicts</strong> walks through the current groups. Choose which mod to keep or skip a group. Every choice applies as one change; the toolkit keeps the disabled library copies available. Shared paths identify overlap, not a guarantee of how the mods behave in game.</p>`
  },
  {
    id: 'catalog-packs',
    category: 'USING THE TOOLKIT',
    icon: 'grid',
    title: 'Update the mod catalog',
    summary: 'Add new releases to the list without replacing the toolkit.',
    body: '<p>When the author publishes a <strong>catalog pack</strong> on the toolkit’s Nexus page, download its ZIP and leave it in Downloads. The toolkit offers it after checking the author’s signature. You can also use <strong>Settings → Mod catalog → Choose a catalog pack</strong>.</p><p>Review the revision and release count, then choose <strong>Update catalog</strong>. No restart is needed. A catalog pack changes the list and package metadata; it does not download, enable or remove mods. Get the actual mod files from their Nexus pages and import them normally.</p><p>Only a newer signed revision is accepted. Older package identities stay in the list so installed mods and saved profiles remain identifiable. If a pack is damaged, incompatible or changed after review, the toolkit refuses it and keeps the previous catalog.</p><p>From the command line, use <code>catalog status</code> or <code>catalog import &quot;path/to/catalog.zip&quot; --yes</code>. The command checks the same signature and retained identities as the window.</p>',
  },
  {
    id: 'command-line',
    category: 'USING THE TOOLKIT',
    icon: 'settings',
    title: 'Use the command line',
    summary: 'For advanced users: manage mods and run checks from a terminal.',
    body: `<p>The Windows and Linux downloads include a separate command-line tool. It uses the same library, saved preferences, file ownership and compatibility checks as the desktop app. It does not need a display or start a browser engine.</p><p>On Windows, open a terminal in the extracted application folder. Start with:</p><pre><code>.\\DriftersToolkitCLI.exe --help\n.\\DriftersToolkitCLI.exe mods list\n.\\DriftersToolkitCLI.exe diagnostics run</code></pre><p>On Linux, open a terminal in the extracted application folder and use <code>./Launch-DriftersToolkit.sh --cli</code>. The launcher opens the command-line program inside the AppImage without starting the graphical interface. Keep the AppImage and launcher together. Use <code>--game "/path/to/Days Gone"</code> before the command when more than one verified Steam installation is available. The loader uses that installation’s existing Proton prefix; it does not create a guessed prefix.</p><pre><code>./Launch-DriftersToolkit.sh --cli --help\n./Launch-DriftersToolkit.sh --cli --game "/path/to/Days Gone" mods list\n./Launch-DriftersToolkit.sh --cli --json diagnostics run</code></pre><p>Use the mod ID shown by <code>mods list</code> when enabling or disabling a package. Import the original PAK or a supported archive first. Every command that changes files or settings needs <code>--yes</code>; importing a package leaves it disabled. An unfamiliar local package needs inspection and explicit approval before it becomes managed. Local imports do not receive Nexus downloads or update checks.</p><p>For a supported save, use <code>saves current</code> to find its current ID, then <code>saves fields &lt;id&gt;</code> to inspect the verified fields. With the game closed, <code>saves edit &lt;id&gt; player-skill-points 1 --yes</code> reviews the change, makes a complete safety backup and applies that value to the selected slot. Other gameplay fields are read-only.</p><p><code>--json</code> returns structured output for scripts. Exit codes distinguish successful commands, invalid arguments, missing confirmation, refused actions, diagnostic findings and unsupported operations. Run <code>--help</code> for the current command list.</p><p>This CLI supports portable-profile listing, capture, launch-plan review and recovery. Starting a monitored profile session and connecting a Nexus account still use the desktop app; the command line never accepts an API key. The command-line tool will explain an unsupported action before moving any files. Closing or losing a terminal is not a safe way to interrupt file changes.</p>`
  },
  {
    id: 'getting-started',
    category: 'GETTING STARTED',
    icon: 'compass',
    title: 'Install your first mod',
    summary: 'Download a mod, add it here and switch it on.',
    body: `<p>Start with the game closed. The toolkit tries to find Days Gone automatically. If more than one valid copy is found, choose the one you launch; if none is found, browse to the folder containing <code>DaysGone.exe</code>. You can import supported packages and read the field guide while the game location is still unset.</p><ol><li>Download the mod you want from its original Nexus Mods page.</li><li>Keep the original ZIP download, or extract it into a normal folder if you prefer. Use an archive tool you trust for other formats.</li><li>Choose <strong>Import mod</strong> and select the original <code>.zip</code> or its extracted <code>.pak</code> file, or drag the file onto the window. Known releases are recognized by their audited hashes. If a ZIP holds several PAKs (tiers, optional extras), you tick the ones you want. An unfamiliar file opens a review of its actual PAK index, hash and conflicts before you can add it.</li><li>Read the mod’s notes and confirm your selection. A single chosen PAK is enabled after import when the game is closed and the setup is ready. Conflicts still need your review.</li><li>Start Days Gone and check that the change behaves as expected.</li></ol><p>The toolkit also checks your Downloads folder for new PAKs and supported archives. Choose <strong>Review &amp; import</strong> in the message at the top of the window. A single chosen PAK is enabled after import; unfamiliar files first show their own <strong>Add and enable</strong> review. Enabling still checks for conflicts. Imports with several selected packages are stored only, so alternatives are not switched on together. If the game is running, the game folder is missing or recovery is pending, the package stays in the library with a message explaining what needs attention.</p><p>Importing keeps a local copy in the toolkit’s library. <strong>Enabled</strong> counts the managed PAKs the game can load. <strong>Stored only</strong> contains every inactive local library package, including new imports and mods you switched off. They stay ready to enable again; their earlier activation history does not split them into another tab. Keeping 20 stored packages does not mean 20 extra mods are active. Enabling installs a library copy into <code>%LOCALAPPDATA%\\BendGame\\Saved\\Paks</code>. Other authors’ files can be managed without a catalog update. Their rows are labeled <strong>Added from a file</strong>; Nexus downloads and update checks are not provided for them. A file that is exactly one of the author’s releases is shown as that release. A mod you installed by hand stays under <strong>Installed outside the toolkit</strong> until you choose Manage, or the toolkit manages it automatically (Settings → Library preferences).</p><aside>Start with one mod. Once it works, add the next one. It makes any problem much easier to track down.</aside>`
  },
  {
    id: 'compatibility',
    category: 'MODS & COMPATIBILITY',
    icon: 'shield',
    title: 'Will these mods work together?',
    summary: 'What a conflict means, and what the check can and cannot tell you.',
    body: `<p>Two mods can touch the same game asset even if they change different things. For example, a recoil mod and a damage mod may both replace the same weapon data. Their changes do not automatically combine.</p><p>The toolkit compares package asset paths when it can read both indexes. <strong>Shared asset paths mean a potential conflict.</strong> A shared path tells us where packages overlap, but it cannot prove that two edits have the same behavior.</p><p><strong>No detected overlap is not a guarantee.</strong> Script behavior, dependencies, game updates and packages that cannot be indexed can still cause problems. An unknown result stays marked as unknown.</p><p>When two mods conflict, keep one of them. On the <strong>Conflicts</strong> page, <strong>Keep this one</strong> switches off the mods that conflict with the one you choose; Undo puts them back. Renaming a PAK to change load order does not merge its edits.</p><aside>Use standalone packages based on vanilla assets. XP Randomizer and No Recoil are separate packages. Only use a compatibility version if the author explicitly publishes one for the exact combination you need.</aside>`
  },
  {
    id: 'mod-health',
    category: 'MODS & COMPATIBILITY',
    icon: 'info',
    title: 'Is an older mod still safe to use?',
    summary: 'An old upload date and a broken mod are different things.',
    body: '<p>A mod can keep working for years without needing an update. Its upload date alone cannot tell you whether it works with your game. Check which game build the author tested, any required mods or DLC, and the notes for the exact file you downloaded.</p><p><strong>Update available</strong> means a newer identified release exists. It does not mean the installed version is broken. A warning from the author about a particular game version matters more than the age of the upload. Read that warning on the original mod page; a comment about another setup is a lead to investigate, not a verdict on yours.</p><p>The toolkit checks readable PAKs for overlapping assets, including other authors’ installed mods. It cannot prove gameplay compatibility or identify an unfamiliar file’s version from its name. Local imports keep their own package review; they do not become verified author releases or receive Nexus update checks.</p><p>If a problem started after a game update, keep a save backup, test the same situation with the suspected mod disabled and use <strong>Diagnostics → Find the bad mod</strong> to narrow down the cause. Note the game build, mod version and result when reporting it.</p>'
  },
  {
    id: 'faster-test-launch',
    category: 'TROUBLESHOOTING',
    icon: 'play',
    title: 'Get back into a mod test faster',
    summary: 'Keep a repeatable test save and use the game’s own startup behaviour.',
    body: '<p>Days Gone’s PC patch 1.12 added automatic skipping of the studio intro videos when a main-menu save exists. That behaviour belongs to the game; older builds, including the Classic branch, may behave differently.</p><p>The toolkit currently starts the game normally. There is no verified option here that bypasses the menu and loads the latest save. The newest file on disk is not necessarily the save you want to test, so the toolkit does not choose a save or send menu inputs for you.</p><p>For a repeatable test, keep a manual save just before the problem, back it up and load that same save each round. Use <strong>Diagnostics → Find the bad mod</strong> for the mod selections, then launch the game and repeat the same steps. Let Days Gone close fully before changing packages. This keeps the comparison useful when a crash depends on a mission, location or action.</p>'
  },
  {
    id: 'not-working',
    category: 'TROUBLESHOOTING',
    icon: 'wrench',
    title: 'My mod isn’t doing anything',
    summary: 'Check the common causes without changing your saves.',
    body: `<ol><li><strong>Close and restart the game.</strong> Days Gone reads its packages at startup; a running session will not reload your changes.</li><li><strong>Check the game location.</strong> Settings must point to the copy you actually launch. It is easy to select an old Steam or Epic installation.</li><li><strong>Check the installation folder.</strong> This toolkit installs PAK mods in <code>%LOCALAPPDATA%\\BendGame\\Saved\\Paks</code>. A ZIP file sitting there is not an installed mod. Files placed in other directories may not be included in the local scan.</li><li><strong>Read the mod’s requirements.</strong> Some changes only show up with a particular weapon, difficulty, save state or action.</li><li><strong>Run diagnostics.</strong> The report checks folders, your mod files, conflicts and whether mods can load. If mod loading is not set up yet, choose <strong>Set up mod loading</strong>.</li><li><strong>Try the mod on its own.</strong> Disable your other managed mods and test again. Review any packages installed manually as well.</li></ol><p>Use <strong>Saves in the sidebar</strong> before experimenting with gameplay mods. Automatic backups are optional. Restoring a save is a separate reviewed action with the game closed.</p>`
  },
  {
    id: 'external-mods',
    category: 'MODS & COMPATIBILITY',
    icon: 'folder',
    title: 'I already have mods installed',
    summary: 'What happens to PAKs you installed before using the toolkit.',
    body: `<p>The scanner looks for PAK files in <code>%LOCALAPPDATA%\\BendGame\\Saved\\Paks</code>. Files it did not install are listed under <strong>Installed outside the toolkit</strong> and marked <strong>Installed by hand</strong>. They are still included in the conflict check when they can be read.</p><p>You can also import the original PAK or ZIP into the library. Review the inspected file and its conflicts, then confirm. A single chosen package can be added and enabled together. Conflicts still need your review. You can choose <strong>Add to my library</strong> instead if you only want to store it.</p><p>Do not keep two copies of the same mod enabled in different folders. The installed external-file list offers guarded actions only when the file is eligible. Choose <strong>Manage</strong> on an installed PAK to let the toolkit take it over, or <strong>Manage all</strong> to take over every eligible file in Saved/Paks with one confirmation. To do that without asking for every new file, turn on <strong>Manage hand-installed PAKs automatically</strong> in Settings → Library preferences. It is on by default; switch it off if another tool looks after those files, because the toolkit can then disable or remove them like its own mods. <strong>Stop managing all</strong> (below your mod list) does the opposite: the files stay installed and become outside files again, and the toolkit deletes only its own library copies. A directly installed Saved/Paks file can become managed in place after you approve its exact identity; it stays enabled, and Disable can then remove its verified installed copy. For a nested file or a file in the game’s Content/Paks folder, confirmation adds a disabled library copy and leaves the original active. Review that original’s removal separately before enabling the managed copy. <strong>Delete</strong> asks first and shows the exact file under File details; keep your own copy if you may want it again. The native application rechecks the same file before it removes anything. A changed file or a running game stops the action.</p><aside>Removing a managed mod deletes its toolkit-owned installed files and cached library copy after confirmation. Unrelated mods and saves are left alone.</aside>`
  },
  {
    id: 'profiles',
    category: 'USING THE TOOLKIT',
    icon: 'layers',
    title: 'Keep a complete setup for each playthrough',
    summary: 'Save a set of mods as a profile and play with it any time.',
    body: `<p>A profile stores its own verified copies of the managed PAKs you have enabled (choose <strong>Save current mods</strong>), together with a manifest that uses relative paths. Profiles are listed one per row; with more than four you can search them by name or by a mod inside, and sort them. <strong>Show mods</strong> lists a profile’s files. It does not depend on the current library keeping those packages. Keep the complete profile folder together when copying it to another toolkit installation. The destination must have approved the exact same package identities before it can launch the copied profile; copying the folder alone does not grant that approval.</p><p>Choose <strong>Launch</strong> to see which of your mods are set aside and which profile mods are used, before anything changes. The plan includes affected files installed outside the toolkit. Read every blocker and warning; a profile cannot start when the toolkit cannot safely isolate the requested package set.</p><p>After you approve the plan, the toolkit temporarily sets the listed original packages aside, copies the profile’s packages into place and asks the selected game to launch. The originals are restored after the game closes. <strong>Keep the toolkit running until restoration finishes.</strong> If it was interrupted, reopen it and check the session status before changing files.</p><p>A profile does not merge overlapping assets, include saves or make an unsupported mod compatible. Package and settings changes are paused during an active session. This is file management around a game session; it does not inject code or change the game executable.</p><p><strong>Older saved selections</strong> are lists of enabled IDs. They are labeled separately because they contain no package snapshots. Apply a selection, then choose <strong>Save current mods</strong> to make an independent copy of that setup.</p>`
  },
  {
    id: 'antivirus',
    category: 'TROUBLESHOOTING',
    icon: 'lock',
    title: 'Windows or my antivirus flagged a file',
    summary: 'A clean scan helps, but no application can promise zero warnings.',
    body: `<p>Download the toolkit and mods from the author’s original release page. The toolkit does not need code injection, a kernel driver, or an antivirus exclusion to install ordinary PAK files.</p><p>A fresh unsigned Windows program can trigger a reputation warning even when a malware scan finds nothing. Antivirus vendors and Windows reputation systems make their own decisions, and those decisions can change.</p><p>If a download is flagged, pause and verify the exact release and publisher. Check the Nexus scan result and any published file hashes. Report the detection name and file hash to the author and the antivirus vendor so they can investigate.</p><aside>Do not disable your antivirus or add a broad game-folder exclusion just to make a warning disappear.</aside>`
  },
  {
    id: 'uninstall',
    category: 'USING THE TOOLKIT',
    icon: 'trash',
    title: 'Disable, remove or start over',
    summary: 'Choose the right action without losing the rest of your setup.',
    body: `<p><strong>Disable</strong> removes the toolkit’s installed copy from the game and keeps the imported package in your library, ready to enable again.</p><p><strong>Remove</strong> removes the managed installation and the toolkit’s cached copy. You will be asked to confirm. It leaves your original download, unrelated installed mods and game saves alone.</p><p>To stop using the toolkit, disable all managed mods first. You can then remove the application and its local library. Files you installed by hand stay where they are unless you choose Manage. You can delete an eligible hand-installed PAK from My mods; it is a separate exact-file action, not part of removing a managed library entry.</p><p>For a normal local update, close Days Gone and disable the old managed version before importing and reviewing the replacement. Local files do not receive Nexus update checks; keep the original download or source build yourself.</p><p>If you delete or replace a managed PAK yourself, the toolkit notices it on its own: a deleted mod moves to Stored only and keeps its library copy, and a replaced file moves to Installed outside the toolkit. No file is deleted or moved for this. If that cannot happen automatically, open the mod’s details and choose <strong>Review installation record</strong>. Compare the old recorded hash with the current file or missing-path evidence. <strong>Forget old installation record</strong> clears only the old ownership metadata; it does not delete, move or replace a PAK, remove the cached copy, or change portable profiles and saves.</p><p>After forgetting, a replacement PAK stays under Installed outside the toolkit and is still checked for conflicts. Use <strong>Manage</strong> on that file under Installed outside the toolkit to approve that exact replacement separately. A nested or game-folder file still uses copy-only review, followed by separate reviewed removal before enabling the library copy. An unchanged managed installation uses <strong>Disable</strong> instead; its valid record cannot be forgotten.</p>`
  },
  {
    id: 'seasonal-relics',
    category: 'USING THE TOOLKIT',
    icon: 'moon',
    title: 'Seasonal themes and falling emoji',
    summary: 'Seasonal lighting, quiet corner details and optional emoji effects.',
    body: `<p>Seasonal themes follow your local date by default. Spring adds soft green morning light, summer uses warm sunlight, autumn brings copper tones and leaf details, and winter uses cool light and frost. Late October has a warmer dusk palette with small corner webs. During December and the first days of January, the winter details also include muted holiday lights. The small badge in the top-right corner names the active theme, for example “Autumn · Copper Cascades” or “Winter · Frost on the Trail”.</p><p>A few seasonal emoji can drift across the window. They use bundled emoji artwork, so the effect does not need an online service or depend on a Linux emoji font being installed. The overlay ignores mouse input and stays faint.</p><p>Open <strong>Settings → Atmosphere</strong> to turn <strong>Falling seasonal emoji</strong> off while keeping the seasonal colors and corner details. Turning off <strong>Seasonal themes</strong>, or choosing <strong>Original</strong>, restores the normal appearance and removes the decorations.</p><p><strong>Reduce motion</strong> removes the moving emoji completely; your system reduced-motion preference is also respected. Static seasonal details remain. Animation pauses while the window is minimized or its page is hidden.</p>`
  },
  {
    id: 'browser-downloads',
    edition: 'nexus',
    category: 'GETTING STARTED',
    icon: 'download',
    title: 'Download from the Nexus website',
    summary: 'Optional browser integration and the authorized-link fallback.',
    body: `<p>Connect your Nexus account first. Choose <strong>Connect</strong> in the sidebar or Settings. Once the toolkit is registered with Nexus, this opens the official website and your password stays in your browser. Until then the window marks browser sign-in unavailable and offers <strong>Use my personal API key instead</strong>, an advanced option for testing and personal use under the Nexus API acceptable use policy: create the key on the Nexus API keys page, paste it once, and the toolkit checks it with Nexus, keeps it in your operating system’s credential store and never shows it again. Premium accounts can request downloads directly. Free accounts need Nexus’s website authorization for each download.</p><p>You can enable <strong>Browser download integration</strong> in Settings. Read the confirmation: this registers the toolkit to receive Nexus <strong>Mod Manager Download</strong> links on your user account. Your browser may ask before opening the application.</p><p>The option is offered only when another mod manager does not already own these links. The toolkit does not replace Vortex or another handler. You can disable the integration later without removing your downloaded or imported mods.</p><p>If a handler is already in use, keep it and choose <strong>Use Nexus link</strong> in Downloads. Choose a known mod when applicable, or let the native application detect a new release; paste the temporary authorized <code>nxm://</code> link and submit it. The field clears immediately; the native application checks the account, file and package before proceeding.</p><aside>A received download still needs package verification. New official releases appear in the live inventory without a loader update. A native inspection shows the actual PAK identity, asset paths and overlaps; review it before adding it to the library. It is not enabled automatically.</aside>`
  },
  {
    id: 'directories',
    category: 'GETTING STARTED',
    icon: 'folder',
    title: 'Find the right folders',
    summary: 'Find your game and mods folders and set up mod loading.',
    body: `<p>Open <strong>Folders</strong> and choose <strong>Scan locations</strong>. The toolkit checks known local game registrations and shows the actual paths it finds. If it finds more than one Days Gone installation, choose the one you launch. You can also browse manually; the executable is checked before the location is accepted.</p><p>Managed PAK files always go into <code>%LOCALAPPDATA%\\BendGame\\Saved\\Paks</code>. The library stores imported packages, and the downloads folder holds new Nexus files before verification. Open any existing known location directly from its card. The cards also show package counts and available disk space when those checks succeed.</p><p><strong>Set up mod loading</strong> is offered only when the startup file can be changed safely. Read the confirmation: it shows the exact original <code>startuppackages.pak</code> and the preserved <code>.drifters-disabled</code> backup. The operation disables this one cache package by renaming it. Close Days Gone first.</p><p>Some mission-map mods need a separate manual folder procedure. Follow their instructions: this operation does not rename the entire <code>sfpaks</code> folder. If you set up mod loading by hand earlier (for example by renaming <code>sfpaks</code> to <code>sfpakss</code>), Folders shows <strong>Mods can load</strong> and the toolkit leaves that setup as it is.</p><p><strong>Restore original setup</strong> is available only for a backup the toolkit owns and can verify. Save folders, unrelated packages and duplicate PAK files are shown for review; they are not moved or cleaned up automatically.</p><aside>The toolkit listens for local package-folder changes and refreshes its package checks after a short pause. Two mods changing the same game file is a conflict; no conflict found cannot prove that every gameplay interaction is compatible.</aside>`
  },
  {
    id: 'display-scaling',
    category: 'USING THE TOOLKIT',
    icon: 'settings',
    title: 'Make the toolkit comfortable on your display',
    summary: 'Automatic sizing for your screen, text size and manual controls.',
    body: `<p><strong>Automatic interface size</strong> is on by default. The toolkit reads the monitor containing its window and accounts for the display scaling already applied by Windows or Linux. Controls and text grow together, including when the window is not maximized. Moving to another monitor updates the size.</p><p>At 100% system scaling, a 1080p monitor uses 100%, a 1440p monitor uses about 135%, and a 4K monitor uses up to 200%. If the system already scales a 4K display to 200%, the toolkit adds no extra enlargement. A very small window limits the size so controls remain reachable; the page can still scroll.</p><p>Open <strong>Settings → Comfort & accessibility</strong> to turn Auto off or choose a <strong>Manual interface size</strong> from 80% to 200%. Your previous manual choice is kept while Auto is on. Selecting a manual value turns Auto off; save the preview to keep it.</p><p><strong>Ctrl + +</strong> and <strong>Ctrl + −</strong> select the next manual size. <strong>Ctrl + 0</strong> selects manual 100%. These shortcuts turn Auto off and save the change immediately.</p><p><strong>Text size</strong> is an additional preference from 90% to 150%. Automatic interface sizing already scales all text; use this preference for a little more or less. The status line shows the size currently applied. Higher contrast makes text, boundaries and scroll thumbs clearer. Scrollbars stay available while scrolling or navigating with the keyboard.</p><p>These settings change the toolkit only. They do not change system display scaling or the game’s resolution. Larger layouts wrap into fewer columns and keep page scrolling available.</p>`
  },
  {
    id: 'launcher-path',
    category: 'TROUBLESHOOTING',
    icon: 'folder',
    title: 'The launcher opens a different copy of the game',
    summary: 'Match the selected installation to Steam, Epic or your actual shortcut.',
    body: `<p>If you have moved a library or kept an older installation, the first folder found may not be the copy your launcher starts. Open <strong>Folders</strong>, scan locations and compare the displayed path with the installation location shown by your launcher.</p><p>Choose <strong>Use this installation</strong> for the correct copy, or browse to its main folder. The toolkit checks for <code>DaysGone.exe</code> before accepting the path. The selected game path is also the one used for the startup-cache check and the Launch game button.</p><p>Managed packages use the current Windows account’s <code>%LOCALAPPDATA%\\BendGame\\Saved\\Paks</code> folder. Putting a ZIP in the game’s large base-archive folder does not install it. Do not replace <code>BendGame-WindowsNoEditor.pak</code>.</p><p>If another Windows account launches the game, it has a different Local AppData location. Check the actual path in the running account rather than copying someone else’s absolute path.</p>`
  },
  {
    id: 'game-update-cache',
    category: 'TROUBLESHOOTING',
    icon: 'refresh',
    title: 'Mods stopped loading after a game update',
    summary: 'Set up mod loading again and check your mods after a game update.',
    body: `<p>A game update or a launcher’s file-verification pass can restore original game files. Close Days Gone, open <strong>Folders</strong> and run <strong>Scan locations</strong>. Read the current startup-package status instead of assuming the previous preparation still applies.</p><p>If <strong>Set up mod loading</strong> is offered, choose it again; its file paths are under File paths. The toolkit only disables the single startup cache package by renaming it and preserves the original contents. It will not adopt an unknown backup or overwrite a restored file just because the names look familiar.</p><p>If Folders shows <strong>Needs a look</strong> or <strong>Startup file not found</strong>, stop and read the message. Do not rename multiple folders until the author’s instructions identify the required setup. Mission-map mods can need a separate manual folder procedure; the toolkit’s single-package operation does not claim to perform that procedure.</p><p>Then run a package scan. A previously working mod can need an update after the game changes. A matching PAK hash identifies a release; it does not prove that release supports every newer game build.</p>`
  },
  {
    id: 'known-overlaps',
    category: 'MODS & COMPATIBILITY',
    icon: 'crosshair',
    title: 'Why do weapon mods conflict when they change different things?',
    summary: 'Real examples of mods that change the same game files.',
    body: `<p>A cooked weapon asset contains more than one setting. A recoil mod and an ammo mod can both replace the entire file, even though their names describe different features.</p><table><thead><tr><th>Packages</th><th>Shared assets in this catalog</th></tr></thead><tbody><tr><td>No Recoil + either Infinite Ammo variant</td><td>89 per pair</td></tr><tr><td>No Recoil + Hidden Arsenal</td><td>17</td></tr><tr><td>Infinite Ammo With Reload + No Reload</td><td>93; choose one</td></tr></tbody></table><p>Those counts describe the exact audited artifacts bundled with this toolkit. A different download or future release needs its own check. A mod can appear partly functional while another package has replaced some of its behavior.</p><p>XP Randomizer has no direct asset overlap with No Recoil in this catalog, and the two remain separate standalone packages. XP Randomizer does share the player-controller asset with Deacon Size Shifter, Teleport To Map Marker, No Enemy Detection Toggle and Noclip Toggle. Input features and reward features can therefore conflict too.</p><aside>The toolkit does not force a load order, copy edits from one mod into another or manufacture a compatibility package. Choose one conflicting package unless the author explicitly supplies the exact supported combination.</aside>`
  },
  {
    id: 'variants',
    category: 'MODS & COMPATIBILITY',
    icon: 'layers',
    title: 'Which version should I choose?',
    summary: 'Some mods come in versions to pick from; use one of them.',
    body: `<p>Some downloads are different choices for the same feature. Select one <strong>Longer Days</strong> duration, one <strong>Infinite Ammo</strong> mode, one <strong>enemy-detection</strong> mode, one <strong>melee-durability</strong> option and one <strong>More Normal Freakers</strong> tier. The catalog marks these as variant groups, and the ZIP picker marks them “pick one”.</p><p><strong>With Reload</strong> keeps magazine and reload timing while providing reserves. <strong>No Reload</strong> removes the need to reload. Installing both does not combine them. Likewise, several Longer Days choices can share an installed filename while containing different map data.</p><p>Read the variant’s actual description before importing it. The toolkit identifies packages by their audited contents rather than guessing from a renamed ZIP. Switching versions must pass the same ownership and conflict checks as enabling any other mod.</p><p>An unavailable first-person beta or an unaudited new download is still unknown. An empty asset list is not a clean compatibility result.</p>`
  },
  {
    id: 'arsenal-notes',
    category: 'TROUBLESHOOTING',
    icon: 'target',
    title: 'A Hidden Arsenal weapon behaves differently than expected',
    summary: 'Separate an installation problem from a feature that still needs gameplay testing.',
    body: `<p>After enabling Hidden Arsenal, open a camp weapon locker to check the added weapons and apply the release’s ammo-storage setup. Compare the exact weapon and aiming mode with the mod’s own notes.</p><p>The release notes describe the tranquilizer’s intended 15-second sleep as unreliable and unfinished. A correctly installed package does not establish that the sedation behavior is fixed. Report the target type, visible effect and wake-up behavior rather than assuming a displayed timer proves it works.</p><p>For Heavy Flamethrower direction, explosive-projectile visuals or carried-heavy movement issues, report the exact weapon, package version and other active weapon mods. A structural package check does not prove that the in-game behavior is correct.</p><p>The current catalog still checks actual shared files. Historical notes mentioning an ammo/arsenal load-order combination do not override a detected overlap or authorize the toolkit to merge packages.</p>`
  },
  {
    id: 'other-mod-pages',
    category: 'MODS & COMPATIBILITY',
    icon: 'book',
    title: 'What about mods from other pages?',
    summary: 'Mods outside the built-in list are added from a file and checked the same way.',
    body: `<p>The built-in catalog covers checked releases from the author’s Nexus pages, including Skill Respec, Art The Clown and the published combined packages. My mods shows the current entries and their availability. Toolkit updates and reviewed signed catalog packs can add releases. Exact checked hashes identify a known release; a different file is reviewed as a local package and checked for conflicts.</p><p>Interaction mods such as <strong>Instant Interaction</strong> change long story actions. Keep a save from before a story mission when you first play with one.</p><p>Check NERO crate opening and long story interactions separately when testing a release. Crate opening, taking an injector, choosing an upgrade and using it are distinct stages. Faster animations do not prove that all of those steps complete correctly.</p><p>If an interaction stalls, stop testing the same sequence repeatedly on your only save. Record the mission, checkpoint, exact interaction and installed build; keep a backup and compare with an earlier checkpoint under the author’s guidance. The toolkit does not claim a gameplay fix for these mods.</p><aside>Files installed by hand are included in the conflict check when they can be read. A file that cannot be read stays “not checked”. Conflict scanning does not merge anything automatically. Open Workshop to inspect local packages, choose complete asset families or prepare a supported property merge in a separate PAK.</aside>`
  },
  {
    id: 'diagnostics',
    category: 'TROUBLESHOOTING',
    icon: 'wrench',
    title: 'Use diagnostics to find a practical next step',
    summary: 'Run the checks, read the results and apply a fix.',
    body: `<p>Open <strong>Diagnostics</strong> and choose <strong>Run diagnostics</strong>. The four counters match the results below them: <strong>Need attention</strong> (red), <strong>Worth a look</strong> (amber), <strong>Passed</strong> (green) and <strong>Not checked</strong> (blue). What needs a look comes first; passed checks are folded away. Open <strong>Details</strong> for the actual files involved.</p><p><strong>Not checked</strong> is a separate result. An unreadable package is not a passed compatibility check, and a clean asset comparison cannot prove that every mission or script interaction works.</p><p>Where a guarded repair is available, choose its review button and read the proposal before confirming. A conflict repair asks <strong>which managed mod you want to disable</strong>; the toolkit does not pick one automatically. Startup and recovery repairs check the files again and stop if the reviewed state has changed.</p><p>If a folder changes after a report, the report is marked stale and repair controls wait for fresh checks. Critical file-action errors stay in a dialog with Diagnostics and Help actions. New actionable package issues can also appear once, with additional notices queued while you are editing settings or handling another dialog.</p><p>Use <strong>Export findings</strong> when reporting a problem, and review the result before sharing. On-screen evidence shows the actual paths. Exports remove personal paths; review the report before sharing. Diagnostics do not run an antivirus scan, merge mods, or establish full gameplay compatibility.</p>`
  },
  {
    id: 'linux-proton',
    category: 'GETTING STARTED',
    icon: 'folder',
    title: 'Use the toolkit on Linux or Steam Deck',
    summary: 'Pick your Steam library and start the game once through Steam first.',
    body: `<p>The Linux build manages the Windows game’s PAK files through its Steam Proton prefix. It looks for the original Days Gone Steam app, <strong>1259420</strong>, in native Steam and Flatpak Steam libraries.</p><ol><li>Install Days Gone through Steam and launch it once so Proton creates its Windows user folders.</li><li>Open the toolkit and check <strong>Folders</strong>. If more than one installation is found, select the one your Steam library launches.</li><li>Check the <strong>Managed mod PAKs</strong> card. It should point inside that library’s <code>steamapps/compatdata/1259420/pfx</code> prefix, ending in <code>AppData/Local/BendGame/Saved/Paks</code>.</li><li>Import a supported package, review any overlaps, then enable it with the game closed.</li></ol><p>The toolkit keeps separate ownership records and libraries for each selected installation. It does not create or replace a prefix or install a Windows PAK into an unrelated Linux home folder. Save backups and reviewed restores use the selected game’s SaveGames folder. If it cannot identify the prefix user safely, launch the game through Steam and scan again.</p><p>Portable profile launches use Steam and keep watching for the actual game process. Keep the toolkit open until restoration finishes. If Steam never starts the expected game, close any pending launch before using explicit recovery.</p><aside>Desktop Mode is the current Steam Deck target. A native build or a passing render check does not prove that every mod works under Proton. Other launchers, custom Wine prefixes and Gaming Mode integration need their own checks.</aside>`
  },
  {
    id: 'new-releases',
    edition: 'nexus',
    category: 'GETTING STARTED',
    icon: 'download',
    title: 'Install a newly uploaded mod',
    summary: 'Add a new release without waiting for a toolkit update.',
    body: `<p>Connect your Nexus account, open <strong>Downloads</strong> and choose <strong>Check releases</strong>. New PAK or ZIP uploads from any Days Gone page verified as belonging to the author appear in the release list and under <strong>New from the author</strong> in My mods.</p><p>A new file is not automatically treated as an audited catalog package. After the native application downloads it from Nexus, it reads the actual PAK index and opens a review with the package name, SHA-256, asset paths and overlaps with installed packages.</p><p>Choose <strong>Add to my library</strong> only after reviewing those details. The package is imported as a separate mod and stays disabled. Enabling it still checks ownership, changed files and conflicts.</p><p>Premium accounts can request the download directly. Free accounts complete Nexus’s website authorization, using optional browser integration or an authorized link. A ZIP with several PAKs lets you tick the ones you want.</p><p>If an installed mod has a newer release, its row shows <strong>Update available</strong> with the installed and latest versions. Notifications group the changed files instead of repeating them. No release is marked up to date when Nexus cannot verify it.</p><aside>The live inventory discovers the author’s Days Gone pages and verifies page ownership before downloads. Existing blocked combined packages remain blocked. Other authors’ files can be imported locally after inspection and your approval, without Nexus downloads or update maintenance. A local approval does not verify a publisher, and renaming a file does not give it official Nexus provenance.</aside>`
  },
  {
    id: 'reporting',
    category: 'TROUBLESHOOTING',
    icon: 'report',
    title: 'Send a useful bug report',
    summary: 'The details that help an author find the problem faster.',
    body: `<p>Open <strong>Diagnostics</strong> and review <strong>Crash logs &amp; reports</strong>. Preview a text log before selecting it. Choose <strong>Prepare support ZIP</strong> to review the exact files, then save the ZIP somewhere you can find it.</p><p>The ZIP includes fresh diagnostics, mod details and only the text logs and crash summaries you selected. Personal paths and known secret patterns are removed. Read the preview and report before sharing; nothing is uploaded automatically, and raw memory dumps are never included.</p><p>If the game crashed, open the newest dump's <strong>Crash summary</strong> and copy it into your report. Its fingerprint lets an author see whether you hit the same crash as someone else.</p><p>Describe what you expected, what happened instead, and the shortest set of steps that reproduces it. Include the game version, active profile and any exact error text. <strong>Share my mod list</strong> (My mods) copies your mods with their versions and Nexus links, without any folder paths.</p><p>For a weapon issue, include the weapon, upgrades, aiming mode and whether you tested with other weapon mods disabled. A short video can help show spread, bloom or recoil behavior.</p><p>A local package scan cannot replace an in-game test. If something behaves strangely, say what you observed rather than assuming that a green status proves compatibility.</p>`
  },
  {
    id: 'crash-summary',
    category: 'TROUBLESHOOTING',
    icon: 'alert',
    title: 'Read a crash summary',
    summary: 'What a Days Gone crash dump can tell you, and what it cannot.',
    body: `<p>When Days Gone crashes, it writes a <code>.dmp</code> file into its <code>Saved\\Logs</code> folder. Open <strong>Diagnostics</strong>, find the dump under <strong>Crash logs &amp; reports</strong> and choose <strong>Crash summary</strong>. The toolkit reads only the crash record, the list of loaded program files and the game's own crash notes. It never shows or shares the memory stored in the dump.</p><p>Every summary starts with <strong>What you can try</strong>: steps matched to the kind of crash, such as switching off recently added mods, verifying the game files, updating the graphics driver or turning off overlays. The details below explain why.</p><p><strong>Running time</strong> tells you when it happened. A crash within the first minutes usually comes from something loaded at startup, such as a newly enabled or updated mod. A crash much later usually depends on what you were doing.</p><p><strong>What happened</strong> names the kind of crash. An <em>access violation</em> means the game used memory it was not allowed to use, often an empty (null) address. A <em>fatal error reported by the game</em> means Unreal Engine stopped on purpose; its error message is shown when the game saved one. If the game asked for an impossible amount of memory, the cause is usually damaged or mismatched data, such as a broken or outdated PAK, not a lack of RAM.</p><p><strong>Where</strong> shows the program file the crash happened in. Inside <code>DaysGone.exe</code> means the game's own code; mods that change game data can trigger that, but so can the unmodded game. Inside an overlay, a graphics driver or another program points to that program instead. Proxy DLLs in the game folder, such as <code>dxgi.dll</code>, usually come from ReShade or script loaders.</p><p>The <strong>fingerprint</strong> identifies the crash location. When the same fingerprint appears several times, it is the same crash repeating. Days Gone writes two dumps for many crashes, one from Unreal and one from its crash reporter; the summary counts them as one.</p><p><strong>Mods during this crash</strong> lists the mod files that were active when that game session started, as long as Drifter&#x27;s Toolkit was open while you played. It also shows what changed since the last run without a crash. A mod that was just added or updated is the first one to check.</p><p>To narrow it down, choose <strong>Find the bad mod</strong>. It switches your mods off and on in rounds, asks whether the problem still happens and puts everything back afterwards.</p><p>The summary cannot show function names or a readable call stack, because Days Gone ships without public debug symbols. Treat it as a strong hint, and confirm the cause by testing in game.</p>`
  },
  {
    id: 'local-downloads',
    edition: 'local',
    category: 'GETTING STARTED',
    icon: 'download',
    title: 'Download mods from Nexus',
    summary: 'Download on the website, then import the file here.',
    body: `<p>Download mods in your browser. The toolkit does not need your Nexus account or an API key. Website sign-in stays in your browser.</p><p>Open the mod’s page on Nexus Mods, use <strong>Manual download</strong> on its Files tab and save the file. In the toolkit, choose <strong>Import mod</strong> and pick the PAK file or the ZIP you downloaded, or drop it onto My mods. Drop a PAK onto Workshop to open a draft for editing. Drop .sav files onto Saves to keep them as a separate backup. New downloads in your Downloads folder are also spotted and offered. The toolkit reads the package before it can be enabled: the author’s releases are recognised against the built-in list, and other authors’ PAK mods are added from a file. If a ZIP holds several PAKs, tick the ones you want.</p><p>To hear about new versions of a mod, use <strong>Track</strong> on its Nexus page. Nexus then notifies you when the author uploads a file.</p>`
  },
  {
    id: 'toolkit-updates',
    category: 'USING THE TOOLKIT',
    icon: 'download',
    title: 'Update Drifter’s Toolkit',
    summary: 'Download the new version and let the toolkit install it.',
    body: `<p>When a new version of the toolkit is published, download its ZIP from the toolkit’s Nexus page. You don’t need to extract it. The toolkit looks in your <strong>Downloads</strong> folder when it starts and when you return to its window. If it finds a newer version, a banner offers <strong>Install and restart</strong>.</p><p>You can also look yourself in <strong>Settings → Toolkit updates</strong>: <strong>Look for a downloaded update</strong> checks your Downloads folder, and <strong>Install from a file…</strong> lets you pick the ZIP anywhere.</p><p>Before anything is replaced, the toolkit checks the author’s signature and the checksum of every file in the ZIP. A ZIP that was changed, damaged or made for the other operating system is refused. During the update the toolkit closes, replaces its program files and starts again. If replacing a file fails, the previous version is put back. Your mods, profiles, settings and saves are not touched.</p><p>While the toolkit stays open, it checks Downloads for a signed update every few minutes. Quiet checks wait while a dialog or file operation is active. In builds with approved Nexus access, <strong>Notify me about new toolkit versions</strong> also checks for a published release for your operating system about every 15 minutes, without signing into an account. It waits longer if the connection fails. These checks pause while the window is hidden and resume when you return. An online notice opens the official download page; it cannot install a file. The public build keeps online checks disabled while Nexus approval is pending.</p><p>Updates install into a folder extracted from a release ZIP (on Linux, the folder with the AppImage and its launcher). Sandbox mode and copies outside such a folder are not updated in place. To get notified about new versions, use <strong>Track</strong> on the toolkit’s Nexus page. After an update, What’s new shows once. Open <strong>What’s new in this version</strong> under Settings → Toolkit updates to read those highlights again. Choose <strong>All versions</strong>, click the version number in the sidebar, or open <strong>Changelog</strong> in Settings to read older changes. The changelog is included with the toolkit and works offline.</p>`
  },
  {
    id: 'find-bad-mod',
    category: 'TROUBLESHOOTING',
    icon: 'search',
    title: 'Find the mod causing a problem',
    summary: 'Let the toolkit switch your mods in rounds until one is left.',
    body: `<p>When a crash or another problem started after you added several mods, <strong>Find the bad mod</strong> narrows it down for you. Start it from <strong>Diagnostics</strong> or from a crash summary.</p><p>Each round, the toolkit enables a different group of the mods it manages. You start Days Gone, do what usually causes the problem, close the game and answer whether it still happened. The first round switches all of them off, to check whether the problem comes from these mods at all. Every following round halves the group that is still suspected, so ten mods take about five rounds.</p><p>At the end the finder names the mod that causes the problem, says that it needs several mods together, or says that your managed mods are not the cause. You can leave the mod it found disabled and restore the available original packages, or choose <strong>Stop finder…</strong> to review your options. Missing or changed original library copies are listed and skipped. <strong>Stop without restoring</strong> clears only the finder record and keeps the current PAK files in place, so a missing download does not leave profiles and mod management blocked. The finder also survives closing the toolkit; its banner offers to continue.</p><p>The finder only switches mods that Drifter&#x27;s Toolkit manages. PAK files you placed yourself stay active, and the finder lists them when the problem remains without the managed mods. Keep Days Gone closed while mods are switched, and answer honestly even when you are not sure: a wrong answer leads to the wrong mod.</p>`
  }
];

window.FIELD_GUIDE.push({
  id: 'asi-plugin-not-loading',
  category: 'TROUBLESHOOTING',
  icon: 'wrench',
  title: 'An ASI mod does not start',
  summary: 'Check the plugin loader, game folder and matching Windows or Proton setup.',
  body: '<p>Start with the ASI mod&#x27;s own requirements. An <code>.asi</code> is a native Windows plugin; importing it as a PAK will not load it. Close Days Gone, then check the required x64 loader, its exact DLL name, the plugin&#x27;s companion files and the supported game build.</p><p>A working PAK does not confirm that its required native component loaded. Check whether the mod lists the ASI as required or as an optional extension, then check the plugin&#x27;s own log for missing files or an unsupported game build. Follow that mod&#x27;s requirements rather than installing every loader you find.</p><p>Use the folder beside the actual <code>DaysGone.exe</code> when the author requests it. For example, DayDG documents <code>BendGame/Binaries/Win64</code> with <code>version.dll</code> and its supplied files together. An existing proxy DLL may belong to another loader or ReShade. Identify it before changing anything; do not overwrite it or add several competing loaders.</p><p>On Linux, follow the plugin&#x27;s Proton/Wine instructions for that game. A <code>version.dll</code> setup may need the matching <code>WINEDLLOVERRIDES="version=n,b"</code> launch setting. A different proxy name needs its own setting; the native Linux toolkit does not load Windows ASI plugins.</p><p>PAK conflict checks and isolated PAK profiles do not inspect, switch off or sandbox these plugins. If a problem began after adding one, use its documented removal steps with the game closed and compare the result before changing unrelated PAKs.</p><p><button type="button" class="button small" data-article="modding-asi">Read the ASI loader guide</button></p>',
  resources: [{ id: 'asi-loader', label: 'Ultimate ASI Loader instructions' }]
});

// Days Gone mod authoring chapters.
window.FIELD_GUIDE.push(
  ...[
    {
      id: 'modding-start',
      chapter: 1,
      icon: 'compass',
      level: 'START HERE',
      title: 'Choose a small first change',
      summary: 'Identify one asset and one property before editing anything.',
      modding: true,
      category: 'MAKING MODS',
      body: '<p>A numeric setting or an existing true/false property is a reasonable starting point. First identify the asset, the object that owns the property, its type, and its original value. A familiar property name is a clue, not proof that the game uses it in the situation you want to change. A child Blueprint or runtime script may override its default.</p><p>Changing cooked Blueprint instructions, character skeletons, animation data, native code, or complete levels needs a separate workflow. Exporting a model or texture preview does not produce an editable replacement package. Unreal&#x27;s cooking step prepares assets for a particular runtime; a new UE5 project is not a drop-in Days Gone mod project.</p>',
      resources: [
        {
          id: 'epic-cooking',
          label: 'Epic: cooking content'
        }
      ]
    },
    {
      id: 'modding-tools',
      chapter: 2,
      icon: 'wrench',
      level: 'TOOLS & PROJECT FOLDERS',
      title: 'Set up your tools and folders',
      summary: 'Use the correct asset profile and keep source files separate from outputs.',
      modding: true,
      category: 'MAKING MODS',
      body: '<p>You need your own installed PC copy of Days Gone, an archive browser such as FModel, UAssetGUI for supported asset properties, and a packer capable of legacy PAK version 3. Download the tools from their maintainers&#x27; release pages.</p><p>The reference setup uses FModel <strong>4.4.4.0</strong> and UAssetGUI <strong>1.1.0</strong>. These are recorded working versions, not a claim that they are the newest releases. Its recorded Steam build is <strong>19221447</strong>. Assets from another game update must be checked again.</p><p>For the reference game assets, FModel uses the UE4.11 profile and UAssetGUI uses <code>VER_UE4_11</code>. Current CUE4Parse also defines a Days Gone override under UE4.11. Use the named Days Gone profile when your FModel build offers it; check parsing against your own archive.</p><p>Create a separate project folder with <code>original</code>, <code>work</code>, <code>stage</code>, and <code>out</code> subfolders. Keep original exports unchanged. Record your game build, tool versions, asset paths, and before/after values in your own notes. Back up saves before gameplay experiments; keep those backups outside the project and release ZIP.</p><p>FModel and UAssetGUI are separate authoring programs. The Linux version of this toolkit does not make those Windows authoring programs native Linux tools. Follow each tool&#x27;s own runtime/setup instructions.</p>',
      resources: [
        {
          id: 'fmodel',
          label: 'FModel project & downloads'
        },
        {
          id: 'fmodel-start',
          label: 'FModel setup'
        },
        {
          id: 'uassetgui',
          label: 'UAssetGUI project & downloads'
        },
        {
          id: 'cue4parse',
          label: 'CUE4Parse profiles'
        }
      ]
    },
    {
      id: 'modding-export',
      chapter: 3,
      icon: 'folder',
      level: 'ORIGINAL ASSETS',
      title: 'Export the raw game package',
      summary: 'Keep the original path and companion files; preview JSON is not an editable asset.',
      modding: true,
      category: 'MAKING MODS',
      body: '<p>Point FModel at your installed game&#x27;s content archives. The main archive is <code>BendGame/Content/Paks/BendGame-WindowsNoEditor.pak</code>. Browse or search for the asset you intend to investigate and export its <strong>raw package data</strong> into <code>original</code>.</p><p>Keep its exact internal path in your notes. For example, a bike movement asset in the reference build is:</p><pre><code>BendGame/Content/Vehicles/BikeHero/BillsBike.uasset</code></pre><p>Keep any companion <code>.uexp</code> or <code>.ubulk</code> files with their package. UAssetAPI specifically requires a paired <code>.uexp</code> beside the <code>.uasset</code> when one exists. FModel&#x27;s properties JSON is useful for inspection, but it is not the UAssetAPI JSON that UAssetGUI imports. Use UAssetGUI to create that editable JSON.</p><p>Start from the game&#x27;s original package, not another mod&#x27;s edited copy. The resulting mod should contain your own intended changes only.</p>',
      resources: [
        {
          id: 'fmodel-start',
          label: 'FModel setup'
        },
        {
          id: 'uassetapi-basics',
          label: 'UAssetAPI package basics'
        }
      ]
    },
    {
      id: 'modding-roundtrip',
      chapter: 4,
      icon: 'settings',
      level: 'ONE CONTROLLED EDIT',
      title: 'Check preservation, then edit one value',
      summary: 'Prove that an unedited round trip works before changing gameplay data.',
      modding: true,
      category: 'MAKING MODS',
      body: '<p>Open the raw asset with the matching version. Inspect its exports and find the exact object/property you identified. Stop if parsing fails, the expected value is absent, or the tool reports a binary-equality problem. A failed unedited round trip is a serialization problem to resolve before making gameplay changes.</p><p>The following PowerShell examples use placeholder filenames. Replace them with files in your project folder:</p><pre><code>&amp; .\\UAssetGUI.exe tojson .\\original\\Example.uasset .\\work\\original.json VER_UE4_11\n&amp; .\\UAssetGUI.exe fromjson .\\work\\original.json .\\work\\roundtrip.uasset\nGet-FileHash .\\original\\Example.uasset -Algorithm SHA256\nGet-FileHash .\\work\\roundtrip.uasset -Algorithm SHA256</code></pre><p>The unedited package should retain its bytes. Check companion files too. The UAssetAPI format supports preserving binary equality, but that does not mean every game&#x27;s custom serialization is supported.</p><p>Now make a separate copy of the JSON or asset, change one intended typed value, and save to a new output. Reopen that output and confirm the saved value. Keep object references, imports, unrelated exports, and function bytecode unchanged. If a property edit rewrites unrelated cooked scripts, use an asset-specific preservation method rather than accepting the extra changes.</p><p>The reference bike example locates <code>VehicleMoveSlowlySpeed</code> on the <code>MovementComp</code> belonging to <code>Default__BillsBike_C</code>, with an expected stock value of <code>950.0</code> on its recorded build. That shared slowdown setting can affect scripted riding as well as camps. It is an example of why an edit&#x27;s scope needs testing, not a universal offset or a camp-only switch.</p>',
      resources: [
        {
          id: 'uassetgui-commands',
          label: 'UAssetGUI command syntax'
        },
        {
          id: 'uassetapi',
          label: 'UAssetAPI serialization notes'
        }
      ]
    },
    {
      id: 'modding-package',
      chapter: 5,
      icon: 'cube',
      level: 'PAK LAYOUT',
      title: 'Build and inspect a standalone PAK',
      summary: 'Keep the original hierarchy and use version 3 with the correct mount point.',
      modding: true,
      category: 'MAKING MODS',
      body: '<p>Put only the changed runtime packages and their required companion files into <code>stage</code>, keeping the original hierarchy:</p><pre><code>stage/\n  BendGame/\n    Content/\n      Vehicles/\n        BikeHero/\n          BillsBike.uasset</code></pre><p>For this legacy package layout, use PAK <strong>version 3</strong>, an unencrypted index, and mount point <code>../../../</code>. The archive entry above must be <code>BendGame/Content/Vehicles/BikeHero/BillsBike.uasset</code>. <code>stage/</code>, your drive letter, and an extra outer mod folder must not become part of that entry. Keep source path spelling/case, including when preparing files on Linux.</p><p>One external option is <strong>repak</strong>, whose upstream documentation lists version-3 reading/writing and configurable mount points. Its CLI source confirms the following command shape. This repak command has been checked against upstream syntax; it has not been gameplay-tested. First check <code>pack --help</code> in the version you download.</p><pre><code>&amp; .\\repak.exe pack --help\n&amp; .\\repak.exe pack --version V3 --mount-point &quot;../../../&quot; .\\stage .\\out\\MyFirstMod_P.pak\n&amp; .\\repak.exe info .\\out\\MyFirstMod_P.pak\n&amp; .\\repak.exe list .\\out\\MyFirstMod_P.pak</code></pre><p>Leave compression off for the first package. Confirm version, mount point, and the complete entry list. Extract the finished PAK into another empty inspection folder and compare its payload hashes with the files you staged. A successful pack command alone does not validate the edited asset.</p><p>Use a distinct <code>_P.pak</code> filename and keep each mod standalone. Two PAKs replacing the same asset are competing complete replacements; renaming them or adjusting load order does not merge their individual property changes.</p>',
      resources: [
        {
          id: 'repak',
          label: 'repak format & usage'
        },
        {
          id: 'repak-releases',
          label: 'repak releases'
        }
      ]
    },
    {
      id: 'modding-test',
      chapter: 6,
      icon: 'play',
      level: 'PRIVATE DEVELOPMENT TEST',
      title: 'Test without replacing your normal setup',
      summary: 'Inspect your own test PAK, keep changes separate and preserve other mods.',
      modding: true,
      category: 'MAKING MODS',
      body: '<p>You can use <strong>Import mod</strong> for your own standalone test PAK or a ZIP (with several PAKs, pick one). The toolkit reads the actual package, keeps a frozen review copy, and shows the identity, asset paths and conflicts before you approve it. It is <strong>Added from a file</strong>, not a verified publisher release. Choose <strong>Add and enable</strong> to install the selected test package, or <strong>Add to my library</strong> to store it without installing it.</p><p>For a managed test, review the overlaps, enable only the intended package and keep a separate copy of your source and built PAK. Disable or remove the managed test copy when you finish. An approved local package can be included in a portable profile like another managed mod; the same overlap and ownership checks apply.</p><p>For a manual test, close the game, record your existing setup, and use the Folders page to locate the selected installation&#x27;s actual <strong>Managed mod PAKs / Saved/Paks</strong> folder. On Windows this normally belongs to <code>%LOCALAPPDATA%/BendGame/Saved/Paks</code>; on Linux use the selected game&#x27;s Proton prefix, not a guessed native Linux path.</p><p>Copy your uniquely named test PAK there only after checking that no file would be overwritten and no existing package replaces the same asset. Keep unrelated mods and saves intact. The toolkit can scan the file as an external package and report readable asset overlaps; that does not approve or adopt it into the managed library.</p><p>Use a normal game launch for an external manual test. <strong>Launching an isolated toolkit profile sets external PAKs aside</strong>, so it deliberately omits a manually copied, unmanaged test mod. To include a test package in a profile, approve its local import, enable the managed copy and capture that setup first.</p><p>Check the Folders page&#x27;s startup-cache guidance if the override never loads. Do not delete base archives or blindly rename the entire streaming folder. Existing preparation and some map-specific overrides have different requirements.</p><p>Test the intended behavior, ordinary behavior that should stay unchanged, save/load, respawn, and affected mission transitions. Then close the game, remove only your test PAK, and repeat the relevant checks without it. Record which game build and scenarios you actually tested. A static validator or a clean antivirus scan cannot establish gameplay correctness.</p>',
      resources: []
    },
    {
      id: 'modding-debug',
      chapter: 7,
      icon: 'wrench',
      level: 'TROUBLESHOOTING YOUR CHANGE',
      title: 'Find the first point that failed',
      summary: 'Separate parsing, packaging and gameplay problems instead of changing several things at once.',
      modding: true,
      category: 'MAKING MODS',
      body: '<table><thead><tr><th>Symptom</th><th>Check next</th></tr></thead><tbody><tr><td>Asset will not open</td><td>Matching engine/game profile, complete companion files, the original package, and tool support for this asset type.</td></tr><tr><td>Unedited round trip changes unexpectedly</td><td>Resolve serialization/version problems before editing; do not force-save around an error.</td></tr><tr><td>Game starts but nothing changes</td><td>PAK version/mount/path, actual Saved/Paks location, startup loading setup, duplicates, and runtime/child-class overrides.</td></tr><tr><td>A different feature changes</td><td>Shared asset scope, inherited defaults, and unrelated serialized differences.</td></tr><tr><td>Crash or mission stall</td><td>Remove only the test PAK with the game closed, confirm the baseline, and reduce the change. Keep normal progression and completion callbacks intact.</td></tr><tr><td>Toolkit refuses import</td><td>The game’s own archive names, blocked combined packages, unreadable PAKs and unsafe ZIP paths are refused. Use a distinct <code>_P.pak</code> name and a standalone package built from vanilla assets; do not edit the shipped catalog to bypass its checks.</td></tr></tbody></table>',
      resources: [
        {
          id: 'uassetgui',
          label: 'UAssetGUI project & downloads'
        },
        {
          id: 'uassetapi-basics',
          label: 'UAssetAPI package basics'
        },
        {
          id: 'repak',
          label: 'repak format & usage'
        }
      ]
    },
    {
      id: 'modding-publish',
      chapter: 8,
      icon: 'report',
      level: 'RELEASE CHECKLIST',
      title: 'Publish a small, reproducible release',
      summary: 'Ship one standalone package and explain what you actually tested.',
      modding: true,
      category: 'MAKING MODS',
      body: '<p>Package the single standalone PAK in a ZIP. Keep extracted originals, game archives, saves, credentials, tool binaries, source caches, and private machine paths out of the upload. Do not redistribute the game&#x27;s base assets as an authoring kit.</p><p>State the tested game build, precise feature changes, replaced asset paths, known overlaps, installation/removal steps, and untested cases. Separate structural checks from actual gameplay results. Keep the source recipe and original-source hashes in your private development records so you can recheck the next game update.</p><p>XP Randomizer and No Recoil remain separate standalone mods. The XP implementation modifies controller/award logic; the recoil implementation also covers actual shot spread, firing bloom, aim behavior, and reticle assets. Neither is a good first exercise in changing one float. Do not build a combined package or retain another mod&#x27;s edits without explicit compatibility work being requested. For No Recoil, preserve normal firing, ammunition use, damage, and projectile/pellet counts.</p>',
      resources: []
    },
    {
      id: 'modding-asi',
      chapter: 9,
      icon: 'wrench',
      level: 'NATIVE PLUGINS',
      title: 'ASI loaders: when a mod needs one',
      summary: 'Understand native plugins, their setup and how they differ from PAK mods.',
      modding: true,
      category: 'MAKING MODS',
      body: '<p><strong>Mods can use PAKs, ASI plugins, or both.</strong> Some Days Gone mods, including some in this catalogue, need native components for their features to work. Follow the requirements for that specific mod: a PAK file does not mean its ASI component is optional. Other mods work entirely through PAKs. Drifter&#x27;s Toolkit itself does not need an ASI loader to manage PAKs.</p><p>The <strong>ASI loader</strong> loads native plugins into the game process. The <code>.asi</code> plugin supplies the mod&#x27;s code. A loader on its own does not add that plugin&#x27;s features, and importing a PAK does not install either component.</p><h2>When a PAK is enough</h2><p>PAKs hold packaged content: data, textures, models and cooked Blueprint content, where supported by the game. They are not limited to changing existing numbers. Use a PAK when the required change can be made through the content and behavior the game exposes. What an Unreal project can do in general is not proof that the same edit is available in a shipped Days Gone asset.</p><h2>Why use native code instead?</h2><p><strong>Bleed Out</strong> is a native-plugin example. Its code checks the current shot against its own approximate vital zones, records wounds and advances bleeding during gameplay. Those zones are authored approximations, not stock anatomical organ colliders. Changing an existing head/body damage value does not add that wound system. This implementation puts the added hit and timer logic in an ASI; it does not need an accompanying PAK.</p><p>Native hooks are useful when the feature needs access to a running game system that the available asset edits do not expose. They are one implementation route, not a claim that every new feature always needs an ASI or that no other modding method could provide it.</p><h2>When a mod uses both</h2><p>A combined mod can put asset or data changes in a PAK and live behavior in a native plugin. Install both when both are required. <strong>Headshot Only Freakers</strong> is a specific example: its PAK handles full-pawn Freakers, including nearby horde members, while the optional horde ASI extends the rule to distant lightweight horde members that this PAK route does not cover. Without that optional part, those distant members can still take body-shot damage. Check each mod&#x27;s requirements separately: here the ASI adds horde coverage, while other mods need an ASI for their core feature.</p><p>The toolkit manages the PAK portion. Switching it off or changing a PAK profile leaves separately installed native plugins active. Check both parts when testing a combined setup, and keep any required companion files together.</p><h2>What the game-version check does</h2><p>The Bleed Out and Headshot native implementations check the supported executable before enabling hooks; their checks also validate the target instructions. A mismatched or unreadable executable leaves their hooks disabled. Other plugins may use different checks, so use each author&#x27;s supported-build instructions. This guard reduces wrong-build risk; it does not prove that every hook, gameplay interaction or plugin combination is bug-free. If the plugin reports an unsupported build, use a matching release rather than bypassing its check.</p><h2>Windows setup</h2><p>Use the author&#x27;s instructions and an official source such as ThirteenAG&#x27;s Ultimate ASI Loader releases. Days Gone needs the matching <strong>x64</strong> build. The proxy DLL name matters: <code>version.dll</code> is one supported x64 option, but a name that works for one setup is not a universal fix.</p><p>For a Days Gone example, <strong>DayDG by TenLus</strong> documents its loader and supplied plugin files beside <code>DaysGone.exe</code>, in <code>BendGame/Binaries/Win64</code>. It uses the x64 <code>version.dll</code> package. Keep its required companion files together and follow its current page for the complete list. This is a separate author&#x27;s tool, not part of Drifter&#x27;s Toolkit. These files go in the documented executable folder, not <code>Saved/Paks</code> or a Windows system folder.</p><p>Close the game first. If a proxy DLL is already there, identify it and keep a backup before changing anything. It may serve an existing loader, ReShade or another tool. Use that tool&#x27;s documented sharing or chaining setup; do not overwrite it, copy several loaders together or guess a replacement filename.</p><h2>Linux and Proton</h2><p>The Linux toolkit manages PAKs; it does not load Windows ASIs. An ASI plugin runs inside the Windows game through Proton or Wine, where both the loader and plugin need to work. For a setup specifically using <code>version.dll</code>, a per-game Steam launch option can prefer the supplied DLL:</p><pre><code>WINEDLLOVERRIDES="version=n,b" %command%</code></pre><p><code>n,b</code> means native Windows DLL first, then Wine&#x27;s built-in fallback. Use this only for the chosen proxy. Keep your other launch options and use the selected game&#x27;s actual prefix; do not create or change an unrelated Wine prefix. Follow the plugin&#x27;s requirements rather than assuming Windows compatibility proves Proton support.</p><h2>Making and testing an ASI</h2><p>Native plugin development uses a matching C++/Windows x64 toolchain and a loader-compatible entry point. Game addresses, hooks, calling conventions and engine interfaces can change between builds. Verify the exact target build and each hook; a successful compile does not prove it is safe. Keep your code, dependencies and licences organised, and publish only files you have permission to distribute, with their required notices. Do not bundle game executables or someone else&#x27;s plugin as your own.</p><p>Back up saves and test one native change at a time. Record the loader, plugin and game versions and test normal play, save/load and removal. <strong>ASI/DLL plugins stay outside the toolkit&#x27;s PAK profiles, conflict scanner and bad-mod finder.</strong> A clean PAK profile does not disable them. The toolkit does not install, update or remove these DLLs for you.</p>',
      resources: [
        { id: 'asi-loader', label: 'Ultimate ASI Loader instructions' },
        { id: 'asi-loader-releases', label: 'Official ASI loader releases' },
        { id: 'daydg', label: 'DayDG: Days Gone installation example' },
        { id: 'proton-launch-options', label: 'Valve: per-game Proton options' },
        { id: 'wine-dll-overrides', label: 'Wine: DLL override syntax' },
        { id: 'epic-cooking', label: 'Epic: cooked asset content' }
      ]
    }
  ]
);
