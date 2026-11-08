param([switch]$Tests, [switch]$CliOnly, [switch]$NexusReviewer, [switch]$InterfaceSmoke, [switch]$LocalEdition, [string]$OutputDirectory = 'out', [string]$SaveCodecHelper, [string]$PrivateSavePreviewHelper = '')
$ErrorActionPreference = 'Stop'
if ($NexusReviewer -and $InterfaceSmoke) { throw 'Reviewer and internal interface-test builds must be built separately.' }
# Public builds use browser downloads and never contact the Nexus API.
if ($LocalEdition -and ($NexusReviewer -or $Tests)) { throw 'Build the local edition separately from tests and reviewer builds.' }
$LocalEdition = !$NexusReviewer -and !$Tests
$projectRoot = Split-Path $PSScriptRoot -Parent
$compilerRoot = Join-Path $projectRoot 'vendor\llvm-mingw-20261006-ucrt-x86_64\bin'
$compiler = Join-Path $compilerRoot 'x86_64-w64-mingw32-clang++.exe'
$cCompiler = Join-Path $compilerRoot 'x86_64-w64-mingw32-clang.exe'
$windres = Join-Path $compilerRoot 'x86_64-w64-mingw32-windres.exe'
if (!(Test-Path -LiteralPath $compiler)) { throw 'Run python scripts/bootstrap_local.py to fetch the pinned build dependencies.' }
$out = [IO.Path]::GetFullPath((Join-Path $projectRoot $OutputDirectory))
if (!$out.StartsWith($projectRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw 'OutputDirectory must be inside the project folder.' }
New-Item -ItemType Directory -Path $out -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $projectRoot 'out') -Force | Out-Null
Push-Location $projectRoot
try {
    $previewArguments = @()
    if ($PrivateSavePreviewHelper) {
        if ($CliOnly -or !(Test-Path -LiteralPath $PrivateSavePreviewHelper -PathType Leaf)) { throw 'Private save previews require the GUI and an existing helper.' }
        Write-Warning 'PRIVATE experimental save-preview helper enabled. Do not distribute before decoder provenance is cleared.'
        $previewArguments = @('--private-save-preview-helper', [IO.Path]::GetFullPath($PrivateSavePreviewHelper))
    }
    if ($SaveCodecHelper) {
        if (!(Test-Path -LiteralPath $SaveCodecHelper -PathType Leaf)) { throw 'The save decoder executable is missing.' }
        $previewArguments += @('--save-codec-helper', [IO.Path]::GetFullPath($SaveCodecHelper))
    }
    python scripts/generate_assets_manifest.py @previewArguments
    if ($LASTEXITCODE -ne 0) { throw 'Failed to generate the app integrity manifest' }
    foreach ($name in @('miniz','miniz_tdef','miniz_tinfl','miniz_zip')) {
        & $cCompiler -O2 -DMINIZ_NO_STDIO -I vendor/miniz -c "vendor/miniz/$name.c" -o "out/$name.o"
        if ($LASTEXITCODE -ne 0) { throw "Failed to compile $name" }
    }
    $reviewerOptions = @()
    if ($NexusReviewer) { $reviewerOptions += '-DDRIFTER_NEXUS_REVIEWER' }
    if ($InterfaceSmoke) { $reviewerOptions += '-DDRIFTER_UI_TESTING' }
    if ($LocalEdition) { $reviewerOptions += '-DDRIFTER_LOCAL_EDITION' }
    & $windres @reviewerOptions src/app.rc -O coff -o out/app-res.o
    if ($LASTEXITCODE -ne 0) { throw 'Failed to compile Windows resources' }
    $common = @('-std=c++17','-O2','-DNDEBUG','-DUNICODE','-D_UNICODE','-D_WIN32_WINNT=0x0A00','-fms-extensions','-static','-Ivendor','-Isrc',"-ffile-prefix-map=$projectRoot=.")
    $common += $reviewerOptions
    $archivePrefix = $env:DRIFTER_ARCHIVE_PREFIX
    if (!$archivePrefix) { $archivePrefix = Join-Path ([IO.Path]::GetTempPath()) 'drifter-archive-static-windows-3.8.11-v5' }
    python scripts/bootstrap_archive.py --download --prefix $archivePrefix --cc $cCompiler --cxx $compiler
    if ($LASTEXITCODE -ne 0) { throw 'Pinned archive decoder build failed. Install CMake/Ninja or set DRIFTER_CMAKE and DRIFTER_NINJA to the developer tools.' }
    $common += @('-DLIBARCHIVE_STATIC','-DLZMA_API_STATIC',('-I' + (Join-Path $archivePrefix 'include')))
    $archiveLibraries = @('archive','zs','bz2','lzma','zstd','lz4') | ForEach-Object { Join-Path $archivePrefix "lib/lib$_.a" }
    $objects = @('out/miniz.o','out/miniz_tdef.o','out/miniz_tinfl.o','out/miniz_zip.o') + $archiveLibraries
    if (!$CliOnly) {
        & $compiler @common -mwindows -municode src/main.cpp src/pak_document.cpp src/pak_workshop.cpp src/workshop_projects.cpp src/save_format.cpp src/save_editor.cpp src/core.cpp src/storage_location.cpp src/mod_archive.cpp src/library_backup.cpp src/game_build.cpp src/save_backups.cpp src/current_saves.cpp src/catalog_pack.cpp src/self_update.cpp src/mod_history.cpp src/external_removal.cpp src/pak.cpp src/temp_workspace.cpp src/nexus.cpp src/nxm.cpp src/directories.cpp src/watcher.cpp src/profiles.cpp src/session_runtime.cpp src/diagnostics.cpp src/crash_logs.cpp src/minidump.cpp src/native_crash.cpp src/game_restart.cpp src/preview_images.cpp src/preview_images_windows.cpp src/save_preview_process.cpp src/toolkit_update_notice.cpp src/toolkit_update_notice_windows.cpp @objects out/app-res.o -o (Join-Path $out 'DriftersToolkit.exe') -lbcrypt -lcrypt32 -lwinhttp -lwindowscodecs -lole32 -lshell32 -lshlwapi -luuid -ldwmapi -lcomdlg32 -ladvapi32 -lktmw32 -luserenv '-Wl,--dynamicbase' '-Wl,--nxcompat' '-Wl,--high-entropy-va' '-Wl,--dependent-load-flag=0x800'
        if ($LASTEXITCODE -ne 0) { throw 'C++ application build failed' }
        Copy-Item -LiteralPath vendor/WebView2Loader.dll -Destination (Join-Path $out 'WebView2Loader.dll') -Force
        if ($PrivateSavePreviewHelper) { Copy-Item -LiteralPath $PrivateSavePreviewHelper -Destination (Join-Path $out 'DriftersToolkitSavePreview.exe') -Force }
        Copy-Item -LiteralPath ui -Destination $out -Recurse -Force
    }
    & $compiler @common -municode src/windows_cli_main.cpp src/pak_document.cpp src/pak_workshop.cpp src/workshop_projects.cpp src/save_format.cpp src/save_editor.cpp src/cli.cpp src/core.cpp src/storage_location.cpp src/mod_archive.cpp src/library_backup.cpp src/game_build.cpp src/save_backups.cpp src/current_saves.cpp src/catalog_pack.cpp src/self_update.cpp src/mod_history.cpp src/external_removal.cpp src/pak.cpp src/temp_workspace.cpp src/nexus.cpp src/directories.cpp src/profiles.cpp src/session_runtime.cpp src/diagnostics.cpp src/crash_logs.cpp src/minidump.cpp src/native_crash.cpp src/save_preview_process.cpp @objects -o (Join-Path $out 'DriftersToolkitCLI.exe') -lbcrypt -lcrypt32 -lwinhttp -lole32 -lshell32 -luuid -ladvapi32 -luserenv '-Wl,--dynamicbase' '-Wl,--nxcompat' '-Wl,--high-entropy-va' '-Wl,--dependent-load-flag=0x800'
    if ($LASTEXITCODE -ne 0) { throw 'C++ command-line build failed' }
    if ($SaveCodecHelper) { Copy-Item -LiteralPath $SaveCodecHelper -Destination (Join-Path $out 'DriftersToolkitSaveCodec.exe') -Force }
    foreach ($folder in @('catalog','config')) { Copy-Item -LiteralPath $folder -Destination $out -Recurse -Force }
    if ($Tests) {
        & $compiler @common -Wall -Wextra tests/drop_import_tests.cpp src/mod_archive.cpp @archiveLibraries -o (Join-Path $out 'drop_import_tests.exe') -lbcrypt
        if ($LASTEXITCODE -ne 0) { throw 'Native drop validation test build failed' }
        & (Join-Path $out 'drop_import_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Native drop validation tests failed' }
        & $compiler @common -Wall -Wextra tests/mod_archive_tests.cpp src/mod_archive.cpp @archiveLibraries -o (Join-Path $out 'mod_archive_tests.exe') -lbcrypt
        if ($LASTEXITCODE -ne 0) { throw 'Archive decoder test build failed' }
        & (Join-Path $out 'mod_archive_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Archive decoder tests failed' }
        & $compiler @common -DDRIFTER_CORE_TESTING tests/library_backup_tests.cpp src/library_backup.cpp src/pak.cpp src/temp_workspace.cpp @objects -o (Join-Path $out 'library_backup_tests.exe') -lbcrypt -lole32 -ladvapi32
        if ($LASTEXITCODE -ne 0) { throw 'Full library backup test build failed' }
        & (Join-Path $out 'library_backup_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Full library backup tests failed' }
        & $compiler @common -DDRIFTER_CORE_TESTING tests/import_backup_manager_tests.cpp src/profiles.cpp src/core.cpp src/storage_location.cpp src/mod_archive.cpp src/library_backup.cpp src/game_build.cpp src/save_backups.cpp src/current_saves.cpp src/catalog_pack.cpp src/self_update.cpp src/mod_history.cpp src/external_removal.cpp src/pak.cpp src/temp_workspace.cpp @objects -o (Join-Path $out 'import_backup_manager_tests.exe') -lbcrypt -lcrypt32 -lole32 -lshell32 -luuid -ladvapi32
        if ($LASTEXITCODE -ne 0) { throw 'Batch import/library backup integration test build failed' }
        & (Join-Path $out 'import_backup_manager_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Batch import/library backup integration tests failed' }
        & $compiler @common -DDRIFTER_UPDATE_TESTING tests/catalog_pack_tests.cpp src/catalog_pack.cpp src/self_update.cpp @objects -o (Join-Path $out 'catalog_pack_tests.exe') -lbcrypt
        if ($LASTEXITCODE -ne 0) { throw 'Signed catalog test build failed' }
        & (Join-Path $out 'catalog_pack_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Signed catalog tests failed' }
        & $compiler @common -DDRIFTER_CORE_TESTING -DDRIFTER_UPDATE_TESTING tests/catalog_manager_tests.cpp src/core.cpp src/storage_location.cpp src/mod_archive.cpp src/library_backup.cpp src/game_build.cpp src/save_backups.cpp src/current_saves.cpp src/catalog_pack.cpp src/self_update.cpp src/mod_history.cpp src/external_removal.cpp src/pak.cpp src/temp_workspace.cpp src/cli.cpp @objects -o (Join-Path $out 'catalog_manager_tests.exe') -lbcrypt -lcrypt32 -lole32 -lshell32 -luuid -ladvapi32
        if ($LASTEXITCODE -ne 0) { throw 'Catalog manager test build failed' }
        & (Join-Path $out 'catalog_manager_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Catalog manager tests failed' }
        & $compiler @common -DDRIFTER_CORE_TESTING tests/external_removal_tests.cpp src/external_removal.cpp -o (Join-Path $out 'external_removal_tests.exe') -lbcrypt -lole32
        if ($LASTEXITCODE -ne 0) { throw 'External removal native test build failed' }
        & (Join-Path $out 'external_removal_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'External removal native tests failed' }
        & $compiler @common -DDRIFTER_CORE_TESTING tests/external_manager_tests.cpp src/cli.cpp src/core.cpp src/storage_location.cpp src/mod_archive.cpp src/library_backup.cpp src/game_build.cpp src/save_backups.cpp src/current_saves.cpp src/catalog_pack.cpp src/self_update.cpp src/mod_history.cpp src/external_removal.cpp src/pak.cpp src/temp_workspace.cpp src/profiles.cpp @objects -o (Join-Path $out 'external_manager_tests.exe') -lbcrypt -lcrypt32 -lole32 -lshell32 -luuid -ladvapi32
        if ($LASTEXITCODE -ne 0) { throw 'External management/adoption test build failed' }
        & (Join-Path $out 'external_manager_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'External management/adoption tests failed' }
        & $compiler @common -DDRIFTER_CORE_TESTING tests/forget_installation_tests.cpp src/cli.cpp src/core.cpp src/storage_location.cpp src/mod_archive.cpp src/library_backup.cpp src/game_build.cpp src/save_backups.cpp src/current_saves.cpp src/catalog_pack.cpp src/self_update.cpp src/mod_history.cpp src/external_removal.cpp src/pak.cpp src/temp_workspace.cpp src/profiles.cpp @objects -o (Join-Path $out 'forget_installation_tests.exe') -lbcrypt -lcrypt32 -lole32 -lshell32 -luuid -ladvapi32
        if ($LASTEXITCODE -ne 0) { throw 'Forget-installation metadata test build failed' }
        & (Join-Path $out 'forget_installation_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Forget-installation metadata tests failed' }
        & $compiler @common -DDRIFTER_CORE_TESTING tests/local_import_tests.cpp src/core.cpp src/storage_location.cpp src/mod_archive.cpp src/library_backup.cpp src/game_build.cpp src/save_backups.cpp src/current_saves.cpp src/catalog_pack.cpp src/self_update.cpp src/mod_history.cpp src/external_removal.cpp src/pak.cpp src/temp_workspace.cpp src/profiles.cpp @objects -o (Join-Path $out 'local_import_tests.exe') -lbcrypt -lcrypt32 -lole32 -lshell32 -luuid -ladvapi32
        if ($LASTEXITCODE -ne 0) { throw 'Local import/profile test build failed' }
        & (Join-Path $out 'local_import_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Local import/profile tests failed' }
        & $compiler @common tests/pak_security_tests.cpp src/pak.cpp -o (Join-Path $out 'pak_security_tests.exe') -lbcrypt
        if ($LASTEXITCODE -ne 0) { throw 'PAK security test build failed' }
        & (Join-Path $out 'pak_security_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'PAK security tests failed' }
        & $compiler @common -Ivendor/miniz tests/miniz_security_tests.cpp @objects -o (Join-Path $out 'miniz_security_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'miniz security test build failed' }
        & (Join-Path $out 'miniz_security_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'miniz security tests failed' }
        & $compiler @common tests/download_watch_tests.cpp src/pak.cpp src/mod_archive.cpp @objects -o (Join-Path $out 'download_watch_tests.exe') -lbcrypt -lole32
        if ($LASTEXITCODE -ne 0) { throw 'Download watcher test build failed' }
        & (Join-Path $out 'download_watch_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Download watcher tests failed' }
        if (!$CliOnly) {
            & $compiler @common tests/preview_images_tests.cpp src/preview_images.cpp src/preview_images_windows.cpp -o (Join-Path $out 'preview_images_tests.exe') -lwinhttp -lwindowscodecs -lole32 -luuid
            if ($LASTEXITCODE -ne 0) { throw 'Preview image test build failed' }
            & (Join-Path $out 'preview_images_tests.exe')
            if ($LASTEXITCODE -ne 0) { throw 'Preview image tests failed' }
            & $compiler @common tests/author_avatar_tests.cpp src/preview_images.cpp src/preview_images_windows.cpp -o (Join-Path $out 'author_avatar_tests.exe') -lwinhttp -lwindowscodecs -lole32 -luuid
            if ($LASTEXITCODE -ne 0) { throw 'Author avatar test build failed' }
            & (Join-Path $out 'author_avatar_tests.exe')
            if ($LASTEXITCODE -ne 0) { throw 'Author avatar tests failed' }
        }
        & $compiler @common tests/windows_display_tests.cpp -o (Join-Path $out 'windows_display_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Windows display test build failed' }
        & (Join-Path $out 'windows_display_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Windows display tests failed' }
        & $compiler @common -DDRIFTER_CORE_TESTING tests/cli_tests.cpp src/cli.cpp src/core.cpp src/storage_location.cpp src/mod_archive.cpp src/library_backup.cpp src/game_build.cpp src/save_backups.cpp src/current_saves.cpp src/catalog_pack.cpp src/self_update.cpp src/mod_history.cpp src/external_removal.cpp src/pak.cpp src/temp_workspace.cpp @objects -o (Join-Path $out 'cli_tests.exe') -lbcrypt -lcrypt32 -lole32 -lshell32 -luuid -ladvapi32
        if ($LASTEXITCODE -ne 0) { throw 'Command-line test build failed' }
        & (Join-Path $out 'cli_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Command-line tests failed' }
        & $compiler @common -DDRIFTER_CORE_TESTING tests/preferences_tests.cpp src/cli.cpp src/core.cpp src/storage_location.cpp src/mod_archive.cpp src/library_backup.cpp src/game_build.cpp src/save_backups.cpp src/current_saves.cpp src/catalog_pack.cpp src/self_update.cpp src/mod_history.cpp src/external_removal.cpp src/pak.cpp src/temp_workspace.cpp @objects -o (Join-Path $out 'preferences_tests.exe') -lbcrypt -lcrypt32 -lole32 -lshell32 -luuid -ladvapi32
        if ($LASTEXITCODE -ne 0) { throw 'Independent preference test build failed' }
        & (Join-Path $out 'preferences_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Independent preference tests failed' }
        & $compiler @common -DDRIFTER_CORE_TESTING tests/core_tests.cpp src/core.cpp src/storage_location.cpp src/mod_archive.cpp src/library_backup.cpp src/game_build.cpp src/save_backups.cpp src/current_saves.cpp src/catalog_pack.cpp src/self_update.cpp src/mod_history.cpp src/external_removal.cpp src/pak.cpp src/temp_workspace.cpp @objects -o (Join-Path $out 'core_tests.exe') -lbcrypt -lcrypt32 -lole32 -lshell32 -luuid -ladvapi32
        if ($LASTEXITCODE -ne 0) { throw 'Core test build failed' }
        & (Join-Path $out 'core_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Core tests failed' }
        & $compiler @common tests/bounded_json_tests.cpp -o (Join-Path $out 'bounded_json_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Bounded JSON test build failed' }
        & (Join-Path $out 'bounded_json_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Bounded JSON tests failed' }
        foreach ($suite in @('library_meta','renderer_snapshot','asset_properties','windows_child_processes')) {
            & $compiler @common -Wall -Wextra "tests/$($suite)_tests.cpp" -o (Join-Path $out "$($suite)_tests.exe")
            if ($LASTEXITCODE -ne 0) { throw "$suite test build failed" }
            & (Join-Path $out "$($suite)_tests.exe")
            if ($LASTEXITCODE -ne 0) { throw "$suite tests failed" }
        }
        & $compiler @common -DDRIFTER_CORE_TESTING tests/save_backups_tests.cpp src/save_backups.cpp -o (Join-Path $out 'save_backups_tests.exe') -lbcrypt -lole32
        if ($LASTEXITCODE -ne 0) { throw 'Save backups test build failed' }
        & (Join-Path $out 'save_backups_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Save backups tests failed' }
        & $compiler @common tests/save_locations_tests.cpp -o (Join-Path $out 'save_locations_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Save-location test build failed' }
        & (Join-Path $out 'save_locations_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Save-location tests failed' }
        & $compiler @common -DDRIFTER_CORE_TESTING tests/current_saves_tests.cpp src/current_saves.cpp -o (Join-Path $out 'current_saves_tests.exe') -lbcrypt -lole32
        if ($LASTEXITCODE -ne 0) { throw 'Current saves test build failed' }
        & (Join-Path $out 'current_saves_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Current saves tests failed' }
        & $compiler @common -DDRIFTER_CORE_TESTING tests/durable_json_tests.cpp src/core.cpp src/storage_location.cpp src/mod_archive.cpp src/library_backup.cpp src/game_build.cpp src/save_backups.cpp src/current_saves.cpp src/catalog_pack.cpp src/self_update.cpp src/mod_history.cpp src/external_removal.cpp src/pak.cpp src/temp_workspace.cpp src/profiles.cpp src/directories.cpp @objects -o (Join-Path $out 'durable_json_tests.exe') -lbcrypt -lcrypt32 -lole32 -lshell32 -luuid -ladvapi32
        if ($LASTEXITCODE -ne 0) { throw 'Durable JSON test build failed' }
        & (Join-Path $out 'durable_json_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Durable JSON tests failed' }
        & $compiler @common -DDRIFTER_CORE_TESTING tests/core_transaction_race_tests.cpp src/core.cpp src/storage_location.cpp src/mod_archive.cpp src/library_backup.cpp src/game_build.cpp src/save_backups.cpp src/current_saves.cpp src/catalog_pack.cpp src/self_update.cpp src/mod_history.cpp src/external_removal.cpp src/pak.cpp src/temp_workspace.cpp src/profiles.cpp @objects -o (Join-Path $out 'core_transaction_race_tests.exe') -lbcrypt -lcrypt32 -lole32 -lshell32 -luuid -ladvapi32
        if ($LASTEXITCODE -ne 0) { throw 'Core/profile game-start regression build failed' }
        & (Join-Path $out 'core_transaction_race_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Core/profile game-start regressions failed' }
        & $compiler @common -DDRIFTER_CORE_TESTING tests/cli_recovery_tests.cpp src/cli.cpp src/core.cpp src/storage_location.cpp src/mod_archive.cpp src/library_backup.cpp src/game_build.cpp src/save_backups.cpp src/current_saves.cpp src/catalog_pack.cpp src/self_update.cpp src/mod_history.cpp src/external_removal.cpp src/pak.cpp src/temp_workspace.cpp src/profiles.cpp src/diagnostics.cpp src/directories.cpp @objects -o (Join-Path $out 'cli_recovery_tests.exe') -lbcrypt -lcrypt32 -lole32 -lshell32 -luuid -ladvapi32
        if ($LASTEXITCODE -ne 0) { throw 'CLI deferred recovery regression build failed' }
        & (Join-Path $out 'cli_recovery_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'CLI deferred recovery regressions failed' }
        if (Test-Path -LiteralPath tests/nexus_tests.cpp) {
            & $compiler @common tests/nexus_tests.cpp src/nexus.cpp src/temp_workspace.cpp -o (Join-Path $out 'nexus_tests.exe') -lbcrypt -lcrypt32 -lwinhttp -lole32 -ladvapi32
            if ($LASTEXITCODE -ne 0) { throw 'Nexus test build failed' }
            & (Join-Path $out 'nexus_tests.exe')
            if ($LASTEXITCODE -ne 0) { throw 'Nexus tests failed' }
        }
        & $compiler @common tests/watcher_tests.cpp src/watcher.cpp -o (Join-Path $out 'watcher_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Watcher test build failed' }
        & (Join-Path $out 'watcher_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Watcher tests failed' }
        if (Test-Path -LiteralPath tests/directory_tests.cpp) {
            & $compiler @common -DDRIFTER_CORE_TESTING tests/directory_tests.cpp src/directories.cpp src/core.cpp src/storage_location.cpp src/mod_archive.cpp src/library_backup.cpp src/game_build.cpp src/save_backups.cpp src/current_saves.cpp src/catalog_pack.cpp src/self_update.cpp src/mod_history.cpp src/external_removal.cpp src/pak.cpp src/temp_workspace.cpp @objects -o (Join-Path $out 'directory_tests.exe') -lbcrypt -lcrypt32 -lole32 -lshell32 -luuid -ladvapi32
            if ($LASTEXITCODE -ne 0) { throw 'Directory test build failed' }
            & (Join-Path $out 'directory_tests.exe')
            if ($LASTEXITCODE -ne 0) { throw 'Directory tests failed' }
        }
        if (Test-Path -LiteralPath tests/nxm_tests.cpp) {
            & $compiler @common -DDRIFTER_NXM_TESTING tests/nxm_tests.cpp src/nxm.cpp -o (Join-Path $out 'nxm_tests.exe') -ladvapi32 -lktmw32 -lshell32 -lole32
            if ($LASTEXITCODE -ne 0) { throw 'NXM integration test build failed' }
            & (Join-Path $out 'nxm_tests.exe')
            if ($LASTEXITCODE -ne 0) { throw 'NXM integration tests failed' }
        }
        foreach ($suite in @('profile','session_runtime','diagnostics','crash_logs','game_build','library_workflow','current_saves_manager')) {
            if (!(Test-Path -LiteralPath "tests/$($suite)_tests.cpp")) { continue }
            $sources = @("tests/$($suite)_tests.cpp",'src/profiles.cpp','src/core.cpp','src/storage_location.cpp','src/mod_archive.cpp','src/library_backup.cpp','src/game_build.cpp','src/save_backups.cpp','src/current_saves.cpp','src/catalog_pack.cpp','src/self_update.cpp','src/mod_history.cpp','src/external_removal.cpp','src/pak.cpp','src/temp_workspace.cpp')
            $suiteOptions = @('-DDRIFTER_CORE_TESTING')
            if ($suite -eq 'session_runtime') { $sources += 'src/session_runtime.cpp'; $suiteOptions += '-DDRIFTER_SESSION_TESTING' }
            if ($suite -eq 'diagnostics') { $sources += @('src/diagnostics.cpp','src/directories.cpp') }
            if ($suite -eq 'crash_logs') { $sources += @('src/crash_logs.cpp','src/minidump.cpp','src/diagnostics.cpp','src/directories.cpp'); $suiteOptions += '-DDRIFTER_CRASH_TESTING' }
            & $compiler @common @suiteOptions @sources @objects -o (Join-Path $out "$($suite)_tests.exe") -lbcrypt -lcrypt32 -lole32 -lshell32 -luuid -ladvapi32
            if ($LASTEXITCODE -ne 0) { throw "$suite test build failed" }
            & (Join-Path $out "$($suite)_tests.exe")
            if ($LASTEXITCODE -ne 0) { throw "$suite tests failed" }
        }
        & $compiler @common -Wall -Wextra tests/minidump_tests.cpp src/minidump.cpp -o (Join-Path $out 'minidump_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Crash dump summary test build failed' }
        & (Join-Path $out 'minidump_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Crash dump summary tests failed' }
        & $compiler @common -Wall -Wextra tests/mod_history_tests.cpp src/mod_history.cpp -o (Join-Path $out 'mod_history_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Mod history test build failed' }
        & (Join-Path $out 'mod_history_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Mod history tests failed' }
        & $compiler @common -Wall -Wextra -DDRIFTER_UPDATE_TESTING tests/self_update_tests.cpp src/self_update.cpp @objects -o (Join-Path $out 'self_update_tests.exe') -lbcrypt
        if ($LASTEXITCODE -ne 0) { throw 'Self-update test build failed' }
        & (Join-Path $out 'self_update_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Self-update tests failed' }
        & $compiler @common -Wall -Wextra tests/toolkit_update_notice_tests.cpp src/toolkit_update_notice.cpp src/self_update.cpp @objects -o (Join-Path $out 'toolkit_update_notice_tests.exe') -lbcrypt
        if ($LASTEXITCODE -ne 0) { throw 'Toolkit release notice test build failed' }
        & (Join-Path $out 'toolkit_update_notice_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Toolkit release notice tests failed' }
        if (Test-Path -LiteralPath tests/game_restart_tests.cpp) {
            & $compiler @common tests/game_restart_tests.cpp src/game_restart.cpp -o (Join-Path $out 'game_restart_tests.exe')
            if ($LASTEXITCODE -ne 0) { throw 'Game restart test build failed' }
            & (Join-Path $out 'game_restart_tests.exe')
            if ($LASTEXITCODE -ne 0) { throw 'Game restart tests failed' }
        }
        if (Test-Path -LiteralPath tests/temp_workspace_tests.cpp) {
            & $compiler @common tests/temp_workspace_tests.cpp src/temp_workspace.cpp -o (Join-Path $out 'temp_workspace_tests.exe') -lbcrypt -ladvapi32
            if ($LASTEXITCODE -ne 0) { throw 'Temporary workspace test build failed' }
            & (Join-Path $out 'temp_workspace_tests.exe')
            if ($LASTEXITCODE -ne 0) { throw 'Temporary workspace tests failed' }
        }
        if (Test-Path -LiteralPath tests/native_crash_tests.cpp) {
            & $compiler @common tests/native_crash_tests.cpp src/native_crash.cpp -o (Join-Path $out 'native_crash_tests.exe') -lbcrypt -ladvapi32
            if ($LASTEXITCODE -ne 0) { throw 'Native crash capture test build failed' }
            & (Join-Path $out 'native_crash_tests.exe')
            if ($LASTEXITCODE -ne 0) { throw 'Native crash capture tests failed' }
        }
    }
    if (!$CliOnly) { Write-Host "Built $out\DriftersToolkit.exe" }
    Write-Host "Built $out\DriftersToolkitCLI.exe"
} finally { Pop-Location }

