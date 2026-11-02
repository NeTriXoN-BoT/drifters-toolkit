'use strict';

// No game files, browser automation, Nexus account or network connection needed.
// Run from any directory: node mod-manager/tests/ui_smoke.js
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const appRoot = path.resolve(__dirname, '..');
const catalog = JSON.parse(fs.readFileSync(path.join(appRoot, 'catalog/catalog.json'), 'utf8'));
const nodes = new Map();
const nativeRequests = [];
const actionFixtures = new Map();
const actionErrors = new Map();
let restartStatus = { pending: false, phase: 'idle' };
let snapshotFailure = false;
let nextSnapshotFixture = null;
let pagePreviewFixture = { available: false };
const timers = new Map();
let nextTimer = 1;
let osReducedMotion = false;
let osLightMode = false;
let nxmStatus = { registered: false, available: true, reason: '' };
let nativeMessageListener;
const globalListeners = new Map();

function node(selector = '') {
  if (nodes.has(selector)) return nodes.get(selector);
  const classes = new Set();
  const attributes = new Map();
  const item = {
    dataset: {},
    style: {
      setProperty(name, value) {
        this[name] = value;
      },
      removeProperty(name) {
        delete this[name];
      }
    },
    classList: {
      toggle(name, enabled) {
        if (enabled ?? !classes.has(name)) classes.add(name);
        else classes.delete(name);
      },
      add(name) {
        classes.add(name);
      },
      remove(name) {
        classes.delete(name);
      },
      contains(name) {
        return classes.has(name);
      }
    },
    value: '',
    removed: false,
    innerHTML: '',
    textContent: '',
    open: false,
    listeners: new Map(),
    addEventListener(type, listener) { this.listeners.set(type, listener); },
    setAttribute(name, value) {
      attributes.set(name, String(value));
    },
    getAttribute(name) {
      return attributes.get(name);
    },
    removeAttribute(name) {
      attributes.delete(name);
    },
    focus() { context.document.activeElement = this; },
    select() {},
    append() {},
    remove() { this.removed = true; },
    contains(target) { return target === this; },
    get attributes() { return [...attributes].map(([name, value]) => ({ name, value })); },
    querySelector: (child) => node(child),
    querySelectorAll: () => [],
    showModal() {
      this.open = true;
    },
    close() {
      this.open = false;
    }
  };
  nodes.set(selector, item);
  return item;
}

const fixture = {
  mods: catalog.mods,
  installed: [],
  conflicts: [],
  profiles: [],
  diagnostics: [],
  settings: { fontScale: 100, interfaceScale: 100 }
};
const directoriesFixture = {
  installations: [{ path: 'C:\\Synthetic\\Days Gone', label: 'Days Gone', source: 'Test fixture' }],
  locations: [
    {
      id: 'managedPaks',
      label: 'Managed PAKs',
      path: 'C:\\Synthetic\\BendGame\\Saved\\Paks',
      exists: true,
      pakCount: 2,
      freeBytes: 10737418240,
      managed: true
    },
    {
      id: 'saves',
      label: 'Save files',
      path: 'C:\\Synthetic\\BendGame\\Saved',
      exists: true,
      freeBytes: 10737418240,
      managed: false
    }
  ],
  startup: {
    status: 'not-prepared',
    canPrepare: true,
    canRestore: false,
    source: 'C:\\Synthetic\\Days Gone\\BendGame\\Content\\sfpaks\\startuppackages.pak',
    backup: 'C:\\Synthetic\\Days Gone\\BendGame\\Content\\sfpaks\\startuppackages.pak.drifters-disabled',
    message: 'Synthetic startup fixture'
  },
  duplicates: [
    { sha256: 'test', count: 2, files: [{ path: 'C:\\Synthetic\\one.pak' }, { path: 'C:\\Synthetic\\two.pak' }] }
  ]
};
const portableFixture = {
  profiles: [
    {
      id: 'independent-ride',
      name: 'Independent ride',
      portable: true,
      status: 'ready',
      count: 1,
      bytes: 123,
      path: 'C:\\Synthetic\\Profiles\\Independent ride',
      mods: [{ id: 'no-recoil', name: 'No Recoil', filename: 'NoRecoil_P.pak', bytes: 123, sha256: 'fixture' }]
    }
  ],
  session: { active: false, status: 'idle' }
};
const launchPlanFixture = {
  profileName: 'Independent ride',
  profileId: 'independent-ride',
  planToken: 'opaque-reviewed-plan',
  canLaunch: true,
  blockers: [],
  warnings: ['Review the temporary files.'],
  moves: [
    {
      source: 'C:\\Synthetic\\External.pak',
      destination: 'C:\\Synthetic\\Holding\\External.pak',
      bytes: 123,
      sha256: 'fixture',
      location: 'managedPaks'
    }
  ],
  additions: [
    {
      source: 'C:\\Synthetic\\Profiles\\Independent ride\\paks\\NoRecoil_P.pak',
      destination: 'C:\\Synthetic\\Saved\\Paks\\NoRecoil_P.pak',
      bytes: 123,
      sha256: 'fixture'
    }
  ],
  gameExe: 'C:\\Synthetic\\DaysGone.exe'
};
const diagnosticFixture = {
  checks: [
    {
      id: 'weapon-overlap',
      severity: 'error',
      status: 'failed',
      title: 'Weapon packages overlap',
      message: 'Choose the managed package to disable.',
      evidence: ['Weapons/shared.uasset'],
      helpKey: 'compatibility',
      fix: {
        action: 'disable',
        label: 'Choose a mod to disable',
        choices: [
          { id: 'no-recoil', action: 'disable', args: { id: 'no-recoil' }, label: 'Disable No Recoil' },
          {
            id: 'infinite-ammo-no-reload',
            action: 'disable',
            args: { id: 'infinite-ammo-no-reload' },
            label: 'Disable No Reload'
          }
        ],
        automatic: false,
        requiresConfirmation: true
      }
    },
    {
      id: 'unknown-index',
      severity: 'warning',
      status: 'unknown',
      title: 'Unreadable package',
      message: 'Its compatibility is unknown.',
      evidence: ['<unknown>.pak'],
      helpKey: 'external-mods'
    },
    {
      id: 'ownership',
      severity: 'info',
      status: 'passed',
      title: 'Owned files match',
      message: 'Only the checked owned files matched.'
    }
  ],
  summary: { errors: 1, warnings: 1, passed: 1, unknown: 1 },
  checkedAt: '2026-10-20T12:00:00Z',
  planToken: 'diagnostic-reviewed-token',
  scope: 'Synthetic package checks'
};
const crashFixture = {
  gameStatus: 'not-selected',
  status: 'available',
  available: true,
  scope: 'Known game and toolkit locations',
  warnings: [],
  logs: [
    {
      id: 'toolkit-text',
      name: 'toolkit-error.log',
      source: 'toolkit',
      kind: 'log',
      bytes: 120,
      modifiedAt: 1792497600,
      previewAvailable: true,
      exportAvailable: true
    },
    {
      id: 'game-dump',
      name: 'Game.dmp',
      source: 'game',
      kind: 'memory-dump',
      bytes: 10000,
      modifiedAt: 1792497600,
      previewAvailable: false,
      summaryAvailable: true,
      exportAvailable: true,
      reason: 'Raw dump contents are never shown or exported.'
    }
  ]
};
const bundleFixture = {
  planToken: 'native-support-plan',
  files: [
    { name: 'diagnostics.json', bytes: 512 },
    { name: 'logs/toolkit-error.log', bytes: 80 }
  ],
  summary: 'Fresh diagnostics and one selected text log.',
  warnings: [],
  redacted: true
};
const context = {
  window: {
    innerWidth: 1440,
    innerHeight: 980,
    scrollTo() {},
    addEventListener(type, handler) {
      globalListeners.set(type, handler);
    },
    matchMedia(query) {
      return { matches: query.includes('color-scheme') ? osLightMode : osReducedMotion };
    },
    chrome: {
      webview: {
        addEventListener(type, handler) {
          nativeMessageListener = handler;
        },
        postMessage(request) {
          nativeRequests.push(request);
          if (actionErrors.has(request.action)) {
            queueMicrotask(() =>
              nativeMessageListener({
                data: { requestId: request.requestId, ok: false, error: actionErrors.get(request.action) }
              })
            );
            return;
          }
          if (snapshotFailure && request.action === 'snapshot') {
            queueMicrotask(() =>
              nativeMessageListener({
                data: { requestId: request.requestId, ok: false, error: 'Synthetic inspection failure' }
              })
            );
            return;
          }
          if (request.action === 'gameRestartQueue')
            restartStatus = { pending: true, phase: 'waiting-for-close', message: 'Close Days Gone normally.' };
          if (request.action === 'gameRestartCancel')
            restartStatus = { pending: false, phase: 'cancelled', message: 'Queued restart cancelled.' };
          if (request.action === 'nxmRegister') nxmStatus = { registered: true, available: true, reason: '' };
          if (request.action === 'nxmUnregister') nxmStatus = { registered: false, available: true, reason: '' };
          let keepOnlyResult = null;
          if (request.action === 'keepOnlyMod' || request.action === 'switchMod') {
            keepOnlyResult = JSON.parse(JSON.stringify(context.ui.state.snapshot));
            const partners = new Set();
            for (const conflict of keepOnlyResult.conflicts || []) {
              if (conflict.a === request.id) partners.add(conflict.b);
              if (conflict.b === request.id) partners.add(conflict.a);
            }
            for (const conflict of keepOnlyResult.mods.find((mod) => mod.id === request.id)?.conflictsWithInstalled || [])
              partners.add(conflict.id);
            const disabled = new Set();
            for (const mod of keepOnlyResult.mods || []) {
              if (mod.id === request.id) mod.enabled = true;
              else if (partners.has(mod.id) && mod.enabled && (mod.owned || mod.imported)) {
                mod.enabled = false;
                disabled.add(mod.id);
              }
            }
            keepOnlyResult.conflicts = (keepOnlyResult.conflicts || []).filter(
              (conflict) => !disabled.has(conflict.a) && !disabled.has(conflict.b)
            );
            nextSnapshotFixture = keepOnlyResult;
          }
          const data = actionFixtures.has(request.action)
            ? actionFixtures.get(request.action)
            : keepOnlyResult
              ? keepOnlyResult
            : request.action === 'nexusPreviewImage'
              ? pagePreviewFixture
              : request.action.startsWith('gameRestart')
                ? restartStatus
                : request.action === 'crashLogsList'
                  ? crashFixture
                  : request.action === 'crashLogRead'
                    ? {
                        id: request.id,
                        name: 'toolkit-error.log',
                        text: '<HOME>\n<escaped log text>',
                        bytes: 80,
                        truncated: true,
                        redacted: true
                      }
                    : request.action === 'crashLogCopy'
                      ? { copied: true }
                      : request.action === 'supportBundlePreview'
                        ? bundleFixture
                        : request.action === 'supportBundleExport'
                          ? { exported: true, path: 'support.zip' }
                          : request.action === 'snapshot'
                            ? nextSnapshotFixture || fixture
                            : request.action.startsWith('directories')
                              ? directoriesFixture
                              : request.action.startsWith('nxm')
                                ? nxmStatus
                                : request.action.startsWith('diagnostics')
                                  ? diagnosticFixture
                                  : request.action === 'profileLaunchPlan'
                                    ? launchPlanFixture
                                    : request.action === 'profilesList'
                                      ? portableFixture
                                      : request.action === 'profileSessionStatus'
                                        ? portableFixture.session
                                        : { imported: false, verified: true, path: 'test-download.zip' };
          if (request.action === 'snapshot') nextSnapshotFixture = null;
          queueMicrotask(() => nativeMessageListener({ data: { requestId: request.requestId, ok: true, data } }));
        }
      }
    }
  },
  document: {
    querySelector: (selector) => node(selector),
    querySelectorAll: () => [],
    getElementById: (id) => nodes.get('#' + id) || null,
    documentElement: node('documentElement'),
    body: node('body'),
    createElement: () => node('temporary')
  },
  // Tests explicitly advance debounce timers; bridge replies arrive through microtasks.
  setTimeout(callback, delay) {
    const id = nextTimer++;
    timers.set(id, { callback, delay });
    return id;
  },
  clearTimeout(id) {
    timers.delete(id);
  },
  console,
  Date,
  Map,
  Number,
  String,
  Math,
  Error,
  URL
};
context.TextEncoder = TextEncoder;
node('#mod-details-panel').hidden = true;
vm.createContext(context);
vm.runInContext(fs.readFileSync(path.join(appRoot, 'ui/help.js'), 'utf8'), context);
vm.runInContext(fs.readFileSync(path.join(appRoot, 'ui/changelog.js'), 'utf8'), context);
const configuredNotes = JSON.parse(fs.readFileSync(path.join(appRoot, 'config/release-notes.json'), 'utf8'));
assert.equal(context.window.WHATS_NEW.version, configuredNotes.version, 'What’s new uses the generated current version');
assert.deepEqual(Array.from(context.window.WHATS_NEW.items), configuredNotes.items, 'Current highlights match the release record rather than a manual help-page copy');
// Probes on raw source text compare in a normalized form (no whitespace, one quote style, leading zeros, no
// semicolon before a closing brace), so code formatting never changes what they prove. Probe strings pass
// through the same normalization.
const compact = (text) =>
  text
    .replace(/\s+/g, '')
    .replace(/'/g, '"')
    .replace(/(?<![\d.])\.(\d)/g, '0.$1')
    .replace(/;}/g, '}');
const contains = (compacted, probe) => compacted.includes(compact(probe));
let source = fs.readFileSync(path.join(appRoot, 'ui/app.js'), 'utf8');
const initialization = source.indexOf("$$('.sidebar [data-view]')");
assert(initialization > 0, 'UI initialization marker must exist');
source =
  source.slice(0, initialization) +
  `globalThis.ui = {
  state, normalizeSnapshot, render, bindContent, navigate, renderOverview, renderMods, renderDownloads, renderFolders, renderConflicts, renderDiagnosticPage,
  renderProfiles, savePortableProfile, renderHelp, renderSettings, renderSaves, activeSeason, filteredMods, fileCatalogMod, isLocalMod, libraryMods, previewPageId, externalList, externalChangeReason, reviewExternalRemoval, reviewExternalAdoption, brokenInstallationRecord, installationRecordReason, reviewForgetInstallation,
  connectNexus, authorizedNexusLink, applyAppearance, confirmDirectoryChange, saveLoaderSettings, finishSettingsSave, run, request, pending,
  cleanSystemAppearance, effectiveColorMode, colorContrast, usableDesktopPalette, applyColorAppearance, handleSystemAppearance, appearanceStatus, renderAppearanceSettings,
  resolveModIcon, modArtwork, renderModRows, cleanPagePreview, hasPreviewMetadata, queuePagePreview, pagePreviewCache, rememberPagePreview, paintPagePreview, handleInterfaceFault, handleInterfaceRecovered, recoverInterfaceView, launchOrReviewRestart, reviewGameRestart, handleGameRestartEvent, renderGameRestartBanner,
  renderSeasonalEffects, renderSeasonalScenery, festiveWinter, seasonLabel, handleWindowVisibility, syncWindowVisibility, configureNxm, renderNxmSetting, showModDetails,
  omittedConflictPairs, detectedConflictCount, conflictSampleNote, handleAction, cleanDisplayMetrics, automaticScaleDecision, handleDisplayMetrics, scaleStatus, autoInterfaceScale, effectiveInterfaceScale, handleInterfaceShortcut, markScrollActivity, markKeyboardNavigation,
  nexusAccountMarkup, nexusCanDownload, acceptReleaseCheck, installedUpdate, renderUpdateSummary, liveNewFiles, renderDiscoveries, reviewDownloadedPackage, nexusModId, nexusFileId, sameNexusFile, includeInspectedUpdates, downloadNexusFile, guideBody, checkAutomaticReleases, visibleGuideArticles, renderModdingChapters, openModdingResource, configureSandboxSmoke, refresh, renderCrashLogs, loadCrashLogs, previewCrashLog, summarizeCrashDump, reviewSupportBundle, loadPortableProfiles, reviewPortableProfile, sessionBlocksAction, renderProfileSessionBanner,
  restoreProfileSession, openFinder, renderFinderBanner, renderCrashMods, manageAllExternal, autoManageExternal, reconcileInstallationRecords, conflictGroups, conflictGroupCount, toggleMod, backgroundCheck, snapshotSignature, versionLabel, importDroppedFile, modFamily, renderGettingStarted, compareMods, renderDownloadOffers, modListText, keepOnlyMod, readableModName, choosePackages, withCatalogNames, showUpdated, releaseAllManaged, manageableExternal, releasableMods, renderUpdateBanner, renderUpdatePanel, checkForUpdate, confirmUpdate, localEdition, renderLocalDownloads, loadDiagnosticReport, reviewDiagnosticFix, diagnosticStatus, considerSetup, collectActionNotices, showNextNotice, showActionError,
  playReadiness, renderPlayReadiness, handleGameProcessChanged, showSaveFiles, cleanSavePicture, rememberSavePicture, showSavePicture, saveGalleryPicture, loadLiveInventory, clearLiveNexusContent,
  pollCrashNotice, checkGameBuild, loadSaveBackups, renderSaveBackupsPanel, saveBackupOptions, reviewSaveBackup,
  selectedSaveBackup, selectSaveBackup, saveDetailsDraft, saveSelectedBackupDetails, updateSaveRestoreStatus,
  saveBackupProfileLinks, filteredSaveBackupList, saveBackupProfiles, openProfileSaveBackups, renderProfileRows, profileSaveBackupWarning, showProfileSaveBackupWarning, handleProfileSessionEvent,
  setSavesTab, currentSavesVisible, currentSavesCanRead, syncCurrentSavesPoll, loadCurrentSaves, renderCurrentSavesPanel, loadCurrentSavePreviews, currentSavePreviewKey,
  bindCurrentSaveControls, bindSaveBackupControls, loadSavePictures, updateSavesGameStatus,
  saveThumbnailState: () => ({ timer: saveThumbnailPumpTimer, currentLoading: currentPreviewsLoading,
    backupLoading: savePicturesLoading, bytes: saveThumbnailBytes(), entries: saveThumbnailEntryLimit, byteLimit: saveThumbnailByteLimit }),
  currentSavesPollState: () => ({ timer: currentSavesTimer, running: currentSavesRequest }),
  parseSharedModList, openSharedModList, updateReleaseNotes, startConflictGuide, showConflictStep,
  personalModMeta, loadLibraryMeta, saveModMeta, showLibraryHistory, openLibraryTools,
  toast, rememberMainFocus, restoreMainFocus, modStatusMarkup, closeModDetails, showModPanel, syncModPanelNavigation, handleDetailsShortcut, handleModalClosed, reviewBatchImport, showBatchImportResults, discardBatchReview, renderActivity, updateActivity, handleNativeOperationProgress, handleDroppedImport, renderSettingsBrowser, filterSettingsSections, showGamePathError, updateGamePathError, gamePathFieldValue, focusGamePathError, renderLibraryBackupPanel, confirmLibraryBackupExport, reviewLibraryBackup, reviewLibraryBackupRecovery, acceptSnapshot, compatibilityCheckReason, unreadableInstalledPackages,
  reviewDuplicateCleanup, bindModAssetDetails, showWhatsNew, showChangelog, changelogReleases,
  renderOverviewHero, advanceHeroSlide, syncHeroSlideshow, bindHeroSlideshow, heroSlideshow, heroScenes, restoredHeroSlideshow,
  renderStatusTicker, statusTicker, statusTickerText,
  checkToolkitRelease, renderToolkitReleaseBanner, newerToolkitVersion,
  checkDownloads, addSpottedDownload, showImportResult, enableImportedSelection, storedMods, activationLabel, libraryToolbar,
  renderModRowActions, modActionBlockReason, applyModActionLocks, libraryDocked, syncLibraryDock,
  syncActivityDismissal, activityNeedsAttention,
  activityDismissalState: () => activityDismissal,
  catalogOffer, renderCatalogPanel, renderCatalogBanner, checkForCatalog, confirmCatalog, latestCatalogRelease, statusFor,
  resetNotices: () => { state.noticeQueue=[];state.setupPrompted=false;seenNoticeFingerprints.clear();shownIssueKeys.clear();state.issues=[];if(noticeDeliveryTimer)clearTimeout(noticeDeliveryTimer);noticeDeliveryTimer=null; },
  restoreWatchState: (saved) => { filesystemDirty=saved.dirty;filesystemRefreshRunning=saved.running;if(filesystemRefreshTimer)clearTimeout(filesystemRefreshTimer);filesystemRefreshTimer=null; },
  watchState: () => ({ dirty: filesystemDirty, running: filesystemRefreshRunning })
};})();`;
vm.runInContext(source, context);
const ui = context.ui;

async function testProfileCreation() {
  const saved = { ...ui.state }, bridge = context.window.chrome.webview, post = bridge.postMessage;
  const actions = ['profilesSave', 'profilesRename', 'profilesList'];
  const previous = new Map(actions.filter((action) => actionFixtures.has(action)).map((action) => [action, actionFixtures.get(action)]));
  try {
    for (const platform of ['windows', 'linux']) {
      const profile = { id: 'c'.repeat(32), name: 'New profile', status: 'ready', owned: true, count: 0, mods: [] };
      const result = { profiles: [profile], session: { active: false } };
      Object.assign(ui.state, { preview: false, loading: false, busy: false, view: 'profiles', profileSearch: 'old search',
        snapshot: ui.normalizeSnapshot({ ...fixture, platform, accountContext: 'create-' + platform }),
        portableProfiles: { profiles: [], session: { active: false } }, profilesLoading: false,
        profileSession: { active: false }, nativeDropActivity: null });
      // Reproduces the former silent close: run() refuses while another action is busy.
      ui.savePortableProfile(); node('#portable-profile-name').value = profile.name;
      ui.state.busy = true;
      await node('#portable-profile-save').onclick();
      assert(node('#modal').open, 'A blocked save keeps the dialog open');
      assert(node('#portable-profile-error').textContent.includes('Wait'), 'A blocked save explains how to retry');
      ui.state.busy = false;
      actionErrors.set('profilesSave', 'Close Days Gone before saving a portable profile.');
      await node('#portable-profile-save').onclick();
      assert(node('#modal').open && node('#portable-profile-name').value === profile.name, 'Native save failure preserves the dialog and entered name');
      assert(node('#portable-profile-error').textContent.includes('Close Days Gone'), 'The actual native refusal is visible beside the name');
      assert(!node('#portable-profile-save').disabled, 'Native refusal leaves Create ready to retry');
      actionErrors.delete('profilesSave'); actionFixtures.set('profilesSave', result);
      let heldList;
      bridge.postMessage = (request) => {
        if (request.action === 'profilesList') { heldList = request; nativeRequests.push(request); }
        else post.call(bridge, request);
      };
      const oldRead = ui.loadPortableProfiles();
      node('#portable-profile-source').value = 'empty';
      const before = nativeRequests.length;
      const saving = node('#portable-profile-save').onclick();
      await node('#portable-profile-save').onclick();
      await saving;
      assert.equal(nativeRequests.slice(before).filter((request) => request.action === 'profilesSave').length, 1, 'Double click submits one native save');
      assert.equal(nativeRequests.at(-1).empty, true, 'Blank creation is explicit and never disables live mods');
      assert(!node('#modal').open, 'Successful creation closes the dialog');
      assert.equal(ui.state.portableProfiles.profiles[0].id, profile.id, 'The complete native save result is shown immediately');
      assert.equal(ui.state.profileSearch, '', 'A stale search cannot hide the new profile');
      nativeMessageListener({ data: { requestId: heldList.requestId, ok: true, data: { profiles: [], session: { active: false } } } });
      await oldRead;
      assert.equal(ui.state.portableProfiles.profiles[0].id, profile.id, 'An older list response cannot erase a freshly saved profile');
      bridge.postMessage = post;
      ui.savePortableProfile(); node('#portable-profile-name').value = profile.name.toUpperCase();
      const duplicates = nativeRequests.length;
      await node('#portable-profile-save').onclick();
      assert.equal(nativeRequests.length, duplicates, 'Duplicate names are rejected inline before a native write');
      node('#portable-profile-name').value = 'bad/name'; await node('#portable-profile-save').onclick();
      assert.equal(nativeRequests.length, duplicates, 'Filename symbols cannot reach the native writer');
      node('#portable-profile-name').value = '\u00e9'.repeat(40); await node('#portable-profile-save').onclick();
      assert.equal(nativeRequests.length, duplicates, 'UTF-8 name limits agree with the native manifest limit');
      node('#modal').close();
      actionFixtures.set('profilesRename', { ...result, profiles: [{ ...profile, name: 'Renamed' }] });
      ui.savePortableProfile(profile); assert.equal(node('#portable-profile-name').value, profile.name);
      node('#portable-profile-name').value = 'Renamed'; await node('#portable-profile-save').onclick();
      assert.equal(nativeRequests.at(-1).action, 'profilesRename');
      assert.equal(nativeRequests.at(-1).id, profile.id, 'Rename keeps the verified native identity');
      assert.equal(ui.state.portableProfiles.profiles[0].name, 'Renamed');
      for (const existingProfile of [null, profile]) {
        const originalContext = ui.state.snapshot.accountContext;
        ui.savePortableProfile(existingProfile);
        const input = node('#portable-profile-name'), submit = node('#portable-profile-save'),
          originalClick = submit.click;
        let submitted = 0, prevented = 0;
        submit.click = () => { submitted++; };
        input.onkeydown({ key: 'Enter', isComposing: true, preventDefault: () => { prevented++; } });
        assert.equal(submitted, 0, 'IME confirmation never submits create or rename prematurely');
        assert.equal(prevented, 0, 'IME Enter keeps its normal composition behavior');
        input.onkeydown({ key: 'Enter', isComposing: false, preventDefault: () => { prevented++; } });
        assert.equal(submitted, 1, 'Ordinary Enter still activates create or rename');
        submit.click = originalClick;
        input.value = 'Keep my typed name';
        ui.state.snapshot.accountContext = originalContext + '-changed-before-submit';
        const requests = nativeRequests.length;
        await submit.onclick();
        assert.equal(nativeRequests.length, requests, 'An old installation dialog sends no create or rename request');
        assert.equal(input.value, 'Keep my typed name', 'Installation changes preserve the entered name');
        assert(node('#modal').open && node('#portable-profile-error').textContent.includes('installation changed'), 'The stale dialog explains how to reopen safely');
        assert.equal(submit.disabled, false, 'The guard never starts progress or locks the input');
        assert.equal(input.disabled, false);
        ui.state.snapshot.accountContext = originalContext;
        node('#modal').close();
      }
      ui.savePortableProfile(); node('#portable-profile-name').value = 'Other installation';
      const staleSave = node('#portable-profile-save').onclick();
      ui.state.snapshot.accountContext += '-changed';
      await staleSave;
      assert.equal(ui.state.portableProfiles.profiles[0].name, 'Renamed', 'An old installation save result cannot replace the current profile list');
      node('#modal').close();
    }
  } finally {
    bridge.postMessage = post;
    for (const action of actions) { actionErrors.delete(action); if (previous.has(action)) actionFixtures.set(action, previous.get(action)); else actionFixtures.delete(action); }
    Object.assign(ui.state, saved); node('#modal').close();
  }
}

async function testChangelogHistory() {
  const snapshot = ui.state.snapshot, history = context.window.CHANGELOG_HISTORY,
    latest = context.window.WHATS_NEW, requestsBefore = nativeRequests.length;
  try {
    const current = history[0]; context.window.WHATS_NEW = current;
    const archived = JSON.parse(fs.readFileSync(path.join(appRoot, 'config/changelog-history.json'), 'utf8')).releases;
    const currentSource = JSON.parse(fs.readFileSync(path.join(appRoot, 'config/release-notes.json'), 'utf8'));
    assert.equal(current.version, currentSource.version, 'The generated archive starts with the current configured release');
    const versionOrder = new Intl.Collator('en', { numeric: true });
    const expectedVersions = [...new Set([currentSource.version, ...archived.map((release) => release.version)])]
      .sort((left, right) => versionOrder.compare(right, left));
    for (const release of archived) {
      const bundled = history.find((item) => item.version === release.version);
      assert(bundled, `Saved ${release.version} changelog is bundled`);
      assert.deepEqual(Array.from(bundled.items), release.items, 'Historical wording is retained in full');
    }
    for (const platform of ['windows', 'linux']) {
      ui.state.snapshot = { ...snapshot, appVersion: current.version, platform };
      ui.showWhatsNew();
      assert(!node('#modal-content').innerHTML.includes('Older changes are in'), 'Removed Nexus history claim stays absent');
      node('#whats-new-changelog').onclick();
      const markup = node('#modal-content').innerHTML;
      assert(markup.includes('work offline') && markup.includes('changelog-installed') && !markup.includes('<select'), 'History is local and uses a themed version picker');
      assert.equal((markup.match(/class="changelog-note"/g) || []).length, ui.changelogReleases().reduce((sum, item) => sum + item.items.length, 0), 'All bounded history panels are built once as unnumbered paragraphs');
      assert.deepEqual(Array.from(ui.changelogReleases(), (item) => item.version), expectedVersions, 'Current and archived versions are newest first on both platforms');
      const selector = node('#changelog-version'), historicalPick = ui.changelogReleases().findIndex((item) => item.version === '1.0.2');
      node('#changelog-pick-' + historicalPick).onclick();
      assert.equal(node('#changelog-entry').dataset.version, '1.0.2', 'The full historical release is selected without rebuilding its notes');
      assert.equal(ui.changelogReleases()[historicalPick].items.length, 30, 'Long historical releases are not truncated to the updater highlight limit');
      assert.equal(node('#changelog-panel-' + historicalPick).hidden, false, 'Selected historical panel is visible');
      assert.equal(node('#modal').open, true, 'Changing version keeps the dialog open');
      assert.equal(node('#changelog-picker').open, false, 'Choosing a version closes only the picker');
      assert.equal(context.document.activeElement, selector, 'Choosing a version returns focus to its control');
      assert.equal(node('#changelog-pick-' + historicalPick).getAttribute('aria-current'), 'true', 'Chosen release is identified accessibly');
      assert.equal(node('#changelog-pick-0').getAttribute('aria-current'), 'false', 'Previous choice is cleared');
      let prevented = 0, stopped = 0;
      const key = (value) => node('#changelog-picker').onkeydown({ key: value, preventDefault() { prevented++; }, stopPropagation() { stopped++; } });
      key('ArrowDown');
      assert.equal(node('#changelog-picker').open, true, 'Arrow key opens the version picker');
      assert.equal(context.document.activeElement, node('#changelog-pick-' + historicalPick), 'Keyboard starts at the chosen release');
      key('End'); assert.equal(context.document.activeElement, node('#changelog-pick-' + (ui.changelogReleases().length - 1)), 'End reaches the oldest release');
      key('Home'); assert.equal(context.document.activeElement, node('#changelog-pick-0'), 'Home reaches the newest release');
      key('Escape');
      assert.equal(node('#changelog-picker').open, false, 'Escape closes the picker');
      assert.equal(node('#modal').open, true, 'Escape in the picker leaves the history dialog open');
      assert.equal(stopped, 1, 'Picker Escape does not reach the outer dialog');
      assert.equal(prevented, 4, 'Navigation keys use the themed picker');
      node('#changelog-picker').open = true;
      node('#changelog-picker').listeners.get('focusout')({ relatedTarget: node('outside-version-picker') });
      assert.equal(node('#changelog-picker').open, false, 'Tabbing out closes the version list');
      node('#changelog-back').onclick();
      assert(node('#modal-content').innerHTML.includes('What’s new in ' + current.version), 'Back returns to the original highlights');
      assert.equal(context.document.activeElement, node('.modal-body'), 'Dialog navigation focuses its readable content');
      await ui.handleAction('changelog');
      assert(node('#modal-content').innerHTML.includes('<h2 id="modal-title">Changelog</h2>'), 'Settings opens history directly');
      assert(ui.renderUpdatePanel().includes('data-action="changelog"'), 'History is discoverable in Settings');
      node('#modal').close();

      // Installing a later test build must not make the previous release's
      // complete notes or its Installed badge depend on the first menu row.
      const previous = archived.find((release) => release.version === '1.0.7');
      assert(previous, 'The published 1.0.7 notes remain archived');
      ui.state.snapshot = { ...snapshot, appVersion: previous.version, platform };
      ui.showChangelog();
      const installedMarkup = node('#modal-content').innerHTML;
      assert(installedMarkup.includes('<span class="changelog-version-number">1.0.7</span><span class="changelog-installed">Installed</span>'),
        'A simulated installed 1.0.7 retains its own badge when newer notes exist');
      assert(installedMarkup.includes('<h3>Version 1.0.7</h3>') &&
        node('#changelog-entry').dataset.version === previous.version &&
        (installedMarkup.match(/class="changelog-note"/g) || []).length === ui.changelogReleases().reduce((sum, item) => sum + item.items.length, 0),
        'The simulated older installation defaults to its full archived notes');
      node('#modal').close();
    }
    context.window.CHANGELOG_HISTORY = [
      { version: '1.0.2', items: ['A <script>alert(1)</script> & a quote "'] },
      { version: '1.0.10', items: ['A later patch'] },
      { version: '<img src=x>', items: ['Invalid version'] },
      { version: '1.0.2', items: ['Duplicate'] },
      { version: '1.0.1', items: [null, 12] }
    ];
    ui.showChangelog('1.0.2');
    assert(node('#modal-content').innerHTML.includes('&lt;script&gt;') && !node('#modal-content').innerHTML.includes('<script>'), 'History text is escaped');
    assert.deepEqual(Array.from(ui.changelogReleases(), (item) => item.version), ['1.0.10', current.version, '1.0.2'], 'Semantic sorting and malformed/duplicate filtering are safe');
    context.window.CHANGELOG_HISTORY = undefined;
    ui.showChangelog();
    assert(node('#modal-content').innerHTML.includes('Version ' + current.version), 'Missing archive falls back to this version’s highlights');
    context.window.WHATS_NEW = undefined;
    ui.showChangelog();
    assert(node('#modal-content').innerHTML.includes('No release notes are bundled'), 'An empty history explains itself without crashing');
    assert.equal(nativeRequests.length, requestsBefore, 'Reading history never calls the native bridge or Nexus');
  } finally {
    context.window.CHANGELOG_HISTORY = history; context.window.WHATS_NEW = latest;
    ui.state.snapshot = snapshot; node('#modal').close();
  }
}

function testLibraryActionPresentation() {
  const saved = { ...ui.state }, width = context.window.innerWidth, active = context.document.activeElement;
  const panel = node('#mod-details-panel');
  try {
    for (const platform of ['windows', 'linux']) {
      const mods = catalog.mods.map((mod) => ({ ...mod, imported: false, enabled: false, owned: false,
        conflictsWithInstalled: [] }));
      const first = mods.find((mod) => mod.available !== false);
      Object.assign(first, { imported: true, enabled: true, owned: true, hasBeenEnabled: true });
      ui.state.snapshot = ui.normalizeSnapshot({ ...fixture, platform, edition: 'local', gameRunning: false,
        accountContext: `library-actions-${platform}`, mods,
        settings: { ...fixture.settings, gamePath: platform === 'linux' ? '/synthetic/DaysGone' : 'C:/Synthetic/DaysGone',
          autoScale: false, interfaceScale: 100, fontScale: 100 } });
      Object.assign(ui.state, { view: 'mods', loading: false, busy: false, nativeDropActivity: null,
        profileSession: { active: false }, detailId: null, filter: 'enabled', search: '', category: 'all',
        favoritesOnly: false, libraryMetaLoaded: true });
      let rows = ui.renderModRows();
      assert(rows.includes('>Disable</button>') && rows.includes('>Details</button>'), 'Enabled rows expose clear Disable and Details actions');
      assert(!rows.includes('role="switch"'), 'The primary mod action is no longer an unexplained switch');
      assert(rows.includes(`data-mod-row="${first.id}"`), 'Rows retain stable package identity');
      ui.state.snapshot.gameRunning = true;
      rows = ui.renderModRows();
      assert(rows.includes('Close Days Gone before changing mods.') && rows.includes(' disabled '), 'A blocked action explains why beside its button');
      ui.state.snapshot.gameRunning = false;
      first.enabled = false; ui.state.snapshot.mods.find((mod) => mod.id === first.id).enabled = false;
      const stored = ui.state.snapshot.mods.find((mod) => mod.id === first.id);
      assert(ui.renderModRowActions(stored).includes('>Enable</button>'), 'Inactive imported packages expose Enable');
      ui.state.profileSession.active = true;
      assert(ui.modActionBlockReason(stored, true).includes('profile session'), 'Profile-session restriction is retained');
      ui.state.profileSession.active = false; ui.state.snapshot.recoveryRequired = true;
      assert(ui.modActionBlockReason(stored, true).includes('recovery'), 'Recovery restriction is retained');
      ui.state.snapshot.recoveryRequired = false; ui.state.snapshot.settings.gamePath = '';
      assert(ui.modActionBlockReason(stored, true).includes('game folder'), 'Missing installation gives a useful enabling reason');
      assert.equal(ui.modActionBlockReason(stored, false), '', 'Missing game selection does not block removing an already owned active copy');
      ui.state.snapshot.settings.gamePath = platform === 'linux' ? '/synthetic/DaysGone' : 'C:/Synthetic/DaysGone';
      context.window.innerWidth = 1920; ui.syncLibraryDock();
      assert(ui.libraryDocked() && context.document.body.classList.contains('library-docked'), 'Wide My mods uses the existing panel beside the collection');
      context.document.activeElement = node('#mod-search');
      ui.showModDetails(stored.id, { focus: false });
      assert.equal(context.document.activeElement, node('#mod-search'), 'Opening initial context can preserve keyboard focus');
      assert.equal(node('#mod-details-panel'), panel, 'Docked context reuses the one persistent details panel');
      const details = panel.innerHTML;
      assert(details.includes('id="detail-remove"') && details.includes('id="detail-personal-notes"') && details.includes('id="detail-assets"'), 'Advanced removal, personal notes and package evidence remain available');
      ui.state.snapshot.gameRunning = true; ui.applyModActionLocks();
      assert.equal(node('#detail-toggle').disabled, true, 'Open details respond to a newly running game');
      assert(node('#detail-action-reason').textContent.includes('Close Days Gone'), 'The selected context also explains a live block');
      ui.state.snapshot.gameRunning = false; stored.enabled = true; ui.applyModActionLocks();
      assert(node('#detail-toggle').innerHTML.includes('Disable') && node('#detail-activation-state').textContent === 'Enabled', 'Current activation labels update without rebuilding notes');
      stored.imported = false;
      assert(ui.renderModRowActions(stored).includes('>Disable</button>'), 'An installed copy remains disableable even if its cached copy is missing');
      context.window.innerWidth = 900; ui.syncLibraryDock();
      assert(!ui.libraryDocked() && !panel.hidden, 'Narrow windows retain the same details as a drawer');
      ui.closeModDetails(false);
    }
  } finally {
    Object.assign(ui.state, saved); context.window.innerWidth = width; context.document.activeElement = active;
    panel.hidden = true;
    ['library-view', 'library-docked', 'details-open'].forEach((name) => context.document.body.classList.remove(name));
  }
}

function testContextualStatusTips() {
  const saved = { ...ui.state }, paused = ui.statusTicker.paused, storage = context.window.localStorage;
  const stored = new Map(), status = node('#footer-status').textContent;
  try {
    context.window.localStorage = { getItem: (key) => stored.get(key) ?? null, setItem: (key, value) => stored.set(key, value) };
    Object.assign(ui.state, { loading: false, view: 'saves' }); ui.statusTicker.paused = false;
    ui.renderStatusTicker();
    assert(node('#status-ticker-track').textContent.includes('save files'), 'The strip has relevant page information');
    assert.equal(node('#footer-status').textContent, status, 'Helpful tips never replace operation status');
    const control = node('#status-ticker-pause');
    control.innerHTML = 'keep-current-icon'; ui.renderStatusTicker();
    assert.equal(control.innerHTML, 'keep-current-icon', 'Unchanged rendering never replaces the icon under a click');
    control.onclick();
    assert.equal(ui.statusTicker.paused, true, 'Tips can be paused');
    assert.equal(stored.get('drifter.statusTipsPaused'), 'true', 'The pause preference is saved');
    assert(node('#status-ticker').classList.contains('is-paused'), 'Paused tips remain static and readable');
    ui.state.view = 'mods'; ui.renderStatusTicker();
    assert(node('#status-ticker-track').textContent.includes('Stored only'), 'Navigation updates information without another timer');
    context.document.documentElement.classList.add('reduce-motion'); ui.renderStatusTicker();
    assert.equal(control.disabled, true, 'Reduced motion keeps the strip static');
    context.document.documentElement.classList.remove('reduce-motion');
    ui.state.loading = true; ui.renderStatusTicker();
    assert.equal(node('#status-ticker').hidden, true, 'Tips stay out of the startup screen');
  } finally {
    Object.assign(ui.state, saved); ui.statusTicker.paused = paused;
    context.window.localStorage = storage;
    context.document.documentElement.classList.remove('reduce-motion');
  }
}

function testOverviewSlideshow() {
  const saved = { ...ui.state }, slide = { ...ui.heroSlideshow },
    all = context.document.querySelectorAll, hidden = context.document.hidden,
    active = context.document.activeElement, focus = context.document.hasFocus, storage = context.window.localStorage;
  const frames = [node('hero-frame-0'), node('hero-frame-1')], stored = new Map();
  try {
    context.document.querySelectorAll = (selector) => selector === '#overview-hero .hero-scene' ? frames : all(selector);
    context.document.hidden = false; context.document.hasFocus = () => true;
    context.window.localStorage = { getItem: (key) => stored.get(key) ?? null, setItem: (key, value) => stored.set(key, value) };
    context.document.activeElement = node('body'); node('#modal').open = false;
    context.document.documentElement.classList.remove('reduce-motion', 'app-hidden');
    // The fake classList takes one token at a time.
    context.document.documentElement.classList.remove('app-hidden');
    Object.assign(ui.state, { view: 'overview', loading: false, nativeWindowHidden: false });
    Object.assign(ui.heroSlideshow, { index: 0, paused: false, hover: false });
    for (const platform of ['windows', 'linux']) {
      ui.state.snapshot = { ...saved.snapshot, platform };
      assert.equal(ui.heroScenes.length, 14, 'Fourteen distinct local landscapes are available on each OS');
      assert.equal((ui.renderOverviewHero().match(/class="hero-scene(?: |")/g) || []).length, 2, 'Only two image layers are loaded for the crossfade');
      frames[0].classList.add('is-current'); frames[1].classList.remove('is-current');
      ui.bindHeroSlideshow();
      assert.equal(timers.get(ui.heroSlideshow.timer).delay, 12000, 'One quiet timer advances the overview');
      const originalPauseIcon = node('#hero-pause').innerHTML;
      node('#hero-pause').innerHTML = 'same-icon-node-sentinel';
      ui.syncHeroSlideshow();
      assert.equal(node('#hero-pause').innerHTML, 'same-icon-node-sentinel', 'Focus and hover scheduling never replace the pause icon under the pointer');
      node('#hero-pause').innerHTML = originalPauseIcon;
      const firstTimer = ui.heroSlideshow.timer;
      ui.syncHeroSlideshow();
      assert(!timers.has(firstTimer), 'Rescheduling clears the old timer');
      ui.advanceHeroSlide();
      assert.equal(node('#hero-scene-title').textContent, 'Into the pines', 'Landscape and caption change together');
      assert(frames[1].classList.contains('is-current') && !frames[0].classList.contains('is-current'), 'Only one landscape is selected');
      node('#hero-previous').onclick();
      assert.equal(ui.heroSlideshow.index, 0, 'Previous landscape is available');
      node('#hero-pause').onclick();
      assert.equal(ui.heroSlideshow.timer, null, 'Pause stops automatic rotation');
      assert.equal(node('#hero-pause').getAttribute('aria-pressed'), 'true', 'Pause is announced accessibly');
      node('#hero-next').onclick();
      assert.equal(JSON.stringify(ui.restoredHeroSlideshow()), JSON.stringify({ index: 1, paused: true }), 'The exact paused image survives reopening');
      node('#hero-previous').onclick();
      node('#hero-pause').onclick();
      assert(ui.heroSlideshow.timer !== null, 'Resume starts rotation again');
      context.document.hasFocus = () => false; ui.syncHeroSlideshow();
      assert(ui.heroSlideshow.timer !== null, 'Visible scenery still rotates while another app has focus');
      context.document.hasFocus = () => true;
      assert.equal(node('#overview-hero').onpointerenter, undefined, 'Resting the mouse over scenery no longer stops automatic playback');
      node('#overview-hero .hero-scene-controls').onpointerenter();
      assert.equal(ui.heroSlideshow.timer, null, 'Hover pauses without changing the user preference');
      node('#overview-hero .hero-scene-controls').onpointerleave();
      assert(ui.heroSlideshow.timer !== null, 'Leaving the hero resumes rotation');
      const hero = node('#overview-hero'), contains = hero.contains;
      hero.contains = (target) => target === node('#hero-next');
      node('#hero-next').matches = (query) => query === ':focus-visible';
      context.document.activeElement = node('#hero-next');
      hero.listeners.get('focusin')();
      assert.equal(ui.heroSlideshow.timer, null, 'Keyboard focus pauses automatic slides');
      hero.listeners.get('focusout')({ relatedTarget: node('outside-hero') });
      const deferred = ui.heroSlideshow.timer;
      assert.equal(timers.get(deferred).delay, 0, 'Focusout waits for the browser to update its active element');
      context.document.activeElement = node('outside-hero');
      const resume = timers.get(deferred).callback;
      timers.delete(deferred); resume();
      assert.equal(timers.get(ui.heroSlideshow.timer).delay, 12000, 'Leaving with the keyboard resumes automatic slides');
      hero.contains = contains;
      delete node('#hero-next').matches;
      context.document.hidden = true; ui.syncHeroSlideshow();
      assert.equal(ui.heroSlideshow.timer, null, 'A hidden app does no slideshow work');
      context.document.hidden = false; ui.state.view = 'mods'; ui.syncHeroSlideshow();
      assert.equal(ui.heroSlideshow.timer, null, 'Other pages do no slideshow work');
      ui.state.view = 'overview'; node('#modal').open = true; ui.syncHeroSlideshow();
      assert.equal(ui.heroSlideshow.timer, null, 'Reading a dialog pauses automatic rotation');
      node('#modal').open = false;
      context.document.documentElement.classList.add('reduce-motion'); ui.syncHeroSlideshow();
      assert.equal(ui.heroSlideshow.timer, null, 'Reduced motion disables automatic rotation');
      assert.equal(node('#hero-pause').disabled, true, 'Reduced-motion pause cannot be overridden accidentally');
      node('#hero-next').onclick();
      assert.equal(ui.heroSlideshow.index, 1, 'Manual browsing is retained with reduced motion');
      context.document.documentElement.classList.remove('reduce-motion');
      ui.heroSlideshow.index = 0;
    }
    stored.set('drifter.heroScene', JSON.stringify('retired-scene-key'));
    assert.equal(ui.restoredHeroSlideshow().index, 0, 'An unavailable saved image safely falls back to the default');
  } finally {
    if (ui.heroSlideshow.timer !== null) context.clearTimeout(ui.heroSlideshow.timer);
    Object.assign(ui.heroSlideshow, slide); Object.assign(ui.state, saved);
    context.document.querySelectorAll = all; context.document.hidden = hidden;
    context.document.activeElement = active; context.document.hasFocus = focus;
    context.window.localStorage = storage;
    context.document.documentElement.classList.remove('reduce-motion');
  }
}

async function testDownloadDiscoveryUX() {
  const saved = { ...ui.state }, originalAll = context.document.querySelectorAll,
    originalHidden = context.document.hidden, originalActive = context.document.activeElement;
  const actions = ['snapshot', 'downloadCandidates', 'downloadImport', 'enable'];
  const originalFixtures = new Map(actions.filter((action) => actionFixtures.has(action)).map((action) => [action, actionFixtures.get(action)]));
  const supported = catalog.mods.filter((mod) => mod.available !== false), first = supported[0], second = supported[1];
  const makeSnapshot = (ids = [], changes = {}) => ({ ...fixture, edition: 'local', platform: 'windows',
    accountContext: 'download-discovery-ux', gameRunning: false, recoveryRequired: false,
    libraryBackupRecovery: { pending: false }, saveRecoveryRequired: false,
    mods: catalog.mods.map((mod) => ({ ...mod, imported: ids.includes(mod.id), enabled: false,
      owned: false, conflictsWithInstalled: [], ...(changes[mod.id] || {}) })),
    settings: { ...fixture.settings, gamePath: 'C:/Synthetic/DaysGone' } });
  const calls = (action) => nativeRequests.filter((request) => request.action === action).length;
  const originalPost = context.window.chrome.webview.postMessage;
  try {
    Object.assign(ui.state, { preview: false, loading: false, busy: false, nativeWindowHidden: false,
      nativeDropActivity: null, downloadDiscoveryChecking: false, profileSession: { active: false },
      downloadOffers: [{ id: 'opaque-download', name: '<NewMod>_P.pak', bytes: 100, kind: 'pak' }] });
    ui.state.snapshot = ui.normalizeSnapshot(makeSnapshot());
    context.document.hidden = false;
    context.document.activeElement = node('body');
    const review = { dataset: { spottedAdd: 'opaque-download' } }, ignore = { dataset: { spottedIgnore: 'opaque-download' } };
    context.document.querySelectorAll = (selector) => selector === '[data-spotted-add]' ? [review]
      : selector === '[data-spotted-ignore]' ? [ignore] : [];
    for (const platform of ['windows', 'linux']) {
      ui.state.snapshot.platform = platform;
      for (const view of ['overview', 'mods', 'downloads', 'settings', 'help']) {
        ui.state.view = view;
        ui.render();
        const markup = node('#main-content').innerHTML;
        assert.equal((markup.match(/class="banner neutral download-offers"/g) || []).length, 1,
          `The ${platform} ${view} page displays exactly one discovery banner`);
        assert(markup.includes('Review &amp; import') && markup.includes('&lt;NewMod&gt;_P.pak'),
          'Discovery explains the review action and escapes the downloaded filename');
        assert.equal(typeof review.onclick, 'function');
        assert.equal(typeof ignore.onclick, 'function', 'Discovery actions remain bound on every page');
      }
    }
    ui.render();
    assert.equal((node('#main-content').innerHTML.match(/download-offers/g) || []).length, 1,
      'Rerendering cannot accumulate discovery banners');
    actionFixtures.set('downloadCandidates', { candidates: [{ id: 'next-download', name: 'Next_P.pak', bytes: 200, kind: 'pak' }] });
    assert(await ui.checkDownloads());
    assert(node('#main-content').innerHTML.includes('Next_P.pak'), 'New discovery updates the current non-library page');
    const beforePoll = calls('downloadCandidates');
    await Promise.all([ui.checkDownloads(), ui.checkDownloads()]);
    assert.equal(calls('downloadCandidates'), beforePoll + 1, 'Concurrent discovery requests share one native poll');
    context.document.hidden = true;
    await ui.checkDownloads();
    assert.equal(calls('downloadCandidates'), beforePoll + 1, 'Hidden windows defer discovery work');
    context.document.hidden = false;

    const imported = makeSnapshot([first.id]);
    actionFixtures.set('downloadImport', imported); actionFixtures.set('snapshot', imported);
    context.window.chrome.webview.postMessage = (request) => {
      if (request.action === 'enable') {
        const snapshot = JSON.parse(JSON.stringify(actionFixtures.get('snapshot')));
        snapshot.mods.find((mod) => mod.id === request.id).enabled = true;
        actionFixtures.set('snapshot', snapshot);
      }
      originalPost(request);
    };
    ui.state.downloadOffers = [{ id: 'opaque-download', name: 'NewMod_P.pak', bytes: 100, kind: 'pak' }];
    const beforeImport = nativeRequests.length, beforeEnable = calls('enable');
    await review.onclick();
    const importRequest = nativeRequests.slice(beforeImport).find((request) => request.action === 'downloadImport');
    assert(importRequest);
    assert.deepEqual(Object.keys(importRequest).sort(), ['action', 'id', 'requestId']);
    assert.equal(importRequest.id, 'opaque-download', 'Discovery imports only the native-issued ID, never a renderer path');
    assert.equal(calls('enable'), beforeEnable + 1, 'A single intended imported PAK goes through the normal enable action automatically');
    assert(ui.state.snapshot.mods.find((mod) => mod.id === first.id).enabled, 'The installed state comes from a fresh native snapshot');

    for (const platform of ['windows', 'linux']) {
      for (const changes of [
        { gameRunning: true }, { gameRunning: null }, { recoveryRequired: true },
        { libraryBackupRecovery: { pending: true } }, { saveRecoveryRequired: true },
        { settings: { ...fixture.settings, gamePath: '' } }
      ]) {
        ui.state.snapshot = ui.normalizeSnapshot({ ...makeSnapshot([first.id]), platform, ...changes });
        const count = calls('enable');
        assert.equal(await ui.enableImportedSelection(first.id, first.sha256, 'download-discovery-ux'), false);
        assert.equal(calls('enable'), count, 'Automatic enabling waits for a closed game, valid location and completed recovery on both systems');
      }
    }
    ui.state.snapshot = ui.normalizeSnapshot(makeSnapshot([first.id]));
    let count = calls('enable');
    assert.equal(await ui.enableImportedSelection(first.id, 'different-hash', 'download-discovery-ux'), false);
    assert.equal(await ui.enableImportedSelection(first.id, first.sha256, 'another-context'), false);
    ui.state.snapshot.mods.find((mod) => mod.id === first.id).enabled = true;
    assert.equal(await ui.enableImportedSelection(first.id, first.sha256, 'download-discovery-ux'), false);
    assert.equal(calls('enable'), count, 'Changed bytes, context and already enabled mods cannot be toggled by stale import intent');

    ui.state.snapshot = ui.normalizeSnapshot(makeSnapshot([first.id]));
    const current = ui.state.snapshot.mods.find((mod) => mod.id === first.id), partner = ui.state.snapshot.mods.find((mod) => mod.id === second.id);
    current.conflictsWithInstalled = [{ id: partner.id, name: partner.name }];
    partner.imported = true; partner.enabled = true;
    assert.equal(await ui.enableImportedSelection(first.id, first.sha256, 'download-discovery-ux'), true);
    assert(node('#modal-content').innerHTML.includes('Switch') && node('#modal-content').innerHTML.includes(partner.name),
      'Automatic enabling retains the reviewed switch when a conflict exists');
    assert.equal(calls('enable'), count, 'A new conflict cannot be silently enabled');
    node('#modal').close();

    for (const outcome of [makeSnapshot([first.id, second.id]),
      makeSnapshot([first.id], { [first.id]: { available: false } })]) {
      ui.state.snapshot = ui.normalizeSnapshot(makeSnapshot());
      actionFixtures.set('snapshot', outcome);
      await ui.showImportResult({ mods: outcome.mods });
      assert.equal(calls('enable'), count, 'Multiple or unsupported imported packages do not get auto-enabled');
    }
    ui.state.snapshot = ui.normalizeSnapshot(makeSnapshot());
    actionFixtures.set('snapshot', makeSnapshot([first.id], { [first.id]: { identityStatus: 'local-import' } }));
    await ui.showImportResult({ mods: actionFixtures.get('snapshot').mods });
    assert.equal(calls('enable'), ++count, 'An approved local single package uses the same native enable flow');

    // A one-package selection from the batch review carries the reviewed hash/context through import.
    const input = { value: 'single-choice', checked: true, disabled: false };
    context.document.querySelectorAll = (selector) => selector === '#modal [name="batch-import-item"]' ? [input] : [];
    for (const platform of ['windows', 'linux']) {
      ui.state.snapshot = ui.normalizeSnapshot({ ...makeSnapshot(), platform });
      actionFixtures.set('snapshot', { ...makeSnapshot([first.id]), platform });
      actionFixtures.set('batchImportConfirm', { batchResult: true, importedCount: 1, failedCount: 0,
        items: [{ itemId: input.value, status: 'imported', name: first.name }] });
      ui.reviewBatchImport({ batchReview: true, batchToken: 'single-pak-' + platform,
        items: [{ itemId: input.value, status: 'ready', name: first.name, mod: first }], totalFiles: 1 });
      assert(node('#batch-import-confirm').innerHTML.includes('Add and enable'), 'A one-package action describes both steps before confirmation');
      await node('#batch-import-confirm').onclick();
      assert.equal(calls('enable'), ++count, 'A single reviewed batch selection automatically enables on both platforms');
      node('#modal').close();
    }
    for (const platform of ['windows', 'linux']) {
      for (const [changes, message] of [
        [{ gameRunning: true }, 'Close Days Gone before enabling it.'],
        [{ settings: { ...fixture.settings, gamePath: '' } }, 'Choose the game folder before enabling it.'],
        [{ saveRecoveryRequired: true }, 'Finish the pending recovery before enabling it.']
      ]) {
        ui.state.snapshot = ui.normalizeSnapshot({ ...makeSnapshot(), platform, ...changes });
        actionFixtures.set('snapshot', { ...makeSnapshot([first.id]), platform, ...changes });
        ui.reviewBatchImport({ batchReview: true, batchToken: 'blocked-single-' + platform,
          items: [{ itemId: input.value, status: 'ready', name: first.name, mod: first }], totalFiles: 1 });
        await node('#batch-import-confirm').onclick();
        const markup = node('#modal-content').innerHTML;
        assert(markup.includes(message) && !markup.includes('The batch does not enable packages.'),
          'An explicit single-package import explains why automatic enabling was deferred');
        assert.equal(calls('enable'), count);
        node('#modal').close();
      }
    }
    context.document.querySelectorAll = originalAll;
    actionFixtures.delete('batchImportConfirm');

    // A failed/expired import review must stop a chained enable or old-version replacement,
    // even if the same package is already cached from another import.
    ui.state.snapshot = ui.normalizeSnapshot(makeSnapshot([first.id]));
    actionErrors.set('confirmReviewedImport', 'This review expired. Select the file again.');
    ui.reviewDownloadedPackage({ reviewRequired: true, reviewToken: 'expired-single', source: 'local-import', mod: first });
    await node('#review-import-enable').onclick();
    assert.equal(calls('enable'), count, 'A failed review cannot enable an already cached matching package');
    node('#modal').close();
    const updated = { ...first, id: 'test-updated-local', sha256: 'c'.repeat(64), version: 'newer', identityStatus: 'local-import',
      imported: true, enabled: false, owned: false };
    ui.state.snapshot = ui.normalizeSnapshot(makeSnapshot([first.id], { [first.id]: { enabled: true } }));
    ui.state.snapshot.mods.push(updated);
    ui.reviewDownloadedPackage({ reviewRequired: true, reviewToken: 'expired-update', source: 'local-import', mod: updated });
    assert(node('#modal-content').innerHTML.includes('id="review-import-replace"'), 'A newer local package offers the existing replacement flow');
    const beforeFailedReplace = nativeRequests.length;
    await node('#review-import-replace').onclick();
    assert(!nativeRequests.slice(beforeFailedReplace).some((request) => ['enable', 'disable', 'switchMod'].includes(request.action)),
      'A failed update review cannot disable the old version or enable an existing cached new version');
    actionErrors.delete('confirmReviewedImport');
    node('#modal').close();
    ui.state.snapshot = ui.normalizeSnapshot(makeSnapshot());
    await ui.showImportResult({ reviewRequired: true, reviewToken: 'opaque-review', source: 'local-import',
      mod: { id: 'unlisted-download', name: 'Unlisted Stone Mod', packageFile: 'Unlisted_P.pak',
        sha256: 'b'.repeat(64), identityStatus: 'local-import', assets: [] } });
    assert(node('#modal-content').innerHTML.includes('Add and enable'),
      'Unknown PAKs retain their explicit inspected Add and enable review');
    node('#modal').close();
    await ui.showImportResult({ batchResult: true, importedCount: 2, failedCount: 0, items: [] });
    assert(node('#modal-content').innerHTML.includes('does not enable packages'), 'Batch alternatives remain disabled after import');
    assert.equal(calls('enable'), count);
  } finally {
    context.window.chrome.webview.postMessage = originalPost;
    actionFixtures.delete('batchImportConfirm');
    actionErrors.delete('confirmReviewedImport');
    for (const action of actions) {
      if (originalFixtures.has(action)) actionFixtures.set(action, originalFixtures.get(action));
      else actionFixtures.delete(action);
    }
    context.document.querySelectorAll = originalAll;
    context.document.hidden = originalHidden; context.document.activeElement = originalActive;
    node('#modal').close(); Object.assign(ui.state, saved);
  }
}

function testLibraryStorageCounts() {
  const saved = { ...ui.state }, supported = catalog.mods.filter((mod) => mod.available !== false);
  try {
    const mods = supported.slice(0, 36).map((mod, index) => ({ ...mod,
      imported: index < 35, enabled: index < 6, owned: index < 6,
      hasBeenEnabled: index < 11, activationHistoryKnown: index < 21,
      status: index < 6 ? 'enabled' : index < 35 ? 'ready' : 'not-imported' }));
    const broken = { ...supported[36], imported: true, enabled: false, owned: true, status: 'changed' };
    for (const platform of ['windows', 'linux']) {
      ui.state.snapshot = ui.normalizeSnapshot({ ...fixture, platform, mods: [...mods, broken] });
      ui.state.filter = 'stored'; ui.state.search = ''; ui.state.category = 'all'; ui.state.favoritesOnly = false;
      const toolbar = ui.libraryToolbar();
      assert(toolbar.includes('Enabled <span>6</span>') && !toolbar.includes('data-filter="disabled"') && toolbar.includes('Stored only <span>29</span>'),
        'All inactive local packages share one stored-only count without duplicating enabled mods');
      assert(toolbar.includes('Every inactive local library package') && !toolbar.includes('Older imports may have no activation history'), 'Stored-only copy describes current use without depending on activation history');
      assert.equal(ui.filteredMods().length, 29, 'Stored filter and its count use the same package set');
      assert.equal(ui.storedMods().length, 29);
      ui.state.filter = 'disabled';
      assert.equal(ui.filteredMods().length, 29, 'An old Disabled filter migrates to Stored only');
      assert.equal(ui.state.filter, 'stored');
      assert.equal(ui.activationLabel(mods[6]), 'Stored only');
      assert.equal(ui.activationLabel(mods[12]), 'Stored only');
      assert.equal(ui.statusFor(mods[12]).note, 'Kept in your library · not loaded by the game');
      assert.equal(ui.statusFor(mods[24]).note, 'Kept in your library · not loaded by the game');
      assert(toolbar.includes('Needs review <span>1</span>'), 'A changed installation record is not described as safely stored');
      ui.state.filter = 'review';
      assert.deepEqual(Array.from(ui.filteredMods(), (mod) => mod.id), [broken.id]);
      ui.openLibraryTools();
      const markup = node('#modal-content').innerHTML;
      assert(markup.includes('6 enabled · 29 stored only') && markup.includes('Share or compare a setup') &&
        markup.includes('Back up your library') && markup.includes('No mod files or private paths are shared'),
        'Library tools separates text sharing from private file backup and explains each task');
      node('#modal').close();
    }
  } finally { Object.assign(ui.state, saved); }
}

async function testBackgroundToolkitUpdates() {
  const saved = { ...ui.state }, originalDate = context.Date, hidden = context.document.hidden;
  let now = originalDate.now();
  context.Date = class extends originalDate { static now() { return now; } };
  const actions = ['snapshot', 'toolkitUpdateCheck', 'updateCheckDownloads'];
  const originalFixtures = new Map(actions.filter((action) => actionFixtures.has(action)).map((action) => [action, actionFixtures.get(action)]));
  const calls = (action) => nativeRequests.filter((request) => request.action === action).length;
  try {
    context.document.hidden = false;
    Object.assign(ui.state, { preview: false, loading: false, busy: false, nativeWindowHidden: false,
      nativeDropActivity: null, profileSession: { active: false }, view: 'overview' });
    for (const platform of ['windows', 'linux']) {
      const next = { ...fixture, platform, edition: 'nexus', appVersion: '1.0.4',
        settings: { ...fixture.settings, autoCheckToolkitUpdates: true } };
      ui.state.snapshot = ui.normalizeSnapshot(next);
      actionFixtures.set('snapshot', next);
      ui.state.update = { status: null, offer: null, lastCheck: 0, checking: false };
      ui.state.toolkitRelease = { status: null, checking: false, nextCheckAt: 0, dismissedVersion: '' };
      actionFixtures.set('toolkitUpdateCheck', { supported: true, status: 'available', available: true,
        version: '1.0.5', retryAfterMs: 900000 });
      actionFixtures.set('updateCheckDownloads', { found: false });
      const remoteBefore = calls('toolkitUpdateCheck'), localBefore = calls('updateCheckDownloads');
      await ui.backgroundCheck();
      assert.equal(calls('toolkitUpdateCheck'), remoteBefore + 1, 'An already-open window checks for online availability');
      assert.equal(calls('updateCheckDownloads'), localBefore + 1, 'The recurring check also detects a newly downloaded signed ZIP');
      const banner = ui.renderToolkitReleaseBanner();
      assert(banner.includes('1.0.5 is out') && banner.includes(platform === 'linux' ? 'Linux ZIP' : 'Windows ZIP'), 'Availability matches the current operating system');
      assert(banner.includes('Open download page') && !banner.includes('data-action="update-install"'), 'Online availability cannot authorize installation');
      const check = nativeRequests.filter((request) => request.action === 'toolkitUpdateCheck').pop();
      assert.deepEqual(Object.keys(check).sort(), ['action', 'requestId'], 'The online bridge accepts no URL or credentials from the renderer');
      now += 60000;
      await ui.backgroundCheck(true);
      assert.equal(calls('toolkitUpdateCheck'), remoteBefore + 1, 'Returning focus and minute ticks do not hammer the metadata endpoint');
      assert.equal(calls('updateCheckDownloads'), localBefore + 1, 'Downloaded ZIP checks retain their five-minute throttle');
      now += 301000;
      node('#modal').showModal();
      await ui.checkForUpdate(true);
      assert.equal(calls('updateCheckDownloads'), localBefore + 1, 'Quiet checks cannot rotate native tokens while an update confirmation is open');
      node('#modal').close();
      await ui.handleAction('toolkit-release-dismiss');
      assert.equal(ui.renderToolkitReleaseBanner(), '', 'Not now hides the notified version');
      now += 901000;
      actionFixtures.set('toolkitUpdateCheck', { supported: true, status: 'available', available: true,
        version: '1.0.6', retryAfterMs: 900000 });
      context.document.hidden = true;
      await ui.backgroundCheck();
      assert.equal(calls('toolkitUpdateCheck'), remoteBefore + 1, 'Hidden windows pause automatic checks');
      context.document.hidden = false;
      await ui.backgroundCheck(true);
      assert.equal(calls('toolkitUpdateCheck'), remoteBefore + 2, 'Returning to the window resumes a due check without restarting');
      assert(ui.renderToolkitReleaseBanner().includes('1.0.6 is out'), 'A later release is shown even after dismissing the previous one');
      ui.state.update.offer = { version: '1.0.6', token: 'only-the-signed-native-local-offer' };
      assert.equal(ui.renderToolkitReleaseBanner(), '', 'A verified local installation offer replaces the online notice');
      ui.state.update.offer = null;
      ui.state.snapshot.settings.autoCheckToolkitUpdates = false;
      ui.state.toolkitRelease.nextCheckAt = 0;
      await ui.checkToolkitRelease();
      assert.equal(calls('toolkitUpdateCheck'), remoteBefore + 2, 'Turning off notifications stops automatic metadata requests');
      assert.equal(ui.renderToolkitReleaseBanner(), '', 'The preference also hides online notification banners');
      now += 901000;
    }
    ui.state.snapshot.settings.autoCheckToolkitUpdates = true;
    ui.state.snapshot.edition = 'local';
    ui.state.toolkitRelease.nextCheckAt = 0;
    const beforeLocal = calls('toolkitUpdateCheck');
    await ui.checkToolkitRelease();
    assert.equal(calls('toolkitUpdateCheck'), beforeLocal, 'Pending-registration public builds never call the Nexus metadata checker');
    assert(ui.renderUpdatePanel().includes('after Nexus approves'), 'Settings explains why online notices are not enabled yet');
    ui.state.snapshot.edition = 'nexus';
    ui.state.toolkitRelease = { status: null, checking: false, nextCheckAt: 0, dismissedVersion: '' };
    const beforeConcurrent = calls('toolkitUpdateCheck');
    await Promise.all([ui.checkToolkitRelease(), ui.checkToolkitRelease()]);
    assert.equal(calls('toolkitUpdateCheck'), beforeConcurrent + 1, 'Only one request can be outstanding');
    ui.state.toolkitRelease.nextCheckAt = 0;
    actionErrors.set('toolkitUpdateCheck', 'Synthetic offline connection');
    const localOffer = { version: '1.0.7', token: 'signed-local-offer' };
    ui.state.update.offer = localOffer;
    await ui.checkToolkitRelease();
    assert.equal(ui.state.update.offer, localOffer, 'An offline metadata check cannot clear a verified local update offer');
    assert.equal(ui.state.toolkitRelease.checking, false, 'Failures release the in-flight guard');
    assert(ui.state.toolkitRelease.nextCheckAt > now, 'Failures wait before retrying');
    for (const version of ['1.0.4', '1.0.3', '1.0.5<script>', '01.0.5', '', null]) {
      assert.equal(ui.newerToolkitVersion(version, '1.0.4'), false, 'Malformed or older versions cannot produce a notice');
    }
  } finally {
    for (const action of actions) {
      if (originalFixtures.has(action)) actionFixtures.set(action, originalFixtures.get(action));
      else actionFixtures.delete(action);
    }
    actionErrors.delete('toolkitUpdateCheck');
    context.Date = originalDate; context.document.hidden = hidden;
    Object.assign(ui.state, saved);
  }
}

async function testActivityDismissal() {
  const saved = { ...ui.state }, originalDate = context.Date, hidden = context.document.hidden, hasFocus = context.document.hasFocus,
    focused = context.document.activeElement, card = node('#operation-activity'), contains = card.contains;
  let now = 1000;
  context.Date = class extends originalDate { static now() { return now; } };
  const tick = (milliseconds = 1000) => {
    const countdown = ui.activityDismissalState(), timer = countdown && timers.get(countdown.timer);
    assert(timer, 'An eligible visible message has one countdown timer');
    timers.delete(countdown.timer); now += milliseconds; timer.callback();
  };
  const finish = (extra = {}) => {
    ui.state.activity = { running: false, outcome: 'complete', message: 'Finished', ...extra };
    ui.renderActivity();
    return ui.state.activity;
  };
  try {
    ui.state.preview = true;
    ui.state.snapshot = { ...saved.snapshot, displayMetrics: { ...saved.snapshot.displayMetrics, windowHidden: false } };
    ui.handleWindowVisibility({ data: { hidden: false } });
    context.document.hidden = false; context.document.activeElement = context.document.body;
    card.contains = (target) => target === card || target === node('#activity-dismiss');
    for (const platform of ['windows', 'linux']) {
      ui.state.snapshot.platform = platform;
      finish();
      assert(card.innerHTML.includes('activity-countdown') && card.innerHTML.includes('aria-hidden="true"'), 'Both platforms show a quiet visual countdown');
      assert.equal(node('#activity-countdown-seconds').textContent, '10');
      const initialAnnouncement = node('#activity-live').textContent;
      assert.equal(initialAnnouncement, 'Finished', 'Generic completion is announced once without duplicate wording');
      for (let second = 9; second >= 1; second--) {
        tick();
        assert.equal(node('#activity-countdown-seconds').textContent, String(second));
        assert.equal(node('#activity-live').textContent, initialAnnouncement, 'Seconds never trigger repeated live announcements');
      }
      tick();
      assert.equal(ui.state.activity, null, 'Ordinary completion disappears after ten elapsed seconds');
      assert.equal(card.hidden, true);
      assert.equal(ui.activityDismissalState(), null, 'Automatic close leaves no timer running');
    }
    finish();
    tick(4000); ui.renderActivity();
    assert.equal(node('#activity-countdown-seconds').textContent, '6', 'Redrawing the same message preserves its remaining time');
    const oldTimer = timers.get(ui.activityDismissalState().timer);
    ui.state.activity = { running: true, message: 'A newer operation' }; ui.renderActivity();
    assert.equal(ui.activityDismissalState(), null, 'A new active operation cancels the old completion timer');
    now += 20000; oldTimer.callback();
    assert.equal(ui.state.activity.message, 'A newer operation', 'A stale callback cannot dismiss a replacement operation');
    finish(); tick(2000); card.onpointerenter();
    assert.equal(ui.activityDismissalState().timer, null, 'Hover pauses timer work');
    now += 60000; ui.syncActivityDismissal();
    assert.equal(node('#activity-countdown-seconds').textContent, '8', 'Hover preserves reading time');
    assert.equal(node('#activity-countdown-ring').style.animationPlayState, 'paused');
    card.onpointerleave(); tick();
    assert.equal(node('#activity-countdown-seconds').textContent, '7', 'Leaving the message resumes the countdown');
    context.document.hasFocus = () => false; ui.syncActivityDismissal();
    assert.equal(timers.get(ui.activityDismissalState().timer).delay, 1000, 'A visible unfocused window keeps its numeric countdown current');
    now += 3000; context.document.hasFocus = () => true; ui.syncActivityDismissal();
    assert.equal(node('#activity-countdown-seconds').textContent, '4', 'Returning shows the time that actually remains');
    context.document.activeElement = node('#activity-dismiss'); card.listeners.get('focusin')();
    assert.equal(ui.activityDismissalState().timer, null, 'Keyboard focus also pauses automatic dismissal');
    now += 60000; ui.syncActivityDismissal();
    assert.equal(node('#activity-countdown-seconds').textContent, '4');
    context.document.activeElement = context.document.body; card.listeners.get('focusout')(); await Promise.resolve();
    tick(); assert.equal(node('#activity-countdown-seconds').textContent, '3');
    ui.handleWindowVisibility({ data: { hidden: true } });
    assert.equal(timers.get(ui.activityDismissalState().timer).delay, 3000, 'Native minimize/hide retains one expiry timeout');
    now += 1000;
    ui.handleWindowVisibility({ data: { hidden: false } });
    assert.equal(node('#activity-countdown-seconds').textContent, '2', 'Restoring the window accounts for minimized time');
    tick();
    ui.showChangelog();
    assert.equal(ui.activityDismissalState().timer, null, 'A dialog pauses the message behind it');
    now += 60000; node('#modal').close(); ui.handleModalClosed();
    assert.equal(node('#activity-countdown-seconds').textContent, '1', 'Closing the dialog resumes the intentionally paused countdown');
    context.document.activeElement = node('#activity-dismiss'); card.listeners.get('focusin')();
    node('#activity-dismiss').onclick();
    assert.equal(ui.state.activity, null); assert.equal(ui.activityDismissalState(), null);
    assert.equal(context.document.activeElement, node('#main-content'), 'Manual keyboard dismissal leaves focus in the main view');
    finish({ outcome: 'cancelled', message: 'Cancelled' });
    assert(ui.activityDismissalState(), 'Ordinary cancellations can expire too');
    tick(3000);
    context.document.documentElement.classList.add('reduce-motion'); ui.syncActivityDismissal();
    tick(2000);
    context.document.documentElement.classList.remove('reduce-motion'); ui.syncActivityDismissal();
    assert.equal(node('#activity-countdown-ring').style.animationDelay, '-5000ms', 'Changing motion preferences keeps the visual ring aligned with remaining time');
    for (const result of [
      { batchReview: true }, { choosePackages: true }, { reviewRequired: true }, { review: { reviewRequired: true } },
      { recoveryRequired: true }, { completed: false }, { failedCount: 1 },
      { batchResult: true, importedCount: 0, failedCount: 0, attentionCount: 1 }
    ]) {
      assert.equal(ui.activityNeedsAttention(result), true);
      finish({ requiresAttention: ui.activityNeedsAttention(result) });
      assert.equal(ui.activityDismissalState(), null, 'Review, incomplete and recovery messages stay visible');
      assert(!card.innerHTML.includes('id="activity-countdown"'));
    }
    finish({ outcome: 'failed', message: 'The operation failed' });
    assert.equal(ui.activityDismissalState(), null, 'Errors never dismiss themselves');
    ui.handleNativeOperationProgress({ requestId: 'drop-import', phase: 'checking', completed: 0, total: 1 });
    ui.handleDroppedImport({ ok: true, data: { batchReview: true, batchToken: 'countdown-review', items: [] } });
    assert.equal(ui.state.activity.requiresAttention, true, 'Native Linux drop reviews are also kept visible');
    assert.equal(ui.activityDismissalState(), null);
    const styles = fs.readFileSync(path.join(appRoot, 'ui/style.css'), 'utf8');
    assert(styles.includes('@keyframes activity-dismiss-drain') && styles.includes('stroke-dashoffset: var(--activity-countdown-offset'), 'Ring has smooth drain with a stepped fallback when animations are disabled');
    assert(styles.includes('@media (forced-colors: active)') && styles.includes('.activity-countdown { color: CanvasText; }'), 'The timer remains visible in forced colors');
  } finally {
    node('#modal').close(); ui.discardBatchReview();
    ui.state.activity = null; ui.renderActivity();
    card.contains = contains; context.Date = originalDate;
    context.document.hidden = hidden; context.document.activeElement = focused; context.document.hasFocus = hasFocus;
    Object.assign(ui.state, saved);
    ui.handleWindowVisibility({ data: { hidden: false } });
  }
}

async function testLiveGameStatusAndSaveFiles() {
  const saved = { snapshot: ui.state.snapshot, loading: ui.state.loading, preview: ui.state.preview,
    view: ui.state.view, diagnosticStale: ui.state.diagnosticStale, saveBackups: ui.state.saveBackups, savePictures: ui.state.savePictures };
  try {
    for (const platform of ['windows', 'linux']) {
      ui.state.snapshot = { ...ui.normalizeSnapshot(fixture), platform, accountContext: 'live-game',
        activePath: '/synthetic/Saved/Paks', gameRunning: false, settings: { gamePath: '/synthetic/game' } };
      ui.state.loading = false; ui.state.preview = false; ui.state.view = 'mods';
      const data = { accountContext: 'live-game', activePath: '/synthetic/Saved/Paks',
        gamePath: '/synthetic/game', processKnown: true, gameRunning: true };
      const before = nativeRequests.length;
      nativeMessageListener({ data: { event: 'gameProcessChanged', data } });
      assert.equal(ui.state.snapshot.gameRunning, true, platform + ' native process event updates state immediately');
      assert(node('#launch-button').innerHTML.includes('Queue restart'), 'Launch control changes without replacing the current page');
      assert.equal(nativeRequests.length, before, 'Running event does not request a PAK scan');
      assert.equal(ui.handleGameProcessChanged({ data: { ...data, gamePath: '/other-game', gameRunning: false } }), false,
        'Old installation event cannot replace current process status');
      assert.equal(ui.handleGameProcessChanged({ data: { ...data, accountContext: 'old-context', gameRunning: false } }), false,
        'Old account context event is rejected');
      assert.equal(ui.handleGameProcessChanged({ data }), false, 'Repeated unchanged event is ignored');
      ui.handleGameProcessChanged({ data: { ...data, processKnown: false, gameRunning: null } });
      assert.equal(ui.state.snapshot.gameRunning, null, 'Unreadable process list remains unknown');
      assert.equal(ui.playReadiness().message, 'Game status needs checking');
      ui.handleGameProcessChanged({ data: { ...data, gameRunning: false } });
      assert.equal(ui.state.snapshot.gameRunning, false, 'Confirmed exit appears without waiting for minute scan');
      assert(node('#launch-button').innerHTML.includes('Launch game'));
    }
    actionFixtures.set('saveFilesList', { files: [{ name: '<slot>.sav', bytes: 4, modifiedAt: 1792502768 }], fileCount: 1, bytes: 4, current: true });
    await ui.showSaveFiles();
    assert(node('#modal-content').innerHTML.includes('&lt;slot&gt;.sav'), 'Save filenames are escaped');
    assert(node('#modal-content').innerHTML.includes('Nothing is changed here'), 'Read-only save browser states its scope');
    actionFixtures.set('saveBackupFiles', { id: 'native-backup', files: [{ name: 'slot.sav', bytes: 4 }], fileCount: 1, bytes: 4 });
    await ui.showSaveFiles('native-backup');
    const request = nativeRequests.filter((request) => request.action === 'saveBackupFiles').at(-1);
    assert.equal(request.id, 'native-backup');
    assert(!('path' in request), 'Save browser sends only native backup IDs');
    const id = 'a'.repeat(64), url = 'data:image/png;base64,AAAA';
    ui.state.savePictures = new Map();
    ui.state.saveBackups = { backups: [{ id, createdAt: 1792502768, files: 1, bytes: 4, pictureAvailable: false }] };
    assert.equal(ui.cleanSavePicture({ available: true, dataUrl: 'file:///private-picture.png' }), '', 'Raw local image paths are never rendered');
    assert.equal(ui.cleanSavePicture({ available: true, dataUrl: 'data:image/svg+xml;base64,AAAA' }), '', 'Only native PNG data is accepted');
    actionFixtures.set('saveBackupPictureBrowse', { id, available: true, dataUrl: url });
    await ui.showSavePicture(id);
    assert(node('#modal-content').innerHTML.includes('metadata removed'), 'Picture picker explains private, sanitised copy');
    await node('#save-picture-choose').onclick();
    const pick = nativeRequests.filter((request) => request.action === 'saveBackupPictureBrowse').at(-1);
    assert.equal(pick.id, id); assert(!('path' in pick), 'Picture chooser asks native OS picker without a caller path');
    assert(ui.saveGalleryPicture({ id }).includes('<img'), 'Saved native preview appears in the backup gallery');
    actionFixtures.set('saveBackupPictureRemove', { id, available: false, removed: true });
    await node('#save-picture-remove').onclick();
    assert(!ui.state.savePictures.has(id), 'Removing picture clears its preview without a save restore');
    for (let n = 0; n < 30; n++) ui.rememberSavePicture(String(n), { available: true, dataUrl: url });
    assert.equal(ui.state.savePictures.size, 24, 'Visible-gallery picture cache has a fixed entry cap');
    for (let n = 0; n < 10; n++) ui.rememberSavePicture(String(n), { available: true, dataUrl: 'data:image/png;base64,' + 'A'.repeat(2400000) });
    assert([...ui.state.savePictures.values()].reduce((bytes, picture) => bytes + picture.length, 0) <= 16 * 1024 * 1024,
      'Large backup pictures remain within the shared thumbnail byte cap');
  } finally {
    actionFixtures.delete('saveFilesList'); actionFixtures.delete('saveBackupFiles');
    actionFixtures.delete('saveBackupPictureBrowse'); actionFixtures.delete('saveBackupPictureRemove');
    Object.assign(ui.state, saved); node('#modal').close();
  }
}

async function testSavesSidebarPage() {
  const saved = { ...ui.state }, previousList = actionFixtures.get('saveBackupsList'),
    oldQueries = context.document.querySelectorAll, hidden = context.document.hidden;
  const list = { available: true, settings: { automatic: false, keep: 5 }, recovery: { pending: false },
    backups: [{ id: 'saves-page-fixture', createdAt: 1792502768, files: 3, bytes: 120, reason: 'manual' }] };
  const calls = () => nativeRequests.filter((request) => request.action === 'saveBackupsList').length;
  const settle = () => new Promise(setImmediate);
  try {
    const index = fs.readFileSync(path.join(appRoot, 'ui/index.html'), 'utf8');
    assert(index.includes('data-view="saves"') && index.includes('data-icon="save"'), 'Saves has a visible sidebar destination');
    actionFixtures.set('saveBackupsList', list);
    Object.assign(ui.state, { preview: false, loading: false, busy: false, saveBackups: null,
      saveBackupsLoading: false, profileSession: { active: false }, gameRestart: { pending: false } });
    context.document.hidden = false;
    for (const platform of ['windows', 'linux']) {
      ui.state.snapshot = ui.normalizeSnapshot({ ...fixture, platform, accountContext: `saves-page-${platform}`,
        gameRunning: false, activePath: '/synthetic/Saved/Paks', libraryBackupRecovery: { pending: false },
        settings: { ...fixture.settings, gamePath: '/synthetic/<selected>' } });
      const beforeSettings = calls();
      ui.navigate('settings'); await settle();
      assert.equal(calls(), beforeSettings, 'Settings no longer reads save backups merely to render');
      assert(ui.renderSettings().includes('data-view="saves"'), 'Settings retains a discoverable shortcut');
      assert(!ui.renderSettings().includes('id="save-backups-panel"'), 'Settings does not duplicate manager controls or IDs');
      ui.state.saveBackups = null;
      await ui.handleAction('save-backups'); await settle();
      assert.equal(ui.state.view, 'saves', 'Save notices and recovery shortcuts open the dedicated page');
      assert.equal(calls(), beforeSettings + 1, 'Entering Saves loads one current backup inventory');
      const output = ui.renderSaves();
      assert(output.includes('Your game saves') && output.includes('&lt;selected&gt;'), 'Saves identifies the installation with escaped text');
      assert(output.includes('data-action="backup-saves"') && output.includes('data-action="save-backups-refresh"'), 'Primary backup and refresh actions remain available');
      assert(output.includes('Saved backups') && output.includes('data-backup-restore="saves-page-fixture"'), 'Backups and reviewed restore are visible');
      assert(output.includes('class="sg-gallery"') && output.includes('Automatic backup preferences'), 'Backups stay visible while existing automatic preferences have a compact disclosure');
      assert(output.includes('save-backup-keep') && output.includes('Automatic save backups'), 'Backup preferences move with their manager');
      const backupControl = node('saves-native-backup-control');
      context.document.querySelectorAll = (selector) => selector === '#main-content [data-action="backup-saves"], #main-content [data-backup-restore]'
        ? [backupControl] : oldQueries(selector);
      const scope = { accountContext: ui.state.snapshot.accountContext, activePath: '/synthetic/Saved/Paks',
        gamePath: '/synthetic/<selected>', processKnown: true };
      ui.handleGameProcessChanged({ data: { ...scope, gameRunning: true } });
      assert(node('#saves-game-status').innerHTML.includes('Days Gone is running'), 'Running-game notice updates without repainting the whole save page');
      assert.equal(backupControl.disabled, true, 'Backup and restore controls pause while the game runs');
      const beforeExit = calls();
      ui.handleGameProcessChanged({ data: { ...scope, gameRunning: false } }); await settle();
      assert.equal(calls(), beforeExit + 1, 'Game exit refreshes the Saves page inventory');
      assert.equal(backupControl.disabled, false, 'Closing the game restores save actions');
      ui.state.snapshot.settings.gamePath = ''; ui.state.snapshot.platform = 'linux';
      const unselected = ui.renderSaves();
      assert(unselected.includes('Proton saves') && !unselected.includes('%LOCALAPPDATA%'), 'Unselected Linux installation is explained without a Windows fallback');
      ui.state.snapshot.libraryBackupRecovery = { pending: true };
      const recovery = ui.renderSaves();
      assert(recovery.includes('Review library recovery') && !recovery.includes('data-action="backup-saves"'), 'Interrupted library recovery has a clear route without offering blocked save changes');
      ui.state.saveBackups = null;
    }
  } finally {
    if (previousList === undefined) actionFixtures.delete('saveBackupsList'); else actionFixtures.set('saveBackupsList', previousList);
    context.document.querySelectorAll = oldQueries; context.document.hidden = hidden;
    Object.assign(ui.state, saved); node('#modal').close();
  }
}

async function testSaveGalleryAndRestorePresentation() {
  const saved = { ...ui.state }, hidden = context.document.hidden;
  const actions = ['saveBackupsList', 'saveBackupDetails', 'saveBackupReview', 'saveBackupFiles', 'saveBackupRestore', 'snapshot'];
  const previous = new Map(actions.filter((action) => actionFixtures.has(action)).map((action) => [action, actionFixtures.get(action)]));
  const calls = (action) => nativeRequests.filter((request) => request.action === action).length;
  const first = { id: 'gallery-first', createdAt: 1792502768, reason: 'before-enabling-mods', files: 3, bytes: 4000, label: 'Before the <cave>', note: '', pinned: true },
    second = { id: 'gallery-second', createdAt: 1792502760, reason: 'manual', files: 2, bytes: 3000, label: '', note: '', pinned: false };
  const makeData = () => ({ available: true, settings: { automatic: true, keep: 5 }, recovery: { pending: false }, backups: [{ ...first }, { ...second }] });
  try {
    Object.assign(ui.state, { preview: false, loading: false, busy: false, view: 'saves', savesTab: 'backups', saveBackupsLoading: false,
      savePictures: new Map(), saveDetailsDrafts: new Map(), saveSelection: null, saveOptionsOpen: false, nativeDropActivity: null });
    context.document.hidden = false;
    for (const platform of ['windows', 'linux']) {
      const snapshot = { ...fixture, platform, accountContext: 'gallery-' + platform, gameRunning: false,
        libraryBackupRecovery: { pending: false }, settings: { ...fixture.settings, gamePath: '/synthetic/game' } };
      ui.state.snapshot = ui.normalizeSnapshot(snapshot); ui.state.saveBackups = makeData();
      actionFixtures.set('snapshot', snapshot); actionFixtures.set('saveBackupsList', ui.state.saveBackups);
      const output = ui.renderSaveBackupsPanel();
      assert.equal((output.match(/data-save-select=/g) || []).length, 2, 'Every real backup has one gallery card');
      assert(output.includes('Before the &lt;cave&gt;') && !output.includes('Before the <cave>'), 'Custom backup names are escaped');
      assert(output.includes('aria-pressed="true"') && output.includes('Leave it out of automatic cleanup.'), 'Initial backup selection and keep control are clear');
      assert.equal(ui.selectedSaveBackup().id, first.id, 'The first available backup is selected initially');
      assert(ui.selectSaveBackup(second.id), 'Selecting a real backup opens its existing actions');
      assert.equal(ui.selectedSaveBackup().id, second.id, 'Selection uses the backup native ID');
      assert.equal(ui.selectSaveBackup('../missing'), false, 'Unknown gallery selections never become native requests');
      const draft = ui.saveDetailsDraft(ui.selectedSaveBackup());
      Object.assign(draft, { label: 'A private <label>', note: 'Remember <this> setup', pinned: true, dirty: true });
      ui.state.saveBackups = makeData();
      const draftMarkup = ui.renderSaveBackupsPanel();
      assert(draftMarkup.includes('value="A private &lt;label&gt;"') && draftMarkup.includes('Remember &lt;this&gt; setup'), 'Draft details survive background gallery redraw and remain escaped');
      ui.selectSaveBackup(first.id); ui.selectSaveBackup(second.id);
      assert.equal(ui.saveDetailsDraft(ui.selectedSaveBackup()).note, 'Remember <this> setup', 'Changing selection preserves unsaved notes for that backup');
      actionFixtures.set('saveBackupDetails', { saved: true });
      const changed = makeData(); Object.assign(changed.backups[1], { label: draft.label, note: draft.note, pinned: true });
      actionFixtures.set('saveBackupsList', changed);
      ui.state.snapshot.gameRunning = true;
      const beforeDetails = calls('saveBackupDetails');
      assert(await ui.saveSelectedBackupDetails(), 'Private details can be saved while the game runs');
      assert.equal(calls('saveBackupDetails'), beforeDetails + 1);
      const details = nativeRequests.filter((request) => request.action === 'saveBackupDetails').at(-1);
      assert.equal(details.id, second.id); assert.equal(details.label, 'A private <label>'); assert.equal(details.pinned, true);
      assert(!('path' in details) && !('files' in details), 'Private details use one native backup ID and metadata only');
      assert.equal(ui.selectedSaveBackup().id, second.id, 'Reloading details preserves the selected backup');
      const pendingDraft = ui.saveDetailsDraft(ui.selectedSaveBackup());
      Object.assign(pendingDraft, { label: 'Submitted name', dirty: true });
      const inFlight = ui.saveSelectedBackupDetails();
      pendingDraft.label = 'Newer unsaved name'; pendingDraft.dirty = true;
      assert(await inFlight);
      assert.equal(ui.saveDetailsDraft(ui.selectedSaveBackup()).label, 'Newer unsaved name', 'Typing while a details save is in flight is preserved');
      assert.equal(ui.saveDetailsDraft(ui.selectedSaveBackup()).dirty, true, 'New edits are not falsely marked saved by an older response');
      ui.state.saveBackups.backups[1].detailsEditable = false;
      ui.state.saveBackups.backups[1].detailsProblem = 'Changed <private record> was preserved';
      Object.assign(ui.saveDetailsDraft(ui.selectedSaveBackup()), { dirty: true });
      const blockedStart = calls('saveBackupDetails');
      assert.equal(await ui.saveSelectedBackupDetails(), false, 'Changed annotation records cannot be blindly overwritten');
      assert.equal(calls('saveBackupDetails'), blockedStart);
      assert(ui.renderSaveBackupsPanel().includes('Changed &lt;private record&gt;'), 'Annotation warning is escaped and visible');
      ui.state.saveBackups = makeData(); ui.state.snapshot.gameRunning = false;
      actionFixtures.set('saveBackupsList', ui.state.saveBackups);
      actionFixtures.set('saveBackupReview', { token: 'gallery-reviewed-token', id: second.id, summary: { files: 2, bytes: 3000, currentFiles: 3, currentBytes: 5000 }, warnings: ['Native <warning>'] });
      actionFixtures.set('saveBackupFiles', { id: second.id, files: [{ name: '<save>.sav', bytes: 3000 }], fileCount: 1 });
      actionFixtures.set('saveBackupRestore', { restored: true });
      const beforeRestore = calls('saveBackupRestore');
      assert(await ui.reviewSaveBackup(second.id));
      const reviewMarkup = node('#modal-content').innerHTML;
      assert(reviewMarkup.includes('Keep your current saves') && reviewMarkup.includes('3 current files'), 'Review explains the native safety copy with real current counts');
      assert(reviewMarkup.includes('&lt;save&gt;.sav') && reviewMarkup.includes('Native &lt;warning&gt;'), 'Native file names and warnings are escaped');
      assert.equal(calls('saveBackupRestore'), beforeRestore, 'Gallery review itself never changes save files');
      ui.state.snapshot.gameRunning = true; ui.updateSaveRestoreStatus();
      assert.equal(node('#confirm-action').disabled, true, 'A game start blocks the open review');
      await node('#confirm-action').onclick();
      assert.equal(calls('saveBackupRestore'), beforeRestore, 'Blocked confirmation never reaches native restoration');
      ui.state.snapshot.gameRunning = null; ui.updateSaveRestoreStatus();
      assert(node('#save-restore-game-state').textContent.includes('not confirmed'), 'Unknown game status is not labelled closed');
      ui.state.snapshot.gameRunning = false; ui.updateSaveRestoreStatus();
      assert.equal(node('#confirm-action').disabled, false, 'A confirmed exit re-enables the same reviewed token');
      ui.state.snapshot.accountContext = 'changed-installation';
      await node('#confirm-action').onclick();
      assert.equal(calls('saveBackupRestore'), beforeRestore, 'Changing installation invalidates the old review UI');
      node('#modal').close();
      ui.state.snapshot = ui.normalizeSnapshot(snapshot);
      assert(await ui.reviewSaveBackup(second.id));
      await node('#confirm-action').onclick();
      assert.equal(calls('saveBackupRestore'), beforeRestore + 1, 'Explicit confirmed restore reaches the existing native action once');
      const restore = nativeRequests.filter((request) => request.action === 'saveBackupRestore').at(-1);
      assert.equal(restore.token, 'gallery-reviewed-token'); assert.equal(restore.confirmed, true);
      assert(!('id' in restore) && !('path' in restore) && !('files' in restore), 'Restoration uses only its native reviewed token');
      ui.state.saveBackups = { ...makeData(), backups: [first] };
      assert.equal(ui.selectedSaveBackup().id, first.id, 'A removed backup falls back to an available selection');
    }
  } finally {
    for (const action of actions) { if (previous.has(action)) actionFixtures.set(action, previous.get(action)); else actionFixtures.delete(action); }
    context.document.hidden = hidden; Object.assign(ui.state, saved); node('#modal').close();
  }
}

async function testCurrentSavesWithoutBackups() {
  const saved = { ...ui.state }, hidden = context.document.hidden;
  const actions = ['currentSavesList', 'currentSavePreview'];
  const previous = new Map(actions.filter((action) => actionFixtures.has(action)).map((action) => [action, actionFixtures.get(action)]));
  const calls = (action) => nativeRequests.filter((request) => request.action === action).length;
  const settle = () => new Promise(setImmediate);
  const png = 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aGxkAAAAASUVORK5CYII=';
  try {
    node('#modal').close(); context.document.hidden = false;
    for (const platform of ['windows', 'linux']) {
      const first = { id: 'current-opaque-a', name: '<Autosave>.sav', size: 5120, modifiedUtc: '2026-10-25T15:00:00Z', previewEligible: true },
        second = { id: 'current-opaque-b', name: 'Manual.sav', size: 2048, modifiedUtc: '2026-10-25T14:00:00Z', previewEligible: false },
        data = { status: 'ready', context: `current-root-${platform}`, signature: 'current-first', saves: [first, second] };
      Object.assign(ui.state, { preview: false, loading: false, busy: false, view: 'saves', savesTab: 'current',
        currentSaves: null, currentSavesLoading: false, currentSaveSelection: null, currentSavePreviews: new Map(),
        saveBackups: null, nativeWindowHidden: false, nativeDropActivity: null, profileSession: { active: false },
        snapshot: ui.normalizeSnapshot({ ...fixture, platform, accountContext: `live-saves-${platform}`, gameRunning: false,
          saveLocation: { resolved: true }, libraryBackupRecovery: { pending: false } }) });
      actionFixtures.set('currentSavesList', data);
      actionFixtures.set('currentSavePreview', { id: first.id, context: data.context, available: true, actualGamePreview: true, dataUrl: png });
      const beforeBackup = calls('saveBackupsList'), beforeList = calls('currentSavesList');
      const firstRead = ui.loadCurrentSaves(); const duplicateRead = ui.loadCurrentSaves();
      assert.equal(await duplicateRead, false, 'Current save reads are singleflight');
      assert(await firstRead); await settle();
      assert.equal(calls('currentSavesList'), beforeList + 1);
      assert.equal(calls('saveBackupsList'), beforeBackup, 'Current saves do not require or enumerate backups');
      const output = ui.renderSaves();
      assert(output.includes('data-saves-tab="current"') && output.includes('data-saves-tab="backups"'), 'Current files and backups have separate familiar tabs');
      assert(output.includes('&lt;Autosave&gt;.sav') && output.includes('Manual.sav') && output.includes('5 KB'), 'Current cards show escaped native names and actual file sizes');
      assert(output.includes('In-game save preview') && output.includes('No in-game picture available'), 'Verified native pictures and files without an eligible picture are distinguished');
      assert(!output.includes('data-backup-restore=') && !output.includes('data-backup-picture='), 'Current files offer neither raw restoration nor manual preview substitution');
      ui.state.currentSaves = { ...data, previewsSupported: false };
      const filesOnly = ui.renderCurrentSavesPanel();
      assert(filesOnly.includes('sg-current-files-only') && filesOnly.includes('Manual.sav'), 'The public build keeps current-file browsing');
      assert(!filesOnly.includes('data-current-save-open=') && !filesOnly.includes('data-current-preview=') && !filesOnly.includes('Preview unavailable'), 'A build without the reader has no broken photo controls');
      ui.state.currentSaves = { ...data, limited: true, skipped: 2 };
      const incomplete = ui.renderCurrentSavesPanel();
      assert(incomplete.includes('list may be incomplete') && incomplete.includes('Some save entries could not be inspected safely'), 'Native bounds and skipped entries are visible without claiming corrupt save data');
      ui.state.currentSaves = data;
      assert.equal(ui.state.currentSavePreviews.get(ui.currentSavePreviewKey(first.id)).url, png);
      const panel = node('#current-saves-panel'); panel.outerHTML = 'unchanged-inventory';
      const beforePreview = calls('currentSavePreview');
      await ui.loadCurrentSaves(); await settle();
      assert.equal(panel.outerHTML, 'unchanged-inventory', 'An unchanged inventory never replaces its DOM');
      assert.equal(calls('currentSavePreview'), beforePreview, 'Unchanged saves reuse their verified image cache');
      ui.syncCurrentSavesPoll();
      const poll = ui.currentSavesPollState().timer;
      assert.equal(timers.get(poll).delay, 3000, 'Visible Current saves poll every three seconds');
      ui.state.busy = true; const whileBusy = calls('currentSavesList');
      await timers.get(poll).callback(); assert.equal(calls('currentSavesList'), whileBusy, 'A busy loader defers live save reads');
      ui.state.busy = false;
      for (const blocked of ['profile', 'recovery', 'hidden', 'minimized', 'tab', 'page', 'preview']) {
        ui.state.profileSession.active = blocked === 'profile';
        ui.state.snapshot.saveRecoveryRequired = blocked === 'recovery';
        context.document.hidden = blocked === 'hidden'; ui.state.nativeWindowHidden = blocked === 'minimized';
        ui.state.savesTab = blocked === 'tab' ? 'backups' : 'current'; ui.state.view = blocked === 'page' ? 'mods' : 'saves';
        ui.state.preview = blocked === 'preview';
        const before = calls('currentSavesList');
        assert.equal(await ui.loadCurrentSaves(), false, `${blocked} defers a native save read`);
        assert.equal(calls('currentSavesList'), before);
        ui.syncCurrentSavesPoll();
        if (['hidden', 'minimized', 'tab', 'page', 'preview'].includes(blocked)) assert.equal(ui.currentSavesPollState().timer, null, 'Invisible Current saves stop their timer');
      }
      Object.assign(ui.state, { preview: false, nativeWindowHidden: false, savesTab: 'current', view: 'saves' });
      ui.state.profileSession.active = false; ui.state.snapshot.saveRecoveryRequired = false; context.document.hidden = false;
      const next = { ...data, signature: 'new-file-identity', saves: [{ ...first, id: 'current-opaque-new', previewEligible: true }] };
      actionFixtures.set('currentSavesList', next);
      actionFixtures.set('currentSavePreview', { id: next.saves[0].id, context: data.context, available: true, dataUrl: png });
      await ui.loadCurrentSaves(); await settle();
      assert(!ui.state.currentSavePreviews.has(ui.currentSavePreviewKey(first.id)), 'Changed opaque file identities discard the old preview');
      assert.equal(ui.state.currentSavePreviews.get(ui.currentSavePreviewKey(next.saves[0].id)).url, '', 'An image without the authentic game-preview verdict is never shown');
      assert(ui.renderCurrentSavesPanel().includes('Preview unavailable'));
      ui.state.currentSavePreviews.clear();
      actionFixtures.set('currentSavePreview', { id: next.saves[0].id, context: data.context, available: true, actualGamePreview: true, dataUrl: png });
      const staleRead = ui.loadCurrentSavePreviews();
      ui.state.currentSaves = { ...next, context: 'changed-native-root' };
      await staleRead;
      assert.equal(ui.state.currentSavePreviews.size, 0, 'A changed native save root rejects an in-flight preview');
      const entries = Array.from({ length: 10 }, (_, index) => ({ ...first, id: `cache-${index}`, name: `Save-${index}.sav` }));
      ui.state.currentSaves = { ...data, signature: 'cache', saves: entries };
      const lazyBefore = calls('currentSavePreview');
      actionFixtures.set('currentSavePreview', { available: false, id: entries[0].id, context: data.context });
      await ui.loadCurrentSavePreviews();
      assert(calls('currentSavePreview') - lazyBefore <= 3, 'Only the selected save and two initial previews are requested');
      for (const entry of entries) {
        ui.state.currentSaveSelection = { context: data.context, id: entry.id };
        actionFixtures.set('currentSavePreview', { available: true, actualGamePreview: true, id: entry.id, context: data.context, dataUrl: png });
        await ui.loadCurrentSavePreviews();
      }
      assert(ui.state.currentSavePreviews.size <= 24, 'Current save preview retention has a fixed entry cap');
      assert([...ui.state.currentSavePreviews.values()].reduce((bytes, picture) => bytes + picture.url.length, 0) <= 16 * 1024 * 1024,
        'Current save pictures respect the shared thumbnail byte cap');
      const notes = new Map([['a', { context: ui.state.snapshot.accountContext, label: 'Unsaved', note: 'Private draft', dirty: true }]]);
      ui.state.saveDetailsDrafts = notes; ui.state.saveSelection = { context: ui.state.snapshot.accountContext, id: 'a' };
      ui.state.savesTab = 'backups'; ui.render(); ui.state.savesTab = 'current'; ui.render();
      assert.equal(ui.state.saveDetailsDrafts.get('a').note, 'Private draft', 'Switching tabs never clears private backup drafts');
      assert.equal(ui.state.saveSelection.id, 'a', 'Current save selection is independent of backup selection');
    }
  } finally {
    for (const action of actions) { if (previous.has(action)) actionFixtures.set(action, previous.get(action)); else actionFixtures.delete(action); }
    context.document.hidden = hidden; Object.assign(ui.state, saved); ui.syncCurrentSavesPoll(); node('#modal').close();
  }
}

async function testProfileLinkedBackups() {
  const saved = { ...ui.state }, hidden = context.document.hidden, append = node('#toasts').append, watch = ui.watchState();
  const actions = ['saveBackupsList', 'profilesList'];
  const previous = new Map(actions.filter((action) => actionFixtures.has(action)).map((action) => [action, actionFixtures.get(action)]));
  const id = 'a'.repeat(32), other = 'b'.repeat(32), empty = 'c'.repeat(32), notices = [];
  const before = { id, name: 'Fog <run>', stage: 'before-launch' }, after = { id, name: 'Fog <run>', stage: 'after-session' };
  const data = { available: true, settings: { automatic: false, keep: 5 }, recovery: { pending: false }, backups: [
    { id: 'linked-copy', createdAt: 1792502768, reason: 'before-profile', files: 2, bytes: 900, profiles: [before, before, after] },
    { id: 'other-copy', createdAt: 1792502760, reason: 'after-profile', files: 2, bytes: 900, profiles: [{ id: other, name: 'Other setup', stage: 'after-session' }] },
    { id: 'unknown-links', createdAt: 1792502750, reason: 'manual', files: 2, bytes: 900, profiles: [before], profileLinksProblem: true }
  ] };
  const settle = () => new Promise(setImmediate);
  try {
    context.document.hidden = false; node('#modal').close();
    node('#toasts').append = (element) => notices.push({ markup: element.innerHTML, className: element.className });
    for (const platform of ['windows', 'linux']) {
      const profiles = { session: { active: false }, profiles: [
        { id, name: before.name, status: 'ready', mods: [], count: 0, bytes: 0 },
        { id: empty, name: 'No linked copies', status: 'ready', mods: [], count: 0, bytes: 0 },
        { id: '../invalid', name: 'Unchecked profile', status: 'ready', mods: [], count: 0, bytes: 0 }
      ] };
      Object.assign(ui.state, { preview: false, loading: false, busy: false, view: 'saves', savesTab: 'backups', saveProfileFilter: null,
        saveBackups: data, saveBackupsLoading: false, savePictures: new Map(), saveSelection: null, saveDetailsDrafts: new Map(),
        profileSession: { active: false }, portableProfiles: profiles, nativeDropActivity: null, profileSearch: '',
        snapshot: ui.normalizeSnapshot({ ...fixture, platform, accountContext: 'profile-links-' + platform, gameRunning: false, libraryBackupRecovery: { pending: false } }) });
      actionFixtures.set('saveBackupsList', data); actionFixtures.set('profilesList', profiles);
      assert.equal(ui.saveBackupProfileLinks(data.backups[0]).length, 2, 'Duplicate stage/profile records are grouped');
      assert.equal(ui.saveBackupProfileLinks(data.backups[2]).length, 0, 'Unverified links never become trusted tags or filter matches');
      assert.equal(ui.saveBackupProfiles().length, 2, 'The filter contains unique native-linked profile IDs');
      let output = ui.renderSaveBackupsPanel();
      assert(output.includes('All profiles') && output.includes('Before launching Fog &lt;run&gt;') && output.includes('After Fog &lt;run&gt; session'), 'Before/after labels use escaped native names without claiming future save ownership');
      assert(output.includes('Profile links could not be verified.'), 'Unknown private link metadata has an honest warning');
      ui.state.saveSelection = { context: ui.state.snapshot.accountContext, id: 'linked-copy' };
      const draft = ui.saveDetailsDraft(ui.selectedSaveBackup()); draft.note = 'Keep this unsaved note'; draft.dirty = true;
      ui.state.saveProfileFilter = { context: ui.state.snapshot.accountContext, id, name: before.name };
      assert.equal(ui.filteredSaveBackupList().length, 1, 'Before and after links to the same file produce one filtered card');
      assert.equal(ui.selectedSaveBackup().id, 'linked-copy');
      const rows = ui.renderProfileRows();
      assert(rows.includes(`data-profile-save-backups="${id}"`) && !rows.includes('data-profile-save-backups="../invalid"'), 'Only verified native profile IDs get the backup shortcut');
      const writesBefore = nativeRequests.filter((request) => /saveBackupRestore|profileLaunch|profilePrepare|backupSaves/.test(request.action)).length;
      assert.equal(ui.openProfileSaveBackups('../invalid'), false);
      assert(ui.openProfileSaveBackups(empty)); await settle();
      assert.equal(ui.state.view, 'saves'); assert.equal(ui.state.savesTab, 'backups');
      output = ui.renderSaveBackupsPanel();
      assert(output.includes('No backups are linked to No linked copies yet.') && output.includes('Automatic backup preferences') && output.includes('launching never swaps your save files'), 'An empty profile filter points to the existing global opt-in and never invents a save set');
      assert.equal(ui.state.saveDetailsDrafts.get('linked-copy').note, 'Keep this unsaved note', 'Profile filters preserve dirty backup notes');
      assert.equal(nativeRequests.filter((request) => /saveBackupRestore|profileLaunch|profilePrepare|backupSaves/.test(request.action)).length, writesBefore, 'Viewing profile-linked backups is read-only');
      assert(ui.openProfileSaveBackups(id)); await settle();
      assert.equal(ui.selectedSaveBackup().id, 'linked-copy');
      const warning = 'Your normal mods are back, but the after-session backup needs <attention>.';
      ui.state.profileSession = { active: true, sessionId: 'warning-session-' + platform };
      const beforeNotice = notices.length;
      assert(ui.showProfileSaveBackupWarning({ warning }));
      assert.equal(notices.length, beforeNotice + 1); assert.equal(notices.at(-1).className, 'toast warning');
      assert(notices.at(-1).markup.includes('&lt;attention&gt;') && notices.at(-1).markup.includes('Open saves'), 'Native backup failures are escaped attention notices with a relevant route');
      assert(ui.showProfileSaveBackupWarning({ saveBackup: { warning } }));
      assert.equal(notices.length, beforeNotice + 1, 'Repeated responses do not spam the same session warning');
      ui.handleProfileSessionEvent({ session: { active: false, status: 'idle', sessionId: 'event-' + platform }, warning: 'After-session copy failed for this test.' }); await settle();
      assert.equal(notices.at(-1).className, 'toast warning', 'Automatic completion events surface backup warnings too');
      ui.state.snapshot.accountContext += '-other';
      assert.equal(ui.filteredSaveBackupList().length, 3, 'A profile filter from another installation is ignored');
    }
  } finally {
    for (const action of actions) { if (previous.has(action)) actionFixtures.set(action, previous.get(action)); else actionFixtures.delete(action); }
    ui.restoreWatchState(watch); node('#toasts').append = append; context.document.hidden = hidden; Object.assign(ui.state, saved); ui.syncCurrentSavesPoll(); node('#modal').close();
  }
}

async function testStaleInstallationReads() {
  const saved = { ...ui.state }, oldError = actionErrors.get('crashLogsList');
  const actions = ['profilesList', 'crashLogsList', 'nexusInventory', 'nexusStatus', 'nexusCheckUpdates', 'diagnosticsRun'];
  const previous = new Map(actions.filter((action) => actionFixtures.has(action)).map((action) => [action, actionFixtures.get(action)]));
  const calls = (action) => nativeRequests.filter((request) => request.action === action).length;
  try {
    node('#modal').close();
    actionFixtures.set('nexusStatus', { connected: false });
    actionFixtures.set('profilesList', { profiles: [{ id: 'old-profile', name: 'Old installation' }], session: { active: true, profileName: 'Old session' } });
    actionFixtures.set('crashLogsList', { logs: [{ id: 'old-log', name: 'old-installation.log', kind: 'text' }] });
    actionFixtures.set('nexusInventory', { mods: [{ modId: 123, verified: true, name: 'Old release' }], files: [{ fileId: 1 }] });
    actionFixtures.set('nexusCheckUpdates', { files: [{ fileId: 1, version: 'Old release' }] });
    actionFixtures.set('diagnosticsRun', { planToken: 'old-diagnostic-token', checks: [{ id: 'old-installation-finding' }] });
    for (const platform of ['windows', 'linux']) {
      const base = { ...fixture, platform, edition: 'local', accountContext: 'read-old-' + platform,
        gameRunning: false, libraryBackupRecovery: { pending: false }, settings: { ...fixture.settings, autoCheckUpdates: false } };
      const reset = () => Object.assign(ui.state, { preview: false, busy: false, loading: false, view: 'overview',
        snapshot: ui.normalizeSnapshot(base), portableProfiles: null, profilesLoading: false, profileSession: { active: false },
        crashLogs: null, crashLogsLoading: false, downloads: { files: [], updates: [] }, nexus: { connected: false },
        diagnosticReport: null, diagnosticsLoading: false, libraryMeta: { entries: {}, history: [] } });
      const switchContext = () => ui.acceptSnapshot({ ...base, accountContext: 'read-new-' + platform });
      reset(); const profiles = ui.loadPortableProfiles(); await switchContext(); await profiles;
      assert.equal(ui.state.portableProfiles, null, 'An old installation’s profile reply is discarded');
      assert.equal(ui.state.profileSession.active, false, 'Old profile session locks cannot leak into a new installation');
      assert.equal(ui.state.profilesLoading, false, 'The new installation can request profiles without the old loading flag');
      reset(); const logs = ui.loadCrashLogs(); await switchContext(); await logs;
      assert.equal(ui.state.crashLogs, null, 'Old log names and selections cannot repopulate the new installation');
      assert.equal(ui.state.crashLogsLoading, false);
      reset(); actionErrors.set('crashLogsList', 'Old installation read failed');
      const failedLogs = ui.loadCrashLogs(); await switchContext(); await failedLogs;
      assert.equal(ui.state.crashLogs, null, 'Old log errors do not replace the new installation’s state');
      actionErrors.delete('crashLogsList');
      reset(); const inventory = ui.loadLiveInventory(); await switchContext(); await inventory;
      assert.equal(ui.state.downloads.pages, undefined, 'Old release inventories never enter the new installation');
      reset(); ui.state.snapshot.edition = 'nexus'; ui.state.snapshot.settings.autoCheckUpdates = true; ui.state.nexus.connected = true;
      const beforeInventory = calls('nexusInventory');
      const releases = ui.checkAutomaticReleases(); await switchContext(); await releases;
      assert.equal(calls('nexusInventory'), beforeInventory, 'A stale automatic release result does not request a second account-sensitive inventory');
      assert.equal(ui.state.downloads.pages, undefined);
      reset(); const diagnostics = ui.loadDiagnosticReport(); await switchContext(); await diagnostics;
      assert.equal(ui.state.diagnosticReport, null, 'Old diagnostic plans are not displayed for the new installation');
      assert.equal(ui.state.diagnosticsLoading, false);
    }
  } finally {
    for (const action of actions) { if (previous.has(action)) actionFixtures.set(action, previous.get(action)); else actionFixtures.delete(action); }
    if (oldError === undefined) actionErrors.delete('crashLogsList'); else actionErrors.set('crashLogsList', oldError);
    Object.assign(ui.state, saved); ui.syncCurrentSavesPoll(); node('#modal').close();
  }
}

async function testNexusContentFiltering() {
  const saved = { ...ui.state }, previousInventory = actionFixtures.get('nexusInventory'),
    previousError = actionErrors.get('nexusInventory'), previousCheckError = actionErrors.get('nexusCheckUpdates'),
    bridge = context.window.chrome.webview,
    originalPost = bridge.postMessage;
  const previewPng = 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+a1ZkAAAAASUVORK5CYII=';
  const safePage = { modId: 1201, verified: true, contentVerified: true, containsAdultContent: false,
    pictureUrl: 'https://staticdelivery.nexusmods.com/mods/1/images/1201/safe.png', name: 'Safe page' };
  const flush = async () => { for (let step = 0; step < 12; ++step) await Promise.resolve(); };
  try {
    for (const platform of ['windows', 'linux']) {
      Object.assign(ui.state, { preview: false, loading: false, busy: false, view: 'settings',
        snapshot: ui.normalizeSnapshot({ ...fixture, platform, edition: 'nexus', accountContext: 'content-policy-' + platform,
          settings: { ...fixture.settings, autoCheckUpdates: false } }),
        downloads: { files: [{ fileId: 900, modId: 1202 }], updates: [{ fileId: 900, modId: 1202 }],
          inventory: [{ fileId: 900, modId: 1202 }], fileUpdates: [{ modId: 1202 }], pages: [{ ...safePage, modId: 1202 }] } });
      const library = ui.state.snapshot.mods;
      ui.rememberPagePreview(1202, { dataUrl: previewPng, width: 1, height: 1 });
      actionFixtures.set('nexusInventory', {
        mods: [safePage,
          { ...safePage, modId: 1202, containsAdultContent: true, name: 'Restricted page fixture' },
          { ...safePage, modId: 1203, contentVerified: undefined },
          { ...safePage, modId: 1204, containsAdultContent: 'false' },
          { ...safePage, modId: 1205, contentVerified: 'true' },
          { ...safePage, modId: 1206, containsAdultContent: undefined },
          { ...safePage, modId: 1207, verified: 'true' }],
        files: Array.from({ length: 7 }, (_, index) => ({ modId: 1201 + index, fileId: 701 + index,
          name: index ? 'Blocked file fixture' : 'Safe file' })),
        fileUpdates: [{ modId: 1201, oldFileId: 700, newFileId: 701 }, { modId: 1202, oldFileId: 701, newFileId: 702 }],
        truncated: false, checkedAt: 2000000000
      });
      actionErrors.delete('nexusInventory');
      const refresh = ui.loadLiveInventory();
      assert.equal(ui.state.downloads.pages.length, 0, `${platform}: starting a refresh removes old page metadata`);
      assert.equal(ui.state.downloads.inventory.length, 0, `${platform}: starting a refresh removes old file metadata`);
      assert.equal(ui.pagePreviewCache.size, 0, `${platform}: starting a refresh removes old pictures`);
      const filteredReply = await refresh;
      assert.deepEqual(Array.from(ui.state.downloads.pages, (page) => page.modId), [1201], `${platform}: only explicit unrestricted native pages enter the UI`);
      assert.deepEqual(Array.from(ui.state.downloads.inventory, (file) => file.fileId), [701], `${platform}: unsafe or unclassified pages cannot contribute file rows`);
      assert.deepEqual(Array.from(ui.state.downloads.fileUpdates, (edge) => edge.modId), [1201], `${platform}: unsafe page update edges are excluded`);
      assert.deepEqual(Array.from(filteredReply.mods, (page) => page.modId), [1201], `${platform}: the caller also receives sanitized page metadata`);
      assert.deepEqual(Array.from(filteredReply.files, (file) => file.fileId), [701], `${platform}: the caller also receives sanitized files`);
      assert.equal(ui.state.snapshot.mods, library, `${platform}: content filtering preserves the local library`);
      for (const page of actionFixtures.get('nexusInventory').mods) {
        ui.state.downloads.pages = [page];
        assert.equal(ui.hasPreviewMetadata(page.modId), page.modId === 1201, `${platform}: preview eligibility requires strict content proof`);
      }
      actionFixtures.set('nexusInventory', { mods: [{ ...safePage, modId: 994 }],
        files: [{ fileId: 801, name: 'Unscoped file must not inherit page994' }, { modId: 994, fileId: 802, name: 'Scoped file' }],
        fileUpdates: [{ oldFileId: 800, newFileId: 801 }, { modId: 994, oldFileId: 801, newFileId: 802 }] });
      await ui.loadLiveInventory();
      assert.deepEqual(Array.from(ui.state.downloads.inventory, (file) => file.fileId), [802], `${platform}: unscoped file rows cannot inherit the default safe page`);
      assert.deepEqual(Array.from(ui.state.downloads.fileUpdates, (edge) => edge.newFileId), [802], `${platform}: unscoped update edges cannot inherit the default safe page`);
      ui.state.downloads.pages = [safePage];
      ui.rememberPagePreview(1201, { dataUrl: previewPng, width: 1, height: 1 });
      actionErrors.set('nexusInventory', 'Synthetic unavailable content classification');
      await assert.rejects(ui.loadLiveInventory(), /Synthetic unavailable content classification/);
      assert.equal(ui.state.downloads.pages.length, 0, `${platform}: failed refresh cannot restore old page content`);
      assert.equal(ui.state.downloads.inventory.length, 0, `${platform}: failed refresh cannot restore old files`);
      assert.equal(ui.state.downloads.files.length, 0, `${platform}: failed refresh clears old selected downloads`);
      assert.equal(ui.state.downloads.updates.length, 0, `${platform}: failed refresh clears old update notices`);
      assert.equal(ui.pagePreviewCache.size, 0, `${platform}: failed refresh cannot restore old pictures`);
      assert.equal(ui.state.snapshot.mods, library, `${platform}: failed refresh never clears installed or stored mods`);
      actionErrors.delete('nexusInventory');

      ui.state.nexus = { connected: true, premium: true, tierVerified: true };
      ui.state.snapshot.settings.autoCheckUpdates = true;
      actionErrors.set('nexusCheckUpdates', 'Synthetic release content verification failure');
      for (const mode of ['manual', 'automatic']) {
        const marker = `stale-policy-content-${platform}-${mode}`;
        ui.state.view = 'downloads';
        ui.state.downloads = { pages: [safePage], inventory: [{ modId: 1201, fileId: 991, name: marker }],
          files: [{ modId: 1201, fileId: 991, name: marker }], updates: [{ modId: 1201, fileId: 991 }],
          fileUpdates: [{ modId: 1201, oldFileId: 990, newFileId: 991 }] };
        ui.rememberPagePreview(1201, { dataUrl: previewPng, width: 1, height: 1 });
        node('#main-content').innerHTML = ui.renderDownloads();
        assert(node('#main-content').innerHTML.includes(marker), `${platform}/${mode}: stale remote text fixture starts visible`);
        const beforeInventory = nativeRequests.filter((request) => request.action === 'nexusInventory').length;
        const check = mode === 'manual' ? ui.handleAction('nexus-check') : ui.checkAutomaticReleases();
        assert(!node('#main-content').innerHTML.includes(marker), `${platform}/${mode}: release checking immediately removes stale remote text`);
        assert.equal(ui.pagePreviewCache.size, 0, `${platform}/${mode}: release checking immediately removes cached previews`);
        await check;
        assert(!node('#main-content').innerHTML.includes(marker), `${platform}/${mode}: failed release check cannot leave stale remote text visible`);
        assert.equal(ui.state.downloads.pages.length, 0, `${platform}/${mode}: failed release check leaves no old page authorization`);
        assert.equal(ui.state.downloads.inventory.length, 0, `${platform}/${mode}: failed release check leaves no old catalogue`);
        assert.equal(ui.state.downloads.files.length, 0, `${platform}/${mode}: failed release check leaves no old download rows`);
        assert.equal(ui.state.downloads.updates.length, 0, `${platform}/${mode}: failed release check leaves no old update rows`);
        assert.equal(ui.pagePreviewCache.size, 0, `${platform}/${mode}: failed release check cannot restore cached previews`);
        assert.equal(ui.state.snapshot.mods, library, `${platform}/${mode}: release verification failure preserves local mods`);
        assert.equal(nativeRequests.filter((request) => request.action === 'nexusInventory').length, beforeInventory,
          `${platform}/${mode}: failed release verification does not continue into catalogue fetching`);
      }
      actionErrors.delete('nexusCheckUpdates');
      ui.state.view = 'settings';

      const held = [];
      bridge.postMessage = (request) => {
        if (request.action !== 'nexusPreviewImage') return originalPost(request);
        nativeRequests.push(request); held.push(request);
      };
      ui.state.downloads.pages = [safePage];
      ui.queuePagePreview(safePage.modId);
      assert.equal(held.length, 1, `${platform}: preview fixture holds one native response`);
      ui.clearLiveNexusContent();
      ui.state.downloads.pages = [safePage];
      nativeMessageListener({ data: { requestId: held[0].requestId, ok: true,
        data: { available: true, dataUrl: previewPng, width: 1, height: 1 } } });
      await flush();
      assert(!ui.pagePreviewCache.has(safePage.modId), `${platform}: an older preview response is discarded even when the same safe page returns`);
      assert.equal(held.length, 2, `${platform}: reverified page receives a new preview request`);
      nativeMessageListener({ data: { requestId: held[1].requestId, ok: true, data: { available: false } } });
      await flush();
      bridge.postMessage = originalPost;
      ui.clearLiveNexusContent();
    }
  } finally {
    bridge.postMessage = originalPost;
    if (previousInventory === undefined) actionFixtures.delete('nexusInventory'); else actionFixtures.set('nexusInventory', previousInventory);
    if (previousError === undefined) actionErrors.delete('nexusInventory'); else actionErrors.set('nexusInventory', previousError);
    if (previousCheckError === undefined) actionErrors.delete('nexusCheckUpdates'); else actionErrors.set('nexusCheckUpdates', previousCheckError);
    ui.clearLiveNexusContent(); Object.assign(ui.state, saved);
  }
}

async function main() {
  assert.equal(catalog.mods.length, 80, 'Public catalog count');
  for (const id of ['art-the-clown-player-model', 'skill-respec', 'bike-wheelie', 'headshot-only-freakers', 'ragdoll-deaths', 'bleed-out'])
    assert.equal(catalog.mods.filter((mod) => mod.id === id).length, 1, `The new ${id} release appears exactly once`);
  assert.equal(catalog.mods.filter((mod) => mod.available === false).length, 2, 'The unreviewed build and manual ASI stay visible; published PAKs remain manageable');
  ui.state.snapshot = ui.normalizeSnapshot(fixture);
  await testChangelogHistory();
  testOverviewSlideshow();
  testContextualStatusTips();
  testLibraryActionPresentation();
  await testBackgroundToolkitUpdates();
  await testDownloadDiscoveryUX();
  testLibraryStorageCounts();
  await testActivityDismissal();
  await testLiveGameStatusAndSaveFiles();
  await testSavesSidebarPage();
  await testSaveGalleryAndRestorePresentation();
  await testCurrentSavesWithoutBackups();
  await testProfileLinkedBackups();
  await testProfileCreation();
  await testStaleInstallationReads();
  await testNexusContentFiltering();
  assert.equal(ui.state.snapshot.mods.filter((mod) => mod.enabled).length, 0, 'Catalog does not invent enabled states');
  ui.state.snapshot.sandbox = true;
  ui.state.loading = false;
  ui.render();
  assert(
    node('#connection-state').innerHTML.includes('Sandbox · game folders isolated'),
    'Native sandbox is clearly identified in the top bar'
  );
  ui.state.snapshot.sandbox = false;

  const resolvedCatalog = catalog.mods.map((mod) => ui.resolveModIcon(mod));
  assert(
    catalog.mods.every((mod) => ui.resolveModIcon(mod).key === mod.icon),
    'Every catalog entry shows the symbol chosen for it'
  );
  assert(
    resolvedCatalog.every((symbol) => typeof symbol.key === 'string' && symbol.key.length),
    'Every current entry has a local representative symbol'
  );
  assert(
    new Set(resolvedCatalog.map((symbol) => symbol.key)).size >= 15,
    'Catalog artwork distinguishes subjects instead of repeating a generic package'
  );
  for (const mod of catalog.mods) {
    const art = ui.modArtwork(mod);
    assert(art.includes('<svg') && art.includes('data-mod-icon='), 'Every catalog tile retains vector artwork');
    assert(
      !art.includes('undefined') && !art.includes('onerror='),
      'Tile artwork has no missing symbol or inline error handler'
    );
  }
  assert.equal(ui.resolveModIcon({ name: 'No Recoil' }).key, 'steadyAim', 'Recoil has a steady reticle');
  assert.equal(ui.resolveModIcon({ name: 'XP Randomizer' }).key, 'progression', 'XP has its own progression emblem');
  assert.equal(
    ui.resolveModIcon({ name: 'A new release', category: 'Camera' }).key,
    'camera',
    'Future category metadata supplies a relevant symbol'
  );
  assert.equal(
    ui.resolveModIcon({ name: 'New release', assets: [{ path: 'BendGame/Player/Health/Effect.uasset' }] }).key,
    'health',
    'Inspected asset paths give an unnamed future release a subject'
  );
  assert.equal(
    ui.resolveModIcon({ name: 'No Recoil', icon: 'bike' }).key,
    'bike',
    'A recognized explicit local icon wins'
  );
  for (const badIcon of [
    'https://remote.invalid/art.svg',
    '<svg onload=attack()>',
    '__proto__',
    'constructor',
    'not-a-symbol'
  ])
    assert.equal(
      ui.resolveModIcon({ name: 'No Recoil', icon: badIcon }).key,
      'steadyAim',
      'Invalid or remote icon metadata falls back to semantic local artwork'
    );
  const unknown = { modId: 1500, fileId: 888, name: 'A completely new release' };
  assert.equal(ui.resolveModIcon(unknown).key, 'fieldTag', 'Unclassified future release gets an illustrated field tag');
  assert.equal(
    JSON.stringify(ui.resolveModIcon(unknown)),
    JSON.stringify(ui.resolveModIcon({ ...unknown, name: 'A renamed release' })),
    'Unclassified renamed release retains its stable identity-based artwork'
  );
  assert.equal(
    ui.resolveModIcon({ category: 'constructor' }).key,
    'fieldTag',
    'Unrecognized category cannot escape the local symbol registry'
  );
  const boundedAssets = Array(100).fill('BendGame/Unclassified.uasset');
  Object.defineProperty(boundedAssets, 96, {
    get() {
      throw Error('Artwork must not scan the full package index');
    }
  });
  assert.equal(
    ui.resolveModIcon({ name: 'New release', assets: boundedAssets }).key,
    'fieldTag',
    'Asset-based artwork inspection is bounded independently of package size'
  );
  assert(ui.modArtwork(null).includes('<svg'), 'Missing metadata never produces a blank tile');
  for (const release of [
    { id: 'no-recoil', name: 'No Recoil' },
    {
      name: 'A future discovery',
      modId: 9001,
      fileId: 3,
      cover: 'https://remote.invalid/art.png',
      image: 'assets/covers/unknown.png'
    }
  ]) {
    const art = ui.modArtwork(release);
    assert(
      art.includes('<svg') &&
        !art.includes('<img') &&
        !art.includes('assets/covers/') &&
        !art.includes('remote.invalid'),
      'All current and future releases use local symbols without a per-mod artwork dependency'
    );
  }
  const artworkSettings = { ...ui.state.snapshot.settings };
  ui.state.snapshot.settings.fontScale = 150;
  ui.applyAppearance();
  const largeArtwork = ui.renderMods();
  assert(
    (largeArtwork.match(/<article class="mod-row/g) || []).length > 0 &&
      (largeArtwork.match(/data-mod-icon=/g) || []).length ===
        (largeArtwork.match(/<article class="mod-row/g) || []).length,
    'Every mod remains illustrated at 150% text size'
  );
  assert(
    ui.modArtwork({ name: 'Unfamiliar title '.repeat(1000), id: 'long-release' }).length < 1200,
    'Long public titles do not expand or inject the artwork markup'
  );
  ui.state.snapshot.settings = artworkSettings;
  ui.applyAppearance();

  const previewPng =
    'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+a1ZkAAAAASUVORK5CYII=';
  assert(
    ui.cleanPagePreview({ available: true, dataUrl: previewPng, width: 1, height: 1 }),
    'Bounded native PNG response is accepted'
  );
  for (const dataUrl of [
    'https://remote.invalid/preview.png',
    'data:image/svg+xml;base64,PHN2Zy8+',
    'data:image/png;base64,PHN2Zy8+',
    'data:image/png;base64,iVBORw0KGgo\" onerror=attack()'
  ])
    assert.equal(
      ui.cleanPagePreview({ available: true, dataUrl, width: 1, height: 1 }),
      null,
      'External, non-PNG or injected preview data is refused'
    );
  assert.equal(
    ui.cleanPagePreview({ available: true, dataUrl: previewPng, width: 4096, height: 1 }),
    null,
    'Preview dimensions are bounded'
  );
  const savedPages = ui.state.downloads.pages;
  ui.state.downloads.pages = [
    { modId: 9001, verified: true, contentVerified: true, containsAdultContent: false,
      pictureUrl: 'https://staticdelivery.nexusmods.com/trusted-metadata.png' }
  ];
  assert(
    ui.hasPreviewMetadata(9001) && !ui.hasPreviewMetadata(9002),
    'Only returned verified page metadata makes a preview eligible'
  );
  assert(
    !ui.modArtwork({ modId: 9001, name: 'Future mod', pictureUrl: '<img onerror=attack()>' }).includes('<img'),
    'Public metadata is never directly inserted as an image'
  );
  pagePreviewFixture = { available: true, dataUrl: previewPng, width: 1, height: 1 };
  const previewStart = nativeRequests.length;
  ui.queuePagePreview(9001);
  ui.queuePagePreview(9001);
  ui.queuePagePreview(-1);
  ui.queuePagePreview(9002);
  for (let step = 0; step < 8; step++) await Promise.resolve();
  const previewRequests = nativeRequests.slice(previewStart).filter((item) => item.action === 'nexusPreviewImage');
  assert.equal(previewRequests.length, 1, 'Concurrent tiles for one future page share one lazy request');
  assert.equal(
    Object.keys(previewRequests[0]).sort().join(','),
    'action,modId,requestId',
    'Preview bridge sends a page ID without arbitrary URLs'
  );
  assert.equal(
    ui.pagePreviewCache.get(9001).dataUrl,
    previewPng,
    'Validated native result is cached without rerendering settings'
  );
  const tile = node('synthetic-preview-tile');
  tile.isConnected = true;
  tile.querySelector = () => null;
  tile.prepend = (element) => {
    tile.previewElement = element;
  };
  ui.paintPagePreview(tile, 9001);
  assert.equal(tile.previewElement.src, previewPng, 'Validated raster reaches the actual image source property');
  assert(tile.classList.contains('has-page-preview'), 'A preview keeps its vector-symbol accent');
  for (let id = 9100; id < 9140; id++) ui.rememberPagePreview(id, { dataUrl: previewPng, width: 1, height: 1 });
  assert(ui.pagePreviewCache.size <= 32, 'Browser preview retention has a fixed entry cap');
  ui.state.downloads.pages = savedPages;
  ui.pagePreviewCache.clear();
  pagePreviewFixture = { available: false };

  const beforeLargeLists = { snapshot: ui.state.snapshot, downloads: ui.state.downloads, view: ui.state.view };
  ui.state.snapshot = {
    ...ui.state.snapshot,
    mods: Array.from({ length: 121 }, (_, index) => ({
      id: `future-${index}`,
      name: `Future package ${index}`,
      category: 'World',
      available: true
    }))
  };
  ui.state.modLimit = 50;
  assert.equal(
    (ui.renderModRows().match(/<article class="mod-row(?: is-selected)?" data-mod-row=/g) || []).length,
    50,
    'Large managed catalogs initially render only 50 rows'
  );
  assert(ui.renderModRows().includes('Showing 50 of 121 matching mods'), 'Catalog paging exposes the full count');
  ui.state.downloads = {
    files: [],
    updates: [],
    inventory: Array.from({ length: 121 }, (_, index) => ({
      modId: 10000 + index,
      fileId: index + 1,
      name: `Future upload ${index}`,
      version: '1',
      fileName: 'Future_P.pak'
    }))
  };
  ui.state.downloadLimit = 50;
  ui.state.discoveryLimit = 50;
  assert.equal(
    (ui.renderDownloads().match(/<article class="mod-row">/g) || []).length,
    50,
    'Large live file inventories initially render only 50 rows'
  );
  assert(ui.renderDownloads().includes('Show 50 more files'), 'Older releases remain reachable');
  assert.equal(
    (ui.renderDiscoveries().match(/<article class="mod-row">/g) || []).length,
    50,
    'Future page discovery cannot create an unbounded initial card list'
  );
  assert(ui.renderDiscoveries().includes('Show 50 more releases'), 'All future discoveries remain reachable');
  await ui.handleAction('downloads-more');
  assert.equal(
    (ui.renderDownloads().match(/<article class="mod-row">/g) || []).length,
    100,
    'Explicit paging reveals the next 50 published files'
  );
  ui.state.snapshot = beforeLargeLists.snapshot;
  ui.state.downloads = beforeLargeLists.downloads;
  ui.state.view = beforeLargeLists.view;
  ui.state.modLimit = 50;
  ui.state.downloadLimit = 50;
  ui.state.discoveryLimit = 50;

  const beforeAppearance = {
    settings: { ...ui.state.snapshot.settings },
    platform: ui.state.snapshot.platform,
    systemAppearance: ui.state.snapshot.systemAppearance
  };
  ui.state.snapshot.settings.colorMode = 'auto';
  delete ui.state.snapshot.systemAppearance;
  osLightMode = true;
  assert.equal(
    ui.effectiveColorMode(ui.state.snapshot.settings),
    'light',
    'Automatic mode uses the browser system preference without native data'
  );
  osLightMode = false;
  ui.state.snapshot.systemAppearance = { mode: 'light', source: 'windows' };
  assert.equal(
    ui.effectiveColorMode({ colorMode: 'auto' }),
    'light',
    'Native system preference is authoritative in automatic mode'
  );
  assert.equal(ui.effectiveColorMode({ colorMode: 'dark' }), 'dark', 'Explicit dark choice overrides a light desktop');
  ui.state.snapshot.systemAppearance = {
    mode: 'dark',
    source: 'Qt palette',
    palette: {
      window: '#202a31',
      base: '#273640',
      text: '#f4f5ed',
      button: '#32414a',
      highlight: '#96562d',
      highlightedText: '#ffffff'
    }
  };
  ui.state.snapshot.platform = 'linux';
  ui.state.snapshot.settings = { ...ui.state.snapshot.settings, colorMode: 'auto' };
  delete ui.state.snapshot.settings.useSystemTheme;
  ui.applyAppearance();
  assert.equal(context.document.documentElement.dataset.systemPalette, '', 'Linux desktop colors require explicit opt-in');
  assert.equal(context.document.documentElement.style['--panel'], undefined, 'A missing preference retains the toolkit palette');
  ui.state.snapshot.settings = { ...ui.state.snapshot.settings, colorMode: 'auto', useSystemTheme: true };
  ui.applyAppearance();
  assert.equal(context.document.documentElement.dataset.colorMode, 'dark', 'Linux auto follows actual native mode');
  assert.equal(context.document.documentElement.dataset.systemPalette, 'linux', 'Readable Linux palette is adopted');
  assert.equal(
    context.document.documentElement.style['--panel'],
    '#273640',
    'Desktop base color becomes the panel color'
  );
  assert(
    ui.colorContrast(
      context.document.documentElement.style['--panel'],
      context.document.documentElement.style['--text']
    ) >= 4.5,
    'Native panel/text pair meets normal-text contrast'
  );
  assert(
    ui.colorContrast(
      context.document.documentElement.style['--panel'],
      context.document.documentElement.style['--muted']
    ) >= 4.5,
    'Derived muted text remains readable'
  );
  assert(
    ui.colorContrast(
      context.document.documentElement.style['--shell'],
      context.document.documentElement.style['--muted']
    ) >= 4.5,
    'Derived muted text remains readable on the desktop window background'
  );
  ui.state.snapshot.settings.colorMode = 'light';
  ui.applyAppearance();
  assert.equal(
    context.document.documentElement.dataset.colorMode,
    'light',
    'Manual light mode remains light on a dark Linux desktop'
  );
  assert.equal(
    context.document.documentElement.dataset.systemPalette,
    '',
    'Mismatched dark palette cannot override manual light mode'
  );
  assert.equal(
    context.document.documentElement.style['--panel'],
    undefined,
    'Old desktop overrides are removed when they no longer apply'
  );
  const settingsLight = ui.renderSettings();
  assert(
    settingsLight.includes('id="color-mode-setting"') && settingsLight.includes('value="light" selected'),
    'All three color modes are selectable'
  );
  assert(settingsLight.includes('id="system-theme-setting"'), 'Linux exposes optional desktop colors');
  ui.state.snapshot.platform = 'windows';
  assert(!ui.renderSettings().includes('id="system-theme-setting"'), 'Linux palette switch is not shown on Windows');
  ui.state.snapshot.platform = 'linux';
  ui.state.snapshot.settings.colorMode = 'dark';
  ui.state.snapshot.settings.useSystemTheme = false;
  ui.applyAppearance();
  assert.equal(
    context.document.documentElement.dataset.systemPalette,
    '',
    'Desktop palette opt-out restores branded colors'
  );
  ui.state.snapshot.settings.useSystemTheme = true;
  const cleaned = ui.cleanSystemAppearance({
    mode: 'dark',
    palette: {
      window: 'url(https://remote.invalid)',
      base: '#fff',
      text: '#ffffff',
      unknown: '#123456',
      highlight: '#123456;--bg:red'
    }
  });
  assert.equal(
    Object.keys(cleaned.palette).join(','),
    'text',
    'Native palette whitelist only accepts exact six-digit hex values'
  );
  ui.state.snapshot.systemAppearance = {
    mode: 'dark',
    palette: { window: '#202a31', base: '#273640', text: '#283740' }
  };
  assert.equal(
    ui.usableDesktopPalette(ui.state.snapshot.settings),
    null,
    'An unreadable desktop palette is rejected rather than weakening text contrast'
  );
  const savedColorMode = ui.state.snapshot.settings.colorMode;
  ui.state.snapshot.settings.highContrast = false;
  ui.bindContent();
  node('#system-theme-setting').setAttribute('aria-checked', 'true');
  node('#color-mode-setting').value = 'light';
  node('#color-mode-setting').onchange();
  assert.equal(
    context.document.documentElement.dataset.colorMode,
    'light',
    'Color selector previews the chosen mode immediately'
  );
  assert.equal(ui.state.snapshot.settings.colorMode, savedColorMode, 'Appearance preview waits for the auto-save');
  ui.handleSystemAppearance({
    data: { mode: 'dark', source: 'desktop', palette: { window: '#202a31', base: '#273640', text: '#f4f5ed' } }
  });
  assert.equal(
    context.document.documentElement.dataset.colorMode,
    'light',
    'Native theme events preserve an unsaved manual mode preview'
  );
  ui.navigate('overview');
  const beforeNavigationSave = nativeRequests.length;
  for (const [id, timer] of [...timers.entries()].filter(([, entry]) => entry.delay === 450)) {
    timers.delete(id);
    await timer.callback();
  }
  const navigationSave = nativeRequests
    .slice(beforeNavigationSave)
    .find((request) => request.action === 'savePreferences');
  assert.equal(
    navigationSave?.preferences.colorMode,
    'light',
    'Leaving Settings before the debounce still saves the chosen mode'
  );
  assert.equal(ui.state.snapshot.settings.colorMode, 'light', 'A navigation save updates the saved settings');
  const settingsSentinel = 'unsaved appearance controls';
  node('#main-content').innerHTML = settingsSentinel;
  const beforeThemeEvent = nativeRequests.length;
  nativeMessageListener({
    data: {
      event: 'systemAppearanceChanged',
      data: { mode: 'light', source: 'desktop', palette: { window: '#e4e8e2', base: '#fcfcf4', text: '#1d3131' } }
    }
  });
  assert.equal(node('#main-content').innerHTML, settingsSentinel, 'Desktop theme event preserves the current form');
  assert.equal(
    nativeRequests.length,
    beforeThemeEvent,
    'Desktop theme event never persists over an unsaved user choice'
  );
  assert.equal(
    context.document.documentElement.dataset.colorMode,
    'light',
    'Desktop changes respect a saved manual mode'
  );
  ui.state.snapshot.settings.colorMode = 'auto';
  ui.applyAppearance();
  assert.equal(
    context.document.documentElement.dataset.colorMode,
    'light',
    'Automatic mode follows a live desktop appearance event'
  );
  assert.equal(context.document.documentElement.dataset.systemPalette, 'linux', 'Readable light palette is adopted');
  ui.state.snapshot.settings.highContrast = true;
  ui.applyAppearance();
  assert.equal(
    context.document.documentElement.dataset.systemPalette,
    '',
    'High contrast keeps its own known readable palette'
  );
  ui.state.snapshot.settings = beforeAppearance.settings;
  ui.state.snapshot.platform = beforeAppearance.platform;
  ui.state.snapshot.systemAppearance = beforeAppearance.systemAppearance;
  ui.applyAppearance();
  const beforeIndependentSettings = { snapshot: ui.state.snapshot, profile: ui.state.profileSession };
  ui.state.snapshot = {
    ...ui.state.snapshot,
    gameRunning: false,
    recoveryRequired: false,
    settings: {
      ...ui.state.snapshot.settings,
      gamePath: 'C:\\Synthetic\\Old',
      theme: 'default',
      seasonalThemes: false,
      highContrast: false
    }
  };
  ui.state.profileSession = { active: false };
  ui.bindContent();
  node('#theme-setting').value = 'newyear';
  node('#theme-setting').onchange();
  assert.equal(
    context.document.documentElement.dataset.season,
    'newyear',
    'Changing season previews its actual theme immediately'
  );
  assert.equal(ui.state.snapshot.settings.theme, 'default', 'Theme preview does not persist early');
  node('#contrast-setting').setAttribute('aria-checked', 'false');
  node('#contrast-setting').onclick();
  assert.equal(context.document.documentElement.dataset.contrast, 'high', 'High contrast applies immediately');
  assert(node('#seasonal-effects').hidden, 'High contrast removes decorative effects');
  node('#contrast-setting').onclick();
  // Look-and-comfort switches save by themselves shortly after a change; game and file settings wait for Save.
  ui.state.view = 'settings';
  ui.state.snapshot.settings.steamLaunch = true;
  node('#steam-launch-setting').setAttribute('aria-checked', 'true');
  node('#steam-launch-setting').onclick();
  assert.equal(
    node('#steam-launch-save').hidden,
    false,
    'A game setting change shows its own Save button instead of saving by itself'
  );
  const beforeAutoSave = nativeRequests.length;
  node('#updates-setting').setAttribute('aria-checked', 'true');
  node('#updates-setting').onclick();
  // The bridge uses fake timers; run the pending auto-save (450 ms) like the debounce tests do.
  for (const [id, timer] of [...timers.entries()].filter(([, entry]) => entry.delay === 450)) {
    timers.delete(id);
    await timer.callback();
  }
  const autoSaved = nativeRequests
    .slice(beforeAutoSave)
    .filter((r) => r.action === 'savePreferences')
    .pop();
  assert(
    autoSaved && autoSaved.preferences.autoCheckUpdates === false && autoSaved.preferences.steamLaunch === true,
    'A comfort switch saves by itself and leaves the unsaved game setting alone'
  );
  const fillPreferences = () => {
    node('#game-path').value = 'C:\\Synthetic\\Missing';
    node('#color-mode-setting').value = 'dark';
    node('#theme-setting').value = 'newyear';
    node('#scale-setting').value = '125';
    node('#interface-scale-setting').value = '150';
    for (const id of ['system-theme', 'seasonal', 'effects', 'updates', 'auto-scale'])
      node('#' + id + '-setting').setAttribute('aria-checked', 'true');
    for (const id of ['motion', 'contrast', 'compact'])
      node('#' + id + '-setting').setAttribute('aria-checked', 'false');
  };
  const advanceAutoSave = async () => {
    for (const [id, timer] of [...timers.entries()].filter(([, entry]) => entry.delay === 450)) {
      timers.delete(id);
      await timer.callback();
    }
  };
  fillPreferences();
  ui.state.snapshot.settings.autoManageExternal = true;
  node('#steam-launch-setting').setAttribute('aria-checked', 'false');
  node('#auto-manage-setting').setAttribute('aria-checked', 'false');
  node('#theme-setting').value = 'winter';
  node('#theme-setting').onchange();
  const beforeLeavingSettings = nativeRequests.length;
  ui.navigate('overview');
  assert.equal(context.document.documentElement.dataset.season, 'winter', 'Navigation retains the pending theme');
  assert(ui.renderSettings().includes('value="winter" selected'), 'Returning before save shows the pending choice');
  // Mimic the vanished form: its old values must not be read by the delayed request.
  node('#theme-setting').value = '';
  node('#game-path').value = 'C:\\Synthetic\\Stale';
  ui.state.busy = true;
  await advanceAutoSave();
  assert.equal(nativeRequests.length, beforeLeavingSettings, 'A pending appearance save waits for another action');
  ui.state.busy = false;
  const savedQuerySelector = context.document.querySelector;
  context.document.querySelector = (selector) =>
    [
      '#settings-form',
      '#settings-note',
      '#game-path',
      '#color-mode-setting',
      '#theme-setting',
      '#scale-setting',
      '#interface-scale-setting'
    ].includes(selector)
      ? null
      : savedQuerySelector(selector);
  await advanceAutoSave();
  context.document.querySelector = savedQuerySelector;
  const leaveRequests = nativeRequests.slice(beforeLeavingSettings);
  const leaveSave = leaveRequests.find((request) => request.action === 'savePreferences');
  assert.equal(leaveSave?.preferences.theme, 'winter', 'The queued save uses the captured choice after navigation');
  assert.equal(leaveSave?.preferences.fontScale, 125, 'Text size travels with the captured appearance choices');
  assert.equal(leaveSave?.preferences.steamLaunch, true, 'Navigation does not save an unconfirmed Steam choice');
  assert.equal(
    leaveSave?.preferences.autoManageExternal,
    true,
    'Navigation does not save an unconfirmed managing choice'
  );
  assert(!('gamePath' in leaveSave.preferences), 'The automatic save contains no game path');
  assert(
    !leaveRequests.some((request) => request.action === 'saveSettings'),
    'Leaving Settings never mutates game setup'
  );
  assert.equal(ui.state.snapshot.settings.gamePath, 'C:\\Synthetic\\Old');
  ui.state.view = 'settings';
  fillPreferences();
  node('#theme-setting').value = 'spring';
  node('#theme-setting').onchange();
  const firstSaveTimer = [...timers.entries()].find(([, entry]) => entry.delay === 450);
  assert(firstSaveTimer, 'The first appearance change is queued');
  timers.delete(firstSaveTimer[0]);
  const savingFirstChoice = firstSaveTimer[1].callback();
  node('#theme-setting').value = 'summer';
  node('#theme-setting').onchange();
  await savingFirstChoice;
  assert.equal(
    context.document.documentElement.dataset.season,
    'summer',
    'An older save never clears a newer preview'
  );
  assert.equal(ui.state.snapshot.settings.theme, 'spring', 'Only the completed request is marked saved');
  await advanceAutoSave();
  assert.equal(ui.state.snapshot.settings.theme, 'summer', 'A change made during a save is persisted next');
  fillPreferences();
  node('#theme-setting').value = 'winter';
  node('#theme-setting').onchange();
  const beforeShortcutSave = nativeRequests.length;
  await ui.handleInterfaceShortcut({ ctrlKey: true, key: '+', preventDefault() {} });
  const shortcutScale = nativeRequests
    .slice(beforeShortcutSave)
    .find((request) => request.action === 'savePreferences').preferences.interfaceScale;
  await advanceAutoSave();
  assert.equal(
    ui.state.snapshot.settings.interfaceScale,
    shortcutScale,
    'A pending appearance save does not undo newer keyboard scaling'
  );
  assert.equal(ui.state.snapshot.settings.autoScale, false);
  fillPreferences();
  actionErrors.set('saveSettings', 'This game folder is missing.');
  let settingsRequestStart = nativeRequests.length;
  await ui.saveLoaderSettings({ preventDefault() {} });
  const independentRequests = nativeRequests.slice(settingsRequestStart);
  assert.equal(
    independentRequests[0].action,
    'savePreferences',
    'Preferences are saved before checking a changed game folder'
  );
  assert(!('gamePath' in independentRequests[0].preferences), 'No game or package settings enter the preference store');
  assert(
    typeof independentRequests[0].preferences.steamLaunch === 'boolean' &&
      typeof independentRequests[0].preferences.autoCheckUpdates === 'boolean',
    'Every preference switch is saved, including the Steam launch choice'
  );
  assert.equal(ui.state.snapshot.settings.theme, 'newyear');
  assert.equal(
    ui.state.snapshot.settings.gamePath,
    'C:\\Synthetic\\Old',
    'Bad game path leaves the selected installation untouched'
  );
  assert(
    node('#settings-note').textContent.includes('Loader settings saved.') &&
      node('#settings-note').textContent.includes('missing'),
    'Partial save explains the remaining game problem'
  );
  actionErrors.delete('saveSettings');
  fillPreferences();
  ui.state.snapshot.gameRunning = true;
  settingsRequestStart = nativeRequests.length;
  await ui.saveLoaderSettings({ preventDefault() {} });
  assert.equal(nativeRequests.slice(settingsRequestStart).filter((r) => r.action === 'savePreferences').length, 1);
  assert(
    !nativeRequests.slice(settingsRequestStart).some((r) => r.action === 'saveSettings'),
    'Running game does not prevent appearance save or receive a game-path mutation'
  );
  ui.state.snapshot.gameRunning = false;
  fillPreferences();
  actionErrors.set('savePreferences', 'Preferences storage is read-only.');
  settingsRequestStart = nativeRequests.length;
  await ui.saveLoaderSettings({ preventDefault() {} });
  assert(
    !nativeRequests.slice(settingsRequestStart).some((r) => r.action === 'saveSettings'),
    'Failed preferences save does not silently change the game folder'
  );
  assert(node('#settings-note').textContent.includes('read-only'));
  actionErrors.delete('savePreferences');
  ui.state.snapshot.steamCopy = true;
  assert(
    ui.renderSettings().includes('id="steam-launch-setting"'),
    'A Steam copy offers the Steam or direct launch choice'
  );
  ui.state.snapshot.steamCopy = false;
  assert(!ui.renderSettings().includes('steam-launch-setting'), 'Other copies do not show the Steam launch choice');
  const settingsSource = fs.readFileSync(path.join(appRoot, 'ui/app.js'), 'utf8');
  assert(
    !/\$\('#(updates|steam-launch)-setting'\)\.getAttribute/.test(settingsSource),
    'Saving settings never assumes an optional switch is on the page (the local edition has no Nexus panel)'
  );
  assert.equal(ui.activeSeason({ theme: 'auto', seasonalThemes: true }, new Date(2026, 11, 31)), 'newyear');
  assert.equal(ui.activeSeason({ theme: 'auto', seasonalThemes: true }, new Date(2027, 0, 1)), 'newyear');
  assert.equal(ui.activeSeason({ theme: 'auto', seasonalThemes: true }, new Date(2027, 0, 2)), 'winter');
  ui.renderSeasonalEffects({ seasonalThemes: true, seasonalEffects: true }, 'newyear');
  assert.equal(
    (node('#seasonal-effects').innerHTML.match(/seasonal-firework /g) || []).length,
    4,
    'New Year adds four restrained fireworks'
  );
  ui.renderSeasonalEffects({ reducedMotion: true }, 'newyear');
  assert(
    node('#seasonal-effects').hidden && node('#seasonal-effects').innerHTML === '',
    'Reduced motion removes fireworks'
  );
  ui.state.snapshot = beforeIndependentSettings.snapshot;
  ui.state.profileSession = beforeIndependentSettings.profile;
  ui.navigate('overview');
  node('#modal').close();
  const appearanceCss = compact(fs.readFileSync(path.join(appRoot, 'ui/style.css'), 'utf8'));
  assert(
    contains(appearanceCss, ':root[data-color-mode="light"]') && contains(appearanceCss, 'color-scheme:light'),
    'Light mode changes native control color scheme'
  );
  assert(
    contains(appearanceCss, '.hero{color:#f0f3f1}') && contains(appearanceCss, '.hero .button.subtle'),
    'Cinematic hero retains its own readable dark-image text and button colors'
  );

  for (const view of ['Overview', 'Mods', 'Downloads', 'Folders', 'Conflicts', 'Profiles', 'Saves', 'Help', 'Settings']) {
    const output = ui[`render${view}`]();
    assert.equal(typeof output, 'string');
    assert(output.length > 100, `${view} renders content`);
    assert(!output.includes('style='), `${view} respects the self-only style policy`);
  }
  assert(
    ui.renderDiagnosticPage().includes('Check my setup'),
    'Diagnostics offers actual checks instead of fake results'
  );
  assert.equal(ui.filteredMods()[0].id, 'no-recoil', 'Featured order uses real packages');
  ui.state.modLimit = 200;
  assert(ui.renderMods().includes('View notes'), 'Unsupported packages offer explanation');
  ui.state.modLimit = 50;
  assert(
    ui.renderFolders().includes('Scan your game folders') && !ui.renderFolders().includes('C:\\Synthetic'),
    'Unscanned folders do not invent detected installations'
  );
  ui.state.directories = directoriesFixture;
  const folders = ui.renderFolders();
  assert(folders.includes('10 GB free'), 'Reported disk space is formatted');
  assert(folders.includes('C:\\Synthetic\\one.pak'), 'Duplicate files are visible');
  assert(folders.includes('data-open-location="managedPaks"'), 'Known locations open by ID');
  assert(!folders.includes('data-open-path='), 'Native open actions do not accept arbitrary displayed paths');
  assert(folders.includes('Restore a reviewed backup on the Saves page'), 'Save location directs restoration to its reviewed workflow');
  assert(
    folders.includes('A few mission and map mods need extra steps'),
    'Startup preparation explains its limited scope'
  );
  assert(folders.includes('data-action="directories-prepare"'), 'Preparation follows backend eligibility');
  ui.state.directories = {
    ...directoriesFixture,
    startup: { ...directoriesFixture.startup, canPrepare: false, status: 'external-prepared' }
  };
  assert(!ui.renderFolders().includes('data-action="directories-prepare"'), 'Manual preparation is not adopted');
  ui.state.directories = directoriesFixture;
  ui.confirmDirectoryChange('prepare');
  const preparation = node('#modal-content').innerHTML;
  assert(preparation.includes(directoriesFixture.startup.source), 'Confirmation shows exact source');
  assert(preparation.includes(directoriesFixture.startup.backup), 'Confirmation shows exact preserved backup');
  assert(
    !nativeRequests.some((request) => request.action === 'directoriesPrepare'),
    'Rendering confirmation never changes directories'
  );

  const hostile = '<img src=x onerror="alert(1)"><script>test</script>';
  ui.state.snapshot.mods[0] = { ...ui.state.snapshot.mods[0], summary: hostile };
  const escaped = ui.renderMods();
  assert(!escaped.includes(hostile), 'Catalog summaries are escaped');
  assert(escaped.includes('&lt;img'), 'Escaped hostile summary remains readable');

  const beforeDense = {
    conflicts: ui.state.snapshot.conflicts,
    omitted: ui.state.snapshot.conflictPairsOmitted,
    view: ui.state.view,
    limit: ui.state.conflictLimit
  };
  const sampled = Array.from({ length: 64 }, (_, index) => `BendGame/Content/<sample-${index}>.uasset`);
  ui.state.snapshot.conflicts = Array.from({ length: 2048 }, (_, index) => ({
    a: `dense-a-${index}`,
    b: `dense-b-${index}`,
    aName: 'First package ' + index,
    bName: 'Second package',
    count: 100000,
    assets: sampled,
    evidenceTruncated: true
  }));
  ui.state.snapshot.conflictPairsOmitted = 97;
  ui.state.conflictLimit = 50;
  ui.state.view = 'conflicts';
  assert.equal(ui.detectedConflictCount(), 2145, 'Detected totals include pairs omitted from the bounded report');
  const dense = ui.renderConflicts();
  assert.equal(
    (dense.match(/class="conflict-card"/g) || []).length,
    50,
    'Dense reports render only the first page of conflict cards'
  );
  // Mods that overlap through a shared partner form one group instead of one card per pair.
  const grouped = ui.conflictGroups([
    {
      a: 'no-recoil',
      b: 'infinite-ammo-no-reload',
      aName: 'No Recoil',
      bName: 'Infinite Ammo',
      count: 3,
      assets: ['w1', 'w2', 'w3']
    },
    {
      a: 'infinite-ammo-no-reload',
      b: 'hidden-arsenal',
      aName: 'Infinite Ammo',
      bName: 'Hidden Arsenal',
      count: 1,
      assets: ['w2']
    },
    {
      a: 'longer-days-2x',
      b: 'longer-days-4x',
      aName: 'Longer Days 2x',
      bName: 'Longer Days 4x',
      count: 1,
      assets: ['d']
    }
  ]);
  assert(
    grouped.length === 2 &&
      grouped[0].ids.length === 3 &&
      grouped[0].pairs.length === 2 &&
      grouped[0].assets.size === 3 &&
      grouped[1].ids.length === 2,
    'Overlapping pairs that share a mod are merged into one group'
  );
  assert(
    dense.includes('97 more pairs of conflicting mods are left out') && dense.includes('2048 of 2145'),
    'The incomplete report is called out with full detected counts'
  );
  assert(
    dense.includes('Showing 64 of 100000 paths') && dense.includes('exported evidence may use the same limit'),
    'A path sample keeps the full count and does not promise a complete export'
  );
  assert(
    !dense.includes('…more paths in the exported report') && !dense.includes('<sample-0>'),
    'Old export promise is removed and sampled paths are escaped'
  );
  await ui.handleAction('conflicts-more');
  assert.equal(ui.state.conflictLimit, 100, 'More pairs are revealed only when requested');
  ui.state.snapshot.conflicts = beforeDense.conflicts;
  ui.state.snapshot.conflictPairsOmitted = beforeDense.omitted;
  ui.state.view = beforeDense.view;
  ui.state.conflictLimit = beforeDense.limit;
  ui.state.snapshot.settings.interfaceScale = 200;
  ui.state.snapshot.settings.autoScale = false;
  const settings = ui.renderSettings();
  assert(settings.includes('id="interface-scale-setting"'), 'Whole-interface sizing exists');
  assert(settings.includes('value="200" selected'), '200% is a valid selected option');
  assert(settings.includes('value="150"'), 'Large text size exists');
  assert(
    settings.includes('<button type="button" class="button " data-action="browse-game"'),
    'Browse and other action buttons cannot accidentally submit unsaved Settings'
  );
  assert(settings.includes('id="effects-setting"'), 'Seasonal effects have their own control');
  assert(settings.includes('id="auto-scale-setting"'), 'Automatic interface scaling has a separate control');
  ui.applyAppearance();
  assert.equal(context.document.documentElement.style.zoom, '2', 'Interface scaling applies without OS changes');
  assert.equal(ui.autoInterfaceScale(1440, 955), 100, 'Normal window remains 100%');
  assert.equal(ui.autoInterfaceScale(2560, 1440), 150, 'Large 1440p window gets a readable fit');
  assert.equal(ui.autoInterfaceScale(3840, 2160), 200, '4K window gets a readable fit');
  assert.equal(ui.autoInterfaceScale(1280, 720), 100, 'Auto avoids shrinking small-window text');
  const nativeDisplay = (width, height, ratio = 1) => ({
    source: 'native-test',
    monitorId: 'monitor-A',
    monitorLogicalWidth: width / ratio,
    monitorLogicalHeight: height / ratio,
    workLogicalWidth: width / ratio,
    workLogicalHeight: height / ratio,
    monitorPixelWidth: width,
    monitorPixelHeight: height,
    devicePixelRatio: ratio,
    windowLogicalWidth: 1440,
    windowLogicalHeight: 955
  });
  for (const [width, height, ratio, expected] of [
    [1920, 1080, 1, 100],
    [2560, 1440, 1, 135],
    [2560, 1440, 1.25, 105],
    [2560, 1440, 1.5, 100],
    [3840, 2160, 1, 200],
    [3840, 2160, 1.25, 160],
    [3840, 2160, 1.5, 135],
    [3840, 2160, 1.75, 115],
    [3840, 2160, 2, 100],
    [3440, 1440, 1, 135],
    [2160, 3840, 1, 200]
  ]) {
    const decision = ui.automaticScaleDecision(1440, 955, nativeDisplay(width, height, ratio));
    assert.equal(
      decision.percent,
      expected,
      `${width}x${height} at system ${ratio * 100}% compensates exactly once in an ordinary window`
    );
    assert.equal(decision.source, 'native monitor', 'The monitor, rather than a window-only guess, selects the size');
  }
  assert.equal(ui.autoInterfaceScale(0, 0, null), 100, 'Unavailable monitor and viewport data keep the baseline size');
  const fourK = nativeDisplay(3840, 2160);
  assert.equal(
    ui.autoInterfaceScale(1280, 800, fourK),
    200,
    'A nonmaximized 4K window keeps the monitor readability benefit'
  );
  assert.equal(
    ui.autoInterfaceScale(960, 700, fourK),
    160,
    'A small window limits enlargement without removing the monitor benefit'
  );
  assert.equal(ui.autoInterfaceScale(640, 480, fourK), 105, 'The minimum window keeps controls reachable');
  assert.equal(
    ui.autoInterfaceScale(1440, 955, {
      ...nativeDisplay(3840, 2160, 1.5),
      devicePixelRatio: 1.25,
      screenDevicePixelRatio: 1.5
    }),
    160,
    'Fractional Wayland uses the actual window surface ratio rather than applying the screen factor twice'
  );
  assert.equal(
    ui.autoInterfaceScale(1440, 955, { ...fourK, logicalDpiX: 192, logicalDpiY: 192 }),
    200,
    'Informational DPI is never multiplied into the native surface ratio'
  );
  assert.equal(
    ui.cleanDisplayMetrics({ ...fourK, monitorPixelWidth: Infinity }),
    null,
    'Nonfinite native dimensions are rejected'
  );
  assert.equal(
    ui.cleanDisplayMetrics({ ...fourK, devicePixelRatio: '2' }),
    null,
    'A string cannot silently replace a native numeric display ratio'
  );
  context.window.screen = { width: 2560, height: 1440 };
  context.window.devicePixelRatio = 1.5;
  assert.equal(
    ui.autoInterfaceScale(1440, 955, null),
    135,
    'The browser monitor fallback uses logical pixels already scaled by the system'
  );
  delete context.window.screen;
  delete context.window.devicePixelRatio;
  const scalingState = { settings: { ...ui.state.snapshot.settings }, metrics: ui.state.snapshot.displayMetrics };
  ui.state.snapshot.settings = { ...ui.state.snapshot.settings, autoScale: true, interfaceScale: 175, fontScale: 125 };
  const beforeDisplayEvent = nativeRequests.length;
  const unsavedDisplayForm = 'unsaved settings remain here';
  node('#main-content').innerHTML = unsavedDisplayForm;
  nativeMessageListener({ data: { event: 'displayMetricsChanged', data: nativeDisplay(2560, 1440) } });
  assert.equal(
    context.document.documentElement.style.zoom,
    '1.35',
    'Moving onto a 1440p monitor applies its automatic size'
  );
  nativeMessageListener({ data: { event: 'displayMetricsChanged', data: fourK } });
  assert.equal(context.document.documentElement.style.zoom, '2', 'Moving onto a 4K monitor updates the existing view');
  assert.equal(
    context.document.documentElement.style['--font-scale'],
    '1.25',
    'Automatic sizing scales text through interface zoom once, preserving the extra text preference'
  );
  assert(
    ui.scaleStatus(ui.state.snapshot.settings).includes('text 250%'),
    'The effective text size includes the interface and the saved preference'
  );
  assert.equal(node('#main-content').innerHTML, unsavedDisplayForm, 'Monitor changes preserve unsaved form content');
  assert.equal(nativeRequests.length, beforeDisplayEvent, 'Monitor movement never saves over user settings');
  assert.equal(ui.state.snapshot.settings.interfaceScale, 175, 'Monitor changes preserve the saved manual size');
  nativeMessageListener({ data: { event: 'displayMetricsChanged', data: nativeDisplay(3840, 2160, 2) } });
  assert.equal(
    context.document.documentElement.style.zoom,
    '1',
    'A 4K monitor at 200% system scaling gets no duplicate enlargement'
  );
  ui.state.snapshot.settings.autoScale = false;
  ui.state.snapshot.settings.interfaceScale = 110;
  nativeMessageListener({ data: { event: 'displayMetricsChanged', data: fourK } });
  assert.equal(context.document.documentElement.style.zoom, '1.1', 'Manual opt-out survives monitor and DPI changes');
  nativeMessageListener({ data: { event: 'displayMetricsChanged', data: { monitorPixelWidth: 0 } } });
  assert.equal(
    ui.state.snapshot.displayMetrics.monitorPixelWidth,
    3840,
    'Malformed monitor events preserve the last valid measurement'
  );
  ui.state.snapshot.settings = scalingState.settings;
  ui.state.snapshot.displayMetrics = scalingState.metrics;
  ui.state.snapshot.settings.autoScale = true;
  ui.state.snapshot.settings.interfaceScale = 175;
  context.window.innerWidth = 2560;
  context.window.innerHeight = 1440;
  ui.applyAppearance();
  assert.equal(context.document.documentElement.style.zoom, '1.5', 'Auto applies actual window fit');
  assert.equal(ui.state.snapshot.settings.interfaceScale, 175, 'Auto preserves saved manual choice');
  let prevented = 0;
  await ui.handleInterfaceShortcut({
    key: '+',
    ctrlKey: true,
    preventDefault() {
      prevented++;
    }
  });
  assert.equal(prevented, 1, 'Interface shortcut suppresses native browser zoom');
  assert.equal(ui.state.snapshot.settings.autoScale, false, 'Manual keyboard sizing turns Auto off');
  assert(
    nativeRequests.some(
      (request) =>
        request.action === 'savePreferences' &&
        request.preferences.autoScale === false &&
        request.preferences.interfaceScale === 175
    ),
    'Keyboard shortcut persists manual mode'
  );
  await ui.handleInterfaceShortcut({ key: '0', ctrlKey: true, preventDefault() {} });
  assert.equal(ui.state.snapshot.settings.interfaceScale, 100, 'Ctrl+0 explicitly chooses manual 100%');
  context.window.innerWidth = 1440;
  context.window.innerHeight = 980;
  ui.state.snapshot.settings.fontScale = 150;
  ui.applyAppearance();
  assert.equal(context.document.documentElement.style['--font-scale'], '1.5', 'Text size applies one global factor');
  const seasonalArtworkOrder = [
    '1f386', '1f387', '2728', '1f383', '1f47b', '1f987',
    '1f342', '2744', '2603', '1f332', '1f338', '1f337',
    '1f343', '2600', '1f33b', '1f33f', '1f341', '1f330'
  ];
  const renderedSeasonalArtwork = new Set();
  for (const season of ['newyear', 'halloween', 'winter', 'spring', 'summer', 'autumn']) {
    ui.renderSeasonalEffects({ seasonalThemes: true, seasonalEffects: true, reducedMotion: false }, season);
    const overlay = node('#seasonal-effects');
    assert.equal(overlay.hidden, false, `${season} effects appear when selected`);
    assert.equal(
      (overlay.innerHTML.match(/class="seasonal-particle /g) || []).length,
      18,
      'At most eighteen lightweight seasonal emoji exist'
    );
    assert(
      !overlay.innerHTML.includes('button') && !overlay.innerHTML.includes('<svg') && !overlay.innerHTML.includes('style='),
      'Seasonal particles use one cached sprite without interactive controls or inline styling'
    );
    const glyphs = [...overlay.innerHTML.matchAll(/class="seasonal-glyph glyph-u([a-f0-9]+)" aria-hidden="true">([^<]+)<\/span>/gu)];
    assert.equal(glyphs.length, 18, 'Every animated particle has its own local artwork cell');
    for (const [, codePoint, symbol] of glyphs) {
      assert.equal(symbol.codePointAt(0).toString(16), codePoint, 'Artwork selects the cell for its actual emoji');
      assert(seasonalArtworkOrder.includes(codePoint), 'Only the eighteen bundled cells can be selected');
      renderedSeasonalArtwork.add(codePoint);
    }
  }
  assert.deepEqual([...renderedSeasonalArtwork].sort(), [...seasonalArtworkOrder].sort(), 'Every bundled seasonal emoji is used');
  ui.renderSeasonalEffects({ seasonalThemes: true, seasonalEffects: true, reducedMotion: true }, 'halloween');
  assert(node('#seasonal-effects').hidden, 'Reduced motion suppresses seasonal effects entirely');
  assert.equal(node('#seasonal-effects').innerHTML, '', 'Reduced motion removes animated nodes');
  ui.renderSeasonalEffects({ seasonalThemes: false, seasonalEffects: true }, 'halloween');
  assert(node('#seasonal-effects').hidden, 'Disabling seasonal themes suppresses effects');
  ui.renderSeasonalEffects({ seasonalThemes: true, seasonalEffects: false }, 'halloween');
  assert(node('#seasonal-effects').hidden, 'Separate effects toggle suppresses effects');
  ui.renderSeasonalEffects({ seasonalThemes: true, seasonalEffects: true, highContrast: true }, 'halloween');
  assert(node('#seasonal-effects').hidden, 'High contrast removes decorative moving emoji');
  osReducedMotion = true;
  ui.renderSeasonalEffects({ seasonalThemes: true, seasonalEffects: true }, 'halloween');
  assert(node('#seasonal-effects').hidden, 'System reduced-motion preference suppresses effects');
  assert(node('#seasonal-effects-status').textContent.includes('system’s Reduce motion'), 'System-paused effects explain why they are hidden');
  osReducedMotion = false;
  ui.renderSeasonalEffects({ seasonalThemes: true, seasonalEffects: true }, 'halloween');
  assert(node('#seasonal-effects-status').textContent.includes('emoji are on'), 'Effects status updates when system motion resumes');
  const effectPlatform = ui.state.snapshot.platform;
  for (const platform of ['windows', 'linux']) {
    ui.state.snapshot.platform = platform;
    for (const season of ['halloween', 'winter', 'spring', 'summer', 'autumn', 'newyear']) {
      ui.renderSeasonalEffects({ seasonalThemes: true, seasonalEffects: true, useSystemTheme: false }, season);
      assert.equal(node('#seasonal-effects').hidden, false, `${platform} ${season} effects remain enabled`);
      assert.equal((node('#seasonal-effects').innerHTML.match(/class="seasonal-particle /g) || []).length, 18);
    }
  }
  ui.state.snapshot.platform = effectPlatform;
  ui.renderSeasonalEffects({ seasonalThemes: true, seasonalEffects: true }, 'halloween');
  assert(
    node('#seasonal-effects').innerHTML.includes('🎃') && node('#seasonal-effects').innerHTML.includes('👻'),
    'Halloween uses actual pumpkin and ghost emoji'
  );
  ui.renderSeasonalScenery({ seasonalThemes: true, reducedMotion: true }, 'halloween');
  assert(
    !node('#seasonal-scenery').hidden && node('#seasonal-scenery').innerHTML.includes('<path'),
    'Static original corner details remain with reduced motion'
  );
  ui.renderSeasonalScenery({ seasonalThemes: false }, null);
  assert(
    node('#seasonal-scenery').hidden && node('#seasonal-scenery').innerHTML === '',
    'Turning seasonal themes off removes background artwork'
  );
  assert(ui.festiveWinter('winter', new Date('2026-12-14T12:00:00')), 'December winter includes muted festive lights');
  assert(ui.festiveWinter('winter', new Date('2027-01-06T12:00:00')), 'The first January days retain holiday details');
  assert(
    !ui.festiveWinter('winter', new Date('2027-02-01T12:00:00')),
    'A manually selected winter theme outside the holiday period uses frost without lights'
  );
  assert(
    !ui.festiveWinter('summer', new Date('2026-12-14T12:00:00')),
    'A manual seasonal theme does not inherit winter decorations'
  );
  assert.equal(
    ui.seasonLabel('autumn', new Date('2026-10-05T12:00:00')),
    'Autumn · Copper Cascades',
    'The autumn badge carries its own name'
  );
  assert.equal(
    ui.seasonLabel('winter', new Date('2027-02-01T12:00:00')),
    'Winter · Frost on the Trail',
    'Winter outside the holidays names the frost'
  );
  assert.equal(
    ui.seasonLabel('winter', new Date('2026-12-14T12:00:00')),
    'Winter · Holiday Lights',
    'December winter names its holiday lights'
  );
  assert.equal(ui.seasonLabel('spring'), 'Spring · Morning Thaw');
  assert.equal(ui.seasonLabel('summer'), 'Summer · High Desert Sun');
  assert.equal(ui.seasonLabel('halloween'), 'Late October · Halloween', 'The Halloween label is unchanged');
  assert.equal(ui.seasonLabel('newyear'), 'New Year’s Eve · Fireworks');
  assert.equal(ui.seasonLabel(null), '', 'No active season leaves the badge text empty');
  assert(
    ['spring', 'summer', 'autumn', 'winter', 'halloween', 'newyear'].every(
      (s) => !/atmosphere/.test(ui.seasonLabel(s, new Date('2026-07-01T12:00:00')))
    ),
    'No offered season falls back to the generic atmosphere label'
  );
  nativeMessageListener({ data: { event: 'windowVisibilityChanged', data: { hidden: true } } });
  assert(
    context.document.documentElement.classList.contains('app-hidden'),
    'Native minimize events pause the seasonal animation'
  );
  nativeMessageListener({ data: { event: 'windowVisibilityChanged', data: { hidden: false } } });
  assert(
    !context.document.documentElement.classList.contains('app-hidden'),
    'Native restoration resumes CSS animation'
  );
  context.document.hidden = true;
  ui.syncWindowVisibility();
  nativeMessageListener({ data: { event: 'windowVisibilityChanged', data: { hidden: false } } });
  assert(
    context.document.documentElement.classList.contains('app-hidden'),
    'A native visible event cannot override a hidden renderer page'
  );
  context.document.hidden = false;
  ui.syncWindowVisibility();
  const fullSource = fs.readFileSync(path.join(appRoot, 'ui/app.js'), 'utf8'),
    compactSource = compact(fullSource);
  const effectFunction = fullSource.slice(
    fullSource.indexOf('function renderSeasonalEffects('),
    fullSource.indexOf('async function refresh(')
  );
  assert(
    !/requestAnimationFrame|setInterval|setTimeout/.test(effectFunction),
    'Effects have no continuous JavaScript animation timers'
  );
  const css = compact(fs.readFileSync(path.join(appRoot, 'ui/style.css'), 'utf8'));
  assert(
    contains(css, "background-image:url('assets/seasonal-emoji.svg')") &&
      contains(css, 'background-size:600% 300%') &&
      /\.seasonal-glyph\{[^}]*width:1em;height:1em;[^}]*color:transparent;/.test(css) &&
      !css.includes('Toolkit Seasonal Emoji') && !css.includes('.ttf'),
    'Seasonal artwork scales with the particle size and has no runtime color-font dependency'
  );
  for (const [index, name] of seasonalArtworkOrder.entries()) {
    const x = index % 6 * 20, y = Math.floor(index / 6) * 50;
    assert(contains(css, `.glyph-u${name}{background-position:${x}% ${y}%}`), `Artwork cell ${name} stays inside the six-by-three atlas`);
  }
  const seasonalSprite = fs.readFileSync(path.join(appRoot, 'ui/assets/seasonal-emoji.svg'), 'utf8');
  assert(
    /viewBox=["']0 0 768 384["']/.test(seasonalSprite) &&
      !/<(?:script|foreignObject)\b/i.test(seasonalSprite) &&
      !/(?:href|src)\s*=\s*["'](?!#)/i.test(seasonalSprite),
    'The sprite has eighteen fixed cells and no executable content or external resources'
  );
  assert(
    /\.seasonal-effects\{[^}]*left:0;top:0;[^}]*width:var\(--toolkit-viewport-width/.test(css),
    'Emoji occupy the full window rather than only edge strips'
  );
  assert(
    !contains(css, 'trail-relic-fall') && !fullSource.includes('seasonalDrawings'),
    'Old SVG border-relic animation is removed'
  );
  for (const season of ['spring', 'summer', 'autumn', 'winter', 'halloween'])
    assert(
      contains(css, `:root[data-season="${season}"]{--season-backdrop:`),
      `${season} has its own background lighting and hero treatment`
    );
  assert(
    contains(css, '.seasonal-scenery{position:fixed;z-index:0;') &&
      contains(css, ':root[data-contrast="high"] .seasonal-scenery{display:none}'),
    'Static corner details sit behind content and respect high contrast'
  );
  assert(!/font-size:\s*\d+(?:\.\d+)?px/.test(css), 'All authored text sizes use rem or inherited sizing');
  assert(
    (css.match(/font-size:[\d.]+rem/g) || []).length >= 159,
    'All former fixed labels and buttons follow the text scale'
  );
  assert.equal((css.match(/var\(--font-scale\)/g) || []).length, 1, 'Font factor is applied exactly once at the root');
  assert(
    contains(css, 'body{width:auto;min-width:0}'),
    'Body uses available layout width instead of viewport width plus scrollbar'
  );
  assert(
    contains(css, '.brand strong{font-size:1.071429rem;letter-spacing:.9px;white-space:nowrap') &&
      contains(css, '.brand b{font-size:1.142857rem;letter-spacing:2.3px;white-space:nowrap'),
    'Brand has deliberate fitting typography and does not split individual words'
  );
  const linuxShell = compact(fs.readFileSync(path.join(appRoot, 'src/linux_main.cpp'), 'utf8'));
  assert(
    contains(linuxShell, 'brandFits:') &&
      contains(linuxShell, 'el.scrollWidth<=el.clientWidth+1') &&
      contains(linuxShell, 'el.clientHeight<=Math.ceil(parseFloat(getComputedStyle(el).lineHeight))+1'),
    'Native Qt render test verifies real brand width and single-line height'
  );
  assert(!/(--[\w-]+):var\(\1\)/.test(css), 'Theme properties do not contain cyclic self references');
  assert(!contains(css, 'overflow-x:hidden'), 'Overflow is fixed by layout rather than hidden');
  assert(contains(css, '.asset-list{max-height:none;overflow:visible}'), 'Conflict paths use primary page scrolling');
  assert(
    !/\.profile-(row|mod)[\w-]*[^{]*\{[^}]*overflow:(auto|scroll)/.test(css),
    'Profiles avoid competing nested scrollers'
  );
  ui.markScrollActivity();
  assert(context.document.documentElement.classList.contains('is-scrolling'), 'Scrolling reveals themed thumbs');
  const scrollTimer = [...timers.entries()].find(([, timer]) => timer.delay === 900);
  assert(scrollTimer, 'Thumbs hide after event-driven inactivity');
  timers.delete(scrollTimer[0]);
  scrollTimer[1].callback();
  assert(!context.document.documentElement.classList.contains('is-scrolling'), 'Thumbs return to quiet state');
  ui.markKeyboardNavigation({ key: 'Tab' });
  assert(
    context.document.documentElement.classList.contains('keyboard-navigation'),
    'Keyboard navigation keeps thumbs visible'
  );
  assert(
    contains(css, '.keyboard-navigation *') && contains(css, 'html[data-contrast="high"] *'),
    'Keyboard and high-contrast scrollbar visibility is styled'
  );
  assert(
    contains(compactSource, "document.addEventListener('scroll',markScrollActivity,{passive:true,capture:true})"),
    'Scroll listener is passive and event driven'
  );
  assert(
    contains(css, '.app-hidden .seasonal-particle{animation-play-state:paused}'),
    'Hidden or minimized windows pause CSS animation'
  );
  assert(/\.seasonal-effects\{[^}]*pointer-events:none/.test(css), 'Seasonal overlay cannot intercept clicks');
  const appHtml = fs.readFileSync(path.join(appRoot, 'ui/index.html'), 'utf8');
  assert(
    appHtml.includes('id="seasonal-effects" class="seasonal-effects" aria-hidden="true"') &&
      appHtml.includes('id="seasonal-scenery" class="seasonal-scenery" aria-hidden="true"'),
    'Decorative overlays are hidden from assistive technology'
  );
  assert(appHtml.includes("img-src 'self' data:"), 'Seasonal artwork stays under the existing local-only image CSP');

  ui.state.busy = false;
  ui.state.nexus = { connected: false, premium: false };
  ui.connectNexus();
  const login = node('#modal-content').innerHTML;
  assert(
    login.includes('Continue on Nexus Mods') && login.includes('id="nexus-login-start" class="button primary">'),
    'Browser authorization is the main, enabled connection action while the slug state is unknown'
  );
  assert(
    login.includes(
      '<details class="disclosure" id="nexus-key-disclosure"><summary>Use my personal API key instead</summary>'
    ),
    'Personal key entry is a collapsed secondary disclosure'
  );
  assert(
    /<input class="input full-width" id="nexus-api-key" type="password" autocomplete="off"/.test(login) &&
      (login.match(/type="password"/g) || []).length === 1,
    'Only the personal-key field is masked; no Nexus password is requested'
  );
  assert(
    login.includes('testing and personal use') &&
      login.includes('credential store') &&
      login.includes('data-action="api-keys"'),
    'Key disclosure explains the policy scope, OS storage and links to the Nexus API keys page'
  );
  const previousAutoCheck = ui.state.snapshot.settings.autoCheckUpdates;
  ui.state.snapshot.settings.autoCheckUpdates = false;
  const beforeKey = nativeRequests.length;
  node('#nexus-api-key').value = 'synthetic-personal-key-0123456789';
  actionFixtures.set('nexusConnect', {
    connected: true,
    premium: false,
    tierVerified: true,
    name: 'Key Tester',
    browserLoginAvailable: false
  });
  await node('#nexus-key-submit').onclick();
  const keyRequests = nativeRequests.slice(beforeKey).filter((request) => request.action === 'nexusConnect');
  assert.equal(keyRequests.length, 1, 'Submitting the key posts exactly one nexusConnect request');
  assert.equal(
    keyRequests[0].apiKey,
    'synthetic-personal-key-0123456789',
    'nexusConnect carries the entered key verbatim'
  );
  assert.equal(node('#nexus-api-key').value, '', 'The key field is cleared when the request leaves');
  assert(
    ui.state.nexus.connected && ui.state.nexus.name === 'Key Tester' && !node('#modal').open,
    'A validated key finishes the connection exactly like browser login'
  );
  assert(!JSON.stringify(ui.state).includes('synthetic-personal-key'), 'The key is not retained in interface state');
  actionFixtures.delete('nexusConnect');
  ui.state.nexus = { connected: false, premium: false, browserLoginAvailable: false };
  assert(
    ui.nexusAccountMarkup().includes('Connect account') && !ui.nexusAccountMarkup().includes('Connect in browser'),
    'Sidebar stops promising browser sign-in when no slug is registered'
  );
  ui.connectNexus();
  const unavailable = node('#modal-content').innerHTML;
  assert(
    unavailable.includes('id="nexus-login-start" class="button primary" disabled>') &&
      unavailable.includes('not available in this build yet'),
    'Browser sign-in is shown as unavailable with a one-line reason when the slug is empty'
  );
  assert(
    unavailable.includes('<details class="disclosure" id="nexus-key-disclosure" open>'),
    'The key disclosure opens by itself when browser sign-in is unavailable'
  );
  actionErrors.set('nexusConnect', 'Paste your own Nexus API key, with no spaces or line breaks.');
  node('#nexus-api-key').value = 'short';
  await node('#nexus-key-submit').onclick();
  assert.equal(
    nativeRequests.filter((request) => request.action === 'nexusConnect').length,
    2,
    'A rejected key still goes to the native validator exactly once'
  );
  assert(
    node('#nexus-key-error').textContent.includes('Paste your own Nexus API key') &&
      node('#nexus-key-error').hidden === false,
    'The backend error text is shown inside the dialog'
  );
  assert.equal(node('#nexus-api-key').value, '', 'The key field is cleared even when validation fails');
  assert(
    !ui.state.nexus.connected && node('#modal').open,
    'A rejected key leaves the dialog open and the account disconnected'
  );
  actionErrors.delete('nexusConnect');
  node('#modal').close();
  ui.state.nexus = { connected: false, premium: false };
  ui.state.snapshot.settings.autoCheckUpdates = previousAutoCheck;
  const authoring = context.window.FIELD_GUIDE.filter((article) => article.modding).sort(
    (a, b) => a.chapter - b.chapter
  );
  assert.equal(authoring.length, 9, 'Make mods has nine grounded beginner chapters');
  assert(
    authoring[3].body.includes('VER_UE4_11') && authoring[3].body.includes('Get-FileHash'),
    'Asset-editing chapter includes the actual version and round-trip check'
  );
  assert(
    authoring[4].body.includes('--version V3') &&
      authoring[4].body.includes('../../../') &&
      authoring[4].body.includes('BendGame/'),
    'Packaging chapter specifies the exact legacy format and mount hierarchy'
  );
  assert(
    authoring[5].body.includes('Added from a file') &&
      authoring[5].body.includes('external PAKs aside') &&
      authoring[5].body.includes('approved local package'),
    'Development tests distinguish reviewed local packages from unmanaged external files'
  );
  const allowedResources = fs.readFileSync(path.join(appRoot, 'src/modding_resources.hpp'), 'utf8');
  for (const article of authoring)
    for (const resource of article.resources)
      assert(
        allowedResources.includes('"' + resource.id + '"'),
        'Every tutorial resource is in the shared native allowlist'
      );
  assert(
    !context.window.FIELD_GUIDE.some((article) =>
      /development handoff|reference workspace|ChatGPT|Codex|generated by AI/i.test(article.body)
    ),
    'Public guide has no private handoff or assistant branding'
  );
  const savedSnapshot = ui.state.snapshot;
  ui.state.loading = false;
  ui.state.snapshot = { ...savedSnapshot, sandbox: false };
  assert.equal(
    ui.configureSandboxSmoke({ fontScale: 150, view: 'help' }),
    false,
    'Display self-test refuses a live nonsandbox state'
  );
  ui.state.snapshot = { ...savedSnapshot, sandbox: true };
  const beforeDisplayTest = nativeRequests.length;
  assert(
    ui.configureSandboxSmoke({ fontScale: 150, view: 'help' }),
    'Sandbox render check applies a real larger-font Help view'
  );
  assert.equal(ui.state.snapshot.settings.fontScale, 150, 'Render check changes the actual UI text scale');
  assert(
    !nativeRequests.slice(beforeDisplayTest).some((request) => request.action === 'saveSettings'),
    'Render QA never persists appearance changes'
  );
  assert.equal(
    ui.configureSandboxSmoke({ fontScale: 200, view: 'help' }),
    false,
    'Render QA is bounded to supported text size'
  );
  context.window.__DRIFTER_SANDBOX_SMOKE__ = true;
  const smokeRefreshed = ui.normalizeSnapshot({ ...savedSnapshot, sandbox: true, settings: { fontScale: 100 } });
  assert.equal(
    smokeRefreshed.settings.fontScale,
    150,
    'Private render preferences survive late read-only startup snapshots'
  );
  context.window.__DRIFTER_SANDBOX_SMOKE__ = false;
  assert.equal(
    ui.normalizeSnapshot({ ...savedSnapshot, sandbox: true, settings: { fontScale: 100 } }).settings.fontScale,
    100,
    'Public snapshots never inherit private smoke preferences'
  );
  ui.state.snapshot = savedSnapshot;
  ui.state.snapshot.settings.fontScale = 100;
  ui.state.view = 'overview';
  ui.applyAppearance();
  await ui.loadCrashLogs();
  const crashPanel = ui.renderCrashLogs();
  assert(
    crashPanel.includes('No game installation is selected') && crashPanel.includes('toolkit-error.log'),
    'Toolkit logs remain useful when the game is not selected'
  );
  assert(
    crashPanel.includes('data-crash-summary="game-dump"') &&
      !crashPanel.includes('data-crash-read="game-dump"') &&
      crashPanel.includes('never opened as raw data'),
    'Memory dumps offer only a native crash summary, never a raw preview'
  );
  actionFixtures.set('crashLogSummary', {
    id: 'game-dump',
    name: 'Game.dmp',
    status: 'complete',
    headline: 'Days Gone crashed 56 seconds after starting. <script>alert(1)</script>',
    crashedAt: 1792502768,
    secondsSinceStart: 56,
    exception: { code: '0xC0000005', name: 'Access violation', access: 'read', nullAddress: true },
    location: { module: 'DaysGone.exe', offset: '0x904178', category: 'game', label: 'part of the game' },
    fingerprint: 'c0000005:daysgone.exe+0x904178',
    memory: { outOfMemory: false, usedPhysicalBytes: 4724464025, totalPhysicalBytes: 51539607552 },
    modules: { total: 9, listed: [{ name: '<img src=x>.dll', category: 'overlay', label: 'an overlay program' }] },
    sameCrash: { occurrences: 2 },
    copies: ['crash-reporter-copy.dmp'],
    advice: ['Turn off this overlay and test again.'],
    limits: 'Function names are not available.',
    redacted: true
  });
  await ui.summarizeCrashDump('game-dump');
  const summaryHtml = node('#modal-content').innerHTML;
  assert(
    summaryHtml.includes('&lt;script&gt;alert(1)&lt;/script&gt;') &&
      !summaryHtml.includes('<script>') &&
      summaryHtml.includes('&lt;img src=x&gt;.dll') &&
      !summaryHtml.includes('<img src=x>'),
    'Crash summary text is escaped before display'
  );
  assert(
    summaryHtml.includes('This crash happened 2 times') &&
      summaryHtml.includes('DaysGone.exe + 0x904178') &&
      summaryHtml.includes('c0000005:daysgone.exe+0x904178') &&
      summaryHtml.includes('56 seconds') &&
      summaryHtml.includes('crash-reporter-copy.dmp') &&
      summaryHtml.includes('Turn off this overlay'),
    'Crash summary shows timing, location, fingerprint, repeats, copies and advice'
  );
  const beforeSummaryCopy = nativeRequests.length;
  await node('#copy-crash-summary').onclick();
  const summaryCopy = nativeRequests.slice(beforeSummaryCopy).find((request) => request.action === 'crashLogCopy');
  assert(
    summaryCopy && summaryCopy.id === 'game-dump' && summaryCopy.summary === true && !('text' in summaryCopy),
    'Copying a crash summary asks the native side for a fresh redacted summary by ID'
  );
  node('#modal').close();
  actionFixtures.set('crashLogSummary', {
    id: 'game-dump',
    name: 'Game.dmp',
    status: 'complete',
    headline: 'Days Gone crashed.',
    crashedAt: 1792502768,
    activeMods: {
      quality: 'launch',
      launchStartedAt: 1792502712,
      setRecordedAt: 1792500000,
      packages: [
        { name: 'Steady <b>Aim</b>', file: 'Steady_P.pak', location: 'saved', managed: true },
        { name: 'Other_P.pak', file: '~mods/Other_P.pak', location: 'game', managed: false }
      ],
      comparison: {
        against: 'clean-run',
        at: 1792400100,
        added: ['Other_P.pak'],
        removed: [],
        updated: [],
        unchanged: false
      }
    },
    limits: 'Function names are not available.',
    redacted: true
  });
  await ui.summarizeCrashDump('game-dump');
  const modsHtml = node('#modal-content').innerHTML;
  assert(
    modsHtml.includes('Mods during this crash') &&
      modsHtml.includes('2 mod files were active when this game session started') &&
      modsHtml.includes('Steady &lt;b&gt;Aim&lt;/b&gt;') &&
      !modsHtml.includes('<b>Aim</b>') &&
      modsHtml.includes('not managed by the toolkit') &&
      modsHtml.includes('<strong>Added:</strong> Other_P.pak') &&
      modsHtml.includes('the last run without a crash') &&
      modsHtml.includes('id="crash-find-mod"'),
    'A crash summary lists the escaped active mods, what changed since a clean run and offers the finder'
  );
  assert(
    ui.renderCrashMods({ quality: 'none' }).includes('Not recorded') &&
      ui
        .renderCrashMods({
          quality: 'recorded-before',
          setRecordedAt: 1792500000,
          packages: [],
          comparison: { against: 'previous-setup', at: 1, added: [], removed: [], updated: [], unchanged: true }
        })
        .includes('unlikely to be the cause'),
    'Unrecorded and unchanged setups are explained plainly'
  );
  node('#modal').close();

  // Guided bad-mod finder: banner, round dialog, answers and the result choice.
  const finderSnapshot = ui.state.snapshot.finder;
  ui.state.snapshot.finder = {
    active: true,
    stage: 'halving',
    round: 2,
    roundsLeft: 2,
    originalCount: 4,
    testing: [
      { id: 'a', name: 'First <i>mod</i>' },
      { id: 'b', name: 'Second mod' }
    ],
    suspects: [],
    combination: [],
    culprit: null
  };
  const finderBanner = ui.renderFinderBanner();
  assert(
    finderBanner.includes('Round 2') &&
      finderBanner.includes('2 of your 4 mods are enabled') &&
      finderBanner.includes('data-action="finder-open"') &&
      finderBanner.includes('data-action="finder-stop"'),
    'A running finder shows its round and offers to continue or stop on every page'
  );
  ui.openFinder();
  const roundHtml = node('#modal-content').innerHTML;
  assert(
    roundHtml.includes('Round 2 · about 2 to go') &&
      roundHtml.includes('First &lt;i&gt;mod&lt;/i&gt;') &&
      roundHtml.includes('id="finder-still"') &&
      roundHtml.includes('id="finder-gone"') &&
      roundHtml.includes('id="finder-launch"'),
    'A finder round lists the escaped test set and both answers'
  );
  const beforeAnswer = nativeRequests.length;
  await node('#finder-still').onclick();
  const answer = nativeRequests.slice(beforeAnswer).find((request) => request.action === 'finderAnswer');
  assert(answer && answer.problem === true, 'Answering sends only the yes/no result to the native finder');
  ui.state.snapshot.finder = {
    active: true,
    stage: 'found',
    round: 4,
    roundsLeft: 0,
    originalCount: 4,
    testing: [{ id: 'c', name: 'Third mod' }],
    culprit: { id: 'c', name: 'Third mod' },
    combination: []
  };
  ui.openFinder();
  assert(
    node('#modal-content').innerHTML.includes('Third mod causes the problem') && node('#finder-keep-off'),
    'A found mod can be left disabled'
  );
  const beforeFinish = nativeRequests.length;
  await node('#finder-keep-off').onclick();
  const finish = nativeRequests.slice(beforeFinish).find((request) => request.action === 'finderFinish');
  assert(finish && finish.disableCulprit === true, 'Keeping the found mod disabled asks the native finder to finish');
  ui.state.snapshot.finder = {
    active: true,
    stage: 'not-mods',
    round: 2,
    originalCount: 4,
    testing: [],
    combination: []
  };
  ui.openFinder();
  assert(
    node('#modal-content').innerHTML.includes('Your managed mods are not the cause') && node('#finder-restore-all'),
    'A problem without managed mods restores everything'
  );
  node('#modal').close();
  ui.state.snapshot.finder = finderSnapshot;

  // Toolkit updates: a signed ZIP found in Downloads is offered once, installs only by token.
  actionFixtures.set('updateCheckDownloads', {
    found: true,
    token: 'native-update-token',
    version: '1.2.0',
    name: 'Drifters Toolkit <b>1.2.0</b>.zip'
  });
  ui.state.update.lastCheck = 0;
  await ui.checkForUpdate(true);
  const updateBanner = ui.renderUpdateBanner();
  assert(
    updateBanner.includes('Drifter’s Toolkit 1.2.0 is ready to install') &&
      updateBanner.includes('&lt;b&gt;1.2.0&lt;/b&gt;') &&
      updateBanner.includes('data-action="update-install"') &&
      updateBanner.includes('data-action="update-dismiss"'),
    'A downloaded update shows an escaped banner with install and dismiss'
  );
  const quietChecks = nativeRequests.filter((request) => request.action === 'updateCheckDownloads').length;
  await ui.checkForUpdate(true);
  assert.equal(
    nativeRequests.filter((request) => request.action === 'updateCheckDownloads').length,
    quietChecks,
    'Quiet update checks are throttled'
  );
  assert(
    updateBanner.includes('update-jump') && updateBanner.includes('Signed by the author'),
    'The update card shows the version jump and the signed mark'
  );
  ui.confirmUpdate(ui.state.update.offer);
  assert(
    node('#modal-content').innerHTML.includes('signed by the author') &&
      node('#modal-content').innerHTML.includes('data-step="unpack"') &&
      node('#modal-content').innerHTML.includes('updater-progress') &&
      node('#update-install-confirm'),
    'Installing an update asks first, shows its steps and explains what stays'
  );
  actionFixtures.set('updateInstall', { installed: true, restarting: true, version: '1.2.0' });
  const beforeInstall = nativeRequests.length;
  await node('#update-install-confirm').onclick();
  const install = nativeRequests.slice(beforeInstall).find((request) => request.action === 'updateInstall');
  assert(
    install && install.token === 'native-update-token' && !('path' in install) && !('zip' in install),
    'The install request carries only the native token'
  );
  assert(
    ui.state.update.offer === null && ui.renderUpdateBanner() === '',
    'The banner clears once the update installs'
  );
  node('#modal').close();
  actionFixtures.delete('updateInstall');
  // After the restart one Updated window carries What's new, and What's new is not shown a second time.
  ui.showUpdated('1.0.3', '1.0.4');
  assert(
    node('#modal-content').innerHTML.includes('Updated to') && node('#modal-content').innerHTML.includes('update-jump'),
    'The first start after an update shows the Updated window'
  );
  node('#modal').close();
  actionFixtures.set('updateCheckDownloads', { found: false });
  ui.state.update.status = { supported: false, reason: 'Updates are not installed in sandbox mode.', version: '1.0.0' };
  assert(
    ui.renderUpdatePanel().includes('not installed in sandbox mode') &&
      ui.renderUpdatePanel().includes('version 1.0.0'),
    'The update panel explains when a copy cannot update itself'
  );
  ui.state.update = { status: null, offer: null, lastCheck: 0 };
  actionFixtures.delete('updateCheckDownloads');

  // Catalog updates remain separate from program updates and accept only a native review token.
  const catalogSnapshot = ui.state.snapshot;
  const catalogState = ui.state.catalogUpdate;
  const catalogPreview = ui.state.preview;
  const catalogProfileSession = ui.state.profileSession;
  ui.state.preview = false;
  ui.state.profileSession = { active: false };
  ui.state.catalogUpdate = { status: null, offer: null, lastCheck: 0, checking: false };
  const catalogCandidate = {
    token: 'a'.repeat(64),
    revision: 2,
    minimumToolkitVersion: '1.0.4',
    modCount: 75,
    bytes: 2048,
    name: 'Drifters-Toolkit-Catalog-<img src=x onerror=bad>.zip'
  };
  assert.equal(ui.catalogOffer(null), null, 'A missing catalog response cannot create an offer');
  for (const candidate of [
    {},
    { ...catalogCandidate, token: '../unreviewed-file.zip' },
    { ...catalogCandidate, token: 'b'.repeat(63) },
    { ...catalogCandidate, revision: 0 },
    { ...catalogCandidate, revision: Number.MAX_SAFE_INTEGER + 1 },
    { ...catalogCandidate, modCount: -1 },
    { ...catalogCandidate, modCount: '75' }
  ]) {
    assert.equal(ui.catalogOffer({ candidate }), null, 'Malformed catalog metadata is not offered');
  }
  actionFixtures.set('catalogCheckDownloads', {
    revision: 0,
    usingPack: false,
    modCount: 74,
    candidate: catalogCandidate
  });
  await ui.checkForCatalog(true);
  const catalogBanner = ui.renderCatalogBanner();
  assert(
    catalogBanner.includes('Catalog revision 2 is ready') &&
      catalogBanner.includes('75 releases') &&
      catalogBanner.includes('data-action="catalog-review"') &&
      catalogBanner.includes('data-action="catalog-dismiss"') &&
      catalogBanner.includes('&lt;img src=x onerror=bad&gt;') &&
      !catalogBanner.includes('<img src=x onerror=bad>'),
    'The catalog banner shows its revision and escapes the downloaded filename'
  );
  const catalogQuietChecks = nativeRequests.filter((request) => request.action === 'catalogCheckDownloads').length;
  await ui.checkForCatalog(true);
  assert.equal(
    nativeRequests.filter((request) => request.action === 'catalogCheckDownloads').length,
    catalogQuietChecks,
    'Quiet catalog checks are throttled independently of toolkit updates'
  );
  ui.state.catalogUpdate.status = { usingPack: true, revision: 1, warning: '<script>bad</script>' };
  const catalogPanel = ui.renderCatalogPanel();
  assert(
    catalogPanel.includes('Catalog revision 1') &&
      catalogPanel.includes('&lt;script&gt;bad&lt;/script&gt;') &&
      !catalogPanel.includes('<script>bad</script>') &&
      catalogPanel.includes('mod files are downloaded and imported separately'),
    'Catalog settings escape warnings and explain that metadata does not download mods'
  );
  ui.confirmCatalog(catalogCandidate);
  assert(
    node('#modal-content').innerHTML.includes('does not download or enable') &&
      node('#modal-content').innerHTML.includes('&lt;img src=x onerror=bad&gt;'),
    'Catalog review explains its limited change and escapes filenames'
  );
  const beforeCatalogFailure = nativeRequests.length;
  const unchangedCatalogMods = JSON.stringify(ui.state.snapshot.mods);
  actionErrors.set('catalogInstall', 'The catalog pack changed after review.');
  await node('#confirm-action').onclick();
  const catalogInstall = nativeRequests.slice(beforeCatalogFailure).find((request) => request.action === 'catalogInstall');
  assert(
    catalogInstall &&
      catalogInstall.token === catalogCandidate.token &&
      Object.keys(catalogInstall).sort().join(',') === 'action,requestId,token',
    'Catalog installation sends only the reviewed token, with no caller path or revision'
  );
  assert.equal(ui.state.snapshot, catalogSnapshot, 'A failed catalog install preserves the current snapshot');
  assert.equal(JSON.stringify(ui.state.snapshot.mods), unchangedCatalogMods, 'A failed catalog install leaves the visible mod list intact');
  assert.equal(ui.state.catalogUpdate.status.revision, 1, 'A failed catalog install does not claim the new revision');
  actionErrors.delete('catalogInstall');
  node('#modal').close();
  ui.state.catalogUpdate.offer = catalogCandidate;
  actionFixtures.set('catalogInstall', { installed: true, usingPack: true, revision: 2, modCount: 75 });
  ui.confirmCatalog(catalogCandidate);
  const beforeCatalogSuccess = nativeRequests.length;
  await node('#confirm-action').onclick();
  assert.equal(ui.state.catalogUpdate.status.revision, 2, 'Only native success advances the displayed catalog revision');
  assert(
    nativeRequests.slice(beforeCatalogSuccess).some((request) => request.action === 'snapshot'),
    'Successful catalog installation refreshes the mod list from the native manager'
  );
  assert.equal(ui.renderCatalogBanner(), '', 'The catalog offer clears after installation');
  actionFixtures.delete('catalogInstall');
  actionFixtures.delete('catalogCheckDownloads');
  ui.state.snapshot = catalogSnapshot;
  ui.state.catalogUpdate = catalogState;
  ui.state.preview = catalogPreview;
  ui.state.profileSession = catalogProfileSession;
  node('#modal').close();

  // Signed replacement links show updates without an API connection or losing old owned releases.
  const beforeCatalogVersions = ui.state.snapshot;
  const beforeCatalogDownloads = ui.state.downloads;
  const oldCatalogRelease = {
    ...catalog.mods[0],
    id: 'catalog-old',
    name: 'Catalog release',
    version: '1.0',
    sha256: 'a'.repeat(64),
    imported: true,
    enabled: true,
    owned: true,
    supersededBy: 'catalog-middle'
  };
  const middleCatalogRelease = {
    ...oldCatalogRelease,
    id: 'catalog-middle',
    version: '2.0',
    sha256: 'b'.repeat(64),
    imported: false,
    enabled: false,
    owned: false,
    supersededBy: 'catalog-latest'
  };
  const latestCatalogRelease = {
    ...middleCatalogRelease,
    id: 'catalog-latest',
    version: '3.0',
    sha256: 'c'.repeat(64),
    supersededBy: undefined
  };
  ui.state.downloads = { updates: [] };
  ui.state.snapshot = {
    ...beforeCatalogVersions,
    mods: [oldCatalogRelease, middleCatalogRelease, latestCatalogRelease],
    installed: []
  };
  assert.equal(ui.latestCatalogRelease(oldCatalogRelease)?.id, 'catalog-latest', 'Catalog replacement chains resolve the newest release');
  const catalogVersionUpdate = ui.installedUpdate(oldCatalogRelease);
  assert(
    catalogVersionUpdate?.currentVersion === '1.0' &&
      catalogVersionUpdate?.version === '3.0' &&
      catalogVersionUpdate?.catalogId === 'catalog-latest',
    'An installed older catalog release has current and latest version details without Nexus API data'
  );
  const catalogVersionStatus = ui.statusFor(oldCatalogRelease);
  assert(
    catalogVersionStatus.label === 'Update available' &&
      catalogVersionStatus.note.includes('Installed 1.0') &&
      catalogVersionStatus.note.includes('latest 3.0'),
    'The installed mod status shows the newer signed catalog release'
  );
  assert(
    ui.libraryMods().some((mod) => mod.id === oldCatalogRelease.id) &&
      !ui.libraryMods().some((mod) => mod.id === middleCatalogRelease.id) &&
      ui.libraryMods().some((mod) => mod.id === latestCatalogRelease.id),
    'Owned old releases stay visible while unused superseded releases leave the Available list'
  );
  oldCatalogRelease.enabled = false;
  oldCatalogRelease.owned = false;
  assert.equal(ui.installedUpdate(oldCatalogRelease)?.version, '3.0', 'A disabled imported older release still shows its newer version');
  assert(ui.libraryMods().some((mod) => mod.id === oldCatalogRelease.id), 'An imported older release stays in the library when disabled');
  oldCatalogRelease.imported = false;
  assert(!ui.libraryMods().some((mod) => mod.id === oldCatalogRelease.id), 'Unused old releases are hidden after they leave the library');
  assert.equal(ui.installedUpdate(oldCatalogRelease), null, 'A release the user does not have is not marked as an installed update');
  oldCatalogRelease.imported = true;
  latestCatalogRelease.supersededBy = oldCatalogRelease.id;
  assert.equal(ui.latestCatalogRelease(oldCatalogRelease), null, 'A malformed replacement cycle fails safely in the interface');
  assert.equal(ui.installedUpdate(oldCatalogRelease), null, 'A replacement cycle cannot invent an installed update');
  latestCatalogRelease.supersededBy = undefined;
  oldCatalogRelease.supersededBy = 'missing-release';
  assert.equal(ui.latestCatalogRelease(oldCatalogRelease), null, 'A missing replacement target fails safely');
  oldCatalogRelease.supersededBy = middleCatalogRelease.id;
  latestCatalogRelease.available = false;
  assert.equal(ui.latestCatalogRelease(oldCatalogRelease), null, 'An unavailable replacement is not offered as an update');
  ui.state.snapshot = beforeCatalogVersions;
  ui.state.downloads = beforeCatalogDownloads;

  // Local edition: no Nexus account, release or picture traffic from the interface.
  const edition = ui.state.snapshot.edition;
  ui.state.snapshot.edition = 'local';
  assert(ui.localEdition(), 'The native snapshot selects the local edition');
  assert(
    ui.nexusAccountMarkup().includes('sidebar-nexus-local') &&
      !ui.nexusAccountMarkup().includes('sidebar-nexus-connect'),
    'The local edition shows no sign-in button'
  );
  assert(
    ui.renderLocalDownloads().includes('Import mod') && ui.renderDownloads() === ui.renderLocalDownloads(),
    'The local edition downloads page imports website downloads'
  );
  const beforeLocal = nativeRequests.length;
  ui.connectNexus();
  await ui.handleAction('nexus-check');
  await ui.checkAutomaticReleases();
  ui.queuePagePreview(994);
  assert(
    !nativeRequests
      .slice(beforeLocal)
      .some((request) =>
        /^nexus(Connect|Releases|Check|PreviewImage|AuthorAvatar|Login|Download)/.test(request.action)
      ),
    'The local edition sends no Nexus requests'
  );
  assert(
    node('#modal-content').innerHTML.includes('Nexus features are on the way'),
    'Nexus actions explain the local edition'
  );
  node('#modal').close();
  const catalogFile = ui.state.snapshot.mods.find(
    (mod) => mod.available !== false && !ui.isLocalMod(mod) && ui.nexusFileId(mod) > 0
  );
  await ui.downloadNexusFile(ui.nexusFileId(catalogFile), ui.nexusModId(catalogFile));
  assert(
    node('#modal-content').innerHTML.includes('Manual download') &&
      node('#modal-content').innerHTML.includes('id="download-import"') &&
      !node('#modal-content').innerHTML.includes('download-connect'),
    'A catalog download in the local edition points to the Nexus file and Import'
  );
  const beforeFile = nativeRequests.length;
  await node('#download-fallback-browser').onclick();
  assert(
    nativeRequests.slice(beforeFile).some((request) => request.action === 'openNexusFile') &&
      !nativeRequests.slice(beforeLocal).some((request) => /^nexus(Download|Connect)/.test(request.action)),
    'The local edition opens the file page in the browser instead of downloading'
  );
  node('#modal').close();
  ui.state.snapshot.edition = edition;
  ui.state.selectedCrashLogs.add('game-dump');
  assert(
    ui.renderCrashLogs().includes('data-crash-select="game-dump" checked'),
    'A crash dump can be selected for the support ZIP as its summary'
  );
  ui.state.selectedCrashLogs.delete('game-dump');
  assert.equal(ui.state.selectedCrashLogs.size, 0, 'Crash logs are never preselected');
  await ui.previewCrashLog('toolkit-text');
  assert(
    node('#modal-content').innerHTML.includes('&lt;escaped log text&gt;') &&
      !node('#modal-content').innerHTML.includes('<escaped log text>'),
    'Sanitized previews still escape log text before display'
  );
  const beforeCopy = nativeRequests.length;
  await node('#copy-log-preview').onclick();
  const copy = nativeRequests.slice(beforeCopy).find((request) => request.action === 'crashLogCopy');
  assert(copy && copy.id === 'toolkit-text' && !('text' in copy), 'Copy requests a fresh native sanitized read by ID');
  node('#modal').close();
  ui.state.selectedCrashLogs.add('toolkit-text');
  ui.state.profileSession = { active: true, profileName: 'Diagnostic session', status: 'active' };
  const beforeBundle = nativeRequests.length;
  await ui.reviewSupportBundle();
  assert(
    node('#modal-content').innerHTML.includes('logs/toolkit-error.log') &&
      node('#modal-content').innerHTML.includes('does not upload'),
    'Bundle review lists exact files and leaves sharing to the user'
  );
  await node('#support-export-confirm').onclick();
  const bundle = nativeRequests.slice(beforeBundle).find((request) => request.action === 'supportBundleExport');
  assert(
    bundle && bundle.planToken === 'native-support-plan' && bundle.confirmed === true,
    'Support export requires the reviewed native plan token'
  );
  assert.equal(JSON.stringify(bundle.ids), '["toolkit-text"]', 'Only the explicitly selected text log is included');
  assert(!('path' in bundle), 'Native save dialog chooses the export destination');
  ui.state.profileSession = { active: false, status: 'idle' };
  ui.state.selectedCrashLogs.clear();
  ui.state.crashLogs = null;
  node('#modal').close();
  ui.state.snapshot.accountContext = 'old-installation';
  fixture.accountContext = 'new-installation';
  ui.state.nexus = { connected: true, premium: true, name: 'Previous account' };
  ui.state.directories = directoriesFixture;
  ui.state.portableProfiles = portableFixture;
  ui.state.crashLogs = crashFixture;
  ui.state.selectedCrashLogs.add('toolkit-text');
  const beforeContext = nativeRequests.length;
  await ui.refresh({ quiet: true });
  assert(
    nativeRequests.slice(beforeContext).some((request) => request.action === 'nexusStatus'),
    'Changing Linux installation context refreshes the actual saved Nexus account'
  );
  assert(
    !ui.state.nexus.connected &&
      ui.state.directories === null &&
      ui.state.portableProfiles === null &&
      ui.state.crashLogs === null &&
      ui.state.selectedCrashLogs.size === 0,
    'A new installation does not display old account, folder, profile or log data'
  );
  delete fixture.accountContext;
  delete ui.state.snapshot.accountContext;
  const tutorial = {
    id: 'fixture-authoring',
    modding: true,
    chapter: 1,
    category: 'MAKING MODS',
    icon: 'layers',
    title: 'Inspect an original asset',
    summary: 'Synthetic authoring chapter',
    body: '<p>Read the instructions in a separate working folder.</p>',
    resources: [{ id: 'fixture-tool-source', label: 'Verified tool source' }]
  };
  context.window.FIELD_GUIDE.push(tutorial);
  ui.state.article = null;
  ui.state.helpSection = 'help';
  assert(
    ui.renderHelp().includes('Make mods for Days Gone') &&
      !ui.visibleGuideArticles().some((article) => article.modding),
    'Field guide prominently links authoring while keeping troubleshooting results separate'
  );
  ui.state.helpSection = 'modding';
  ui.state.helpSearch = '';
  const chapterList = ui.renderHelp();
  assert(
    chapterList.includes('data-article="fixture-authoring"') && chapterList.includes('chapter-number'),
    'Beginner chapters have a clear ordered entry point'
  );
  ui.state.article = 'fixture-authoring';
  const chapter = ui.renderHelp();
  assert(
    chapter.includes('data-modding-resource="fixture-tool-source"') &&
      chapter.includes('does not download or run modding tools'),
    'Tool source buttons explain their browser-only role'
  );
  const beforeResource = nativeRequests.length;
  await ui.openModdingResource('fixture-tool-source');
  const resourceRequest = nativeRequests
    .slice(beforeResource)
    .find((request) => request.action === 'openModdingResource');
  assert(
    resourceRequest && resourceRequest.resourceId === 'fixture-tool-source' && !('url' in resourceRequest),
    'Tool links send an allowlisted resource ID instead of a renderer-supplied URL'
  );
  context.window.FIELD_GUIDE.pop();
  ui.state.article = null;
  ui.state.helpSection = 'help';
  ui.state.helpSearch = '';
  ui.state.nexus = { connected: true, name: 'Ada Trail', premium: true };
  const premium = ui.nexusAccountMarkup();
  assert(premium.includes('Ada Trail') && premium.includes('Premium'), 'Connected name shows actual Premium tier');
  assert(!premium.includes('OkayDude1337'), 'Mod author is not substituted for the signed-in user');
  ui.state.nexus = { connected: true, name: '<img src=x onerror=alert(1)>', premium: false };
  const free = ui.nexusAccountMarkup();
  assert(free.includes('Free account') && !free.includes('<img src='), 'Free-account name is escaped');
  ui.state.nexus = { connected: true, premium: null, tierVerified: false, offline: true, name: 'Connected account' };
  assert(
    ui.nexusAccountMarkup().includes('Membership unavailable') && !ui.nexusAccountMarkup().includes('Free account'),
    'Unverified/offline membership never becomes Free'
  );
  assert(!ui.nexusCanDownload(), 'Unverified membership never enables Premium downloads');
  assert(
    !ui.renderDownloads().includes('Free accounts need browser authorization'),
    'Downloads explains verification outage accurately'
  );
  ui.state.nexus = { connected: false, premium: true, name: 'Stale user' };
  const disconnected = ui.nexusAccountMarkup();
  assert(
    disconnected.includes('Not connected') &&
      !disconnected.includes('Free account') &&
      !disconnected.includes('Premium'),
    'Disconnected state invents no membership tier'
  );
  assert(
    fs.readFileSync(path.join(appRoot, 'ui/index.html'), 'utf8').includes('Days Gone mod loader'),
    'Requested subtitle is preserved'
  );

  ui.state.snapshot = ui.normalizeSnapshot({
    ...fixture,
    mods: catalog.mods.map((mod) => ({ ...mod, imported: mod.id === 'no-recoil', enabled: mod.id === 'no-recoil' }))
  });
  ui.resetNotices();
  const releases = {
    updates: [
      {
        id: 'no-recoil',
        currentFileId: 10,
        fileId: 11,
        currentVersion: '1.0',
        version: '2.0',
        updateAvailable: true,
        auditedInstallable: false
      }
    ],
    checkedAt: '2026-10-20T12:00:00Z'
  };
  ui.acceptReleaseCheck(releases);
  ui.acceptReleaseCheck(releases);
  assert.equal(ui.state.noticeQueue.length, 1, 'Repeated release checks group and deduplicate installed updates');
  ui.state.filter = 'enabled';
  assert(
    ui.renderMods().includes('Update available') && ui.renderMods().includes('Installed 1.0 → latest 2.0'),
    'Managed older version shows both installed and newest versions'
  );
  ui.state.filter = 'available';
  assert(ui.renderOverview().includes('installed 1.0 → latest 2.0'), 'Overview highlights installed updates');
  ui.state.filter = 'available';
  assert(
    !ui.filteredMods().some((m) => m.id === 'no-recoil') && ui.filteredMods().some((m) => m.id === 'xp-randomizer'),
    'Available mods lists only mods people do not have yet'
  );
  assert(ui.renderMods().includes('Available mods'), 'The first tab is called Available mods');
  const availableRows = ui.renderMods();
  assert(
    availableRows.includes('data-download="xp-randomizer"') &&
      availableRows.includes('aria-label="Details for XP Randomizer"'),
    'Available rows retain Details beside the existing Get mod action'
  );
  const editionBefore = ui.state.snapshot.edition;
  ui.state.snapshot.edition = 'local';
  ui.showModDetails('xp-randomizer');
  assert(
    node('#mod-details-panel').innerHTML.includes('Manual download') &&
      node('#mod-details-panel').innerHTML.includes('id="detail-import"') &&
      node('#mod-details-panel').innerHTML.includes('id="detail-nexus"'),
    'Get mod shows the details, the download steps, the Nexus page and Import in one window'
  );
  ui.closeModDetails();
  ui.state.snapshot.edition = editionBefore;
  // Categories list only what the current tab has, with counts; an empty list says why and offers a way back.
  const filterBefore = ui.state.filter,
    categoryBefore = ui.state.category,
    searchBefore = ui.state.search;
  ui.state.filter = 'available';
  ui.state.category = 'all';
  ui.state.search = '';
  const availableToolbar = ui.renderMods();
  const availableCategories = new Set(
    ui
      .libraryMods()
      .filter((m) => !(m.imported || m.enabled || m.owned))
      .map((m) => m.appearance || m.category === 'Characters' || m.detectedCharacter === 'deacon'
        ? 'Character appearance' : m.category || 'Gameplay')
  );
  assert(
    [...availableCategories].every((c) => availableToolbar.includes(`>${c} · `)),
    'Each category with mods in the tab is listed with its count'
  );
  ui.state.search = 'no mod is called this';
  assert(
    ui.renderModRows().includes('No matching mods') && ui.renderModRows().includes('data-clear-filters'),
    'A search with no result offers to show everything again'
  );
  ui.state.search = '';
  ui.state.filter = filterBefore;
  ui.state.category = categoryBefore;
  ui.state.search = searchBefore;
  // Switching on a mod that conflicts with one that is on offers a swap; nothing changes before confirmation.
  const modsBeforeSwitch = ui.state.snapshot.mods;
  ui.state.snapshot.mods = [
    { id: 'keep-on', name: 'Keep On', imported: true, enabled: true, owned: true, available: true },
    {
      id: 'try-on',
      name: 'Try On',
      imported: true,
      enabled: false,
      available: true,
      conflictsWithInstalled: [{ id: 'keep-on', name: 'Keep On', count: 3 }]
    },
    {
      id: 'by-hand',
      name: 'By Hand',
      imported: true,
      enabled: false,
      available: true,
      conflictsWithInstalled: [{ id: 'external:saved:Other_P.pak', name: 'Other_P.pak', count: 2 }]
    }
  ];
  const beforeSwitch = nativeRequests.length;
  await ui.toggleMod('try-on');
  assert(
    node('#modal-content').innerHTML.includes('Only one of them can work at a time') &&
      !nativeRequests.slice(beforeSwitch).some((r) => ['enable', 'disable', 'keepOnlyMod', 'switchMod'].includes(r.action)),
    'A conflict is offered as a swap before anything changes'
  );
  await node('#confirm-action').onclick();
  const switchRequests = nativeRequests
    .slice(beforeSwitch)
    .filter((r) => ['enable', 'disable', 'keepOnlyMod', 'switchMod'].includes(r.action))
    .map((r) => `${r.action}:${r.id}`);
  assert.deepEqual(switchRequests, ['switchMod:try-on'], 'The swap uses one atomic native change');
  ui.state.snapshot.mods = modsBeforeSwitch.concat([
    {
      id: 'by-hand',
      name: 'By Hand',
      imported: true,
      enabled: false,
      available: true,
      conflictsWithInstalled: [{ id: 'external:saved:Other_P.pak', name: 'Other_P.pak', count: 2 }]
    }
  ]);
  const beforeOutside = nativeRequests.length;
  await ui.toggleMod('by-hand');
  assert(
    node('#modal-content').innerHTML.includes('installed outside the toolkit') &&
      !nativeRequests.slice(beforeOutside).some((r) => ['enable', 'disable', 'keepOnlyMod', 'switchMod'].includes(r.action)),
    'A conflict with a hand-installed file explains what to do and changes nothing'
  );
  node('#modal').close();
  ui.state.snapshot.mods = modsBeforeSwitch;
  // Drag and drop: only a PAK or ZIP is taken, and it goes through the same native import review.
  const snapshotBeforeDrop = ui.state.snapshot;
  const beforeDrop = nativeRequests.length;
  assert.equal(await ui.importDroppedFile([{ name: 'readme.txt' }]), false, 'A non-mod file is not sent anywhere');
  assert.equal(nativeRequests.length, beforeDrop, 'Nothing is requested for a non-mod drop');
  await ui.importDroppedFile([{ name: 'Cool_P.pak' }]);
  assert(
    nativeRequests.slice(beforeDrop).some((r) => r.action === 'importDropped' && !('path' in r)),
    'A dropped mod asks the native side to import it, without a page-supplied path'
  );
  ui.state.snapshot = snapshotBeforeDrop;
  // Notes from a mod's ZIP show in the import review, escaped.
  ui.reviewDownloadedPackage({
    reviewRequired: true,
    source: 'local-import',
    reviewToken: 'readme-review',
    mod: {
      id: 'local:readme',
      name: 'Noted',
      packageFile: 'Noted_P.pak',
      sha256: 'c',
      assets: [],
      readme: 'Install <here>'
    },
    conflicts: [],
    notes: ['Picked from your computer.']
  });
  assert(
    node('#modal-content').innerHTML.includes('Notes from the download') &&
      node('#modal-content').innerHTML.includes('Install &lt;here&gt;') &&
      node('#modal-content').innerHTML.includes('Picked from your computer.'),
    'ZIP notes and review notes are shown, escaped'
  );
  node('#modal').close();
  // Unknown PAKs get readable names; a file identical to a catalog release shows as that release.
  assert.equal(ui.readableModName('BikeCrashesHurtMore_P'), 'Bike Crashes Hurt More', 'File names become readable');
  assert.equal(ui.readableModName('ZZZ-HiddenArsenal_P'), 'Hidden Arsenal', 'Load-order prefixes are dropped');
  assert.equal(
    ui.readableModName('MoreNormalFreakers10x_P'),
    'More Normal Freakers 10x',
    'Numbers stay with their unit'
  );
  assert.equal(ui.readableModName('My Mod'), 'My Mod', 'Names with spaces stay as they are');
  const aliased = ui.withCatalogNames([
    {
      id: 'weaker',
      name: 'Unforgiving: Weaker Healing',
      sha256: 'aa',
      nexusModId: 994,
      nexusFileId: 3230,
      download: 'Unforgiving Standalone'
    },
    { id: 'local:x', name: 'WeakerHealing_P', sha256: 'aa', identityStatus: 'local-import', enabled: true },
    { id: 'local:y', name: 'SomeoneElsesMod_P', sha256: 'bb', identityStatus: 'local-import' }
  ]);
  assert(
    aliased[0].heldLocally === 'local:x' &&
      aliased[1].name === 'Unforgiving: Weaker Healing' &&
      aliased[1].catalogMatch === 'weaker' &&
      aliased[2].name === 'Someone Elses Mod',
    'A local copy of a release shows the release name; others get readable names'
  );
  // A ZIP with several PAKs opens a picker; tiers of one mod replace each other.
  ui.choosePackages({
    choosePackages: true,
    pickToken: 'pick',
    archive: 'More Normal Freakers.zip',
    known: true,
    packages: [
      { file: 'MoreNormalFreakers3x_P.pak', name: 'More Normal Freakers 3x', bytes: 10, variantGroup: 'mnf' },
      { file: 'MoreNormalFreakers5x_P.pak', name: 'More Normal Freakers 5x', bytes: 10, variantGroup: 'mnf' },
      { file: 'BiggerHordes_P.pak', name: 'Bigger Hordes', bytes: 10, inLibrary: true }
    ]
  });
  const pickHtml = node('#modal-content').innerHTML;
  assert(
    pickHtml.includes('type="checkbox"') && pickHtml.includes('pick one') && pickHtml.includes('already in your mods'),
    'The ZIP picker lists every PAK, marks tiers and what is already added'
  );
  node('#modal').close();
  // Share my mod list: names and links only, never paths.
  const listText = ui.modListText();
  assert(listText.startsWith('My Days Gone mods') && !/[A-Z]:\\/.test(listText), 'The shared list has no file paths');
  // Spotted downloads: offered by ID with Review & import / Ignore; no path is shown.
  ui.state.downloadOffers = [{ id: 'abc', name: 'CoolMod_P.pak', bytes: 10, kind: 'pak' }];
  const offersHtml = ui.renderDownloadOffers();
  assert(
    offersHtml.includes('data-spotted-add="abc"') &&
      offersHtml.includes('CoolMod_P.pak') &&
      offersHtml.includes('Ignore'),
    'A spotted download offers Review & import and Ignore'
  );
  ui.state.downloadOffers = [];
  // Sorting: A–Z and enabled-first orders; the toolbar offers the sort menu.
  const sortBefore = ui.state.sort;
  ui.state.sort = 'name';
  assert(ui.compareMods({ name: 'Alpha' }, { name: 'Bravo' }) < 0, 'Name sort is A–Z');
  ui.state.sort = 'enabled';
  assert(ui.compareMods({ id: 'x', name: 'Z', enabled: true }, { id: 'y', name: 'A' }) < 0, 'Enabled mods come first');
  ui.state.sort = sortBefore;
  assert(ui.renderMods().includes('id="sort-filter"'), 'My mods offers a sort menu');
  // Getting started: shown until the four steps are done, with only the next step's button.
  const settingsBeforeStart = ui.state.snapshot.settings,
    modsBeforeStart = ui.state.snapshot.mods;
  ui.state.snapshot.settings = { ...settingsBeforeStart, gamePath: '' };
  ui.state.snapshot.mods = [];
  const firstRun = ui.renderGettingStarted();
  assert(
    firstRun.includes('Getting started') &&
      firstRun.includes('data-action="browse-game"') &&
      !firstRun.includes('data-action="import"'),
    'A first run shows the checklist with only the next step to do'
  );
  ui.state.snapshot.settings = settingsBeforeStart;
  ui.state.snapshot.mods = modsBeforeStart;
  // A new download of a mod you have is recognised as its newer version.
  assert.equal(ui.modFamily('NoRecoil_v2_P.pak'), ui.modFamily('No Recoil 1.1'), 'Versions and suffixes are ignored');
  assert.notEqual(ui.modFamily('Infinite Nitro'), ui.modFamily('Infinite Ammo'), 'Different mods stay different');
  const modsBeforeUpdate = ui.state.snapshot.mods;
  ui.state.snapshot.mods = [
    {
      id: 'local:old',
      name: 'Cool Mod',
      packageFile: 'CoolMod_P.pak',
      sha256: 'a',
      imported: true,
      enabled: true,
      identityStatus: 'local-import'
    }
  ];
  ui.reviewDownloadedPackage({
    reviewRequired: true,
    source: 'local-import',
    reviewToken: 'update-review',
    mod: { id: 'local:new', name: 'Cool Mod v2', packageFile: 'CoolMod_v2_P.pak', sha256: 'b', assets: [] },
    conflicts: []
  });
  assert(
    node('#modal-content').innerHTML.includes('newer version of Cool Mod') &&
      node('#modal-content').innerHTML.includes('id="review-import-replace"'),
    'A new download of a mod you have offers to replace the old version'
  );
  node('#modal').close();
  ui.state.snapshot.mods = modsBeforeUpdate;
  // A release without a version number shows no version instead of "vunversioned".
  assert.equal(ui.versionLabel('unversioned'), '', 'An unversioned release shows no version');
  assert.equal(ui.versionLabel('v1.2'), 'v1.2', 'A real version keeps one v');
  // A setup where Days Gone cannot load PAK mods yet says so where mods are switched on.
  const setupBefore = ui.state.snapshot.setup,
    directoriesBefore = ui.state.directories;
  ui.state.directories = null;
  ui.state.snapshot.setup = { state: 'ready', startup: { status: 'needs-preparation', canPrepare: true } };
  const gamePathBefore = ui.state.snapshot.settings.gamePath;
  ui.state.snapshot.settings.gamePath = 'C:/Synthetic/Days Gone';
  assert(
    ui.renderMods().includes('Mods will not load yet') && ui.renderOverview().includes('data-action="loading-setup"'),
    'An unprepared game shows the mod-loading banner with its setup button'
  );
  ui.state.directories = { startup: { status: 'prepared' } };
  assert(!ui.renderMods().includes('Mods will not load yet'), 'A fresh folder scan hides the banner once prepared');
  ui.state.snapshot.setup = setupBefore;
  ui.state.directories = directoriesBefore;
  ui.state.snapshot.settings.gamePath = gamePathBefore;
  const readinessBefore = {
    snapshot: ui.state.snapshot,
    profileSession: ui.state.profileSession,
    restart: ui.state.gameRestart,
    preview: ui.state.preview,
    loading: ui.state.loading,
    scanFailed: ui.state.scanFailed
  };
  ui.state.preview = false;
  ui.state.loading = false;
  ui.state.scanFailed = false;
  ui.state.profileSession = { active: false };
  ui.state.gameRestart = { pending: false };
  ui.state.snapshot = {
    ...ui.state.snapshot,
    sandbox: false,
    gameRunning: false,
    recoveryRequired: false,
    finder: { active: false },
    diagnostics: [],
    conflicts: [],
    conflictPairsOmitted: 0,
    setup: { state: 'ready', startup: { status: 'prepared' } },
    settings: { ...ui.state.snapshot.settings, gamePath: 'C:/Synthetic/Days Gone' }
  };
  assert.equal(ui.playReadiness().message, 'Ready to play', 'A checked, prepared, conflict-free setup is ready');
  assert.equal(ui.playReadiness().tone, 'success');
  ui.state.snapshot.saveRecoveryRequired = true;
  assert.equal(ui.playReadiness().message, 'A save restore needs recovery', 'Pending save restoration is never shown as ready to play');
  assert.equal(ui.playReadiness().action, 'save-backups', 'Save recovery links to its reviewed recovery workflow');
  assert.notEqual(ui.playReadiness().tone, 'success');
  ui.state.snapshot.saveRecoveryRequired = false;
  ui.renderPlayReadiness();
  assert(
    node('#play-readiness').innerHTML.includes('aria-live="polite"'),
    'The readiness text has a polite status announcement'
  );
  for (const status of ['missing', 'unverified', 'changed', 'recovery-pending', undefined]) {
    ui.state.snapshot.setup.startup.status = status;
    assert.notEqual(ui.playReadiness().tone, 'success', `Startup status ${status} is never presented as ready`);
    assert.equal(ui.playReadiness().action, 'folders');
  }
  ui.state.snapshot.setup.startup.status = 'needs-preparation';
  assert.equal(ui.playReadiness().action, 'loading-setup', 'The missing loading step offers the existing setup review');
  ui.state.snapshot.setup.startup.status = 'external-prepared';
  assert.equal(ui.playReadiness().tone, 'success', 'A checked manual startup preparation is accepted');
  ui.state.snapshot.gameRunning = undefined;
  assert.equal(ui.playReadiness().action, 'diagnose', 'Unknown process state is not the same as a closed game');
  ui.state.snapshot.gameRunning = false;
  ui.state.snapshot.diagnostics = [{ level: 'warning', code: 'process-check' }];
  assert.equal(
    ui.playReadiness().message,
    'Game status needs checking',
    'A failed native process inspection cannot look ready'
  );
  ui.state.snapshot.diagnostics = [{ level: 'warning', code: 'unreadable-package' }];
  assert.equal(
    ui.playReadiness().action,
    'diagnose',
    'Unreadable packages offer diagnostics instead of claiming compatibility'
  );
  ui.state.snapshot.diagnostics = [{ level: 'info', code: 'first-run' }];
  assert.equal(ui.playReadiness().tone, 'success', 'Informational advice does not create a false blocker');
  ui.state.snapshot.conflictPairsOmitted = 1;
  assert.equal(ui.playReadiness().action, 'conflicts', 'Conflicts beyond the displayed sample still need review');
  ui.state.snapshot.conflictPairsOmitted = 0;
  ui.state.snapshot.conflicts = [{ a: 'first', b: 'second', count: 1 }];
  assert.equal(ui.playReadiness().action, 'conflicts');
  ui.state.snapshot.conflicts = [];
  ui.state.snapshot.recoveryRequired = true;
  assert.equal(ui.playReadiness().action, 'diagnose', 'Interrupted package changes take priority');
  ui.state.snapshot.recoveryRequired = false;
  ui.state.profileSession = { active: true };
  assert.equal(ui.playReadiness().action, 'profiles', 'A closed game with an active profile offers restoration review');
  ui.state.profileSession.status = 'launching';
  assert.equal(
    ui.playReadiness().message,
    'Waiting for Days Gone to start',
    'A normal Steam startup is not labelled as unfinished recovery'
  );
  ui.state.profileSession = { active: false };
  ui.state.snapshot.gameRunning = true;
  assert.equal(ui.playReadiness().message, 'Days Gone is running');
  assert.notEqual(ui.playReadiness().tone, 'success');
  ui.state.snapshot.gameRunning = false;
  ui.state.snapshot.finder = { active: true };
  assert.equal(ui.playReadiness().action, 'finder-open', 'An active test setup is labelled as a finder round');
  ui.state.snapshot.finder.active = false;
  ui.state.gameRestart = { pending: true };
  assert.equal(ui.playReadiness().action, 'restart-cancel', 'Queued restart offers cancellation');
  ui.state.gameRestart = { pending: false };
  ui.state.scanFailed = true;
  assert.notEqual(ui.playReadiness().tone, 'success', 'A failed latest scan never leaves a green readiness claim');
  ui.state.scanFailed = false;
  ui.state.snapshot.settings.gamePath = '';
  assert.equal(ui.playReadiness().action, 'browse-game', 'Missing game setup offers the native folder picker');
  ui.state.snapshot = readinessBefore.snapshot;
  ui.state.profileSession = readinessBefore.profileSession;
  ui.state.gameRestart = readinessBefore.restart;
  ui.state.preview = readinessBefore.preview;
  ui.state.loading = readinessBefore.loading;
  ui.state.scanFailed = readinessBefore.scanFailed;
  // The background safety net asks for a fresh snapshot but redraws only when something changed.
  assert.equal(
    ui.snapshotSignature(ui.state.snapshot),
    ui.snapshotSignature(JSON.parse(JSON.stringify(ui.state.snapshot))),
    'An unchanged snapshot has the same signature'
  );
  assert.notEqual(
    ui.snapshotSignature({ ...ui.state.snapshot, installed: [{ id: 'external:saved:New_P.pak', bytes: 4 }] }),
    ui.snapshotSignature(ui.state.snapshot),
    'A new hand-installed file changes the signature'
  );
  // Keep this one switches off the enabled mods that conflict with the chosen one, and nothing else.
  const modsBeforeKeep = ui.state.snapshot.mods,
    conflictsBeforeKeep = ui.state.snapshot.conflicts;
  ui.state.snapshot.mods = [
    { id: 'keep', name: 'Keep', imported: true, enabled: true },
    { id: 'rival', name: 'Rival', imported: true, enabled: true },
    { id: 'bystander', name: 'Bystander', imported: true, enabled: true }
  ];
  ui.state.snapshot.conflicts = [{ a: 'keep', b: 'rival', aName: 'Keep', bName: 'Rival', count: 2, assets: ['A'] }];
  assert(
    ui.renderConflicts().includes('data-keep-one="keep"') && ui.renderConflicts().includes('PICK ONE'),
    'Conflict cards offer Keep this one'
  );
  const beforeKeep = nativeRequests.length;
  await ui.keepOnlyMod('keep');
  const keepRequests = nativeRequests
    .slice(beforeKeep)
    .filter((r) => ['enable', 'disable', 'keepOnlyMod'].includes(r.action))
    .map((r) => `${r.action}:${r.id}`);
  assert.deepEqual(keepRequests, ['keepOnlyMod:keep'], 'Conflict choice uses one atomic native request');
  ui.state.snapshot.conflicts = [];
  assert(
    ui.renderConflicts().includes('No conflicts') && !ui.renderConflicts().includes('The game uses only one of them'),
    'No conflicts shows a calm empty state'
  );
  ui.state.snapshot.mods = modsBeforeKeep;
  ui.state.snapshot.conflicts = conflictsBeforeKeep;
  // Mods that conflict through a shared partner count once in the navigation, like the Conflicts cards.
  const conflictsBeforeCount = ui.state.snapshot.conflicts;
  ui.state.snapshot.conflicts = [
    { a: 'x', b: 'y', aName: 'X', bName: 'Y', count: 1, assets: ['A'] },
    { a: 'y', b: 'z', aName: 'Y', bName: 'Z', count: 1, assets: ['B'] },
    { a: 'x', b: 'z', aName: 'X', bName: 'Z', count: 1, assets: ['C'] }
  ];
  assert.equal(ui.conflictGroupCount(), 1, 'Three conflicting pairs among three mods count as one group');
  ui.state.snapshot.conflicts = conflictsBeforeCount;
  ui.state.filter = 'enabled';
  assert(
    ui.filteredMods().some((m) => m.id === 'no-recoil'),
    'Enabled mods stay under Enabled'
  );
  ui.state.filter = 'available';
  node('#modal').close();
  ui.state.loading = false;
  ui.state.busy = false;
  ui.showNextNotice(true);
  assert(
    node('#modal-content').innerHTML.includes('awaiting package inspection') ||
      node('#modal-content').innerHTML.includes('awaiting package'),
    'Unaudited newer release stays pending inspection'
  );
  node('#modal').close();
  ui.state.downloads = { files: [], updates: [] };
  ui.resetNotices();
  ui.state.downloads.inventory = [
    {
      fileId: 99001,
      name: 'New <script>release</script>',
      version: '3.0',
      fileName: 'NewRelease.zip',
      nexusModId: 994,
      audited: false
    }
  ];
  const discovery = ui.renderMods();
  assert(
    discovery.includes('New from the author') && discovery.includes('data-nexus-file="99001"'),
    'New official upload appears without changing bundled catalog'
  );
  assert(discovery.includes('&lt;script&gt;') && !discovery.includes('<script>'), 'Live release names are escaped');
  assert(
    discovery.includes('data-mod-icon=') && ui.renderDownloads().includes('data-mod-icon='),
    'Live discoveries and Downloads share the local artwork resolver'
  );
  assert(discovery.includes('Inspection required'), 'New upload is never presented as audited install-ready');
  ui.state.busy = false;
  node('#modal').close();
  const newReview = {
    reviewRequired: true,
    reviewToken: 'native-opaque-review',
    mod: {
      name: 'Future <mod>',
      version: '3.0',
      packageFile: 'Future_P.pak',
      sha256: '123',
      assets: ['BendGame/Asset<one>.uasset'],
      provenance: { kind: 'nexus-inspected', modId: 994, fileId: 99001 }
    },
    conflicts: [{ name: 'Existing package', count: 1, assets: ['BendGame/Asset<one>.uasset'] }],
    warnings: ['Not gameplay tested']
  };
  assert(ui.reviewDownloadedPackage(newReview), 'Native inspection opens a concrete package review');
  const inspected = node('#modal-content').innerHTML;
  assert(
    inspected.includes('data-mod-icon=') && inspected.includes('detail-artwork'),
    'Native-reviewed future package gets artwork before import'
  );
  assert(
    inspected.includes('Technical details') &&
      inspected.includes('Asset&lt;one&gt;') &&
      inspected.includes('Changes the same game files as Existing package'),
    'Review shows actual escaped paths and package conflicts'
  );
  assert(
    inspected.includes('SHA-256') && inspected.includes('99001') && inspected.includes('Add to my library'),
    'Review identifies bytes and official release before confirmation'
  );
  assert(inspected.includes('id="review-import-enable"'), 'An imported mod can be added and switched on in one step');
  const beforeReview = nativeRequests.length;
  await node('#review-import-confirm').onclick();
  const confirmed = nativeRequests.slice(beforeReview).find((request) => request.action === 'confirmReviewedImport');
  assert(
    confirmed && confirmed.reviewToken === 'native-opaque-review' && confirmed.confirmed === true,
    'Only native opaque review token grants import confirmation'
  );
  assert(
    !('provenance' in confirmed) && !('sha256' in confirmed) && !('assets' in confirmed),
    'Renderer metadata cannot manufacture native import trust'
  );
  ui.resetNotices();
  node('#modal').showModal();
  ui.reviewDownloadedPackage({ ...newReview, reviewToken: 'deferred-native-review' });
  assert.equal(
    ui.state.noticeQueue[0]?.type,
    'package-review',
    'External download review waits for an existing dialog'
  );
  node('#modal').close();
  ui.resetNotices();
  ui.state.downloads = { files: [], updates: [] };
  const dynamicMod = {
    id: 'native-approved-future',
    name: 'Future mod',
    identityStatus: 'nexus-inspected',
    version: '1.0',
    nexusModId: 1500,
    nexusFileId: 99001,
    imported: true,
    enabled: true,
    assets: ['Future.uasset']
  };
  ui.state.snapshot = ui.normalizeSnapshot({ ...fixture, mods: [...catalog.mods, dynamicMod] });
  ui.state.downloads.inventory = [
    { modId: 1500, fileId: 99002, name: 'Future 2', version: '2.0' },
    { modId: 2500, fileId: 99002, name: 'Another page', version: '9.0' }
  ];
  ui.state.downloads.fileUpdates = [{ modId: 1500, oldFileId: 99001, newFileId: 99002 }];
  ui.acceptReleaseCheck({ updates: [], checkedAt: '2026-10-20T12:00:00Z' });
  assert.equal(
    ui.installedUpdate(dynamicMod)?.version,
    '2.0',
    'Official update edges cover locally approved future mods'
  );
  assert(
    ui.renderDiscoveries().includes('data-nexus-mod-id="1500"') &&
      ui.renderDiscoveries().includes('data-nexus-mod-id="2500"'),
    'Live discovery keeps page identity alongside file identity'
  );
  assert(
    !ui.sameNexusFile({ modId: 1500, fileId: 5 }, { modId: 2500, fileId: 5 }),
    'Equal file numbers on different pages never merge'
  );
  ui.state.nexus = { connected: true, premium: true, tierVerified: true };
  const beforeFutureDownload = nativeRequests.length;
  await ui.downloadNexusFile(99002, 1500);
  const futureDownload = nativeRequests
    .slice(beforeFutureDownload)
    .find((request) => request.action === 'nexusDownload');
  assert(
    futureDownload && futureDownload.modId === 1500 && futureDownload.fileId === 99002,
    'Future download routes the verified source page and file'
  );
  assert(!('id' in futureDownload), 'Local approved ID is not misrepresented as a shipped Nexus catalog anchor');
  ui.state.snapshot.platform = 'linux';
  ui.state.snapshot.activePath = '/home/test/pfx/AppData/Local/BendGame/Saved/Paks';
  const linuxGuide = ui.guideBody(context.window.FIELD_GUIDE.find((article) => article.id === 'getting-started'));
  assert(
    linuxGuide.includes(ui.state.snapshot.activePath) && !linuxGuide.includes('%LOCALAPPDATA%'),
    'Linux guide uses the actual validated Proton destination'
  );
  assert(
    context.window.FIELD_GUIDE.some((article) => article.id === 'linux-proton'),
    'Linux setup has a dedicated practical guide'
  );
  ui.state.snapshot.platform = 'windows';
  ui.state.downloads = { files: [], updates: [] };
  ui.resetNotices();
  node('#modal').close();
  ui.state.snapshot.settings.autoCheckUpdates = false;
  const beforeOptOut = nativeRequests.length;
  await ui.checkAutomaticReleases();
  assert.equal(nativeRequests.length, beforeOptOut, 'Explicit update-check opt-out is respected');
  ui.state.snapshot.settings.autoCheckUpdates = true;
  ui.state.view = 'settings';
  node('#main-content').innerHTML = 'unsaved settings fixture';
  const beforeAuto = nativeRequests.length;
  await Promise.all([ui.checkAutomaticReleases(), ui.checkAutomaticReleases()]);
  assert.equal(
    nativeRequests.slice(beforeAuto).filter((request) => request.action === 'nexusCheckUpdates').length,
    1,
    'Concurrent login/startup triggers coalesce into one version check'
  );
  assert.equal(
    nativeRequests.slice(beforeAuto).filter((request) => request.action === 'nexusInventory').length,
    1,
    'Automatic version check also refreshes live author inventory'
  );
  assert.equal(
    node('#main-content').innerHTML,
    'unsaved settings fixture',
    'Automatic release results preserve an edited Settings form'
  );
  ui.state.view = 'overview';
  ui.state.downloads = { files: [], updates: [] };
  ui.resetNotices();

  ui.state.snapshot = ui.normalizeSnapshot({
    ...fixture,
    mods: catalog.mods.map((mod) => ({ ...mod, imported: mod.id === 'no-recoil', enabled: mod.id === 'no-recoil' })),
    installed: [{ id: 'external:manual.pak', name: 'Manual package', owned: false, readable: false }],
    conflicts: [
      {
        a: 'no-recoil',
        aName: 'No Recoil',
        b: 'external:manual.pak',
        bName: 'Manual package',
        count: 1,
        assets: ['a.uasset']
      }
    ],
    profiles: [{ name: 'Test profile', mods: ['no-recoil'] }]
  });
  assert(ui.renderConflicts().includes('Manual package'), 'External names appear in conflicts');
  assert(ui.renderConflicts().includes('could not be read'), 'Unreadable packages remain unknown');
  assert(ui.renderProfiles().includes('No Recoil'), 'Profile entries show catalog names');
  await ui.loadPortableProfiles();
  const portable = ui.renderProfiles();
  assert(
    portable.includes('Independent ride') && portable.includes('1 mod · 123 B') && portable.includes('No Recoil'),
    'A profile row shows its name, size and the mods inside'
  );
  assert(!portable.includes('NoRecoil_P.pak'), 'File names stay folded until the mod list is opened');
  ui.state.openProfiles = new Set(['Independent ride']);
  assert(
    ui.renderProfiles().includes('NoRecoil_P.pak') && ui.renderProfiles().includes('Hide mods'),
    'Opening a profile shows each mod file and its size'
  );
  ui.state.openProfiles = new Set();
  // Many profiles: search by profile or mod name and sort; problems stay on top.
  const profilesBefore = ui.state.portableProfiles;
  ui.state.portableProfiles = {
    profiles: [
      { name: 'Zeta', status: 'ready', count: 1, bytes: 5, mods: [{ id: 'a', name: 'Alpha Mod' }] },
      {
        name: 'Alpha',
        status: 'ready',
        count: 2,
        bytes: 9,
        mods: [
          { id: 'b', name: 'Beta Mod' },
          { id: 'c', name: 'Gamma Mod' }
        ]
      },
      { name: 'Broken', status: 'changed', count: 0, bytes: 0, mods: [], error: 'Changed.' },
      { name: 'Mid', status: 'ready', count: 0, bytes: 1, mods: [] },
      { name: 'Last', status: 'ready', count: 0, bytes: 1, mods: [] }
    ],
    session: { active: false }
  };
  const many = ui.renderProfiles();
  assert(
    many.includes('id="profile-search"') && many.includes('id="profile-sort"'),
    'Many profiles get search and sort'
  );
  assert(
    many.indexOf('Broken') < many.indexOf('>Alpha<') && many.indexOf('>Alpha<') < many.indexOf('>Zeta<'),
    'Problems first, then A–Z'
  );
  ui.state.profileSearch = 'gamma';
  const searched = ui.renderProfiles();
  assert(searched.includes('>Alpha<') && !searched.includes('>Zeta<'), 'Search finds profiles by the mods inside');
  ui.state.profileSearch = '';
  ui.state.portableProfiles = profilesBefore;
  assert(portable.includes('Older saved selections'), 'Legacy ID lists are not presented as portable snapshots');
  await ui.reviewPortableProfile('Independent ride');
  const launch = node('#modal-content').innerHTML;
  assert(
    launch.includes(launchPlanFixture.moves[0].source) && launch.includes(launchPlanFixture.moves[0].destination),
    'Launch review shows exact external-file moves'
  );
  assert(
    launch.includes('class="launch-name-list"') && launch.includes('<summary>File paths</summary>'),
    'Launch review lists PAK names first and keeps the exact paths folded'
  );
  assert(
    launch.includes(launchPlanFixture.additions[0].source),
    'Launch review shows independent profile package source'
  );
  assert(
    !nativeRequests.some((request) => request.action === 'profileLaunch'),
    'Review does not launch before confirmation'
  );
  await node('#portable-launch-confirm').onclick();
  const launchRequest = nativeRequests.find((request) => request.action === 'profileLaunch');
  assert.equal(launchRequest.name, 'Independent ride');
  assert.equal(launchRequest.planToken, 'opaque-reviewed-plan', 'Launch uses the reviewed plan token');
  assert.equal(launchRequest.confirmed, true, 'Launch carries explicit confirmation');
  ui.state.profileSession = { active: true, profileName: 'Independent ride', status: 'running' };
  assert(
    ui.sessionBlocksAction('enable') && ui.sessionBlocksAction('saveSettings'),
    'Active session blocks package and settings mutations'
  );
  assert(ui.renderProfileSessionBanner().includes('restored'), 'Active session explains restoration');
  const settingsBefore = nativeRequests.filter((request) => request.action === 'savePreferences').length;
  await ui.handleInterfaceShortcut({ key: '+', ctrlKey: true, preventDefault() {} });
  assert.equal(
    nativeRequests.filter((request) => request.action === 'savePreferences').length,
    settingsBefore + 1,
    'Keyboard appearance preferences remain available during a profile session'
  );
  ui.state.profileSession = { active: false, status: 'idle' };
  ui.state.profileSession = {
    active: true,
    canRestore: true,
    profileName: 'Independent ride',
    gamePath: 'C:\\Synthetic'
  };
  ui.restoreProfileSession();
  assert(
    node('#modal-content').innerHTML.includes('Days Gone must be closed'),
    'Recovery requires a clear closed-game warning'
  );
  await node('#confirm-action').onclick();
  assert(
    nativeRequests.some((request) => request.action === 'profileFinishSession' && request.confirmed),
    'Reviewed recovery calls the guarded native restoration endpoint'
  );
  ui.state.profileSession = { active: false, status: 'idle' };

  await ui.loadDiagnosticReport();
  assert.equal(ui.state.view, 'diagnostics', 'Diagnostics has its own view');
  const findings = ui.renderDiagnosticPage();
  assert(
    findings.includes('Weapon packages overlap') && findings.includes('Details ·'),
    'Findings explain issues and expose evidence'
  );
  assert(findings.includes('&lt;unknown&gt;.pak'), 'Evidence paths are escaped');
  assert.equal(
    ui.diagnosticStatus(diagnosticFixture.checks[1]).label,
    'Not checked',
    'Unreadable compatibility is never a pass'
  );
  ui.reviewDiagnosticFix('weapon-overlap');
  const repairModal = node('#modal-content').innerHTML;
  assert(
    repairModal.includes('Choose a mod to disable…') &&
      repairModal.includes('id="diagnostic-repair-confirm" class="button primary" disabled'),
    'Conflict repair starts with no automatic winner'
  );
  node('#diagnostic-choice').value = '';
  await node('#diagnostic-repair-confirm').onclick();
  assert(
    !nativeRequests.some((request) => request.action === 'diagnosticsRepair'),
    'No repair happens without explicit mod choice'
  );
  node('#diagnostic-choice').value = 'no-recoil';
  node('#diagnostic-choice').onchange();
  await node('#diagnostic-repair-confirm').onclick();
  const repairRequest = nativeRequests.find((request) => request.action === 'diagnosticsRepair');
  assert.equal(repairRequest.planToken, 'diagnostic-reviewed-token');
  assert.equal(repairRequest.choiceId, 'no-recoil');
  assert.equal(repairRequest.confirmed, true, 'Repair carries reviewed finding and explicit choice');
  ui.state.diagnosticStale = true;
  const repairsBeforeStale = nativeRequests.filter((request) => request.action === 'diagnosticsRepair').length;
  ui.reviewDiagnosticFix('weapon-overlap');
  assert.equal(
    nativeRequests.filter((request) => request.action === 'diagnosticsRepair').length,
    repairsBeforeStale,
    'Stale findings cannot be repaired from the old UI report'
  );
  ui.state.diagnosticStale = false;

  node('#modal').close();
  assert(
    ui.showActionError(new Error('Changed file <one.pak>'), 'enable'),
    'Critical file errors use a persistent dialog'
  );
  assert(
    node('#modal-content').innerHTML.includes('Run diagnostics') &&
      node('#modal-content').innerHTML.includes('&lt;one.pak&gt;'),
    'Persistent errors include diagnostics, help and escaped details'
  );
  assert(
    !ui.showActionError(new Error('Authentication ended'), 'nexusBeginLogin'),
    'Account-flow errors do not spawn repeated critical file dialogs'
  );
  node('#modal').close();

  ui.resetNotices();
  ui.state.view = 'overview';
  ui.state.loading = false;
  ui.state.busy = false;
  ui.considerSetup({ setup: { state: 'ready', installations: [{ path: 'C:\\Synthetic\\Unique' }] } });
  assert.equal(ui.state.noticeQueue.length, 0, 'Uniquely auto-selected ready setup asks no unnecessary question');
  ui.resetNotices();
  const missingSetup = { setup: { state: 'not-found', installations: [] } };
  ui.considerSetup(missingSetup);
  ui.considerSetup(missingSetup);
  assert.equal(ui.state.noticeQueue.length, 1, 'Missing-installation prompt is queued once');
  assert(ui.showNextNotice(), 'Missing-game prompt can be shown while idle');
  assert(
    node('#modal-content').innerHTML.includes('Not now') && node('#modal-content').innerHTML.includes('still import'),
    'Missing-game setup does not block offline library use'
  );
  node('#modal').close();
  ui.resetNotices();
  ui.considerSetup({
    setup: {
      state: 'choose-installation',
      installations: [
        { path: 'C:\\Synthetic\\Steam', label: 'Steam' },
        { path: 'D:\\Synthetic\\Epic', label: 'Epic' }
      ]
    }
  });
  ui.showNextNotice();
  assert(
    node('#modal-content').innerHTML.includes('C:\\Synthetic\\Steam') &&
      node('#modal-content').innerHTML.includes('D:\\Synthetic\\Epic'),
    'Ambiguous setup shows actual candidate paths'
  );
  node('#modal').close();
  ui.resetNotices();
  const changedSnapshot = {
    diagnostics: [
      {
        code: 'asset-conflict',
        modIds: ['no-recoil', 'infinite-ammo-no-reload'],
        message: 'Two packages overlap (12).'
      },
      { code: 'owned-file-changed', modId: 'no-recoil', message: 'Managed file changed.' },
      { code: 'unreadable-package', message: 'Unknown external file.' }
    ],
    settings: { seenNotices: [] }
  };
  ui.collectActionNotices(changedSnapshot);
  ui.collectActionNotices(changedSnapshot);
  assert.equal(
    ui.state.noticeQueue.length,
    1,
    'Several scan findings share one pop-up and avoid generic unknown-warning storms'
  );
  ui.state.view = 'settings';
  assert.equal(ui.showNextNotice(), false, 'Automatic notices preserve an active Settings page');
  assert.equal(node('#attention-count').textContent, '2', 'Deferred issues remain visible in a notice badge');
  ui.state.view = 'overview';
  ui.state.busy = true;
  assert.equal(ui.showNextNotice(), false, 'Notices wait for busy file operations');
  ui.state.busy = false;
  const beforeSummary = nativeRequests.length;
  assert(ui.showNextNotice(), 'Queued notice opens on the next safe idle opportunity');
  const issueSummaryHtml = node('#modal-content').innerHTML;
  assert(
    issueSummaryHtml.includes('Run diagnostics') &&
      issueSummaryHtml.includes('Review conflicts') &&
      issueSummaryHtml.includes('change the same game files') &&
      issueSummaryHtml.includes('Managed file changed'),
    'One pop-up lists every finding and offers actionable checks'
  );
  const remembered = nativeRequests
    .slice(beforeSummary)
    .filter((r) => r.action === 'savePreferences')
    .pop();
  assert(
    remembered && remembered.preferences.seenNotices.length === 2 && Object.keys(remembered.preferences).length === 1,
    'Shown findings are remembered in the native preferences, nothing else is saved'
  );
  node('#modal').close();
  ui.collectActionNotices({
    ...changedSnapshot,
    diagnostics: [
      { ...changedSnapshot.diagnostics[0], message: 'Two packages overlap (13).' },
      changedSnapshot.diagnostics[1]
    ]
  });
  assert.equal(ui.state.noticeQueue.length, 0, 'Known findings do not pop up again when a count changes');
  ui.resetNotices();
  const afterRestart = { ...changedSnapshot, settings: { seenNotices: remembered.preferences.seenNotices } };
  ui.collectActionNotices(afterRestart);
  assert.equal(ui.state.noticeQueue.length, 0, 'Findings shown before a restart stay quiet');
  assert.equal(node('#attention-count').textContent, '2', 'Quiet findings still count on the notice button');
  ui.collectActionNotices({
    ...afterRestart,
    diagnostics: [...afterRestart.diagnostics, { code: 'asset-conflict', modIds: ['a', 'b'], message: 'New overlap.' }]
  });
  assert.equal(ui.state.noticeQueue.length, 1, 'A new finding opens the combined pop-up once');
  assert(ui.showNextNotice(true) && node('#modal-content').innerHTML.includes('3 things need a look'));
  node('#modal').close();
  ui.resetNotices();
  ui.considerSetup({
    setup: {
      state: 'needs-attention',
      notices: [
        { id: 'startup', message: 'The startup package is unverified.' },
        { id: 'paks', message: 'The Paks folder is missing.' }
      ]
    }
  });
  ui.collectActionNotices({ diagnostics: [], settings: { seenNotices: [] } });
  assert.equal(ui.state.noticeQueue.length, 1, 'Several setup findings share the one pop-up');
  assert(
    ui.showNextNotice(true) &&
      node('#modal-content').innerHTML.includes('startup package is unverified') &&
      node('#modal-content').innerHTML.includes('Paks folder is missing'),
    'The combined pop-up lists every setup finding'
  );
  node('#modal').close();
  ui.resetNotices();
  ui.resetNotices();
  ui.state.snapshot.mods = ui.state.snapshot.mods.map((mod) => ({
    ...mod,
    imported: mod.id === 'no-recoil',
    enabled: mod.id === 'no-recoil'
  }));
  const beforeRemoval = nativeRequests.length;
  ui.showModDetails('no-recoil');
  node('#detail-remove').onclick();
  assert.equal(
    nativeRequests.slice(beforeRemoval).filter((request) => request.action === 'remove').length,
    0,
    'Removal waits for explicit confirmation'
  );
  await node('#confirm-action').onclick();
  const removalActions = nativeRequests
    .slice(beforeRemoval)
    .filter((request) => ['disable', 'remove'].includes(request.action));
  assert.equal(removalActions[0].action, 'disable', 'Removing an enabled mod disables it first');
  assert.equal(removalActions[1].action, 'remove', 'Cache removal follows a successful disable');

  ui.state.nxm = { registered: false, available: true };
  assert(ui.renderNxmSetting().includes('Enable integration'), 'Available optional browser association can be enabled');
  await ui.configureNxm(true);
  const association = node('#modal-content').innerHTML;
  assert(association.includes('will not replace Vortex'), 'Association confirmation preserves other mod managers');
  assert(
    !nativeRequests.some((request) => request.action === 'nxmRegister'),
    'Association is not changed before confirmation'
  );
  await node('#confirm-action').onclick();
  assert(
    nativeRequests.some((request) => request.action === 'nxmRegister' && request.confirmed === true),
    'Association request carries explicit confirmation'
  );
  assert(ui.state.nxm.registered, 'Association status refreshes after registration');
  ui.state.nxm = { registered: false, available: false, reason: 'Vortex owns this handler.' };
  const occupied = ui.renderNxmSetting();
  assert(!occupied.includes('data-action="nxm-register"'), 'Occupied associations cannot be replaced');
  assert(occupied.includes('PASTE LINK AVAILABLE'), 'Authorized-link fallback remains available');

  ui.state.downloads = { files: [], updates: [{ id: 'no-recoil', fileId: 99999, updateAvailable: true }] };
  assert.equal(ui.fileCatalogMod(99999).id, 'no-recoil', 'New Nexus file IDs map through the update chain');
  ui.authorizedNexusLink();
  const authorizedDialog = node('#modal-content').innerHTML;
  assert(authorizedDialog.includes('value="no-recoil"'), 'Authorized links identify a specific catalog package');
  assert(
    authorizedDialog.includes('value="first-person-beta"'),
    'Review-required beta can use native inspection without being called audited'
  );
  assert(
    !authorizedDialog.includes('value="no-recoil-unlimited-ammo"'),
    'Blocked combined builds stay outside standalone selection'
  );
  const temporaryToken = 'nxm://daysgone/mods/994/files/99999?key=test&expires=9999999999&user_id=1';
  node('#nxm-mod').value = 'no-recoil';
  node('#nxm-link').value = temporaryToken;
  await node('#nxm-submit').onclick();
  const download = nativeRequests.findLast((request) => request.action === 'nexusDownload');
  assert.equal(download.id, 'no-recoil', 'Authorized download keeps the selected mod ID');
  assert.equal(download.nxmUrl, temporaryToken, 'Native code receives the authorized link for validation');
  assert.equal(node('#nxm-link').value, '', 'Temporary authorized link is cleared before completion');

  const snapshotCount = () => nativeRequests.filter((request) => request.action === 'snapshot').length;
  const debounceTimers = () => [...timers.entries()].filter(([, timer]) => timer.delay === 600);
  async function advanceDebounce() {
    const [id, timer] = debounceTimers()[0] || [];
    assert(timer, 'A background refresh was scheduled');
    timers.delete(id);
    await timer.callback();
  }
  ui.state.view = 'settings';
  node('#main-content').innerHTML = 'unsaved-settings-sentinel';
  const before = snapshotCount();
  for (let i = 0; i < 4; i++) nativeMessageListener({ data: { event: 'filesystemChanged' } });
  assert.equal(debounceTimers().length, 1, 'File-event bursts debounce into one refresh');
  assert.equal(snapshotCount(), before, 'No eager scan for each filesystem event');
  await advanceDebounce();
  assert.equal(snapshotCount(), before + 1, 'Debounced event requests one updated snapshot');
  assert.equal(
    node('#main-content').innerHTML,
    'unsaved-settings-sentinel',
    'Background changes preserve unsaved Settings controls'
  );
  assert(
    node('#footer-status').textContent.includes('The list is up to date'),
    'Background refresh gives a quiet status'
  );

  ui.state.busy = true;
  nativeMessageListener({ data: { event: 'filesystemChanged' } });
  const beforeBusy = snapshotCount();
  await advanceDebounce();
  assert.equal(snapshotCount(), beforeBusy, 'Watcher waits while a mutation is busy');
  assert(ui.watchState().dirty, 'Deferred event remains dirty');
  ui.state.busy = false;
  await ui.run('scan', {}, { refresh: false });
  await advanceDebounce();
  assert.equal(snapshotCount(), beforeBusy + 1, 'Deferred event refreshes after operation completion');
  assert(!ui.watchState().dirty, 'Completed watcher refresh clears its dirty flag');
  ui.state.view = 'folders';
  ui.state.directories = directoriesFixture;
  const directoryScans = nativeRequests.filter((request) => request.action === 'directoriesScan').length;
  nativeMessageListener({ data: { event: 'filesystemChanged' } });
  await advanceDebounce();
  assert.equal(
    nativeRequests.filter((request) => request.action === 'directoriesScan').length,
    directoryScans + 1,
    'Visible Folders view quietly rescans startup state after filesystem changes'
  );
  const beforeDownloadEvent = snapshotCount();
  nativeMessageListener({ data: { event: 'nxmDownloadResult', ok: true, data: { imported: true } } });
  await advanceDebounce();
  assert.equal(
    snapshotCount(),
    beforeDownloadEvent + 1,
    'Native browser-download result refreshes package state without a token in UI'
  );

  ui.state.busy = false;
  ui.state.loading = false;
  ui.state.view = 'overview';
  ui.state.profileSession = { active: false };
  node('#modal').close();
  const beforeRestartReview = nativeRequests.length;
  ui.reviewGameRestart();
  assert(
    node('#modal-content').innerHTML.includes('will not stop the game'),
    'Restart confirmation explains normal game closure'
  );
  assert.equal(nativeRequests.length, beforeRestartReview, 'Opening a restart confirmation does not queue anything');
  await node('#restart-queue-confirm').onclick();
  const queuedRestart = nativeRequests
    .slice(beforeRestartReview)
    .find((request) => request.action === 'gameRestartQueue');
  assert(queuedRestart?.confirmed, 'Restart is queued only with explicit confirmation');
  assert(ui.renderGameRestartBanner().includes('Cancel restart'), 'Pending restart remains visible and cancellable');
  const beforeRestartEvent = nativeRequests.length;
  ui.resetNotices();
  ui.handleGameRestartEvent({
    data: {
      pending: false,
      phase: 'profile-review-required',
      profileName: 'Independent ride',
      message: 'Original files restored.'
    }
  });
  assert.equal(ui.state.noticeQueue[0]?.type, 'restart-profile', 'Profile restart waits for a new launch review');
  assert.equal(
    nativeRequests.length,
    beforeRestartEvent,
    'Native profile-restart event never reuses a launch token or automatically starts the game'
  );
  ui.handleGameRestartEvent({
    data: {
      pending: false,
      phase: 'profile-review-required',
      profileName: 'Independent ride',
      message: 'Original files restored.'
    }
  });
  assert.equal(ui.state.noticeQueue.length, 1, 'Repeated restart status does not duplicate the profile review');
  node('#modal').close();
  ui.showNextNotice(true);
  await new Promise((resolve) => setImmediate(resolve));
  assert(
    nativeRequests.slice(beforeRestartEvent).some((request) => request.action === 'profileLaunchPlan'),
    'Profile restart fetches a fresh concrete file plan'
  );
  assert(
    !nativeRequests.slice(beforeRestartEvent).some((request) => request.action === 'profileLaunch'),
    'Profile launch still needs the new plan confirmation'
  );
  ui.state.gameRestart = { pending: false, phase: 'idle' };
  ui.resetNotices();
  node('#modal').close();

  ui.state.gameRestart = { pending: false, phase: 'idle' };
  ui.state.profileSession = { active: false };
  ui.state.view = 'overview';
  const previousFixtureSettings = fixture.settings,
    previousFixtureRunning = fixture.gameRunning,
    previousFixtureSession = fixture.profileSession;
  fixture.settings = { ...fixture.settings, gamePath: 'C:\\Synthetic\\Days Gone' };
  fixture.gameRunning = true;
  fixture.profileSession = { active: false };
  ui.state.snapshot.gameRunning = false;
  const beforeFreshRestart = nativeRequests.length;
  await ui.launchOrReviewRestart();
  assert.equal(nativeRequests[beforeFreshRestart].action, 'snapshot', 'Launch first reads current native game state');
  assert(
    node('#modal-content').innerHTML.includes('Restart after closing Days Gone'),
    'A newly running game replaces a stale Launch decision with restart confirmation'
  );
  assert(
    !nativeRequests
      .slice(beforeFreshRestart)
      .some((request) => request.action === 'launchGame' || request.action === 'gameRestartQueue'),
    'Fresh running-state detection does not launch or queue without confirmation'
  );
  node('#modal').close();
  fixture.gameRunning = false;
  fixture.profileSession = {
    active: true,
    status: 'recovery-pending',
    canRestore: true,
    profileName: 'Interrupted session'
  };
  ui.state.snapshot.gameRunning = true;
  const beforeClosedProfile = nativeRequests.length;
  await ui.launchOrReviewRestart();
  await new Promise((resolve) => setImmediate(resolve));
  assert.equal(ui.state.view, 'profiles', 'A closed profile needing recovery routes to Profiles');
  assert(
    !nativeRequests
      .slice(beforeClosedProfile)
      .some(
        (request) =>
          request.action === 'launchGame' || request.action === 'gameRestartQueue' || request.action === 'profileLaunch'
      ),
    'Profile recovery is not bypassed by a queued or normal launch'
  );
  fixture.profileSession = { active: false };
  ui.state.profileSession = { active: false };
  snapshotFailure = true;
  const beforeFailedLaunch = nativeRequests.length;
  assert.equal(await ui.launchOrReviewRestart(), false, 'Failed fresh inspection blocks launch');
  assert(
    !nativeRequests
      .slice(beforeFailedLaunch)
      .some((request) => request.action === 'launchGame' || request.action === 'gameRestartQueue'),
    'Inspection errors never fall back to cached launch state'
  );
  snapshotFailure = false;
  ui.state.view = 'overview';
  ui.state.snapshot.gameRunning = true;
  const beforeClosedNormal = nativeRequests.length;
  await ui.launchOrReviewRestart();
  assert.deepEqual(
    nativeRequests.slice(beforeClosedNormal).map((request) => request.action),
    ['snapshot', 'launchGame'],
    'A fresh closed normal game launches once after read-only inspection'
  );
  fixture.settings = previousFixtureSettings;
  if (previousFixtureRunning === undefined) delete fixture.gameRunning;
  else fixture.gameRunning = previousFixtureRunning;
  if (previousFixtureSession === undefined) delete fixture.profileSession;
  else fixture.profileSession = previousFixtureSession;
  ui.state.gameRestart = { pending: false, phase: 'idle' };
  ui.state.profileSession = { active: false };
  node('#modal').close();
  nativeMessageListener({ data: { event: 'interfaceRecovered', message: 'ignored secret string' } });
  assert(
    !node('#interface-recovery').hidden && !node('#interface-recovery-message').textContent.includes('secret'),
    'Native renderer recovery uses fixed public copy'
  );
  assert(
    globalListeners.has('error') && globalListeners.has('unhandledrejection'),
    'Both global script failure handlers are registered'
  );
  assert(
    nativeRequests.every((request) => /^[^:]+:[0-9]+$/.test(request.requestId)),
    'Each request has a per-page namespace before its sequence'
  );
  ui.state.busy = false;
  ui.state.loading = false;
  ui.state.view = 'settings';
  node('#modal').close();
  let preventedFaults = 0;
  const secretFault = {
    type: 'unhandledrejection',
    preventDefault() {
      preventedFaults++;
    },
    get reason() {
      throw Error('A secret reason must never be read');
    }
  };
  for (let i = 0; i < 20; i++) assert(ui.handleInterfaceFault(secretFault), 'Unhandled errors are safely contained');
  assert.equal(
    preventedFaults,
    20,
    'Default console rejection output is suppressed without reading a potentially sensitive reason'
  );
  assert(
    !node('#interface-recovery').hidden && node('#interface-recovery-message').textContent.includes('keeps hitting'),
    'Repeated errors update one persistent recovery notice'
  );
  assert(
    !ui.handleInterfaceFault({ type: 'error', target: { tagName: 'IMG' } }),
    'Missing optional artwork is not treated as a script failure'
  );
  ui.state.busy = true;
  const faultRequests = nativeRequests.length;
  assert.equal(await ui.recoverInterfaceView(), false, 'Recovery waits for a running native action');
  assert.equal(nativeRequests.length, faultRequests, 'Busy recovery never queues or repeats an action');
  ui.state.busy = false;
  const beforeRecovery = nativeRequests.length;
  node('#main-content').innerHTML = 'unsaved settings remain';
  assert(await ui.recoverInterfaceView(), 'Read-only refresh can recover the interface');
  const recoveryRequests = nativeRequests.slice(beforeRecovery);
  assert.equal(recoveryRequests.length, 1, 'Recovery makes one read-only request');
  assert.equal(recoveryRequests[0].action, 'snapshot', 'Recovery never replays a mutation');
  assert.equal(
    node('#main-content').innerHTML,
    'unsaved settings remain',
    'Recovery preserves an unsaved Settings form'
  );
  assert(node('#interface-recovery').hidden, 'Successful refresh clears the recovery banner');
  // Local authority is distinct from public Nexus metadata, including coincident hashes/IDs.
  const localMod = {
    id: 'local:opaque-hash',
    name: 'Local <test> weapon',
    identityStatus: 'local-import',
    localManaged: true,
    provenance: { kind: 'local-import' },
    category: 'Local mods',
    version: '',
    imported: true,
    enabled: false,
    sha256: 'c0ffee'.padEnd(64, '0'),
    assets: ['BendGame/Weapons/local.uasset']
  };
  const hiddenApproval = {
    ...localMod,
    id: 'local:profile-only',
    name: 'Removed local approval',
    imported: false,
    owned: false,
    cached: false,
    enabled: false
  };
  const coincidentLocal = {
    ...localMod,
    id: 'local:coincident',
    nexusModId: 994,
    nexusFileId: catalog.mods[0].nexusFileId
  };
  ui.state.snapshot = ui.normalizeSnapshot({
    ...fixture,
    mods: [...catalog.mods, localMod, hiddenApproval, coincidentLocal]
  });
  ui.state.view = 'mods';
  ui.state.filter = 'available';
  ui.state.search = '';
  ui.state.category = 'all';
  ui.state.profileSession = { active: false };
  ui.state.busy = false;
  ui.state.preview = false;
  ui.state.downloads = {
    files: [],
    updates: [
      { id: localMod.id, updateAvailable: true, version: '9.0' },
      { id: coincidentLocal.id, updateAvailable: true, version: '9.0' }
    ]
  };
  assert(
    ui.isLocalMod(localMod) && ui.isLocalMod({ localManaged: true }),
    'Native local authority has an explicit discriminator'
  );
  assert(
    !ui.libraryMods().some((m) => m.id === hiddenApproval.id),
    'Removed local approval metadata does not leave a library ghost'
  );
  assert(
    ui.libraryMods().some((m) => m.id === localMod.id),
    'Cached local packages stay in the shared library'
  );
  assert.equal(
    ui.previewPageId(coincidentLocal),
    null,
    'Local provenance never inherits an official preview from coincident Nexus IDs'
  );
  assert.equal(
    ui.installedUpdate({ ...localMod, enabled: true }),
    null,
    'Local packages never receive Nexus update status'
  );
  assert.equal(ui.nexusModId(coincidentLocal), 0, 'Local authority has no derived Nexus source page');
  ui.state.filter = 'stored';
  const localRows = ui.renderMods();
  ui.state.filter = 'available';
  assert(
    localRows.includes('Added from a file') &&
      !localRows.includes('Version not supplied') &&
      localRows.includes('data-toggle="local:opaque-hash"'),
    'Local imports use the ordinary managed controls with honest version text'
  );
  assert(
    localRows.includes('Local &lt;test&gt; weapon') && !localRows.includes('Removed local approval'),
    'Local metadata is escaped and removed approval-only rows are hidden'
  );
  node('#modal').close();
  ui.showModDetails(localMod.id);
  const localDetails = node('#mod-details-panel').innerHTML;
  assert(
    !localDetails.includes('detail-nexus') && localDetails.includes('cannot confirm who made it'),
    'Local details do not offer a Nexus page or imply publisher verification'
  );
  ui.authorizedNexusLink();
  assert(
    !node('#modal-content').innerHTML.includes('value="local:'),
    'Authorized Nexus release selection excludes every local package'
  );
  node('#modal').close();
  const localReview = {
    reviewRequired: true,
    reviewToken: 'native-local-frozen-review',
    source: 'local-import',
    mod: localMod,
    conflicts: [
      { name: 'Installed <other>', count: 80, assets: ['BendGame/Asset<one>.uasset'], evidenceTruncated: true }
    ],
    warnings: ['Keep a source backup']
  };
  actionFixtures.set('browseImports', localReview);
  const beforeLocalImport = nativeRequests.length;
  await ui.handleAction('import');
  assert.equal(
    nativeRequests[beforeLocalImport].action,
    'browseImports',
    'Local import starts at the native file picker'
  );
  assert(
    !nativeRequests.slice(beforeLocalImport).some((r) => r.action === 'confirmReviewedImport'),
    'Local inspection does not approve itself'
  );
  const localReviewHtml = node('#modal-content').innerHTML;
  assert(
    localReviewHtml.includes('Add this mod?') &&
      localReviewHtml.includes('checked copy') &&
      localReviewHtml.includes('80 shared assets') &&
      localReviewHtml.includes('sample'),
    'Local review identifies frozen bytes and bounded conflict evidence'
  );
  assert(!localReviewHtml.includes('Nexus Mods · mod'), 'A local file receives no forged official source label');
  await node('#review-import-confirm').onclick();
  const localConfirm = nativeRequests.slice(beforeLocalImport).find((r) => r.action === 'confirmReviewedImport');
  assert.equal(localConfirm.reviewToken, 'native-local-frozen-review');
  assert.equal(localConfirm.confirmed, true);
  assert(
    !('path' in localConfirm) && !('mod' in localConfirm) && !('provenance' in localConfirm),
    'Local confirmation submits only the native review token and explicit approval'
  );
  actionFixtures.delete('browseImports');
  node('#modal').close();
  const external = {
    id: 'external:saved:manual.pak',
    name: 'Manual <mod>',
    file: 'manual.pak',
    path: 'Synthetic/PAKs/<manual>.pak',
    location: 'saved',
    relative: 'manual.pak',
    sha256: 'a'.repeat(64),
    bytes: 123,
    owned: false,
    canRemoveExternal: true,
    canReviewAdoption: true
  };
  assert.equal(
    ui.previewPageId(external),
    null,
    'Unidentified external PAKs cannot inherit the author’s official page picture'
  );
  const oldInstalled = fixture.installed;
  const oldRunning = fixture.gameRunning;
  fixture.installed = [external];
  fixture.gameRunning = false;
  fixture.profileSession = { active: false };
  ui.state.snapshot = ui.normalizeSnapshot(fixture);
  ui.state.profileSession = { active: false };
  actionFixtures.set('reviewExternalRemoval', {
    planToken: 'native-external-plan',
    file: external,
    warnings: ['Removal cannot be undone.'],
    expiresInSeconds: 300
  });
  const externalRows = ui.externalList();
  assert(
    externalRows.includes('>Delete<') && externalRows.includes('Synthetic/PAKs/&lt;manual&gt;.pak'),
    'Installed external files expose a guarded action with an escaped actual path'
  );
  const beforeExternal = nativeRequests.length;
  assert(await ui.reviewExternalRemoval(external.id), 'Eligible external file opens a fresh native review');
  assert.equal(
    nativeRequests[beforeExternal].action,
    'snapshot',
    'External action refreshes game and file state first'
  );
  const requestedReview = nativeRequests.slice(beforeExternal).find((r) => r.action === 'reviewExternalRemoval');
  assert.equal(requestedReview.id, external.id);
  assert(!('path' in requestedReview), 'External review never sends a file path from the renderer');
  assert(
    !nativeRequests.slice(beforeExternal).some((r) => r.action === 'removeExternal'),
    'Opening exact-file review does not delete a file'
  );
  const externalReviewHtml = node('#modal-content').innerHTML;
  assert(
    externalReviewHtml.includes('Synthetic/PAKs/&lt;manual&gt;.pak') &&
      externalReviewHtml.includes(external.sha256) &&
      externalReviewHtml.includes('cannot be undone'),
    'External removal review identifies exact bytes/path and irreversibility'
  );
  await node('#external-remove-confirm').onclick();
  const externalConfirm = nativeRequests.slice(beforeExternal).find((r) => r.action === 'removeExternal');
  assert.equal(externalConfirm.id, external.id);
  assert.equal(externalConfirm.planToken, 'native-external-plan');
  assert.equal(externalConfirm.confirmed, true);
  assert(
    !('path' in externalConfirm) && !('sha256' in externalConfirm),
    'External removal submits no arbitrary path or claimed hash'
  );
  node('#modal').close();
  const directAdoption = {
    ...localReview,
    reviewToken: 'native-direct-adoption',
    adoption: { mode: 'take-ownership', enabledAfterConfirmation: true, file: external }
  };
  actionFixtures.set('reviewExternalAdoption', directAdoption);
  const beforeDirect = nativeRequests.length;
  assert(await ui.reviewExternalAdoption(external.id), 'Direct installed file gets a fresh management review');
  assert.equal(
    nativeRequests[beforeDirect].action,
    'snapshot',
    'Installed management refreshes native process/file state first'
  );
  const adoptionRequest = nativeRequests.slice(beforeDirect).find((r) => r.action === 'reviewExternalAdoption');
  assert.equal(adoptionRequest.id, external.id);
  assert(!('path' in adoptionRequest), 'Management review accepts only the native installed ID');
  const directHtml = node('#modal-content').innerHTML;
  assert(
    directHtml.includes('takes it over') &&
      directHtml.includes('it stays enabled') &&
      directHtml.includes('Disable will then remove') &&
      directHtml.includes('Synthetic/PAKs/&lt;manual&gt;.pak'),
    'Direct adoption explains installed ownership and future disable behavior with exact identity'
  );
  assert(
    directHtml.includes('Manage installed file') && !directHtml.includes('enabling it is a later choice'),
    'Direct adoption is not mislabeled as a disabled library import'
  );
  assert(
    !nativeRequests.slice(beforeDirect).some((r) => r.action === 'confirmReviewedImport'),
    'Management requires explicit reviewed approval'
  );
  await node('#review-import-confirm').onclick();
  const directConfirm = nativeRequests.slice(beforeDirect).find((r) => r.action === 'confirmReviewedImport');
  assert.equal(directConfirm.reviewToken, 'native-direct-adoption');
  assert(
    directConfirm.confirmed && !('path' in directConfirm) && !('adoption' in directConfirm),
    'Direct adoption submits no claimed path or ownership mode'
  );
  actionFixtures.set('reviewExternalAdoption', {
    ...directAdoption,
    reviewToken: 'native-copy-only',
    adoption: { ...directAdoption.adoption, mode: 'copy-only', enabledAfterConfirmation: false }
  });
  node('#modal').close();
  const beforeCopyAdoption = nativeRequests.length;
  assert(await ui.reviewExternalAdoption(external.id));
  const copyAdoptionHtml = node('#modal-content').innerHTML;
  assert(
    copyAdoptionHtml.includes('installed by hand and stays as it is') &&
      copyAdoptionHtml.includes('delete the hand-installed file first') &&
      copyAdoptionHtml.includes('Add to my library'),
    'Nested/game-folder import does not silently adopt or remove its original'
  );
  await node('#review-import-confirm').onclick();
  const copyAdoptionConfirm = nativeRequests
    .slice(beforeCopyAdoption)
    .find((r) => r.action === 'confirmReviewedImport');
  assert.equal(copyAdoptionConfirm.reviewToken, 'native-copy-only');
  assert(
    !nativeRequests.slice(beforeCopyAdoption).some((r) => r.action === 'enable' || r.action === 'removeExternal'),
    'Copy-only approval does not enable or delete files'
  );
  // Manage all: one confirmation, then each eligible file goes through the native review and confirm by token.
  const external2 = {
    ...external,
    id: 'external:saved:second.pak',
    name: 'Second <b>pak</b>',
    path: 'Synthetic/PAKs/second.pak',
    relative: 'second.pak'
  };
  fixture.installed = [external, external2];
  ui.state.snapshot = ui.normalizeSnapshot(fixture);
  actionFixtures.set('reviewExternalAdoption', directAdoption);
  assert(
    ui.externalList().includes('data-action="external-manage-all"'),
    'Several eligible outside files offer Manage all'
  );
  const beforeAll = nativeRequests.length;
  await ui.manageAllExternal();
  assert(
    node('#modal-content').innerHTML.includes('Second &lt;b&gt;pak&lt;/b&gt;') &&
      !nativeRequests
        .slice(beforeAll)
        .some((r) => r.action === 'confirmReviewedImport' || r.action === 'reviewExternalAdoption'),
    'Manage all asks once and lists the escaped files before changing anything'
  );
  await node('#manage-all-confirm').onclick();
  const allReviews = nativeRequests.slice(beforeAll).filter((r) => r.action === 'reviewExternalAdoption');
  const allConfirms = nativeRequests.slice(beforeAll).filter((r) => r.action === 'confirmReviewedImport');
  assert(
    allReviews.length === 2 &&
      allReviews.every((r) => !('path' in r)) &&
      allConfirms.length === 2 &&
      allConfirms.every((r) => r.reviewToken === 'native-direct-adoption' && r.confirmed && !('path' in r)),
    'Manage all reviews and confirms each file by its native token'
  );
  node('#modal').close();
  // Automatic management: nothing happens while the setting is off; when on, each eligible file is taken over
  // without a dialog, and only once per session.
  fixture.settings = { ...fixture.settings, autoManageExternal: false };
  ui.state.snapshot = ui.normalizeSnapshot(fixture);
  let beforeAutoManage = nativeRequests.length;
  assert.equal(await ui.autoManageExternal(), false, 'Automatic management stays off when switched off');
  assert(!nativeRequests.slice(beforeAutoManage).some((r) => r.action === 'reviewExternalAdoption'));
  // On by default: a setup that never chose runs it (the native default is on; see preferences_tests).
  const { autoManageExternal: _unset, ...withoutChoice } = fixture.settings;
  fixture.settings = withoutChoice;
  ui.state.snapshot = ui.normalizeSnapshot(fixture);
  delete ui.state.snapshot.settings.autoManageExternal;
  beforeAutoManage = nativeRequests.length;
  assert(await ui.autoManageExternal(), 'Automatic management runs by default');
  const autoManageConfirms = nativeRequests.slice(beforeAutoManage).filter((r) => r.action === 'confirmReviewedImport');
  assert(
    autoManageConfirms.length === 2 && autoManageConfirms.every((r) => r.reviewToken === 'native-direct-adoption'),
    'Automatic management takes each eligible file over through the native review'
  );
  beforeAutoManage = nativeRequests.length;
  assert.equal(await ui.autoManageExternal(), false, 'A file is tried automatically only once per session');
  fixture.settings = { ...fixture.settings, autoManageExternal: false };
  ui.state.snapshot = ui.normalizeSnapshot(fixture);
  fixture.installed = [external];
  // Stop managing all: one confirmation, then one native request with the mod IDs only.
  const oldMods = fixture.mods;
  fixture.mods = catalog.mods.map((m, index) => (index < 2 ? { ...m, enabled: true, owned: true, imported: true } : m));
  ui.state.snapshot = ui.normalizeSnapshot(fixture);
  assert.equal(ui.releasableMods().length, 2, 'Enabled managed mods can be handed back');
  const beforeRelease = nativeRequests.length;
  await ui.releaseAllManaged();
  assert(
    node('#modal-content').innerHTML.includes('stay installed') &&
      !nativeRequests.slice(beforeRelease).some((r) => r.action === 'releaseManaged'),
    'Stop managing all explains that files stay installed and asks first'
  );
  await node('#release-all-confirm').onclick();
  const release = nativeRequests.slice(beforeRelease).find((r) => r.action === 'releaseManaged');
  assert(
    release &&
      release.confirmed === true &&
      JSON.stringify(release.ids) === JSON.stringify(fixture.mods.slice(0, 2).map((m) => m.id)) &&
      !('path' in release),
    'Stop managing all sends only the confirmed mod IDs'
  );
  node('#modal').close();
  fixture.mods = oldMods;
  ui.state.snapshot = ui.normalizeSnapshot(fixture);
  actionFixtures.delete('reviewExternalAdoption');
  const oldCanAdopt = external.canReviewAdoption;
  external.canReviewAdoption = false;
  external.adoptionReason = 'The index cannot be read.';
  const beforeIneligible = nativeRequests.length;
  assert.equal(
    await ui.reviewExternalAdoption(external.id),
    false,
    'Unsupported installed packages cannot enter adoption'
  );
  assert(
    !nativeRequests.slice(beforeIneligible).some((r) => r.action === 'reviewExternalAdoption'),
    'Eligibility refusal does not request a native adoption plan'
  );
  external.canReviewAdoption = oldCanAdopt;
  delete external.adoptionReason;
  fixture.gameRunning = true;
  ui.state.snapshot.gameRunning = false;
  const beforeRunningRemoval = nativeRequests.length;
  assert.equal(
    await ui.reviewExternalRemoval(external.id),
    false,
    'A freshly detected running game blocks stale removal controls'
  );
  assert(
    !nativeRequests
      .slice(beforeRunningRemoval)
      .some((r) => r.action === 'reviewExternalRemoval' || r.action === 'removeExternal'),
    'Running game blocks the native removal request'
  );
  fixture.gameRunning = false;
  fixture.profileSession = { active: true };
  ui.state.profileSession = { active: false };
  const beforeProfileRemoval = nativeRequests.length;
  assert.equal(
    await ui.reviewExternalRemoval(external.id),
    false,
    'Unfinished profile restoration blocks external removal'
  );
  assert(
    !nativeRequests
      .slice(beforeProfileRemoval)
      .some((r) => r.action === 'reviewExternalRemoval' || r.action === 'removeExternal'),
    'Profile recovery never gets bypassed'
  );
  fixture.profileSession = { active: false };
  ui.state.profileSession = { active: false };
  ui.state.busy = true;
  const beforeBusyRemoval = nativeRequests.length;
  assert.equal(await ui.reviewExternalRemoval(external.id), false, 'Busy state blocks another external action');
  assert.equal(nativeRequests.length, beforeBusyRemoval);
  ui.state.busy = false;
  snapshotFailure = true;
  const beforeFailedRemoval = nativeRequests.length;
  assert.equal(await ui.reviewExternalRemoval(external.id), false, 'Failed fresh snapshot blocks file removal');
  assert(
    !nativeRequests
      .slice(beforeFailedRemoval)
      .some((r) => r.action === 'reviewExternalRemoval' || r.action === 'removeExternal')
  );
  snapshotFailure = false;
  actionFixtures.delete('reviewExternalRemoval');
  fixture.installed = oldInstalled;
  if (oldRunning === undefined) delete fixture.gameRunning;
  else fixture.gameRunning = oldRunning;
  delete fixture.profileSession;
  const rawWindows = compact(fs.readFileSync(path.join(appRoot, 'src/main.cpp'), 'utf8')),
    rawLinux = compact(fs.readFileSync(path.join(appRoot, 'src/linux_main.cpp'), 'utf8'));
  assert(
    contains(rawWindows, 'action=="import"||action=="inspectLocalImport"'),
    'Windows raw bridge rejects file-path import endpoints before native picker routing'
  );
  assert(
    contains(rawLinux, 'action=="import"||action=="inspectLocalImport"'),
    'Linux raw bridge rejects file-path import endpoints before native picker routing'
  );
  ui.state.nexus = { connected: true, offline: true, tierVerified: false, premium: null };
  assert(
    ui.renderSettings().includes('Saved connection · verification unavailable'),
    'Unverified saved login is not headed as an online verified connection'
  );
  assert(
    ui.renderDownloads().includes('Saved connection · verification unavailable'),
    'Downloads disclose saved connection verification state'
  );
  ui.state.nexus = { connected: true, premium: false, tierVerified: true };
  assert(
    ui.renderDownloads().includes('paste the authorized link') &&
      !ui.renderDownloads().includes('then import the extracted PAK'),
    'Free Nexus flow matches native authorized download inspection'
  );
  assert(
    ui.renderProfiles().includes('Your own mods come back when the game closes') &&
      fs.readFileSync(path.join(appRoot, 'ui/help.js'), 'utf8').includes('The destination must have approved'),
    'Profiles are explained plainly; the guide keeps the exact destination approval'
  );
  // Forgetting a stale installation is explicitly metadata-only and never adopts its replacement.
  const previousForgetFixture = {
    mods: fixture.mods,
    gameRunning: fixture.gameRunning,
    profileSession: fixture.profileSession,
    recoveryRequired: fixture.recoveryRequired
  };
  const brokenMod = {
    ...localMod,
    id: 'local:old-installed',
    name: 'Old <installation>',
    packageFile: 'Test_P.pak',
    sha256: 'a'.repeat(64),
    owned: true,
    imported: true,
    enabled: false,
    status: 'changed',
    canForgetInstallation: true
  };
  const forgetPlan = {
    planToken: 'native-forget-plan',
    mod: { id: brokenMod.id, name: brokenMod.name, packageFile: brokenMod.packageFile, sha256: brokenMod.sha256 },
    original: { file: brokenMod.packageFile, sha256: brokenMod.sha256 },
    current: {
      status: 'changed',
      path: 'Synthetic/PAKs/<replacement>.pak',
      present: true,
      sha256: 'b'.repeat(64),
      bytes: 321
    },
    warnings: ['The present file stays unchanged.'],
    expiresInSeconds: 300
  };
  fixture.mods = [...catalog.mods, brokenMod];
  fixture.gameRunning = false;
  fixture.profileSession = { active: false };
  fixture.recoveryRequired = false;
  ui.state.snapshot = ui.normalizeSnapshot(fixture);
  ui.state.profileSession = { active: false };
  ui.state.busy = false;
  ui.state.preview = false;
  ui.state.view = 'mods';
  ui.state.filter = 'available';
  ui.state.search = '';
  ui.state.category = 'all';
  ui.state.downloads = { files: [], updates: [] };
  node('#modal').close();
  assert(
    ui.brokenInstallationRecord(brokenMod) && !ui.brokenInstallationRecord({ ...brokenMod, owned: false }),
    'Only a registered broken installation has a forget-record flow'
  );
  ui.state.filter = 'review';
  const brokenRows = ui.renderMods();
  ui.state.filter = 'available';
  assert(
    brokenRows.includes('Review record') && !brokenRows.includes('data-toggle="local:old-installed"'),
    'A stale owned record does not misleadingly offer Enable'
  );
  ui.showModDetails(brokenMod.id);
  const brokenDetails = node('#mod-details-panel').innerHTML;
  assert(
    brokenDetails.includes('Review installation record') &&
      !brokenDetails.includes('id="detail-remove"') &&
      !brokenDetails.includes('id="detail-toggle"'),
    'Broken ownership leads to record review before cache removal or enable'
  );
  node('#modal').close();
  actionFixtures.set('reviewForgetInstallation', forgetPlan);
  const beforeForget = nativeRequests.length;
  assert(await ui.reviewForgetInstallation(brokenMod.id));
  assert.equal(
    nativeRequests[beforeForget].action,
    'snapshot',
    'Record review first refreshes native game and file state'
  );
  const forgetReviewRequest = nativeRequests.slice(beforeForget).find((r) => r.action === 'reviewForgetInstallation');
  assert.equal(forgetReviewRequest.id, brokenMod.id);
  assert(
    !('path' in forgetReviewRequest) && !('sha256' in forgetReviewRequest),
    'Record review submits only the native mod ID'
  );
  const forgetHtml = node('#modal-content').innerHTML;
  assert(
    forgetHtml.includes('No file is deleted, moved or replaced') && forgetHtml.includes('was replaced outside'),
    'Record review precisely describes metadata-only effects'
  );
  assert(
    forgetHtml.includes('Synthetic/PAKs/&lt;replacement&gt;.pak') &&
      forgetHtml.includes('a'.repeat(64)) &&
      forgetHtml.includes('b'.repeat(64)),
    'Review contrasts escaped current file evidence with the old expected identity'
  );
  assert(
    forgetHtml.includes('shows under Installed outside the toolkit, where you can manage it'),
    'Forgetting does not silently manage or exempt a replacement'
  );
  assert(
    !nativeRequests.slice(beforeForget).some((r) => r.action === 'forgetInstallation'),
    'Opening a record review makes no ownership change'
  );
  await node('#forget-installation-confirm').onclick();
  const forgetConfirmation = nativeRequests.slice(beforeForget).find((r) => r.action === 'forgetInstallation');
  assert.equal(forgetConfirmation.id, brokenMod.id);
  assert.equal(forgetConfirmation.planToken, 'native-forget-plan');
  assert.equal(forgetConfirmation.confirmed, true);
  assert(
    !('path' in forgetConfirmation) && !('sha256' in forgetConfirmation) && !('current' in forgetConfirmation),
    'Metadata confirmation has no renderer-supplied path or file identity'
  );
  assert(
    !nativeRequests
      .slice(beforeForget)
      .some((r) =>
        ['remove', 'removeExternal', 'enable', 'disable', 'reviewExternalAdoption', 'confirmReviewedImport'].includes(
          r.action
        )
      ),
    'Forgetting never invokes a file mutation, adoption or import'
  );
  // Deleted or replaced managed files are handled automatically through the same native review and token.
  fixture.mods = [...catalog.mods, brokenMod];
  ui.state.snapshot = ui.normalizeSnapshot(fixture);
  actionFixtures.set('reviewForgetInstallation', forgetPlan);
  const beforeReconcile = nativeRequests.length;
  assert(await ui.reconcileInstallationRecords(), 'A replaced managed file is handled without asking');
  const reconcileForget = nativeRequests.slice(beforeReconcile).find((r) => r.action === 'forgetInstallation');
  assert(
    reconcileForget && reconcileForget.planToken === 'native-forget-plan' && reconcileForget.id === brokenMod.id,
    'Automatic handling uses the native review token'
  );
  assert(
    !nativeRequests
      .slice(beforeReconcile)
      .some((r) => ['remove', 'removeExternal', 'enable', 'disable'].includes(r.action)),
    'Automatic handling never deletes, moves or enables a file'
  );
  assert.equal(await ui.reconcileInstallationRecords(), false, 'Each change is handled once');
  ui.collectActionNotices({
    ...ui.state.snapshot,
    diagnostics: [{ code: 'owned-file-changed', modId: brokenMod.id, message: 'Changed outside.' }],
    settings: { seenNotices: [] }
  });
  assert.equal(ui.state.issues.length, 0, 'Records handled automatically do not open a pop-up');
  const missingRecord = { ...brokenMod, status: 'missing' };
  fixture.mods = [...catalog.mods, missingRecord];
  actionFixtures.set('reviewForgetInstallation', {
    ...forgetPlan,
    planToken: 'native-missing-record',
    current: { status: 'missing', path: 'Synthetic/PAKs/missing.pak', present: false }
  });
  node('#modal').close();
  assert(await ui.reviewForgetInstallation(missingRecord.id));
  assert(
    node('#modal-content').innerHTML.includes('Missing · no file exists') &&
      !node('#modal-content').innerHTML.includes('Current SHA-256'),
    'Missing-file review does not invent a current hash'
  );
  actionErrors.set(
    'forgetInstallation',
    'The current file changed after review. No installation record was forgotten.'
  );
  const beforeStaleForget = nativeRequests.length;
  await node('#forget-installation-confirm').onclick();
  assert.equal(
    nativeRequests.slice(beforeStaleForget).filter((r) => r.action === 'forgetInstallation').length,
    1,
    'A stale native token is attempted once without retry'
  );
  assert(
    node('#modal-content').innerHTML.includes('current file changed after review'),
    'Native changed-file refusal remains actionable'
  );
  actionErrors.delete('forgetInstallation');
  node('#modal').close();
  const validRecord = { ...brokenMod, status: 'enabled', enabled: true, canForgetInstallation: false };
  fixture.mods = [...catalog.mods, validRecord];
  ui.state.snapshot = ui.normalizeSnapshot(fixture);
  ui.showModDetails(validRecord.id);
  const validDetails = node('#mod-details-panel').innerHTML;
  assert(
    !validDetails.includes('data-record-review') &&
      validDetails.includes('id="detail-toggle"') &&
      validDetails.includes('Disable'),
    'An unchanged owned file uses Disable, never Forget'
  );
  node('#modal').close();
  const beforeValidForget = nativeRequests.length;
  assert.equal(await ui.reviewForgetInstallation(validRecord.id), false);
  assert(
    !nativeRequests
      .slice(beforeValidForget)
      .some((r) => r.action === 'reviewForgetInstallation' || r.action === 'forgetInstallation'),
    'A newly restored matching installation blocks a stale Forget control'
  );
  fixture.mods = [
    ...catalog.mods,
    {
      ...brokenMod,
      canForgetInstallation: false,
      forgetInstallationReason: 'The current file is not a safe ordinary file.'
    }
  ];
  const beforeUnsafeForget = nativeRequests.length;
  assert.equal(await ui.reviewForgetInstallation(brokenMod.id), false);
  assert(
    !nativeRequests.slice(beforeUnsafeForget).some((r) => r.action === 'reviewForgetInstallation'),
    'Native ineligibility blocks record review'
  );
  fixture.mods = [...catalog.mods, brokenMod];
  fixture.gameRunning = true;
  ui.state.snapshot.gameRunning = false;
  const beforeGameForget = nativeRequests.length;
  assert.equal(await ui.reviewForgetInstallation(brokenMod.id), false);
  assert(
    !nativeRequests
      .slice(beforeGameForget)
      .some((r) => r.action === 'reviewForgetInstallation' || r.action === 'forgetInstallation'),
    'Freshly running game blocks metadata ownership changes'
  );
  fixture.gameRunning = false;
  fixture.profileSession = { active: true };
  const beforeProfileForget = nativeRequests.length;
  assert.equal(await ui.reviewForgetInstallation(brokenMod.id), false);
  assert(
    !nativeRequests.slice(beforeProfileForget).some((r) => r.action === 'reviewForgetInstallation'),
    'Active or unresolved profile session blocks record review'
  );
  assert(ui.sessionBlocksAction('forgetInstallation'), 'Profile session gate also covers direct forget confirmation');
  fixture.profileSession = { active: false };
  ui.state.profileSession = { active: false };
  fixture.recoveryRequired = true;
  const beforeRecoveryForget = nativeRequests.length;
  assert.equal(await ui.reviewForgetInstallation(brokenMod.id), false);
  assert(
    !nativeRequests.slice(beforeRecoveryForget).some((r) => r.action === 'reviewForgetInstallation'),
    'Pending package recovery is not bypassed by Forget'
  );
  fixture.recoveryRequired = false;
  ui.state.busy = true;
  const beforeBusyForget = nativeRequests.length;
  assert.equal(await ui.reviewForgetInstallation(brokenMod.id), false);
  assert.equal(nativeRequests.length, beforeBusyForget, 'Busy Forget cannot queue a second request');
  ui.state.busy = false;
  snapshotFailure = true;
  const beforeFailedForget = nativeRequests.length;
  assert.equal(await ui.reviewForgetInstallation(brokenMod.id), false);
  assert(
    !nativeRequests
      .slice(beforeFailedForget)
      .some((r) => r.action === 'reviewForgetInstallation' || r.action === 'forgetInstallation'),
    'Failed fresh snapshot never falls back to stored ownership state'
  );
  snapshotFailure = false;
  ui.state.snapshot = ui.normalizeSnapshot(fixture);
  ui.state.diagnosticReport = {
    checks: [
      {
        id: 'owned:' + brokenMod.id,
        title: 'Owned package changed',
        status: 'failed',
        severity: 'error',
        message: 'Review the installation record.'
      }
    ],
    summary: { errors: 1, warnings: 0, passed: 0, unknown: 0 }
  };
  assert(
    ui.renderDiagnosticPage().includes('data-details="local:old-installed"'),
    'An owned-package diagnostic links to the affected Details review'
  );
  const uninstallHelp = context.window.FIELD_GUIDE.find((article) => article.id === 'uninstall').body;
  assert(
    uninstallHelp.includes('Forget old installation record') &&
      uninstallHelp.includes('approve that exact replacement separately'),
    'Manual local update help preserves separate record and replacement approval'
  );
  actionFixtures.delete('reviewForgetInstallation');
  for (const [key, value] of Object.entries(previousForgetFixture)) {
    if (value === undefined) delete fixture[key];
    else fixture[key] = value;
  }
  ui.state.profileSession = { active: false };
  node('#modal').close();
  await currentPlanWorkflows();
  await approvedUxWorkflows();
  await auditUxWorkflows();
  // A request timeout is not evidence that a native file operation was cancelled.
  const originalAuditPost = context.window.chrome.webview.postMessage;
  const heldAuditRequests = [];
  context.window.chrome.webview.postMessage = (request) => { nativeRequests.push(request); heldAuditRequests.push(request); };
  ui.state.busy = false;
  ui.state.profileSession = { active: false };
  const slowBackup = ui.run('backupSaves', {}, { refresh: false });
  const heldBackup = heldAuditRequests.at(-1);
  assert.equal(ui.state.busy, true, 'A foreground save operation keeps its lock');
  const foregroundTimer = [...timers.values()].find((timer) => timer.delay === 120000);
  assert(foregroundTimer, 'A slow-operation warning is scheduled');
  foregroundTimer.callback();
  assert.equal(ui.state.busy, true, 'Warning about a slow operation does not unlock retries');
  assert(ui.pending.has(heldBackup.requestId), 'A slow operation remains attached to its native completion');
  const countBeforeRetry = heldAuditRequests.length;
  await ui.run('remove', { id: 'not-sent' });
  assert.equal(heldAuditRequests.length, countBeforeRetry, 'A second mutation is not queued while the first is unfinished');
  nativeMessageListener({ data: { requestId: heldBackup.requestId, ok: true, data: { files: 1 } } });
  assert.equal((await slowBackup).files, 1, 'Late native success completes the original operation');
  assert.equal(ui.state.busy, false, 'Only native completion releases the foreground lock');
  const backgroundWait = ui.request('snapshot', { action: 'remove', requestId: 'spoofed' });
  const heldRead = heldAuditRequests.at(-1);
  assert.equal(heldRead.action, 'snapshot', 'Arguments cannot replace the native action');
  assert.notEqual(heldRead.requestId, 'spoofed', 'Arguments cannot replace the native correlation ID');
  const backgroundFailure = assert.rejects(backgroundWait, /did not finish/);
  const backgroundTimer = [...timers.values()].filter((timer) => timer.delay === 120000).at(-1);
  backgroundTimer.callback();
  await backgroundFailure;
  assert(!ui.pending.has(heldRead.requestId), 'Unanswered background status reads still expire');
  context.window.chrome.webview.postMessage = () => { throw new Error('Synthetic bridge failure'); };
  const beforeBridgeFailure = ui.pending.size;
  await assert.rejects(ui.request('snapshot'), /Synthetic bridge failure/);
  assert.equal(ui.pending.size, beforeBridgeFailure, 'A synchronous bridge failure leaves no pending timer/request');
  context.window.chrome.webview.postMessage = originalAuditPost;

  // A user may read another page while a requested settings save completes.
  const originalAuditQuery = context.document.querySelector;
  context.document.querySelector = (selector) => selector === '#settings-note' ? null : originalAuditQuery(selector);
  ui.state.snapshot.gameRunning = false;
  ui.state.snapshot.recoveryRequired = false;
  ui.state.snapshot.saveRecoveryRequired = false;
  ui.state.view = 'mods';
  actionFixtures.set('saveSettings', { ...fixture, settings: { ...fixture.settings, gamePath: 'C:/Synthetic/new-game' } });
  nextSnapshotFixture = { ...fixture, settings: { ...fixture.settings, gamePath: 'C:/Synthetic/new-game' } };
  const beforeNavigatedSave = nativeRequests.length;
  await ui.finishSettingsSave({ settings: {} }, {}, 'C:/Synthetic/new-game', 'C:/Synthetic/old-game');
  assert(nativeRequests.slice(beforeNavigatedSave).some((request) => request.action === 'saveSettings' && request.settings.gamePath === 'C:/Synthetic/new-game'), 'The authorized game folder save finishes after leaving Settings');
  actionFixtures.delete('saveSettings');
  context.document.querySelector = originalAuditQuery;

  console.log(
    'UI smoke passed: metadata-only installation record review, reviewed local imports/adoption, exact-file external removal, local/Nexus separation, dark/light/automatic appearance, safe native palette integration, automatic local mod symbols, bounded interface recovery, explicit queued restarts, ten views including Saves, actionable diagnostics/explicit repairs, setup prompts/notice dedupe, catalog safety, scaling/scrolling, account tiers, portable sessions/recovery, browser SSO, folders, seasonal effects and live updates.'
  );
}

async function currentPlanWorkflows() {
  const saved = { snapshot: ui.state.snapshot, view: ui.state.view, preview: ui.state.preview,
    loading: ui.state.loading, libraryMeta: ui.state.libraryMeta, filter: ui.state.filter,
    favoritesOnly: ui.state.favoritesOnly, category: ui.state.category, search: ui.state.search };
  ui.state.preview = false;
  ui.state.loading = false;
  ui.state.busy = false;
  ui.state.view = 'overview';
  ui.state.profileSession = { active: false };
  ui.state.snapshot = ui.normalizeSnapshot(fixture);
  ui.resetNotices();
  node('#modal').close();

  const crash = { id: 'native-crash-event', logId: 'game-dump', headline: 'Days Gone crashed.',
    advice: ['Check the new mod <before reporting it>.'], kind: 'crash-summary' };
  actionFixtures.set('crashLogNotice', { notice: crash, watching: false, pending: false });
  actionFixtures.set('crashLogSummary', { id: 'game-dump', status: 'complete', headline: 'A native crash summary.', advice: ['Check your mods.'] });
  ui.state.crashWatch = { watching: false, pending: false, lastCheck: 0, checking: false };
  const beforeCrash = nativeRequests.length;
  await ui.pollCrashNotice();
  assert.equal(ui.state.noticeQueue.filter((notice) => notice.type === 'game-crash').length, 1, 'Fresh native crash queues one notice');
  await ui.pollCrashNotice();
  assert.equal(nativeRequests.slice(beforeCrash).filter((request) => request.action === 'crashLogNotice').length, 1, 'Idle crash polling is throttled');
  ui.state.crashWatch.lastCheck = 0;
  await ui.pollCrashNotice();
  assert.equal(ui.state.noticeQueue.filter((notice) => notice.type === 'game-crash').length, 1, 'Repeated native crash id is deduplicated');
  assert(ui.showNextNotice(), 'Queued crash is shown when the interface is idle');
  assert(node('#modal-content').innerHTML.includes('&lt;before reporting it&gt;'), 'Crash advice is escaped');
  await Promise.resolve();
  const crashDismiss = nativeRequests.slice(beforeCrash).find((request) => request.action === 'crashLogNoticeDismiss');
  assert.equal(crashDismiss.id, crash.id, 'Crash acknowledgement uses the native event id');
  await node('#crash-notice-summary').onclick();
  assert(nativeRequests.slice(beforeCrash).some((request) => request.action === 'crashLogSummary' && request.id === 'game-dump'), 'Crash notice opens the existing opaque log summary');
  assert(!nativeRequests.slice(beforeCrash).some((request) => request.path), 'Crash workflow never grants a renderer log path');
  node('#modal').close();
  ui.resetNotices();
  ui.state.crashWatch.lastCheck = 0;
  actionFixtures.set('crashLogNotice', { notice: null, watching: false, pending: true });
  await ui.pollCrashNotice();
  assert.equal(ui.state.noticeQueue.length, 0, 'A normal exit awaiting its reporter does not claim a crash');

  actionFixtures.set('gameBuildCheck', { status: 'changed', changed: true, build: { version: '1.7.0.0', steamBuild: '123' },
    notice: { id: 'native-build-event', message: 'Check mod loading <after the game update>.' } });
  ui.state.gameBuildChecked = 0;
  ui.state.diagnosticStale = false;
  const beforeBuild = nativeRequests.length;
  await ui.checkGameBuild();
  assert(ui.state.diagnosticStale, 'A game update invalidates the old diagnostic result');
  assert(nativeRequests.slice(beforeBuild).some((request) => request.action === 'directoriesScan'), 'Game update rechecks mod loading');
  await ui.checkGameBuild(true);
  assert.equal(nativeRequests.slice(beforeBuild).filter((request) => request.action === 'gameBuildCheck').length, 1, 'Focus requests cannot hammer game-build inspection');
  ui.state.gameBuildChecked = 0;
  await ui.checkGameBuild();
  assert.equal(ui.state.noticeQueue.filter((notice) => notice.type === 'game-updated').length, 1, 'Same game build queues only one update notice');
  assert(ui.showNextNotice(), 'Game update notice can be reviewed');
  assert(node('#modal-content').innerHTML.includes('&lt;after the game update&gt;'), 'Game update guidance is escaped');
  await Promise.resolve();
  assert(nativeRequests.slice(beforeBuild).some((request) => request.action === 'gameBuildDismiss' && request.id === 'native-build-event'), 'Build acknowledgement binds to the checked native build');
  node('#modal').close();
  ui.resetNotices();
  ui.state.gameBuildChecked = 0;
  actionFixtures.set('gameBuildCheck', { status: 'unavailable', changed: false, notice: null });
  await ui.checkGameBuild();
  assert.equal(ui.state.noticeQueue.length, 0, 'Unknown build does not invent an update');

  // A deferred popup belongs to the installation that produced its native IDs.
  const previousFixtureContext = fixture.accountContext;
  ui.state.snapshot.accountContext = 'old-installation';
  ui.state.view = 'settings';
  ui.state.crashWatch = { watching: false, pending: false, lastCheck: 0, checking: false };
  actionFixtures.set('crashLogNotice', { notice: { ...crash, id: 'old-context-crash' }, watching: false, pending: true });
  await ui.pollCrashNotice();
  assert(ui.state.noticeQueue.some((notice) => notice.type === 'game-crash'), 'Crash popup waits while Settings is open');
  ui.state.gameBuildChecked = 0;
  actionFixtures.set('gameBuildCheck', { changed: true, build: { version: 'old-context' }, notice: { id: 'old-context-build' } });
  await ui.checkGameBuild();
  fixture.accountContext = 'new-installation';
  await ui.refresh({ quiet: true });
  assert(!ui.state.noticeQueue.some((notice) => notice.type === 'game-crash' || notice.type === 'game-updated'), 'Selecting another installation drops old crash/build popups');
  assert.equal(ui.state.crashWatch.watching, false, 'New installation starts with an unobserved game process');
  assert.equal(ui.state.crashWatch.pending, false, 'Old installation exit polling does not survive selection change');
  ui.state.view = 'overview';
  ui.resetNotices();
  ui.state.snapshot.accountContext = 'reply-old-installation';
  ui.state.crashWatch.lastCheck = 0;
  const lateCrash = ui.pollCrashNotice();
  ui.state.snapshot.accountContext = 'reply-new-installation';
  await lateCrash;
  assert(!ui.state.noticeQueue.some((notice) => notice.type === 'game-crash'), 'Late crash reply cannot enqueue a popup for the newly selected installation');
  ui.state.gameBuildChecked = 0;
  ui.state.snapshot.accountContext = 'build-old-installation';
  const beforeLateBuild = nativeRequests.length;
  const lateBuild = ui.checkGameBuild();
  ui.state.snapshot.accountContext = 'build-new-installation';
  await lateBuild;
  assert(!ui.state.noticeQueue.some((notice) => notice.type === 'game-updated'), 'Late build reply cannot announce another installation update');
  assert(!nativeRequests.slice(beforeLateBuild).some((request) => request.action === 'directoriesScan'), 'Old build reply does not recheck a different installation loading setup');
  if (previousFixtureContext === undefined) delete fixture.accountContext;
  else fixture.accountContext = previousFixtureContext;
  ui.state.snapshot = ui.normalizeSnapshot(fixture);
  ui.resetNotices();

  const backupData = { available: true, settings: { automatic: true, keep: 5 }, recovery: { pending: false },
    backups: [{ id: 'native-save-id', createdAt: 1792502768, files: 3, bytes: 120, reason: 'Before <changing mods>' }] };
  actionFixtures.set('saveBackupsList', backupData);
  actionFixtures.set('saveBackupsSettings', { saved: true });
  await ui.loadSaveBackups();
  assert.equal(ui.state.saveBackups.backups[0].id, 'native-save-id');
  const backupHtml = ui.renderSaveBackupsPanel();
  assert(backupHtml.includes('aria-label="Automatic save backups"') && backupHtml.includes('aria-checked="true"'), 'Automatic backup switch exposes its name and state');
  assert(backupHtml.includes('Before &lt;changing mods&gt;'), 'Backup labels are escaped');
  const beforeOptions = nativeRequests.length;
  await ui.saveBackupOptions(false, 10);
  const optionsRequest = nativeRequests.slice(beforeOptions).find((request) => request.action === 'saveBackupsSettings');
  assert.deepEqual({ automatic: optionsRequest.automatic, keep: optionsRequest.keep }, { automatic: false, keep: 10 }, 'Backup settings use bounded native options');
  actionFixtures.set('saveBackupReview', { token: 'native-restore-token', summary: { files: 3, bytes: 120 }, warnings: ['Review <these saves>.'] });
  actionFixtures.set('saveBackupRestore', { restored: true });
  const beforeRestore = nativeRequests.length;
  await ui.reviewSaveBackup('native-save-id');
  assert.equal(nativeRequests.slice(beforeRestore).filter((request) => request.action === 'saveBackupRestore').length, 0, 'Opening review does not restore saves');
  assert(node('#modal-content').innerHTML.includes('Review &lt;these saves&gt;'), 'Restore warning is escaped');
  await node('#confirm-action').onclick();
  const restore = nativeRequests.slice(beforeRestore).find((request) => request.action === 'saveBackupRestore');
  assert.equal(restore.token, 'native-restore-token');
  assert.equal(restore.confirmed, true);
  assert(!('path' in restore) && !('id' in restore) && !('files' in restore), 'Save restoration is native-token-only');
  actionFixtures.set('saveBackupRecoveryReview', { token: 'native-recovery-token', summary: { files: 3 }, warnings: [] });
  actionFixtures.set('saveBackupRecover', { recovered: true });
  const beforeRecovery = nativeRequests.length;
  await ui.reviewSaveBackup('', true);
  assert(!nativeRequests.slice(beforeRecovery).some((request) => request.action === 'saveBackupRecover'), 'Recovery waits for review confirmation');
  await node('#confirm-action').onclick();
  const recover = nativeRequests.slice(beforeRecovery).find((request) => request.action === 'saveBackupRecover');
  assert.equal(recover.token, 'native-recovery-token');
  assert.equal(recover.confirmed, true);
  assert(!('path' in recover) && !('files' in recover), 'Recovery cannot use caller-provided save paths');
  actionFixtures.set('saveBackupReview', { summary: { files: 3 } });
  const missingTokenStart = nativeRequests.length;
  await ui.reviewSaveBackup('native-save-id');
  assert(!nativeRequests.slice(missingTokenStart).some((request) => request.action === 'saveBackupRestore'), 'Missing native token cannot restore saves');

  ui.state.snapshot.mods = [
    { ...catalog.mods[0], id: 'variant-a', name: 'Variant A', nexusModId: 2222, nexusFileId: 3333, enabled: true, imported: true },
    { ...catalog.mods[0], id: 'variant-b', name: 'Variant B', nexusModId: 2222, nexusFileId: 3333, enabled: false, imported: false, owned: false, cached: false }
  ];
  const shared = 'From Nexus Mods:\n- A grouped download · https://www.nexusmods.com/daysgone/mods/2222?tab=files&file_id=3333\n- Same download twice · https://www.nexusmods.com/daysgone/mods/2222?tab=files&file_id=3333\nAdded from files:\n- Unknown local file';
  const checklist = ui.parseSharedModList(shared);
  assert.equal(checklist.length, 2, 'Shared links deduplicate by native Nexus file identity');
  assert.equal(checklist[0].status, 'partial', 'Having one grouped variant does not claim the whole download is installed');
  assert.equal(checklist[1].status, 'unknown', 'Unknown local entry stays a manual checklist item');
  for (const url of ['https://www.nexusmods.com.evil.invalid/daysgone/mods/2222', 'https://user:secret@www.nexusmods.com/daysgone/mods/2222', 'http://www.nexusmods.com/daysgone/mods/2222', 'https://www.nexusmods.com/skyrim/mods/2222', 'https://www.nexusmods.com/daysgone/mods/2222?key=secret', 'https://www.nexusmods.com/daysgone/mods/2222#payload', 'https://www.nexusmods.com/daysgone/mods/2222?file_id=3333&file_id=4444'])
    assert.equal(ui.parseSharedModList('- Hostile link · ' + url)[0].page, null, 'Untrusted pasted URL never becomes a browser target');
  assert.throws(() => ui.parseSharedModList('x'.repeat(131073)), /128 KB/);
  assert.throws(() => ui.parseSharedModList(Array(1001).fill('line').join('\n')), /too many lines/);
  const beforeChecklist = nativeRequests.length;
  ui.openSharedModList();
  node('#shared-mod-list').value = shared + '\n- <img src=x onerror=attack()>';
  node('#shared-list-check').onclick();
  assert(node('#modal-content').innerHTML.includes('Check variants') && node('#modal-content').innerHTML.includes('&lt;img'), 'Checklist is escaped and labels partial variant groups');
  assert(!nativeRequests.slice(beforeChecklist).some((request) => ['enable', 'keepOnlyMod', 'nexusDownload', 'confirmReviewedImport'].includes(request.action)), 'Shared checklist never enables, downloads or imports mods');
  node('#modal').close();

  const notes = ui.updateReleaseNotes({ version: '1.0.5', releaseNotes: { version: '1.0.5', items: ['A <safe> improvement', 12, ...Array(30).fill('More changes')] } });
  assert(notes.includes('A &lt;safe&gt; improvement') && !notes.includes('<safe>'), 'Signed release-note text is escaped');
  assert.equal((notes.match(/<li>/g) || []).length, 20, 'Release-note display is bounded');
  assert.equal(ui.updateReleaseNotes({ version: '1.0.5' }), '', 'Older updates without notes remain supported');
  assert.equal(ui.updateReleaseNotes({ version: '1.0.5', releaseNotes: { version: '9.9.9', items: ['Wrong version'] } }), '', 'Release notes must match the verified offer version');

  ui.state.snapshot.mods = [
    { ...catalog.mods[0], id: 'guide-a', name: 'Guide A', enabled: true, owned: true, imported: true },
    { ...catalog.mods[1], id: 'guide-b', name: 'Guide B', enabled: true, owned: true, imported: true }
  ];
  ui.state.snapshot.conflicts = [{ a: 'guide-a', b: 'guide-b', aName: 'Guide A', bName: 'Guide B', count: 1, assets: ['sample'] }];
  ui.state.conflictGuide = { skipped: new Set(), resolved: 0 };
  ui.showConflictStep();
  assert(node('#modal-content').innerHTML.includes('data-guide-keep="guide-a"'), 'Guide presents enabled choices for the actual conflict group');
  const beforeSkip = nativeRequests.length;
  node('#conflict-guide-skip').onclick();
  assert(node('#modal-content').innerHTML.includes('Some conflicts still need attention') && ui.state.conflictGuide === null, 'Skipped guide group stays unresolved');
  assert.equal(nativeRequests.length, beforeSkip, 'Skipping a guide group changes no files');
  ui.state.snapshot.conflicts = [];
  ui.state.conflictGuide = { skipped: new Set(), resolved: 1 };
  ui.showConflictStep();
  assert(node('#modal-content').innerHTML.includes('No file conflicts remain') && node('#modal-content').innerHTML.includes('1 choice applied'), 'Guide ends only from the current conflict scan');

  ui.state.snapshot = ui.normalizeSnapshot(fixture);
  const favoriteId = ui.state.snapshot.mods[0].id;
  ui.state.snapshot.mods[0].enabled = true;
  ui.state.libraryMeta = { entries: { [favoriteId]: { favorite: true, note: 'PRIVATE-NOTE-SENTINEL <script>' } }, history: [] };
  assert.equal(ui.personalModMeta(favoriteId).favorite, true);
  ui.state.favoritesOnly = true;
  ui.state.filter = 'enabled';
  ui.state.category = 'all';
  ui.state.search = '';
  assert.equal(ui.filteredMods().length, 1, 'Favourite filter keeps only marked packages');
  assert.equal(ui.filteredMods()[0].id, favoriteId, 'Favourite filter uses stored native package identity');
  assert(ui.modListText().includes(ui.state.snapshot.mods[0].name), 'Private-note test shares the actual favourite mod');
  assert(!ui.modListText().includes('PRIVATE-NOTE-SENTINEL'), 'Shared list excludes personal notes');
  ui.showModDetails(favoriteId);
  assert(node('#mod-details-panel').innerHTML.includes('PRIVATE-NOTE-SENTINEL &lt;script&gt;'), 'Personal note renders as escaped text');
  node('#modal').close();
  actionFixtures.set('libraryMetaSave', { entries: { [favoriteId]: { favorite: false, note: 'PRIVATE-NOTE-SENTINEL' } }, history: [] });
  const beforeMeta = nativeRequests.length;
  assert(await ui.saveModMeta(favoriteId, { favorite: false, note: 'PRIVATE-NOTE-SENTINEL' }));
  const metaRequest = nativeRequests.slice(beforeMeta).find((request) => request.action === 'libraryMetaSave');
  assert.equal(metaRequest.id, favoriteId);
  assert(!('path' in metaRequest), 'Notes are keyed by native mod id, never a renderer file path');
  assert(!ui.modListText().includes('PRIVATE-NOTE-SENTINEL'), 'Saving private notes does not add them to shared lists');
  actionFixtures.set('libraryHistory', { changes: [{ at: 1792502768, added: ['<new mod>'], removed: ['old mod'] }] });
  await ui.showLibraryHistory();
  assert(node('#modal-content').innerHTML.includes('&lt;new mod&gt;') && !node('#modal-content').innerHTML.includes('PRIVATE-NOTE-SENTINEL'), 'History displays escaped mod changes without personal notes');
  node('#modal').close();
  assert.equal(node('#modal').getAttribute('aria-labelledby'), 'modal-title', 'Every dialog names its title for assistive technology');
  assert(node('#modal-content').innerHTML.includes('id="modal-title"'), 'Dialog title target exists');
  ui.confirmUpdate({ version: '1.0.5', token: 'native-update-token' });
  assert(/<progress[^>]*aria-label="[^"]+"/.test(node('#modal-content').innerHTML), 'Updater progress has an accessible name');
  node('#modal').close();
  const filterHtml = ui.renderMods();
  assert(/<button[^>]*data-filter="enabled"[^>]*aria-pressed="true"/.test(filterHtml) || /<button[^>]*aria-pressed="true"[^>]*data-filter="enabled"/.test(filterHtml), 'Selected package filter exposes its pressed state');
  assert(/data-backup-restore="native-save-id"[^>]*aria-label="[^"]+"/.test(backupHtml) || /aria-label="Restore[^"]+"[^>]*data-backup-restore="native-save-id"/.test(backupHtml), 'Each restore action includes its backup date in its accessible name');
  const main = node('#main-content'), focusInput = node('#mod-search');
  const oldContains = main.contains;
  main.contains = (target) => target === main || target === focusInput;
  focusInput.id = 'mod-search';
  focusInput.tagName = 'INPUT';
  focusInput.selectionStart = 2;
  focusInput.selectionEnd = 4;
  focusInput.setSelectionRange = (start, end) => { focusInput.selectionStart = start; focusInput.selectionEnd = end; };
  focusInput.focus();
  const focusState = ui.rememberMainFocus();
  context.document.activeElement = node('body');
  ui.restoreMainFocus(focusState);
  assert.equal(context.document.activeElement, focusInput, 'Read-only redraw restores the active input');
  assert.deepEqual([focusInput.selectionStart, focusInput.selectionEnd], [2, 4], 'Read-only redraw retains input selection');
  main.contains = oldContains;
  const beforeToastTimers = new Set(timers.keys());
  ui.toast('A keyboard-reachable action', 'success', { label: 'Undo', run() {} });
  const toast = node('temporary');
  toast.removed = false;
  context.document.activeElement = toast;
  const expiry = [...timers].find(([id, timer]) => !beforeToastTimers.has(id) && timer.delay === 10000);
  assert(expiry, 'Action toast has a bounded expiry timer');
  expiry[1].callback();
  assert(!toast.removed, 'Toast stays visible while it contains keyboard focus');
  const retry = [...timers].filter(([id, timer]) => !beforeToastTimers.has(id) && timer.delay === 1500).at(-1);
  assert(retry, 'Focused toast postpones expiry');
  context.document.activeElement = node('body');
  retry[1].callback();
  assert(toast.removed, 'Toast can expire once keyboard focus has left it');
  const duplicateReview = { token: 'native-duplicates-token', count: 2,
    groups: [{ keep: { file: 'Keep_P.pak', location: 'Saved Paks', owned: true },
      remove: [{ file: 'Extra <one>.pak', location: 'Game Paks', owned: false }, { file: 'Extra-two.pak', location: 'Saved Paks', owned: true }] }],
    skipped: [{ reason: 'Cannot establish a safe keeper' }] };
  actionFixtures.set('diagnosticsReviewDuplicates', duplicateReview);
  actionFixtures.set('diagnosticsCleanDuplicates', { completed: true, partial: false, count: 2, removed: ['opaque-one', 'opaque-two'] });
  const beforeDuplicateReview = nativeRequests.length;
  await ui.reviewDuplicateCleanup();
  const duplicateHtml = node('#modal-content').innerHTML;
  assert(duplicateHtml.includes('Keep this copy') && duplicateHtml.includes('Keep_P.pak') && duplicateHtml.includes('Extra &lt;one&gt;.pak'), 'Duplicate review names the keeper and escapes every removal candidate');
  assert(!nativeRequests.slice(beforeDuplicateReview).some((request) => request.action === 'diagnosticsCleanDuplicates'), 'Reviewing duplicates changes no files');
  node('#modal').close();
  assert(!nativeRequests.slice(beforeDuplicateReview).some((request) => request.action === 'diagnosticsCleanDuplicates'), 'Cancelling duplicate review changes no files');
  await ui.reviewDuplicateCleanup();
  await node('#duplicate-clean-confirm').onclick();
  const duplicateClean = nativeRequests.slice(beforeDuplicateReview).find((request) => request.action === 'diagnosticsCleanDuplicates');
  assert.equal(duplicateClean.token, 'native-duplicates-token');
  assert.equal(duplicateClean.confirmed, true);
  assert(!('path' in duplicateClean) && !('files' in duplicateClean) && !('groups' in duplicateClean), 'Duplicate removal consumes only the reviewed native token');
  assert(node('#modal-content').innerHTML.includes('Duplicate cleanup finished') && node('#modal-content').innerHTML.includes('2 extra files removed'), 'Completed cleanup reports the actual removal count');
  node('#modal').close();
  actionFixtures.set('diagnosticsCleanDuplicates', { partial: true, count: 1, removed: ['opaque-one'], stoppedReason: 'A later file <changed>.', needsReview: true });
  await ui.reviewDuplicateCleanup();
  await node('#duplicate-clean-confirm').onclick();
  const partialHtml = node('#modal-content').innerHTML;
  assert(partialHtml.includes('Duplicate cleanup stopped') && partialHtml.includes('1 extra file removed') && partialHtml.includes('Review duplicates again'), 'Partial cleanup reports completed work and requires another review');
  assert(partialHtml.includes('A later file &lt;changed&gt;.'), 'Partial cleanup reason is escaped');
  node('#modal').close();
  actionFixtures.set('diagnosticsCleanDuplicates', { completed: false, partial: false, count: 0, removed: [],
    stoppedReason: 'The Keep copy <changed> before removal.' });
  await ui.reviewDuplicateCleanup();
  await node('#duplicate-clean-confirm').onclick();
  const zeroRemovedHtml = node('#modal-content').innerHTML;
  assert(zeroRemovedHtml.includes('Duplicate cleanup stopped') && zeroRemovedHtml.includes('0 extra files removed'), 'Safety refusal before the first removal is not reported as completed cleanup');
  assert(zeroRemovedHtml.includes('Review duplicates again') && zeroRemovedHtml.includes('&lt;changed&gt;'), 'Any stopped cleanup requires a new review and an escaped reason');
  node('#modal').close();
  actionFixtures.set('diagnosticsReviewDuplicates', { count: 0, groups: [], skipped: [] });
  await ui.reviewDuplicateCleanup();
  assert(!node('#modal-content').innerHTML.includes('id="duplicate-clean-confirm"'), 'An empty duplicate review offers no destructive action');
  node('#modal').close();

  ui.state.snapshot = ui.normalizeSnapshot(fixture);
  const assetMod = ui.state.snapshot.mods[0];
  delete assetMod.assets;
  assetMod.assetCount = 2300;
  const nativeAssets = ['BendGame/<script>.uasset', ...Array.from({ length: 99 }, (_, index) => `BendGame/Fixture${index}.uasset`)];
  actionFixtures.set('modAssetDetails', { id: assetMod.id, assetCount: 2300, assets: nativeAssets });
  node('#detail-assets').open = false;
  const beforeAssetDetails = nativeRequests.length;
  ui.showModDetails(assetMod.id);
  assert(node('#mod-details-panel').innerHTML.includes('2300 paths'), 'Slim inventory retains the complete asset count in Details');
  assert(!nativeRequests.slice(beforeAssetDetails).some((request) => request.action === 'modAssetDetails'), 'Closed asset details do not serialize a package index');
  node('#detail-assets').open = true;
  await node('#detail-assets').ontoggle();
  const assetRequest = nativeRequests.slice(beforeAssetDetails).find((request) => request.action === 'modAssetDetails');
  assert.equal(assetRequest.id, assetMod.id);
  assert(!('path' in assetRequest) && !('assets' in assetRequest), 'Lazy index request accepts only native mod identity');
  const assetHtml = node('#detail-assets-content').innerHTML;
  assert(assetHtml.includes('BendGame/&lt;script&gt;.uasset'), 'Package asset names are escaped');
  assert.equal((assetHtml.match(/<li>/g) || []).length, 64, 'Even an oversized native preview stays bounded to 64 rows');
  await node('#detail-assets').ontoggle();
  assert.equal(nativeRequests.slice(beforeAssetDetails).filter((request) => request.action === 'modAssetDetails').length, 1, 'Reopening one Details panel reuses its fetched sample');
  node('#modal').close();
  ui.showModDetails(assetMod.id);
  const oldDetails = node('#detail-assets'), oldContent = node('#detail-assets-content');
  oldContent.innerHTML = 'Previous dialog';
  oldDetails.open = true;
  const delayedAssets = oldDetails.ontoggle();
  nodes.delete('#detail-assets');
  nodes.delete('#detail-assets-content');
  const replacementDetails = node('#detail-assets'), replacementContent = node('#detail-assets-content');
  replacementContent.innerHTML = 'New dialog stays intact';
  await delayedAssets;
  assert.equal(replacementContent.innerHTML, 'New dialog stays intact', 'Late index reply cannot replace a newer Details dialog');
  assert.equal(oldContent.innerHTML, 'Previous dialog', 'Late index reply also leaves the detached panel alone');
  replacementDetails.open = false;
  node('#modal').close();
  ui.resetNotices();
  for (const action of ['crashLogNotice', 'crashLogSummary', 'gameBuildCheck', 'saveBackupsList', 'saveBackupsSettings', 'saveBackupReview', 'saveBackupRestore', 'saveBackupRecoveryReview', 'saveBackupRecover', 'libraryMetaSave', 'libraryHistory', 'diagnosticsReviewDuplicates', 'diagnosticsCleanDuplicates', 'modAssetDetails']) actionFixtures.delete(action);
  Object.assign(ui.state, saved);
}

async function approvedUxWorkflows() {
  const saved = { ...ui.state }, originalAll = context.document.querySelectorAll;
  ui.state.preview = false; ui.state.busy = false; ui.state.profileSession = { active: false };
  ui.state.snapshot = ui.normalizeSnapshot({ ...fixture, accountContext: 'ux-test', installed: [], conflicts: [] });
  const mod = ui.state.snapshot.mods[0];
  Object.assign(mod, { imported: true, enabled: true });
  ui.state.downloads = { updates: [{ id: mod.id, updateAvailable: true, currentVersion: '1', version: '2' }] };
  let markup = ui.modStatusMarkup(mod);
  assert(markup.includes('Enabled') && markup.includes('Update available'), 'Installed activation remains visible beside an update');
  Object.assign(mod, { enabled: false });
  markup = ui.modStatusMarkup(mod);
  assert(markup.includes('Stored only') && markup.includes('Update available'), 'Stored-only activation remains visible beside an update');
  ui.state.downloads = { updates: [] };
  mod.conflictsWithInstalled = [{ id: 'other', name: '<other>' }];
  markup = ui.modStatusMarkup(mod);
  assert(markup.includes('Stored only') && markup.includes('Conflict') && markup.includes('&lt;other&gt;'), 'Conflict and activation are separate and escaped');
  ui.state.snapshot.installed = [{ name: 'Unreadable', readable: false }];
  assert(ui.renderConflicts().includes('<h3>Checks incomplete</h3>') && !ui.renderConflicts().includes('<h3>No conflicts</h3>'), 'Unreadable PAKs cannot produce a clean-scan headline');
  ui.state.snapshot.installed = []; ui.state.scanFailed = true;
  assert(ui.renderConflicts().includes('<h3>Checks incomplete</h3>'), 'A failed scan cannot headline a clean result');
  ui.state.scanFailed = false;

  node('#modal').close(); ui.closeModDetails(false);
  ui.state.filter = 'available'; ui.state.category = 'all'; ui.state.search = ''; ui.state.favoritesOnly = false;
  const sequence = ui.filteredMods();
  ui.showModDetails(sequence[0].id);
  assert.equal(node('#mod-details-panel').hidden, false, 'Details open in an independent panel');
  assert.equal(node('#modal').open, false, 'Library stays available while details are open');
  node('#mod-panel-next').onclick();
  assert.equal(ui.state.detailId, sequence[1].id, 'Next moves through the current filtered library');
  let prevented = false;
  assert(ui.handleDetailsShortcut({ key: 'Escape', preventDefault() { prevented = true; } }));
  assert(prevented && node('#mod-details-panel').hidden, 'Escape closes the nonmodal panel');

  ui.state.view = 'settings'; node('#game-path').value = 'C:/Synthetic/invalid';
  ui.showGamePathError('Choose a folder containing DaysGone.exe.', node('#game-path').value);
  assert.equal(node('#game-path').getAttribute('aria-invalid'), 'true');
  assert(node('#game-path-error').textContent.includes('DaysGone.exe'), 'Validation stays next to its field');
  node('#game-path').value = 'C:/Synthetic/new-choice'; ui.updateGamePathError();
  assert.equal(node('#game-path').getAttribute('aria-invalid'), 'false', 'Editing a rejected folder makes the new choice unverified, not invalid');
  assert(node('#game-path-error').textContent.includes('Save this folder'), 'The unresolved save remains visible after editing');
  await ui.run('savePreferences', { preferences: { theme: 'winter' } }, { refresh: false });
  assert(ui.state.gamePathError, 'Saving appearance does not silently clear a game-folder problem');
  ui.state.gamePathError = null;
  ui.state.snapshot.settings.gamePath = 'C:/Synthetic/saved';
  node('#game-path').value = 'C:/Synthetic/newer-choice';
  await ui.finishSettingsSave({ settings: {} }, {}, 'C:/Synthetic/saved', 'C:/Synthetic/saved');
  assert.equal(node('#game-path-save').hidden, false, 'Completing an earlier save keeps a newer unsaved folder choice pending');

  const items = [
    { itemId: 'opaque-a', status: 'ready', name: '<first>', source: 'mods.7z', member: 'first.pak', bytes: 50, conflicts: [{ name: '<installed>' }] },
    { itemId: 'opaque-b', status: 'ready', name: 'second', source: 'mods.rar', bytes: 80 },
    { itemId: 'opaque-c', status: 'duplicate', name: 'duplicate', bytes: 50 },
    { itemId: 'opaque-d', status: 'error', name: 'bad', message: '<bad package>' }
  ];
  const inputs = items.map((item) => ({ value: item.itemId, checked: item.status === 'ready', disabled: item.status !== 'ready' }));
  context.document.querySelectorAll = (selector) => selector === '#modal [name="batch-import-item"]' ? inputs : [];
  ui.reviewBatchImport({ batchReview: true, batchToken: 'native-batch-token', items, totalFiles: 3, duplicateCount: 1, errorCount: 1 });
  const reviewHtml = node('#modal-content').innerHTML;
  assert(reviewHtml.includes('&lt;first&gt;') && reviewHtml.includes('&lt;installed&gt;') && reviewHtml.includes('&lt;bad package&gt;'), 'Batch review escapes native metadata and shows failures with conflicts');
  node('#batch-pick-none').onclick(); assert(node('#batch-import-confirm').disabled, 'An empty batch cannot be confirmed');
  inputs[1].checked = true; inputs[1].onchange();
  actionFixtures.set('batchImportConfirm', { batchResult: true, importedCount: 1, failedCount: 0, items: [{ itemId: 'opaque-b', status: 'imported', name: 'second' }] });
  const beforeBatch = nativeRequests.length;
  await node('#batch-import-confirm').onclick();
  const confirm = nativeRequests.slice(beforeBatch).find((request) => request.action === 'batchImportConfirm');
  assert.deepEqual(Array.from(confirm.items), ['opaque-b']);
  assert.equal(confirm.batchToken, 'native-batch-token'); assert.equal(confirm.confirmed, true);
  assert(!('path' in confirm) && !('member' in confirm), 'Batch confirmation uses native-issued authority only');
  assert(!nativeRequests.slice(beforeBatch).some((request) => request.action === 'enable'), 'Batch imports never silently enable packages');
  assert(node('#modal-content').innerHTML.includes('Import results') && node('#modal-content').innerHTML.includes('Added to library'), 'Batch completion reports each result');
  actionFixtures.delete('batchImportConfirm');
  context.document.querySelectorAll = originalAll;

  ui.state.activity = { running: true, message: 'Backing up' };
  ui.updateActivity({ phase: 'Copying', file: '<private>.pak', total: 9, completed: 4 });
  assert(node('#operation-activity').innerHTML.includes('&lt;private&gt;.pak') && node('#operation-activity').innerHTML.includes('4 of 9'), 'Progress card shows actual file and count safely');
  ui.state.activity.running = false; ui.updateActivity({ message: 'late progress' });
  assert.equal(ui.state.activity.message, 'Backing up', 'Late progress cannot revive a completed operation');
  ui.handleNativeOperationProgress({ requestId: 'drop-import', phase: 'checking', completed: 0, total: 2 });
  assert(ui.state.activity.running, 'Native Linux drops can start progress without a renderer request');
  ui.handleDroppedImport({ ok: true });
  assert.equal(ui.state.activity.running, false, 'The native drop result always finishes its progress card');
  assert(ui.renderLibraryBackupPanel().includes('library-backup-export') && ui.renderLibraryBackupPanel().includes('library-backup-restore'));
  ui.confirmLibraryBackupExport();
  assert(node('#modal-content').innerHTML.includes('private ZIP') && node('#modal-content').innerHTML.includes('credentials'), 'Private library backup explains its scope before the picker');
  actionFixtures.set('browseLibraryBackupRestore', { token: 'native-backup-token', packageCount: 5, totalBytes: 90, profileCount: 2, settingsIncluded: true, notesIncluded: true });
  actionFixtures.set('libraryBackupRestore', { restored: true, message: 'Restored safely.' });
  assert(await ui.reviewLibraryBackup());
  const beforeRestore = nativeRequests.length;
  await node('#library-backup-restore-confirm').onclick();
  const restore = nativeRequests.slice(beforeRestore).find((request) => request.action === 'libraryBackupRestore');
  assert.deepEqual({ token: restore.token, confirmed: restore.confirmed }, { token: 'native-backup-token', confirmed: true });
  assert(!('path' in restore) && !nativeRequests.slice(beforeRestore).some((request) => request.action === 'enable'), 'Library restore neither trusts renderer paths nor enables restored mods');
  actionFixtures.delete('browseLibraryBackupRestore'); actionFixtures.delete('libraryBackupRestore');
  ui.state.snapshot.libraryBackupRecovery = { pending: true, reason: 'Interrupted restore.' };
  assert(ui.renderLibraryBackupPanel().includes('Review library recovery') && !ui.renderLibraryBackupPanel().includes('data-action="library-backup-restore"'), 'Pending recovery replaces normal restore controls');
  actionFixtures.set('libraryBackupRecover', { recovered: true, rolledBack: true });
  ui.reviewLibraryBackupRecovery();
  const beforeRecovery = nativeRequests.length;
  await node('#library-backup-recover-confirm').onclick();
  assert(nativeRequests.slice(beforeRecovery).some((request) => request.action === 'libraryBackupRecover' && request.confirmed === true), 'Library recovery requires its own explicit confirmation');
  actionFixtures.delete('libraryBackupRecover');
  node('#modal').close(); ui.closeModDetails(false); Object.assign(ui.state, saved);
}

async function auditUxWorkflows() {
  const saved = { ...ui.state }, originalPost = context.window.chrome.webview.postMessage,
    originalAll = context.document.querySelectorAll;
  ui.state.preview = false; ui.state.loading = false; ui.state.busy = false;
  ui.state.profileSession = { active: false }; ui.state.scanFailed = false;
  ui.state.snapshot = ui.normalizeSnapshot({ ...fixture, edition: 'local', platform: 'linux', accountContext: 'audit-old',
    recoveryRequired: false, libraryBackupRecovery: { pending: false }, installed: [], conflicts: [], diagnostics: [] });

  // A native Linux drop can be ahead of an already queued renderer operation.
  const held = [];
  context.window.chrome.webview.postMessage = (request) => { nativeRequests.push(request); held.push(request); };
  const task = ui.run('batchImportConfirm', { batchToken: 'opaque', items: ['one'], confirmed: true }, { refresh: false });
  const foreground = held.at(-1), foregroundCard = ui.state.activity;
  ui.handleNativeOperationProgress({ requestId: 'drop-import', phase: 'checking', completed: 0, total: 2, message: 'Checking dropped files' });
  const dropCard = ui.state.activity;
  assert.notEqual(dropCard, foregroundCard, 'A native drop has its own progress ownership');
  ui.handleDroppedImport({ ok: true });
  assert(ui.state.busy && foregroundCard.running && !dropCard.running, 'A drop result cannot finish another queued foreground operation');
  ui.handleNativeOperationProgress({ requestId: foreground.requestId, phase: 'importing', completed: 1, total: 2, message: 'Adding selected packages' });
  assert.equal(ui.state.activity, foregroundCard, 'Progress returns to the actual correlated foreground request');
  nativeMessageListener({ data: { requestId: foreground.requestId, ok: true, data: { batchResult: true, importedCount: 1, failedCount: 0, items: [] } } });
  await task;
  assert.equal(ui.state.activity.running, false, 'The actual foreground result finishes its visible card');
  const finished = ui.state.activity;
  ui.handleNativeOperationProgress({ requestId: foreground.requestId, phase: 'importing', completed: 1, total: 2, message: 'Late reply' });
  ui.handleNativeOperationProgress({ requestId: 'drop-import', phase: 'review', completed: 2, total: 2, message: 'Late drop reply' });
  assert.equal(ui.state.activity, finished); assert.equal(ui.state.activity.running, false, 'Late progress cannot revive a finished card');
  ui.handleNativeOperationProgress({ requestId: 'drop-import', phase: 'checking', completed: 0, total: 1 });
  const beforeBlocked = nativeRequests.length;
  assert.equal(await ui.run('disable', { id: 'one' }), null);
  assert.equal(nativeRequests.length, beforeBlocked, 'New foreground mutations wait for a native drop to finish');
  ui.handleDroppedImport({ ok: false, error: 'Synthetic failed drop' });
  context.window.chrome.webview.postMessage = originalPost;

  node('#modal').close(); ui.discardBatchReview();
  const inputs = [{ value: 'one', disabled: false, checked: true }];
  context.document.querySelectorAll = (selector) => selector === '#modal [name="batch-import-item"]' ? inputs : [];
  delete node('#batch-import-confirm').dataset.superseded;
  const review = (token) => ({ batchReview: true, batchToken: token, totalFiles: 1,
    items: [{ itemId: 'one', status: 'ready', name: 'Synthetic mod', bytes: 1 }] });
  ui.reviewBatchImport(review('audit-first'));
  const oldConfirm = node('#batch-import-confirm').onclick;
  ui.reviewBatchImport(review('audit-second'));
  inputs[0].onchange();
  assert(node('#batch-import-confirm').disabled && node('#batch-selection-status').textContent.includes('newer selection'), 'Changing checkboxes cannot re-enable a superseded review');
  const beforeOldConfirm = nativeRequests.length;
  await oldConfirm();
  assert(!nativeRequests.slice(beforeOldConfirm).some((request) => request.action === 'batchImportConfirm'), 'An invalidated batch review cannot submit');
  ui.discardBatchReview(); node('#modal').close();
  ui.reviewBatchImport(review('audit-third'));
  assert(!ui.state.noticeQueue.some((notice) => notice.type === 'batch-review'), 'A newer direct review removes older queued batch reviews');
  const password = { value: 'new-dialog-value' }, beforeLateClose = nativeRequests.length;
  context.document.querySelectorAll = (selector) => selector === 'input[type="password"]' ? [password] : [];
  ui.handleModalClosed();
  assert.equal(password.value, 'new-dialog-value');
  assert(!nativeRequests.slice(beforeLateClose).some((request) => request.action === 'batchImportDiscard'), 'A queued old close event cannot clear the current dialog or discard its review');
  context.document.querySelectorAll = originalAll;
  ui.showBatchImportResults({ importedCount: 1, failedCount: 0, items: [{ name: 'Broken source', status: 'error', message: 'Could not inspect this file.' }] });
  assert(node('#modal-content').innerHTML.includes('1 need attention') && node('#modal-content').innerHTML.includes('Could not be checked'), 'Source inspection errors remain visible in final batch results');
  const cleanupResult = { batchResult: true, importedCount: 1, failedCount: 0, attentionCount: 1, recoveryRequired: true,
    items: [{ name: 'Durably imported package', status: 'imported', needsAttention: true, message: 'Added, but interrupted cleanup needs recovery.' }] };
  actionFixtures.set('batchImportConfirm', cleanupResult);
  await ui.run('batchImportConfirm', { batchToken: 'cleanup-review', items: ['one'], confirmed: true }, { refresh: false });
  assert.equal(ui.state.activity.outcome, 'failed');
  assert(ui.state.activity.message.includes('need attention') && ui.state.activity.message.includes('recovery required'), 'A committed import with failed cleanup cannot be shown as an entirely successful operation');
  ui.showBatchImportResults(cleanupResult);
  assert(node('#modal-content').innerHTML.includes('Added · needs attention') && node('#modal-content').innerHTML.includes('batch-results-diagnostics'), 'Imported-only cleanup failures keep a visible recovery path');
  assert(!node('#modal-content').innerHTML.includes('Added · disabled'), 'Batch results do not assert a current activation state that another process may have changed');
  actionFixtures.delete('batchImportConfirm'); ui.state.snapshot.recoveryRequired = false;
  context.document.querySelectorAll = originalAll;

  // Applying a game-folder result must invalidate the previous installation's UI caches.
  const changed = { ...fixture, edition: 'local', platform: 'linux', accountContext: 'audit-new',
    libraryBackupRecovery: { pending: false }, settings: { ...fixture.settings, gamePath: '/synthetic/new-game' } };
  ui.state.libraryMeta = { entries: { privateOldContext: { note: 'old-context-note' } }, history: [] };
  ui.state.saveBackups = { backups: [{ id: 'old-backup' }] };
  ui.state.portableProfiles = { profiles: [{ id: 'old-profile' }] };
  ui.state.view = 'mods';
  ui.showModDetails(ui.state.snapshot.mods[0].id);
  actionFixtures.set('saveSettings', changed); nextSnapshotFixture = changed;
  await ui.finishSettingsSave({ settings: {} }, {}, '/synthetic/new-game', '/synthetic/old-game');
  assert.equal(ui.state.snapshot.accountContext, 'audit-new');
  assert.equal(ui.state.saveBackups, null); assert.equal(ui.state.portableProfiles, null);
  assert(!Object.keys(ui.state.libraryMeta.entries).length && node('#mod-details-panel').hidden, 'Manual installation switches discard old private caches and close stale details');
  actionFixtures.delete('saveSettings');
  ui.state.view = 'settings'; node('#game-path').value = '/synthetic/rejected';
  ui.showGamePathError('DaysGone.exe is missing.', '/synthetic/rejected');
  node('#game-path').value = '/synthetic/newer-unsaved'; ui.updateGamePathError();
  ui.state.view = 'mods';
  assert.equal(ui.gamePathFieldValue(), '/synthetic/newer-unsaved', 'A rejected folder keeps its latest editable draft when leaving Settings');
  assert(ui.renderSettings().includes('value="/synthetic/newer-unsaved"'), 'Go to setting can return to the unresolved draft instead of silently showing the old valid path');
  ui.state.gamePathError = null;

  ui.state.snapshot.installed = [{ name: 'No index proof supplied' }];
  assert(ui.renderConflicts().includes('<h3>Checks incomplete</h3>'), 'Missing readable evidence is unknown rather than a clean scan');
  ui.state.snapshot.installed = [];
  ui.state.snapshot.libraryBackupRecovery = { pending: true };
  ui.state.snapshot.recoveryRequired = true; ui.state.snapshot.sandbox = false;
  assert.equal(ui.playReadiness().action, 'library-backup-recovery', 'Library recovery readiness opens the relevant recovery flow');
  assert(ui.renderConflicts().includes('<h3>Checks incomplete</h3>'), 'Recovery-only snapshots cannot headline No conflicts');
  assert(!ui.renderSaveBackupsPanel().includes('data-action="backup-saves"'), 'Recovery cannot offer blocked save-backup actions');
  ui.state.saveBackups = null;
  const beforeRecoveryNav = nativeRequests.length;
  ui.navigate('settings'); await Promise.resolve();
  assert(!nativeRequests.slice(beforeRecoveryNav).some((request) => request.action === 'saveBackupsList'), 'Opening recovery Settings does not trigger a blocked auto-load');
  ui.state.snapshot.activePath = ''; ui.state.snapshot.platform = 'linux';
  assert(!ui.renderSettings().includes('%LOCALAPPDATA%') && !ui.renderFolders().includes('%LOCALAPPDATA%'), 'Linux without a selected prefix never displays a Windows environment path');
  assert(!ui.guideBody({ id: 'getting-started', body: '<code>%LOCALAPPDATA%\\BendGame\\Saved\\Paks</code>' }).includes('%LOCALAPPDATA%'), 'Linux help also explains an unselected prefix');

  ui.state.snapshot.libraryBackupRecovery = { pending: false }; ui.state.snapshot.recoveryRequired = false;
  const blocked = ui.state.snapshot.mods[0];
  Object.assign(blocked, { available: false, imported: true, enabled: true });
  ui.state.view = 'mods'; ui.state.filter = 'enabled'; ui.state.category = 'all'; ui.state.search = ''; ui.state.favoritesOnly = false;
  assert(ui.renderModRows().includes(`data-toggle="${blocked.id}"`), 'An already-enabled release remains disableable after its catalog blocks new installs');
  ui.showModDetails(blocked.id);
  assert(node('#mod-details-panel').innerHTML.includes('id="detail-remove"') && node('#mod-details-panel').innerHTML.includes('id="detail-toggle"'), 'Owned blocked releases still have guarded disable/remove actions');
  const beforeDisable = nativeRequests.length;
  await ui.toggleMod(blocked.id);
  assert(nativeRequests.slice(beforeDisable).some((request) => request.action === 'disable') && !nativeRequests.slice(beforeDisable).some((request) => request.action === 'enable'), 'Blocked status never prevents safe Disable or allows Enable');
  ui.closeModDetails(false); node('#modal').close(); ui.discardBatchReview();
  Object.assign(ui.state, saved);
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
