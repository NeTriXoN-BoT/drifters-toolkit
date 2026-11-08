(() => {
  'use strict';
  const $ = (s, root = document) => root.querySelector(s);
  const $$ = (s, root = document) => [...root.querySelectorAll(s)];
  const escape = (value) =>
    String(value ?? '').replace(
      /[&<>"']/g,
      (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]
    );
  const iconPaths = {
    overview:
      '<rect x="3" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="3" width="7" height="7" rx="1.5"/><rect x="3" y="14" width="7" height="7" rx="1.5"/><rect x="14" y="14" width="7" height="7" rx="1.5"/>',
    grid: '<rect x="3" y="3" width="18" height="18" rx="2"/><path d="M3 9h18M9 9v12"/>',
    shield: '<path d="m12 3 8 3v6c0 5-8 9-8 9s-8-4-8-9V6l8-3Z"/><path d="m8 12 3 3 5-6"/>',
    layers: '<path d="m12 3 10 6-10 6L2 9l10-6ZM3 14l9 5 9-5M3 18l9 5 9-5"/>',
    book: '<path d="M12 5v16M12 5C9 3 5 3 2 4v15c3-1 7-1 10 2 3-3 7-3 10-2V4c-3-1-7-1-10 1Z"/>',
    settings:
      '<path d="m10 3-.7 2.4-2 .9L5 5.4 2.7 9.3l1.8 1.8v2l-1.8 1.8L5 18.8l2.3-.9 2 .9L10 21h4l.7-2.2 2-.9 2.3.9 2.3-3.9-1.8-1.8v-2l1.8-1.8L19 5.4l-2.3.9-2-.9L14 3h-4Z"/><circle cx="12" cy="12" r="3"/>',
    external: '<path d="M14 3h7v7M21 3l-10 10M10 3H5a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h14a2 2 0 0 0 2-2v-5"/>',
    refresh: '<path d="M20 7v5h-5M4 17v-5h5"/><path d="M5 8a7 7 0 0 1 12-3l3 3M4 16l3 3a7 7 0 0 0 12-3"/>',
    plus: '<path d="M12 4v16M4 12h16"/>',
    folder: '<path d="M3 6a2 2 0 0 1 2-2h5l2 3h7a2 2 0 0 1 2 2v10a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V6Z"/>',
    play: '<path d="m9 4 12 8-12 8V4Z"/>',
    search: '<circle cx="10" cy="10" r="6.5"/><path d="m15 15 6 6"/>',
    arrow: '<path d="M4 12h16m-6-6 6 6-6 6"/>',
    back: '<path d="M20 12H4m6-6-6 6 6 6"/>',
    check: '<path d="m5 12 5 5L20 7"/>',
    alert: '<path d="m12 3 10 18H2L12 3Z"/><path d="M12 9v5M12 17v.2"/>',
    error: '<path d="M8 3h8l5 5v8l-5 5H8l-5-5V8l5-5Z"/><path d="M12 8v5M12 16v.2"/>',
    unknown: '<circle cx="12" cy="12" r="9"/><path d="M9.5 9.5a2.5 2.5 0 1 1 3.4 2.3c-.6.3-.9.8-.9 1.4v.6M12 17v.2"/>',
    info: '<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7v.2"/>',
    close: '<path d="m6 6 12 12M6 18 18 6"/>',
    more: '<circle cx="5" cy="12" r="1"/><circle cx="12" cy="12" r="1"/><circle cx="19" cy="12" r="1"/>',
    crosshair: '<circle cx="12" cy="12" r="6"/><path d="M12 2v6M12 16v6M2 12h6M16 12h6"/>',
    bolt: '<path d="m13 2-9 12h7l-1 8 10-13h-7l1-7Z"/>',
    wrench: '<path d="M14 6a5 5 0 0 0-6 6l-5 5a2.8 2.8 0 0 0 4 4l5-5a5 5 0 0 0 6-6l-3 3-4-4 3-3Z"/>',
    compass: '<circle cx="12" cy="12" r="9"/><path d="m16 8-3 5-5 3 3-5 5-3Z"/>',
    lock: '<rect x="4" y="10" width="16" height="11" rx="2"/><path d="M8 10V7a4 4 0 0 1 8 0v3M12 14v3"/>',
    trash: '<path d="M3 6h18M8 6V3h8v3M6 6l1 15h10l1-15M10 10v7M14 10v7"/>',
    download: '<path d="M12 3v12m-5-5 5 5 5-5M4 16v5h16v-5"/>',
    report: '<path d="M14 3H5v18h14V8l-5-5ZM14 3v5h5M8 12h8M8 16h6"/>',
    clock: '<circle cx="12" cy="12" r="9"/><path d="M12 6v6l4 2"/>',
    cube: '<path d="m12 2 9 5v10l-9 5-9-5V7l9-5ZM3 7l9 5 9-5M12 12v10M7 4.8l9 5"/>',
    moon: '<path d="M20.5 14A9 9 0 0 1 10 3.5 9 9 0 1 0 20.5 14Z"/>',
    target: '<circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="5"/><circle cx="12" cy="12" r="1"/>',
    save: '<path d="M4 3h13l4 4v14H3V3h1ZM7 3v6h10V3M7 21v-8h10v8"/>',
    camera: '<path d="M8 4h8l2 3h3v14H3V7h3l2-3Z"/><circle cx="12" cy="13" r="4"/>',
    user: '<circle cx="12" cy="8" r="4"/><path d="M4 21v-2a8 8 0 0 1 16 0v2"/>'
  };
  const icon = (name) =>
    `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${iconPaths[name] || iconPaths.cube}</svg>`;

  let interfaceFaultCount = 0,
    interfaceFaultHandling = false,
    interfaceRecoveryRunning = false;
  function handleInterfaceFault(event) {
    if (event?.type === 'error' && event.target && event.target !== window) return false;
    try {
      event?.preventDefault?.();
      if (interfaceFaultHandling) return false;
      interfaceFaultHandling = true;
      interfaceFaultCount = Math.min(3, interfaceFaultCount + 1);
      const panel = $('#interface-recovery'),
        message = $('#interface-recovery-message');
      if (!panel || !message) return false;
      message.textContent =
        interfaceFaultCount >= 3
          ? 'The interface keeps hitting an error. Wait for any current file operation to finish, then restart the toolkit.'
          : 'The interface hit an error. Refresh this view to read the current state. A file operation already in progress may still finish.';
      panel.hidden = false;
      return true;
    } catch {
      return false;
    } finally {
      interfaceFaultHandling = false;
    }
  }
  function handleInterfaceRecovered() {
    try {
      $('#interface-recovery-message').textContent =
        'The interface restarted and read your current library. Earlier file actions were not repeated. Refresh this view if its status has changed.';
      $('#interface-recovery').hidden = false;
    } catch {
      /* Native state remains available even if the recovery banner cannot render. */
    }
  }
  async function recoverInterfaceView() {
    if (interfaceRecoveryRunning) return false;
    const message = $('#interface-recovery-message'),
      control = $('#interface-recovery-refresh');
    if (state.busy || state.nativeDropActivity?.running || pending.size) {
      if (message)
        message.textContent =
          'Wait for the current request to finish before refreshing. This avoids interrupting its confirmation or progress.';
      return false;
    }
    interfaceRecoveryRunning = true;
    if (control) control.disabled = true;
    try {
      const ok = await refresh({ quiet: true, preserveForm: true, background: true });
      if (ok) $('#interface-recovery').hidden = true;
      else if (message)
        message.textContent =
          'The view could not be refreshed. Your saved settings are still stored. Try again after the current operation finishes, or restart the toolkit.';
      return ok;
    } catch {
      if (message)
        message.textContent =
          'The interface could not be recovered. Wait for any current file operation to finish before restarting the toolkit.';
      return false;
    } finally {
      interfaceRecoveryRunning = false;
      if (control) control.disabled = false;
    }
  }
  function populateIcons(root = document) {
    $$('[data-icon]', root).forEach((el) => (el.innerHTML = icon(el.dataset.icon)));
  }
  const state = {
    storage: null,
    storageLoading: false,
    workshop: null,
    workshopLoaded: false,
    workshopAsset: null,
    workshopSelection: '',
    workshopSearch: '',
    workshopChoicesOnly: false,
    workshopPropertySearch: '',
    workshopError: '',
    workshopWorking: false,
    workshopLoading: false,
    workshopProjectsLoading: false,
    view: 'overview',
    snapshot: {
      mods: [],
      installed: [],
      conflicts: [],
      settings: {
        colorMode: 'auto',
        useSystemTheme: false,
        seasonalThemes: true,
        seasonalEffects: true,
        theme: 'auto',
        reducedMotion: false,
        fontScale: 100,
        interfaceScale: 100,
        autoScale: true,
        highContrast: false,
        autoCheckUpdates: true,
        autoCheckToolkitUpdates: true
      },
      profiles: [],
      diagnostics: []
    },
    conflictLimit: 50,
    profileSearch: '',
    profileSort: ['name', 'mods', 'size'].includes(storedValue('drifter.profileSort', 'name'))
      ? storedValue('drifter.profileSort', 'name')
      : 'name',
    openProfiles: new Set(),
    modLimit: 50,
    downloadLimit: 50,
    discoveryLimit: 50,
    filter: 'available',
    filterChosen: false,
    category: 'all',
    characterAppearanceView: false,
    characterLibraryReturn: null,
    sort: readStoredSort(),
    search: '',
    helpSearch: '',
    helpSection: 'help',
    settingsSearch: '',
    settingsSection: 'all',
    gamePathError: null,
    activity: null,
    nativeDropActivity: null,
    detailId: null,
    article: null,
    loading: true,
    preview: !window.chrome?.webview,
    nexus: { connected: false, premium: false },
    nxm: { registered: false, available: false, reason: 'Handler status has not been checked.' },
    downloads: { files: [], updates: [] },
    directories: null,
    directoriesLoading: false,
    portableProfiles: null,
    profileSession: { active: false, status: 'idle' },
    gameRestart: { pending: false, phase: 'idle' },
    profilesLoading: false,
    diagnosticReport: null,
    diagnosticStale: false,
    nativeWindowHidden: false,
    diagnosticsLoading: false,
    crashLogs: null,
    crashLogsLoading: false,
    saveBackups: null,
    saveBackupsLoading: false,
    savePictures: new Map(),
    saveSelection: null,
    saveOptionsOpen: false,
    saveRestoreReview: null,
    saveDeleteReview: null,
    currentSaveReplaceReview: null,
    saveEditor: null,
    saveDetailsDrafts: new Map(),
    saveProfileFilter: null,
    savesTab: 'current',
    currentSaves: null,
    currentSavesLoading: false,
    currentSaveSelection: null,
    currentSavePreviews: new Map(),
    crashWatch: { watching: false, pending: false, lastCheck: 0, checking: false },
    gameBuild: null,
    gameBuildChecked: 0,
    libraryMeta: { entries: {}, history: [] },
    libraryMetaLoaded: false,
    libraryMetaLoading: false,
    libraryHistory: [],
    favoritesOnly: false,
    conflictGuide: null,
    selectedCrashLogs: new Set(),
    noticeQueue: [],
    issues: [],
    setupIssues: [],
    update: { status: null, offer: null, lastCheck: 0, checking: false },
    toolkitRelease: { status: null, checking: false, nextCheckAt: 0,
      dismissedVersion: storedValue('drifter.toolkitUpdateDismissed', '') },
    catalogUpdate: { status: null, offer: null, lastCheck: 0, checking: false },
    setupPrompted: false,
    scanFailed: false,
    busy: false
  };
  const pending = new Map();
  const activityRequests = new Map();
  const recentActivity = [];
  const completedActivities = new WeakSet();
  const RecentActivityLimit = 50;
  const requestSession =
    window.crypto?.randomUUID?.() || `${Date.now().toString(36)}-${Math.random().toString(36).slice(2)}`;
  let requestNumber = 0;
  let appearancePreview = null,
    sandboxSmokeAppearance = null;
  const seenNoticeFingerprints = new Set();
  let pendingPackageReview = null;
  let activeIssueSummary = false;
  let downloadDiscoveryTimer = null;
  const intentionallyRemovedDownloadHashes = new Set();
  let modalContentGeneration = 0;
  const shownIssueKeys = new Set();
  let noticeDeliveryTimer = null;
  let filesystemDirty = false,
    filesystemRefreshTimer = null,
    filesystemRefreshRunning = false;
  const interfacePresets = [80, 90, 100, 110, 125, 150, 175, 200];
  function errorText(err) {
    return typeof err === 'string'
      ? err
      : err?.message || err?.error || 'The request failed without an error message. Run diagnostics before retrying.';
  }
  function request(action, args = {}, objects = null, waitForCompletion = false, activity = null) {
    if (state.preview) return previewRequest(action, args);
    const requestId = `${requestSession}:${++requestNumber}`;
    if (activity) { activity.requestId = requestId; activityRequests.set(requestId, activity); }
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        if (!pending.has(requestId)) return;
        if (waitForCompletion) {
          // Copying or a native picker can outlast this warning. Keep the
          // promise and foreground lock until its actual result arrives.
          setStatus('Still working. Keep the toolkit open until this finishes.');
          return;
        }
        pending.delete(requestId);
        reject(new Error('The toolkit did not finish this request. Check Diagnostics before trying again.'));
      }, 120000);
      pending.set(requestId, { resolve, reject, timer });
      try {
        if (objects?.length && typeof window.chrome.webview.postMessageWithAdditionalObjects === 'function')
          window.chrome.webview.postMessageWithAdditionalObjects({ ...args, requestId, action }, objects);
        else window.chrome.webview.postMessage({ ...args, requestId, action });
      } catch (error) {
        pending.delete(requestId);
        clearTimeout(timer);
        reject(error);
      }
    });
  }
  if (!state.preview)
    window.chrome.webview.addEventListener('message', (event) => {
      let result = event.data;
      if (typeof result === 'string') {
        try {
          result = JSON.parse(result);
        } catch {
          return;
        }
      }
      if (!result) return;
      if (result.event === 'nexusStatusChanged') {
        handleNexusStatusChanged(result);
        return;
      }
      if (result.event === 'windowVisibilityChanged') {
        handleWindowVisibility(result);
        return;
      }
      if (result.event === 'filesDropped') {
        importDroppedFile(result.files, result.dropToken);
        return;
      }
      if (result.event === 'droppedImport') {
        handleDroppedImport(result);
        return;
      }
      if (result.event === 'displayMetricsChanged') {
        handleDisplayMetrics(result);
        return;
      }
      if (result.event === 'systemAppearanceChanged') {
        handleSystemAppearance(result);
        return;
      }
      if (result.event === 'gameProcessChanged') {
        handleGameProcessChanged(result);
        return;
      }
      if (result.event === 'gameRestartChanged') {
        handleGameRestartEvent(result);
        return;
      }
      if (result.event === 'interfaceRecovered') {
        handleInterfaceRecovered();
        return;
      }
      if (result.event === 'updateFinished') {
        const version = (value) => (typeof value === 'string' && /^\d+\.\d+\.\d+$/.test(value) ? value : '');
        const to = version(result.data?.to);
        if (to) showUpdated(version(result.data?.from), to);
        return;
      }
      if (result.event === 'updateProgress') {
        handleUpdateProgress(result.data);
        return;
      }
      if (result.event === 'operationProgress') {
        handleNativeOperationProgress(result.data);
        return;
      }
      if (result.event === 'profileSessionChanged') {
        handleProfileSessionEvent(result);
        return;
      }
      if (result.event === 'nxmDownloadResult') {
        handleNativeDownloadResult(result);
        return;
      }
      if (result.event === 'filesystemChanged') {
        scheduleFilesystemRefresh();
        return;
      }
      const p = pending.get(String(result.requestId));
      if (!p) return;
      pending.delete(String(result.requestId));
      clearTimeout(p.timer);
      if (result.ok === false) p.reject(new Error(errorText(result.error)));
      else p.resolve(result.data ?? result.result ?? result);
    });
  async function previewRequest(action) {
    if (action === 'snapshot') {
      let raw;
      try {
        const res = await fetch('../catalog/catalog.json');
        if (!res.ok) throw new Error();
        raw = await res.json();
      } catch {
        raw = { mods: [] };
      }
      const mods = Array.isArray(raw) ? raw : raw.mods || raw.catalog || [];
      return {
        ...state.snapshot,
        mods: mods.map((m) => ({ ...m, imported: false, enabled: false, status: 'available' })),
        installed: [],
        conflicts: [],
        diagnostics: []
      };
    }
    if (action === 'directoriesScan')
      return {
        installations: [],
        locations: [],
        duplicates: [],
        startup: {
          status: 'preview',
          canPrepare: false,
          canRestore: false,
          message:
            'The desktop application checks your actual game folders and startup package. No directories have been scanned in this preview.'
        }
      };
    if (action === 'diagnosticsRun')
      return {
        checks: [],
        summary: { errors: 0, warnings: 0, passed: 0, unknown: 0 },
        checkedAt: null,
        scope: 'preview'
      };
    if (action === 'profilesList') return { profiles: [], session: { active: false, status: 'preview' } };
    if (action === 'profileSessionStatus') return { active: false, status: 'preview' };
    if (action === 'nxmStatus')
      return {
        registered: false,
        available: false,
        reason: 'Browser download integration is available in the desktop application.'
      };
    if (action === 'nexusStatus') return { connected: false, premium: false };
    if (action === 'nexusPreviewImage' || action === 'nexusAuthorAvatar') return { available: false };
    if (action === 'crashLogsList')
      return { logs: [], status: 'preview', scope: 'Log discovery needs the desktop application.', warnings: [] };
    if (action === 'openAuthorProfile') {
      window.open('https://www.nexusmods.com/daysgone/users/62048576', '_blank', 'noopener,noreferrer');
      return {};
    }
    if (action === 'openNexus') {
      window.open('https://www.nexusmods.com/daysgone/mods/994', '_blank', 'noopener,noreferrer');
      return {};
    }
    throw new Error(
      'This is the interface preview. Open the desktop application to manage packages and connect to Nexus Mods.'
    );
  }
  function normalizeSnapshot(data) {
    const source = data?.snapshot || data || {};
    let mods = source.mods || [];
    if (!Array.isArray(mods)) mods = Object.values(mods);
    return {
      ...state.snapshot,
      ...source,
      mods: withCatalogNames(
        mods.map((m, i) => ({
          ...m,
          id: String(m.id ?? m.slug ?? i),
          name: m.name || m.title || 'Unnamed package',
          summary: m.summary || m.description || '',
          imported: !!(m.imported || m.managed || m.cached),
          enabled: !!m.enabled
        }))
      ),
      installed: source.installed || [],
      conflicts: source.conflicts || [],
      profiles: source.profiles || [],
      diagnostics: source.diagnostics || [],
      settings: {
        ...state.snapshot.settings,
        ...source.settings,
        ...(window.__DRIFTER_SANDBOX_SMOKE__ && source.sandbox ? sandboxSmokeAppearance || {} : {})
      }
    };
  }
  // "BikeCrashesHurtMore_P" -> "Bike Crashes Hurt More", for PAKs added before the toolkit named them this way.
  function readableModName(name) {
    const text = String(name || '');
    if (/\s/.test(text) || !/^[A-Za-z0-9_-]+$/.test(text)) return text;
    const words = text
      .replace(/_P$/i, '')
      .replace(/^(?:Z{2,}[-_]?|Z[-_])+/i, '')
      .replace(/[_-]+/g, ' ')
      .replace(/([a-z])([A-Z])/g, '$1 $2')
      .replace(/([A-Z]+)([A-Z][a-z])/g, '$1 $2')
      .replace(/([A-Za-z])(\d)/g, '$1 $2')
      .replace(/\s+/g, ' ')
      .trim();
    return words || text;
  }
  // A file added by hand that is exactly a catalog release shows as that release (name, version, Nexus link), and the
  // catalog entry leaves the Available list because the person already has it.
  function withCatalogNames(mods) {
    const bySha = new Map();
    for (const m of mods) if (!isLocalMod(m) && m.sha256 && !bySha.has(m.sha256)) bySha.set(m.sha256, m);
    return mods.map((m) => {
      if (!isLocalMod(m)) return m;
      const known = m.sha256 && bySha.get(m.sha256);
      if (!known) return { ...m, name: readableModName(m.name) };
      known.heldLocally = m.id;
      return {
        ...m,
        catalogMatch: known.id,
        name: known.name,
        category: known.category || m.category,
        summary: known.summary || m.summary,
        description: known.description || m.description,
        version: known.version,
        icon: known.icon,
        download: known.download,
        nexusModId: known.nexusModId,
        nexusFileId: known.nexusFileId
      };
    });
  }
  function isLocalMod(m) {
    return m?.identityStatus === 'local-import' || m?.provenance?.kind === 'local-import' || m?.localManaged === true;
  }
  function libraryMods() {
    return state.snapshot.mods.filter((m) =>
      (!isLocalMod(m) || m.imported || m.cached || m.owned || m.enabled) &&
      (!latestCatalogRelease(m) || hasMod(m))
    );
  }
  // A mod someone has: imported into the library, switched on, or installed by the toolkit.
  function hasMod(m) {
    return !!(m.imported || m.enabled || m.owned || m.heldLocally);
  }
  function personalModMeta(id) {
    return Object.hasOwn(state.libraryMeta?.entries || {}, id) ? state.libraryMeta.entries[id] : { favorite: false, note: '' };
  }
  async function loadLibraryMeta() {
    if (state.preview || state.libraryMetaLoading) return;
    const context = state.snapshot.accountContext;
    state.libraryMetaLoading = true;
    try {
      const result = await request('libraryMetaList');
      if (context !== state.snapshot.accountContext) return;
      if (!result?.entries || typeof result.entries !== 'object' || Array.isArray(result.entries)) throw new Error();
      state.libraryMeta = result;
      state.libraryMetaLoaded = true;
    } catch { if (context === state.snapshot.accountContext) state.libraryMetaLoaded = false; }
    finally { if (context === state.snapshot.accountContext) state.libraryMetaLoading = false; }
    if (state.view === 'mods') render();
  }
  function renderPersonalModNotes(mod) {
    const personal = personalModMeta(mod.id), disabled = !state.libraryMetaLoaded && !state.preview;
    return `<h3>Your notes</h3><p class="field-note">Saved on this computer. These stay out of shared lists and support reports.</p><button type="button" class="button small" id="detail-favorite" aria-pressed="${personal.favorite === true}" ${disabled ? 'disabled' : ''}>${personal.favorite ? 'Remove favourite' : 'Add favourite'}</button><label class="field-label" for="detail-personal-note">Note for ${escape(mod.name)}</label><textarea class="input full-width" id="detail-personal-note" rows="3" maxlength="4000" ${disabled ? 'disabled' : ''}>${escape(personal.note || '')}</textarea><div class="inline-actions"><button type="button" class="button small" id="detail-note-save" ${disabled ? 'disabled' : ''}>Save note</button><small id="detail-note-status" role="status">${disabled ? 'Notes are not loaded yet. Reopen Details to retry.' : ''}</small></div>`;
  }
  function bindPersonalModNotes(id) {
    const favorite = $('#detail-favorite'), save = $('#detail-note-save');
    if (favorite) favorite.onclick = () => saveModMeta(id, { favorite: !personalModMeta(id).favorite });
    if (save) save.onclick = () => saveModMeta(id, { note: $('#detail-personal-note').value });
  }
  async function saveModMeta(id, change) {
    const context = state.snapshot.accountContext;
    const result = await run('libraryMetaSave', { id, ...change }, { refresh: false, progress: 'Saving your mod notes…' });
    if (context !== state.snapshot.accountContext) return false;
    if (result) {
      state.libraryMeta = result;
      render();
      const favorite = $('#detail-favorite');
      if (favorite) {
        favorite.setAttribute('aria-pressed', String(personalModMeta(id).favorite));
        favorite.textContent = personalModMeta(id).favorite ? 'Remove favourite' : 'Add favourite';
      }
      const note = $('#detail-note-status');
      if (note) note.textContent = 'Saved on this computer.';
    }
    return !!result;
  }
  function openCharacterAppearance() {
    if (!state.characterAppearanceView) state.characterLibraryReturn = {
      filter: state.filter === 'disabled' ? 'stored' : state.filter,
      category: state.category, search: state.search, favoritesOnly: state.favoritesOnly
    };
    state.characterAppearanceView = true; state.filter = 'all'; state.filterChosen = true;
    state.category = 'Character appearance'; state.search = ''; state.favoritesOnly = false; state.modLimit = 50;
    navigate('mods', { characterAppearance: true });
  }
  function leaveCharacterAppearance() {
    if (!state.characterAppearanceView) return;
    const previous = state.characterLibraryReturn;
    if (previous) Object.assign(state, previous);
    else { state.category = 'all'; state.filter = enabledMods().length ? 'enabled' : 'available'; }
    if (state.filter === 'disabled') state.filter = 'stored';
    state.characterAppearanceView = false; state.characterLibraryReturn = null; state.modLimit = 50;
  }
  function openLibraryTools() {
    const installed = enabledMods().length, stored = storedMods().length;
    modal('Library tools', `<p>${installed} enabled · ${stored} stored only. Stored only includes every inactive local library package. Only enabled mods load in the game.</p><div class="library-tool-groups"><section><h3>Work on local mods</h3><p class="field-note">Workshop edits supported properties or prepares a separate combined PAK. Character appearance brings skins, outfits and their supplied previews together.</p><div class="library-tool-list"><button type="button" class="button" id="library-tool-workshop">${icon('wrench')}Open workshop</button><button type="button" class="button" id="library-tool-characters">${icon('grid')}Character appearance</button></div></section><section><h3>Share or compare a setup</h3><p class="field-note">Share a text list with links, or compare someone else’s list with yours. No mod files or private paths are shared.</p><div class="library-tool-list"><button class="button" id="library-tool-share">${icon('report')}Share my mod list</button><button class="button" id="library-tool-open">${icon('search')}Compare a shared mod list</button></div></section><section><h3>Back up your library</h3><p class="field-note">Keep the actual PAK files, profiles and personal notes in a private ZIP. Game saves have their own backups.</p><div class="library-tool-list"><button type="button" class="button" id="library-tool-backup">${icon('save')}Back up library…</button><button type="button" class="button" id="library-tool-restore">${icon('folder')}Restore a library backup…</button></div></section><section><h3>Find a mod or a recent change</h3><p class="field-note">Favourites filter the collection. Recent changes show which managed mods were switched on or off.</p><div class="library-tool-list"><button class="button" id="library-tool-favorites">${icon('search')}${state.favoritesOnly ? 'Show all mods' : 'Show favourites only'}</button><button class="button" id="library-tool-history">${icon('refresh')}Recently changed</button></div></section></div>`, '<button class="button" data-modal-close>Close</button>');
    $('#library-tool-workshop').onclick = () => { $('#modal').close(); navigate('workshop'); };
    $('#library-tool-characters').onclick = () => { $('#modal').close(); openCharacterAppearance(); };
    $('#library-tool-share').onclick = shareModList;
    $('#library-tool-open').onclick = openSharedModList;
    $('#library-tool-backup').onclick = () => { $('#modal').close(); confirmLibraryBackupExport(); };
    $('#library-tool-restore').onclick = () => { $('#modal').close(); reviewLibraryBackup(); };
    $('#library-tool-favorites').onclick = () => { state.favoritesOnly = !state.favoritesOnly; state.view = 'mods'; $('#modal').close(); render(); };
    $('#library-tool-history').onclick = showLibraryHistory;
  }
  async function showLibraryHistory() {
    const result = await run('libraryHistory', {}, { refresh: false, progress: 'Reading recent mod changes…' });
    if (!result) return;
    state.libraryHistory = Array.isArray(result.changes) ? result.changes : [];
    const changes = [...state.libraryHistory].reverse();
    modal('Recently changed', `<p>Recorded changes to the active PAK collection. Personal notes and favourites stay separate and are not included in shared lists or support reports.</p>${changes.length ? `<ul class="library-history">${changes.map((change) => `<li><strong>${escape(logTime(change.at))}</strong>${change.added?.length ? `<p>Switched on: ${change.added.map((name) => escape(name)).join(', ')}</p>` : ''}${change.removed?.length ? `<p>Switched off: ${change.removed.map((name) => escape(name)).join(', ')}</p>` : ''}</li>`).join('')}</ul>` : '<p>No changes have been recorded yet.</p>'}`, '<button class="button" data-modal-close>Close</button>');
  }
  function enabledMods() {
    return state.snapshot.mods.filter((m) => m.enabled);
  }
  function importedMods() {
    return state.snapshot.mods.filter((m) => m.imported);
  }
  function storedMods() {
    return libraryMods().filter((m) => m.imported && !m.enabled && !brokenInstallationRecord(m));
  }
  function activationLabel(mod) {
    return brokenInstallationRecord(mod) ? 'State unverified' : mod.enabled ? 'Enabled' : 'Stored only';
  }
  function externalMods() {
    return state.snapshot.installed.filter(
      (m) => m.external || m.owned === false || m.managed === false || m.source === 'external' || !m.id
    );
  }
  function brokenInstallationRecord(m) {
    return !!m?.owned && (m.status === 'changed' || m.status === 'missing');
  }
  function installationRecordReason(m) {
    return externalChangeReason(m) || m?.forgetInstallationReason || '';
  }
  function getMod(id) {
    return state.snapshot.mods.find((m) => m.id === String(id));
  }
  function modName(value) {
    return (
      getMod(typeof value === 'object' ? value?.id : value)?.name ||
      state.snapshot.installed.find((m) => m.id === value)?.name ||
      (typeof value === 'object' ? value?.name || value?.path : value) ||
      'Unknown package'
    );
  }
  function conflictIds(c) {
    return [c.a?.id ?? c.a, c.b?.id ?? c.b].map(String);
  }
  // Mods that overlap each other, directly or through a shared partner, form one group: "these mods change
  // the same game files". Used by the Conflicts page and the scan pop-up instead of one entry per pair.
  function conflictGroups(conflicts) {
    const parent = new Map();
    const find = (id) => {
      while (parent.get(id) !== id) id = parent.get(id);
      return id;
    };
    for (const conflict of conflicts) {
      const [a, b] = conflictIds(conflict);
      for (const id of [a, b]) if (!parent.has(id)) parent.set(id, id);
      const ra = find(a),
        rb = find(b);
      if (ra !== rb) parent.set(ra, rb);
    }
    const groups = new Map();
    for (const conflict of conflicts) {
      const [a, b] = conflictIds(conflict);
      const root = find(a);
      if (!groups.has(root))
        groups.set(root, { ids: [], names: new Map(), pairs: [], assets: new Set(), sampled: false });
      const group = groups.get(root);
      for (const [id, name] of [
        [a, conflict.aName || modName(conflict.a)],
        [b, conflict.bName || modName(conflict.b)]
      ])
        if (!group.names.has(id)) {
          group.ids.push(id);
          group.names.set(id, name);
        }
      group.pairs.push(conflict);
      const assets = conflict.assets || conflict.paths || [];
      for (const asset of assets) group.assets.add(typeof asset === 'string' ? asset : asset.path || asset.name);
      if (conflictSampleNote(conflict, assets)) group.sampled = true;
    }
    return [...groups.values()].sort((x, y) => y.ids.length - x.ids.length || y.pairs.length - x.pairs.length);
  }
  function groupTitle(group) {
    const names = group.ids.map((id) => group.names.get(id));
    return names.length === 2
      ? `${names[0]} & ${names[1]}`
      : `${names.slice(0, -1).join(', ')} & ${names[names.length - 1]}`;
  }
  function omittedConflictPairs(source = state.snapshot) {
    const value = Number(source.conflictPairsOmitted);
    return Number.isFinite(value) && value > 0 ? Math.floor(value) : 0;
  }
  // Mods that conflict through a shared partner count once, like the cards on the Conflicts page.
  function conflictGroupCount() {
    return conflictGroups(state.snapshot.conflicts).length || (omittedConflictPairs() ? 1 : 0);
  }
  function detectedConflictCount() {
    return state.snapshot.conflicts.length + omittedConflictPairs();
  }
  function conflictSampleNote(conflict, assets = conflict.assets || conflict.paths || []) {
    const count = Number(conflict.count ?? assets.length),
      shown = Math.min(64, assets.length);
    return conflict.evidenceTruncated || count > shown
      ? `Showing ${shown} of ${Number.isFinite(count) ? count : assets.length} paths. This is a sample; exported evidence may use the same limit.`
      : '';
  }
  // "v1.2" for a real version; nothing for an empty one or the catalog's "unversioned".
  function versionLabel(version) {
    const text = String(version ?? '')
      .trim()
      .replace(/^v/i, '');
    return !text || /^unversioned$/i.test(text) ? '' : 'v' + text;
  }
  function modCategory(m) {
    if (characterAppearance(m)) return 'Character appearance';
    return m.category || 'Gameplay';
  }
  function characterAppearance(mod) {
    const value = mod?.appearance;
    if (value && typeof value === 'object' && /^[a-z][a-z0-9-]{0,39}$/.test(value.character || '') &&
        ['outfit', 'skin', 'hair', 'accessory'].includes(value.type)) {
      const preview = typeof value.preview === 'string' && /^assets\/mod-[a-z0-9-]{1,80}\.png$/.test(value.preview)
        ? value.preview : '';
      return { character: value.character, type: value.type, preview,
        label: typeof value.label === 'string' ? value.label.slice(0, 200) : mod.name || 'Character appearance', explicit: true };
    }
    if (mod?.detectedCharacter === 'deacon')
      return { character: 'deacon', type: 'detected', preview: '', label: 'Deacon character files', explicit: false };
    if (mod?.category === 'Characters')
      return { character: '', type: 'detected', preview: '', label: 'Character files', explicit: false };
    return null;
  }
  function characterName(value) {
    return value === 'deacon' ? 'Deacon' : String(value || 'Character').replace(/^./, (first) => first.toUpperCase());
  }
  function renderCharacterAppearance(mod) {
    const appearance = characterAppearance(mod);
    if (!appearance) return '';
    const character = characterName(appearance.character);
    return `<section class="character-appearance"><h3>${escape(character)} appearance</h3><p>${appearance.explicit
      ? `${escape(appearance.label)} · ${escape(appearance.type)}. Enable it from the normal mod controls; any overlaps or declared alternatives are reviewed before switching.`
      : `This package contains ${appearance.character ? escape(character) + ' character' : 'character'} files. Check its notes for what it replaces. Conflict checks still use the actual package contents.`}</p>${appearance.preview
        ? `<figure class="character-model-preview"><button type="button" class="character-preview-open" id="detail-character-preview" aria-label="Open ${escape(appearance.label)} model preview"><img id="detail-character-image" src="${escape(appearance.preview)}" alt="${escape(appearance.label)} model preview" loading="lazy" decoding="async"><span id="detail-character-preview-label">View model preview ${icon('external')}</span></button><figcaption>Author model preview · static reference image</figcaption></figure>` : '<p class="field-note character-preview-missing">No model preview is supplied for this package. Check its appearance in the game.</p>'}<p class="field-note">Supplied previews are static reference images; the toolkit does not render the character in 3D.</p></section>`;
  }
  function bindCharacterPreview(mod) {
    const appearance = characterAppearance(mod), control = $('#detail-character-preview'), image = $('#detail-character-image');
    if (!appearance?.preview || !control) return;
    control.onclick = () => showCharacterModelPreview(mod.id);
    if (image) image.onerror = () => {
      image.hidden = true; control.disabled = true;
      const label = $('#detail-character-preview-label'); if (label) label.textContent = 'Preview unavailable';
    };
  }
  function showCharacterModelPreview(id) {
    const mod = getMod(id), appearance = characterAppearance(mod);
    if (!appearance?.preview) return false;
    modal(appearance.label,
      `<figure class="character-model-preview expanded"><img src="${escape(appearance.preview)}" alt="${escape(appearance.label)} model preview"><figcaption>Author model preview · static reference image</figcaption></figure>`,
      '<button type="button" class="button primary" data-modal-close>Close preview</button>');
    return true;
  }
  // Local stamped drawings. Public release metadata selects a symbol, never SVG markup or a URL.
  const modSymbols = {
    character: {
      tone: 'blue',
      paths: '<path d="M4 22v-3c0-3 3.6-5 8-5s8 2 8 5v3M9 14l3 4 3-4M8 4c2-1.5 6-1.5 8 0l1 4-1 3-4 2-4-2-1-3 1-4ZM9 7h.1m5.8 0h.1M10 10h4"/>'
    },
    steadyAim: {
      tone: 'blue',
      paths:
        '<path d="M8 3H4v4m12-4h4v4M4 17v4h4m12-4v4h-4M2 12h5m10 0h5M12 2v5m0 10v5"/><circle cx="12" cy="12" r="4"/><path d="m10.6 12 1 1 1.8-2"/>'
    },
    progression: {
      tone: 'orange',
      paths: '<path d="m12 2 7 4v9l-7 7-7-7V6l7-4Z M8 14l4-4 4 4M8 10l4-4 4 4M10 18h4"/>'
    },
    ammunition: {
      tone: 'orange',
      paths:
        '<path d="M4 9c0-3 2-6 3-7 1 1 3 4 3 7v12H4V9Zm10 3c0-3 2-6 3-7 1 1 3 4 3 7v9h-6v-9ZM4 16h6m4 1h6M4 19h6m4 2h6"/>'
    },
    arsenal: {
      tone: 'orange',
      paths: '<path d="m3 17 4-4 4-1 1-3h6V7h3v5h-4l-4 3-3 1-2 4H5l1-4M11 15l2 4h3l-2-5M17 9h4M3 19l-1 2"/>'
    },
    suppressor: {
      tone: 'blue',
      paths: '<path d="m3 16 10-10 5 5L8 21l-5-5Zm10-10 3-3 5 5-3 3M7 13l4 4m-1-7 4 4M3 6l2 1M8 2l1 3M19 17l2 1"/>'
    },
    melee: { tone: 'orange', paths: '<path d="m4 21 9-12 2 2-9 12M12 4l5-2 5 4-3 6-6-3-1-5Zm2 1 4 2 1 3M5 18l2 2"/>' },
    knife: {
      tone: 'blue',
      paths: '<path d="M20 2c-1 7-5 11-11 14l-2-3C10 7 14 4 20 2ZM8 14l3 3M7 15l-5 5 2 2 5-5M13 11l4-5"/>'
    },
    bike: {
      tone: 'orange',
      paths:
        '<circle cx="5" cy="17" r="3.5"/><circle cx="19" cy="17" r="3.5"/><path d="m5 17 4-7 5 7H5Zm9 0 4-10h-3M8 8h5m5 2 1 7M3 11l4-1"/>'
    },
    fuel: {
      tone: 'orange',
      paths: '<path d="m7 3 4-1 2 3h4l3 4v12H4V7l3-4Zm4 2H7L5 8M15 5V3h3v4M8 11l8 7m0-7-8 7M8 8h3"/>'
    },
    nitro: {
      tone: 'blue',
      paths: '<path d="M9 2h6v4l3 4v10l-2 2H8l-2-2V10l3-4V2Zm0 4h6M8 11h8M7 18h10m-6-6-2 4h3l-1 4 4-6h-3l1-2"/>'
    },
    headlight: {
      tone: 'orange',
      paths: '<path d="M12 5c5 0 8 3 8 7s-3 7-8 7V5Zm0 3H9v8h3M6 5 2 3m4 6H1m5 6H1m5 4-4 2"/>'
    },
    repair: {
      tone: 'orange',
      paths:
        '<path d="M16 3a5 5 0 0 0-5 6L3 17a2.8 2.8 0 0 0 4 4l8-8a5 5 0 0 0 6-6l-4 3-3-3 2-4ZM4 4l3 1 2 3-2 2-3-2-1-3ZM14 16l6 6M17 14l5 6"/>'
    },
    route: {
      tone: 'blue',
      paths:
        '<path d="m3 5 6-3 6 3 6-3v17l-6 3-6-3-6 3V5Zm6-3v17m6-14v17M5 12l2-2m4 2 2 2m4-1 2-3"/><circle cx="18" cy="8" r="1.5"/>'
    },
    movement: {
      tone: 'blue',
      paths: '<path d="m9 3 6 2-2 7 4 2 4 1v5H3v-4l4-4 2-9Zm-2 9 6 2M4 18h15M10 6l4 1m-5 2 4 1M2 6h3M1 10h3"/>'
    },
    stealth: {
      tone: 'purple',
      paths:
        '<path d="M2 12c3-5 6-7 10-7s7 2 10 7c-3 5-6 7-10 7S5 17 2 12ZM4 3l16 18"/><path d="M9 9a4 4 0 0 0 6 6m-4-7a4 4 0 0 1 5 5"/>'
    },
    daylight: {
      tone: 'orange',
      paths: '<path d="M3 17h18M5 20h14M6 14a6 6 0 0 1 12 0M12 2v3M3 6l3 3m12 0 3-3M1 13h3m16 0h3M10 14l2-4 2 4"/>'
    },
    night: {
      tone: 'purple',
      paths: '<path d="M17 3A8 8 0 1 0 21 16 9 9 0 0 1 17 3ZM5 18l2-4 2 4m-2-4v7M3 21h9M20 2v4m-2-2h4"/>'
    },
    camera: {
      tone: 'blue',
      paths:
        '<path d="M3 7h5l2-3h5l2 3h4v14H3V7Zm2 3h3m9 0h2"/><circle cx="12" cy="14" r="4"/><path d="m10 14 1 2 3-3"/>'
    },
    health: {
      tone: 'purple',
      paths: '<path d="M12 21C7 17 2 13 2 8a5 5 0 0 1 10-2 5 5 0 0 1 10 2c0 5-5 9-10 13ZM9 12h6m-3-3v6"/>'
    },
    stamina: {
      tone: 'blue',
      paths:
        '<path d="M12 3v8m-2-4C7 4 4 9 3 15c-1 6 5 7 7 3V7Zm4 0c3-3 6 2 7 8 1 6-5 7-7 3V7ZM5 16l3-3m8 0 3 3M10 11l2-2 2 2"/>'
    },
    focus: {
      tone: 'purple',
      paths:
        '<path d="m12 2 3 6 7 4-7 4-3 6-3-6-7-4 7-4 3-6Z"/><circle cx="12" cy="12" r="3"/><path d="M12 5v2m0 10v2M5 12h2m10 0h2"/>'
    },
    crafting: {
      tone: 'orange',
      paths: '<path d="M3 9h18v12H3V9Zm2 0 3-4 4 4 4-4 3 4M12 9v12M3 14h18M8 5 6 2m10 3 2-3M6 18h3m6 0h3"/>'
    },
    horde: {
      tone: 'purple',
      paths:
        '<path d="M5 4h14l3 7-4 4-1 6H7l-1-6-4-4 3-7Zm0 4 5 3-3 2-2-5Zm14 0-5 3 3 2 2-5ZM9 16v3m3-4v5m3-4v3M10 6h4"/>'
    },
    wilderness: { tone: '', paths: '<path d="m12 2-5 6h3l-6 7h6l-4 5h12l-4-5h6l-6-7h3l-5-6ZM12 20v3M3 21h3m12 0h3"/>' },
    vision: {
      tone: 'blue',
      paths:
        '<path d="M2 13c3-5 6-7 10-7s7 2 10 7c-3 5-6 7-10 7s-7-2-10-7Z"/><circle cx="12" cy="13" r="3"/><path d="M12 1v2.5M4.5 3.5 6 5.5m13.5-2L18 5.5M1 8l2.2.8M23 8l-2.2.8"/>'
    },
    loot: {
      tone: 'orange',
      paths:
        '<path d="M2 12h14v9H2v-9Zm0 4h14M7 12v9m4-9v9M4 12l2-3h6l2 3"/><path d="m19 2 1 2.5 2.5 1-2.5 1L19 9l-1-2.5-2.5-1 2.5-1L19 2Z"/>'
    },
    skull: {
      tone: 'purple',
      paths:
        '<path d="M12 2C7 2 4 5.5 4 10c0 2.8 1.3 4.8 3 6v4h10v-4c1.7-1.2 3-3.2 3-6 0-4.5-3-8-8-8Z"/><circle cx="9" cy="11" r="2"/><circle cx="15" cy="11" r="2"/><path d="m12 13.5-1 2h2l-1-2ZM10 20v-2.5m4 2.5v-2.5"/>'
    },
    injector: {
      tone: 'purple',
      paths: '<path d="m15 3 6 6m-3-3-9 9-4 1 1-4 9-9m-6 6 2 2m1-5 2 2m1-5 2 2M5 19l-3 3M19 1l4 4"/>'
    },
    armor: {
      tone: 'blue',
      paths:
        '<path d="M12 2 4 5v6c0 5 3.5 8.5 8 11 4.5-2.5 8-6 8-11V5l-8-3Z"/><path d="M8 8h8v5.5L12 16l-4-2.5V8Z"/><circle cx="7" cy="7" r=".6"/><circle cx="17" cy="7" r=".6"/>'
    },
    bolt: {
      tone: 'orange',
      paths: '<path d="M4 20 18 6m0 0 1-4 3 3-4 1ZM4 20l-2 2m2-2-1-3m1 3 3 1m0-4-1-3m1 3 3 1m0-4-1-3m1 3 3 1"/>'
    },
    credits: {
      tone: 'orange',
      paths:
        '<ellipse cx="9" cy="6" rx="6" ry="3"/><path d="M3 6v4c0 1.7 2.7 3 6 3s6-1.3 6-3V6M3 10v4c0 1.7 2.7 3 6 3"/><ellipse cx="16" cy="15.5" rx="5" ry="2.5"/><path d="M11 15.5v4c0 1.4 2.2 2.5 5 2.5s5-1.1 5-2.5v-4"/>'
    },
    tap: {
      tone: 'blue',
      paths:
        '<path d="M9 12V4.5a1.5 1.5 0 0 1 3 0V11m0-1.5a1.5 1.5 0 0 1 3 0V12m0-1a1.5 1.5 0 0 1 3 0v5.5c0 3-2 5.5-5 5.5h-1.5c-2 0-3.2-.9-4.2-2.3L4.6 16a1.5 1.5 0 0 1 2.4-1.8L9 16"/><path d="M5 5 3 3m11 0 1.5-1.5M4 9H2"/>'
    },
    speed: {
      tone: 'orange',
      paths:
        '<path d="M3 17a9 9 0 1 1 18 0"/><path d="m12 17 5-6M6 17h1.5m9 0H18M12 8v1.5M7.5 10.5l1 1M16.5 10.5l-1 1M3 21h18"/><circle cx="12" cy="17" r="1.5"/>'
    },
    holster: {
      tone: 'orange',
      paths:
        '<path d="M9 2h7v4l-2 1.5V10h-3V7.5L9 6V2Zm7 1.5h2"/><path d="M6 10h12l-1.5 11h-9L6 10Zm4 4h4M6 10l-2-2m14 2 2-2"/>'
    },
    camp: {
      tone: 'orange',
      paths: '<path d="M2 21 11 8l9 13H2Zm9-13v13m-3.5 0L11 15l3.5 6M11 8V2l5 2-5 2M1 21h22"/>'
    },
    size: {
      tone: 'blue',
      paths:
        '<circle cx="8" cy="5" r="3"/><path d="M3 22v-7a5 5 0 0 1 10 0v7M8 14v8"/><circle cx="18" cy="13.5" r="1.8"/><path d="M15.5 22v-4a2.5 2.5 0 0 1 5 0v4M17 4h4m-2-2v4"/>'
    },
    fieldTag: {
      tone: '',
      paths:
        '<path d="m5 3 11-1 5 6-2 13-15 1L2 8l3-5Z"/><circle cx="8" cy="7" r="1.2"/><path d="m8 17 8-8m-2-1 3 1 1 3M7 12l5 5m-6-5 2-1 2 2-1 2M14 18h3M5 19h2"/>'
    }
  };
  const modIconAliases = {
    crosshair: 'steadyAim',
    target: 'steadyAim',
    bolt: 'progression',
    wrench: 'repair',
    compass: 'route',
    shield: 'horde',
    cube: 'fieldTag',
    eye: 'stealth',
    map: 'route',
    ammo: 'ammunition',
    weapon: 'arsenal',
    pine: 'wilderness'
  };
  const modIconRules = [
    ['character', /character|deacon|player.?model|outfit|costume|\bskins?\b/],
    ['skull', /unforgiving|hardcore|difficulty|hard mode/],
    ['speed', /speed limit|speed cap|speedometer/],
    ['size', /size shifter|giant|tiny|\bsize\b/],
    ['injector', /injector|syringe|\bnero\b/],
    ['armor', /armor|armour|plate/],
    ['bolt', /\bbolts?\b|crossbow/],
    ['credits', /credit|\bears?\b|money|price|trade/],
    ['tap', /interact|instant|hold to/],
    ['holster', /holster/],
    ['camp', /encampment|\bcamps?\b/],
    ['vision', /survival vision|vision|senses|outline/],
    ['loot', /glow|shimmer|\bloot\b|pickup/],
    ['steadyAim', /recoil|spread|bloom|crosshair|reticle|accuracy|aim better|perfect bike aim|steady aim/],
    ['progression', /\bxp\b|experience|progression|level.?up|skill.?point|reward/],
    ['suppressor', /silencer|suppressor/],
    ['knife', /knife|dagger/],
    ['melee', /melee|meele|axe|hatchet|machete/],
    ['ammunition', /ammo|ammunition|reload|magazine/],
    ['fuel', /fuel|gasoline/],
    ['nitro', /nitro|nitrous/],
    ['headlight', /headlight|flashlight/],
    ['repair', /repair|durability/],
    ['bike', /bike|motorcycle|ejection|vehicle|engine/],
    ['route', /teleport|fast travel|map marker|mission distance|mission movement/],
    ['stealth', /stealth|detection|detect|invisib/],
    ['stamina', /stamina|endurance/],
    ['health', /health|heal|medic/],
    ['focus', /focus|slow.?motion/],
    ['crafting', /craft|recipe|resource cost/],
    ['movement', /noclip|no clip|movement|sprint|size shifter|jump|parkour/],
    ['night', /night|darkness/],
    ['daylight', /longer days|day.?length|daylight|time.?scale|hour day|weather/],
    ['camera', /camera|first person|third person|field of view|\bfov\b|\bhud\b|visual/],
    ['horde', /horde|freaker|enemy|difficulty/],
    ['arsenal', /arsenal|weapon|rifle|pistol|shotgun|projectile|damage/],
    ['wilderness', /world|environment|vegetation|foliage/]
  ];
  function modIconText(value) {
    return typeof value === 'string'
      ? value
          .slice(0, 1024)
          .replace(/([a-z])([A-Z])/g, '$1 $2')
          .replace(/[_/\\.-]+/g, ' ')
          .toLowerCase()
      : '';
  }
  function matchModSymbol(text) {
    return modIconRules.find(([, pattern]) => pattern.test(text))?.[0];
  }
  function modArtworkIdentity(m) {
    const modId = m.nexusModId || m.modId || m.provenance?.modId,
      fileId = m.nexusFileId || m.fileId || m.file_id || m.provenance?.fileId;
    return modId && fileId
      ? `${modId}:${fileId}`
      : String(m.id || m.sha256 || m.packageFile || m.fileName || 'unidentified-package').slice(0, 512);
  }
  function resolveModIcon(value) {
    const m = value && typeof value === 'object' ? value : {};
    const explicit = typeof m.icon === 'string' ? m.icon : typeof m.icon?.key === 'string' ? m.icon.key : '';
    let key = Object.hasOwn(modSymbols, explicit)
      ? explicit
      : Object.hasOwn(modIconAliases, explicit)
        ? modIconAliases[explicit]
        : null;
    if (!key)
      key = matchModSymbol(
        [m.name, m.title, m.modName, m.pageName, m.packageFile, m.fileName, m.filename].map(modIconText).join(' ')
      );
    if (!key) {
      const category = modIconText(m.category);
      const categories = {
        weapons: 'arsenal',
        bike: 'bike',
        movement: 'movement',
        survival: 'health',
        stealth: 'stealth',
        world: 'wilderness',
        progression: 'progression',
        camera: 'camera',
        difficulty: 'skull',
        enemies: 'horde',
        visuals: 'vision',
        'quality of life': 'tap'
      };
      if (Object.hasOwn(categories, category)) key = categories[category];
    }
    if (!key && Array.isArray(m.assets))
      key = matchModSymbol(
        m.assets
          .slice(0, 96)
          .map((asset) => modIconText(typeof asset === 'string' ? asset : asset?.path || asset?.name))
          .join(' ')
      );
    if (!key) key = 'fieldTag';
    let tone = modSymbols[key].tone;
    if (key === 'fieldTag') {
      let hash = 2166136261;
      for (const c of modArtworkIdentity(m)) hash = Math.imul(hash ^ c.charCodeAt(0), 16777619) >>> 0;
      tone = ['', 'orange', 'blue', 'purple'][hash % 4];
    }
    return { key, tone };
  }
  const pagePreviewCache = new Map(),
    pagePreviewPending = new Set(),
    pagePreviewQueue = [];
  let pagePreviewActive = 0,
    pagePreviewObserver = null,
    pagePreviewBytes = 0,
    pagePreviewGeneration = 0;
  function previewPageId(mod) {
    if (isLocalMod(mod)) return null;
    const id = Number(mod?.nexusModId || mod?.modId || mod?.provenance?.modId || 0);
    return Number.isInteger(id) && id > 0 && id <= 2147483647 ? id : null;
  }
  function hasPreviewMetadata(id) {
    return (state.downloads.pages || []).some(
      (page) =>
        Number(page.modId || page.nexusModId) === id &&
        page.verified === true &&
        page.contentVerified === true &&
        page.containsAdultContent === false &&
        typeof page.pictureUrl === 'string' &&
        page.pictureUrl.length > 0 &&
        page.pictureUrl.length <= 4096
    );
  }
  function cleanPagePreview(result) {
    if (
      result?.available !== true ||
      !Number.isInteger(result.width) ||
      !Number.isInteger(result.height) ||
      result.width < 1 ||
      result.height < 1 ||
      result.width > 1024 ||
      result.height > 1024
    )
      return null;
    const url = result.dataUrl;
    if (
      typeof url !== 'string' ||
      url.length > 7 * 1024 * 1024 ||
      !/^data:image\/png;base64,iVBORw0KGgo[A-Za-z0-9+/]*={0,2}$/.test(url)
    )
      return null;
    return { dataUrl: url, width: result.width, height: result.height };
  }
  function rememberPagePreview(id, result) {
    const previous = pagePreviewCache.get(id);
    if (previous?.dataUrl) pagePreviewBytes -= previous.dataUrl.length;
    pagePreviewCache.delete(id);
    pagePreviewCache.set(id, result);
    if (result?.dataUrl) pagePreviewBytes += result.dataUrl.length;
    while (pagePreviewCache.size > 32 || pagePreviewBytes > 16 * 1024 * 1024) {
      const key = pagePreviewCache.keys().next().value,
        entry = pagePreviewCache.get(key);
      if (entry?.dataUrl) pagePreviewBytes -= entry.dataUrl.length;
      pagePreviewCache.delete(key);
    }
  }
  function paintPagePreview(tile, id) {
    const image = pagePreviewCache.get(id);
    if (!hasPreviewMetadata(id) || !image?.dataUrl || !tile.isConnected || tile.querySelector('.nexus-page-preview')) return;
    const element = document.createElement('img');
    element.className = 'nexus-page-preview';
    element.alt = '';
    element.setAttribute('aria-hidden', 'true');
    element.decoding = 'async';
    element.loading = 'lazy';
    element.width = image.width;
    element.height = image.height;
    element.addEventListener(
      'error',
      () => {
        element.remove();
        tile.classList.remove('has-page-preview');
        rememberPagePreview(id, null);
      },
      { once: true }
    );
    element.src = image.dataUrl;
    tile.prepend(element);
    tile.classList.add('has-page-preview');
    tile.title = 'Preview from the mod’s Nexus page';
  }
  function pumpPagePreviews() {
    while (pagePreviewActive < 2 && pagePreviewQueue.length) {
      const id = pagePreviewQueue.shift();
      if (!hasPreviewMetadata(id)) { pagePreviewPending.delete(id); continue; }
      const generation = pagePreviewGeneration;
      ++pagePreviewActive;
      request('nexusPreviewImage', { modId: id })
        .then((result) => {
          if (generation === pagePreviewGeneration && hasPreviewMetadata(id))
            rememberPagePreview(id, cleanPagePreview(result));
        })
        .catch(() => {
          if (generation === pagePreviewGeneration && hasPreviewMetadata(id)) rememberPagePreview(id, null);
        })
        .finally(() => {
          pagePreviewPending.delete(id);
          --pagePreviewActive;
          $$('[data-preview-mod-id]')
            .filter((tile) => Number(tile.dataset.previewModId) === id)
            .forEach((tile) => paintPagePreview(tile, id));
          if (generation !== pagePreviewGeneration) queuePagePreview(id);
          pumpPagePreviews();
        });
    }
  }
  function queuePagePreview(id) {
    if (
      state.preview ||
      localEdition() ||
      !hasPreviewMetadata(id) ||
      pagePreviewCache.has(id) ||
      pagePreviewPending.has(id) ||
      pagePreviewQueue.length >= 30
    )
      return;
    pagePreviewPending.add(id);
    pagePreviewQueue.push(id);
    pumpPagePreviews();
  }
  function bindPagePreviews() {
    if (pagePreviewObserver) pagePreviewObserver.disconnect();
    const tiles = $$('[data-preview-mod-id]');
    if (typeof IntersectionObserver === 'function' && !state.preview) {
      pagePreviewObserver = new IntersectionObserver(
        (entries) => {
          for (const entry of entries)
            if (entry.isIntersecting) {
              queuePagePreview(Number(entry.target.dataset.previewModId));
              pagePreviewObserver.unobserve(entry.target);
            }
        },
        { rootMargin: '160px', threshold: 0.01 }
      );
    }
    for (const tile of tiles) {
      const id = Number(tile.dataset.previewModId);
      paintPagePreview(tile, id);
      if (hasPreviewMetadata(id) && !pagePreviewCache.has(id)) {
        if (pagePreviewObserver) pagePreviewObserver.observe(tile);
        else if (!state.preview) queuePagePreview(id);
      }
    }
  }
  function modArtwork(m, { detail = false, compact = false } = {}) {
    const { key, tone } = resolveModIcon(m);
    return `<div class="mod-tile ${tone}${detail ? ' detail-artwork' : ''}${compact ? ' compact-artwork' : ''}" data-mod-icon="${key}"${previewPageId(m) ? ` data-preview-mod-id="${previewPageId(m)}"` : ''} aria-hidden="true"><svg class="mod-symbol" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${modSymbols[key].paths}</svg></div>`;
  }
  function manualNativeMod(m) { return m?.installationPolicy === 'manual-native'; }
  function statusFor(m) {
    if (manualNativeMod(m)) return { label: 'Manual setup', className: 'unknown', note: 'ASI plugin · not managed by the toolkit' };
    if (m.available === false) return { label: 'Unavailable', className: 'unknown', note: 'See mod notes' };
    if (m.status === 'changed' || m.status === 'missing')
      return { label: 'Needs a look', className: 'conflict', note: 'Changed outside the toolkit' };
    if (m.status === 'external')
      return { label: 'Installed by hand', className: 'unknown', note: 'Not managed by the toolkit' };
    const update = installedUpdate(m);
    if (update)
      return {
        label: 'Update available',
        className: 'conflict',
        note: escape(`Installed ${update.currentVersion || m.version || '?'} → latest ${update.version || '?'}`)
      };
    if (m.enabled) return { label: 'Enabled', className: 'enabled', note: 'Installed by the toolkit' };
    if (m.conflictsWithInstalled?.length)
      return {
        label: 'Conflict',
        className: 'conflict',
        note: escape(`With ${m.conflictsWithInstalled[0].name || modName(m.conflictsWithInstalled[0].id)}`)
      };
    if (m.imported) return { label: activationLabel(m), className: 'disabled', note: 'Kept in your library · not loaded by the game' };
    return { label: 'Not added yet', className: 'available', note: 'Download it on Nexus' };
  }
  function modStatusMarkup(m) {
    const status = statusFor(m),
      activation = m.imported ? activationLabel(m) : '';
    return `<div class="mod-status ${status.className}"><span class="status-dot"></span><div>${activation ? `<span class="activation-state ${m.enabled ? 'enabled' : 'disabled'}">${activation}</span>` : ''}${activation && status.label !== activation ? `<span class="mod-warning-state">${escape(status.label)}</span>` : !activation ? escape(status.label) : ''}<small>${status.note}</small></div></div>`;
  }
  const ActivityDismissMs = 10000;
  let activityDismissal = null;
  function activityClock() {
    return typeof performance !== 'undefined' && typeof performance.now === 'function' ? performance.now() : Date.now();
  }
  function noticeWindowVisible() {
    return !document.hidden && !state.nativeWindowHidden &&
      !document.documentElement.classList.contains('app-hidden');
  }
  function noticeInteractionVisible() {
    return noticeWindowVisible() && document.hasFocus?.() !== false;
  }
  function activityNeedsAttention(result) {
    return !!(result && (result.batchReview || result.choosePackages || result.reviewRequired ||
      result.review?.reviewRequired || result.recoveryRequired || result.completed === false ||
      result.failedCount > 0 || (result.batchResult && batchAttentionCount(result) > 0)));
  }
  function activityCanAutoDismiss(activity) {
    return !!activity && !activity.running && !activity.requiresAttention &&
      (activity.outcome === 'complete' || activity.outcome === 'cancelled');
  }
  function stopActivityDismissal() {
    if (activityDismissal?.timer != null) clearTimeout(activityDismissal.timer);
    activityDismissal = null;
  }
  function syncActivityDismissal(redraw = false) {
    const card = $('#operation-activity'), activity = state.activity;
    if (!card || !activityCanAutoDismiss(activity)) { stopActivityDismissal(); return; }
    if (activityDismissal?.activity !== activity) {
      stopActivityDismissal();
      activityDismissal = { activity, remaining: ActivityDismissMs, deadline: null, timer: null,
        hovered: card.matches?.(':hover') === true };
    }
    const countdown = activityDismissal, now = activityClock();
    if (countdown.timer != null) clearTimeout(countdown.timer);
    countdown.timer = null;
    if (countdown.deadline !== null) countdown.remaining = Math.max(0, countdown.deadline - now);
    const visible = noticeWindowVisible(), foreground = visible && document.hasFocus?.() !== false;
    // Native hide/blur can retain a hovered or focused element. Those stale
    // states must not keep a timed notice alive while the user is elsewhere.
    if (!foreground) countdown.hovered = false;
    const paused = foreground && (countdown.hovered || card.matches?.(':hover') === true ||
      card.contains?.(document.activeElement) || $('#modal')?.open);
    countdown.deadline = paused ? null : now + countdown.remaining;
    if (countdown.remaining <= 0) {
      // A delayed callback belongs only to the card that created it.
      if (state.activity === countdown.activity) { state.activity = null; renderActivity(); }
      return;
    }
    const seconds = Math.max(1, Math.ceil(countdown.remaining / 1000)),
      ring = $('#activity-countdown-ring'), number = $('#activity-countdown-seconds'),
      indicator = $('#activity-countdown');
    if (number) number.textContent = String(seconds);
    if (indicator) indicator.title = paused ? 'Automatic close paused' : `Closes in ${seconds} seconds`;
    if (ring) {
      ring.style.setProperty('--activity-countdown-offset', String(100 * (1 - countdown.remaining / ActivityDismissMs)));
      const reducedMotion = document.documentElement.classList.contains('reduce-motion');
      if (redraw || countdown.reducedMotion !== reducedMotion) {
        ring.style.animationDuration = `${ActivityDismissMs}ms`;
        ring.style.animationDelay = `-${ActivityDismissMs - countdown.remaining}ms`;
      }
      countdown.reducedMotion = reducedMotion;
      ring.style.animationPlayState = paused ? 'paused' : 'running';
    }
    if (!paused) countdown.timer = setTimeout(() => {
      if (activityDismissal === countdown && state.activity === countdown.activity) syncActivityDismissal();
    }, visible ? Math.min(1000, countdown.remaining) : countdown.remaining);
  }
  function renderActivity() {
    const card = $('#operation-activity'), activity = state.activity;
    if (!card) return;
    card.hidden = !activity;
    if (!activity) { stopActivityDismissal(); card.innerHTML = ''; return; }
    const measured = Number.isSafeInteger(activity.total) && activity.total > 0 && Number.isSafeInteger(activity.completed),
      completed = measured ? Math.min(activity.total, Math.max(0, activity.completed)) : 0;
    card.classList.toggle('activity-failed', activity.outcome === 'failed');
    const outcome = activity.running ? 'In progress' : activity.outcome === 'failed' ? 'Stopped. Review the error before trying again.' : activity.outcome === 'cancelled' ? 'Cancelled' : 'Finished';
    const automatic = activityCanAutoDismiss(activity),
      countdown = automatic ? '<span class="activity-countdown" id="activity-countdown" aria-hidden="true"><svg viewBox="0 0 32 32"><circle class="countdown-track" cx="16" cy="16" r="13"/><circle class="countdown-drain" id="activity-countdown-ring" cx="16" cy="16" r="13" pathLength="100"/></svg><span class="activity-countdown-seconds" id="activity-countdown-seconds">10</span></span><span class="sr-only" id="activity-auto-dismiss-hint">Closes automatically after 10 seconds. Hover over this message or focus its close button to pause.</span>' : '';
    card.innerHTML = `<div class="activity-symbol">${activity.running ? '<span class="spinner"></span>' : icon(activity.outcome === 'failed' ? 'alert' : 'check')}</div><div class="activity-copy"><strong id="activity-title">${escape(activity.message || 'Working…')}</strong>${activity.file ? `<span class="activity-file">${escape(activity.file)}</span>` : ''}${activity.phase ? `<small>${escape(activity.phase)}</small>` : ''}${measured ? `<progress aria-labelledby="activity-title" max="${activity.total}" value="${completed}"></progress><small>${completed} of ${activity.total} · ${activity.total - completed} remaining</small>` : ''}${activity.message === outcome ? '' : `<span class="activity-announcement">${escape(outcome)}</span>`}</div>${activity.running ? '' : `<div class="activity-controls">${countdown}<button class="icon-button" id="activity-dismiss" aria-label="Dismiss completed activity"${automatic ? ' aria-describedby="activity-auto-dismiss-hint"' : ''}>${icon('close')}</button></div>`}`;
    const live = $('#activity-live'), announcement = activity.running ? activity.phase || 'Operation in progress' : activity.message === outcome ? outcome : `${outcome} ${activity.message || ''}`;
    if (live && live.textContent !== announcement) live.textContent = announcement;
    const close = $('#activity-dismiss');
    if (close) close.onclick = () => {
      if (state.activity !== activity || close !== $('#activity-dismiss')) return;
      const focused = card.contains?.(document.activeElement);
      state.activity = null; renderActivity();
      if (focused) $('#main-content')?.focus?.({ preventScroll: true });
    };
    card.onpointerenter = () => { if (activityDismissal) activityDismissal.hovered = true; syncActivityDismissal(); };
    card.onpointerleave = () => { if (activityDismissal) activityDismissal.hovered = false; syncActivityDismissal(); };
    if (!card.dataset.activityFocusBound) {
      card.dataset.activityFocusBound = 'true';
      card.addEventListener('focusin', () => syncActivityDismissal());
      card.addEventListener('focusout', () => {
        const countdown = activityDismissal;
        Promise.resolve().then(() => { if (countdown === activityDismissal) syncActivityDismissal(); });
      });
    }
    syncActivityDismissal(true);
  }
  function updateActivity(data) {
    if (!state.activity?.running || !data || typeof data !== 'object') return;
    for (const key of ['message', 'phase', 'file'])
      if (typeof data[key] === 'string') state.activity[key] = data[key].slice(0, 512);
    if (Number.isSafeInteger(data.total) && data.total >= 0) state.activity.total = data.total;
    if (Number.isSafeInteger(data.completed) && data.completed >= 0) state.activity.completed = data.completed;
    renderActivity();
  }
  function handleNativeOperationProgress(data) {
    if (!data || typeof data !== 'object' || typeof data.requestId !== 'string') return;
    let activity = activityRequests.get(data.requestId);
    if (data.requestId === 'drop-import' && !activity) {
      // A new native Linux drop starts with the first source. Do not let a late
      // review/finished event revive a card whose result has already arrived.
      if (data.phase !== 'checking' || data.completed !== 0) return;
      activity = { running: true, requestId: data.requestId, message: 'Checking dropped files…' };
      state.nativeDropActivity = activity;
      activityRequests.set(data.requestId, activity);
    }
    if (!activity?.running) return;
    state.activity = activity;
    updateActivity(data);
    renderPlayReadiness();
  }
  function setStatus(text) {
    $('#footer-status').textContent = text;
  }
  const toastHistory = new Map();
  function toast(message, type = 'success', action = null) {
    recordRecentActivity(message, { type });
    const key = type + ':' + String(message), now = Date.now(), last = toastHistory.get(key);
    if (!action && last !== undefined && now - last < (type === 'error' ? 5000 : 15000)) return;
    toastHistory.delete(key); toastHistory.set(key, now);
    while (toastHistory.size > 128) toastHistory.delete(toastHistory.keys().next().value);
    const el = document.createElement('div');
    el.className = `toast ${type}`;
    el.setAttribute('role', type === 'error' ? 'alert' : 'status');
    el.innerHTML = `${icon(type === 'error' ? 'error' : type === 'warning' ? 'alert' : 'check')}<span>${escape(message)}</span>${action ? `<button class="toast-action">${escape(action.label)}</button>` : ''}<button class="toast-close" aria-label="Dismiss notification">${icon('close')}</button>`;
    el.querySelector('.toast-close').onclick = () => el.remove();
    if (action)
      el.querySelector('.toast-action').onclick = () => {
        el.remove();
        action.run();
      };
    const region = $('#toasts');
    region.append(el);
    while (region.children?.length > 3) region.firstElementChild.remove();
    const duration = Math.min(20000, Math.max(type === 'error' ? 10000 : action ? 10000 : type === 'warning' ? 7000 : 3500, String(message).length * 45));
    let remaining = duration, previous = Date.now(), hovered = false;
    el.addEventListener('pointerenter', () => { hovered = true; });
    el.addEventListener('pointerleave', () => { hovered = false; });
    const expire = () => {
      if (!el.isConnected) return;
      const at = Date.now();
      if (!document.hidden && !state.nativeWindowHidden && !hovered && !(noticeInteractionVisible() && el.contains?.(document.activeElement))) remaining -= Math.min(500, Math.max(0, at - previous));
      previous = at;
      if (remaining <= 0) el.remove();
      else setTimeout(expire, Math.min(500, remaining));
    };
    setTimeout(expire, 500);
  }

  function recordRecentActivity(message, options = {}) {
    const text = String(message || '').trim().slice(0, 2048);
    if (!text) return;
    const at = Date.now(), previous = recentActivity[0],
      type = ['success', 'warning', 'error', 'info', 'cancelled'].includes(options.type) ? options.type : 'info',
      label = String(options.label || '').slice(0, 256);
    // A completed operation often emits the same toast. Keep one readable result.
    if (previous?.message === text && at - previous.at < 1500) {
      if (label) { previous.label = label; previous.type = type; }
    } else {
      recentActivity.unshift({ message: text, type, label, at });
      recentActivity.length = Math.min(recentActivity.length, RecentActivityLimit);
    }
    updateRecentActivity();
  }
  function recordCompletedActivity(activity) {
    if (!activity || activity.running || completedActivities.has(activity)) return;
    completedActivities.add(activity);
    recordRecentActivity(activity.message || 'Finished', {
      label: activity.label || 'Operation',
      type: activity.outcome === 'failed' ? 'error' : activity.requiresAttention ? 'warning'
        : activity.outcome === 'cancelled' ? 'cancelled' : 'success'
    });
  }
  function recentActivityMarkup() {
    if (!recentActivity.length) return '<p class="recent-activity-empty">No results yet. Completed operations and notices will appear here while the toolkit is open.</p>';
    const labels = { success: 'Finished', warning: 'Needs attention', error: 'Failed', info: 'Notice', cancelled: 'Cancelled' };
    return `<ol class="recent-activity-list">${recentActivity.map((entry) => `<li class="recent-activity-entry recent-activity-${entry.type}"><span class="recent-activity-symbol">${icon(entry.type === 'error' ? 'error' : entry.type === 'warning' ? 'alert' : entry.type === 'info' || entry.type === 'cancelled' ? 'info' : 'check')}</span><div><div class="recent-activity-meta"><strong>${escape(entry.label || labels[entry.type])}</strong>${entry.label ? `<span>${escape(labels[entry.type])}</span>` : ''}<time datetime="${new Date(entry.at).toISOString()}">${escape(new Date(entry.at).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' }))}</time></div><p>${escape(entry.message)}</p></div></li>`).join('')}</ol>`;
  }
  function updateRecentActivity() {
    const content = $('#recent-activity-content');
    if (!content || !$('#modal').open) return;
    content.innerHTML = recentActivityMarkup();
    const clear = $('#recent-activity-clear');
    if (clear) clear.disabled = !recentActivity.length;
  }
  function showRecentActivity() {
    if ($('#modal').open) return false;
    modal('Recent activity', `<p>The latest ${RecentActivityLimit} results and notices from this session stay here after their messages close. This list clears when you close the toolkit.</p><div id="recent-activity-content">${recentActivityMarkup()}</div>`, '<button type="button" class="button" id="recent-keyboard-help">Keyboard shortcuts</button><button type="button" class="button" id="recent-activity-clear">Clear list</button><button type="button" class="button" data-modal-close>Close</button>');
    $('#recent-activity-clear').disabled = !recentActivity.length;
    $('#recent-activity-clear').onclick = () => { recentActivity.length = 0; updateRecentActivity(); };
    $('#recent-keyboard-help').onclick = () => showKeyboardShortcuts(true);
    return true;
  }
  function showKeyboardShortcuts(fromActivity = false) {
    modal('Keyboard shortcuts', '<p>Use these shortcuts throughout the toolkit. Search stays on the current page when it has a search field.</p><dl class="keyboard-shortcuts"><div><dt><kbd>Ctrl</kbd> + <kbd>F</kbd> or <kbd>/</kbd></dt><dd>Focus search. Pages without search open My mods.</dd></div><div><dt><kbd>Esc</kbd></dt><dd>Clear the focused search, then close mod details or the current dialog.</dd></div><div><dt><kbd>Ctrl</kbd> + <kbd>+</kbd> / <kbd>−</kbd></dt><dd>Increase or decrease the interface size.</dd></div><div><dt><kbd>Ctrl</kbd> + <kbd>0</kbd></dt><dd>Use manual 100% interface size.</dd></div><div><dt><kbd>?</kbd></dt><dd>Open this shortcut guide while no text field is being edited.</dd></div></dl><p class="field-note">Interface-size shortcuts save immediately. Escape in a dialog closes that dialog. Slash and question mark work when you are outside a text field.</p>', `${fromActivity ? '<button type="button" class="button" id="keyboard-activity-back">Back to recent activity</button>' : ''}<button type="button" class="button" data-modal-close>Close</button>`);
    if (fromActivity) $('#keyboard-activity-back').onclick = () => { $('#modal').close(); showRecentActivity(); };
  }

  const desktopPaletteKeys = [
    'window',
    'windowText',
    'base',
    'text',
    'button',
    'buttonText',
    'highlight',
    'highlightedText',
    'mid',
    'alternateBase'
  ];
  const desktopColorVariables = [
    '--bg',
    '--shell',
    '--panel',
    '--panel-deep',
    '--panel-hover',
    '--control',
    '--control-hover',
    '--text',
    '--secondary',
    '--muted',
    '--dim',
    '--border',
    '--line-strong',
    '--focus-color',
    '--selection-bg',
    '--selection-text'
  ];
  function cleanSystemAppearance(value) {
    const appearance = value && typeof value === 'object' ? value : {};
    const palette = {};
    for (const key of desktopPaletteKeys) {
      const color = appearance.palette?.[key];
      if (typeof color === 'string' && /^#[0-9a-f]{6}$/i.test(color)) palette[key] = color.toLowerCase();
    }
    return {
      mode: ['dark', 'light'].includes(appearance.mode) ? appearance.mode : null,
      source: typeof appearance.source === 'string' ? appearance.source.slice(0, 80) : '',
      palette
    };
  }
  function colorChannels(color) {
    return [1, 3, 5].map((index) => parseInt(color.slice(index, index + 2), 16));
  }
  function colorLuminance(color) {
    return colorChannels(color)
      .map((value) => {
        value /= 255;
        return value <= 0.04045 ? value / 12.92 : Math.pow((value + 0.055) / 1.055, 2.4);
      })
      .reduce((sum, value, index) => sum + value * [0.2126, 0.7152, 0.0722][index], 0);
  }
  function colorContrast(a, b) {
    const x = colorLuminance(a),
      y = colorLuminance(b);
    return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05);
  }
  function mixColor(a, b, amount) {
    const left = colorChannels(a),
      right = colorChannels(b);
    return (
      '#' +
      left
        .map((value, index) =>
          Math.round(value * (1 - amount) + right[index] * amount)
            .toString(16)
            .padStart(2, '0')
        )
        .join('')
    );
  }
  function readableColor(background, text, amount = 0.7, otherBackground = background) {
    let color = mixColor(background, text, amount);
    while (amount < 1 && Math.min(colorContrast(background, color), colorContrast(otherBackground, color)) < 4.5) {
      amount = Math.min(1, amount + 0.05);
      color = mixColor(background, text, amount);
    }
    return color;
  }
  function effectiveColorMode(settings) {
    if (settings.colorMode === 'dark' || settings.colorMode === 'light') return settings.colorMode;
    const native = cleanSystemAppearance(state.snapshot.systemAppearance);
    return native.mode || (window.matchMedia?.('(prefers-color-scheme: light)')?.matches ? 'light' : 'dark');
  }
  function usableDesktopPalette(settings, mode = effectiveColorMode(settings)) {
    if (state.snapshot.platform !== 'linux' || settings.useSystemTheme !== true || settings.highContrast) return null;
    const p = cleanSystemAppearance(state.snapshot.systemAppearance).palette;
    if (!p.window || !p.base || !p.text) return null;
    const paletteMode = colorLuminance(p.window) < 0.35 ? 'dark' : 'light';
    if (paletteMode !== mode || colorContrast(p.window, p.text) < 4.5 || colorContrast(p.base, p.text) < 4.5)
      return null;
    return p;
  }
  function applyColorAppearance(settings) {
    const root = document.documentElement,
      mode = effectiveColorMode(settings);
    root.dataset.colorMode = mode;
    root.dataset.systemPalette = '';
    for (const name of desktopColorVariables) root.style.removeProperty(name);
    const p = usableDesktopPalette(settings, mode);
    if (p) {
      const colors = {
        '--bg': mixColor(p.window, p.base, 0.35),
        '--shell': p.window,
        '--panel': p.base,
        '--panel-deep': mixColor(p.window, p.base, 0.5),
        '--panel-hover': mixColor(p.base, p.text, 0.06),
        '--control': p.base,
        '--control-hover': mixColor(p.base, p.text, 0.09),
        '--text': p.text,
        '--secondary': readableColor(p.base, p.text, 0.84, p.window),
        '--muted': readableColor(p.base, p.text, 0.7, p.window),
        '--dim': readableColor(p.base, p.text, 0.7, p.window),
        '--border': mixColor(p.base, p.text, 0.32),
        '--line-strong': mixColor(p.base, p.text, 0.42)
      };
      if (p.highlight && colorContrast(p.base, p.highlight) >= 3) colors['--focus-color'] = p.highlight;
      if (p.highlight && p.highlightedText && colorContrast(p.highlight, p.highlightedText) >= 4.5) {
        colors['--selection-bg'] = p.highlight;
        colors['--selection-text'] = p.highlightedText;
      }
      for (const [name, color] of Object.entries(colors)) root.style.setProperty(name, color);
      root.dataset.systemPalette = 'linux';
    }
    const status = $('#appearance-status');
    if (status) status.textContent = appearanceStatus(settings);
  }
  function appearanceStatus(settings) {
    const mode = effectiveColorMode(settings),
      native = cleanSystemAppearance(state.snapshot.systemAppearance);
    return `${mode === 'dark' ? 'Dark' : 'Light'}${settings.colorMode === 'auto' || !settings.colorMode ? ' · automatic' : ''}${usableDesktopPalette(settings, mode) ? ' · Linux desktop colors' : native.source ? ' · ' + native.source : ''}`;
  }
  function handleSystemAppearance(result) {
    state.snapshot.systemAppearance = cleanSystemAppearance(result.data || result.systemAppearance);
    applyAppearance();
  }
  function renderAppearanceSettings(settings) {
    return `<section class="panel settings-full"><div class="panel-heading"><div><h2>Appearance</h2><p>Choose a light or dark interface, or follow your system.</p></div>${icon('moon')}</div><div class="panel-body"><div class="setting-row"><div class="setting-copy"><label for="color-mode-setting">Color mode</label><p>Automatic follows the system preference. Seasonal colors and motion settings stay separate.</p><div class="effective-scale" id="appearance-status">${escape(appearanceStatus(settings))}</div></div><select id="color-mode-setting" name="colorMode">${[
      ['auto', 'Automatic'],
      ['dark', 'Dark'],
      ['light', 'Light']
    ]
      .map(
        ([value, label]) =>
          `<option value="${value}" ${value === (settings.colorMode || 'auto') ? 'selected' : ''}>${label}</option>`
      )
      .join(
        ''
      )}</select></div>${state.snapshot.platform === 'linux' ? `<div class="setting-row"><div class="setting-copy"><label for="system-theme-setting">Use Linux desktop colors</label><p>Off by default for the toolkit’s own look. Switch on to use your desktop’s panel and text colors.</p></div><button type="button" class="switch" id="system-theme-setting" role="switch" aria-checked="${settings.useSystemTheme === true}" aria-label="Use Linux desktop colors"></button></div>` : ''}</div></section>`;
  }
  function activeSeason(s = { ...state.snapshot.settings, ...appearancePreview }, date = new Date()) {
    if (s.seasonalThemes === false || s.theme === 'default') return null;
    if (s.theme && s.theme !== 'auto') return s.theme;
    const month = date.getMonth() + 1,
      day = date.getDate();
    if ((month === 12 && day === 31) || (month === 1 && day === 1)) return 'newyear';
    if ((month === 10 && day >= 20) || (month === 11 && day <= 2)) return 'halloween';
    if (month === 12 || (month === 1 && day <= 6)) return 'winter';
    if (month >= 3 && month <= 5) return 'spring';
    if (month >= 6 && month <= 8) return 'summer';
    if (month >= 9 && month <= 11) return 'autumn';
    return null;
  }
  const displayNumber = (value, min, max) =>
    typeof value === 'number' && Number.isFinite(value) && value >= min && value <= max;
  function cleanDisplayMetrics(value) {
    if (!value || typeof value !== 'object') return null;
    const m = value;
    if (
      !displayNumber(m.monitorLogicalWidth, 128, 65536) ||
      !displayNumber(m.monitorLogicalHeight, 128, 65536) ||
      !displayNumber(m.monitorPixelWidth, 128, 131072) ||
      !displayNumber(m.monitorPixelHeight, 128, 131072) ||
      !displayNumber(m.devicePixelRatio, 0.5, 8)
    )
      return null;
    const result = {
      source: typeof m.source === 'string' ? m.source.slice(0, 80) : 'native monitor',
      monitorId: typeof m.monitorId === 'string' ? m.monitorId.slice(0, 160) : ''
    };
    for (const key of [
      'monitorLogicalWidth',
      'monitorLogicalHeight',
      'monitorPixelWidth',
      'monitorPixelHeight',
      'devicePixelRatio'
    ])
      result[key] = m[key];
    for (const key of ['workLogicalWidth', 'workLogicalHeight', 'windowLogicalWidth', 'windowLogicalHeight'])
      if (displayNumber(m[key], 1, 65536)) result[key] = m[key];
    if (displayNumber(m.screenDevicePixelRatio, 0.5, 8)) result.screenDevicePixelRatio = m.screenDevicePixelRatio;
    if (typeof m.windowHidden === 'boolean') result.windowHidden = m.windowHidden;
    return result;
  }
  function automaticScaleDecision(
    width = window.innerWidth,
    height = window.innerHeight,
    metrics = state.snapshot.displayMetrics
  ) {
    const native = cleanDisplayMetrics(metrics);
    let target = 1,
      source = 'window',
      monitor = null;
    if (native) {
      monitor = native;
      target = Math.min(native.monitorPixelWidth, native.monitorPixelHeight) / 1080 / native.devicePixelRatio;
      source = 'native monitor';
    } else if (displayNumber(window.screen?.width, 128, 65536) && displayNumber(window.screen?.height, 128, 65536)) {
      const ratio = displayNumber(window.devicePixelRatio, 0.5, 8) ? window.devicePixelRatio : 1;
      monitor = {
        monitorLogicalWidth: window.screen.width,
        monitorLogicalHeight: window.screen.height,
        monitorPixelWidth: Math.round(window.screen.width * ratio),
        monitorPixelHeight: Math.round(window.screen.height * ratio),
        devicePixelRatio: ratio
      };
      target = Math.min(window.screen.width, window.screen.height) / 1080;
      source = 'browser monitor';
    } else if (displayNumber(width, 1, 65536) && displayNumber(height, 1, 65536)) {
      const ratio = Math.min(width / 1440, height / 900);
      target = 1 + Math.max(0, ratio - 1) * 0.85;
    }
    const monitorScale = Math.max(100, Math.min(200, Math.round(target * 20) * 5));
    const windowLimit =
      displayNumber(width, 1, 65536) && displayNumber(height, 1, 65536)
        ? Math.max(100, Math.min(200, Math.floor(Math.min(width / 600, height / 400) * 20) * 5))
        : 200;
    return { percent: Math.min(monitorScale, windowLimit), monitorScale, windowLimit, source, monitor };
  }
  function autoInterfaceScale(
    width = window.innerWidth,
    height = window.innerHeight,
    metrics = state.snapshot.displayMetrics
  ) {
    return automaticScaleDecision(width, height, metrics).percent;
  }
  function effectiveInterfaceScale(settings) {
    return settings.autoScale !== false
      ? autoInterfaceScale()
      : Math.max(80, Math.min(200, Number(settings.interfaceScale) || 100));
  }
  function textPreference(settings) {
    const value = Number(settings.fontScale);
    return Math.max(0.9, Math.min(1.5, (value > 2 ? value / 100 : value) || 1));
  }
  function scaleStatus(settings) {
    const scale = effectiveInterfaceScale(settings),
      text = Math.round(scale * textPreference(settings));
    const monitor = automaticScaleDecision().monitor;
    return `${settings.autoScale !== false ? 'Auto' : 'Manual'} · interface ${scale}% · text ${text}%${monitor ? ` · ${Math.round(monitor.monitorPixelWidth)} × ${Math.round(monitor.monitorPixelHeight)}, system ${Math.round(monitor.devicePixelRatio * 100)}%` : ''}`;
  }
  function handleDisplayMetrics(result) {
    const metrics = cleanDisplayMetrics(result.data || result.displayMetrics);
    if (!metrics) return;
    state.snapshot.displayMetrics = metrics;
    applyAppearance();
  }
  let authorRefreshAt = 0,
    authorLoading = false,
    authorRefreshTimer = null;
  // The native provider returns a pixel-only PNG, or a validated WebP container when
  // the OS has no WebP codec. Nothing else is accepted for the author tile.
  function cleanAuthorImage(result) {
    if (
      result?.available !== true ||
      !Number.isInteger(result.width) ||
      !Number.isInteger(result.height) ||
      result.width < 1 ||
      result.height < 1 ||
      result.width > 512 ||
      result.height > 512
    )
      return null;
    const url = result.dataUrl;
    if (typeof url !== 'string' || url.length > 3 * 1024 * 1024) return null;
    const png = /^data:image\/png;base64,iVBORw0KGgo[A-Za-z0-9+/]*={0,2}$/.test(url),
      webp = result.format === 'webp' && /^data:image\/webp;base64,UklGR[A-Za-z0-9+/]*={0,2}$/.test(url);
    return png || webp ? { dataUrl: url, width: result.width, height: result.height } : null;
  }
  // One pending timer at a time: every trigger (startup, refresh button, visibility
  // change) funnels through here instead of starting another timer chain.
  function scheduleAuthorRefresh(delay) {
    if (authorRefreshTimer) clearTimeout(authorRefreshTimer);
    authorRefreshTimer = setTimeout(
      () => {
        authorRefreshTimer = null;
        refreshAuthorAvatar();
      },
      Math.max(120000, delay)
    );
  }
  async function refreshAuthorAvatar() {
    if (state.preview || localEdition() || authorLoading || document.hidden) return;
    if (Date.now() < authorRefreshAt) {
      scheduleAuthorRefresh(authorRefreshAt - Date.now());
      return;
    }
    authorLoading = true;
    authorRefreshAt = Date.now() + 120000;
    try {
      const result = await request('nexusAuthorAvatar');
      if (result?.authorUserId !== 62048576) return;
      const image = cleanAuthorImage(result),
        frame = $('#author-avatar');
      if (image) {
        frame.innerHTML = '<img alt="Mod author" decoding="async">';
        const picture = $('img', frame);
        picture.onerror = () => {
          frame.textContent = 'O';
          frame.title = 'The author picture could not be shown. The profile link still works.';
        };
        picture.onload = () => {
          frame.title = '';
        };
        picture.src = image.dataUrl;
        authorRefreshAt = Date.now() + (result.stale ? 120000 : 900000);
      } else if (!$('img', frame)) {
        frame.textContent = 'O';
        frame.title =
          typeof result.reason === 'string'
            ? result.reason.slice(0, 160)
            : 'The author picture is unavailable right now. The profile link still works.';
      }
      if (
        typeof result.name === 'string' &&
        result.name.length > 0 &&
        result.name.length <= 100 &&
        !/[\x00-\x1f\x7f]/.test(result.name)
      )
        $('#author-name').textContent = result.name;
    } catch {
      /* The author link and local fallback remain available offline. */
    } finally {
      authorLoading = false;
      scheduleAuthorRefresh(authorRefreshAt - Date.now());
    }
  }
  function syncWindowVisibility() {
    const native = state.snapshot.displayMetrics?.windowHidden;
    document.documentElement.classList.toggle(
      'app-hidden',
      !!document.hidden || (typeof native === 'boolean' ? native : state.nativeWindowHidden)
    );
    syncActivityDismissal();
    syncHeroSlideshow();
    syncCurrentSavesPoll();
  }
  function handleWindowVisibility(result) {
    if (typeof result.data?.hidden !== 'boolean') return;
    state.nativeWindowHidden = result.data.hidden;
    if (state.snapshot.displayMetrics) state.snapshot.displayMetrics.windowHidden = result.data.hidden;
    syncWindowVisibility();
    if (!result.data.hidden) {
      checkForUpdate(true);
      checkToolkitRelease(true);
      checkDownloads();
      if (state.view === 'saves' && state.savesTab === 'backups' && state.saveBackups) loadSavePictures();
    } else cancelDownloadContinuation();
  }

  function applyAppearance() {
    const s = { ...state.snapshot.settings, ...appearancePreview };
    applyColorAppearance(s);
    const season = activeSeason(s);
    document.documentElement.dataset.season = season || '';
    document.documentElement.dataset.contrast = s.highContrast ? 'high' : '';
    document.documentElement.classList.toggle('high-contrast', !!s.highContrast);
    document.documentElement.classList.toggle('large-text', Number(s.fontScale) >= 125);
    document.documentElement.classList.toggle('reduce-motion', !!s.reducedMotion);
    document.documentElement.classList.toggle('compact-mode', !!s.compactMode);
    document.documentElement.style.setProperty('--font-scale', String(textPreference(s)));
    const interfaceScale = effectiveInterfaceScale(s);
    document.documentElement.style.zoom = String(interfaceScale / 100);
    document.documentElement.style.setProperty(
      '--toolkit-viewport-width',
      `${window.innerWidth / (interfaceScale / 100)}px`
    );
    document.documentElement.style.setProperty(
      '--toolkit-viewport-height',
      `${window.innerHeight / (interfaceScale / 100)}px`
    );
    document.documentElement.classList.toggle('scale-compact', window.innerWidth / (interfaceScale / 100) < 980);
    document.documentElement.classList.toggle('scale-narrow', window.innerWidth / (interfaceScale / 100) < 680);
    const scaleLabel = $('#effective-scale-value');
    if (scaleLabel) scaleLabel.textContent = scaleStatus(s);
    renderSeasonalScenery(s, season);
    renderSeasonalEffects(s, season);
    syncWindowVisibility();
    const badge = $('#season-badge');
    badge.hidden = !season;
    badge.textContent = seasonLabel(season);
    renderStatusTicker();
    syncLibraryDock();
  }
  // Fixed emoji artwork and original corner drawings; no remote seasonal code or assets.
  const seasonalEmoji = {
    newyear: ['🎆', '🎇', '✨'],
    halloween: ['🎃', '👻', '🦇', '🍂'],
    winter: ['❄️', '☃️', '🌲'],
    spring: ['🌸', '🌷', '🍃'],
    summer: ['☀️', '🌻', '🌿'],
    autumn: ['🍁', '🍂', '🌰']
  };
  const seasonalScenery = {
    newyear:
      '<path d="m295 34 0-22m0 44v22m-22-22-16 16m60-60-16 16m-50 6h-23m65 0h23m-57-16-16-16m60 60-16-16M42 226v-17m0 34v17m-16-17-12 12m44-44-12 12m-22 4H7m52 0h17m-46-12-12-12m40 40-12-12"/><circle cx="295" cy="45" r="4"/><circle cx="42" cy="232" r="3"/>',
    halloween:
      '<path d="M358 2 235 5M358 2 277 43M358 2 315 91M358 2 354 151M327 3q-1 12 12 12m-37-12q-2 24 22 26m-53-25q-2 40 40 41m-71-40q-1 57 56 62m-59-61q1 73 76 88M339 15q-10 9-4 20m-12-7q-17 16-7 34m-5-18q-24 23-12 50m36-60q-1 18 18 20m-37 7q-1 28 37 33m-54-1q0 41 54 47"/><path d="m283 73-5 20m-4-2 8 3m-10-6 13 6m-11 2 9 2"/>',
    winter:
      '<path d="m359 23-38 12-29-4m29 4 1-19m0 19-12 22m-18-26-15-15m15 15-21 9M358 87l-24-12-19 5m19-5 5-21m-5 21-8 20m-11-15-18-11M359 143l-31 18-13 21m13-21-9-22m9 22 20 16"/><path d="m3 244 28-16 30 5m-30-5 2-22m-2 22 7 24m23-19 19-18m-19 18 20 12M8 180l22-15 20 4m-20-4-3-19m3 19 11 18"/><path class="seasonal-frost" d="m359 5-51 6m51 99-28-12M3 259l61-9"/>',
    spring:
      '<path d="M359 11c-42 32-55 67-58 121m35-99c-22-5-26 1-13 13m8-4c13-16 22-15 17 0m-29 21c-24-7-29 0-13 15m6-6c19-17 26-13 20 3m-27 16c-21-5-27 3-12 15m8-5c17-14 24-9 17 6M1 254c22-5 46-22 69-47m-43 36c-13-12-8-19 4-11m9 3c1 17 9 19 14 5m1-19c-6-17 1-21 9-8"/><path class="seasonal-bloom" d="m331 16 4-4 4 5-4 5-4-6m-39 108 4-4 4 5-5 4-3-5"/>',
    summer:
      '<path d="M362 4c-37 5-70 38-78 75M349 8l-2 17m-17-10-5 16m-14-3-7 15m-8 1-10 14M4 255c22-7 43-5 65 4M14 251l-5-20m14 19-1-26m12 26 3-18m8 19 8-27m4 29 9-16"/><path class="seasonal-sunray" d="m337 0-81 90m102-54-69 70m-20-101-23 41"/>',
    autumn:
      '<path d="M360 7c-26 23-48 36-85 44m54-21-2-18m-7 23 9 17m-30-7-9-14M1 258c30-27 63-39 96-41m-60 17-6-19m15 14 9 20"/><path class="seasonal-leaf" d="m313 68 17-6 6 5-7 9-18 2 2-10Zm0 0 17-6m-42-38 7-16-4-8-10 8-1 15 8 1Zm0 0 6-16M61 236l17-8 7 7-9 9-18 1 3-9Zm0 0 17-8"/>'
  };
  // Badge text for the active seasonal theme (top-right corner). Every season carries its own name in the spirit of the
  // Halloween label instead of the generic "<Season> atmosphere"; December and early January winter names its lights.
  const seasonLabels = {
    autumn: 'Autumn · Copper Cascades',
    winter: 'Winter · Frost on the Trail',
    spring: 'Spring · Morning Thaw',
    summer: 'Summer · High Desert Sun',
    halloween: 'Late October · Halloween',
    newyear: 'New Year’s Eve · Fireworks',
    holiday: 'Holiday · Lights in the Snow'
  };
  function seasonLabel(season, date = new Date()) {
    if (!season) return '';
    if (festiveWinter(season, date)) return 'Winter · Holiday Lights';
    return seasonLabels[season] || `${season[0].toUpperCase() + season.slice(1)} atmosphere`;
  }
  function festiveWinter(season, date = new Date()) {
    return season === 'winter' && (date.getMonth() === 11 || (date.getMonth() === 0 && date.getDate() <= 6));
  }
  function renderSeasonalScenery(settings, season) {
    const layer = $('#seasonal-scenery');
    if (!layer) return;
    const visible = !!season && settings.seasonalThemes !== false;
    layer.hidden = !visible;
    if (!visible) {
      layer.innerHTML = '';
      layer.dataset.season = '';
      document.documentElement.dataset.festive = '';
      return;
    }
    const festive = festiveWinter(season),
      key = season + (festive ? '-festive' : '');
    document.documentElement.dataset.festive = festive ? 'true' : '';
    if (layer.dataset.season === key && layer.innerHTML) return;
    layer.dataset.season = key;
    const lights = festive
      ? '<path class="seasonal-light-wire" d="M102 8q112 42 249 0"/><path class="seasonal-light-bulb light-copper" d="M133 17v8q-4 8 3 9 7-3 1-9v-7m47 8v9q-4 8 3 9 7-3 1-9v-8"/><path class="seasonal-light-bulb light-pine" d="M236 27v8q-4 8 3 9 7-3 1-9v-9m47-6v8q-4 8 3 9 7-3 1-9v-9"/>'
      : '';
    layer.innerHTML = `<svg class="seasonal-corner corner-upper" viewBox="0 0 360 260" fill="none" stroke="currentColor" stroke-width="1" stroke-linecap="round" aria-hidden="true">${seasonalScenery[season] || seasonalScenery.winter}${lights}</svg><svg class="seasonal-corner corner-lower" viewBox="0 0 360 260" fill="none" stroke="currentColor" stroke-width="1" stroke-linecap="round" aria-hidden="true">${seasonalScenery[season] || seasonalScenery.winter}</svg>`;
  }
  function seasonalEffectsStatus(settings, season) {
    if (!season || settings.seasonalThemes === false) return 'Choose a seasonal theme to show its emoji.';
    if (settings.seasonalEffects === false) return 'Falling emoji are switched off.';
    if (settings.highContrast) return 'Hidden while higher contrast is on.';
    if (settings.reducedMotion) return 'Paused while Reduce motion is on.';
    if (window.matchMedia?.('(prefers-reduced-motion: reduce)')?.matches)
      return 'Paused by your system’s Reduce motion setting. Static seasonal details stay visible.';
    return 'Seasonal emoji are on. They pause while the window is hidden.';
  }
  function renderSeasonalEffects(settings, season) {
    const overlay = $('#seasonal-effects');
    if (!overlay) return;
    const status = $('#seasonal-effects-status');
    const statusText = seasonalEffectsStatus(settings, season);
    if (status && status.textContent !== statusText) status.textContent = statusText;
    const reduced = settings.reducedMotion || window.matchMedia?.('(prefers-reduced-motion: reduce)')?.matches;
    const visible =
      !!season &&
      settings.seasonalThemes !== false &&
      settings.seasonalEffects !== false &&
      !settings.highContrast &&
      !reduced;
    overlay.hidden = !visible;
    if (!visible) {
      overlay.innerHTML = '';
      overlay.dataset.season = '';
      return;
    }
    if (overlay.dataset.season === season && overlay.innerHTML) return;
    const symbols = seasonalEmoji[season] || seasonalEmoji.winter;
    overlay.dataset.season = season;
    overlay.innerHTML =
      Array.from(
        { length: 18 },
        (_, i) => {
          const symbol = symbols[i % symbols.length];
          const artwork = symbol.codePointAt(0).toString(16);
          return `<span class="seasonal-particle emoji-${i + 1}" aria-hidden="true"><span class="seasonal-glyph glyph-u${artwork}" aria-hidden="true">${symbol}</span></span>`;
        }
      ).join('') +
      (season === 'newyear'
        ? Array.from(
            { length: 4 },
            (_, i) => `<span class="seasonal-firework firework-${i + 1}" aria-hidden="true"></span>`
          ).join('')
        : '');
  }
  async function acceptSnapshot(data) {
    const previousContext = state.snapshot.accountContext;
    const previousWorkshopContext = workshopContext();
    state.snapshot = normalizeSnapshot(data);
    resetCleanSaveEditorContext();
    if (previousWorkshopContext !== workshopContext()) {
      ++workshopGeneration; ++workshopAssetGeneration; clearWorkshopDraftInputs();
      state.workshopLoaded = false; state.workshopLoading = false; state.workshopWorking = false; state.workshopProjectsLoading = false;
      state.workshopAsset = null; state.workshopMerge = null; state.workshopError = '';
    }
    if (previousContext && state.snapshot.accountContext && previousContext !== state.snapshot.accountContext) {
      if (autoSaveTimer) clearTimeout(autoSaveTimer);
      autoSaveTimer = null; pendingAutoSavePreferences = null; appearancePreview = null;
      closeModDetails(false);
      undoSet = null; undoContext = null;
      state.gamePathError = null;
      discardBatchReview();
      state.directories = null;
      state.directoriesLoading = false;
      state.portableProfiles = null;
      state.profilesLoading = false;
      state.diagnosticReport = null;
      state.diagnosticStale = false;
      state.diagnosticsLoading = false;
      state.crashLogs = null;
      state.crashLogsLoading = false;
      state.crashWatch = { watching: false, pending: false, lastCheck: 0, checking: false };
      state.noticeQueue = state.noticeQueue.filter((notice) => !['game-crash', 'game-updated', 'batch-review', 'package-review'].includes(notice.type));
      state.saveBackups = null;
      state.saveBackupsLoading = false;
      state.savePictures.clear();
      state.saveSelection = null;
      state.saveOptionsOpen = false;
      state.saveRestoreReview = null;
      state.saveDeleteReview = null;
      state.currentSaveReplaceReview = null;
      state.saveDetailsDrafts.clear();
      state.saveProfileFilter = null;
      state.currentSaves = null;
      state.currentSavesLoading = false;
      state.currentSaveSelection = null;
      state.currentSavePreviews.clear();
      state.libraryMeta = { entries: {}, history: [] };
      state.libraryMetaLoaded = false;
      state.libraryMetaLoading = false;
      state.gameBuildChecked = 0;
      state.gameBuild = null;
      state.conflictGuide = null;
      state.selectedCrashLogs.clear();
      state.downloads = { files: [], updates: [] };
      state.downloadOffers = [];
      intentionallyRemovedDownloadHashes.clear();
      cancelDownloadContinuation();
      state.nexus = { connected: false, premium: null, tierVerified: false };
      const context = state.snapshot.accountContext;
      try {
        const nexus = await request('nexusStatus');
        if (context !== state.snapshot.accountContext) return;
        state.nexus = nexus;
      } catch {
        /* The new installation remains disconnected until its account can be checked. */
      }
      if (context === state.snapshot.accountContext) checkAutomaticReleases();
    }
    reconcileImportNotices();
  }
  async function refresh({ quiet = false, preserveForm = false, background = false } = {}) {
    const context = state.snapshot.accountContext;
    if (!quiet) setStatus('Checking your mods…');
    try {
      const snapshot = await request('snapshot');
      if (context !== state.snapshot.accountContext) return false;
      const incomingContext = (snapshot?.snapshot || snapshot)?.accountContext ?? context;
      await acceptSnapshot(snapshot);
      if (incomingContext !== state.snapshot.accountContext) return false;
      state.profileSession = state.snapshot.profileSession || state.profileSession;
      if (state.snapshot.gameRestart) state.gameRestart = state.snapshot.gameRestart;
      if (!state.filterChosen) state.filter = enabledMods().length ? 'enabled' : 'available';
      rememberAddedTimes();
      maybeShowWhatsNew();
      considerSetup(state.snapshot);
      collectActionNotices(state.snapshot);
      setTimeout(afterScanMaintenance, 0);
      applyAppearance();
      state.loading = false;
      state.scanFailed = false;
      lastRefreshAt = Date.now();
      if (!(preserveForm && state.view === 'settings')) render();
      else renderPlayReadiness();
      if (state.view === 'saves' && state.savesTab === 'backups' && !state.saveBackups && !state.snapshot.libraryBackupRecovery?.pending) loadSaveBackups();
      setStatus(
        state.preview
          ? 'Interface preview · Open the desktop app to manage mods.'
          : `${importedMods().length} imported · ${enabledMods().length} enabled · ${conflictGroupCount()} conflict${conflictGroupCount() === 1 ? '' : 's'}`
      );
      return true;
    } catch (error) {
      if (context !== state.snapshot.accountContext) return false;
      state.loading = false;
      state.scanFailed = true;
      if (!(preserveForm && state.view === 'settings')) render();
      else renderPlayReadiness();
      if (!background) toast(errorText(error), 'error');
      setStatus('Could not check your mods. Try again, or run Diagnostics.');
      return false;
    } finally {
      if (filesystemDirty && !filesystemRefreshRunning && !state.busy) scheduleFilesystemRefresh();
      scheduleNoticeDelivery();
    }
  }
  // Safety net next to the folder watcher: a quiet recheck when the window comes back and about once a minute while
  // it is visible, for changes the watcher cannot see (folders on redirected drives, a change while it restarted).
  // The page is redrawn only when the installed files or mod states actually changed.
  let lastRefreshAt = Date.now();
  function snapshotSignature(snapshot) {
    return JSON.stringify([
      (snapshot.installed || []).map((f) => [f.id, f.sha256, f.bytes, f.owned, f.readable]),
      (snapshot.mods || []).map((m) => [m.id, m.enabled, m.imported, m.status]),
      (snapshot.conflicts || []).length,
      !!snapshot.gameRunning,
      snapshot.setup?.startup?.status || '',
      snapshot.setup?.state || '',
      !!snapshot.recoveryRequired,
      (snapshot.diagnostics || []).map((issue) => [issue.code, issue.level])
    ]);
  }
  async function backgroundCheck(returned = false) {
    if (state.preview || state.busy || state.nativeDropActivity?.running || state.loading || filesystemRefreshRunning || document.hidden) return false;
    checkForUpdate(true);
    checkToolkitRelease(true);
    checkDownloads();
    checkGameBuild(returned);
    // New crash reports appear after the game closes; the list is metadata only.
    if (returned && state.view === 'diagnostics' && state.crashLogs) loadCrashLogs();
    if (Date.now() - lastRefreshAt < 15000) return false;
    lastRefreshAt = Date.now();
    const context = state.snapshot.accountContext;
    try {
      const next = normalizeSnapshot(await request('snapshot'));
      if (context !== state.snapshot.accountContext) return false;
      if (snapshotSignature(next) === snapshotSignature(state.snapshot)) {
        state.scanFailed = false;
        renderPlayReadiness();
        return false;
      }
      scheduleFilesystemRefresh();
      return true;
    } catch {
      if (context !== state.snapshot.accountContext) return false;
      state.scanFailed = true;
      renderPlayReadiness();
      return false; // the next check, the folder watcher or the refresh button tries again
    }
  }
  function scheduleBackgroundCheck() {
    const timer = setTimeout(async () => {
      await backgroundCheck();
      scheduleBackgroundCheck();
    }, 60000);
    timer?.unref?.(); // never keeps a test process alive
  }
  function scheduleFilesystemRefresh() {
    if (state.diagnosticReport) state.diagnosticStale = true;
    filesystemDirty = true;
    renderPlayReadiness();
    if (filesystemRefreshTimer) clearTimeout(filesystemRefreshTimer);
    filesystemRefreshTimer = setTimeout(flushFilesystemRefresh, 600);
  }
  async function flushFilesystemRefresh() {
    filesystemRefreshTimer = null;
    if (!filesystemDirty || filesystemRefreshRunning) return;
    if (state.busy || state.nativeDropActivity?.running || state.loading) return;
    filesystemDirty = false;
    filesystemRefreshRunning = true;
    let context = state.snapshot.accountContext;
    try {
      const updated = await refresh({ quiet: true, preserveForm: true, background: true });
      if (context !== state.snapshot.accountContext) return;
      if (!updated) {
        setStatus('Your mods folder changed. Press refresh to check it.');
        return;
      }
      context = state.snapshot.accountContext;
      if (state.view === 'profiles') await loadPortableProfiles();
      if (context !== state.snapshot.accountContext) return;
      if (state.view === 'folders' && state.directories) {
        const result = await request('directoriesScan');
        if (context !== state.snapshot.accountContext) return;
        state.directories = result.directories || result;
        render();
      }
      // An open Diagnostics report checks again by itself instead of only being marked out of date.
      if (state.view === 'diagnostics' && state.diagnosticReport && !state.diagnosticsLoading) {
        state.diagnosticsLoading = true;
        try {
          const report = await request('diagnosticsRun');
          if (context !== state.snapshot.accountContext) return;
          if (report?.checks) {
            state.diagnosticReport = report;
            state.diagnosticStale = false;
            render();
          }
        } catch {
          /* the out-of-date banner still offers Run again */
        } finally {
          if (context === state.snapshot.accountContext) state.diagnosticsLoading = false;
        }
      }
      setStatus('Your mods folder changed. The list is up to date.');
    } catch {
      if (context === state.snapshot.accountContext) setStatus('Your mods folder changed. Press refresh to check it.');
    } finally {
      filesystemRefreshRunning = false;
      renderPlayReadiness();
      if (filesystemDirty) scheduleFilesystemRefresh();
      scheduleNoticeDelivery();
      resumeSaveThumbnails();
    }
  }
  async function run(action, args = {}, options = {}) {
    if (sessionBlocksAction(action)) {
      toast(
        'A profile session is active. Close Days Gone and wait until your own mods are back before changing mods.',
        'warning'
      );
      return null;
    }
    if (state.busy) return;
    if (state.nativeDropActivity?.running) {
      toast('Wait for the dropped files to finish checking, then try again.', 'warning');
      return null;
    }
    state.busy = true;
    renderPlayReadiness();
    document.body.classList.add('busy');
    applyExternalLocks();
    applyRecordLocks();
    applyModActionLocks();
    setStatus(options.progress || 'Working…');
    const showActivity = options.activity !== false && (options.activity === true || /import|backup|restore|^scan$|^enable$|^disable$|switchMod|diagnosticsRun|profileLaunch/i.test(action));
    const activity = showActivity ? { running: true, message: options.progress || 'Working…',
      label: (options.progress || 'Operation').replace(/[.…]+$/, ''), action } : null;
    if (activity) { state.activity = activity; renderActivity(); }
    try {
      const result = await request(action, args, options.objects, true, activity);
      if (options.refresh !== false) await refresh({ quiet: true });
      const successMessage = options.success
        ? typeof options.success === 'function' ? options.success(result) : options.success : null;
      if (successMessage) toast(successMessage);
      if (activity) {
        const issues = result?.batchResult ? batchAttentionCount(result) : 0;
        activity.running = false;
        activity.requiresAttention = activityNeedsAttention(result);
        activity.outcome = result?.cancelled ? 'cancelled' : result?.completed === false || result?.recoveryRequired || result?.failedCount > 0 || issues > 0 ? 'failed' : 'complete';
        activity.message = result?.cancelled ? 'Cancelled' : result?.batchReview ? 'Import ready for review' : result?.batchResult ? `${Number(result.importedCount) || 0} mods added${issues ? ` · ${issues} need attention` : ''}${result.recoveryRequired ? ' · recovery required' : ''}` : successMessage || result?.message || 'Finished';
        recordCompletedActivity(activity);
        if (!state.activity?.running || state.activity === activity) { state.activity = activity; renderActivity(); }
      }
      return result;
    } catch (error) {
      if (activity) {
        activity.running = false; activity.outcome = 'failed'; activity.message = errorText(error);
        recordCompletedActivity(activity);
        if (!state.activity?.running || state.activity === activity) { state.activity = activity; renderActivity(); }
      }
      const handled = typeof options.onError === 'function' && options.onError(error) === true;
      if (!handled && !showActionError(error, action)) toast(errorText(error), 'error');
      setStatus('That did not finish. See the message.');
      return null;
    } finally {
      if (activity?.requestId) activityRequests.delete(activity.requestId);
      state.busy = false;
      renderPlayReadiness();
      document.body.classList.remove('busy');
      applyExternalLocks();
      applyRecordLocks();
      applyModActionLocks();
      if (/import|^remove$|^restoreLibraryCopy$/i.test(action)) scheduleDownloadContinuation();
      if (filesystemDirty) scheduleFilesystemRefresh();
      scheduleNoticeDelivery();
      resumeSaveThumbnails();
    }
  }
  function header(kicker, title, description, actions = '') {
    return `<div class="page-header"><div><div class="eyebrow"><span class="eyebrow-line"></span>${escape(kicker)}</div><h1>${escape(title)}</h1><p>${escape(description)}</p></div>${actions ? `<div class="header-actions">${actions}</div>` : ''}</div>`;
  }
  function button(label, action, iconName = 'plus', className = '') {
    return `<button type="button" class="button ${className}" data-action="${action}">${icon(iconName)}${escape(label)}</button>`;
  }
  function previewNote() {
    return state.preview
      ? `<div class="banner neutral">${icon('info')}<p><strong>You’re looking at the interface preview.</strong> The catalog is real; installed states and compatibility results appear after the desktop application scans your setup.</p></div>`
      : '';
  }
  function nexusModId(file) {
    if (isLocalMod(file) && !file.catalogMatch) return 0;
    return Number(file?.nexusModId || file?.modId || file?.provenance?.modId || 994);
  }
  function nexusFileId(file) {
    return Number(file?.nexusFileId || file?.fileId || file?.file_id || file?.provenance?.fileId || 0);
  }
  function sameNexusFile(a, b) {
    return nexusModId(a) === nexusModId(b) && nexusFileId(a) === nexusFileId(b);
  }
  function liveNewFiles() {
    return (state.downloads.inventory || []).filter(
      (file) => !state.snapshot.mods.some((mod) => sameNexusFile(mod, file)) && !file.catalogId
    );
  }

  function clearLiveNexusContent() {
    ++pagePreviewGeneration;
    for (const id of pagePreviewQueue) pagePreviewPending.delete(id);
    pagePreviewQueue.length = 0;
    pagePreviewCache.clear();
    pagePreviewBytes = 0;
    if (pagePreviewObserver) pagePreviewObserver.disconnect();
    state.downloads.inventory = [];
    state.downloads.fileUpdates = [];
    state.downloads.pages = [];
    state.downloads.files = [];
    state.downloads.updates = [];
    state.downloads.inventoryCheckedAt = null;
    for (const image of $$('.nexus-page-preview')) {
      image.parentElement?.classList.remove('has-page-preview');
      image.remove();
    }
  }
  async function loadLiveInventory() {
    const context = state.snapshot.accountContext;
    clearLiveNexusContent();
    if (['overview', 'mods', 'downloads'].includes(state.view)) render();
    const result = await request('nexusInventory');
    if (context !== state.snapshot.accountContext) return null;
    const nextPages = (Array.isArray(result.mods) ? result.mods : []).filter(
      (page) => page?.verified === true && page.contentVerified === true && page.containsAdultContent === false
    );
    const allowed = new Set(nextPages.map((page) => Number(page.modId || page.nexusModId)).filter(
      (id) => Number.isInteger(id) && id > 0 && id <= 2147483647
    ));
    state.downloads.inventory = (Array.isArray(result.files) ? result.files : []).filter(
      (file) => allowed.has(Number(file?.modId ?? file?.nexusModId))
    );
    state.downloads.fileUpdates = (Array.isArray(result.fileUpdates) ? result.fileUpdates : []).filter(
      (file) => allowed.has(Number(file?.modId ?? file?.nexusModId))
    );
    state.downloads.pages = nextPages;
    state.downloads.inventoryTruncated = !!result.truncated;
    state.downloads.inventoryCheckedAt = result.checkedAt;
    return { ...result, mods: nextPages, files: state.downloads.inventory, fileUpdates: state.downloads.fileUpdates };
  }
  function renderDiscoveries() {
    const files = liveNewFiles();
    if (!files.length) return '';
    const shown = files.slice(0, state.discoveryLimit);
    return `<section class="mt-28"><div class="section-heading"><div><h2>New from the author <span>${files.length}</span></h2><p>Live Nexus releases appear here without a loader update. Their actual packages need inspection before being added.</p></div></div><div class="mod-list">${shown.map((file) => `<article class="mod-row">${modArtwork(file)}<div><strong class="mod-name">${escape(file.name)}</strong><p class="mod-summary">${escape(file.fileName || 'Official Nexus release')}</p><div class="mod-meta"><span>${escape(file.version || 'Version not supplied')}</span><span>· ${escape(file.modName || file.pageName || `Nexus page ${nexusModId(file)}`)}</span></div></div><div class="mod-status unknown"><span class="status-dot"></span><div>Available from Nexus<small>Inspection required</small></div></div><div class="mod-row-actions"><button class="button small" data-nexus-file="${escape(file.fileId)}" data-nexus-mod-id="${escape(nexusModId(file))}">${icon('download')}Download & inspect</button></div></article>`).join('')}</div>${shown.length < files.length ? `<p class="field-note">Showing ${shown.length} of ${files.length} new releases.</p>${button('Show 50 more releases', 'discoveries-more', 'plus')}` : ''}</section>`;
  }
  // "NoRecoil_v2_P.pak", "No Recoil 1.1" and "NoRecoil_P.pak" all become "norecoil": versions, the _P suffix and
  // punctuation are ignored, so a new download can be matched to the mod it updates.
  function modFamily(value) {
    return String(value || '')
      .toLowerCase()
      .replace(/\.(pak|zip)$/, '')
      .replace(/_p$/, '')
      .replace(/(^|[^a-z])v?\d+([._-]\d+)*/g, '$1')
      .replace(/[^a-z]/g, '');
  }
  // The mod a new import would update: same package file or same name family, but different bytes. Official catalog
  // entries are told apart by the catalog (variants such as 2x/4x are not versions), so one side must be a local file.
  function olderVersionOf(mod, local = isLocalMod(mod)) {
    const families = [modFamily(mod.packageFile), modFamily(mod.name)].filter((family) => family.length >= 4);
    if (!families.length) return null;
    return (
      libraryMods().find(
        (m) =>
          m.id !== mod.id &&
          hasMod(m) &&
          (local || isLocalMod(m)) &&
          (!mod.sha256 || m.sha256 !== mod.sha256) &&
          (families.includes(modFamily(m.packageFile)) || families.includes(modFamily(m.name)))
      ) || null
    );
  }
  const contentDigest = (value) => typeof value === 'string' && /^[a-f0-9]{64}$/i.test(value) ? value.toLowerCase() : '';
  function presentDownloadHashes(snapshot = state.snapshot) {
    const hashes = new Set();
    const add = (value) => { const hash = contentDigest(value); if (hash) hashes.add(hash); };
    for (const mod of snapshot.mods || []) {
      if (!mod.imported && !mod.enabled) continue;
      add(mod.sha256); add(mod.archiveSha256);
      add(mod.provenance?.sourceSha256); add(mod.provenance?.downloadSha256);
    }
    for (const file of snapshot.installed || []) if (file.readable === true) add(file.sha256);
    return hashes;
  }
  function managedReviewPresent(review) {
    const hash = contentDigest(review?.mod?.sha256);
    if (!hash) return false;
    return (state.snapshot.mods || []).some((mod) => contentDigest(mod.sha256) === hash &&
      (review.adoption?.mode === 'take-ownership'
        ? mod.id === review.mod.id && mod.owned === true && mod.enabled === true
        : mod.imported === true || mod.owned === true && mod.enabled === true));
  }
  function discardPackageReview(review = pendingPackageReview?.review) {
    if (pendingPackageReview?.review?.reviewToken === review?.reviewToken) pendingPackageReview = null;
    if (review?.reviewToken && !state.preview)
      request('discardReviewedImport', { reviewToken: review.reviewToken }).catch(() => {});
  }
  function batchReviewPresent(review) {
    const items = Array.isArray(review?.items) ? review.items : [];
    const ready = items.filter((item) => item.status === 'ready');
    return ready.length > 0 && items.every((item) => item.status === 'duplicate' ||
      item.status === 'ready' && managedReviewPresent({ mod: item.mod }));
  }
  function reconcileImportNotices() {
    const hashes = presentDownloadHashes(), offers = state.downloadOffers || [];
    for (const hash of hashes) intentionallyRemovedDownloadHashes.delete(hash);
    state.downloadOffers = offers.filter((offer) => !hashes.has(contentDigest(offer.sha256)) &&
      !intentionallyRemovedDownloadHashes.has(contentDigest(offer.sha256)));
    state.noticeQueue = state.noticeQueue.filter((notice) => {
      if (notice.type === 'package-review' && managedReviewPresent(notice.review)) {
        discardPackageReview(notice.review); return false;
      }
      if (notice.type === 'batch-review' && batchReviewPresent(notice.review)) {
        if (!state.preview) request('batchImportDiscard', { batchToken: notice.review.batchToken }).catch(() => {});
        return false;
      }
      return true;
    });
    if (pendingPackageReview && pendingPackageReview.context === state.snapshot.accountContext &&
        managedReviewPresent(pendingPackageReview.review)) {
      discardPackageReview();
      if ($('#modal').open) $('#modal').close();
    }
    if (pendingBatchReviewData && batchReviewPresent(pendingBatchReviewData)) {
      discardBatchReview();
      if ($('#modal').open) $('#modal').close();
    }
    updateNoticeBadge();
  }
  function cancelDownloadContinuation() {
    if (downloadDiscoveryTimer !== null) clearTimeout(downloadDiscoveryTimer);
    downloadDiscoveryTimer = null;
  }
  function scheduleDownloadContinuation() {
    if (downloadDiscoveryTimer !== null || state.preview || document.hidden || state.nativeWindowHidden) return;
    const context = state.snapshot.accountContext;
    downloadDiscoveryTimer = setTimeout(() => {
      downloadDiscoveryTimer = null;
      if (context === state.snapshot.accountContext) checkDownloads();
    }, 250);
    downloadDiscoveryTimer?.unref?.();
  }
  // New mod downloads spotted in the Downloads folder. Each is offered once through the normal import review;
  // Ignore stops offering it.
  async function checkDownloads() {
    if (state.preview || state.busy || state.nativeDropActivity?.running || state.profileSession.active ||
        document.hidden || state.nativeWindowHidden || state.downloadDiscoveryChecking) return false;
    state.downloadDiscoveryChecking = true;
    cancelDownloadContinuation();
    const context = state.snapshot.accountContext;
    checkForCatalog(true);
    try {
      const result = await request('downloadCandidates');
      if (context !== state.snapshot.accountContext) return false;
      const hashes = presentDownloadHashes();
      const candidates = result?.candidates || [];
      const removed = candidates.filter((offer) => intentionallyRemovedDownloadHashes.has(contentDigest(offer.sha256)));
      for (const offer of removed.slice(0, 5)) {
        try { await request('downloadDismiss', { id: offer.id, sha256: offer.sha256 }); } catch { /* retry on a later ordinary poll */ }
        if (context !== state.snapshot.accountContext) return false;
      }
      const next = candidates.filter((offer) => !hashes.has(contentDigest(offer.sha256)) &&
        !intentionallyRemovedDownloadHashes.has(contentDigest(offer.sha256))).slice(0, 5);
      const changed = next.map((c) => c.id).join() !== (state.downloadOffers || []).map((c) => c.id).join();
      state.downloadOffers = next;
      if (result?.contentCheckPending || removed.length > 5) scheduleDownloadContinuation();
      if (changed) render();
      return changed;
    } catch {
      return false;
    } finally {
      state.downloadDiscoveryChecking = false;
    }
  }
  function renderDownloadOffers() {
    const hashes = presentDownloadHashes();
    const offers = (state.downloadOffers || []).filter((offer) => !hashes.has(contentDigest(offer.sha256)) &&
      !intentionallyRemovedDownloadHashes.has(contentDigest(offer.sha256)));
    if (!offers.length || state.preview) return '';
    return `<div class="banner neutral download-offers">${icon('download')}<div><p><strong>New mod download${offers.length === 1 ? '' : 's'} found</strong> in your Downloads folder. Review ${offers.length === 1 ? 'it' : 'each file'} before adding it. A single chosen PAK is enabled when it can be installed safely.</p><ul>${offers.map((offer) => `<li><span>${escape(offer.name)}</span><span class="inline-actions"><button type="button" class="button small primary" data-spotted-add="${escape(offer.id)}">Review &amp; import</button><button type="button" class="button small" data-spotted-ignore="${escape(offer.id)}">Ignore</button></span></li>`).join('')}</ul></div></div>`;
  }
  async function addSpottedDownload(id) {
    if (state.preview || state.busy || state.nativeDropActivity?.running || state.profileSession.active) return false;
    state.downloadOffers = (state.downloadOffers || []).filter((offer) => offer.id !== id);
    render();
    const result = await run('downloadImport', { id }, { refresh: false, progress: 'Checking the mod…' });
    if (result) await showImportResult(result);
  }
  async function ignoreSpottedDownload(id) {
    state.downloadOffers = (state.downloadOffers || []).filter((offer) => offer.id !== id);
    render();
    try {
      await request('downloadDismiss', { id });
    } catch {
      /* it is simply offered again next time */
    }
  }
  async function showImportResult(result) {
    const importContext = state.snapshot.accountContext;
    if (result?.batchReview) return reviewBatchImport(result);
    if (result?.batchResult) return showBatchImportResults(result);
    if (result?.choosePackages) return choosePackages(result);
    if (reviewDownloadedPackage(result)) return;
    const previous = new Set(importedMods().map((mod) => mod.id));
    const reported = Array.isArray(result?.added) ? result.added : Array.isArray(result?.mods)
      ? result.mods.filter((mod) => mod.imported && !previous.has(mod.id)).map((mod) => mod.id) : [];
    const refreshed = await refresh({ quiet: true });
    const candidate = reported.length === 1 && !previous.has(reported[0]) ? getMod(reported[0]) : null;
    const canEnable = refreshed && !result?.cancelled && result?.completed !== false && !result?.recoveryRequired &&
      candidate?.imported && !candidate.enabled && candidate.available !== false && !brokenInstallationRecord(candidate);
    if (canEnable && await enableImportedSelection(candidate.id, candidate.sha256, importContext)) return;
    const added = (result?.added || []).map((id) => modName(id));
    toast(
      added.length > 1
        ? `Added ${added.length} mods: ${added.join(', ')}. Switch them on under My mods.`
        : canEnable ? `${candidate.name} added to your library. ${importEnableBlockReason() || 'Open My mods to switch it on.'}`
          : result?.message || 'Mod added to your library. Open My mods to see its state.',
      'success'
    );
  }
  function importEnableBlockReason() {
    if (state.profileSession.active) return 'Wait for the profile session to finish before enabling it.';
    if (state.snapshot.gameRunning === true) return 'Close Days Gone before enabling it.';
    if (!state.snapshot.settings.gamePath) return 'Choose the game folder before enabling it.';
    if (state.snapshot.recoveryRequired || state.snapshot.libraryBackupRecovery?.pending || state.snapshot.saveRecoveryRequired)
      return 'Finish the pending recovery before enabling it.';
    if (state.snapshot.gameRunning !== false) return 'Check the game status before enabling it.';
    return '';
  }
  function importReviewContext() {
    return JSON.stringify([state.snapshot.accountContext || '', state.snapshot.settings?.gamePath || '', state.snapshot.activePath || '']);
  }
  // An explicit one-package import means "use this package". Keep normal native
  // enable/swap checks and never guess between several imported alternatives.
  async function enableImportedSelection(id, hash, context) {
    const latest = getMod(id);
    if (typeof hash !== 'string' || !/^[a-f0-9]{64}$/i.test(hash) ||
        state.busy || state.nativeDropActivity?.running || $('#modal').open || importEnableBlockReason() ||
        state.snapshot.accountContext !== context || !latest?.imported || latest.enabled ||
        latest.available === false || brokenInstallationRecord(latest) || latest.sha256 !== hash) return false;
    await toggleMod(id);
    return true;
  }
  let pendingBatchReview = null;
  let pendingBatchReviewData = null;
  function discardBatchReview() {
    const token = pendingBatchReview;
    pendingBatchReview = null;
    pendingBatchReviewData = null;
    if (token && !state.preview) request('batchImportDiscard', { batchToken: token }).catch(() => {});
  }
  function reviewBatchImport(data) {
    const items = Array.isArray(data.items) ? data.items.slice(0, 2048) : [];
    if (!data.batchToken || !items.length) {
      toast('No packages were available to review. Try another download.', 'warning');
      return false;
    }
    state.noticeQueue = state.noticeQueue.filter((notice) => notice.type !== 'batch-review');
    if (state.busy || $('#modal').open) {
      // Native inspection replaces the prior staged batch. Do not leave an
      // older queued review or a clickable confirmation for that expired plan.
      if (pendingBatchReview && pendingBatchReview !== data.batchToken) {
        const confirm = $('#batch-import-confirm'), note = $('#batch-selection-status');
        if (confirm) { confirm.disabled = true; confirm.dataset.superseded = 'true'; }
        if (note) note.textContent = 'A newer selection is ready. Close this review to check it.';
      }
      enqueueNotice({ type: 'batch-review', id: data.batchToken, review: data,
        message: 'Your mod downloads are ready to review.', fingerprint: 'batch-review:' + data.batchToken });
      return true;
    }
    discardBatchReview();
    const ready = items.filter((item) => item.status === 'ready' && typeof item.itemId === 'string');
    modal('Review your imports', `<p>Choose the packages to add. <strong>A single selected package is enabled after import</strong>, with a review if it conflicts. Several selected packages are stored only, so alternative versions are not switched on together.</p><p class="field-note">${Number(data.totalFiles) || 0} source file${Number(data.totalFiles) === 1 ? '' : 's'} · ${ready.length} ready · ${Number(data.duplicateCount) || 0} already in your library · ${Number(data.errorCount) || 0} need attention</p><div class="inline-actions"><button type="button" class="button small" id="batch-pick-all">Select ready packages</button><button type="button" class="button small" id="batch-pick-none">Clear selection</button></div><div class="batch-import-list">${items.map((item) => {
      const enabled = ready.includes(item), conflicts = Array.isArray(item.conflicts) ? item.conflicts : [],
        warnings = Array.isArray(item.warnings) ? item.warnings : [];
      return `<article class="batch-import-item" data-state="${escape(item.status)}"><label><input type="checkbox" name="batch-import-item" value="${escape(item.itemId || '')}" ${enabled ? 'checked' : 'disabled'}><span><strong>${escape(item.name || item.mod?.name || item.member || 'Package')}</strong><small>${escape(item.source || '')}${item.member ? ` · ${escape(item.member)}` : ''} · ${formatBytes(item.bytes)}</small>${item.mod?.version ? `<small>${escape(versionLabel(item.mod.version))}</small>` : ''}<span class="badge">${enabled ? 'Ready to add' : item.status === 'duplicate' ? 'Already in your library' : 'Could not be imported'}</span></span></label>${!enabled && item.message ? `<p class="field-note">${escape(item.message)}</p>` : ''}${item.mod?.variantGroup ? '<p class="field-note">One of several variants. Select just the one you want to use, or store several for later.</p>' : ''}${conflicts.length ? `<p class="field-note">Overlaps with ${conflicts.map((conflict) => escape(conflict.name || conflict.id || 'another package')).join(', ')}. The existing mod is kept unless you confirm a switch.</p>` : ''}${warnings.map((warning) => `<p class="field-note">${escape(typeof warning === 'string' ? warning : warning.message || '')}</p>`).join('')}</article>`;
    }).join('')}</div><p id="batch-selection-status" role="status">${ready.length} package${ready.length === 1 ? '' : 's'} selected.</p>`, `<button type="button" class="button" data-modal-close>Cancel</button><button type="button" class="button primary" id="batch-import-confirm" ${ready.length ? '' : 'disabled'}>${icon('plus')}Add selected packages</button>`);
    pendingBatchReview = data.batchToken;
    pendingBatchReviewData = data;
    const inputs = $$('#modal [name="batch-import-item"]'), confirm = $('#batch-import-confirm'), generation = modalContentGeneration,
      reviewContext = importReviewContext(), batchToken = data.batchToken;
    let used = false;
    const current = () => $('#modal').open && generation === modalContentGeneration && confirm === $('#batch-import-confirm') &&
      pendingBatchReview === batchToken && pendingBatchReviewData === data && reviewContext === importReviewContext();
    const update = () => {
      if (used || !current()) return;
      const count = inputs.filter((input) => !input.disabled && input.checked).length;
      const superseded = confirm.dataset.superseded === 'true';
      confirm.disabled = !count || superseded;
      confirm.innerHTML = `${icon('plus')}${count === 1 ? 'Add and enable' : 'Add selected packages'}`;
      $('#batch-selection-status').textContent = superseded ? 'A newer selection is ready. Close this review to check it.' : `${count} package${count === 1 ? '' : 's'} selected.`;
    };
    inputs.forEach((input) => input.onchange = update);
    $('#batch-pick-all').onclick = () => { if (!used && current()) { inputs.forEach((input) => { if (!input.disabled) input.checked = true; }); update(); } };
    $('#batch-pick-none').onclick = () => { if (!used && current()) { inputs.forEach((input) => input.checked = false); update(); } };
    update();
    confirm.onclick = async () => {
      const chosen = inputs.filter((input) => !input.disabled && input.checked).map((input) => input.value);
      if (used || !current() || !chosen.length || state.busy || state.loading || state.nativeDropActivity?.running || confirm.dataset.superseded === 'true') return false;
      used = true; confirm.disabled = true;
      pendingBatchReview = null;
      pendingBatchReviewData = null;
      $('#modal').close();
      const intended = chosen.length === 1 ? ready.find((item) => item.itemId === chosen[0])?.mod : null,
        importContext = state.snapshot.accountContext;
      const result = await run('batchImportConfirm', { batchToken, items: chosen, confirmed: true }, { refresh: false, progress: `Adding ${chosen.length} package${chosen.length === 1 ? '' : 's'}…` });
      if (result) {
        const refreshed = await refresh({ quiet: true }), row = result.items?.find((item) => item.itemId === chosen[0]);
        const singleAdded = refreshed && result.completed !== false && intended?.id && intended?.sha256 &&
          row?.status === 'imported' && !row.needsAttention;
        if (reviewContext !== importReviewContext()) return true;
        if (singleAdded && !batchAttentionCount(result) && !result.recoveryRequired &&
            await enableImportedSelection(intended.id, intended.sha256, importContext)) return;
        const activationNote = singleAdded
          ? result.recoveryRequired ? 'Finish the pending recovery before enabling this package.'
            : importEnableBlockReason() || (state.snapshot.accountContext !== importContext
              ? 'The selected game installation changed. Review this package in the current library before enabling it.'
              : batchAttentionCount(result) ? 'Check the other import results before enabling this package.'
                : 'Review the current package in My mods before enabling it.')
          : '';
        if ($('#modal').open) enqueueNotice({ type: 'batch-result', result, activationNote,
          message: 'Your import results are ready.', fingerprint: 'batch-result:' + batchToken });
        else showBatchImportResults(result, activationNote);
      }
    };
    return true;
  }
  function showBatchImportResults(data, activationNote = '') {
    const items = Array.isArray(data.items) ? data.items.slice(0, 2048) : [];
    const issues = batchAttentionCount(data);
    if (data.recoveryRequired) { state.snapshot.recoveryRequired = true; renderPlayReadiness(); }
    modal('Import results', `<p><strong>${Number(data.importedCount) || 0} mods added.</strong> ${activationNote ? 'The selected package is stored in your library.' : 'The batch does not enable packages.'} ${issues ? `${issues} need attention; check the messages below before continuing.` : 'Open My mods to see their current state and choose what to enable.'}</p>${activationNote ? `<p class="field-note" role="status">${escape(activationNote)}</p>` : ''}${data.recoveryRequired ? '<div class="banner warning"><p><strong>An interrupted file change needs recovery.</strong> Run Diagnostics before importing or changing more mods.</p></div>' : ''}<div class="batch-import-list">${items.map((item) => `<article class="batch-import-item" data-state="${escape(item.needsAttention ? 'failed' : item.status)}"><strong>${escape(item.name || 'Package')}</strong><span class="badge">${escape(item.status === 'imported' && item.needsAttention ? 'Added · needs attention' : { imported: 'Added to library', duplicate: 'Already in your library', skipped: 'Skipped', failed: 'Not added', error: 'Could not be checked', 'review-required': 'Needs a fresh review' }[item.status] || 'Review required')}</span>${item.message ? `<p class="field-note">${escape(item.message)}</p>` : ''}</article>`).join('')}</div>`, '<button type="button" class="button" data-modal-close>Close</button>' + (data.recoveryRequired ? '<button type="button" class="button primary" id="batch-results-diagnostics">Run Diagnostics</button>' : '<button type="button" class="button primary" id="batch-results-library">Open My mods</button>'));
    if (data.recoveryRequired) {
      $('#batch-results-diagnostics').onclick = () => { $('#modal').close(); loadDiagnosticReport(); };
      return true;
    }
    $('#batch-results-library').onclick = () => { $('#modal').close(); state.filter = 'stored'; state.filterChosen = true; navigate('mods'); };
    return true;
  }
  function batchAttentionCount(data) {
    const rows = Array.isArray(data?.items) ? data.items : [],
      count = rows.filter((item) => item.needsAttention === true || ['failed', 'error', 'review-required'].includes(item.status)).length,
      reported = Number.isSafeInteger(data?.attentionCount) && data.attentionCount >= 0 ? data.attentionCount : 0,
      failed = Number.isSafeInteger(data?.failedCount) && data.failedCount >= 0 ? data.failedCount : 0;
    return Math.max(count, reported, failed);
  }
  // A ZIP with several PAKs (tiers, optional extras, standalone pieces): tick the ones you want. PAKs of a known
  // release can be added together; from a ZIP the toolkit does not know, one PAK is checked at a time.
  function choosePackages(data) {
    const packages = Array.isArray(data.packages) ? data.packages : [];
    const several = !!data.known;
    const groups = new Set(packages.map((p) => p.variantGroup).filter(Boolean));
    const rows = packages
      .map((p) => {
        const done = !!p.inLibrary,
          blocked = p.available === false;
        return `<label class="pick-row${done || blocked ? ' done' : ''}"><input type="${several ? 'checkbox' : 'radio'}" name="zip-pick" value="${escape(p.file)}" data-group="${escape(p.variantGroup || '')}" ${done || blocked ? 'disabled' : ''}><span class="pick-text"><span class="pick-title"><strong>${escape(p.name || p.file)}</strong>${p.variantGroup ? '<em class="pick-one">pick one</em>' : ''}</span>${p.summary ? `<small>${escape(p.summary)}</small>` : ''}<small class="pick-file">${escape(p.file)} · ${formatBytes(p.bytes)}${done ? ' · already in your mods' : blocked ? ' · not supported' : ''}</small></span></label>`;
      })
      .join('');
    modal(
      `Choose from ${data.archive || 'this ZIP'}`,
      `<p>This ZIP has ${packages.length} PAK files. ${several ? 'Tick the ones you want; each one becomes its own mod you can switch on and off.' : 'Pick the one you want. The toolkit checks it before adding it; add the ZIP again for another one.'}${groups.size ? ' The ones marked “pick one” replace each other, so only one of them can be ticked.' : ''}</p><div class="pick-list">${rows}</div>`,
      `<button class="button" data-modal-close>Cancel</button><button class="button primary" id="zip-pick-confirm" disabled>${icon('plus')}Add and enable</button>`
    );
    const inputs = [...$$('#modal [name="zip-pick"]')],
      confirm = $('#zip-pick-confirm');
    const update = () => {
      const chosen = inputs.filter((input) => input.checked).length;
      confirm.disabled = !chosen;
      confirm.innerHTML = `${icon('plus')}${chosen > 1 ? `Store ${chosen} mods` : 'Add and enable'}`;
    };
    inputs.forEach(
      (input) =>
        (input.onchange = () => {
          if (input.checked && input.dataset.group)
            inputs.forEach((other) => {
              if (other !== input && other.dataset.group === input.dataset.group) other.checked = false;
            });
          update();
        })
    );
    confirm.onclick = async () => {
      const members = inputs.filter((input) => input.checked).map((input) => input.value);
      if (!members.length) return;
      $('#modal').close();
      const result = await run(
        'importPick',
        { pickToken: data.pickToken, members },
        { refresh: false, progress: members.length > 1 ? `Adding ${members.length} mods…` : 'Checking the mod…' }
      );
      if (result) await showImportResult(result);
    };
    return true;
  }
  // Dropping a downloaded PAK or ZIP onto the window adds it like Import mod: the same review opens first.
  const supportedImportName = (name) => /\.(pak|zip|zipx|7z|rar|tar|tgz|tbz2?|txz|tzst|gz|bz2|xz|lzma|lz|zst|zstd|lz4|cab|cpio|lha|lzh|ar|z)$/i.test(name || '');
  async function importDroppedFile(files, dropToken = null) {
    if (state.preview || state.busy || state.loading || state.nativeDropActivity?.running) return false;
    if ($('#modal').open) { toast('Close the current review before dropping more files.', 'info'); return false; }
    const target = state.view === 'workshop' ? 'workshop' : state.view === 'saves' ? 'saves' : 'mods',
      accepts = (file) => target === 'workshop' ? /\.pak$/i.test(file?.name || '') : target === 'saves' ? /\.sav$/i.test(file?.name || '') : supportedImportName(file?.name),
      chosen = [...(files || [])];
    if (!chosen.length || chosen.length > 64 || chosen.some((file) => !accepts(file))) {
      toast(target === 'workshop' ? 'Drop PAK files here to open a Workshop draft.' : target === 'saves' ? 'Drop .sav files here to keep them as a separate backup.' : 'Drop PAK files or supported mod archives here.', 'info'); return false;
    }
    if (target === 'workshop') return browseWorkshop(chosen, dropToken);
    if (target === 'saves') return importSaveFiles(chosen, dropToken);
    const result = await run('importDropped', { target, ...(dropToken ? { dropToken } : {}) }, { refresh: false, progress: 'Checking your mod files…', objects: dropToken ? undefined : chosen });
    if (result) await showImportResult(result);
    return !!result;
  }
  // Linux: the window catches the drop itself and reports the import result here.
  function handleDroppedImport(result) {
    const activity = state.nativeDropActivity;
    if (activity?.running) {
      activity.running = false;
      activity.requiresAttention = activityNeedsAttention(result.data);
      activity.outcome = result.ok === false ? 'failed' : 'complete';
      activity.message = result.ok === false ? errorText(result.error) : result.data?.batchReview ? 'Import ready for review' : 'Dropped files checked';
      activity.label = 'Dropped files';
      recordCompletedActivity(activity);
      if (!state.activity?.running || state.activity === activity) { state.activity = activity; renderActivity(); }
    }
    state.nativeDropActivity = null;
    activityRequests.delete('drop-import');
    renderPlayReadiness();
    if (result.ok === false) toast(errorText(result.error || 'The dropped file could not be added.'), 'error');
    else if (result.data) showImportResult(result.data);
  }
  function reviewDownloadedPackage(result) {
    const data = result?.review || result;
    if (!data?.reviewRequired) return false;
    const local = data.source === 'local-import' || isLocalMod(data.mod);
    if (state.busy || $('#modal').open) {
      enqueueNotice({
        type: 'package-review',
        id: data.reviewToken,
        message: local ? 'A mod is ready to add.' : 'A new Nexus package is ready for review.',
        review: data,
        fingerprint: 'package-review:' + data.reviewToken
      });
      return true;
    }
    const reviewContext = state.snapshot.accountContext, mod = data.mod || {},
      assets = mod.assets || [],
      conflicts = data.conflicts || [],
      warnings = data.warnings || [],
      adoption = data.adoption,
      direct = adoption?.mode === 'take-ownership',
      copyOnly = adoption?.mode === 'copy-only',
      installedFile = adoption?.file;
    const conflictNames = conflicts.map(
      (c) => c.name || (c.aName === mod.name ? c.bName : c.aName) || 'Installed package'
    );
    const intro = direct
      ? `<p><strong>${escape(mod.name || 'This PAK')}</strong> is already in your mods folder, installed by hand. The toolkit takes it over and it stays enabled. Disable will then remove the installed file and keep a copy in your library, like with your other mods.</p>`
      : copyOnly
        ? `<p><strong>${escape(mod.name || 'This PAK')}</strong> is already installed by hand and stays as it is. The toolkit adds its own copy to your library, switched off. To use that copy instead, delete the hand-installed file first.</p>`
        : `<p>Add <strong>${escape(mod.name || 'this mod')}</strong> to your mods. You can enable it right away or later.</p>`;
    const older = !direct && !copyOnly ? olderVersionOf(mod, local) : null;
    const update = older
      ? `<div class="banner neutral">${icon('download')}<p><strong>This looks like a newer version of ${escape(older.name)}.</strong> ${older.enabled ? `Replace old version switches ${escape(older.name)} off and this one on.` : 'Replace old version adds this one and switches it on.'} The old version stays in your library.</p></div>`
      : '';
    const readme = mod.readme
      ? `<details class="finding-evidence mod-readme"><summary>Notes from the download</summary><pre>${escape(mod.readme)}</pre></details>`
      : '';
    const conflictNote = conflicts.length
      ? `<div class="banner warning">${icon('alert')}<p><strong>Changes the same game files as ${escape(conflictNames.join(', '))}.</strong> ${direct ? 'Both are installed right now, so only one of them works. Keep only one of them enabled.' : 'Only one of them can work at a time.'}</p></div>`
      : '<p class="inline-muted">No conflicts with your installed mods.</p>';
    const technical = `<details class="finding-evidence"><summary>Technical details</summary><p>${local ? 'The toolkit checked the file you picked and uses that checked copy, even if the original changes later. It cannot confirm who made it.' : 'Downloaded from the connected Nexus release and checked.'}</p>${(data.notes || []).map((note) => `<p class="field-note">${escape(note)}</p>`).join('')}${installedFile ? `<dl class="detail-grid"><dt>Installed file</dt><dd><code>${escape(installedFile.path || 'Path not supplied')}</code></dd><dt>Size</dt><dd>${formatBytes(installedFile.bytes)}</dd><dt>SHA-256</dt><dd><code>${escape(installedFile.sha256 || mod.sha256 || '')}</code></dd></dl>` : ''}<dl class="detail-grid"><dt>Package</dt><dd><code>${escape(mod.packageFile || '')}</code></dd><dt>SHA-256</dt><dd><code>${escape(mod.sha256 || '')}</code></dd><dt>Source</dt><dd>${local ? 'Local import · user-selected file' : `Nexus Mods · mod ${escape(mod.provenance?.modId || 994)} · file ${escape(mod.provenance?.fileId || 'unknown')}`}</dd></dl>${conflicts
      .map(
        (conflict) =>
          `<details class="finding-evidence"><summary>${escape(conflict.aName || conflict.bName || conflict.name || 'Installed package')} · ${escape(conflict.count ?? conflict.assets?.length ?? 0)} shared assets</summary><ul>${(
            conflict.assets || []
          )
            .slice(0, 64)
            .map((asset) => `<li>${escape(typeof asset === 'string' ? asset : asset.path || asset.name || '')}</li>`)
            .join(
              ''
            )}</ul>${conflictSampleNote(conflict) ? `<p class="field-note">${escape(conflictSampleNote(conflict))}</p>` : ''}</details>`
      )
      .join(
        ''
      )}<details class="finding-evidence"><summary>Game files it changes · ${Math.min(64, assets.length)} shown of ${escape(mod.assetCount ?? mod.count ?? assets.length)}</summary><ul>${assets
      .slice(0, 64)
      .map((asset) => `<li>${escape(typeof asset === 'string' ? asset : asset.path || asset.name || '')}</li>`)
      .join(
        ''
      )}</ul></details><p class="field-note">${assets.length > 64 || mod.evidenceTruncated ? 'Only the first 64 game files are shown here. ' : ''}${local ? 'Local imports use the same conflict checks as other mods. Nexus update checks are not provided for local files.' : 'This is a newly inspected Nexus release, rather than an author-audited entry bundled with the loader.'}</p></details>`;
    modal(
      direct
        ? 'Manage this installed PAK?'
        : copyOnly
          ? 'Copy this installed PAK to the library?'
          : local
            ? 'Add this mod?'
            : 'Review this new Nexus package',
      `<div class="mod-detail-heading">${modArtwork(mod, { detail: true })}<div><strong>${escape(mod.name || 'Mod')}</strong>${versionLabel(mod.version) ? ' · ' + escape(versionLabel(mod.version)) : local ? ' · Local file' : ''}</div></div>${intro}${update}${warnings.length ? `<div class="banner warning"><p>${warnings.map((item) => escape(typeof item === 'string' ? item : item.message || 'Review required')).join('<br>')}</p></div>` : ''}${conflictNote}${readme}${technical}`,
      `<button class="button" data-modal-close>Not now</button><button class="button ${direct || copyOnly ? 'primary' : ''}" id="review-import-confirm">${icon('plus')}${direct ? 'Manage installed file' : 'Add to my library'}</button>${direct || copyOnly ? '' : older ? `<button class="button primary" id="review-import-replace">${icon('refresh')}Replace old version</button>` : `<button class="button primary" id="review-import-enable">${icon('check')}Add and enable</button>`}`
    );
    pendingPackageReview = { review: data, context: reviewContext };
    const generation = modalContentGeneration, confirmButton = $('#review-import-confirm'), reviewToken = data.reviewToken,
      installation = importReviewContext();
    let used = false;
    const current = () => $('#modal').open && generation === modalContentGeneration && confirmButton === $('#review-import-confirm') &&
      pendingPackageReview?.review?.reviewToken === reviewToken && pendingPackageReview.context === reviewContext && installation === importReviewContext();
    // Most people import a mod to play with it: one click adds it and switches it on (conflicts are offered as a swap).
    // Replacing: add the new version, switch the old one off, switch the new one on; the old one comes back on if the
    // new one cannot be switched on.
    const replaceButton = older && $('#review-import-replace');
    if (replaceButton)
      replaceButton.onclick = async () => {
        if (!current() || !await confirmButton.onclick()) return;
        const added = mod.id && getMod(mod.id);
        if (installation !== importReviewContext() || $('#modal').open || state.snapshot.accountContext !== reviewContext || !added?.imported || added.enabled ||
            added.sha256 !== mod.sha256 || importEnableBlockReason()) return;
        const wasOn = !!getMod(older.id)?.enabled;
        if (
          wasOn &&
          (await run('disable', { id: older.id }, { refresh: false, progress: `Switching ${older.name} off…` })) == null
        )
          return refresh({ quiet: true });
        if (installation !== importReviewContext() || $('#modal').open || state.snapshot.accountContext !== reviewContext) return;
        const enabled = await run(
          'enable',
          { id: added.id },
          {
            progress: `Switching ${added.name} on…`,
            success: `Updated: ${added.name} is on. The old version stays in your library.`
          }
        );
        if (installation !== importReviewContext() || state.snapshot.accountContext !== reviewContext) return;
        if (enabled == null && wasOn) {
          await run('enable', { id: older.id }, { refresh: false });
          await refresh({ quiet: true });
        }
      };
    const enableButton = $('#review-import-enable');
    if (enableButton)
      enableButton.onclick = async () => {
        if (!current() || !await confirmButton.onclick()) return;
        const added = mod.id && getMod(mod.id);
        if (installation === importReviewContext() && added?.imported && !added.enabled &&
            !await enableImportedSelection(added.id, mod.sha256, reviewContext))
          toast(`Added to your library. ${importEnableBlockReason() || 'Review the current package before enabling it.'}`, 'warning');
      };
    confirmButton.onclick = async () => {
      if (used || !current() || state.busy || state.loading || state.nativeDropActivity?.running) return false;
      used = true; confirmButton.disabled = true;
      pendingPackageReview = null;
      $('#modal').close();
      const confirmed = await run(
        'confirmReviewedImport',
        { reviewToken, confirmed: true },
        {
          success: direct
            ? 'Installed PAK is now managed and remains enabled.'
            : copyOnly
              ? 'Library copy added. The original remains external; review its removal before enabling the managed copy.'
              : local
                ? 'Mod added to your library.'
                : 'Inspected Nexus package added to your library.'
        }
      );
      if (confirmed) render();
      return confirmed;
    };
    return true;
  }
  function nexusTier(account = state.nexus) {
    return account.tierVerified === false || account.premium === null
      ? 'Membership unavailable'
      : account.premium === true
        ? 'Premium'
        : 'Free account';
  }
  function nexusCanDownload() {
    return (
      state.nexus.connected &&
      state.nexus.tierVerified !== false &&
      state.nexus.premium === true &&
      !state.nexus.offline
    );
  }
  function includeInspectedUpdates() {
    const updates = [...(state.downloads.updates || [])];
    for (const mod of state.snapshot.mods) {
      if (mod.identityStatus !== 'nexus-inspected') continue;
      const modId = nexusModId(mod),
        currentFileId = nexusFileId(mod);
      if (!currentFileId) continue;
      const edges = (state.downloads.fileUpdates || []).filter((edge) => nexusModId(edge) === modId);
      const reached = new Set([currentFileId]);
      let changed = true;
      for (let depth = 0; changed && depth < 128; depth++) {
        changed = false;
        for (const edge of edges) {
          const from = Number(edge.oldFileId),
            to = Number(edge.newFileId);
          if (reached.has(from) && !reached.has(to)) {
            reached.add(to);
            changed = true;
          }
        }
      }
      if (changed || reached.size > 256) continue;
      const candidates = (state.downloads.inventory || []).filter(
        (file) =>
          nexusModId(file) === modId &&
          nexusFileId(file) !== currentFileId &&
          reached.has(nexusFileId(file)) &&
          !edges.some((edge) => Number(edge.oldFileId) === nexusFileId(file) && reached.has(Number(edge.newFileId)))
      );
      if (candidates.length !== 1) continue;
      const latest = candidates[0];
      const index = updates.findIndex((update) => update.id === mod.id);
      const update = {
        id: mod.id,
        name: mod.name,
        modId,
        nexusModId: modId,
        currentFileId,
        currentVersion: mod.version || 'Version unknown',
        fileId: nexusFileId(latest),
        version: latest.version || 'Version not supplied',
        updateAvailable: true,
        auditedInstallable: false
      };
      if (index < 0) updates.push(update);
      else updates[index] = update;
    }
    state.downloads.updates = updates;
  }
  function latestCatalogRelease(mod) {
    let latest = mod;
    const visited = new Set([mod.id]);
    for (let step = 0; step < 32; step++) {
      if (!latest.supersededBy) return latest === mod ? null : latest;
      const next = state.snapshot.mods.find((item) => item.id === latest.supersededBy);
      if (!next || next.available === false || visited.has(next.id)) return null;
      visited.add(next.id);
      latest = next;
    }
    return null;
  }
  function installedUpdate(mod) {
    if (isLocalMod(mod)) return null;
    const installed =
      mod.imported || mod.enabled || mod.owned || state.snapshot.installed.some((file) => file.catalogId === mod.id && file.owned);
    if (!installed) return null;
    const latest = latestCatalogRelease(mod);
    if (latest) return {
      id: mod.id,
      name: mod.name,
      nexusModId: nexusModId(latest),
      modId: nexusModId(latest),
      currentVersion: mod.version,
      currentFileId: mod.nexusFileId,
      fileId: latest.nexusFileId,
      version: latest.version,
      updateAvailable: true,
      catalogId: latest.id
    };
    return (state.downloads.updates || []).find((update) => update.id === mod.id && update.updateAvailable === true) || null;
  }
  function installedUpdates() {
    return state.snapshot.mods.map((mod) => ({ mod, update: installedUpdate(mod) })).filter((item) => item.update);
  }
  function acceptReleaseCheck(result) {
    state.downloads = { ...state.downloads, ...result, checkedAt: result.checkedAt || new Date().toISOString() };
    includeInspectedUpdates();
    const updates = installedUpdates();
    if (updates.length)
      enqueueNotice({
        type: 'updates',
        id: 'new-releases',
        message: `${updates.length} installed mod${updates.length === 1 ? ' has' : 's have'} a newer published release.`,
        updates,
        fingerprint:
          'updates:' +
          updates
            .map(({ mod, update }) => `${nexusModId(mod)}:${mod.id}:${update.currentFileId}:${update.fileId}`)
            .sort()
            .join('|')
      });
  }
  function renderUpdateSummary() {
    const updates = installedUpdates();
    if (!updates.length) return '';
    return `<div class="banner warning">${icon('download')}<p><strong>${updates.length} installed mod${updates.length === 1 ? ' has' : 's have'} an update available.</strong> ${updates.map(({ mod, update }) => `${escape(mod.name)}: installed ${escape(update.currentVersion || mod.version || 'version unknown')} → latest ${escape(update.version || 'version not supplied')}`).join(' · ')}. New files require package inspection before installation.</p><button class="button small" data-view="downloads">Review releases</button></div>`;
  }
  function browserLoginAvailable() {
    return state.nexus.browserLoginAvailable !== false;
  }
  function nexusAccountMarkup() {
    if (localEdition())
      return `<button class="account-card" id="sidebar-toolkit-source" title="Open the toolkit’s source code on GitHub"><span class="account-avatar disconnected">${icon('code')}</span><span class="account-copy"><strong>Open source</strong><small>Drifter’s Toolkit on GitHub</small><span class="account-connect">View source ${icon('external')}</span></span></button>`;
    const account = state.nexus;
    if (!account.connected)
      return `<button class="account-card" id="sidebar-nexus-connect" title="${browserLoginAvailable() ? 'Connect your Nexus Mods account in your browser' : 'Connect your Nexus Mods account'}"><span class="account-avatar disconnected">${icon('user')}</span><span class="account-copy"><strong>Not connected</strong><small>Nexus Mods account</small><span class="account-connect">${browserLoginAvailable() ? 'Connect in browser' : 'Connect account'} ${icon('external')}</span></span></button>`;
    const name = String(account.name || 'Connected account').trim() || 'Connected account';
    const initials = Array.from(
      name
        .split(/\s+/)
        .map((part) => Array.from(part)[0])
        .slice(0, 2)
        .join('')
    )
      .slice(0, 2)
      .join('')
      .toLocaleUpperCase();
    return `<button class="account-card" id="sidebar-nexus-settings" title="${escape(name)} · ${nexusTier(account)}"><span class="account-avatar ${account.tierVerified === false || account.premium === null ? 'unverified' : account.premium ? 'premium' : 'free'}">${escape(initials)}</span><span class="account-copy"><strong>${escape(name)}</strong><small class="account-membership ${account.tierVerified === false || account.premium === null ? 'unverified' : account.premium ? 'premium' : 'free'}">${nexusTier(account)}</small><span class="account-connect">Nexus Mods account</span></span></button>`;
  }
  function handleNexusStatusChanged(result) {
    const data = result.data;
    if (!data || typeof data !== 'object') return;
    const safe = {};
    for (const key of ['connected', 'premium', 'tierVerified', 'offline', 'verificationPending', 'requiresReconnect'])
      if (typeof data[key] === 'boolean' || (key === 'premium' && data[key] === null)) safe[key] = data[key];
    if (typeof data.name === 'string') safe.name = data.name.slice(0, 160);
    state.nexus = safe;
    renderNexusAccount();
    if (state.view === 'downloads' && !$('#modal').open) render();
    if (safe.connected) checkAutomaticReleases();
  }
  function renderNexusAccount() {
    const panel = $('#nexus-account');
    if (!panel) return;
    panel.innerHTML = nexusAccountMarkup();
    const connect = $('#sidebar-nexus-connect');
    if (connect) connect.onclick = connectNexus;
    const settings = $('#sidebar-nexus-settings');
    if (settings) settings.onclick = () => navigate('settings');
    const source = $('#sidebar-toolkit-source');
    if (source) source.onclick = () => run('openModdingResource', { resourceId: 'toolkit-source' }, { refresh: false });
    const version = $('#build-version');
    if (version) version.onclick = () => showChangelog();
  }
  function rememberMainFocus() {
    const active = document.activeElement, main = $('#main-content');
    if (!active || !main?.contains?.(active) || active === main) return null;
    return { element: active, tag: active.tagName, id: active.id,
      attrs: Array.from(active.attributes || []).filter((item) => item.name.startsWith('data-')).map((item) => [item.name, item.value]),
      start: active.selectionStart, end: active.selectionEnd };
  }
  function restoreMainFocus(saved) {
    if (!saved || $('#modal').open) return;
    const main = $('#main-content');
    const target = saved.id ? document.getElementById?.(saved.id) : saved.attrs.length
      ? Array.from(main.querySelectorAll?.('button, input, select, textarea, a, summary') || []).find((item) => item.tagName === saved.tag && saved.attrs.every(([key, value]) => item.getAttribute(key) === value)) : null;
    if (!target || !main.contains?.(target) || target.disabled) return;
    target.focus?.({ preventScroll: true });
    if (typeof saved.start === 'number' && typeof target.setSelectionRange === 'function') {
      try { target.setSelectionRange(saved.start, saved.end); } catch { /* Some input types do not have a caret. */ }
    }
  }
  function rememberSettingsDraft() {
    const main = $('#main-content');
    if (state.view !== 'settings' || main.dataset.renderView !== 'settings' ||
        main.dataset.renderContext !== String(state.snapshot.accountContext || '')) return [];
    return [['game-path', 'game-path'], ['steam-launch', 'steam-launch-setting'], ['auto-manage', 'auto-manage-setting']]
      .filter(([key, id]) => $('#' + key + '-save')?.hidden === false && $('#' + id))
      .map(([key, id]) => ({ key, id, value: key === 'game-path' ? $('#' + id).value : $('#' + id).getAttribute('aria-checked') }));
  }
  function restoreSettingsDraft(draft) {
    for (const item of draft) {
      const control = $('#' + item.id);
      if (!control) continue;
      if (item.key === 'game-path') control.value = item.value;
      else control.setAttribute('aria-checked', item.value);
      markPendingSave(item.key);
    }
  }
  function render() {
    const savedFocus = rememberMainFocus();
    const settingsDraft = rememberSettingsDraft();
    syncLibraryDock();
    renderStatusTicker();
    const versionLabel = $('#build-version');
    if (
      versionLabel &&
      typeof state.snapshot.appVersion === 'string' &&
      /^\d+\.\d+\.\d+$/.test(state.snapshot.appVersion)
    )
      versionLabel.textContent = 'v' + state.snapshot.appVersion;
    renderNexusAccount();
    updateNoticeBadge();
    const labels = {
      overview: 'Overview',
      mods: 'My mods',
      downloads: 'Downloads',
      folders: 'Folders',
      conflicts: 'Conflicts',
      diagnostics: 'Diagnostics',
      profiles: 'Profiles',
      saves: 'Saves',
      workshop: 'Workshop',
      help: 'Field guide',
      settings: 'Settings'
    };
    $$('.nav-item').forEach((el) => {
      const current = el.dataset.view === state.view;
      el.classList.toggle('active', current);
      el.title = labels[el.dataset.view] || el.textContent.trim();
      el.setAttribute('aria-label', labels[el.dataset.view] || el.textContent.trim());
      if (current) el.setAttribute('aria-current', 'page');
      else el.removeAttribute('aria-current');
    });
    $('#breadcrumb-current').textContent = state.view === 'mods' && state.characterAppearanceView ? 'CHARACTER APPEARANCE' : labels[state.view]?.toUpperCase() || 'OVERVIEW';
    renderPlayReadiness();
    renderGameControls();
    $('#nav-mod-count').textContent = String(importedMods().length);
    $('#nav-conflict-count').textContent = String(conflictGroupCount());
    const con = $('#connection-state');
    con.classList.toggle('preview', state.preview);
    con.classList.toggle('sandbox', !!state.snapshot.sandbox);
    con.title = state.snapshot.sandbox
      ? 'This test session uses isolated mod folders. The title bar also marks the sandbox.'
      : state.preview
        ? 'Interface preview; no native package management is connected.'
        : 'Current toolkit connection';
    con.innerHTML = `<i></i>${state.preview ? 'Interface preview' : state.snapshot.sandbox ? 'Sandbox · game folders isolated' : state.snapshot.settings.gamePath ? 'Game folder set' : 'Choose game folder'}`;
    if (state.loading) {
      $('#main-content').innerHTML =
        '<div class="loading-screen"><span class="spinner"></span>Reading your mod library…</div>';
      return;
    }
    const views = {
      overview: renderOverview,
      mods: renderMods,
      downloads: renderDownloads,
      folders: renderFolders,
      conflicts: renderConflicts,
      diagnostics: renderDiagnosticPage,
      profiles: renderProfiles,
      saves: renderSaves,
      workshop: renderWorkshop,
      help: renderHelp,
      settings: renderSettings
    };
    $('#main-content').innerHTML =
      `${renderProfileSessionBanner()}${renderGameRestartBanner()}${renderToolkitReleaseBanner()}${renderUpdateBanner()}${renderCatalogBanner()}${renderFinderBanner()}${renderDownloadOffers()}${(views[state.view] || renderOverview)()}`;
    $('#main-content').dataset.renderView = state.view;
    $('#main-content').dataset.renderContext = String(state.snapshot.accountContext || '');
    bindContent();
    bindHeroSlideshow();
    restoreSettingsDraft(settingsDraft);
    if (state.view === 'settings') updateGamePathError();
    applySessionLocks();
    restoreMainFocus(savedFocus);
    syncCurrentSavesPoll();
  }
  const statusTicker = { paused: storedValue('drifter.statusTipsPaused', false) === true };
  function statusTickerText() {
    return {
      overview: 'Press / to search your mod collection.   •   Mods that replace the same assets need a compatibility check.',
      mods: 'Enabled loads with the game.   •   Stored only keeps inactive library packages together.   •   Workshop creates a separate PAK for testing.',
      downloads: 'New downloads are offered for review.   •   One chosen PAK enables after import when setup checks pass.',
      folders: 'Use the installation your launcher starts.   •   The toolkit checks its folders before changing PAKs.',
      conflicts: 'Overlapping assets show a conflict.   •   An incomplete check needs a closer look before enabling a mod.',
      diagnostics: 'Run diagnostics after a change.   •   Review exported findings before you share them.',
      profiles: 'Profiles keep their own mod selections.   •   Keep the toolkit open until your normal setup has been restored.',
      saves: 'Current save files are read from the game’s save folder.   •   Backups keep separate copies for a reviewed restore.',
      workshop: 'Edits update a temporary draft.   •   Build a separate PAK before testing it in the game.   •   Originals stay in place.',
      help: 'Search for a symptom or an error message.   •   The Field guide works offline.',
      settings: 'Appearance settings can be saved separately from the selected game folder.'
    }[state.view] || 'Check your setup before changing mods.';
  }
  function renderStatusTicker() {
    const ticker = $('#status-ticker'), track = $('#status-ticker-track'), control = $('#status-ticker-pause'),
      viewport = $('#status-ticker-viewport');
    if (!ticker || !track || !control || !viewport) return;
    ticker.hidden = state.loading;
    const text = statusTickerText(), reduced = heroReducedMotion();
    if (track.textContent !== text) track.textContent = text;
    viewport.setAttribute('aria-label', text);
    viewport.title = text;
    ticker.classList.toggle('is-paused', statusTicker.paused || reduced);
    control.disabled = reduced;
    control.setAttribute('aria-pressed', String(statusTicker.paused || reduced));
    control.setAttribute('aria-label', reduced ? 'Moving tips paused for reduced motion' : statusTicker.paused ? 'Resume moving tips' : 'Pause moving tips');
    control.title = reduced ? 'Moving tips paused for reduced motion' : statusTicker.paused ? 'Resume moving tips' : 'Pause moving tips';
    const appearance = `${statusTicker.paused}:${reduced}`;
    if (control.dataset.tickerAppearance !== appearance) {
      control.dataset.tickerAppearance = appearance;
      control.innerHTML = statusTicker.paused || reduced ? icon('play') : '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" aria-hidden="true"><path d="M8 5v14M16 5v14"/></svg>';
    }
    control.onclick = () => {
      if (heroReducedMotion()) return;
      statusTicker.paused = !statusTicker.paused;
      storeValue('drifter.statusTipsPaused', statusTicker.paused);
      renderStatusTicker();
    };
  }
  const heroScenes = [
    { key: 'road', title: 'The broken road', caption: 'Oregon · Pacific Northwest' },
    { key: 'forest', title: 'Into the pines', caption: 'Backroads · Morning mist' },
    { key: 'pass', title: 'Over the pass', caption: 'High country · First snowfall' },
    { key: 'outpost', title: 'One last stop', caption: 'The foothills · Dusk' },
    { key: 'fogwood', title: 'Something in the trees', caption: 'Deep forest · After midnight' },
    { key: 'tunnel', title: 'The way through', caption: 'Overgrown tunnel · Low cloud' },
    { key: 'lookout', title: 'Above the silence', caption: 'Abandoned lookout · Last light' },
    { key: 'sawmill', title: 'The mill stands still', caption: 'Old timber country · Evening mist' },
    { key: 'lake', title: 'Across the water', caption: 'The lakeshore · Cold morning' },
    { key: 'quarantine', title: 'Nobody at the gate', caption: 'Deserted checkpoint · Rainfall' },
    { key: 'camp', title: 'The fire went out', caption: 'Forest camp · Before dawn' },
    { key: 'burn', title: 'What the fire left', caption: 'Burnt woodland · Gathering storm' },
    { key: 'freakers', title: 'Movement in the fog', caption: 'The treeline · Blue hour' },
    { key: 'breaker', title: 'A road best avoided', caption: 'Logging country · After dark' }
  ];
  function restoredHeroSlideshow() {
    const key = storedValue('drifter.heroScene', 'road');
    return { index: Math.max(0, heroScenes.findIndex((scene) => scene.key === key)),
      paused: storedValue('drifter.heroPaused', false) === true };
  }
  const heroSlideshow = { ...restoredHeroSlideshow(), timer: null, hover: false };
  function rememberHeroSlideshow() {
    storeValue('drifter.heroScene', heroScenes[heroSlideshow.index].key);
    storeValue('drifter.heroPaused', heroSlideshow.paused);
  }
  function heroReducedMotion() {
    return document.documentElement.classList.contains('reduce-motion') ||
      !!window.matchMedia?.('(prefers-reduced-motion: reduce)')?.matches;
  }
  function heroControlMarkup() {
    const reduced = heroReducedMotion();
    return `<div class="hero-scene-controls" aria-label="Overview slideshow"><button type="button" id="hero-previous" aria-label="Previous landscape" title="Previous landscape">${icon('back')}</button><button type="button" id="hero-pause" aria-label="${reduced ? 'Slideshow paused for reduced motion' : heroSlideshow.paused ? 'Resume slideshow' : 'Pause slideshow'}" title="${reduced ? 'Automatic slides are paused for reduced motion' : heroSlideshow.paused ? 'Resume slideshow' : 'Pause slideshow'}" aria-pressed="${heroSlideshow.paused || reduced}"${reduced ? ' disabled' : ''}>${reduced || heroSlideshow.paused ? icon('play') : '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" aria-hidden="true"><path d="M8 5v14M16 5v14"/></svg>'}</button><button type="button" id="hero-next" aria-label="Next landscape" title="Next landscape">${icon('arrow')}</button></div>`;
  }
  function renderOverviewHero() {
    const scene = heroScenes[heroSlideshow.index];
    return `<section id="overview-hero" class="hero" aria-label="Days Gone mod overview"><div class="hero-scenes" aria-hidden="true"><div class="hero-scene hero-scene-${scene.key} is-current"></div><div class="hero-scene"></div></div><div class="hero-content"><div class="eyebrow"><span class="eyebrow-line"></span>DAYS GONE · MOD MANAGEMENT</div><h1>Your mods.<br><em>Your setup.</em></h1><p>Install the mods you want, see what conflicts,<br>and keep track of each change.</p><div class="hero-actions">${button('Import mod', 'import', 'plus', 'primary')}${button('Open mods folder', 'open-folder', 'folder', 'subtle')}</div></div><div class="hero-corner"><span id="hero-scene-title">${escape(scene.title)}</span><small id="hero-scene-caption">${escape(scene.caption)}</small>${heroControlMarkup()}</div></section>`;
  }
  function advanceHeroSlide(direction = 1) {
    heroSlideshow.index = (heroSlideshow.index + direction + heroScenes.length) % heroScenes.length;
    const scene = heroScenes[heroSlideshow.index];
    const frames = $$('#overview-hero .hero-scene'), current = frames.find((frame) => frame.classList.contains('is-current')),
      incoming = frames.find((frame) => frame !== current);
    if (incoming) {
      incoming.className = `hero-scene hero-scene-${scene.key}`;
      incoming.classList.add('is-current');
      if (current) current.classList.remove('is-current');
    }
    const title = $('#hero-scene-title'), caption = $('#hero-scene-caption');
    if (title) title.textContent = scene.title;
    if (caption) caption.textContent = scene.caption;
    rememberHeroSlideshow();
    syncHeroSlideshow();
  }
  function syncHeroSlideshow() {
    if (heroSlideshow.timer !== null) clearTimeout(heroSlideshow.timer);
    heroSlideshow.timer = null;
    const control = $('#hero-pause'), reduced = heroReducedMotion();
    const controlState = `${heroSlideshow.paused}:${reduced}`;
    if (control && control.dataset.heroPauseState !== controlState) {
      control.dataset.heroPauseState = controlState;
      control.disabled = reduced;
      control.setAttribute('aria-pressed', String(heroSlideshow.paused || reduced));
      control.setAttribute('aria-label', reduced ? 'Slideshow paused for reduced motion' : heroSlideshow.paused ? 'Resume slideshow' : 'Pause slideshow');
      control.title = reduced ? 'Automatic slides are paused for reduced motion' : heroSlideshow.paused ? 'Resume slideshow' : 'Pause slideshow';
      control.innerHTML = heroSlideshow.paused || reduced ? icon('play') : '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" aria-hidden="true"><path d="M8 5v14M16 5v14"/></svg>';
    }
    if (state.loading || state.view !== 'overview' || heroSlideshow.paused || heroReducedMotion() ||
        document.hidden || state.nativeWindowHidden || document.documentElement.classList.contains('app-hidden') ||
        heroSlideshow.hover || $('#modal')?.open ||
        ($('#overview-hero')?.contains(document.activeElement) && document.activeElement?.matches?.(':focus-visible'))) return;
    heroSlideshow.timer = setTimeout(() => { heroSlideshow.timer = null; advanceHeroSlide(); }, 12000);
  }
  function bindHeroSlideshow() {
    if (state.view !== 'overview' || state.loading) { syncHeroSlideshow(); return; }
    const hero = $('#overview-hero');
    if (!hero) return;
    const controls = $('#overview-hero .hero-scene-controls');
    heroSlideshow.hover = controls?.matches?.(':hover') === true;
    if (controls) {
      controls.onpointerenter = () => { heroSlideshow.hover = true; syncHeroSlideshow(); };
      controls.onpointerleave = () => { heroSlideshow.hover = false; syncHeroSlideshow(); };
    }
    hero.addEventListener('focusin', () => syncHeroSlideshow());
    hero.addEventListener('focusout', (event) => {
      if (!hero.contains(event.relatedTarget)) {
        // Focusout fires before the browser installs the new active element.
        if (heroSlideshow.timer !== null) clearTimeout(heroSlideshow.timer);
        heroSlideshow.timer = setTimeout(syncHeroSlideshow, 0);
      }
    });
    $('#hero-previous').onclick = () => advanceHeroSlide(-1);
    $('#hero-next').onclick = () => advanceHeroSlide(1);
    $('#hero-pause').onclick = () => {
      if (heroReducedMotion()) return;
      heroSlideshow.paused = !heroSlideshow.paused;
      rememberHeroSlideshow();
      syncHeroSlideshow();
    };
    syncHeroSlideshow();
  }
  function renderOverview() {
    const imported = importedMods(),
      enabled = enabledMods(),
      conflicts = state.snapshot.conflicts,
      external = externalMods();
    return `${renderOverviewHero()}${renderGettingStarted() || renderLoadingSetupBanner()}${renderUpdateSummary()}<div class="stats-grid">${stat('cube', imported.length, 'Mods in your library', imported.length ? 'info' : 'calm')}${stat('check', enabled.length, 'Currently enabled', enabled.length ? 'success' : 'calm')}${stat('shield', conflictGroupCount(), conflictGroupCount() === 1 ? 'Conflict' : 'Conflicts', conflictGroupCount() ? 'warning' : 'calm')}${stat('folder', external.length, 'Installed by hand', external.length ? 'info' : 'calm')}</div><section><div class="section-heading"><div><h2>Your mod collection <span>${libraryMods().filter(hasMod).length} mods</span></h2><p>The mods you have and the ones you can get, with their state.</p></div><button class="link-button" data-view="mods">View all mods ${icon('arrow')}</button></div>${state.preview ? '<p class="unavailable-note">Preview mode · Package states will come from your game scan in the desktop app.</p>' : ''}${libraryToolbar(false)}<div class="mod-list" id="mod-list">${renderModRows(true)}</div></section><div class="overview-bottom"><div class="info-card"><div class="info-card-head">${icon('shield')}Check for conflicts</div><p>The toolkit compares the game files each mod changes. Mods that change the same files cannot all work at once, so keep one of them enabled.</p><button class="text-action" data-view="conflicts">Review compatibility ${icon('arrow')}</button></div><div class="info-card tip"><div class="info-card-head">${icon('compass')}Installing your first mod?</div><p>Read the installation steps, check common conflicts or look up an error.</p><button class="text-action" data-view="help">Take a look at the field guide ${icon('arrow')}</button></div></div>`;
  }
  function renderGameControls() {
    const restartable = state.snapshot.gameRunning || state.profileSession.active;
    $('#launch-button').disabled = state.preview || !state.snapshot.settings.gamePath || state.gameRestart.pending;
    $('#launch-button').innerHTML =
      icon(restartable ? 'refresh' : 'play') +
      (state.gameRestart.pending ? 'Restart queued' : restartable ? 'Queue restart' : 'Launch game');
    $('#launch-button').title = state.gameRestart.pending
      ? 'Waiting for Days Gone to close normally'
      : restartable
        ? 'Restart after you close Days Gone normally'
        : !state.snapshot.settings.gamePath
          ? 'Choose the game folder in Settings first'
          : 'Start Days Gone';
  }
  function handleGameProcessChanged(event) {
    const data = event?.data, snapshot = state.snapshot;
    if (state.preview || !data || typeof data.processKnown !== 'boolean' ||
        data.accountContext !== snapshot.accountContext ||
        data.gamePath !== (snapshot.settings?.gamePath || '') ||
        data.activePath !== snapshot.activePath ||
        (data.processKnown && typeof data.gameRunning !== 'boolean')) return false;
    const next = data.processKnown ? data.gameRunning : null;
    if (snapshot.gameRunning === next) return false;
    const wasRunning = snapshot.gameRunning === true;
    snapshot.gameRunning = next;
    if (data.processKnown)
      snapshot.diagnostics = (snapshot.diagnostics || []).filter((issue) => issue.code !== 'process-check');
    renderGameControls();
    renderPlayReadiness();
    applyExternalLocks();
    applyRecordLocks();
    applyModActionLocks();
    updateSavesGameStatus();
    if (wasRunning && next === false) {
      state.diagnosticStale = true;
      state.crashWatch.lastCheck = 0;
      pollCrashNotice();
      if (state.view === 'saves' && state.savesTab === 'backups' && state.saveBackups) loadSaveBackups();
      if (state.view === 'saves' && state.savesTab === 'current') loadCurrentSaves();
    }
    return true;
  }
  // This describes the latest scan, not a promise about gameplay compatibility. Launch still does its own check.
  function playReadiness() {
    const snapshot = state.snapshot,
      startup = snapshot.setup?.startup,
      issues = snapshot.diagnostics || [],
      result = (message, tone = 'info', label = '', action = '') => ({ message, tone, label, action });
    if (state.loading) return result('Checking your setup…');
    if (state.preview) return result('Preview · setup not checked');
    if (snapshot.sandbox) return result('Sandbox · game folders isolated');
    if (state.gameRestart.pending) return result('Restart queued', 'info', 'Cancel', 'restart-cancel');
    if (snapshot.saveRecoveryRequired)
      return result('A save restore needs recovery', 'warning', 'Review saves', 'save-backups');
    if (snapshot.libraryBackupRecovery?.pending)
      return result('A library restore needs recovery', 'warning', 'Review library', 'library-backup-recovery');
    if (snapshot.recoveryRequired)
      return result('An unfinished change needs recovery', 'warning', 'Review', 'diagnose');
    if (state.profileSession.active && state.profileSession.status === 'launching')
      return result('Waiting for Days Gone to start', 'info', 'Review', 'profiles');
    if (state.profileSession.active && !snapshot.gameRunning)
      return result('Original files need restoring', 'warning', 'Review', 'profiles');
    if (!snapshot.settings.gamePath) return result('Choose your game folder', 'warning', 'Browse', 'browse-game');
    if (snapshot.gameRunning === true) return result('Days Gone is running');
    if (finderState().active) return result('Mod finder in progress', 'info', 'Continue', 'finder-open');
    if (state.busy || state.nativeDropActivity?.running) return result('Finishing the current action…');
    if (filesystemDirty || filesystemRefreshRunning) return result('Checking changed mod files…');
    if (state.scanFailed) return result('The last scan could not finish', 'warning', 'Check', 'diagnose');
    if (snapshot.gameRunning !== false || issues.some((issue) => issue.code === 'process-check'))
      return result('Game status needs checking', 'info', 'Check', 'diagnose');
    if (startup?.status === 'needs-preparation')
      return result('Mod loading needs setting up', 'warning', 'Set up', 'loading-setup');
    if (snapshot.setup?.state !== 'ready' || !['prepared', 'external-prepared'].includes(startup?.status))
      return result('Mod-loading setup needs a look', 'warning', 'Review', 'folders');
    if (detectedConflictCount()) return result('Conflicting mods are enabled', 'warning', 'Review', 'conflicts');
    if (issues.some((issue) => ['warning', 'error'].includes(issue.level)))
      return result('The mod scan needs a look', 'warning', 'Review', 'diagnose');
    return result('Ready to play', 'success');
  }
  function renderPlayReadiness() {
    const status = $('#play-readiness');
    if (!status) return;
    const ready = playReadiness();
    const signature = JSON.stringify(ready);
    if (status.dataset.readiness === signature) return;
    status.dataset.readiness = signature;
    status.className = `play-readiness ${ready.tone}`;
    status.innerHTML = `<span role="status" aria-live="polite">${icon(ready.tone === 'success' ? 'check' : ready.tone === 'warning' ? 'alert' : 'info')}${escape(ready.message)}</span>${ready.action ? `<button type="button" class="readiness-action">${escape(ready.label)}</button>` : ''}`;
    status.title =
      ready.tone === 'success'
        ? 'The current scan found no loading or PAK conflicts. Some gameplay issues can only be checked in the game.'
        : ready.message;
    const action = $('.readiness-action', status);
    if (ready.action && action)
      action.onclick = () =>
        ['profiles', 'folders', 'conflicts'].includes(ready.action) ? navigate(ready.action) : handleAction(ready.action);
  }
  // Days Gone loads PAK mods only after its startup cache is set aside. Until then, the pages where mods are
  // switched on say so plainly and offer the same reviewed preparation as the Folders page.
  function renderLoadingSetupBanner() {
    const startup = state.directories?.startup || state.snapshot.setup?.startup;
    if (state.preview || state.profileSession.active || startup?.status !== 'needs-preparation') return '';
    return `<div class="banner warning loading-setup-banner">${icon('alert')}<div><p><strong>Mods will not load yet.</strong> Days Gone needs one startup file set aside before it loads PAK mods. The toolkit renames it and can put it back any time.</p><div class="inline-actions">${button('Set up mod loading', 'loading-setup', 'wrench', 'small primary')}</div></div></div>`;
  }
  // `tone` colours a tile by what it counts (danger, warning, success, info, or calm for nothing to report).
  // The four first steps, shown on the Overview until all are done. Each open step has its own button.
  function gettingStartedSteps() {
    const startup = state.directories?.startup || state.snapshot.setup?.startup;
    return [
      {
        title: 'Find your game',
        done: !!state.snapshot.settings?.gamePath,
        hint: state.snapshot.settings?.gamePath ? 'Days Gone was found.' : 'Choose the folder with DaysGone.exe.',
        action: button('Choose game folder', 'browse-game', 'folder', 'small')
      },
      {
        title: 'Set up mod loading',
        done: !!state.snapshot.settings?.gamePath && startup?.status !== 'needs-preparation',
        hint: 'Days Gone needs one startup file set aside before it loads PAK mods.',
        action: button('Set up mod loading', 'loading-setup', 'wrench', 'small')
      },
      {
        title: 'Add a mod',
        done: importedMods().length > 0 || enabledMods().length > 0,
        hint: 'Download one on Nexus, then drop it here or choose Import mod.',
        action: button('Import mod', 'import', 'plus', 'small')
      },
      {
        title: 'Switch it on',
        done: enabledMods().length > 0,
        hint: 'Switch a mod on under My mods, then start the game.',
        action: '<button type="button" class="button small" data-view="mods">Open My mods</button>'
      }
    ];
  }
  function maybeShowWhatsNew() {
    const notes = window.WHATS_NEW,
      version = state.snapshot.appVersion;
    if (state.preview || !notes || !version || notes.version !== version) return false;
    const seen = storedValue('drifter.whatsNewSeen', null);
    if (seen === version) return false;
    storeValue('drifter.whatsNewSeen', version);
    if (seen === null && !importedMods().length && !externalMods().length) return false;
    enqueueNotice({ type: 'whats-new', id: 'whats-new-' + version, fingerprint: 'whats-new-' + version });
    return true;
  }
  function showWhatsNew() {
    const notes = window.WHATS_NEW || { version: state.snapshot.appVersion || '', items: [] };
    modal(
      `What’s new in ${notes.version}`,
      `<div class="release-notes">${notes.items.map((item) => `<p class="changelog-note">${escape(item)}</p>`).join('')}</div>`,
      `<button class="button" id="whats-new-changelog">${icon('book')}All versions</button><button class="button primary" data-modal-close>Got it</button>`
    );
    const generation = modalContentGeneration;
    $('#whats-new-changelog').onclick = () => {
      if ($('#modal').open && generation === modalContentGeneration) showChangelog(notes.version, showWhatsNew);
    };
    return true;
  }
  function changelogReleases() {
    const releases = new Map();
    const notes = window.WHATS_NEW;
    // The bundled archive has the full notes; What's new is a shorter summary in some older builds.
    for (const release of [...(Array.isArray(window.CHANGELOG_HISTORY) ? window.CHANGELOG_HISTORY : []), notes]) {
      if (!release || typeof release.version !== 'string' || !/^\d{1,6}\.\d{1,6}\.\d{1,6}$/.test(release.version) ||
          !Array.isArray(release.items) || releases.has(release.version)) continue;
      const items = release.items.filter((item) => typeof item === 'string' && item.trim()).slice(0, 100);
      if (items.length) releases.set(release.version, { version: release.version, items });
    }
    return [...releases.values()].sort((a, b) => {
      const left = a.version.split('.').map(Number), right = b.version.split('.').map(Number);
      return right[0] - left[0] || right[1] - left[1] || right[2] - left[2];
    }).slice(0, 100);
  }
  function changelogEntry(release) {
    return release
      ? `<h3>Version ${escape(release.version)}</h3><div class="release-notes">${release.items.map((item) => `<p class="changelog-note">${escape(item)}</p>`).join('')}</div>`
      : '<p>No release notes are bundled with this copy.</p>';
  }
  function changelogVersionLabel(release) {
    return `<span class="changelog-version-number">${escape(release.version)}</span>${release.version === state.snapshot.appVersion ? '<span class="changelog-installed">Installed</span>' : ''}`;
  }
  function showChangelog(version = '', back = null) {
    const releases = changelogReleases();
    const selected = releases.find((release) => release.version === version) ||
      releases.find((release) => release.version === state.snapshot.appVersion) || releases[0];
    modal(
      'Changelog',
      `<p>Read the changes from earlier versions here. These notes are included with the toolkit and work offline.</p>${releases.length ? `<div class="changelog-version"><span id="changelog-version-label">Version</span><details id="changelog-picker" class="changelog-picker"><summary id="changelog-version" aria-describedby="changelog-version-label"><span id="changelog-selected" class="changelog-selected"><span id="changelog-selected-version" class="changelog-version-number">${escape(selected.version)}</span><span id="changelog-installed-badge" class="changelog-installed"${selected.version === state.snapshot.appVersion ? '' : ' hidden'}>Installed</span></span><span class="changelog-chevron" aria-hidden="true">${icon('arrow')}</span></summary><nav class="changelog-options" aria-label="Changelog versions">${releases.map((release, index) => `<button type="button" id="changelog-pick-${index}" class="changelog-option" data-changelog-version="${escape(release.version)}" aria-current="${release === selected ? 'true' : 'false'}" aria-controls="changelog-panel-${index}">${changelogVersionLabel(release)}<span class="changelog-option-check">${icon('check')}</span></button>`).join('')}</nav></details></div>` : ''}<p id="changelog-status" class="sr-only" role="status" aria-live="polite"></p><section id="changelog-entry" class="changelog-entry">${releases.length ? releases.map((release, index) => `<div id="changelog-panel-${index}" class="changelog-entry-panel"${release === selected ? '' : ' hidden'}>${changelogEntry(release)}</div>`).join('') : changelogEntry(null)}</section>`,
      `${typeof back === 'function' ? '<button class="button" id="changelog-back">Back</button>' : ''}<button class="button primary" data-modal-close>Close</button>`
    );
    if (releases.length) {
      const generation = modalContentGeneration, dialog = $('#modal'), content = $('#modal-content'),
        entry = $('#changelog-entry'), selectedLabel = $('#changelog-selected-version'),
        installedBadge = $('#changelog-installed-badge'), status = $('#changelog-status');
      const picker = $('#changelog-picker'), summary = $('#changelog-version'),
        menu = picker.querySelector?.('.changelog-options'),
        options = releases.map((release, index) => $(`#changelog-pick-${index}`)),
        panels = releases.map((release, index) => $(`#changelog-panel-${index}`));
      let selectedIndex = releases.indexOf(selected);
      panels.forEach((panel, index) => { panel.hidden = index !== selectedIndex; });
      entry.dataset.version = selected.version;
      selectedLabel.textContent = selected.version;
      installedBadge.hidden = selected.version !== state.snapshot.appVersion;
      const current = () => dialog.open && generation === modalContentGeneration &&
        $('#changelog-picker') === picker && $('#changelog-entry') === entry;
      const fitOptions = () => {
        if (!current() || !picker.open || !menu) return;
        const body = picker.closest?.('.modal-body');
        if (!body) return;
        const anchor = summary.getBoundingClientRect(), bounds = body.getBoundingClientRect();
        if (!(anchor.width > 0 && bounds.height > 0)) return;
        const scale = summary.offsetWidth > 0 ? anchor.width / summary.offsetWidth : 1;
        const below = Math.min(bounds.bottom, window.innerHeight) - anchor.bottom - 6;
        const above = anchor.top - Math.max(0, bounds.top) - 6;
        const upward = below < 80 && above > below;
        menu.dataset.placement = upward ? 'above' : 'below';
        const available = Math.max(0, (upward ? above : below) - 8) / scale;
        const maximum = 15 * (parseFloat(getComputedStyle(document.documentElement).fontSize) || 14);
        menu.style.maxHeight = `${Math.max(32, Math.min(maximum, available))}px`;
      };
      picker.addEventListener('toggle', fitOptions);
      options.forEach((option, index) => {
        option.setAttribute('aria-current', String(releases[index] === selected));
        option.onclick = () => {
          // A queued click can arrive after Close/Back replaced the dialog.
          // Keep it scoped to its original nodes instead of querying a new modal.
          if (!current()) return;
          if (selectedIndex !== index) {
            // Keep parsed notes and accessibility nodes stable while browsing.
            // Announce only the version, rather than the entire release again.
            panels[selectedIndex].hidden = true;
            panels[index].hidden = false;
            selectedIndex = index;
            entry.dataset.version = releases[index].version;
            selectedLabel.textContent = releases[index].version;
            installedBadge.hidden = releases[index].version !== state.snapshot.appVersion;
            status.textContent = `Showing changes for version ${releases[index].version}.`;
            options.forEach((item) => item.setAttribute('aria-current', String(item === option)));
          }
          // Move focus before collapsing its currently focused option.
          summary.focus({ preventScroll: true });
          picker.open = false;
        };
      });
      picker.onkeydown = (event) => {
        if (!current()) return;
        if (event.key === 'Escape' && picker.open) {
          event.preventDefault(); event.stopPropagation(); summary.focus({ preventScroll: true }); picker.open = false; return;
        }
        const index = options.indexOf(document.activeElement);
        let next = null;
        if (event.key === 'ArrowDown') next = index < 0 ? options.findIndex((item) => item.getAttribute('aria-current') === 'true') : (index + 1) % options.length;
        else if (event.key === 'ArrowUp') next = index < 0 ? options.length - 1 : (index + options.length - 1) % options.length;
        else if (event.key === 'Home' && index >= 0) next = 0;
        else if (event.key === 'End' && index >= 0) next = options.length - 1;
        if (next !== null) { event.preventDefault(); picker.open = true; fitOptions(); options[Math.max(0, next)].focus(); }
      };
      picker.addEventListener('focusout', (event) => { if (current() && !picker.contains(event.relatedTarget)) picker.open = false; });
      content.onclick = (event) => { if (current() && !picker.contains(event.target)) picker.open = false; };
    }
    if (typeof back === 'function') {
      const generation = modalContentGeneration;
      $('#changelog-back').onclick = () => {
        if ($('#modal').open && generation === modalContentGeneration) back();
      };
    }
    return true;
  }
  // Mod-loading state in plain words: green when mods load, amber when a step is missing, red when files changed.
  function startupBadge(status) {
    const [label, tone] = {
      prepared: ['Mods can load', 'success'],
      'external-prepared': ['Mods can load', 'success'],
      'needs-preparation': ['Not set up yet', 'warning'],
      missing: ['Startup file not found', 'warning'],
      changed: ['Needs a look', 'danger'],
      unverified: ['Needs a look', 'danger'],
      'recovery-pending': ['Unfinished change', 'danger'],
      'sandbox-protected': ['Read only', 'info']
    }[status] || ['Not checked', 'info'];
    return `<span class="badge tone-badge ${tone}">${escape(label)}</span>`;
  }
  function renderGettingStarted() {
    if (state.preview) return '';
    const steps = gettingStartedSteps();
    if (steps.every((step) => step.done)) return '';
    const next = steps.findIndex((step) => !step.done);
    return `<section class="getting-started mt-24" aria-label="Getting started"><div class="section-heading"><div><h2>Getting started</h2><p>Four steps and you’re playing with mods.</p></div></div><ol class="start-steps">${steps.map((step, index) => `<li class="${step.done ? 'done' : index === next ? 'next' : ''}"><span class="step-mark">${step.done ? icon('check') : index + 1}</span><div><strong>${escape(step.title)}</strong><small>${escape(step.done ? 'Done' : step.hint)}</small>${!step.done && index === next ? step.action : ''}</div></li>`).join('')}</ol></section>`;
  }
  function stat(name, value, label, tone = '') {
    return `<div class="stat-card${tone ? ' tone-' + tone : ''}"><div class="stat-icon">${icon(name)}</div><div><div class="stat-num">${value}</div><div class="stat-label">${escape(label)}</div></div></div>`;
  }
  // The mods a tab lists before search and category are applied.
  function inTab(m) {
    if (state.filter === 'disabled') state.filter = 'stored';
    return (
      (state.characterAppearanceView && state.filter === 'all') ||
      (state.filter === 'available' && !hasMod(m)) ||
      (state.filter === 'enabled' && m.enabled) ||
      (state.filter === 'stored' && m.imported && !m.enabled && !brokenInstallationRecord(m)) ||
      (state.filter === 'review' && brokenInstallationRecord(m))
    );
  }
  // Categories that have mods in the current tab, with their counts; a chosen category always stays listed.
  function categoryOptions() {
    const counts = new Map();
    for (const m of libraryMods()) if (inTab(m)) counts.set(modCategory(m), (counts.get(modCategory(m)) || 0) + 1);
    if (state.category !== 'all' && !counts.has(state.category)) counts.set(state.category, 0);
    return [...counts.entries()].sort((a, b) => a[0].localeCompare(b[0]));
  }
  function libraryToolbar(full = true) {
    if (state.filter === 'disabled') state.filter = 'stored';
    const categories = categoryOptions(), tabMods = state.characterAppearanceView ? libraryMods().filter(characterAppearance) : libraryMods(),
      enabled = tabMods.filter((mod) => mod.enabled).length, stored = tabMods.filter((mod) => mod.imported && !mod.enabled && !brokenInstallationRecord(mod)).length,
      needsReview = tabMods.filter(brokenInstallationRecord).length;
    return `<div class="library-toolbar"><div class="tabs" role="group" aria-label="Filter packages">${state.characterAppearanceView ? `<button class="tab ${state.filter === 'all' ? 'active' : ''}" data-filter="all" aria-pressed="${state.filter === 'all'}">All character mods <span>${tabMods.length}</span></button>` : ''}<button class="tab ${state.filter === 'available' ? 'active' : ''}" data-filter="available" aria-pressed="${state.filter === 'available'}">Available mods <span>${tabMods.filter((m) => !hasMod(m)).length}</span></button><button class="tab ${state.filter === 'enabled' ? 'active' : ''}" data-filter="enabled" aria-pressed="${state.filter === 'enabled'}">Enabled <span>${enabled}</span></button><button class="tab ${state.filter === 'stored' ? 'active' : ''}" data-filter="stored" aria-pressed="${state.filter === 'stored'}">Stored only <span>${stored}</span></button>${needsReview || state.filter === 'review' ? `<button class="tab ${state.filter === 'review' ? 'active' : ''}" data-filter="review" aria-pressed="${state.filter === 'review'}">Needs review <span>${needsReview}</span></button>` : ''}</div><div class="toolbar-right">${
      full
        ? `${state.characterAppearanceView ? '' : `<select class="category-select" id="category-filter" aria-label="Filter by category"><option value="all">All categories</option>${categories.map(([c, count]) => `<option value="${escape(c)}" ${state.category === c ? 'selected' : ''}>${escape(c)} · ${count}</option>`).join('')}</select>`}<select class="category-select" id="sort-filter" aria-label="Sort mods">${Object.entries(
            sortOrders
          )
            .map(
              ([value, label]) => `<option value="${value}" ${state.sort === value ? 'selected' : ''}>${label}</option>`
            )
            .join('')}</select>`
        : ''
    }<div class="search-wrap">${icon('search')}<input type="search" class="search-input" id="mod-search" placeholder="Find a mod…" value="${escape(state.search)}" aria-label="Find a mod"><kbd>/</kbd></div></div></div><p class="field-note library-state-note">${state.filter === 'available' ? 'Available mods are listed in the catalog; they are not installed or stored in your library yet.' : state.filter === 'enabled' ? 'These managed packages are installed for the game to load. Hand-installed files are listed separately below.' : state.filter === 'review' ? 'These installation records changed or went missing. Review them before changing the files.' : state.filter === 'all' ? 'All recognised character packages, including available releases and your local library.' : 'Every inactive local library package is kept here, including mods you switched off. These copies are not loaded by the game.'}</p>`;
  }
  // Small per-window conveniences kept in browser storage (never needed for anything to work).
  function storedValue(key, fallback) {
    try {
      const value = window.localStorage?.getItem(key);
      return value == null ? fallback : JSON.parse(value);
    } catch {
      return fallback;
    }
  }
  function storeValue(key, value) {
    try {
      window.localStorage?.setItem(key, JSON.stringify(value));
    } catch {
      /* storage is optional */
    }
  }
  function readStoredSort() {
    const sort = storedValue('drifter.sort', 'recommended');
    return ['recommended', 'name', 'recent', 'enabled', 'category'].includes(sort) ? sort : 'recommended';
  }
  // When each mod first appeared in the library, for "Recently added". Mods present when this was first used count
  // as older (time 1); later additions get the time they showed up.
  function rememberAddedTimes() {
    const times = storedValue('drifter.addedAt', null);
    const first = !times;
    const next = { ...(times || {}) };
    let changed = first;
    for (const m of state.snapshot.mods || [])
      if (hasMod(m) && !next[m.id]) {
        next[m.id] = first ? 1 : Date.now();
        changed = true;
      }
    if (changed) storeValue('drifter.addedAt', next);
  }
  const sortOrders = {
    recommended: 'Recommended order',
    name: 'Name A–Z',
    recent: 'Recently added',
    enabled: 'Enabled first',
    category: 'Category'
  };
  function compareMods(a, b) {
    const priority = { 'no-recoil': 0, 'xp-randomizer': 1, 'hidden-arsenal': 2 };
    const recommended =
      (priority[a.id] ?? (a.available === false ? 20 : 9)) - (priority[b.id] ?? (b.available === false ? 20 : 9));
    const byName = String(a.name || '').localeCompare(String(b.name || ''));
    if (state.sort === 'name') return byName;
    const favorites = Number(personalModMeta(b.id).favorite) - Number(personalModMeta(a.id).favorite);
    if (favorites) return favorites;
    if (state.sort === 'category') return modCategory(a).localeCompare(modCategory(b)) || byName;
    if (state.sort === 'enabled') return Number(!!b.enabled) - Number(!!a.enabled) || recommended;
    if (state.sort === 'recent') {
      const times = storedValue('drifter.addedAt', {}) || {};
      return (times[b.id] || 0) - (times[a.id] || 0) || recommended;
    }
    return recommended;
  }
  function filteredMods() {
    return libraryMods()
      .filter(
        (m) =>
          inTab(m) &&
          (!state.favoritesOnly || personalModMeta(m.id).favorite) &&
          (state.category === 'all' || modCategory(m) === state.category) &&
          `${m.name} ${m.summary} ${modCategory(m)} ${characterAppearance(m)?.character || ''} ${characterAppearance(m)?.label || ''} ${(m.tags || []).join?.(' ') || m.tags || ''}`
            .toLowerCase()
            .includes(state.search.toLowerCase())
      )
      .sort(compareMods);
  }
  function getModRow(m) {
    return !brokenInstallationRecord(m) && m.available !== false && !m.imported && !isLocalMod(m);
  }
  function modActionBlockReason(mod, enabling = false) {
    if (!mod) return 'This package is no longer in the current library.';
    if (state.busy || state.nativeDropActivity?.running) return 'Wait for the current operation to finish.';
    if (state.profileSession.active) return 'Wait until the profile session restores your normal setup.';
    if (state.snapshot.libraryBackupRecovery?.pending || state.snapshot.recoveryRequired || state.snapshot.saveRecoveryRequired)
      return 'Finish the pending recovery before changing mods.';
    if (state.snapshot.gameRunning === true) return 'Close Days Gone before changing mods.';
    if (state.snapshot.gameRunning === null) return 'Game status is unavailable. Refresh before changing mods.';
    if (enabling && !state.snapshot.settings?.gamePath) return 'Choose the game folder in Settings before enabling this mod.';
    if (enabling && mod?.available === false) return mod.unsupportedReason || 'This release is unavailable. Read its notes.';
    return '';
  }
  function canRemoveMod(mod) {
    return !!mod && (brokenInstallationRecord(mod) ? mod.canRemoveLibrary === true
      : mod.imported === true || mod.enabled === true || mod.canRemoveLibrary === true);
  }
  function removeModButton(mod) {
    if (!canRemoveMod(mod)) return '';
    const reason = modActionBlockReason(mod);
    return `<button type="button" class="button small row-remove" data-remove-mod="${escape(mod.id)}" aria-label="Remove ${escape(mod.name)}"${reason ? ` disabled title="${escape(reason)}"` : ''}>${icon('trash')}Remove</button>`;
  }
  function renderModRowActions(mod) {
    const id = escape(mod.id), details = `<button type="button" class="button small row-details" data-details="${id}" aria-label="Details for ${escape(mod.name)}" aria-expanded="${state.detailId === mod.id}">${icon('info')}Details</button>`;
    if (manualNativeMod(mod))
      return `<button type="button" class="button small row-details" data-details="${id}" aria-label="Manual setup for ${escape(mod.name)}" aria-expanded="${state.detailId === mod.id}">${icon('book')}View setup</button>`;
    if (brokenInstallationRecord(mod))
      return `${removeModButton(mod)}<button type="button" class="button small" data-details="${id}">${icon('report')}Review record</button>`;
    if (mod.available === false && !mod.enabled)
      return `${removeModButton(mod)}<button type="button" class="button small" data-details="${id}">${icon('book')}View notes</button>`;
    if (mod.imported || mod.enabled) {
      const reason = modActionBlockReason(mod, !mod.enabled);
      return `<button type="button" class="button small mod-primary-action ${mod.enabled ? '' : 'primary'}" data-toggle="${id}" aria-label="${mod.enabled ? 'Disable' : 'Enable'} ${escape(mod.name)}"${reason ? ` disabled title="${escape(reason)}"` : ''}>${icon(mod.enabled ? 'close' : 'check')}${mod.enabled ? 'Disable' : 'Enable'}</button>${details}${removeModButton(mod)}<small class="mod-action-reason" data-mod-action-note="${id}"${reason ? '' : ' hidden'}>${escape(reason)}</small>`;
    }
    if (mod.canRemoveLibrary === true) return `${details}${removeModButton(mod)}`;
    return `${isLocalMod(mod) ? `<button type="button" class="button small" data-action="import">${icon('plus')}Import file</button>` : `<button type="button" class="button small" data-download="${id}">${icon('download')}Get mod</button>`}${details}`;
  }
  function applyModActionLocks() {
    $$('[data-remove-mod]').forEach((control) => {
      const mod = getMod(control.dataset.removeMod), reason = modActionBlockReason(mod);
      control.disabled = !canRemoveMod(mod) || !!reason;
      control.title = reason || 'Remove this mod from the toolkit';
    });
    $$('[data-toggle]').forEach((control) => {
      const mod = getMod(control.dataset.toggle), reason = modActionBlockReason(mod, !mod?.enabled);
      control.disabled = !mod || !!reason;
      control.title = reason || `${mod.enabled ? 'Disable' : 'Enable'} ${mod.name}`;
    });
    $$('[data-mod-action-note]').forEach((note) => {
      const mod = getMod(note.dataset.modActionNote), reason = modActionBlockReason(mod, !mod?.enabled);
      note.textContent = reason;
      note.hidden = !reason;
    });
    const selected = getMod(state.detailId);
    if (selected && !$('#mod-details-panel').hidden) {
      const stateLabel = $('#detail-activation-state');
      if (stateLabel) stateLabel.textContent = selected.imported || selected.enabled ? activationLabel(selected) : statusFor(selected).label;
      for (const [selector, enabling] of [['#detail-toggle', !selected.enabled], ['#detail-remove', false], ['#detail-import', false], ['#detail-restore-library', false]]) {
        const control = $(selector);
        if (!control) continue;
        const reason = modActionBlockReason(selected, enabling);
        control.disabled = !!reason || selector === '#detail-remove' && !canRemoveMod(selected) ||
          selector === '#detail-restore-library' && selected.canRestoreLibraryCopy !== true;
        control.title = reason;
        if (selector === '#detail-toggle') {
          const label = selected.enabled ? 'Disable' : 'Enable';
          if (control.dataset.activationLabel !== label) {
            control.dataset.activationLabel = label;
            control.innerHTML = `${icon(selected.enabled ? 'close' : 'check')}${label}`;
          }
        }
      }
      const note = $('#detail-action-reason'), reason = modActionBlockReason(selected, !selected.enabled);
      if (note) { note.textContent = reason; note.hidden = !reason; }
    }
  }
  function renderModRows(short = false) {
    const mods = filteredMods();
    const shown = mods.slice(0, short ? 5 : state.modLimit);
    if (!shown.length) {
      if (state.characterAppearanceView) {
        const anyPackages = libraryMods().some(characterAppearance);
        return `<div class="empty-state compact">${icon('grid')}<h3>${anyPackages ? 'No character packages in this view' : 'No character packages found'}</h3><p>${anyPackages ? 'Try all character mods, or clear your search to see the other packages.' : 'Import a character PAK to manage it here when its game files are recognised. A mod name alone does not identify a skin.'}</p><div class="inline-actions justify-center">${anyPackages ? '<button type="button" class="button small" data-clear-filters>Show all character mods</button>' : button('Import mods', 'import', 'plus', 'small')}<button type="button" class="button small" data-action="character-all-mods">All mods</button></div></div>`;
      }
      if (!state.snapshot.mods.length)
        return `<div class="empty-state compact">${icon('search')}<h3>No catalog entries loaded</h3><p>Import a PAK to start your library, or check that the application’s catalog file is present.</p></div>`;
      // Say why the list is empty: a search or category, or simply nothing in this tab yet.
      const filtered = !!state.search || state.category !== 'all' || state.favoritesOnly;
      const [title, text] = filtered
        ? ['No matching mods', 'Nothing in this tab matches your search or category.']
        : state.filter === 'available'
          ? ['You have every mod', 'Every mod from the list is already in your library. New releases show up here.']
          : state.filter === 'enabled'
            ? ['No managed mods enabled', 'Enable a package under Stored only, or get one under Available mods.']
            : state.filter === 'review'
              ? ['No installation records need review', 'The recorded installed files match the last scan.']
              : ['No stored-only packages', 'Inactive packages stay here after you import them or switch them off.'];
      return `<div class="empty-state compact">${icon('search')}<h3>${title}</h3><p>${text}</p>${filtered ? '<div class="inline-actions justify-center"><button type="button" class="button small" data-clear-filters>Show all</button></div>' : ''}</div>`;
    }
    return (
      shown
        .map((m) => {
          return `<article class="mod-row${state.detailId === m.id ? ' is-selected' : ''}" data-mod-row="${escape(m.id)}">${modArtwork(m)}<div><button class="mod-name" data-details="${escape(m.id)}" aria-expanded="${state.detailId === m.id}">${escape(m.name)}</button>${personalModMeta(m.id).favorite ? '<span class="badge personal-favorite">Favourite</span>' : ''}${isLocalMod(m) && !m.catalogMatch ? '<span class="badge local-origin">Added from a file</span>' : versionLabel(m.version) ? `<span class="mod-version">${escape(versionLabel(m.version))}</span>` : ''}<p class="mod-summary">${escape(m.summary?.length > 150 ? m.summary.slice(0, 147) + '…' : m.summary)}</p><div class="mod-meta"><span class="mod-category">${escape(modCategory(m))}</span>${m.variant ? `<span>· ${escape(m.variant)}</span>` : m.variantGroup ? '<span>· Choose one variant</span>' : ''}${m.packageCount ? `<span>· ${escape(m.packageCount)} packages</span>` : ''}</div></div>${modStatusMarkup(m)}<div class="mod-row-actions">${renderModRowActions(m)}</div></article>`;
        })
        .join('') +
      (!short && shown.length < mods.length
        ? `<div class="mt-20"><p class="field-note">Showing ${shown.length} of ${mods.length} matching mods.</p>${button('Show 50 more mods', 'mods-more', 'plus')}</div>`
        : '')
    );
  }
  function renderCharacterLibraryIntro() {
    const packages = libraryMods().filter(characterAppearance), previews = packages.filter((mod) => characterAppearance(mod).preview),
      shown = previews.slice(0, 8);
    return `<section class="character-library-intro"><div><h2>Skins, outfits and character files</h2><p>${packages.length} recognised package${packages.length === 1 ? '' : 's'} · ${previews.length} supplied preview${previews.length === 1 ? '' : 's'}. Enable a package from its row. Overlaps and declared alternatives are reviewed before switching.</p><p class="field-note">Previews are static reference images, not a 3D character viewer. Packages without a supplied picture still appear below. Import other character PAKs to manage them here when their files are recognised.</p></div>${shown.length ? `<div class="character-preview-gallery">${shown.map((mod) => `<button type="button" class="character-preview-card" data-character-preview="${escape(mod.id)}" aria-label="View supplied preview for ${escape(mod.name)}"><img src="${escape(characterAppearance(mod).preview)}" alt="${escape(mod.name)} model preview" loading="lazy" decoding="async"><span><strong>${escape(mod.name)}</strong><small>View supplied preview ${icon('external')}</small></span></button>`).join('')}</div>` : '<p class="field-note">No model previews are supplied for these packages. You can still read their details and manage them below.</p>'}</section>`;
  }
  function renderMods() {
    if (state.characterAppearanceView) return `${header('MOD LIBRARY', 'Character appearance.', 'Manage character packages and view the reference pictures supplied for them.', button('All mods', 'character-all-mods', 'back') + button('Library tools', 'library-tools', 'layers') + button('Import mods', 'import', 'plus', 'primary'))}${previewNote()}${renderLoadingSetupBanner()}${renderCharacterLibraryIntro()}${libraryToolbar()}<div class="mod-list" id="mod-list">${renderModRows()}</div>`;
    return `${header('MOD LIBRARY', 'Manage your mods.', 'Add mods, read their notes and choose which ones are on.', (undoSet ? button('Undo last change', 'undo', 'refresh') : '') + button('Character appearance', 'character-appearance', 'grid') + button('Library tools', 'library-tools', 'layers') + button('Import folder', 'import-folder', 'folder') + button('Import mods', 'import', 'plus', 'primary'))}${previewNote()}${renderLoadingSetupBanner()}${renderUpdateSummary()}${libraryToolbar()}<div class="mod-list" id="mod-list">${renderModRows()}</div>${releasableMods().length ? `<div class="inline-actions bulk-release">${button('Stop managing all', 'release-all', 'folder', 'small')}</div>` : ''}${renderDiscoveries()}${externalMods().length ? externalList() : ''}`;
  }
  function renderConflicts() {
    const conflicts = state.snapshot.conflicts,
      omitted = omittedConflictPairs(),
      groups = conflictGroups(conflicts),
      visible = groups.slice(0, state.conflictLimit);
    const unreadable = unreadableInstalledPackages(), incomplete = compatibilityCheckReason();
    return `${header('COMPATIBILITY', 'Check mod conflicts.', 'Review shared game files. Keep one version enabled, or prepare a supported combination in Workshop.', button('Open workshop', 'workshop-enter', 'wrench') + button('Review combination', 'combination-review', 'layers') + button('Sort out conflicts', 'conflict-guide', 'shield') + button('Check again', 'scan', 'refresh') + button('Diagnostics', 'diagnose', 'wrench'))}${previewNote()}${conflicts.length && incomplete ? `<div class="banner warning"><p><strong>Checks incomplete.</strong> The overlaps below are confirmed. ${escape(incomplete)}</p></div>` : ''}${conflicts.length ? `<div class="banner warning">${icon('alert')}<p><strong>The game uses only one of them.</strong> When two mods replace the same game file, only one version wins. Keep one enabled, use a compatible release from the authors, or review a supported property merge in Workshop.</p></div>` : ''}${omitted ? `<div class="banner warning"><p><strong>Not every conflict is listed here: ${omitted} more pairs of conflicting mods are left out.</strong> The scanner returned ${conflicts.length} of ${detectedConflictCount()} detected pairs within its report limit. Resolve some overlaps and scan again to narrow the report. Exports may use the same evidence limits.</p></div>` : ''}${
      conflicts.length
        ? `<p class="field-note">${groups.length === 1 ? 'One conflict to sort out.' : `${groups.length} conflicts to sort out.`} Choose Keep this one on the mod you want; the toolkit switches off the ones it conflicts with.${visible.length < groups.length ? ` Showing the first ${visible.length}.` : ''}</p>${visible
            .map((group) => {
              const pairs = group.pairs,
                single = pairs.length === 1 ? pairs[0] : null,
                files = single ? Number(single.count ?? group.assets.size) : group.assets.size,
                shownAssets = [...group.assets].slice(0, 64),
                sample = single
                  ? conflictSampleNote(single, single.assets || single.paths || [])
                  : group.sampled || group.assets.size > shownAssets.length
                    ? `Showing ${shownAssets.length} shared paths. This is a sample; exported evidence may use the same limit.`
                    : '';
              return `<article class="conflict-card"><div class="conflict-card-header"><div class="conflict-icon">${icon('alert')}</div><div class="conflict-title"><h3>${escape(groupTitle(group))}</h3><p>${group.ids.length > 2 ? `${group.ids.length} mods change the same game files` : 'Both change the same game files'} · ${escape(files)}${single ? '' : '+'} shared file${files === 1 ? '' : 's'}</p></div><span class="conflict-badge">PICK ONE</span></div><ul class="conflict-members">${group.ids
                .map((id) => {
                  const mod = getMod(id);
                  return `<li><strong>${escape(group.names.get(id))}</strong><span class="member-state ${mod?.enabled ? 'on' : mod ? 'off' : 'by-hand'}">${mod?.enabled ? 'Enabled' : mod ? 'Off' : 'Installed by hand'}</span>${mod?.enabled ? `<span class="inline-actions"><button class="button small" data-keep-one="${escape(id)}">Keep this one</button><button class="button ghost small" data-toggle="${escape(id)}">Switch off</button></span>` : ''}</li>`;
                })
                .join('')}</ul>${
                pairs.length > 1
                  ? `<details class="finding-evidence"><summary>Which of them conflict · ${pairs.length} pair${pairs.length === 1 ? '' : 's'}</summary><ul>${pairs
                      .map(
                        (pair) =>
                          `<li>${escape(pair.aName || modName(pair.a))} ↔ ${escape(pair.bName || modName(pair.b))} · ${escape(pair.count ?? (pair.assets || []).length)} files</li>`
                      )
                      .join('')}</ul></details>`
                  : ''
              }${
                shownAssets.length
                  ? `<details class="finding-evidence"><summary>Shared files</summary><div class="asset-list">${shownAssets
                      .map((a) => escape(a))
                      .join('<br>')}</div>${sample ? `<p class="field-note">${escape(sample)}</p>` : ''}</details>`
                  : sample
                    ? `<p class="field-note">${escape(sample)}</p>`
                    : ''
              }<div class="conflict-actions"><p>Keep one enabled, or use Open workshop to inspect whether a supported combination is possible.</p><div class="inline-actions">${button('Export report', 'export', 'report', 'small')}</div></div></article>`;
            })
            .join(
              ''
            )}${visible.length < groups.length ? `<div class="mt-20">${button('Show 50 more', 'conflicts-more', 'plus')}</div>` : ''}`
        : `<div class="empty-state">${icon('shield')}<h3>${state.preview ? 'Conflict checks need the desktop app' : incomplete ? 'Checks incomplete' : 'No conflicts'}</h3><p>${escape(state.preview ? 'The app reads your actual mod files before it can report a conflict.' : incomplete || 'No file overlaps were found among your installed mods. Check each mod’s own requirements too.')}</p>${state.snapshot.libraryBackupRecovery?.pending ? button('Review library recovery', 'library-backup-recovery', 'shield') : button('Check again', 'scan', 'refresh')}</div>`
    }${unreadable.length ? `<div class="banner warning mt-20">${icon('alert')}<p><strong>${unreadable.length} PAK file${unreadable.length === 1 ? '' : 's'} could not be read</strong>, so conflicts with ${unreadable.length === 1 ? 'it' : 'them'} can’t be checked. Diagnostics has the details.</p></div>` : ''}${renderPotentialConflicts()}${externalMods().length ? externalList() : ''}${renderDiagnostics()}`;
  }
  function combinationCandidates() {
    const candidates = new Map();
    for (const mod of state.snapshot.mods || []) {
      if (typeof mod.id !== 'string' || !mod.id || !mod.imported && !mod.enabled) continue;
      candidates.set(mod.id, { id: mod.id, name: mod.name || 'Local package',
        note: mod.enabled ? 'Enabled' : activationLabel(mod), kind: 'library' });
    }
    for (const mod of state.snapshot.installed || []) {
      if (typeof mod.id !== 'string' || !mod.id || candidates.has(mod.id)) continue;
      candidates.set(mod.id, { id: mod.id, name: mod.name || mod.file || 'Installed package',
        note: mod.readable === true ? 'Installed outside the toolkit' : 'Installed · needs a readable index', kind: 'installed' });
    }
    return [...candidates.values()].sort((a, b) => a.name.localeCompare(b.name));
  }
  function combinationSourceFingerprint(ids) {
    return JSON.stringify(ids.map((id) => {
      const mod = getMod(id) || state.snapshot.installed.find((item) => item.id === id);
      return mod ? [id, mod.sha256, mod.bytes, mod.imported, mod.enabled, mod.cacheIntegrity,
        mod.readable, mod.indexed, mod.indexStatus, mod.status] : [id, 'missing'];
    }));
  }
  function openCombinationReview(initial = []) {
    if (state.busy) { toast('Wait for the current operation to finish, then review the combination.', 'warning'); return false; }
    const candidates = combinationCandidates(), context = state.snapshot.accountContext,
      selected = new Set(initial.filter((id) => candidates.some((mod) => mod.id === id)));
    modal('Combination review', '<p>Choose the local packages you want to compare, including mods from other authors. The toolkit checks which cooked assets they share before you decide what to use.</p><p class="field-note">Choose at least two. This checks package paths without changing enabled mods. If the selected package data exceeds a safe processing limit, the review explains why.</p>' +
      (state.preview ? '<p>The desktop app needs to read your actual PAKs before a combination can be reviewed.</p>' :
        candidates.length < 2 ? '<p>Add at least two packages to your library, or scan installed mods, to compare them here.</p>' :
          '<label class="field-label" for="combination-search">Find a local package</label><input id="combination-search" class="input full-width" type="search" placeholder="Search package names" maxlength="240"><p class="field-note" id="combination-count" aria-live="polite"></p><div class="batch-import-list combination-list" id="combination-list"></div>') +
      '<p class="field-error" id="combination-error" role="alert"></p>',
      '<button type="button" class="button" data-modal-close>Close</button>' +
      (!state.preview && candidates.length >= 2 ? '<button type="button" class="button primary" id="combination-check">Review selected packages</button>' : ''));
    if (state.preview || candidates.length < 2) return false;
    const generation = modalContentGeneration, check = $('#combination-check'), list = $('#combination-list'),
      current = () => $('#modal').open && generation === modalContentGeneration && context === state.snapshot.accountContext;
    let checking = false;
    const paint = () => {
      if (!current()) return;
      const search = $('#combination-search').value.trim().toLocaleLowerCase(),
        filtered = candidates.filter((mod) => !search || mod.name.toLocaleLowerCase().includes(search)), shown = filtered.slice(0, 60);
      $('#combination-count').textContent = `${selected.size} selected${filtered.length > shown.length ? ` · Showing ${shown.length} of ${filtered.length}; narrow the search to find more` : ''}`;
      check.disabled = checking || selected.size < 2;
      list.innerHTML = shown.length ? shown.map((mod) => `<article class="batch-import-item"><label><input type="checkbox" name="combination-package" value="${escape(mod.id)}" ${selected.has(mod.id) ? 'checked' : ''} ${checking ? 'disabled' : ''}><span><strong>${escape(mod.name)}</strong><small>${escape(mod.note)}</small></span></label></article>`).join('') : '<p class="field-note">No matching local packages.</p>';
    };
    $('#combination-search').oninput = paint;
    list.onchange = (event) => {
      const input = event.target;
      if (!current() || checking || input?.name !== 'combination-package' || !candidates.some((mod) => mod.id === input.value)) return;
      if (input.checked) selected.add(input.value); else if (!input.checked) selected.delete(input.value);
      $('#combination-error').textContent = '';
      paint();
    };
    check.onclick = async () => {
      if (!current() || checking || selected.size < 2) return false;
      const ids = [...selected], fingerprint = combinationSourceFingerprint(ids);
      checking = true; check.textContent = 'Reading package paths…'; paint();
      $('#combination-error').textContent = '';
      try {
        const result = await request('modMergeReview', { ids });
        if (!current()) return false;
        if (fingerprint !== combinationSourceFingerprint(ids)) throw new Error('A selected package changed. Close this review and choose the current files again.');
        if (result?.operation !== 'mod-merge-review' || result.canAutoMerge !== false ||
            !['needs-asset-merge', 'no-path-overlap', 'incomplete'].includes(result.status) || !Array.isArray(result.sources) ||
            result.sources.length !== ids.length || new Set(result.sources.map((source) => source.id)).size !== ids.length ||
            result.sources.some((source) => !ids.includes(source.id)))
          throw new Error('The package review did not return a complete result. Try again after scanning your mods.');
        showCombinationReport(result, ids);
        return true;
      } catch (error) {
        if (current()) $('#combination-error').textContent = errorText(error);
        return false;
      } finally {
        checking = false;
        if (current()) { check.textContent = 'Review selected packages'; paint(); }
      }
    };
    paint();
    return true;
  }
  function showCombinationReport(result, ids) {
    const allSources = result.sources, sources = allSources.slice(0, 100), families = Array.isArray(result.families) ? result.families.slice(0, 80) : [],
      incomplete = result.status === 'incomplete' || allSources.some((source) => source.available !== true),
      hasCookedAssets = families.some((family) => family.kind === 'asset'),
      overlapCount = Math.max(0, Number(result.overlapCount) || 0),
      title = incomplete ? 'Some packages could not be checked' : result.status === 'needs-asset-merge' ? hasCookedAssets ? 'These packages share cooked assets' : 'These packages share game files' : 'No shared package paths found',
      explanation = incomplete ? 'The readable files may show overlaps below. Compatibility involving an unchecked package is still unknown.' :
        result.status === 'needs-asset-merge' ? hasCookedAssets ? 'Keeping both sets of changes needs a dedicated asset merge. Choosing one whole asset can discard the other mod’s edits.' :
          'Keeping both sets of changes needs a merge tool that understands those file formats. Choosing one whole file can discard the other mod’s edits.' :
          'These local files do not replace the same package paths. This does not guarantee that their gameplay changes, requirements or scripts work together.',
      warnings = Array.isArray(result.warnings) ? result.warnings.filter((item) => typeof item === 'string').slice(0, 16) : [];
    modal('Combination review', `<div class="banner ${incomplete || result.status === 'needs-asset-merge' ? 'warning' : 'neutral'}"><p><strong>${escape(title)}</strong><br>${escape(explanation)}</p></div><p class="field-note">Read-only report. No files were merged, enabled or installed. ${allSources.length} selected packages.${allSources.length > sources.length ? ` The first ${sources.length} are shown below; all selected packages are included in the result.` : ''}</p><div class="batch-import-list">${sources.map((source) => `<article class="batch-import-item" data-state="${source.available === true ? 'ready' : 'error'}"><strong>${escape(source.name || 'Local package')}</strong><small>${source.kind === 'installed' ? 'Installed outside the toolkit' : 'Library package'} · ${source.available === true ? `${Math.max(0, Number(source.assetCount) || 0)} indexed paths` : 'Not checked'}</small>${source.reason ? `<p class="field-note">${escape(String(source.reason).slice(0, 1000))}</p>` : ''}</article>`).join('')}</div>${families.length ? `<h3>${hasCookedAssets ? 'Shared asset families' : 'Shared package paths'} · ${overlapCount}</h3><p class="field-note">${hasCookedAssets ? 'An asset and its companion files are grouped together. ' : ''}These are paths inside the PAKs, not folders on your computer.</p>${families.map((family) => `<details class="finding-evidence combination-family"><summary><code>${escape(String(family.path || 'Shared asset').slice(0, 4096))}</code></summary>${(Array.isArray(family.sources) ? family.sources.slice(0, 100) : []).map((source) => `<p><strong>${escape(source.name || 'Local package')}</strong></p><div class="asset-list">${(Array.isArray(source.paths) ? source.paths.slice(0, 16) : []).map((asset) => escape(String(asset).slice(0, 4096))).join('<br>')}</div>`).join('')}</details>`).join('')}` : ''}${result.truncated || overlapCount > families.length || families.some((family) => Array.isArray(family.sources) && family.sources.length > 100) ? '<p class="field-note">Only part of the overlap evidence is shown. Resolve some overlaps and review again before treating the report as complete.</p>' : ''}${warnings.length ? `<details class="finding-evidence"><summary>What this review can tell you</summary><ul>${warnings.map((warning) => `<li>${escape(warning.slice(0, 2000))}</li>`).join('')}</ul></details>` : ''}`,
      '<button type="button" class="button" data-modal-close>Close</button><button type="button" class="button primary" id="combination-choose">Choose packages</button>');
    $('#combination-choose').onclick = () => openCombinationReview(ids);
  }
  function unreadableInstalledPackages() {
    return state.snapshot.installed.filter((mod) => mod.readable !== true || mod.indexed === false ||
      ['unreadable', 'not-checked', 'not_checked'].includes(mod.indexStatus) || mod.status === 'unknown');
  }
  function compatibilityCheckReason() {
    if (state.preview) return 'The desktop app needs to read your installed files before compatibility can be checked.';
    if (state.snapshot.libraryBackupRecovery?.pending) return 'Finish the interrupted library restore before checking compatibility.';
    if (state.snapshot.recoveryRequired) return 'An interrupted file change needs recovery before compatibility can be checked.';
    if (state.loading) return 'The package scan has not finished yet.';
    if (state.scanFailed) return 'The last scan did not finish. Check again or run Diagnostics before relying on these results.';
    if (state.snapshot.diagnostics.some((finding) => finding.code === 'game-not-selected'))
      return 'Choose the game installation to check all of its mod folders. Any overlaps shown apply only to the folders already scanned.';
    const count = unreadableInstalledPackages().length;
    if (count) return `${count} PAK file${count === 1 ? '' : 's'} could not be checked. Conflicts involving those packages remain unknown. Run Diagnostics to see what needs fixing.`;
    if (omittedConflictPairs()) return 'Some detected overlaps are missing from this report. Run Diagnostics before treating the result as complete.';
    return '';
  }

  function renderPotentialConflicts() {
    const candidates = state.snapshot.mods.filter(
      (m) => m.imported && !m.enabled && m.available !== false && m.conflictsWithInstalled?.length
    );
    if (!candidates.length) return '';
    return `<section class="mt-28"><div class="section-heading"><div><h2>Conflicts with your enabled mods</h2><p>These mods are off because each one changes the same game files as a mod you have on. Switch one on and the toolkit offers to swap them.</p></div></div><div class="mod-list">${candidates.map((m) => `<div class="external-row conflict-candidate"><span class="candidate-icon">${icon('alert')}</span><div><button class="mod-name" data-details="${escape(m.id)}">${escape(m.name)}</button>${personalModMeta(m.id).favorite ? '<span class="badge personal-favorite">Favourite</span>' : ''}<small>Conflicts with <strong>${escape(m.conflictsWithInstalled.map((c) => c.name || modName(c.id)).join(', '))}</strong></small></div><button type="button" class="button small" data-toggle="${escape(m.id)}">${icon('check')}Switch on</button></div>`).join('')}</div></section>`;
  }
  async function keepOnlyMod(id) {
    const kept = getMod(id);
    if (!kept || state.busy) return false;
    const partners = new Set();
    for (const conflict of state.snapshot.conflicts || []) {
      const [a, b] = conflictIds(conflict);
      if (a === id) partners.add(b);
      if (b === id) partners.add(a);
    }
    const managed = [...partners].filter((partner) => getMod(partner)?.enabled),
      byHand = [...partners].filter((partner) => !getMod(partner));
    if (!managed.length && !byHand.length) return false;
    rememberUndo();
    if (managed.length && (await run('keepOnlyMod', { id }, { refresh: false, progress: `Keeping ${kept.name} and checking its conflicts…` })) == null)
      return false;
    await refresh({ quiet: true });
    if (byHand.length)
      toast(
        `${kept.name} still conflicts with ${byHand.map((partner) => modName(partner)).join(', ')}, installed by hand. Manage or delete ${byHand.length === 1 ? 'it' : 'them'} below.`,
        'warning'
      );
    else if (managed.length)
      toast(
        `Kept ${kept.name}. Switched off: ${managed.map((partner) => modName(partner)).join(', ')}.`,
        'success',
        undoAction()
      );
    return true;
  }
  async function startConflictGuide() {
    if (state.busy) return;
    await refresh({ quiet: true });
    state.conflictGuide = { skipped: new Set(), resolved: 0 };
    showConflictStep();
  }
  function showConflictStep() {
    const guide = state.conflictGuide;
    if (!guide) return;
    const signature = (group) => [...group.ids].sort().join('|');
    const groups = conflictGroups(state.snapshot.conflicts || []);
    const group = groups.find((item) => !guide.skipped.has(signature(item)));
    if (!group) {
      modal('Conflict review finished', `<p>${groups.length || omittedConflictPairs() ? 'Some conflicts still need attention. Skipped groups and files installed by hand stay as they are.' : 'No file conflicts remain in the current scan.'}</p><p>${guide.resolved} choice${guide.resolved === 1 ? '' : 's'} applied. Game behavior still depends on the mods themselves.</p>`, '<button class="button" data-modal-close>Close</button>');
      state.conflictGuide = null;
      return;
    }
    const choices = group.ids.filter((id) => getMod(id)?.enabled);
    modal('Sort out this conflict', `<p><strong>${escape(groupTitle(group))}</strong></p><p>Choose the mod to keep. Other enabled mods that directly conflict with it are switched off; their library copies stay available.</p><div class="conflict-guide-choices">${choices.map((id) => `<button type="button" class="button" data-guide-keep="${escape(id)}">Keep ${escape(modName(id))}</button>`).join('')}</div>${group.ids.some((id) => !getMod(id)) ? '<p>Some files were installed by hand. The guide leaves those files alone; manage or remove them from My mods first.</p>' : ''}`,
      '<button class="button" data-modal-close>Close</button><button class="button" id="conflict-guide-skip">Skip this group</button>');
    $('#conflict-guide-skip').onclick = () => { guide.skipped.add(signature(group)); showConflictStep(); };
    $$('[data-guide-keep]').forEach((el) => (el.onclick = async () => {
      $('#modal').close();
      const before = (state.snapshot.conflicts || []).length;
      await keepOnlyMod(el.dataset.guideKeep);
      if ((state.snapshot.conflicts || []).length < before) guide.resolved++;
      else guide.skipped.add(signature(group));
      showConflictStep();
    }));
  }
  function updateNoticeBadge() {
    const button = $('#attention-button'),
      count = $('#attention-count');
    if (!button || !count) return;
    // Scan findings count one by one even though they share a single pop-up.
    const total = state.noticeQueue.filter((notice) => notice.type !== 'issues').length + state.issues.length;
    button.hidden = !total;
    count.textContent = String(total);
    button.title = `${total} setup or package notice${total === 1 ? '' : 's'}`;
  }
  function enqueueNotice(notice) {
    const fingerprint = notice.fingerprint || JSON.stringify([notice.type, notice.id, notice.message]);
    if (seenNoticeFingerprints.has(fingerprint)) return false;
    seenNoticeFingerprints.add(fingerprint);
    state.noticeQueue.push({ ...notice, fingerprint });
    updateNoticeBadge();
    scheduleNoticeDelivery();
    return true;
  }
  function considerSetup(snapshot) {
    const setup = snapshot.setup;
    // Setup findings join the scan findings in the one combined, remembered pop-up.
    state.setupIssues =
      setup?.state === 'needs-attention'
        ? (setup.notices?.length
            ? setup.notices
            : [
                {
                  id: 'setup-needs-attention',
                  message:
                    'The mod-loading setup needs attention. Run diagnostics to review the current files and available next steps.'
                }
              ]
          ).map((notice) => {
            const message =
              typeof notice === 'string'
                ? notice
                : notice.message || notice.title || 'The mod-loading setup needs review.';
            return {
              key: `setup:${(typeof notice === 'object' && (notice.id || notice.code)) || message}`.slice(0, 500),
              message
            };
          })
        : [];
    if (state.setupPrompted || !setup) return;
    state.setupPrompted = true;
    if (setup.state === 'choose-installation') {
      enqueueNotice({
        type: 'setup-choice',
        id: 'choose-installation',
        message: 'Choose which Days Gone installation to use.',
        installations: setup.installations || []
      });
    } else if (setup.state === 'not-found') {
      enqueueNotice({ type: 'setup-missing', id: 'not-found', message: 'Days Gone was not found automatically.' });
    }
  }
  // Scan findings (overlaps, changed or missing managed files) share one pop-up, and it opens by itself only
  // when something new turns up. Findings already shown are remembered in the native interface preferences,
  // so the same problems do not pop up again on every scan or launch; the notice button still lists them.
  function issueKey(finding, code) {
    const ids = Array.isArray(finding.modIds) ? finding.modIds.map(String).sort().join('+') : finding.modId || '';
    return `${code}:${ids || finding.message || ''}`.slice(0, 500);
  }
  function collectActionNotices(snapshot) {
    if (state.profileSession.active) return;
    const issues = [...(state.setupIssues || [])];
    // Overlaps are grouped like the Conflicts page: one line per group of mods.
    const pairs = (snapshot.diagnostics || [])
      .filter((finding) => (finding.code || finding.id) === 'asset-conflict' && Array.isArray(finding.modIds))
      .map((finding) => {
        const [a, b] = finding.modIds.map(String),
          known = (snapshot.conflicts || []).find((c) => conflictIds(c).sort().join('+') === [a, b].sort().join('+'));
        return { a, b, aName: known?.aName || modName(a), bName: known?.bName || modName(b) };
      });
    for (const group of conflictGroups(pairs))
      issues.push({
        key: `asset-conflict:${[...group.ids].sort().join('+')}`.slice(0, 500),
        message: `${groupTitle(group)} change the same game files. Keep only one of them enabled.`,
        conflict: true
      });
    for (const finding of snapshot.diagnostics || []) {
      const code = finding.code || finding.id;
      if (!['asset-conflict', 'owned-file-changed', 'owned-file-missing'].includes(code)) continue;
      if (code === 'asset-conflict' && Array.isArray(finding.modIds)) continue;
      const owner = finding.modId && (snapshot.mods || []).find((m) => m.id === String(finding.modId));
      if (
        (code === 'owned-file-changed' || code === 'owned-file-missing') &&
        owner?.canForgetInstallation === true &&
        !installationRecordReason(owner)
      )
        continue; // handled automatically after the scan
      const key = issueKey(finding, code);
      if (issues.some((issue) => issue.key === key)) continue;
      issues.push({
        key,
        message: finding.message || finding.detail || 'An installed mod needs a look.',
        conflict: code === 'asset-conflict'
      });
    }
    state.issues = issues;
    if (!issues.length && activeIssueSummary) {
      activeIssueSummary = false;
      if ($('#modal').open) $('#modal').close();
    }
    const remembered = Array.isArray(snapshot.settings?.seenNotices) ? snapshot.settings.seenNotices : [];
    const fresh = issues.filter((issue) => !remembered.includes(issue.key) && !shownIssueKeys.has(issue.key));
    state.noticeQueue = state.noticeQueue.filter((notice) => notice.type !== 'issues');
    if (fresh.length) {
      state.noticeQueue.push({ type: 'issues', fingerprint: 'issues' });
      scheduleNoticeDelivery();
    }
    // Forget resolved findings, so a problem that comes back later is new again.
    const kept = remembered.filter((key) => issues.some((issue) => issue.key === key));
    if (kept.length !== remembered.length) rememberIssues(kept);
    updateNoticeBadge();
  }
  function rememberIssues(keys) {
    if (state.preview) return;
    if (state.snapshot.settings) state.snapshot.settings.seenNotices = keys;
    request('savePreferences', { preferences: { seenNotices: keys.slice(0, 256) } }).catch(() => {});
  }
  function showIssueSummary() {
    const issues = state.issues;
    if (!issues.length) return false;
    const conflicts = issues.filter((issue) => issue.conflict).length,
      others = issues.length - conflicts,
      shown = issues.slice(0, 8);
    modal(
      issues.length === 1
        ? conflicts
          ? 'Conflicting mods'
          : 'Something needs a look'
        : `${issues.length} things need a look`,
      `<p>The last scan found ${issues.length === 1 ? 'this' : 'these'}:</p><ul class="finder-mods">${shown.map((issue) => `<li>${escape(issue.message)}</li>`).join('')}</ul>${issues.length > shown.length ? `<p class="field-note">And ${issues.length - shown.length} more. Conflicts and Diagnostics list all of them.</p>` : ''}<p class="field-note">New findings stay on the notice button at the top. Open them when you are ready to review them.</p>`,
      // Conflicts are fixed on the Conflicts page; everything else is checked by Diagnostics.
      `<button class="button" data-modal-close>Close</button>${conflicts ? `<button class="button ${others ? '' : 'primary'}" id="issues-conflicts">Review conflicts</button>` : ''}${others ? '<button class="button primary" id="issues-diagnostics">Run diagnostics</button>' : ''}`
    );
    activeIssueSummary = true;
    const review = $('#issues-conflicts');
    if (review)
      review.onclick = () => {
        $('#modal').close();
        navigate('conflicts');
      };
    const diagnose = $('#issues-diagnostics');
    if (diagnose)
      diagnose.onclick = () => {
        $('#modal').close();
        loadDiagnosticReport();
      };
    state.noticeQueue = state.noticeQueue.filter((notice) => notice.type !== 'issues');
    issues.forEach((issue) => shownIssueKeys.add(issue.key));
    rememberIssues(issues.map((issue) => issue.key));
    updateNoticeBadge();
    return true;
  }
  function scheduleNoticeDelivery() {
    if (!state.noticeQueue.length) return;
    if (noticeDeliveryTimer) clearTimeout(noticeDeliveryTimer);
    noticeDeliveryTimer = setTimeout(() => {
      noticeDeliveryTimer = null;
      showNextNotice();
    }, 150);
  }
  function showNextNotice(force = false) {
    if (
      !state.noticeQueue.length ||
      state.busy ||
      state.loading ||
      $('#modal').open ||
      state.nativeDropActivity?.running ||
      (state.view === 'settings' && !force)
    )
      return false;
    // Background findings remain on the notice button. They should not open
    // a chain of dialogs as the user navigates or closes another review.
    const passive = new Set(['issues', 'game-updated', 'whats-new', 'updated']),
      index = force ? 0 : state.noticeQueue.findIndex((notice) => !passive.has(notice.type));
    if (index < 0) return false;
    const next = state.noticeQueue[index];
    if (state.profileSession.active && next.type !== 'error') return false;
    state.noticeQueue.splice(index, 1);
    updateNoticeBadge();
    if (next.type === 'issues') {
      showIssueSummary();
    } else if (next.type === 'setup-choice') {
      modal(
        'Choose your Days Gone installation',
        `<p>More than one valid installation was found. Choose the copy you actually launch. This saves the game location; it does not move your mods or change game assets.</p><div class="setup-choice-list">${(next.installations || []).map((item, index) => `<button class="setup-choice" data-setup-choice="${index}"><strong>${escape(item.label || 'Days Gone')} · ${escape(item.source || 'Detected installation')}</strong><code>${escape(item.path)}</code></button>`).join('')}</div><p class="field-note">You can choose Not now and keep importing packages or reading the field guide.</p>`,
        '<button class="button" data-modal-close>Not now</button>'
      );
      $$('[data-setup-choice]', $('#modal')).forEach(
        (button) =>
          (button.onclick = async () => {
            const item = next.installations[Number(button.dataset.setupChoice)];
            if (!item) return;
            $('#modal').close();
            await run(
              'saveSettings',
              { settings: { gamePath: item.path } },
              { success: 'Game installation selected.' }
            );
          })
      );
    } else if (next.type === 'game-crash') {
      modal('Days Gone crashed', `<p>${escape((next.advice || [])[0] || 'Open the crash summary for the report and suggested checks.')}</p><p>The crash summary includes the active mod set when it can be identified.</p>`,
        '<button class="button" data-modal-close>Close</button><button class="button primary" id="crash-notice-summary">View crash summary</button>');
      request('crashLogNoticeDismiss', { id: next.id }).catch(() => {});
      $('#crash-notice-summary').onclick = async () => {
        $('#modal').close();
        await loadCrashLogs();
        if (state.crashLogs?.logs?.some((log) => log.id === next.logId)) await summarizeCrashDump(next.logId);
        else { navigate('diagnostics'); toast('The report changed while it was being written. Refresh the crash logs and choose its summary.'); }
      };
    } else if (next.type === 'game-updated') {
      if (next.id) request('gameBuildDismiss', { id: next.id }).catch(() => {});
      modal('Days Gone files changed', `<p>${escape(next.message || 'The game build changed since the previous check. Mod loading has been checked again. Some mods may need an update.')}</p><p>A changed build does not prove a mod is broken. Check its notes and run diagnostics before playing.</p>`,
        '<button class="button" data-modal-close>Close</button><button class="button primary" id="game-update-diagnostics">Run diagnostics</button>');
      $('#game-update-diagnostics').onclick = () => { $('#modal').close(); loadDiagnosticReport(); };
    } else if (next.type === 'updated') {
      updatedModal();
    } else if (next.type === 'whats-new') {
      if (state.justUpdated?.to && state.justUpdated.to === window.WHATS_NEW?.version) updatedModal();
      else showWhatsNew();
    } else if (next.type === 'restart-profile') {
      reviewPortableProfile(next.profileName);
    } else if (next.type === 'package-review') {
      reviewDownloadedPackage(next.review);
    } else if (next.type === 'batch-review') {
      reviewBatchImport(next.review);
    } else if (next.type === 'batch-result') {
      showBatchImportResults(next.result, next.activationNote || '');
    } else if (next.type === 'updates') {
      modal(
        'New releases for your installed mods',
        `<p>${escape(next.message)}</p><div class="finding-evidence"><ul>${next.updates.map(({ mod, update }) => `<li><strong>${escape(mod.name)}</strong> · installed ${escape(update.currentVersion || mod.version || 'version unknown')} → latest ${escape(update.version || 'version not supplied')}<br>${update.auditedInstallable ? 'Matches an audited catalog package.' : 'Download available · awaiting package inspection and verification.'}</li>`).join('')}</ul></div><p>Downloads do not replace an enabled package automatically. Review the variant and actual asset conflicts first.</p>`,
        '<button class="button" data-modal-close>Later</button><button class="button primary" id="notice-updates">Review downloads</button>'
      );
      $('#notice-updates').onclick = () => {
        $('#modal').close();
        navigate('downloads');
      };
    } else if (next.type === 'setup-missing') {
      modal(
        'Help the toolkit find Days Gone',
        `<p>No valid game installation was found automatically. Choose the main folder containing <code>DaysGone.exe</code> when you’re ready.</p><p>You can still import supported packages, manage your library and use the field guide without choosing the game folder now.</p>`,
        '<button class="button" data-modal-close>Not now</button><button class="button primary" id="setup-browse">Browse for game</button>'
      );
      $('#setup-browse').onclick = async () => {
        $('#modal').close();
        const result = await run('browseGame', {}, { refresh: false });
        if (result && !result.cancelled) await refresh({ quiet: true });
      };
    } else {
      modal(
        next.type === 'error'
          ? 'This action needs attention'
          : next.conflict
            ? 'Conflicting mods'
            : 'Your setup needs a closer look',
        `${next.messages?.length > 1 ? `<ul class="finder-mods">${next.messages.map((message) => `<li>${escape(message)}</li>`).join('')}</ul>` : `<p>${escape(next.message)}</p>`}<p>${next.type === 'error' ? 'Run diagnostics if the message above does not say what to do next.' : 'Run diagnostics to see what is wrong and what can be fixed.'}</p>`,
        `<button class="button" data-modal-close>Close</button><button class="button" id="notice-help">Read the field guide</button><button class="button primary" id="notice-diagnostics">Run diagnostics</button>`
      );
      $('#notice-help').onclick = () => {
        $('#modal').close();
        openGuideArticle(next.helpKey || 'reporting');
      };
      $('#notice-diagnostics').onclick = () => {
        $('#modal').close();
        loadDiagnosticReport();
      };
    }
    return true;
  }
  function showActionError(error, action) {
    const critical = new Set([
      'supportBundleExport',
      'nexusDownload',
      'confirmReviewedImport',
      'restoreLibraryCopy',
      'enable',
      'disable',
      'remove',
      'browseImport',
      'browseImports',
      'browseImportFolder',
      'batchImportConfirm',
      'libraryBackupRestore',
      'libraryBackupRecover',
      'browseLibraryBackupExport',
      'browseLibraryBackupRestore',
      'import',
      'reviewExternalRemoval',
      'reviewExternalAdoption',
      'removeExternal',
      'reviewForgetInstallation',
      'forgetInstallation',
      'saveSettings',
      'browseGame',
      'profilesSave',
      'profilesDelete',
      'profileLaunch',
      'profileFinishSession',
      'directoriesPrepare',
      'directoriesRestore',
      'diagnosticsRepair',
      'diagnosticsRun'
    ]);
    if (!critical.has(action)) return false;
    const message = errorText(error),
      helpKey = action.toLowerCase().includes('forgetinstallation')
        ? 'uninstall'
        : action.startsWith('profile')
          ? 'profiles'
          : action.startsWith('directories')
            ? 'directories'
            : ['enable', 'disable'].includes(action)
              ? 'compatibility'
              : 'reporting';
    if ($('#modal').open) {
      enqueueNotice({ type: 'error', id: action, message, helpKey });
      return true;
    }
    modal(
      'The action could not finish',
      `<p>${escape(message)}</p>${action.toLowerCase().includes('forgetinstallation') ? '<p>Refresh the mod list and review this installation record again. The toolkit checks the current file before forgetting it.</p>' : '<p>Run diagnostics if the message above does not say what to do next.</p>'}`,
      '<button class="button" data-modal-close>Close</button><button class="button" id="action-error-help">Get help</button><button class="button primary" id="action-error-diagnostics">Run diagnostics</button>'
    );
    $('#action-error-help').onclick = () => {
      $('#modal').close();
      openGuideArticle(helpKey);
    };
    $('#action-error-diagnostics').onclick = () => {
      $('#modal').close();
      loadDiagnosticReport();
    };
    return true;
  }
  // Checks that passed fold into one line; opened, each shows its full result, details and any fix.
  function renderPassedChecks(checks) {
    if (!checks.length) return '';
    return `<details class="finding-evidence passed-checks"><summary>${icon('check')}${checks.length} check${checks.length === 1 ? '' : 's'} passed</summary><ul>${checks.map((check) => `<li><strong>${escape(check.title || check.id || 'Check')}</strong><span>${escape(check.message || '')}</span>${diagnosticEvidence(check)}${check.fix ? `<button class="button small" data-diagnostic-fix="${escape(check.id)}" ${state.diagnosticStale ? 'disabled' : ''}>${icon('wrench')}${escape(check.fix.label || 'Review fix')}</button>` : ''}</li>`).join('')}</ul></details>`;
  }
  function diagnosticStatus(check) {
    if (check.status === 'passed') return { className: 'passed', icon: 'check', label: 'Passed' };
    if (check.status === 'unknown') return { className: 'unknown', icon: 'unknown', label: 'Not checked' };
    if (check.severity === 'error') return { className: 'error', icon: 'error', label: 'Needs attention' };
    if (check.severity === 'warning' || check.status === 'warning')
      return { className: 'warning', icon: 'alert', label: 'Worth a look' };
    if (check.status === 'failed') return { className: 'error', icon: 'error', label: 'Needs attention' };
    return { className: 'info', icon: 'info', label: 'Information' };
  }
  function diagnosticEvidence(check, open = false) {
    const evidence = check.evidence || [];
    return evidence.length
      ? `<details ${open ? 'open ' : ''}class="finding-evidence"><summary>Details · ${evidence.length}</summary><ul>${evidence.map((item) => `<li>${escape(typeof item === 'string' ? item : item.message || item.path || `${item.label || item.name || 'Evidence'}: ${item.value ?? ''}`)}</li>`).join('')}</ul></details>`
      : '';
  }
  function renderDiagnosticPage() {
    const report = state.diagnosticReport,
      checks = report?.checks || [],
      summary = report?.summary,
      settled = (check) => ['passed', 'info'].includes(diagnosticStatus(check).className),
      scopeNote = checks.find((check) => check.id === 'gameplay-scope'),
      results = checks.filter((check) => check !== scopeNote),
      problems = results.filter((check) => !settled(check)),
      fine = results.filter(settled),
      count = (kind) => results.filter((check) => diagnosticStatus(check).className === kind).length,
      tiles = { errors: count('error'), warnings: count('warning'), passed: fine.length, unknown: count('unknown') };
    return `${header('DIAGNOSTICS', 'Check your setup.', 'Find missing files, loading problems and conflicting mods. Every fix asks first.', button('Review duplicates', 'diagnostics-duplicates', 'copy') + button('Export findings', 'diagnostics-export', 'report') + button('Find the bad mod', 'finder-open', 'search') + button('Run diagnostics', 'diagnose', 'wrench', 'primary'))}${previewNote()}${
      !report
        ? `<div class="empty-state">${icon('wrench')}<h3>Check your setup.</h3><p>Diagnostics checks the game folder, mod loading, your mod files, conflicts and profiles, and lists what needs a look first.</p>${button('Check my setup', 'diagnose', 'wrench', 'primary')}</div>`
        : `${state.diagnosticStale ? `<div class="banner warning">${icon('clock')}<p><strong>Files changed after this report.</strong> Run diagnostics again before applying a repair.</p>${button('Run again', 'diagnose', 'refresh', 'small')}</div>` : ''}${summary ? `<div class="stats-grid diagnostic-summary">${stat('error', tiles.errors, 'Need attention', tiles.errors ? 'danger' : 'calm')}${stat('alert', tiles.warnings, 'Worth a look', tiles.warnings ? 'warning' : 'calm')}${stat('check', tiles.passed, 'Passed', tiles.passed ? 'success' : 'calm')}${stat('unknown', tiles.unknown, 'Not checked', tiles.unknown ? 'info' : 'calm')}</div>` : ''}<div class="section-heading"><div><h2>Results</h2><p>${report.checkedAt ? 'Checked ' + escape(formatDate(report.checkedAt)) + '.' : ''}${problems.length ? ` ${problems.length === 1 ? 'One thing needs' : problems.length + ' things need'} a look, listed first.` : ''}</p></div></div>${results.length && !problems.length ? `<div class="banner neutral">${icon('check')}<p><strong>Everything checked out.</strong> Nothing needs your attention.</p></div>` : ''}<div class="finding-list">${
            checks.length
              ? problems
                  .map((check) => {
                    const status = diagnosticStatus(check);
                    return `<article class="finding-card ${status.className}"><div class="finding-heading"><span class="finding-icon">${icon(status.icon)}</span><div><span class="finding-state">${status.label}</span><h2>${escape(check.title || check.id || 'Check')}</h2></div></div><p class="finding-message">${escape(check.message || 'No further details were supplied.')}</p>${diagnosticEvidence(check)}<div class="finding-actions">${check.fix ? `<button class="button small" data-diagnostic-fix="${escape(check.id)}" ${state.diagnosticStale ? 'disabled' : ''}>${icon('wrench')}${escape(check.fix.label || 'Review fix')}</button>` : ''}${brokenInstallationRecord(getMod(check.modId || String(check.id || '').replace(/^owned:/, ''))) ? `<button class="button small" data-details="${escape(check.modId || String(check.id).replace(/^owned:/, ''))}">${icon('report')}Review installation record</button>` : ''}${check.helpKey ? `<button class="button ghost small" data-diagnostic-help="${escape(check.helpKey)}">${icon('book')}Read the field guide</button>` : ''}</div></article>`;
                  })
                  .join('') +
                renderPassedChecks(fine) +
                (scopeNote
                  ? `<p class="field-note scope-note">${icon('unknown')}Diagnostics checks your files and folders, not gameplay. Test new mods in game, and expect some mods to need an update after a game update.</p>`
                  : '')
              : `<div class="empty-state compact"><h3>${state.preview ? 'Live checks need the desktop application' : 'No findings were supplied'}</h3><p>${state.preview ? 'This preview does not scan your files or invent passed checks.' : 'Run diagnostics again if you expected a specific check. An empty report does not establish compatibility.'}</p></div>`
          }</div>`
    }${renderCrashLogs()}<div class="info-card mt-24"><div class="info-card-head">${icon('shield')}Fixes always ask first</div><p>When two mods conflict, you choose which one to switch off; the toolkit never picks a winner or merges mods. Deleting a hand-installed file is a separate step with its own check. Diagnostics is not a virus scan.</p></div>`;
  }
  function logTime(value) {
    if (!value) return 'Time not reported';
    const date = new Date(typeof value === 'number' && value < 1e12 ? value * 1000 : value);
    return Number.isNaN(date.getTime())
      ? 'Time not reported'
      : date.toLocaleString(undefined, {
          year: 'numeric',
          month: 'short',
          day: 'numeric',
          hour: '2-digit',
          minute: '2-digit'
        });
  }
  function exportableLog(log) {
    // A dump can join a support ZIP only as its native crash summary, never as raw bytes.
    if (log.kind === 'memory-dump') return log.summaryAvailable === true && log.exportAvailable !== false;
    return log.previewAvailable !== false && log.exportAvailable !== false;
  }
  function renderCrashLogs() {
    const result = state.crashLogs,
      logs = result?.logs || [],
      selected = state.selectedCrashLogs.size;
    let empty =
      result?.status === 'preview'
        ? 'Log discovery needs the desktop application.'
        : result?.gameStatus === 'not-selected'
          ? 'No toolkit logs were found. Choose the game installation to check its crash folders.'
          : result?.available === false || result?.status === 'unavailable'
            ? 'Log locations could not be checked. Review the message below and try again.'
            : result
              ? 'No recent text logs were found in the checked locations.'
              : 'Logs have not been checked yet.';
    return `<section class="crash-panel panel mt-24"><div class="panel-heading"><div><h2>Crash logs & reports</h2><p>Read recent logs here, then choose what to include in a support ZIP.</p></div><div class="inline-actions"><button class="button small" data-action="crash-refresh" ${state.crashLogsLoading ? 'disabled' : ''}>${icon('refresh')}${state.crashLogsLoading ? 'Checking logs…' : 'Refresh logs'}</button><button class="button small" data-action="support-preview" ${state.preview || state.crashLogsLoading ? 'disabled' : ''}>${icon('report')}Prepare support ZIP${selected ? ' · ' + selected + ' selected' : ''}</button></div></div><div class="panel-body">${result?.gameStatus === 'not-selected' ? '<div class="banner neutral"><p>No game installation is selected. Toolkit logs can still be checked; choose the game in Folders to include its crash locations.</p></div>' : result?.gameStatus === 'unavailable' ? '<div class="banner warning"><p>The game’s log locations could not be checked. Any toolkit logs found are listed below.</p></div>' : ''}<p class="field-note">Nothing is uploaded. Memory dumps are never opened as raw data: Crash summary reads only the crash type, location and timing.</p>${result?.warnings?.length ? `<div class="banner warning"><p>${result.warnings.map((item) => escape(typeof item === 'string' ? item : item.message || 'A location could not be checked.')).join('<br>')}</p></div>` : ''}${
      logs.length
        ? `<div class="crash-log-list">${logs
            .map((log, index) => {
              const allowed = exportableLog(log);
              return `<article class="crash-log-row"><div class="crash-selection"><input type="checkbox" id="crash-choice-${index}" data-crash-select="${escape(log.id)}" ${state.selectedCrashLogs.has(log.id) ? 'checked' : ''} ${allowed ? '' : 'disabled'} aria-label="Include ${escape(log.name || 'log')} in the support ZIP"></div><span class="log-kind-icon">${icon(log.kind === 'memory-dump' ? 'lock' : 'report')}</span><div class="crash-log-copy"><h3>${escape(log.name || 'Log file')}</h3><p>${log.source === 'toolkit' ? 'Toolkit' : 'Game'} · ${escape(String(log.kind || 'log').replace(/-/g, ' '))} · ${formatBytes(log.bytes)} · ${escape(logTime(log.modifiedAt))}</p>${log.reason ? `<small>${escape(log.reason)}</small>` : ''}</div>${log.kind === 'memory-dump' ? `<button class="button small" data-crash-summary="${escape(log.id)}" ${log.summaryAvailable === true ? '' : 'disabled'}>${icon('info')}Crash summary</button>` : `<button class="button small" data-crash-read="${escape(log.id)}" ${log.previewAvailable === false ? 'disabled' : ''}>${icon('search')}Preview</button>`}</article>`;
            })
            .join('')}</div>`
        : `<div class="crash-empty">${icon('report')}<p>${escape(empty)}</p></div>`
    }${result?.scope ? `<p class="field-note log-scope">${escape(typeof result.scope === 'string' ? result.scope : result.scope.message || 'Known game and toolkit log locations only.')}</p>` : ''}<p class="field-note">The ZIP includes fresh diagnostics and mod details plus only the text logs and crash summaries you select. Personal paths and known secret patterns are removed; review the preview before sharing.</p></div></section>`;
  }
  async function loadCrashLogs() {
    if (state.crashLogsLoading) return;
    const context = state.snapshot.accountContext;
    state.crashLogsLoading = true;
    if (state.view === 'diagnostics') render();
    try {
      const result = await request('crashLogsList');
      if (context !== state.snapshot.accountContext) return;
      state.crashLogs = { ...result, logs: Array.isArray(result.logs) ? result.logs : [] };
      const allowed = new Set(state.crashLogs.logs.filter(exportableLog).map((log) => log.id));
      state.selectedCrashLogs = new Set([...state.selectedCrashLogs].filter((id) => allowed.has(id)));
    } catch (error) {
      if (context !== state.snapshot.accountContext) return;
      state.crashLogs = {
        logs: [],
        status: 'unavailable',
        warnings: [errorText(error)],
        scope: 'Log locations could not be checked.'
      };
    } finally {
      if (context === state.snapshot.accountContext) {
        state.crashLogsLoading = false;
        if (state.view === 'diagnostics') render();
      }
    }
  }
  async function pollCrashNotice() {
    const watch = state.crashWatch;
    const context = state.snapshot.accountContext;
    if (state.preview || state.loading || state.busy || watch.checking) return;
    const now = Date.now(), interval = watch.watching || watch.pending ? 2000 : 15000;
    if (now - watch.lastCheck < interval) return;
    watch.lastCheck = now;
    watch.checking = true;
    try {
      const result = await request('crashLogNotice');
      if (context !== state.snapshot.accountContext) return;
      watch.watching = result?.watching === true;
      watch.pending = result?.pending === true;
      const notice = result?.notice;
      if (notice && typeof notice.id === 'string' && typeof notice.logId === 'string')
        enqueueNotice({ ...notice, type: 'game-crash', fingerprint: `game-crash:${notice.id}` });
    } catch {
      watch.pending = false;
    } finally {
      watch.checking = false;
    }
  }
  async function checkGameBuild(force = false) {
    if (state.preview || state.busy || state.loading) return;
    const context = state.snapshot.accountContext;
    const now = Date.now();
    if (now - state.gameBuildChecked < (force ? 60000 : 300000)) return;
    state.gameBuildChecked = now;
    try {
      const result = await request('gameBuildCheck');
      if (context !== state.snapshot.accountContext) return;
      state.gameBuild = result;
      if (result?.changed || result?.notice) {
        state.diagnosticStale = true;
        await loadDirectories();
        if (context !== state.snapshot.accountContext) return;
        enqueueNotice({ type: 'game-updated', id: result.notice?.id, message: result.notice?.message || result.message, build: result.build,
          fingerprint: `game-build:${result.notice?.id || JSON.stringify(result.build)}` });
      }
    } catch {
      /* An unavailable build number does not stop ordinary mod management. */
    }
  }
  async function loadSaveBackups() {
    if (state.preview || state.saveBackupsLoading) return;
    const context = state.snapshot.accountContext;
    state.saveBackupsLoading = true;
    if (state.view === 'saves') updateSaveBackupsPanel();
    try {
      const result = await request('saveBackupsList');
      if (context !== state.snapshot.accountContext) return;
      state.saveBackups = result;
      for (const id of state.savePictures.keys())
        if (!saveBackupList().some((backup) => backup.id === id && backup.pictureAvailable === true)) state.savePictures.delete(id);
      if (state.view === 'saves' && state.savesTab === 'backups' && !document.hidden) loadSavePictures();
    } catch (error) {
      if (context === state.snapshot.accountContext) state.saveBackups = { backups: [], available: false, reason: errorText(error) };
    } finally {
      if (context === state.snapshot.accountContext) state.saveBackupsLoading = false;
      if (context === state.snapshot.accountContext && state.view === 'saves') updateSaveBackupsPanel();
    }
  }
  function updateSaveBackupsPanel() {
    if (state.view !== 'saves' || state.savesTab !== 'backups') return;
    const panel = $('#save-backups-panel');
    if (!panel) return;
    const focused = rememberMainFocus();
    panel.outerHTML = renderSaveBackupsPanel();
    bindSaveBackupControls($('#save-backups-panel'));
    updateSavesGameStatus();
    restoreMainFocus(focused);
  }
  function bindSaveBackupControls(root) {
    if (!root) return;
    const profileFilter = $('#save-profile-filter', root);
    if (profileFilter) profileFilter.onchange = () => {
      const profile = saveBackupProfiles().find((item) => item.id === profileFilter.value);
      state.saveProfileFilter = profile ? { context: state.snapshot.accountContext, ...profile } : null;
      updateSaveBackupsPanel(); loadSavePictures();
    };
    $$('[data-save-select]', root).forEach((el) => (el.onclick = () => selectSaveBackup(el.dataset.saveSelect)));
    $$('[data-backup-restore]', root).forEach((el) => (el.onclick = () => reviewSaveBackup(el.dataset.backupRestore)));
    $$('[data-backup-delete]', root).forEach((el) => (el.onclick = () => reviewSaveDeletion(el.dataset.backupDelete, false)));
    $$('[data-backup-files]', root).forEach((el) => (el.onclick = () => showSaveFiles(el.dataset.backupFiles)));
    $$('[data-backup-picture]', root).forEach((el) => (el.onclick = () => showSavePicture(el.dataset.backupPicture)));
    const keep = $('#save-backup-keep', root);
    if (keep) keep.onchange = () => saveBackupOptions(state.saveBackups?.settings?.automatic === true, Number(keep.value));
    const options = $('#save-gallery-options', root);
    if (options) options.ontoggle = () => { state.saveOptionsOpen = options.open; };
    const selected = selectedSaveBackup(), context = state.snapshot.accountContext;
    if (selected && $('#save-details-label', root)) {
      const draft = saveDetailsDraft(selected);
      const changed = () => {
        if (context !== state.snapshot.accountContext || selectedSaveBackup()?.id !== selected.id) return;
        draft.label = $('#save-details-label').value;
        draft.note = $('#save-details-note').value;
        draft.pinned = $('#save-details-pinned').checked;
        draft.dirty = draft.label !== (selected.label || '') || draft.note !== (selected.note || '') || draft.pinned !== (selected.pinned === true);
        $('#save-details-status').textContent = draft.dirty ? 'Not saved yet' : 'Details stay on this computer.';
        $('#save-details-confirm').disabled = !draft.dirty || selected.detailsEditable === false || state.busy || state.saveBackups?.recovery?.pending;
      };
      $('#save-details-label').oninput = changed;
      $('#save-details-note').oninput = changed;
      $('#save-details-pinned').onchange = changed;
      $('#save-details-confirm').onclick = saveSelectedBackupDetails;
    }
    $$('[data-action]', root).forEach((el) => (el.onclick = (event) => { event.preventDefault(); handleAction(el.dataset.action); }));
    bindSaveThumbnailVisibility('backups', root);
  }
  function saveBackupReason(reason) {
    return { manual: 'Manual backup', 'before-enabling-mods': 'Before enabling mods', 'before-profile': 'Before launching a profile',
      'after-profile': 'After a profile session', 'before-restore': 'Before restoring saves', 'before-recovery': 'Before recovery',
      imported: 'Imported save files', 'before-delete-save': 'Before deleting a save' }[reason] || reason || 'Save backup';
  }
  function renderSaves() {
    const snapshot = state.snapshot, game = snapshot.settings.gamePath || '',
      blocked = snapshot.libraryBackupRecovery?.pending,
      location = state.saveBackups?.location || snapshot.saveLocation;
    const actions = !blocked
      ? button('Refresh', 'save-backups-refresh', 'refresh') + button('Import saves as backup…', 'save-import', 'folder') + button('Back up now', 'backup-saves', 'save', 'primary')
      : '';
    return `${header('SAVES', 'Your game saves', 'Import shared .sav files as a backup, then review a restore. Select a current slot to import into that slot instead.', actions)}${previewNote()}<div class="saves-context"><div><span class="field-label">Selected installation</span><code>${escape(game || (snapshot.platform === 'linux' ? 'Choose a game installation so the toolkit can find its Proton saves.' : 'Choose your Days Gone installation in Settings.'))}</code>${location ? `<span class="field-label sg-location-label">Save location</span>${location.resolved === true && typeof location.path === 'string' ? `<code>${escape(location.path)}</code>` : `<p class="field-note">${escape(location.reason || 'The save location has not been identified safely yet.')}</p>`}` : ''}</div><button type="button" class="button small" data-view="${game ? 'folders' : 'settings'}">${icon('folder')}${game ? 'Check folders' : 'Choose game folder'}</button></div><div id="saves-game-status" role="status">${savesGameStatus()}</div><div class="sg-tabs" aria-label="Save collections"><button type="button" class="button" data-saves-tab="current" aria-pressed="${state.savesTab === 'current'}">${icon('save')}Current saves</button><button type="button" class="button" data-saves-tab="backups" aria-pressed="${state.savesTab === 'backups'}">${icon('folder')}Backups</button></div>${state.savesTab === 'backups' ? renderSaveBackupsPanel() : renderCurrentSavesPanel()}`;
  }
  function setSavesTab(tab) {
    if (!['current', 'backups'].includes(tab)) return;
    state.savesTab = tab;
    if (state.view !== 'saves') navigate('saves');
    else render();
    if (tab === 'backups' && !state.snapshot.libraryBackupRecovery?.pending) loadSaveBackups();
    if (tab === 'current') loadCurrentSaves();
  }
  function currentSavesVisible() {
    return !state.preview && !document.hidden && !state.nativeWindowHidden &&
      state.snapshot.displayMetrics?.windowHidden !== true && state.view === 'saves' && state.savesTab === 'current';
  }
  function currentSavesCanRead() {
    return currentSavesVisible() && !state.loading && !state.busy && !state.nativeDropActivity?.running &&
      !state.profileSession?.active && !state.snapshot.libraryBackupRecovery?.pending &&
      !state.snapshot.saveRecoveryRequired && !state.saveBackups?.recovery?.pending && !$('#modal')?.open;
  }
  let currentSavesTimer = null, currentSavesRequest = false, saveThumbnailWasVisible = false;
  function syncCurrentSavesPoll() {
    if (currentSavesTimer !== null) clearTimeout(currentSavesTimer);
    currentSavesTimer = null;
    const thumbnailsVisible = saveThumbnailsVisible('current') || saveThumbnailsVisible('backups');
    if (thumbnailsVisible && !saveThumbnailWasVisible) resetCurrentPreviewFailures(false);
    saveThumbnailWasVisible = thumbnailsVisible;
    for (const kind of ['current', 'backups']) if (!saveThumbnailsVisible(kind)) stopSaveThumbnailWatch(kind);
    if (!saveThumbnailsVisible('current') && !saveThumbnailsVisible('backups') && saveThumbnailPumpTimer !== null) {
      clearTimeout(saveThumbnailPumpTimer); saveThumbnailPumpTimer = null;
    }
    if (saveThumbnailsVisible('backups') && state.saveBackups && !saveThumbnailWatches.backups.root)
      bindSaveThumbnailVisibility('backups', $('#save-backups-panel'));
    if (currentSavesVisible() && state.currentSaves && !saveThumbnailWatches.current.root)
      bindSaveThumbnailVisibility('current', $('#current-saves-panel'));
    if (!currentSavesVisible()) return;
    currentSavesTimer = setTimeout(async () => {
      currentSavesTimer = null;
      await loadCurrentSaves();
      syncCurrentSavesPoll();
    }, 3000);
  }
  function currentSaveFiles() {
    return (Array.isArray(state.currentSaves?.saves) ? state.currentSaves.saves : []).filter((save) =>
      save && typeof save.id === 'string' && save.id.length > 0 && save.id.length <= 256 &&
      typeof save.name === 'string' && save.name.length <= 512 && Number.isFinite(save.size) && save.size >= 0).slice(0, 256);
  }
  function selectedCurrentSave() {
    const files = currentSaveFiles(), context = state.currentSaves?.context;
    const selected = files.find((save) => state.currentSaveSelection?.context === context && save.id === state.currentSaveSelection.id) || files[0];
    if (selected) state.currentSaveSelection = { context, id: selected.id };
    return selected || null;
  }
  const saveThumbnailEntryLimit = 24, saveThumbnailByteLimit = 16 * 1024 * 1024;
  const saveThumbnailWatches = {
    current: { observer: null, root: null, serial: 0, scope: '', visible: new Set(), attempted: new Set(), evicted: new Set(), measured: false },
    backups: { observer: null, root: null, serial: 0, scope: '', visible: new Set(), attempted: new Set(), evicted: new Set(), measured: false }
  };
  let saveThumbnailPumpTimer = null, saveThumbnailFallbackListening = false;
  function saveThumbnailBytes() {
    return [...state.currentSavePreviews.values()].reduce((bytes, picture) => bytes + (picture?.url?.length || 0), 0) +
      [...state.savePictures.values()].reduce((bytes, url) => bytes + (typeof url === 'string' ? url.length : 0), 0);
  }
  function trimSaveThumbnailCaches(active) {
    const currentPin = active === 'current' && saveThumbnailsVisible('current') ? selectedCurrentSave()?.id : null,
      backupPin = active === 'backups' && saveThumbnailsVisible('backups') ? selectedSaveBackup()?.id : null;
    const groups = [
      { kind: 'current', cache: state.currentSavePreviews, pin: currentPin ? currentSavePreviewKey(currentPin) : null },
      { kind: 'backups', cache: state.savePictures, pin: backupPin }
    ];
    const evict = (group) => {
      // The selected picture must remain usable in the side viewer. All valid
      // pictures are individually smaller than the total budget, so evict other
      // entries first; the fallback also bounds unexpectedly oversized input.
      const key = [...group.cache.keys()].find((value) => value !== group.pin) ?? group.cache.keys().next().value;
      if (key === undefined) return;
      const entry = group.cache.get(key), hadImage = group.kind === 'current' ? !!entry?.url : !!entry;
      group.cache.delete(key);
      const id = group.kind === 'current' ? currentSaveFiles().find((save) => currentSavePreviewKey(save.id) === key)?.id : key;
      if (!id) return;
      const watch = saveThumbnailWatches[group.kind];
      if (!watch.visible.has(id)) watch.attempted.delete(id);
      // A missing-picture verdict is small and already painted truthfully; do
      // not replace profile/no-picture explanations with a fictitious image.
      if (!hadImage) return;
      watch.evicted.add(id);
      while (watch.evicted.size > 256) watch.evicted.delete(watch.evicted.values().next().value);
      // A detached map entry must not keep its large data URL alive in an old
      // card. Re-entry/selection can request it again through the same queue.
      if (group.kind === 'current') updateCurrentPreviewElements(id);
      else updateSaveBackupPictureElements(id);
    };
    for (const group of groups)
      while (group.cache.size > saveThumbnailEntryLimit) evict(group);
    const ordered = active === 'current' ? [groups[1], groups[0]] : groups;
    for (const group of ordered)
      while (group.cache.size && saveThumbnailBytes() > saveThumbnailByteLimit) evict(group);
  }
  function saveThumbnailsVisible(kind) {
    return !state.preview && !document.hidden && !state.nativeWindowHidden &&
      state.snapshot.displayMetrics?.windowHidden !== true && state.view === 'saves' && state.savesTab === kind;
  }
  function saveThumbnailsCanRead(kind) {
    return kind === 'current' ? currentSavesCanRead() : saveThumbnailsVisible(kind) && !state.loading && !state.busy &&
      !state.nativeDropActivity?.running && !$('#modal')?.open;
  }
  function saveThumbnailScope(kind) {
    return `${state.snapshot.accountContext || ''}:${kind === 'current' ? state.currentSaves?.context || '' : 'backups'}`;
  }
  function stopSaveThumbnailWatch(kind) {
    const watch = saveThumbnailWatches[kind];
    if (watch.observer) watch.observer.disconnect();
    watch.observer = null; watch.root = null; watch.visible.clear(); watch.attempted.clear(); watch.evicted.clear(); watch.measured = false;
    ++watch.serial;
  }
  function queueSaveThumbnailWork() {
    if (saveThumbnailPumpTimer !== null) return;
    saveThumbnailPumpTimer = setTimeout(() => {
      saveThumbnailPumpTimer = null;
      if (saveThumbnailsCanRead('current')) loadCurrentSavePreviews();
      else if (saveThumbnailsCanRead('backups')) loadSavePictures();
    }, 0);
  }
  function resumeSaveThumbnails() {
    if (saveThumbnailsVisible('current') || saveThumbnailsVisible('backups')) {
      updateSavesGameStatus(); queueSaveThumbnailWork();
    }
  }
  function measureSaveThumbnailVisibility(kind) {
    const watch = saveThumbnailWatches[kind];
    if (!watch.root || watch.observer) return;
    const selector = kind === 'current' ? '[data-current-save-select]' : '[data-save-select]',
      field = kind === 'current' ? 'currentSaveSelect' : 'saveSelect',
      height = Number(window.innerHeight || document.documentElement.clientHeight);
    if (!(height > 0)) return;
    const cards = $$(selector, watch.root).filter((card) => typeof card.getBoundingClientRect === 'function');
    if (!cards.length) return;
    const previous = new Set(watch.visible);
    watch.measured = true; watch.visible.clear();
    for (const card of cards) {
      const rect = card.getBoundingClientRect();
      if (rect.bottom > -120 && rect.top < height + 120) watch.visible.add(card.dataset[field]);
    }
    for (const id of previous) if (!watch.visible.has(id)) watch.attempted.delete(id);
  }
  function bindSaveThumbnailVisibility(kind, root) {
    stopSaveThumbnailWatch(kind === 'current' ? 'backups' : 'current');
    stopSaveThumbnailWatch(kind);
    if (!root || !saveThumbnailsVisible(kind)) return;
    const watch = saveThumbnailWatches[kind], serial = watch.serial, scope = saveThumbnailScope(kind),
      selector = kind === 'current' ? '[data-current-save-select]' : '[data-save-select]',
      field = kind === 'current' ? 'currentSaveSelect' : 'saveSelect';
    watch.root = root; watch.scope = scope;
    if (typeof IntersectionObserver === 'function') {
      watch.observer = new IntersectionObserver((entries) => {
        if (watch.serial !== serial || watch.scope !== scope || scope !== saveThumbnailScope(kind)) return;
        for (const entry of entries) {
          const id = entry.target?.dataset?.[field];
          if (!id) continue;
          if (entry.isIntersecting) watch.visible.add(id);
          else { watch.visible.delete(id); watch.attempted.delete(id); }
        }
        queueSaveThumbnailWork();
      }, { rootMargin: '120px', threshold: 0.01 });
      for (const card of $$(selector, root)) watch.observer.observe(card);
    } else {
      measureSaveThumbnailVisibility(kind);
      if (!saveThumbnailFallbackListening) {
        const changed = () => {
          if (saveThumbnailsVisible('current') || saveThumbnailsVisible('backups')) queueSaveThumbnailWork();
        };
        document.addEventListener?.('scroll', changed, { passive: true, capture: true });
        window.addEventListener?.('resize', changed);
        saveThumbnailFallbackListening = true;
      }
    }
    queueSaveThumbnailWork();
  }
  function saveThumbnailCandidates(kind, entries, selected) {
    const watch = saveThumbnailWatches[kind], scope = saveThumbnailScope(kind);
    if (watch.scope !== scope) { watch.scope = scope; watch.visible.clear(); watch.attempted.clear(); watch.evicted.clear(); }
    measureSaveThumbnailVisibility(kind);
    const eligible = kind === 'current' ? (item) => item?.previewEligible === true : (item) => item?.pictureAvailable === true;
    const visible = watch.observer || watch.measured ? entries.filter((item) => watch.visible.has(item.id))
      : entries.slice(0, kind === 'current' ? 3 : 4);
    return [selected, ...visible].filter((item, index, all) => eligible(item) &&
      all.findIndex((other) => other?.id === item.id) === index && !watch.attempted.has(item.id) &&
      !(kind === 'current' ? state.currentSavePreviews.has(currentSavePreviewKey(item.id)) : state.savePictures.has(item.id)));
  }
  function currentSavePreviewKey(id, context = state.currentSaves?.context) { return `${context || ''}:${id}`; }
  function resetCurrentPreviewFailures(force = false) {
    for (const save of currentSaveFiles()) {
      const key = currentSavePreviewKey(save.id), picture = state.currentSavePreviews.get(key);
      if (!picture?.url && picture?.retryable === true && (force || Date.now() >= (picture.retryAfter || 0))) {
        state.currentSavePreviews.delete(key); saveThumbnailWatches.current.attempted.delete(save.id);
      }
    }
    if (force) saveThumbnailWatches.current.attempted.clear();
  }
  function currentSavePictureResult(result) {
    const picture = { url: result?.actualGamePreview === true ? cleanSavePicture(result) : '',
      reason: 'Preview unavailable', reasonCode: 'unverified-picture', retryable: true,
      retryAfter: Date.now() + 5000 };
    if (picture.url) { picture.reason = ''; picture.retryable = false; return picture; }
    if (typeof result?.reason === 'string' && result.reason.trim()) picture.reason = result.reason.trim().slice(0, 512);
    if (typeof result?.reasonCode === 'string' && /^[a-z-]{1,64}$/.test(result.reasonCode)) picture.reasonCode = result.reasonCode;
    picture.retryable = result?.retryable !== false && !['profile-data', 'no-picture'].includes(picture.reasonCode);
    return picture;
  }
  function currentSavePicture(save) {
    const picture = state.currentSavePreviews.get(currentSavePreviewKey(save.id));
    const message = picture?.reasonCode === 'profile-data' ? 'Profile data · no in-game picture' :
      picture?.reasonCode === 'no-picture' ? 'No in-game picture stored' :
      picture?.reason || (saveThumbnailWatches.current.evicted.has(save.id) ? 'Picture available · select to reload' :
        save.previewEligible ? 'Loading in-game picture…' : 'No in-game picture available');
    return picture?.url ? `<img src="${escape(picture.url)}" alt="In-game save preview for ${escape(save.name)}" loading="lazy" decoding="async">`
      : `<span class="sg-picture-empty">${icon(picture?.reasonCode === 'profile-data' ? 'report' : 'camera')}<span>${escape(message)}${picture?.retryable && !/refresh/i.test(message) ? '<br><small>Use Refresh to try again.</small>' : ''}</span></span>`;
  }
  function renderCurrentSaveActionReason(save) {
    const reason = save ? currentSaveChangeReason(currentSaveGuard(save.id)) : '';
    return `<p class="field-note" id="current-save-action-reason" role="status" ${reason ? '' : 'hidden'}>${escape(reason)}</p>`;
  }
  function renderCurrentSavesPanel() {
    const data = state.currentSaves, files = currentSaveFiles(), selected = selectedCurrentSave();
    const changeReason = selected ? currentSaveChangeReason(currentSaveGuard(selected.id)) : '',
      readReason = selected ? currentSaveChangeReason(currentSaveGuard(selected.id), true) : '';
    const previewsSupported = data?.previewsSupported !== false;
    const deferred = state.snapshot.libraryBackupRecovery?.pending || state.snapshot.saveRecoveryRequired || state.saveBackups?.recovery?.pending;
    const notices = `${data?.limited ? '<p class="field-note" role="status">This folder contains more entries than can be shown in one check. The list may be incomplete.</p>' : ''}${Number(data?.skipped) > 0 ? '<p class="field-note" role="status">Some save entries could not be inspected safely. Refresh after the game finishes saving.</p>' : ''}`;
    const message = deferred ? 'Finish the pending recovery before checking current saves.' :
      data?.message || (state.currentSavesLoading || !data ? 'Reading the game’s save folder…' : 'No save files were found in the selected save folder.');
    return `<section class="sg-manager sg-current-manager${previewsSupported ? '' : ' sg-current-files-only'}" id="current-saves-panel" aria-labelledby="current-saves-title"><div class="section-heading"><div><h2 id="current-saves-title">Current save files <span>${files.length}</span></h2><p>Read directly from the game’s save folder. No backup is needed to browse them.</p></div><span class="sg-live-status">${state.currentSavesLoading ? 'Checking…' : 'Updates while this page is open'}</span></div>${notices}${selected && !deferred ? `<div class="sg-layout"><div class="sg-gallery-column"><div class="sg-gallery" aria-label="Choose a current save">${files.map((save) => `<button type="button" class="sg-card" data-current-save-select="${escape(save.id)}" aria-pressed="${save.id === selected.id}" aria-controls="current-save-context">${previewsSupported ? `<span class="sg-card-picture" data-current-preview="${escape(save.id)}">${currentSavePicture(save)}</span>` : ''}<span class="sg-card-copy"><strong>${escape(save.name)}</strong><small>${escape(logTime(save.modifiedUtc))}</small><small>${formatBytes(save.size)}</small></span></button>`).join('')}</div></div><aside class="sg-selected" id="current-save-context" aria-label="Selected current save">${previewsSupported ? `<button type="button" class="sg-selected-picture" data-current-preview="${escape(selected.id)}" data-current-save-open="${escape(selected.id)}" aria-label="View in-game preview for ${escape(selected.name)}" ${state.currentSavePreviews.get(currentSavePreviewKey(selected.id))?.url ? '' : 'disabled'}>${currentSavePicture(selected)}</button>` : ''}<div class="sg-selected-body"><span class="eyebrow">CURRENT SAVE</span><h3>${escape(selected.name)}</h3><dl class="sg-backup-facts sg-current-facts"><div><dt>Modified</dt><dd>${escape(logTime(selected.modifiedUtc))}</dd></div><div><dt>File size</dt><dd>${formatBytes(selected.size)}</dd></div></dl>${previewsSupported ? '<p class="field-note">The picture, when available, comes from this game save. The date above is the file’s modified time.</p>' : '<p class="field-note">The date shown is the file’s modified time.</p>'}<p class="field-note">To keep a copy of all current saves, use Back up now above. Your existing backups are in the Backups tab.</p>${!saveEditorAvailable() || selected.deleteEligible === true || selected.exportEligible === true || selected.replaceEligible === true ? `<div class="sg-selected-actions">${selected.replaceEligible === true ? `<button type="button" class="button small" data-current-save-edit="${escape(selected.id)}" ${!saveEditorAvailable() ? 'aria-describedby="save-editor-availability"' : changeReason ? 'aria-describedby="current-save-action-reason"' : ''} ${!saveEditorAvailable() || changeReason ? 'disabled' : ''}>${icon('wrench')}${state.saveEditor?.sourceId === selected.id && state.saveEditor.session.inspectionOnly !== true ? 'Continue edit…' : 'Edit save…'}</button>` : ''}${selected.exportEligible === true ? `<button type="button" class="button small" data-current-save-inspect="${escape(selected.id)}" ${!saveEditorAvailable() || readReason ? 'disabled' : ''}>${icon('search')}${state.saveEditor?.sourceId === selected.id && state.saveEditor.session.inspectionOnly === true ? 'Continue inspection…' : 'Inspect save…'}</button>` : ''}${selected.exportEligible === true ? `<button type="button" class="button small" data-current-save-export="${escape(selected.id)}" ${readReason ? 'disabled' : ''}>${icon('download')}Export save…</button>` : ''}${selected.replaceEligible === true ? `<button type="button" class="button small" data-current-save-import="${escape(selected.id)}" ${changeReason ? 'disabled' : ''}>${icon('folder')}Import into this slot…</button>` : ''}${selected.deleteEligible === true ? `<button type="button" class="button small" data-current-save-delete="${escape(selected.id)}" ${changeReason ? 'disabled' : ''}>${icon('trash')}Delete save…</button>` : ''}</div>` : ''}${renderCurrentSaveActionReason(selected)}${!saveEditorAvailable() ? `<p class="field-note" id="save-editor-availability">${escape(saveEditorUnavailableReason())} Save pictures, backups, import and export are still available.</p>` : ''}
</div></aside></div>` : `<div class="empty-state compact sg-empty">${icon('save')}<h3>${deferred ? 'Recovery needs attention' : data?.status === 'ambiguous' ? 'Choose a save location' : data?.status === 'unavailable' ? 'Saves could not be read' : files.length ? 'Current saves' : 'No current saves listed yet'}</h3><p>${escape(message)}</p>${deferred ? button(state.snapshot.libraryBackupRecovery?.pending ? 'Review library recovery' : 'Review save recovery', state.snapshot.libraryBackupRecovery?.pending ? 'library-backup-recovery' : 'save-backups-recovery', 'shield', 'small') : ''}</div>`}</section>`;
  }
  function updateCurrentSavesPanel() {
    if (state.view !== 'saves' || state.savesTab !== 'current') return;
    const panel = $('#current-saves-panel');
    if (!panel) return;
    const focused = rememberMainFocus();
    panel.outerHTML = renderCurrentSavesPanel();
    bindCurrentSaveControls($('#current-saves-panel'));
    restoreMainFocus(focused);
  }
  async function loadCurrentSaves() {
    if (!currentSavesCanRead() || currentSavesRequest) return false;
    const account = state.snapshot.accountContext;
    currentSavesRequest = true; state.currentSavesLoading = true;
    if (!state.currentSaves) updateCurrentSavesPanel();
    try {
      const result = await request('currentSavesList');
      if (account !== state.snapshot.accountContext || !currentSavesVisible()) return false;
      if (!result || typeof result.context !== 'string' || typeof result.signature !== 'string' || !Array.isArray(result.saves))
        throw new Error('The save folder check did not return a usable file list. Try Refresh.');
      const changed = !state.currentSaves || result.context !== state.currentSaves.context ||
        result.signature !== state.currentSaves.signature || result.status !== state.currentSaves.status || result.message !== state.currentSaves.message;
      state.currentSavesLoading = false;
      if (changed) {
        state.currentSaves = result;
        const liveKeys = new Set(currentSaveFiles().map((save) => currentSavePreviewKey(save.id)));
        for (const key of state.currentSavePreviews.keys()) if (!liveKeys.has(key)) state.currentSavePreviews.delete(key);
        updateCurrentSavesPanel();
      }
      loadCurrentSavePreviews();
      return true;
    } catch (error) {
      if (account === state.snapshot.accountContext) {
        const message = errorText(error);
        if (state.currentSaves?.status !== 'unavailable' || state.currentSaves?.message !== message) {
          state.currentSaves = { status: 'unavailable', context: '', signature: '', saves: [], message };
          state.currentSavesLoading = false;
          state.currentSavePreviews.clear(); updateCurrentSavesPanel();
        }
      }
      return false;
    } finally {
      currentSavesRequest = false;
      if (account === state.snapshot.accountContext) state.currentSavesLoading = false;
    }
  }
  let currentPreviewsLoading = false, currentPreviewsPending = false;
  async function loadCurrentSavePreviews() {
    if (currentPreviewsLoading || savePicturesLoading) { currentPreviewsPending = true; return; }
    if (!currentSavesCanRead()) return;
    const account = state.snapshot.accountContext, context = state.currentSaves?.context;
    const selected = selectedCurrentSave(), files = currentSaveFiles();
    if (!context || !selected || state.currentSaves?.previewsSupported === false) return;
    if (!saveThumbnailWatches.current.root || saveThumbnailWatches.current.scope !== saveThumbnailScope('current'))
      bindSaveThumbnailVisibility('current', $('#current-saves-panel'));
    const selectedKey = currentSavePreviewKey(selected.id, context), selectedPicture = state.currentSavePreviews.get(selectedKey);
    if (selectedPicture) { state.currentSavePreviews.delete(selectedKey); state.currentSavePreviews.set(selectedKey, selectedPicture); }
    const watch = saveThumbnailWatches.current;
    const candidates = saveThumbnailCandidates('current', files, selected).slice(0, 12);
    currentPreviewsLoading = true;
    try {
      for (const save of candidates) {
        if (!currentSavesCanRead() || account !== state.snapshot.accountContext || context !== state.currentSaves?.context) break;
        const key = currentSavePreviewKey(save.id, context);
        if (state.currentSavePreviews.has(key)) continue;
        if (watch.observer && save.id !== selectedCurrentSave()?.id && !watch.visible.has(save.id)) continue;
        watch.attempted.add(save.id);
        let picture = currentSavePictureResult(null);
        try {
          const result = await request('currentSavePreview', { id: save.id });
          if (!currentSavesVisible() || account !== state.snapshot.accountContext || context !== state.currentSaves?.context ||
              !currentSaveFiles().some((item) => item.id === save.id)) break;
          if (result?.id !== save.id || result?.context !== context) continue;
          picture = currentSavePictureResult(result);
        } catch { /* Unsupported or changing saves stay readable without a picture. */ }
        if (!currentSavesVisible() || account !== state.snapshot.accountContext || context !== state.currentSaves?.context ||
            !currentSaveFiles().some((item) => item.id === save.id)) break;
        state.currentSavePreviews.set(key, picture);
        watch.evicted.delete(save.id);
        trimSaveThumbnailCaches('current');
        updateCurrentPreviewElements(save.id);
      }
    } finally {
      currentPreviewsLoading = false;
      const pending = currentPreviewsPending; currentPreviewsPending = false;
      if (saveThumbnailsCanRead('current') && (pending || saveThumbnailCandidates('current', currentSaveFiles(), selectedCurrentSave()).length))
        queueSaveThumbnailWork();
      else if (savePicturesSelectionPending && saveThumbnailsCanRead('backups')) queueSaveThumbnailWork();
    }
  }
  function updateCurrentPreviewElements(id) {
    if (!currentSavesVisible()) return;
    const save = currentSaveFiles().find((item) => item.id === id);
    if (!save) return;
    $$('[data-current-preview]', $('#current-saves-panel')).forEach((element) => {
      if (element.dataset.currentPreview !== id) return;
      element.innerHTML = currentSavePicture(save);
      if (element.dataset.currentSaveOpen) element.disabled = !state.currentSavePreviews.get(currentSavePreviewKey(id))?.url;
    });
  }
  function bindCurrentSaveControls(root) {
    if (!root) return;
    $$('[data-current-save-export]', root).forEach((element) => (element.onclick = () => exportCurrentSave(element.dataset.currentSaveExport)));
    $$('[data-current-save-import]', root).forEach((element) => (element.onclick = () => importCurrentSave(element.dataset.currentSaveImport)));
    $$('[data-current-save-edit]', root).forEach((element) => (element.onclick = () => openSaveEditor(element.dataset.currentSaveEdit)));
    $$('[data-current-save-inspect]', root).forEach((element) => (element.onclick = () => openSaveEditor(element.dataset.currentSaveInspect, true)));
    $$('[data-current-save-delete]', root).forEach((element) => (element.onclick = () => reviewSaveDeletion(element.dataset.currentSaveDelete, true)));
    $$('[data-current-save-select]', root).forEach((element) => { element.onclick = () => {
      if (!currentSaveFiles().some((save) => save.id === element.dataset.currentSaveSelect)) return;
      state.currentSaveSelection = { context: state.currentSaves.context, id: element.dataset.currentSaveSelect };
      updateCurrentSavesPanel(); loadCurrentSavePreviews();
    }; });
    $$('[data-action]', root).forEach((element) => (element.onclick = () => handleAction(element.dataset.action)));
    $$('[data-current-save-open]', root).forEach((element) => { element.onclick = () => {
      const save = currentSaveFiles().find((item) => item.id === element.dataset.currentSaveOpen),
        picture = save && state.currentSavePreviews.get(currentSavePreviewKey(save.id));
      if (save && picture?.url) modal(save.name, `<img class="save-picture-preview" src="${escape(picture.url)}" alt="In-game save preview for ${escape(save.name)}"><p class="field-note">Read from the current game save.</p>`);
    }; });
    bindSaveThumbnailVisibility('current', root);
  }
  function savesGameStatus() {
    return state.snapshot.gameRunning === true
      ? `<div class="banner warning">${icon('info')}<p>Days Gone is running. You can browse files, pictures and read-only save data; close the game before editing or making a backup.</p></div>` : '';
  }
  function updateSavesGameStatus() {
    updateSaveRestoreStatus();
    updateSaveDeleteStatus();
    updateCurrentSaveReplaceStatus();
    updateSaveEditorStatus();
    if (state.view !== 'saves') return;
    const status = $('#saves-game-status');
    if (status) status.innerHTML = savesGameStatus();
    const location = state.saveBackups?.location || state.snapshot.saveLocation;
    [...$$('#main-content [data-action="backup-saves"], #main-content [data-backup-restore]'),
      ...$$('#main-content [data-current-save-delete], #main-content [data-current-save-import], #main-content [data-current-save-edit], #main-content [data-current-save-inspect]')].forEach((control) => {
      const currentId = control.dataset.currentSaveDelete ?? control.dataset.currentSaveImport ?? control.dataset.currentSaveEdit ?? control.dataset.currentSaveInspect;
      if (currentId !== undefined) {
        const reason = currentSaveChangeReason(currentSaveGuard(currentId), control.dataset.currentSaveInspect !== undefined) ||
          ((control.dataset.currentSaveEdit !== undefined || control.dataset.currentSaveInspect !== undefined) && !saveEditorAvailable() ? saveEditorUnavailableReason() : '');
        control.disabled = !!reason;
        control.title = reason;
        return;
      }
      control.disabled = state.busy || state.snapshot.gameRunning !== false || location?.resolved === false ||
        (control.dataset.currentSaveEdit !== undefined && !saveEditorAvailable());
      if (location?.resolved === false) control.title = location.reason || 'The save location has not been identified safely.';
    });
    const reasonElement = $('#current-save-action-reason');
    if (reasonElement) {
      const save = selectedCurrentSave(), reason = save ? currentSaveChangeReason(currentSaveGuard(save.id)) : '';
      reasonElement.textContent = reason; reasonElement.hidden = !reason;
    }
  }
  function renderSavesSettingsLink() {
    return `<section class="panel settings-full"><div class="panel-heading"><div><h2>Game save backups</h2><p>Save management now has its own page in the sidebar.</p></div>${icon('save')}</div><div class="panel-body"><p>Browse saves, manage backups and pictures, or change automatic backup options under Saves.</p><button type="button" class="button small" data-view="saves">${icon('save')}Open Saves</button></div></section>`;
  }
  function saveBackupList() {
    return (Array.isArray(state.saveBackups?.backups) ? state.saveBackups.backups : [])
      .filter((backup) => backup && typeof backup.id === 'string' && backup.id.length <= 128).slice(0, 256);
  }
  function saveBackupProfileLinks(backup) {
    if (backup?.profileLinksProblem || !Array.isArray(backup?.profiles)) return [];
    const seen = new Set();
    return backup.profiles.slice(0, 64).filter((profile) => {
      if (!profile || !/^[0-9a-f]{32}$/.test(profile.id) || typeof profile.name !== 'string' ||
          !profile.name.trim() || profile.name.length > 256 || !['before-launch', 'after-session'].includes(profile.stage)) return false;
      const key = `${profile.id}:${profile.stage}`;
      if (seen.has(key)) return false;
      seen.add(key); return true;
    });
  }
  function activeSaveProfileFilter() {
    return state.saveProfileFilter && state.saveProfileFilter.context === state.snapshot.accountContext &&
      /^[0-9a-f]{32}$/.test(state.saveProfileFilter.id) ? state.saveProfileFilter : null;
  }
  function saveBackupProfiles() {
    const profiles = new Map();
    for (const backup of saveBackupList()) for (const profile of saveBackupProfileLinks(backup))
      if (!profiles.has(profile.id)) profiles.set(profile.id, { id: profile.id, name: profile.name });
    const selected = activeSaveProfileFilter();
    if (selected && !profiles.has(selected.id)) profiles.set(selected.id, { id: selected.id, name: selected.name });
    return [...profiles.values()].sort((a, b) => a.name.localeCompare(b.name));
  }
  function filteredSaveBackupList() {
    const filter = activeSaveProfileFilter();
    return saveBackupList().filter((backup) => !filter || saveBackupProfileLinks(backup).some((profile) => profile.id === filter.id));
  }
  function saveProfileTags(backup, expanded = false) {
    const links = saveBackupProfileLinks(backup), shown = expanded ? links : links.slice(0, 2);
    return `${links.length ? `<span class="sg-profile-tags">${shown.map((profile) => `<span class="sg-profile-tag">${escape(profile.stage === 'before-launch' ? `Before launching ${profile.name}` : `After ${profile.name} session`)}</span>`).join('')}${links.length > shown.length ? `<small>${links.length - shown.length} more profile link${links.length - shown.length === 1 ? '' : 's'}</small>` : ''}</span>` : ''}${backup?.profileLinksProblem ? '<small class="sg-profile-warning">Profile links could not be verified.</small>' : ''}`;
  }
  function openProfileSaveBackups(id) {
    if (!/^[0-9a-f]{32}$/.test(id)) return false;
    const profile = (state.portableProfiles?.profiles || []).find((item) => item.id === id && item.status === 'ready');
    if (!profile || typeof profile.name !== 'string') return false;
    state.saveProfileFilter = { context: state.snapshot.accountContext, id, name: profile.name };
    setSavesTab('backups');
    return true;
  }
  function selectedSaveBackup(backups = filteredSaveBackupList()) {
    const context = state.snapshot.accountContext;
    const selected = backups.find((backup) => state.saveSelection?.context === context && backup.id === state.saveSelection.id) || backups[0];
    if (selected) state.saveSelection = { context, id: selected.id };
    return selected || null;
  }
  function selectSaveBackup(id) {
    const backup = filteredSaveBackupList().find((item) => item.id === id);
    if (!backup) return false;
    state.saveSelection = { context: state.snapshot.accountContext, id };
    updateSaveBackupsPanel();
    if (!state.busy && !document.hidden) loadSavePictures();
    return true;
  }
  function saveBackupTitle(backup) {
    return typeof backup?.label === 'string' && backup.label.trim() ? backup.label : logTime(backup?.createdAt);
  }
  function saveGalleryPicture(backup, expanded = false) {
    const picture = state.savePictures.get(backup.id);
    return picture ? `<img src="${escape(picture)}" alt="" loading="lazy" decoding="async">`
      : `<span class="sg-picture-placeholder">${icon('camera')}<span>${backup.pictureAvailable ? 'Picture available' : expanded ? 'Add a picture to recognise this backup' : 'No picture added'}</span></span>`;
  }
  function saveDetailsDraft(backup) {
    const context = state.snapshot.accountContext;
    let draft = state.saveDetailsDrafts.get(backup.id);
    if (!draft || draft.context !== context || !draft.dirty) {
      draft = { context, label: typeof backup.label === 'string' ? backup.label : '',
        note: typeof backup.note === 'string' ? backup.note : '', pinned: backup.pinned === true, dirty: false };
      state.saveDetailsDrafts.set(backup.id, draft);
    }
    while (state.saveDetailsDrafts.size > 256) state.saveDetailsDrafts.delete(state.saveDetailsDrafts.keys().next().value);
    return draft;
  }
  function renderSaveDetails(backup) {
    const draft = saveDetailsDraft(backup), blocked = state.busy || backup.detailsEditable === false || state.saveBackups?.recovery?.pending || state.snapshot.libraryBackupRecovery?.pending;
    return `<section class="sg-details-form" aria-label="Backup details">${backup.detailsProblem ? `<p class="field-note" role="status">${escape(backup.detailsProblem)}</p>` : ''}<label for="save-details-label">Backup name</label><input id="save-details-label" class="input" type="text" maxlength="100" value="${escape(draft.label)}" placeholder="A name to find it again" ${blocked ? 'disabled' : ''}><label for="save-details-note">Private note</label><textarea id="save-details-note" class="input" rows="3" maxlength="2000" placeholder="What you want to remember" ${blocked ? 'disabled' : ''}>${escape(draft.note)}</textarea><label class="sg-pin-option"><input type="checkbox" id="save-details-pinned" ${draft.pinned ? 'checked' : ''} ${blocked ? 'disabled' : ''}><span>Keep this backup<small>Leave it out of automatic cleanup.</small></span></label><div class="sg-details-save"><span id="save-details-status" class="field-note">${draft.dirty ? 'Not saved yet' : 'Details stay on this computer.'}</span><button type="button" class="button small" id="save-details-confirm" ${blocked || !draft.dirty ? 'disabled' : ''}>Save details</button></div></section>`;
  }
  async function saveSelectedBackupDetails() {
    const backup = selectedSaveBackup(), context = state.snapshot.accountContext;
    if (!backup || state.busy || backup.detailsEditable === false || state.saveBackups?.recovery?.pending || state.snapshot.libraryBackupRecovery?.pending) return false;
    const draft = saveDetailsDraft(backup);
    if (!draft.dirty || draft.context !== context) return false;
    const submitted = { label: draft.label, note: draft.note, pinned: draft.pinned };
    const result = await run('saveBackupDetails', { id: backup.id, ...submitted },
      { refresh: false, activity: false, progress: 'Saving backup details…' });
    if (!result || context !== state.snapshot.accountContext) return false;
    draft.dirty = draft.label !== submitted.label || draft.note !== submitted.note || draft.pinned !== submitted.pinned;
    await loadSaveBackups();
    if (context === state.snapshot.accountContext) toast(draft.dirty ? 'Details saved. Your newer edits are still unsaved.' : 'Backup details saved.');
    return true;
  }
  function renderSaveSelection(backup) {
    const location = state.saveBackups?.location || state.snapshot.saveLocation,
      restoring = state.busy || state.snapshot.gameRunning !== false || state.saveBackups?.recovery?.pending || location?.resolved === false;
    return `<aside class="sg-selected" aria-label="Selected save backup"><button type="button" class="sg-selected-picture" data-backup-preview="${escape(backup.id)}" data-backup-picture="${escape(backup.id)}" aria-label="${backup.pictureAvailable || state.savePictures.has(backup.id) ? 'View or change picture for' : 'Add a picture to'} ${escape(saveBackupTitle(backup))}">${saveGalleryPicture(backup, true)}</button><div class="sg-selected-body"><span class="eyebrow">SELECTED BACKUP</span><h3>${escape(saveBackupTitle(backup))}</h3>${backup.label ? `<p class="sg-date">${escape(logTime(backup.createdAt))}</p>` : ''}<p>${escape(saveBackupReason(backup.reason))}</p>${saveProfileTags(backup, true)}<dl class="sg-backup-facts"><div><dt>Files</dt><dd>${Number(backup.files) || 0}</dd></div><div><dt>Size</dt><dd>${formatBytes(Number(backup.bytes) || 0)}</dd></div></dl><div class="sg-selected-actions"><button type="button" class="button small" data-backup-files="${escape(backup.id)}">${icon('report')}View files</button><button type="button" class="button small" data-backup-picture="${escape(backup.id)}">${icon('camera')}${backup.pictureAvailable || state.savePictures.has(backup.id) ? 'Change picture' : 'Add picture'}</button><button type="button" class="button small" data-backup-restore="${escape(backup.id)}" aria-label="Restore backup from ${escape(logTime(backup.createdAt))}" ${restoring ? 'disabled' : ''}>${icon('back')}Review restore…</button><button type="button" class="button small" data-backup-delete="${escape(backup.id)}" ${state.busy || state.saveBackups?.recovery?.pending ? 'disabled' : ''}>${icon('trash')}Delete backup…</button></div>${renderSaveDetails(backup)}</div></aside>`;
  }
  function renderSaveBackupsPanel() {
    if (state.snapshot.libraryBackupRecovery?.pending)
      return `<section class="panel settings-full" id="save-backups-panel"><div class="panel-heading"><h2>Save backups</h2>${icon('shield')}</div><div class="panel-body"><p>Finish the interrupted library restore before checking save backups. Your save files have not been changed.</p>${button('Review library recovery', 'library-backup-recovery', 'shield', 'small')}</div></section>`;
    const data = state.saveBackups, settings = data?.settings || { automatic: false, keep: 5 },
      backups = filteredSaveBackupList(), selected = selectedSaveBackup(backups), filter = activeSaveProfileFilter(), profiles = saveBackupProfiles();
    const profileFilter = profiles.length ? `<label class="sg-profile-filter" for="save-profile-filter"><span>Profile</span><select class="input" id="save-profile-filter"><option value="">All profiles</option>${profiles.map((profile) => `<option value="${escape(profile.id)}" ${profile.id === filter?.id ? 'selected' : ''}>${escape(profile.name)}</option>`).join('')}</select></label>` : '';
    const options = `<details id="save-gallery-options" class="sg-options"${state.saveOptionsOpen ? ' open' : ''}><summary>Automatic backup preferences</summary><div class="setting-row"><div class="setting-copy"><strong>Automatic save backups</strong><p>Keep a copy before enabling mods and before and after profile sessions. A failed pre-change backup stops that change; a failed after-session backup leaves your restored mods in place. Unchanged saves reuse a copy.</p></div><button type="button" class="switch" role="switch" data-action="save-backups-toggle" aria-label="Automatic save backups" aria-checked="${settings.automatic === true}" ${data?.settings ? '' : 'disabled'}></button></div><div class="setting-row"><label for="save-backup-keep">Keep recent backups</label><select id="save-backup-keep" class="input" ${data?.settings ? '' : 'disabled'}>${Array.from({ length: 30 }, (_, i) => i + 1).map((count) => `<option value="${count}" ${Number(settings.keep) === count ? 'selected' : ''}>${count} backup${count === 1 ? '' : 's'}</option>`).join('')}</select></div></details>`;
    const notices = `${data?.reason ? `<p class="field-note" role="status">${escape(data.reason)}</p>` : ''}${data?.warnings?.length ? `<p class="field-note" role="status">${data.warnings.slice(0, 8).map((warning) => escape(warning)).join(' ')}</p>` : ''}${data?.recovery?.pending ? `<div class="banner warning"><div><strong>A save restore needs attention.</strong><p>${escape(data.recovery.reason || 'Review the recovery record before restoring another backup.')}</p>${button('Review recovery', 'save-backups-recovery', 'shield', 'small')}</div></div>` : ''}`;
    const empty = state.saveBackupsLoading ? 'Checking backups…' : filter && data?.available !== false ? `No backups are linked to ${filter.name} yet. Automatic backup preferences below can keep copies before and after profile sessions. This option applies to all profiles; launching never swaps your save files.` : data?.warnings?.length ? 'No verified backups are available. Preserved folders need a manual check.' : data?.available === false ? 'Backups could not be listed. Check the message above.' : 'No backups yet. Use Back up now to keep a copy of your current saves.';
    return `<section class="sg-manager" id="save-backups-panel">${notices}<section class="saved-backups" aria-labelledby="saved-backups-title"><div class="section-heading"><h2 id="saved-backups-title">Saved backups <span>${backups.length}</span></h2>${profileFilter}${button('Current save files', 'save-files', 'folder', 'small')}</div>${selected ? `<div class="sg-layout"><div class="sg-gallery-column"><div class="sg-gallery" aria-label="Choose a save backup">${backups.map((backup) => `<button type="button" id="save-card-${escape(backup.id)}" class="sg-card" data-save-select="${escape(backup.id)}" aria-pressed="${backup.id === selected.id}" aria-controls="save-selected-context"><span class="sg-card-picture" data-backup-preview="${escape(backup.id)}">${saveGalleryPicture(backup)}${backup.pinned ? `<span class="sg-pinned" aria-label="Kept out of automatic cleanup">${icon('lock')}</span>` : ''}</span><span class="sg-card-copy"><strong>${escape(saveBackupTitle(backup))}</strong><small>${backup.label ? escape(logTime(backup.createdAt)) + ' · ' : ''}${escape(saveBackupReason(backup.reason))}</small><small>${Number(backup.files) || 0} files · ${formatBytes(Number(backup.bytes) || 0)}</small>${saveProfileTags(backup)}</span></button>`).join('')}</div>${options}</div><div id="save-selected-context">${renderSaveSelection(selected)}</div></div>` : `<div class="empty-state compact sg-empty">${icon('save')}<h3>${state.saveBackupsLoading ? 'Checking save backups' : 'Your backup collection'}</h3><p>${escape(empty)}</p></div>${options}`}</section><p class="field-note sg-private-note">Pictures and notes stay on this computer. Restoring opens a review first and keeps a safety copy of your current saves.</p></section>`;
  }
  function cleanSavePicture(data) {
    const url = data?.dataUrl;
    return data?.available === true && typeof url === 'string' && url.length <= 2800000 &&
      /^data:image\/png;base64,[A-Za-z0-9+/]+={0,2}$/.test(url) ? url : '';
  }
  function rememberSavePicture(id, data) {
    const url = cleanSavePicture(data);
    state.savePictures.delete(id);
    if (url) { state.savePictures.set(id, url); saveThumbnailWatches.backups.evicted.delete(id); }
    trimSaveThumbnailCaches('backups');
    return url;
  }
  let savePicturesLoading = false, savePicturesSelectionPending = false;
  async function loadSavePictures() {
    if (!saveThumbnailsCanRead('backups')) return;
    if (savePicturesLoading || currentPreviewsLoading) { savePicturesSelectionPending = true; return; }
    const context = state.snapshot.accountContext;
    if (!saveThumbnailWatches.backups.root || saveThumbnailWatches.backups.scope !== saveThumbnailScope('backups'))
      bindSaveThumbnailVisibility('backups', $('#save-backups-panel'));
    const watch = saveThumbnailWatches.backups;
    savePicturesLoading = true;
    try {
      const available = filteredSaveBackupList(), selected = selectedSaveBackup(available);
      if (selected && state.savePictures.has(selected.id)) {
        const picture = state.savePictures.get(selected.id);
        state.savePictures.delete(selected.id); state.savePictures.set(selected.id, picture);
      }
      const candidates = saveThumbnailCandidates('backups', available, selected).slice(0, 12);
      for (const backup of candidates) {
        if (context !== state.snapshot.accountContext || !saveThumbnailsCanRead('backups')) break;
        if (state.savePictures.has(backup.id)) continue;
        if (watch.observer && backup.id !== selectedSaveBackup()?.id && !watch.visible.has(backup.id)) continue;
        watch.attempted.add(backup.id);
        try {
          const data = await request('saveBackupPicture', { id: backup.id });
          if (context !== state.snapshot.accountContext || !saveThumbnailsVisible('backups')) break;
          if (data?.id !== backup.id || !filteredSaveBackupList().some((item) => item.id === backup.id && item.pictureAvailable === true)) continue;
          rememberSavePicture(backup.id, data);
          updateSaveBackupPictureElements(backup.id);
        } catch { /* A missing picture never hides or alters its save backup. */ }
      }
    } finally {
      savePicturesLoading = false;
      const followSelection = savePicturesSelectionPending;
      savePicturesSelectionPending = false;
      if (saveThumbnailsCanRead('backups') && (followSelection || context !== state.snapshot.accountContext ||
          saveThumbnailCandidates('backups', filteredSaveBackupList(), selectedSaveBackup()).length)) queueSaveThumbnailWork();
      else if (currentPreviewsPending && currentSavesCanRead()) queueSaveThumbnailWork();
    }
  }
  function updateSaveBackupPictureElements(id) {
    if (!saveThumbnailsVisible('backups')) return;
    const backup = filteredSaveBackupList().find((item) => item.id === id);
    if (!backup) return;
    $$('[data-backup-preview]', $('#save-backups-panel')).forEach((element) => {
      if (element.dataset.backupPreview === id)
        element.innerHTML = saveGalleryPicture(backup, element.dataset.backupPicture !== undefined);
    });
  }
  async function showSavePicture(id) {
    const context = state.snapshot.accountContext;
    const backup = (state.saveBackups?.backups || []).find((backup) => backup.id === id);
    if (!backup || state.busy) return false;
    let picture = state.savePictures.get(id) || '';
    if (!picture && backup.pictureAvailable) {
      const data = await run('saveBackupPicture', { id }, { refresh: false, activity: false, progress: 'Opening backup picture…' });
      if (context !== state.snapshot.accountContext || !data) return false;
      picture = rememberSavePicture(id, data);
    }
    modal('Picture for this save backup',
      `${picture ? `<img class="save-picture-preview" src="${escape(picture)}" alt="Your picture for the selected save backup">` : '<div class="empty-state compact">' + icon('camera') + '<p>Add a screenshot or picture so this backup is easier to recognise.</p></div>'}<p>${escape(logTime(backup.createdAt))} · ${Number(backup.files) || 0} files · ${formatBytes(Number(backup.bytes) || 0)}</p><p class="field-note">Choose a PNG or JPEG up to 5 MB, 4096 pixels per side and 8 megapixels. The toolkit keeps a small copy with image metadata removed. It stays on this computer, separate from the game’s save files.</p>`,
      `<button type="button" class="button" data-modal-close>Close</button>${picture ? '<button type="button" class="button" id="save-picture-remove">Remove picture</button>' : ''}<button type="button" class="button primary" id="save-picture-choose">${icon('camera')}${picture ? 'Change picture' : 'Choose picture…'}</button>`);
    $('#save-picture-choose').onclick = async () => {
      if (context !== state.snapshot.accountContext) return;
      $('#modal').close();
      const data = await run('saveBackupPictureBrowse', { id }, { refresh: false, activity: false, progress: 'Saving backup picture…' });
      if (!data || data.cancelled || context !== state.snapshot.accountContext) return;
      const picture = rememberSavePicture(id, data);
      backup.pictureAvailable = !!picture;
      updateSaveBackupsPanel();
      if (picture) showSavePicture(id);
    };
    if (picture) $('#save-picture-remove').onclick = async () => {
      if (context !== state.snapshot.accountContext) return;
      $('#modal').close();
      const data = await run('saveBackupPictureRemove', { id }, { refresh: false, activity: false, progress: 'Removing backup picture…' });
      if (!data || context !== state.snapshot.accountContext) return;
      state.savePictures.delete(id);
      backup.pictureAvailable = false;
      updateSaveBackupsPanel();
      toast('Picture removed. The save backup is unchanged.');
    };
    return true;
  }
  async function showSaveFiles(id = '') {
    const context = state.snapshot.accountContext;
    const data = await run(id ? 'saveBackupFiles' : 'saveFilesList', id ? { id } : {},
      { refresh: false, activity: false, progress: 'Reading save file details…' });
    if (!data || context !== state.snapshot.accountContext) return false;
    const files = Array.isArray(data.files) ? data.files.slice(0, 250) : [];
    modal(id ? 'Files in this save backup' : 'Your current save files',
      `<p>${Number(data.fileCount) || 0} files · ${formatBytes(Number(data.bytes) || 0)}. ${id ? 'These are the files recorded in this backup.' : 'Newest modified files appear first. Save files and game options are listed by their actual filename.'}</p><ul class="backup-list">${files.map((file) => `<li><div><strong>${escape(file.name || 'Save file')}</strong><small>${formatBytes(Number(file.bytes) || 0)}${file.modifiedAt ? ' · ' + escape(logTime(file.modifiedAt)) : ''}</small></div></li>`).join('')}</ul>${!files.length ? '<p>No save files were found. Make a save in Days Gone, then check again.</p>' : ''}${data.truncated ? '<p class="field-note">Showing the first 250 files. Open the save folder to see the full collection.</p>' : ''}<p class="field-note">Nothing is changed here. Backups contain the whole save folder; restoring still needs a separate review with Days Gone closed.</p>`,
      '<button type="button" class="button primary" data-modal-close>Close</button>');
    return true;
  }
  function renderLibraryBackupPanel() {
    const recovery = state.snapshot.libraryBackupRecovery;
    return `<section class="panel settings-full" id="library-backup-panel"><div class="panel-heading"><div><h2>Library backups</h2><p>Keep the actual mod files and profiles, ready to bring back later.</p></div>${icon('layers')}</div><div class="panel-body"><p>Save your library, profiles, personal notes and portable preferences in one private ZIP. This is separate from your game saves. Account credentials and machine-specific paths are excluded.</p>${recovery?.pending ? `<div class="banner warning"><div><strong>A library restore was interrupted.</strong><p>${escape(recovery.reason || 'Review recovery before importing another library backup.')}</p>${button('Review library recovery', 'library-backup-recovery', 'shield', 'small')}</div></div>` : `<div class="inline-actions">${button('Back up library…', 'library-backup-export', 'save')}${button('Restore library…', 'library-backup-restore', 'folder')}</div>`}<p class="field-note">Restore adds checked files to the library with mods disabled. Your current installed mods, profiles and settings stay in place.</p></div></section>`;
  }
  function reviewLibraryBackupRecovery() {
    modal('Recover the interrupted library restore?', '<p>The toolkit will check its restore record and roll back incomplete additions it can still verify. Existing game files, saves and the original backup ZIP stay in place.</p><p>If a file changed after the interruption, recovery stops so you can check it. Keep the recovery files until this finishes.</p>', '<button type="button" class="button" data-modal-close>Cancel</button><button type="button" class="button primary" id="library-backup-recover-confirm">' + icon('shield') + 'Recover library</button>');
    $('#library-backup-recover-confirm').onclick = async () => {
      $('#modal').close();
      const result = await run('libraryBackupRecover', { confirmed: true }, { refresh: false, progress: 'Checking interrupted library additions…', activity: true });
      if (result) {
        await refresh({ quiet: true, preserveForm: true });
        toast('Library recovery finished. Review the library before restoring another backup.');
      }
    };
  }
  function confirmLibraryBackupExport() {
    modal('Back up your mod library', '<p>This private ZIP contains the actual cached mod versions, profile copies, favourites, personal notes and portable preferences. Keep it for your own use.</p><p>Game saves, account credentials, downloads and computer-specific paths are excluded. Your current files stay where they are.</p>', '<button type="button" class="button" data-modal-close>Cancel</button><button type="button" class="button primary" id="library-backup-export-confirm">' + icon('save') + 'Choose where to save…</button>');
    $('#library-backup-export-confirm').onclick = async () => {
      $('#modal').close();
      const result = await run('browseLibraryBackupExport', {}, { refresh: false, progress: 'Checking and backing up your library…', activity: true });
      if (!result || result.cancelled) return;
      modal('Library backup saved', `<p>${Number(result.packageCount) || 0} packages backed up${Number.isFinite(result.totalBytes) ? ` · ${formatBytes(result.totalBytes)}` : ''}.</p><p>The backup is private and includes your personal notes. It can be restored in Library tools or Settings → Backups.</p>`, '<button type="button" class="button primary" data-modal-close>Done</button>');
    };
  }
  async function reviewLibraryBackup() {
    const review = await run('browseLibraryBackupRestore', {}, { refresh: false, progress: 'Checking the library backup…', activity: true });
    if (!review || review.cancelled) return false;
    if (!review.token) { toast('The backup could not be reviewed. No files were restored.', 'warning'); return false; }
    modal('Restore this library backup?', `<p>Checked ${Number(review.packageCount) || 0} packages · ${formatBytes(review.totalBytes)}.</p><dl class="detail-grid"><dt>Portable profiles</dt><dd>${Number(review.profileCount) || 0}</dd><dt>Saved mod selections</dt><dd>${Number(review.selectionProfileCount) || 0}</dd><dt>Personal notes</dt><dd>${review.notesIncluded ? 'Included' : 'Not included'}</dd><dt>Portable preferences</dt><dd>${review.settingsIncluded ? 'Included as reference; your current settings stay in place' : 'Not included'}</dd></dl><p><strong>Restore adds files to your library with mods disabled.</strong> Existing installed mods, saves, profiles and settings stay in place. Matching library files are reused. Existing notes take priority, and restored profiles get separate names.</p><p class="field-note">The toolkit checks the backup again before restoring it.</p>`, '<button type="button" class="button" data-modal-close>Cancel</button><button type="button" class="button primary" id="library-backup-restore-confirm">' + icon('folder') + 'Restore library</button>');
    $('#library-backup-restore-confirm').onclick = async () => {
      $('#modal').close();
      const result = await run('libraryBackupRestore', { token: review.token, confirmed: true }, { refresh: false, progress: 'Restoring the checked library files…', activity: true });
      if (!result) return;
      state.libraryMetaLoaded = false;
      state.portableProfiles = null;
      await refresh({ quiet: true, preserveForm: true });
      if (state.view === 'profiles') await loadPortableProfiles();
      modal('Library restored', `<p>${escape(result.message || 'The checked library files were restored. Existing game files and settings were left in place.')}</p><p>Restored mods start disabled. Review your collection before switching anything on.</p>`, '<button type="button" class="button" data-modal-close>Close</button><button type="button" class="button primary" id="library-backup-open-mods">Open My mods</button>');
      $('#library-backup-open-mods').onclick = () => { $('#modal').close(); state.filter = 'stored'; navigate('mods'); };
    };
    return true;
  }
  async function saveBackupOptions(automatic, keep) {
    const result = await run('saveBackupsSettings', { automatic, keep }, { refresh: false, progress: 'Saving backup settings…' });
    if (result) { await loadSaveBackups(); flashSaved(); }
  }
  function saveRestoreBlockReason(record) {
    if (record.context !== state.snapshot.accountContext) return 'The selected installation changed. Close this review and choose the backup again.';
    if (state.busy || state.nativeDropActivity?.running) return 'Wait for the current operation to finish.';
    if (state.snapshot.libraryBackupRecovery?.pending) return 'Finish the interrupted library restore first.';
    const location = state.saveBackups?.location || state.snapshot.saveLocation;
    if (location?.resolved === false) return location.reason || 'The save location has not been identified safely.';
    if (state.snapshot.gameRunning === true) return 'Close Days Gone before restoring saves.';
    if (state.snapshot.gameRunning !== false) return 'Game status is not confirmed. Refresh the toolkit before restoring saves.';
    if (!record.recovery && (state.saveBackups?.recovery?.pending || state.snapshot.saveRecoveryRequired)) return 'Review the interrupted save restore before restoring another backup.';
    return '';
  }
  function updateSaveRestoreStatus() {
    const record = state.saveRestoreReview;
    if (!record) return;
    if (!$('#modal')?.open || $('#save-restore-review') !== record.element) { state.saveRestoreReview = null; return; }
    const reason = saveRestoreBlockReason(record), status = $('#save-restore-game-state'), control = $('#confirm-action');
    if (status) {
      status.textContent = reason || 'Days Gone is closed. The toolkit checks again before changing saves.';
      status.classList.toggle('sg-review-blocked', !!reason);
    }
    if (control) { control.disabled = !!reason; control.title = reason; }
  }
  function saveRestoreFiles(files) {
    if (!files) return '<p class="field-note">File details could not be displayed. Native verification still checks the reviewed files before restoring.</p>';
    return `<details class="sg-review-files"><summary>Show the files involved${Number.isSafeInteger(files.fileCount) ? ' · ' + files.fileCount : ''}</summary>${files.files?.length ? `<ul>${files.files.slice(0, 250).map((file) => `<li><span>${escape(file.name || 'Save file')}</span><span>${formatBytes(Number(file.bytes) || 0)}</span></li>`).join('')}</ul>` : '<p>No files were returned in this list.</p>'}${files.truncated ? '<p class="field-note">Showing the first 250 files. The native review covers the complete backup.</p>' : ''}</details>`;
  }
  function saveManagementContext() {
    return JSON.stringify([state.snapshot.accountContext || '', state.snapshot.settings?.gamePath || '', state.snapshot.activePath || '']);
  }
  function currentSaveGuard(id) {
    return { context: saveManagementContext(), saveContext: state.currentSaves?.context, id };
  }
  function currentSaveChangeReason(record, readOnly = false) {
    if (!record || record.context !== saveManagementContext()) return 'The selected installation changed. Close this review and select the save again.';
    if (state.busy || state.nativeDropActivity?.running) return 'Wait for the current operation to finish.';
    if (state.snapshot.libraryBackupRecovery?.pending || state.snapshot.saveRecoveryRequired || state.saveBackups?.recovery?.pending)
      return 'Finish the interrupted recovery before using these save files.';
    if (state.currentSaves?.context !== record.saveContext || selectedCurrentSave()?.id !== record.id)
      return 'The current save selection changed. Close this review and select it again.';
    if (!readOnly && !String(state.snapshot.settings?.gamePath || '').trim())
      return 'Choose your Days Gone game folder in Settings before changing a save. Pictures and export remain available.';
    if (!readOnly && state.profileSession?.active) return 'Finish the profile session before changing a current save.';
    if (!readOnly && state.snapshot.gameRunning !== false) return 'Close Days Gone and wait until its status is confirmed before changing a save.';
    return '';
  }
  async function finishCurrentSaveChange(result, record, message) {
    const retainSelection = state.currentSaveSelection?.context === record.saveContext && state.currentSaveSelection?.id === record.id;
    await loadSaveBackups();
    if (record.context !== saveManagementContext()) return false;
    await loadCurrentSaves();
    if (record.context !== saveManagementContext()) return false;
    const selected = currentSaveFiles().filter((save) => save.name === record.targetName);
    if (retainSelection && selected.length === 1) state.currentSaveSelection = { context: state.currentSaves.context, id: selected[0].id };
    if (state.view === 'saves') updateCurrentSavesPanel();
    toast(message, 'success', typeof result.safetyBackupId === 'string' && result.safetyBackupId ? {
      label: 'Show safety backup', run: () => {
        if (record.context !== saveManagementContext()) return;
        state.saveProfileFilter = null; state.saveSelection = { context: state.snapshot.accountContext, id: result.safetyBackupId }; setSavesTab('backups');
      }
    } : undefined);
    return true;
  }
  async function exportCurrentSave(id) {
    const record = currentSaveGuard(id), selected = selectedCurrentSave();
    if (!selected || selected.id !== id || selected.exportEligible !== true || currentSaveChangeReason(record, true)) return false;
    const result = await run('currentSaveExportBrowse', { id }, { refresh: false, activity: true, progress: 'Checking and exporting the selected save…',
      onError: () => record.context !== saveManagementContext() });
    if (record.context !== saveManagementContext() || !result || result.cancelled || result.exported !== true) return false;
    toast('Save exported. Your current slot is unchanged.', 'success'); return true;
  }
  function currentSavePlanBody(plan, editChanges = null) {
    const changes = Array.isArray(editChanges) ? editChanges.slice(0, 512) : null;
    return `<div class="save-slot-review"><h3>${escape(plan.targetName || 'Selected save slot')}</h3><p>${changes ? 'Apply these checked gameplay changes to this save only.' : `Replace this slot with <strong>${escape(plan.sourceName || 'the selected save file')}</strong>.`}</p>${changes ? `<dl class="save-editor-changes">${changes.map((change) => `<div><dt>${escape(change.label || 'Verified field')}</dt><dd><span>${escape(String(change.before))}</span>${icon('arrow')}<strong>${escape(String(change.after))}</strong></dd></div>`).join('')}</dl>` : ''}<p>The toolkit backs up all current saves first, even when automatic backups are off. Other save slots, installed mods and profiles stay in place.</p>${Number.isSafeInteger(plan.beforeBytes) && Number.isSafeInteger(plan.afterBytes) ? `<p class="field-note">Current slot: ${formatBytes(plan.beforeBytes)} · New file: ${formatBytes(plan.afterBytes)}</p>` : ''}<p class="field-note">${changes ? 'Protected progress fields are left unchanged.' : 'File verification cannot prove that an imported save matches your game version or account.'}</p>${workshopWarnings(plan.warnings)}<p class="field-note" role="status" id="save-slot-status"></p></div>`;
  }
  function updateCurrentSaveReplaceStatus() {
    const record = state.currentSaveReplaceReview;
    if (!record || !$('#modal').open || record.generation !== modalContentGeneration) return;
    const reason = currentSaveChangeReason(record), status = $('#save-slot-status'), confirm = $('#save-slot-confirm');
    if (status) status.textContent = reason || record.error || 'A complete safety backup is required before this slot is replaced.';
    if (confirm) confirm.disabled = !!reason || record.used;
  }
  async function importCurrentSave(id) {
    const record = currentSaveGuard(id), selected = selectedCurrentSave(), generation = modalContentGeneration;
    if (!selected || selected.id !== id || selected.replaceEligible !== true || currentSaveChangeReason(record)) return false;
    const plan = await run('currentSaveImportBrowse', { id }, { refresh: false, activity: true, progress: 'Checking the selected save file…',
      onError: () => record.context !== saveManagementContext() || generation !== modalContentGeneration });
    if (record.context !== saveManagementContext() || generation !== modalContentGeneration || currentSaveChangeReason(record) || !plan || plan.cancelled) return false;
    if (plan.id !== id || plan.operation !== 'import-current-save' || plan.safetyBackupRequired !== true || typeof plan.token !== 'string' || !plan.token || plan.token.length > 512) return false;
    Object.assign(record, { token: plan.token, targetName: plan.targetName || selected.name, used: false, error: '' });
    modal('Import into this save slot?', currentSavePlanBody(plan), '<button type="button" class="button" data-modal-close>Cancel</button><button type="button" class="button primary" id="save-slot-confirm">Back up and replace this slot</button>');
    record.generation = modalContentGeneration; state.currentSaveReplaceReview = record;
    $('#save-slot-confirm').onclick = async () => {
      if (state.currentSaveReplaceReview !== record || record.generation !== modalContentGeneration || !$('#modal').open || record.used || currentSaveChangeReason(record)) { updateCurrentSaveReplaceStatus(); return false; }
      record.used = true; $('#save-slot-confirm').disabled = true;
      const result = await run('currentSaveReplace', { token: record.token, confirmed: true }, { refresh: false, activity: true, progress: 'Backing up saves and replacing the reviewed slot…',
        onError: (error) => { record.error = errorText(error) + ' Close this review and select the source again before retrying.'; return true; } });
      if (record.context !== saveManagementContext() || record.generation !== modalContentGeneration || state.currentSaveReplaceReview !== record || !$('#modal').open) return false;
      if (result?.replaced !== true) { record.error ||= 'The slot replacement did not finish. Close this review and select the source again.'; updateCurrentSaveReplaceStatus(); return false; }
      state.currentSaveReplaceReview = null; $('#modal').close();
      return finishCurrentSaveChange(result, record, 'Selected save slot replaced. Its safety backup is in Backups.');
    };
    updateCurrentSaveReplaceStatus(); return true;
  }
  let saveEditorTimer = null, saveEditorGeneration = 0;
  function resetCleanSaveEditorContext() {
    const record = state.saveEditor;
    if (!record || record.context === saveManagementContext() || record.session.dirty === true || record.pending) return false;
    const visible = saveEditorVisible(record);
    ++saveEditorGeneration;
    if (saveEditorTimer !== null) clearTimeout(saveEditorTimer);
    saveEditorTimer = null;
    state.saveEditor = null;
    // Linux replaces the native editor with the installation. Its old session
    // must not be sent to the new backend just to discard an unchanged draft.
    if (visible) $('#modal').close();
    return true;
  }
  function saveEditorAvailable() { return state.currentSaves?.saveEditorAvailable === true; }
  function saveEditorUnavailableReason() {
    const reason = state.currentSaves?.saveEditorUnavailableReason;
    return typeof reason === 'string' && reason.trim() ? reason.slice(0, 512) : 'Save editing is not available in this build. Use the complete loader download with its verified save decoder.';
  }
  function saveEditorSession(value) {
    if (!value || typeof value.id !== 'string' || !value.id || value.id.length > 512 || !Number.isSafeInteger(value.revision) || value.revision < 0 ||
        typeof value.sourceId !== 'string' || value.sourceId.length > 256 || typeof value.saveName !== 'string' || value.saveName.length > 512 ||
        !Array.isArray(value.fields) || value.fields.length > 512 || value.fields.some((field) => !field || typeof field.id !== 'string' || !field.id || field.id.length > 128) ||
        new Set(value.fields.map((field) => field.id)).size !== value.fields.length ||
        value.protectedFields !== undefined && (!Array.isArray(value.protectedFields) || value.protectedFields.length > 512) ||
        value.inspectionOnly === true && value.readOnly !== true) return null;
    return value;
  }
  function saveEditorDeclaredFields(record = state.saveEditor) {
    return record?.session?.readOnly === true ? [] : (record?.session?.fields || []).filter((field) => field.editable !== false && field.protected !== true && workshopFieldEditor(field));
  }
  function saveEditorFields(record = state.saveEditor) {
    return saveEditorDeclaredFields(record).filter((field) => field.advanced !== true || record?.advancedEnabled === true);
  }
  function saveEditorAdvancedChanged(record = state.saveEditor) {
    return saveEditorDeclaredFields(record).some((field) => field.advanced === true && field.originalValue !== undefined && field.value !== field.originalValue);
  }
  function saveEditorAdvancedArgs(record) {
    return saveEditorDeclaredFields(record).some((field) => field.advanced === true) ? { advanced: record.advancedEnabled === true } : {};
  }
  function saveEditorVisible(record = state.saveEditor) {
    return record && state.saveEditor === record && $('#modal').open && record.modalGeneration === modalContentGeneration && !!$('#save-editor-content');
  }
  function saveEditorPendingValues(record = state.saveEditor) {
    return record?.pending ? [record.pending, ...(Array.isArray(record.pendingQueue) ? record.pendingQueue : [])].slice(0, 512) : [];
  }
  function setSaveEditorPendingValues(record, values) {
    record.pending = values[0] || null;
    record.pendingQueue = values.slice(1, 512);
  }
  function scheduleSaveEditorFlush(record, delay = 300) {
    if (saveEditorTimer !== null) clearTimeout(saveEditorTimer);
    saveEditorTimer = null;
    if (!saveEditorVisible(record) || record.mode !== 'edit' || record.working ||
        !saveEditorPendingValues(record).some((value) => value.valid) || saveEditorReason(record)) return;
    saveEditorTimer = setTimeout(() => {
      saveEditorTimer = null;
      if (state.saveEditor === record && saveEditorVisible(record) && record.mode === 'edit') flushSaveEditorEdit();
    }, delay);
  }
  function saveEditorReason(record) {
    const blocked = saveEditorAvailable() ? currentSaveChangeReason(record, record?.session?.inspectionOnly === true) : saveEditorUnavailableReason();
    if (blocked) return blocked;
    const advancedIds = new Set(saveEditorDeclaredFields(record).filter((field) => field.advanced === true).map((field) => field.id)),
      pendingAdvanced = saveEditorPendingValues(record).some((value) => advancedIds.has(value.field));
    return record?.advancedEnabled !== true && (saveEditorAdvancedChanged(record) || pendingAdvanced)
      ? 'Advanced changes are still in this draft. Turn Advanced back on to review them, or revert those values first.' : '';
  }
  function updateSaveEditorStatus() {
    const record = state.saveEditor;
    if (!saveEditorVisible(record)) return;
    const reason = saveEditorReason(record), status = $('#save-editor-status'), review = $('#save-editor-review'),
      pending = saveEditorPendingValues(record), invalid = pending.some((value) => !value.valid);
    if (status) { status.classList.toggle('field-error', !!record.error); status.textContent = reason || record.error || (record.working ? 'Checking the draft value…' : record.pending ? 'Apply or revert the value that needs attention.' : !saveEditorDeclaredFields(record).length ? 'Read-only inspection. No save data is changed.' : 'Changes stay in this temporary draft until you review and apply them.'); }
    if (review) review.disabled = !!reason || record.working || invalid || (record.session.dirty !== true && !pending.length);
    const apply = $('#save-editor-apply'); if (apply) apply.disabled = !!reason || record.working || record.reviewUsed === true;
    const advanced = $('#save-editor-advanced'); if (advanced) advanced.disabled = record.working;
    for (const id of ['save-editor-close', 'save-editor-revert', 'save-editor-back', 'save-editor-keep-editing', 'save-editor-discard']) {
      const control = $('#' + id); if (control) control.disabled = record.working;
    }
    $$('[data-modal-close]', $('#modal')).forEach((control) => { control.disabled = record.working; });
  }
  function saveEditorFieldControl(field, index, record) {
    const editor = workshopFieldEditor(field), pending = saveEditorPendingValues(record).find((value) => value.field === field.id), value = pending ? pending.raw : field.value,
      attributes = `id="save-editor-field-${index}" data-save-editor-field="${escape(field.id)}"`;
    if (editor === 'boolean') return `<input type="checkbox" ${attributes} ${value ? 'checked' : ''}>`;
    if (editor === 'enum') return `<select class="input" ${attributes}>${field.choices.map((choice, choiceIndex) => `<option value="${choiceIndex}" ${choice.value === (pending?.value ?? field.value) ? 'selected' : ''}>${escape(choice.label)}</option>`).join('')}</select>`;
    return `<input class="input full-width" type="text" inputmode="${editor === 'integer' ? 'numeric' : editor === 'number' ? 'decimal' : 'text'}" ${attributes} value="${escape(String(value))}" maxlength="${editor === 'text' ? field.maxLength : 80}" autocomplete="off" spellcheck="false">`;
  }
  const saveEditorPageSize = 24;
  function saveEditorDisplayValue(value) {
    if (typeof value === 'boolean') return value ? 'Yes' : 'No';
    if (typeof value === 'number' && Number.isFinite(value)) return String(value);
    if (typeof value === 'string') return value.length <= 4096 ? value : value.slice(0, 4096) + '… (display shortened)';
    return 'Not exposed';
  }
  function saveEditorEntries(record = state.saveEditor) {
    const writable = new Set(saveEditorFields(record).map((field) => field.id)), entries = [];
    for (const [index, field] of (record?.session?.fields || []).slice(0, 512).entries()) {
      entries.push({ field, index, editable: writable.has(field.id) });
    }
    for (const [index, field] of (Array.isArray(record?.session?.protectedFields) ? record.session.protectedFields : []).slice(0, 512).entries()) {
      if (field && typeof field === 'object') entries.push({ field, index: index + 512, editable: false });
    }
    return entries.map((entry) => ({ ...entry,
      label: String(entry.field.label || entry.field.name || entry.field.id || 'Other save data').slice(0, 256),
      group: typeof entry.field.group === 'string' && entry.field.group.trim() ? entry.field.group.slice(0, 128) : entry.editable ? 'Editable values' : 'Protected and read-only progress',
      type: typeof entry.field.type === 'string' ? entry.field.type.slice(0, 128) : '',
      reason: typeof (entry.field.reason || entry.field.hint) === 'string' ? (entry.field.reason || entry.field.hint).slice(0, 1024) : entry.editable ? '' : 'This field is not approved for editing.'
    }));
  }
  function saveEditorInspector(record = state.saveEditor) {
    const entries = saveEditorEntries(record), filter = ['editable', 'read-only', 'all'].includes(record?.inspectorFilter) ? record.inspectorFilter : 'editable',
      query = typeof record?.inspectorQuery === 'string' ? record.inspectorQuery.slice(0, 256).trim().toLocaleLowerCase() : '',
      filtered = entries.filter((entry) => (filter === 'all' || entry.editable === (filter === 'editable')) &&
        (!query || [entry.label, entry.group, entry.type, entry.reason, entry.field.id || '', saveEditorDisplayValue(entry.field.value)].join(' ').toLocaleLowerCase().includes(query))),
      pages = Math.max(1, Math.ceil(filtered.length / saveEditorPageSize)),
      page = Math.min(Math.max(Number.isSafeInteger(record?.inspectorPage) ? record.inspectorPage : 0, 0), pages - 1);
    return { entries, filter, query, filtered, page, pages, shown: filtered.slice(page * saveEditorPageSize, (page + 1) * saveEditorPageSize) };
  }
  function saveEditorInspectorBody(record = state.saveEditor) {
    const view = saveEditorInspector(record), groups = new Map();
    record.inspectorPage = view.page;
    for (const entry of view.shown) { if (!groups.has(entry.group)) groups.set(entry.group, []); groups.get(entry.group).push(entry); }
    const rows = [...groups].map(([group, entries], index) => `<section class="save-editor-group" aria-labelledby="save-editor-group-${index}"><h4 id="save-editor-group-${index}">${escape(group)}</h4>${entries.map((entry) => {
      const { field, editable } = entry, id = `save-editor-field-${entry.index}`, changed = editable && field.originalValue !== undefined && field.originalValue !== field.value,
        details = [entry.type, editable && typeof field.min === 'number' && typeof field.max === 'number' ? `${field.min} to ${field.max}` : ''].filter(Boolean).join(' · '),
        description = `${details ? `<span class="save-editor-value-type">${escape(details)}</span>` : ''}${field.advanced === true ? '<span class="save-editor-advanced-label">Advanced · Not game-tested</span>' : ''}${entry.reason ? `<small id="${id}-hint">${escape(entry.reason)}</small>` : ''}${changed ? `<small class="save-editor-original">Saved value: ${escape(saveEditorDisplayValue(field.originalValue))}</small>` : ''}`;
      return `<div class="save-editor-field${editable ? '' : ' save-editor-readonly'}"><div class="save-editor-field-label">${editable ? `<label for="${id}"><strong>${escape(entry.label)}</strong></label>` : `<strong>${escape(entry.label)}</strong>`}${description}</div>${editable ? saveEditorFieldControl(field, entry.index, record) : `<div class="save-editor-recorded"><span class="save-editor-access">${field.advanced === true && record.session.readOnly !== true ? 'Advanced is off' : 'Read-only'}</span><span class="save-editor-recorded-value">${escape(saveEditorDisplayValue(field.value))}</span></div>`}</div>`;
    }).join('')}</section>`).join('');
    const first = view.filtered.length ? view.page * saveEditorPageSize + 1 : 0, last = Math.min((view.page + 1) * saveEditorPageSize, view.filtered.length);
    return `${rows || '<p class="save-editor-empty">No values match this view. Try another search or choose All values.</p>'}<div class="save-editor-pagination"><p class="field-note" role="status">${first}–${last} of ${view.filtered.length} matching values</p><div><button type="button" class="button" id="save-editor-page-back" ${view.page === 0 ? 'disabled' : ''}>${icon('back')}Previous</button><button type="button" class="button" id="save-editor-page-next" ${view.page + 1 >= view.pages ? 'disabled' : ''}>Next${icon('arrow')}</button></div></div>`;
  }
  function updateSaveEditorInspector(record = state.saveEditor) {
    if (!saveEditorVisible(record) || record.mode !== 'edit') return false;
    const generation = record.modalGeneration, current = () => saveEditorVisible(record) && record.modalGeneration === generation && record.mode === 'edit',
      view = saveEditorInspector(record), content = $('#save-editor-inspector');
    if (!content) return false;
    content.innerHTML = saveEditorInspectorBody(record);
    for (const filter of ['editable', 'read-only', 'all']) {
      const control = $('#save-editor-filter-' + filter); if (control) control.setAttribute('aria-pressed', String(filter === view.filter));
    }
    $$('[data-save-editor-field]', content).forEach((control) => {
      const field = saveEditorFields(record).find((item) => item.id === control.dataset.saveEditorField);
      if (field?.reason || field?.hint) control.setAttribute('aria-describedby', control.id + '-hint');
      control.oninput = () => current() ? queueSaveEditorEdit(control.dataset.saveEditorField, control) : false;
    });
    for (const [id, delta] of [['save-editor-page-back', -1], ['save-editor-page-next', 1]]) {
      const control = $('#' + id); if (control) control.onclick = () => {
        if (!current()) return false;
        record.inspectorPage = Math.max(0, Math.min(view.pages - 1, view.page + delta));
        updateSaveEditorInspector(record); $('#save-editor-results-heading')?.focus?.({ preventScroll: true }); return true;
      };
    }
    return true;
  }
  function bindSaveEditorClose(record) {
    record.modalGeneration = modalContentGeneration;
    const generation = record.modalGeneration;
    $$('[data-modal-close]', $('#modal')).forEach((control) => {
      control.onclick = () => saveEditorVisible(record) && record.modalGeneration === generation
        ? requestCloseSaveEditor() : false;
    });
  }
  function showSaveEditor(record = state.saveEditor) {
    if (!record || state.saveEditor !== record) return false;
    const entries = saveEditorEntries(record), canEdit = saveEditorDeclaredFields(record).length > 0, editableCount = entries.filter((entry) => entry.editable).length,
      readonlyCount = entries.length - editableCount, advancedCount = saveEditorDeclaredFields(record).filter((field) => field.advanced === true).length, active = document.activeElement,
      focused = active?.id?.startsWith('save-editor-field-') || active?.id === 'save-editor-search'
        ? { id: active.id, start: active.selectionStart, end: active.selectionEnd } : null;
    record.mode = 'edit';
    const filters = [['editable', 'Editable', editableCount], ['read-only', 'Read-only', readonlyCount], ['all', 'All values', entries.length]],
      toolbar = `<div class="save-editor-browser"><label class="save-editor-search" for="save-editor-search">${icon('search')}<span class="sr-only">Search save values</span><input class="input" id="save-editor-search" type="search" maxlength="256" value="${escape((record.inspectorQuery || '').slice(0, 256))}" placeholder="Find a value, group or type…" autocomplete="off" spellcheck="false"></label><div class="save-editor-filters" role="group" aria-label="Save value filter">${filters.map(([key, label, count]) => `<button type="button" class="button" id="save-editor-filter-${key}" aria-pressed="${saveEditorInspector(record).filter === key}">${label}<span>${count}</span></button>`).join('')}</div></div>`;
    const advancedMarkup = advancedCount ? `<label class="save-editor-advanced-choice" for="save-editor-advanced"><input type="checkbox" id="save-editor-advanced" ${record.advancedEnabled === true ? 'checked' : ''}><span><strong>Advanced fields <span>${advancedCount}</span></strong><small>Show extra fields whose storage format is known but whose effect in the game is unverified. Infusion entries are separate list positions, numbered from zero in the raw name. Read each entry’s description before changing it.</small></span></label>` : '';
    modal(canEdit ? 'Edit save' : 'Inspect save', `<div class="save-editor-dialog" id="save-editor-content">${state.currentSaves?.saveEditorDevelopment === true ? '<div class="banner warning"><p><strong>Save-editor test build.</strong> Use a save with Skills unlocked, load that exact slot after editing and check the value in the game. A safety backup is made before applying.</p></div>' : ''}<h3>${escape(record.session.saveName)}</h3><p>${canEdit ? 'Change supported values or inspect the data identified in this save. Changes stay in a temporary draft until you review and apply them.' : 'These entries are read-only. Browse the identified values and their protection reasons; no change is made to this save.'}</p>${toolbar}${advancedMarkup}<h4 class="sr-only" id="save-editor-results-heading" tabindex="-1">Save values</h4><div class="save-editor-fields" id="save-editor-inspector">${saveEditorInspectorBody(record)}</div><p class="field-note save-editor-protected">Protected and read-only progress stays unchanged. Its entries explain what is known and why editing is restricted. Unknown blocks are preserved.</p>${record.session.inspectorTruncated === true ? '<p class="field-note">This save contains more data than the bounded inspector can show. Values outside this view stay unchanged.</p>' : ''}${canEdit ? '<p class="field-note">Use a save with Skills unlocked and load this exact slot through Load Game after applying.</p><p class="field-note">The full current-save set is backed up before applying. Other slots and protected progress stay unchanged.</p>' : '<p class="field-note">Inspection is a read-only snapshot. Reopen it after the game saves to read the new data. To make supported edits, close this inspection and choose Edit save after closing the game.</p>'}<p class="field-note" role="status" id="save-editor-status"></p></div>`, '<button type="button" class="button" id="save-editor-close">Close draft</button><button type="button" class="button" id="save-editor-revert">Revert pending value</button><button type="button" class="button primary" id="save-editor-review">Review changes…</button>');
    bindSaveEditorClose(record);
    const generation = record.modalGeneration;
    const current = () => saveEditorVisible(record) && record.modalGeneration === generation && record.mode === 'edit';
    $('#save-editor-close').textContent = canEdit ? 'Close draft' : 'Close inspection';
    $('#save-editor-close').onclick = () => current() ? requestCloseSaveEditor() : false;
    $('#save-editor-revert').hidden = !record.pending;
    $('#save-editor-revert').textContent = saveEditorPendingValues(record).length > 1 ? 'Revert pending values' : 'Revert pending value';
    $('#save-editor-revert').onclick = () => { if (!current() || record.working) return false; setSaveEditorPendingValues(record, []); record.error = ''; if (saveEditorTimer !== null) clearTimeout(saveEditorTimer); saveEditorTimer = null; showSaveEditor(record); };
    $('#save-editor-review').hidden = !canEdit;
    $('#save-editor-review').onclick = () => current() ? reviewSaveEditor() : false;
    const search = $('#save-editor-search');
    search.oninput = () => {
      if (!current()) return false;
      record.inspectorQuery = search.value.slice(0, 256); record.inspectorPage = 0;
      return updateSaveEditorInspector(record);
    };
    search.onkeydown = (event) => {
      if (event.key !== 'Escape' || !search.value || !current()) return;
      event.preventDefault(); event.stopPropagation(); search.value = ''; search.oninput();
    };
    for (const [filter] of filters) $('#save-editor-filter-' + filter).onclick = () => {
      if (!current()) return false;
      record.inspectorFilter = filter; record.inspectorPage = 0; return updateSaveEditorInspector(record);
    };
    const advanced = $('#save-editor-advanced');
    if (advancedCount && advanced) advanced.onchange = () => {
      if (!current() || record.working) { advanced.checked = record.advancedEnabled === true; return false; }
      record.advancedEnabled = advanced.checked === true; record.inspectorPage = 0;
      showSaveEditor(record); $('#save-editor-advanced')?.focus?.({ preventScroll: true }); return true;
    };
    updateSaveEditorInspector(record); updateSaveEditorStatus();
    if (focused) { const target = $('#' + focused.id); target?.focus?.({ preventScroll: true }); try { target?.setSelectionRange?.(focused.start, focused.end); } catch {} }
    scheduleSaveEditorFlush(record);
    return true;
  }
  async function openSaveEditor(id, inspectOnly = false) {
    if (!saveEditorAvailable()) return false;
    inspectOnly = inspectOnly === true;
    const guard = currentSaveGuard(id), selected = selectedCurrentSave();
    if (selected?.id !== id || !(inspectOnly ? selected.exportEligible === true : selected.replaceEligible === true) || currentSaveChangeReason(guard, inspectOnly)) return false;
    if (state.saveEditor) {
      if (state.saveEditor.sourceId === id && state.saveEditor.context === saveManagementContext() &&
          (state.saveEditor.session.inspectionOnly === true) === inspectOnly) return showSaveEditor();
      return requestCloseSaveEditor(() => openSaveEditor(id, inspectOnly));
    }
    const generation = ++saveEditorGeneration, modalGeneration = modalContentGeneration;
    const result = await run('saveEditorOpen', { id, ...(inspectOnly ? { inspectOnly: true } : {}) }, { refresh: false, activity: true, progress: inspectOnly ? 'Reading a protected save snapshot…' : 'Reading verified save fields…',
      onError: () => guard.context !== saveManagementContext() || modalGeneration !== modalContentGeneration });
    if (guard.context !== saveManagementContext() || generation !== saveEditorGeneration || modalGeneration !== modalContentGeneration || currentSaveChangeReason(guard, inspectOnly)) {
      if (saveEditorSession(result?.session)) request('saveEditorClose', { session: result.session.id }).catch(() => {});
      return false;
    }
    if (!result) return false;
    const session = saveEditorSession(result.session);
    if ((result.supported !== true && result.inspectionAvailable !== true) || !session || session.sourceId !== id || (session.inspectionOnly === true) !== inspectOnly || !saveEditorEntries({ session }).length) {
      if (session) request('saveEditorClose', { session: session.id }).catch(() => {});
      modal('This save cannot be edited', `<p>${escape(typeof result.reason === 'string' ? result.reason : 'Its gameplay format has not been verified for editing.')}</p><p>Your current save has not changed. Exporting it and keeping backups are still separate options.</p>`); return false;
    }
    state.saveEditor = { ...guard, sourceId: id, session, generation, mode: 'edit', pending: null, working: false, error: '', targetName: session.saveName,
      inspectorFilter: saveEditorFields({ session }).length ? 'editable' : 'read-only', advancedEnabled: false };
    return showSaveEditor();
  }
  function queueSaveEditorEdit(id, input) {
    const record = state.saveEditor, field = saveEditorFields(record).find((value) => value.id === id);
    if (!saveEditorAvailable() || !record || !field || !saveEditorVisible(record) || record.context !== saveManagementContext() || record.mode !== 'edit') return false;
    const editor = workshopFieldEditor(field), raw = editor === 'boolean' ? input.checked === true : input.value;
    let value = raw, valid = true;
    if (editor === 'enum') { const index = /^(0|[1-9]\d*)$/.test(String(raw)) ? Number(raw) : -1; valid = Number.isSafeInteger(index) && index >= 0 && index < field.choices.length; value = valid ? field.choices[index].value : field.value; }
    else if (editor === 'text') valid = typeof raw === 'string' && raw.length <= field.maxLength && !raw.includes('\0');
    else if (editor !== 'boolean') {
      value = Number(String(raw).trim()); valid = String(raw).trim() !== '' && Number.isFinite(value) && (editor !== 'integer' || Number.isSafeInteger(value));
      if (typeof field.min === 'number') valid &&= value >= field.min;
      if (typeof field.max === 'number') valid &&= value <= field.max;
    }
    const values = saveEditorPendingValues(record), index = values.findIndex((entry) => entry.field === id),
      pending = { field: id, raw, value, valid };
    if (index >= 0) values[index] = pending;
    else if (values.length < 512) values.push(pending);
    else return false;
    setSaveEditorPendingValues(record, values);
    record.error = values.some((entry) => !entry.valid) ? 'A pending value is outside its verified format or range. Correct it or revert the pending values.' : '';
    scheduleSaveEditorFlush(record);
    const revert = $('#save-editor-revert'); if (revert) revert.hidden = false;
    updateSaveEditorStatus(); return valid;
  }
  async function flushSaveEditorEdit(continueQueue = true) {
    if (saveEditorTimer !== null) clearTimeout(saveEditorTimer); saveEditorTimer = null;
    const record = state.saveEditor, pending = saveEditorPendingValues(record).find((value) => value.valid);
    if (!record || record.session.readOnly === true || !pending || record.working || !saveEditorVisible(record) || record.mode !== 'edit' || saveEditorReason(record)) return false;
    record.working = true; updateSaveEditorStatus();
    const session = record.session.id, revision = record.session.revision;
    const result = await run('saveEditorSet', { session, revision, field: pending.field, value: pending.value, ...saveEditorAdvancedArgs(record) }, { refresh: false, progress: 'Checking the save draft value…',
      onError: (error) => { if (state.saveEditor === record) record.error = errorText(error); return true; } });
    if (state.saveEditor === record) record.working = false;
    if (state.saveEditor !== record || record.context !== saveManagementContext() || record.session.id !== session || record.session.revision !== revision) return false;
    const updated = saveEditorSession(result?.session);
    if (updated && updated.id === session && updated.sourceId === record.sourceId && updated.revision >= revision && updated.revision <= revision + 1) {
      record.session = updated;
      const remaining = saveEditorPendingValues(record).filter((value) => value !== pending);
      setSaveEditorPendingValues(record, remaining);
      record.error = remaining.some((value) => !value.valid) ? 'A pending value is outside its verified format or range. Correct it or revert the pending values.' : '';
    } else if (saveEditorPendingValues(record).includes(pending)) { pending.valid = false; record.error ||= 'The draft value was not confirmed. Correct it or revert the pending value.'; }
    if (saveEditorVisible(record) && record.mode === 'edit') showSaveEditor(record);
    if (continueQueue) scheduleSaveEditorFlush(record, 0);
    else if (saveEditorTimer !== null) { clearTimeout(saveEditorTimer); saveEditorTimer = null; }
    return !!updated;
  }
  async function closeSaveEditor(record, afterClose = null) {
    if (state.saveEditor !== record || record.working) return false;
    const generation = modalContentGeneration;
    record.working = true;
    updateSaveEditorStatus();
    const result = await run('saveEditorClose', { session: record.session.id }, { refresh: false, progress: 'Closing the temporary save draft…',
      onError: (error) => { record.error = errorText(error); return true; } });
    if (state.saveEditor !== record) return false;
    record.working = false;
    if (!result || result.closed !== true || result.session !== null) { record.error ||= 'The native save draft did not confirm that it closed.'; if (generation === modalContentGeneration) showSaveEditor(record); return false; }
    ++saveEditorGeneration; if (saveEditorTimer !== null) clearTimeout(saveEditorTimer); saveEditorTimer = null;
    state.saveEditor = null; if (generation === modalContentGeneration) $('#modal').close(); updateCurrentSavesPanel();
    if (typeof afterClose === 'function' && generation === modalContentGeneration) await afterClose(); return true;
  }
  function requestCloseSaveEditor(afterClose = null) {
    const record = state.saveEditor;
    if (!record || record.working) return false;
    if (saveEditorTimer !== null) clearTimeout(saveEditorTimer); saveEditorTimer = null;
    if (record.session.dirty !== true && !record.pending) return closeSaveEditor(record, afterClose);
    record.mode = 'discard';
    modal('Discard this save draft?', '<div class="save-editor-dialog" id="save-editor-content"><p>These changes have not been applied to the current game save. Close the temporary draft and discard them?</p><p>The original save is still in place.</p></div>', '<button type="button" class="button" id="save-editor-keep-editing">Keep editing</button><button type="button" class="button danger" id="save-editor-discard">Discard draft</button>');
    record.modalGeneration = modalContentGeneration;
    const generation = record.modalGeneration;
    const current = () => saveEditorVisible(record) && record.modalGeneration === generation && record.mode === 'discard';
    const keepEditing = () => current() && !record.working ? showSaveEditor(record) : false;
    $$('[data-modal-close]', $('#modal')).forEach((control) => { control.onclick = keepEditing; });
    $('#save-editor-keep-editing').onclick = keepEditing;
    $('#save-editor-discard').onclick = () => current() ? closeSaveEditor(record, afterClose) : false;
    return true;
  }
  async function reviewSaveEditor() {
    const record = state.saveEditor;
    if (!record || record.session.readOnly === true || record.working || !saveEditorVisible(record) || record.mode !== 'edit' || saveEditorReason(record) ||
        saveEditorPendingValues(record).some((value) => !value.valid)) return false;
    for (let count = 0; record.pending && count < 512; ++count) {
      if (!await flushSaveEditorEdit(false)) return false;
      if (state.saveEditor !== record || record.context !== saveManagementContext() || !saveEditorVisible(record) || record.mode !== 'edit' ||
          saveEditorPendingValues(record).some((value) => !value.valid) || saveEditorReason(record)) return false;
    }
    if (record.pending || record.session.dirty !== true) return false;
    const session = record.session.id, revision = record.session.revision, generation = modalContentGeneration;
    record.working = true; updateSaveEditorStatus();
    const result = await run('saveEditorReview', { session, revision, ...saveEditorAdvancedArgs(record) }, { refresh: false, activity: true, progress: 'Checking save changes and the safety-backup plan…',
      onError: (error) => { record.error = errorText(error); return true; } });
    if (state.saveEditor === record) record.working = false;
    if (state.saveEditor !== record || record.context !== saveManagementContext() || record.session.id !== session || record.session.revision !== revision) return false;
    if (generation !== modalContentGeneration || !$('#modal').open) return false;
    if (!result || result.id !== record.sourceId || result.operation !== 'edit-current-save' || typeof result.token !== 'string' || !result.token || result.token.length > 512 || result.safetyBackupRequired !== true || !Array.isArray(result.changes) || !result.changes.length) {
      record.error ||= 'The change review did not return a verified plan.'; showSaveEditor(record); return false;
    }
    record.mode = 'review'; record.reviewUsed = false; record.review = result;
    const advancedWarning = Number.isSafeInteger(result.advancedChangeCount) && result.advancedChangeCount > 0
      ? `<div class="banner warning"><p><strong>Advanced changes included.</strong> ${escape(typeof result.advancedWarning === 'string' ? result.advancedWarning.slice(0, 1024) : 'These fields are schema-checked, but their in-game effect has not been verified. Keep the safety backup and test the exact saved slot.')}</p></div>` : '';
    modal('Apply changes to this save?', '<div class="save-editor-dialog" id="save-editor-content">' + advancedWarning + currentSavePlanBody({ ...result, targetName: result.targetName || record.session.saveName }, result.changes) + '<p class="field-note" id="save-editor-status" role="status"></p></div>', '<button type="button" class="button" id="save-editor-back">Back to editor</button><button type="button" class="button primary" id="save-editor-apply">Back up and apply changes</button>');
    bindSaveEditorClose(record);
    const reviewGeneration = record.modalGeneration;
    const currentReview = () => saveEditorVisible(record) && record.modalGeneration === reviewGeneration &&
      record.mode === 'review' && record.review === result && record.session.revision === revision;
    $('#save-editor-back').onclick = () => currentReview() && !record.working ? showSaveEditor(record) : false;
    $('#save-editor-apply').onclick = async () => {
      if (!currentReview()) return false;
      if (record.session.readOnly === true || record.working || record.pending || record.reviewUsed || saveEditorReason(record)) { updateSaveEditorStatus(); return false; }
      record.reviewUsed = true; record.working = true; updateSaveEditorStatus();
      const completed = await run('saveEditorApply', { token: result.token, confirmed: true }, { refresh: false, activity: true, progress: 'Backing up current saves and applying the reviewed changes…',
        onError: (error) => { record.error = errorText(error) + ' Return to the editor and review again before retrying.'; return true; } });
      if (state.saveEditor === record) record.working = false;
      if (state.saveEditor !== record || record.context !== saveManagementContext()) return false;
      if (completed?.replaced !== true) { updateSaveEditorStatus(); return false; }
      if (completed.session !== null) await request('saveEditorClose', { session }).catch(() => {});
      if (state.saveEditor !== record || record.context !== saveManagementContext()) return false;
      state.saveEditor = null; ++saveEditorGeneration;
      if (record.modalGeneration === modalContentGeneration) $('#modal').close();
      return finishCurrentSaveChange(completed, record, 'Save changes applied. Its safety backup is in Backups.');
    };
    updateSaveEditorStatus(); return true;
  }
  function saveDeleteBlockReason(record) {
    if (record.context !== saveManagementContext()) return 'The selected installation changed. Close this review and select the file again.';
    if (state.busy || state.nativeDropActivity?.running) return 'Wait for the current operation to finish.';
    if (state.snapshot.libraryBackupRecovery?.pending || state.snapshot.saveRecoveryRequired || state.saveBackups?.recovery?.pending)
      return 'Finish the interrupted recovery before deleting saved files.';
    if (record.current) {
      if (state.profileSession?.active) return 'Finish the profile session before deleting a current save.';
      if (state.snapshot.gameRunning !== false) return 'Close Days Gone and wait until its status is confirmed before deleting a current save.';
      if (state.currentSaves?.context !== record.saveContext || selectedCurrentSave()?.id !== record.id)
        return 'The current save selection changed. Close this review and select it again.';
    } else if (selectedSaveBackup()?.id !== record.id) return 'The selected backup changed. Close this review and select it again.';
    return '';
  }
  function updateSaveDeleteStatus() {
    const record = state.saveDeleteReview;
    if (!record) return;
    if (!$('#modal')?.open || record.modalGeneration !== modalContentGeneration || $('#save-delete-review') !== record.element) { state.saveDeleteReview = null; return; }
    const reason = saveDeleteBlockReason(record), status = $('#save-delete-status'), control = $('#save-delete-confirm');
    if (status) status.textContent = reason || record.error || (record.current ? 'A complete safety backup is required before this file can be removed.' : 'This removes only the selected stored backup.');
    if (control) { control.disabled = !!reason; control.title = reason; }
  }
  let saveDeleteGeneration = 0;
  async function reviewSaveDeletion(id, current) {
    const context = saveManagementContext(), generation = ++saveDeleteGeneration, modalGeneration = modalContentGeneration,
      saveContext = state.currentSaves?.context,
      selected = current ? selectedCurrentSave() : selectedSaveBackup();
    if (!selected || selected.id !== id || state.busy || (current && selected.deleteEligible !== true)) return false;
    const guard = { context, id, current, saveContext };
    if (saveDeleteBlockReason(guard)) { toast(saveDeleteBlockReason(guard), 'warning'); return false; }
    const review = await run(current ? 'currentSaveDeleteReview' : 'backupDeleteReview', { id },
      { refresh: false, progress: current ? 'Checking the selected save…' : 'Checking the selected backup…',
        onError: () => context !== saveManagementContext() || generation !== saveDeleteGeneration || modalGeneration !== modalContentGeneration });
    if (generation !== saveDeleteGeneration || modalGeneration !== modalContentGeneration || saveDeleteBlockReason(guard) || typeof review?.token !== 'string' || !review.token || review.token.length > 512 || review.id !== id) return false;
    const name = current ? review.name || selected.name : saveBackupTitle(selected),
      warnings = (Array.isArray(review.warnings) ? review.warnings : []).filter((value) => typeof value === 'string').slice(0, 16);
    modal(current ? 'Delete this current save?' : 'Delete this backup?',
      `<div id="save-delete-review"><h3>${escape(name)}</h3><p>${current
        ? 'The toolkit backs up all current save files first, even when automatic backups are off. It then removes this one reviewed file. Other saves stay in place.'
        : 'This permanently removes the selected backup, its picture, notes and profile links. Your current game saves and other backups stay in place.'}</p>${warnings.length ? `<ul class="sg-review-warnings">${warnings.map((warning) => `<li>${escape(warning)}</li>`).join('')}</ul>` : ''}<p id="save-delete-status" class="field-note" role="status"></p></div>`,
      `<button type="button" class="button" data-modal-close>Cancel</button><button type="button" class="button danger" id="save-delete-confirm" aria-describedby="save-delete-status">${icon('trash')}${current ? 'Back up and delete save' : 'Delete backup'}</button>`);
    const record = { ...guard, token: review.token, element: $('#save-delete-review'), modalGeneration: modalContentGeneration, used: false, error: '' };
    state.saveDeleteReview = record;
    $('#save-delete-confirm').onclick = async () => {
      if (state.saveDeleteReview !== record || record.modalGeneration !== modalContentGeneration || $('#save-delete-review') !== record.element || !$('#modal').open) return false;
      if (saveDeleteBlockReason(record)) { updateSaveDeleteStatus(); return false; }
      if (record.used) return reviewSaveDeletion(id, current);
      record.used = true;
      const result = await run(current ? 'currentSaveDelete' : 'backupDelete', { token: record.token, confirmed: true },
        { refresh: false, activity: true, progress: current ? 'Backing up saves before deleting the selected file…' : 'Deleting the reviewed backup…',
          onError: (error) => { record.error = errorText(error); return true; } });
      if (context !== saveManagementContext() || state.saveDeleteReview !== record || record.modalGeneration !== modalContentGeneration || !$('#modal').open || $('#save-delete-review') !== record.element) return false;
      if (result?.deleted !== true) {
        record.error ||= 'The deletion did not finish. Review this selection again before retrying.';
        $('#save-delete-confirm').textContent = 'Review again'; updateSaveDeleteStatus(); return false;
      }
      state.saveDeleteReview = null; $('#modal').close();
      if (!current) { state.savePictures.delete(id); state.saveDetailsDrafts.delete(id); }
      await loadSaveBackups();
      if (context !== saveManagementContext()) return false;
      if (current) await loadCurrentSaves();
      if (context !== saveManagementContext()) return false;
      toast(current ? 'Save removed. Its safety backup is in Backups.' : 'Backup deleted.', 'success', current && result.safetyBackupId ? {
        label: 'Show backup', run: () => { if (context !== saveManagementContext()) return; state.saveProfileFilter = null; state.saveSelection = { context: state.snapshot.accountContext, id: result.safetyBackupId }; setSavesTab('backups'); }
      } : undefined);
      return true;
    };
    updateSaveDeleteStatus();
    return true;
  }
  let saveImportGeneration = 0;
  async function importSaveFiles(files = null, dropToken = null) {
    if (state.busy || state.nativeDropActivity?.running) return false;
    const context = saveManagementContext(), generation = ++saveImportGeneration,
      modalGeneration = modalContentGeneration, originalView = state.view;
    const result = await run(files ? 'importDropped' : 'saveImportBrowse', files ? { target: 'saves', ...(dropToken ? { dropToken } : {}) } : {}, { objects: files && !dropToken ? files : undefined, refresh: false, activity: true, progress: 'Keeping the selected saves in a separate backup…',
      onError: () => context !== saveManagementContext() || generation !== saveImportGeneration || modalGeneration !== modalContentGeneration });
    if (context !== saveManagementContext() || generation !== saveImportGeneration || !result || result.cancelled || result.imported !== true) return false;
    await loadSaveBackups();
    if (context !== saveManagementContext() || generation !== saveImportGeneration) return false;
    state.saveProfileFilter = null;
    state.saveSelection = { context: state.snapshot.accountContext, id: result.id };
    state.savesTab = 'backups';
    if (state.view === 'saves') render();
    if (modalGeneration === modalContentGeneration && state.view === originalView)
      toast('Saves imported as a separate backup. Choose Review restore to use them.', 'success');
    return true;
  }
  let saveRestoreGeneration = 0;
  async function reviewSaveBackup(id, recovery = false) {
    const context = state.snapshot.accountContext, generation = ++saveRestoreGeneration;
    const review = await run(recovery ? 'saveBackupRecoveryReview' : 'saveBackupReview', recovery ? {} : { id },
      { refresh: false, progress: 'Checking the saved files…' });
    if (context !== state.snapshot.accountContext || generation !== saveRestoreGeneration ||
        typeof review?.token !== 'string' || !review.token || review.token.length > 512) return false;
    const summary = review.summary || {}, warnings = (Array.isArray(review.warnings) ? review.warnings : []).filter((warning) => typeof warning === 'string').slice(0, 16);
    let files = null;
    if (!recovery) {
      try {
        const data = await request('saveBackupFiles', { id });
        if (Array.isArray(data?.files)) files = data;
      } catch { /* A read-only file list cannot replace or authorize a native restore plan. */ }
    }
    if (context !== state.snapshot.accountContext || generation !== saveRestoreGeneration || state.busy) return false;
    const backup = saveBackupList().find((item) => item.id === (review.id || id)),
      picture = backup && state.savePictures.get(backup.id),
      title = backup ? saveBackupTitle(backup) : summary.createdAt ? logTime(summary.createdAt) : recovery ? 'Preserved save files' : 'Reviewed save backup';
    const currentFiles = Number.isSafeInteger(summary.currentFiles) ? `${summary.currentFiles} current files · ${formatBytes(Number(summary.currentBytes) || 0)}` : 'The native review checked the current save folder.';
    modal(recovery ? 'Recover the interrupted restore' : 'Restore this save backup',
      `<div class="sg-restore-review" id="save-restore-review"><div class="sg-review-source">${picture ? `<img src="${escape(picture)}" alt="Picture for the reviewed save backup">` : `<span class="sg-review-placeholder">${icon('save')}</span>`}<div><span class="eyebrow">${recovery ? 'RECOVERY RECORD' : 'SELECTED BACKUP'}</span><h3>${escape(title)}</h3>${backup?.label ? `<p>${escape(logTime(backup.createdAt))}</p>` : ''}<p>${escape(saveBackupReason(summary.reason || backup?.reason))}</p><small>${Number(summary.files) || 0} files · ${formatBytes(Number(summary.bytes) || 0)}</small></div></div><ol class="sg-review-steps"><li><span class="sg-step-number">1</span><div><strong>Keep your current saves</strong><p>${escape(currentFiles)}</p><p>A verified safety copy is made before anything is replaced.</p></div>${icon('shield')}</li><li><span class="sg-step-number">2</span><div><strong>${recovery ? 'Put back the preserved save files' : 'Restore the reviewed save files'}</strong><p>${recovery ? 'The recovery record restores the files held by the interrupted operation. Changed or unexpected files block recovery.' : 'Matching save files are replaced by this backup. Other current saves stay in place.'}</p><p>Mods and profiles stay as they are.</p></div></li></ol>${warnings.length ? `<ul class="sg-review-warnings">${warnings.map((warning) => `<li>${escape(warning)}</li>`).join('')}</ul>` : ''}${!recovery ? saveRestoreFiles(files) : '<p class="field-note">The native recovery record names every file involved; the record is checked again when you confirm.</p>'}<p id="save-restore-game-state" class="sg-review-game-state" role="status"></p></div>`,
      `<button class="button" data-modal-close>Cancel</button><button id="confirm-action" class="button primary" aria-describedby="save-restore-game-state">${icon('back')}${recovery ? 'Recover saves' : 'Restore backup'}</button>`);
    const record = { context, recovery, element: $('#save-restore-review'), token: review.token };
    state.saveRestoreReview = record;
    $('#confirm-action').onclick = async () => {
      if (state.saveRestoreReview !== record || $('#save-restore-review') !== record.element || !$('#modal').open) return false;
      if (saveRestoreBlockReason(record)) { updateSaveRestoreStatus(); return false; }
      state.saveRestoreReview = null;
      $('#modal').close();
      const result = await run(recovery ? 'saveBackupRecover' : 'saveBackupRestore', { token: record.token, confirmed: true },
        { refresh: false, progress: recovery ? 'Recovering the previous saves…' : 'Backing up current saves and restoring…' });
      if (context !== state.snapshot.accountContext) return false;
      await refresh({ quiet: true, preserveForm: true, background: true });
      if (context !== state.snapshot.accountContext) return false;
      renderPlayReadiness();
      await loadSaveBackups();
      if (context === state.snapshot.accountContext && (recovery ? result?.recovered === true : result?.restored === true))
        toast(recovery ? 'Save recovery finished.' : 'Save backup restored.', 'success');
      return !!result;
    };
    updateSaveRestoreStatus();
    return true;
  }
  async function previewCrashLog(id) {
    const result = await run(
      'crashLogRead',
      { id },
      { refresh: false, progress: 'Reading the selected text log and removing personal details…' }
    );
    if (!result) return;
    modal(
      result.name || 'Log preview',
      `<p>${result.redacted ? 'Personal paths and known secret patterns have been removed.' : 'Review this text before sharing.'} ${result.truncated ? 'This preview is shortened to the safe reading limit.' : ''}</p>${result.binary ? `<div class="banner neutral">${icon('info')}<p><strong>This file is mostly binary data.</strong> Only the readable text found inside it is shown.</p></div>` : result.skippedBytes ? `<p class="field-note">Unreadable bytes (often left over after a crash) were left out.</p>` : ''}<pre class="log-preview" tabindex="0" aria-label="Log text">${escape(result.text || '(The log contains no readable text.)')}</pre>`,
      `<button class="button" data-modal-close>Close</button><button class="button primary" id="copy-log-preview">${icon('report')}Copy preview</button>`
    );
    $('#copy-log-preview').onclick = async () => {
      const copied = await run('crashLogCopy', { id }, { refresh: false });
      if (copied) toast('Sanitized log preview copied.');
    };
  }
  function crashDuration(seconds) {
    if (!Number.isFinite(seconds) || seconds < 0) return '';
    if (seconds < 90) return `${seconds} second${seconds === 1 ? '' : 's'}`;
    if (seconds < 5400) {
      const minutes = Math.round(seconds / 60);
      return `${minutes} minute${minutes === 1 ? '' : 's'}`;
    }
    const hours = Math.round(seconds / 3600);
    return `${hours} hour${hours === 1 ? '' : 's'}`;
  }
  // The native provider reads only the bounded crash-record parts of the dump and
  // redacts every string. Everything is still escaped here before display.
  async function summarizeCrashDump(id) {
    const result = await run(
      'crashLogSummary',
      { id },
      { refresh: false, progress: 'Reading the crash details from the dump…' }
    );
    if (!result) return;
    const text = (value) => (typeof value === 'string' ? value : '');
    const exception = result.exception || {},
      location = result.location || {},
      memory = result.memory || {},
      system = result.system || {},
      hardware = result.hardware || {},
      engine = result.engine || {},
      same = result.sameCrash || {};
    const listed = Array.isArray(result.modules?.listed) ? result.modules.listed : [];
    const advice = Array.isArray(result.advice) ? result.advice.filter((item) => typeof item === 'string') : [];
    const copies = Array.isArray(result.copies) ? result.copies.filter((item) => typeof item === 'string') : [];
    const gigabytes = (bytes) => (Number.isFinite(bytes) && bytes > 0 ? `${(bytes / 1073741824).toFixed(1)} GB` : '');
    const rows = [
      ['Crashed at', Number.isFinite(result.crashedAt) ? logTime(result.crashedAt) : ''],
      ['Running time', crashDuration(result.secondsSinceStart)],
      ['What happened', exception.name ? `${text(exception.name)} (${text(exception.code)})` : ''],
      [
        'Where',
        location.module
          ? `${text(location.module)} + ${text(location.offset)} · ${text(location.label)}`
          : location.address
            ? `Address ${text(location.address)} · ${text(location.label)}`
            : ''
      ],
      ['Fingerprint', text(result.fingerprint)],
      ['Out of memory', typeof memory.outOfMemory === 'boolean' ? (memory.outOfMemory ? 'Yes' : 'No') : ''],
      [
        'Memory in use',
        gigabytes(memory.usedPhysicalBytes) && gigabytes(memory.totalPhysicalBytes)
          ? `${gigabytes(memory.usedPhysicalBytes)} of ${gigabytes(memory.totalPhysicalBytes)}`
          : ''
      ],
      [
        'System',
        system.windowsVersion
          ? `Windows ${text(system.windowsVersion)} · ${Number(system.logicalProcessors) || '?'} logical processors · ${text(system.architecture)}`
          : ''
      ],
      ['CPU', text(hardware.cpu)],
      ['GPU', text(hardware.gpu)],
      [
        'Game engine',
        engine.version ? `${text(engine.version)}${engine.configuration ? ' · ' + text(engine.configuration) : ''}` : ''
      ]
    ].filter(([, value]) => value);
    modal(
      'Crash summary',
      `<p class="crash-headline">${escape(text(result.headline) || 'This crash dump could not be summarized.')}</p>${
        result.status === 'incomplete'
          ? `<div class="banner warning"><p>${escape(text(result.reason) || 'The dump is incomplete or damaged.')}</p></div>`
          : ''
      }${
        Number(same.occurrences) > 1
          ? `<div class="banner neutral"><p><strong>This crash happened ${Number(same.occurrences)} times</strong> in the dumps listed here. A crash that repeats at the same place usually has one specific cause.</p></div>`
          : ''
      }${
        advice.length
          ? `<h3 class="crash-advice-title">What you can try</h3><ul class="crash-advice">${advice.map((item) => `<li>${escape(item)}</li>`).join('')}</ul>`
          : `<h3 class="crash-advice-title">What you can try</h3><ul class="crash-advice"><li>Start the game with your mods switched off. If it no longer crashes, Find the bad mod narrows down which mod causes it. If it still crashes, verify the game files in Steam or Epic and update your graphics driver.</li></ul>`
      }${renderCrashMods(result.activeMods || {})}${rows.length ? `<details class="finding-evidence"><summary>Technical details</summary><dl class="detail-grid crash-details">${rows.map(([label, value]) => `<dt>${escape(label)}</dt><dd>${escape(value)}</dd>`).join('')}</dl></details>` : ''}${
        copies.length
          ? `<p class="field-note">The crash reporter saved another copy of this crash: ${copies.map((name) => escape(name)).join(', ')}.</p>`
          : ''
      }${
        listed.length
          ? `<details class="finding-evidence"><summary>Other programs loaded into the game · ${listed.length}</summary><ul>${listed.map((module) => `<li><code>${escape(text(module.name))}</code> · ${escape(text(module.label))}</li>`).join('')}</ul></details>`
          : ''
      }${
        result.unreal?.errorMessage
          ? `<p><strong>Game error message</strong></p><pre class="log-preview" tabindex="0">${escape(text(result.unreal.errorMessage))}</pre>`
          : ''
      }${
        result.unreal?.callStack
          ? `<p><strong>Game call stack</strong></p><pre class="log-preview" tabindex="0">${escape(text(result.unreal.callStack))}</pre>`
          : ''
      }<p class="field-note">${escape(text(result.limits))}</p>`,
      `<button class="button" data-modal-close>Close</button><button class="button" id="crash-summary-guide">${icon('book')}How to read this</button><button class="button" id="crash-find-mod">${icon('search')}Find the bad mod</button><button class="button primary" id="copy-crash-summary">${icon('report')}Copy summary</button>`
    );
    $('#crash-find-mod').onclick = () => {
      $('#modal').close();
      openFinder();
    };
    $('#copy-crash-summary').onclick = async () => {
      const copied = await run('crashLogCopy', { id, summary: true }, { refresh: false });
      if (copied) toast('Crash summary copied.');
    };
    $('#crash-summary-guide').onclick = () => {
      $('#modal').close();
      openGuideArticle('crash-summary');
    };
  }
  async function reviewSupportBundle() {
    const ids = [...state.selectedCrashLogs];
    const plan = await run(
      'supportBundlePreview',
      { ids },
      { refresh: false, progress: 'Preparing fresh diagnostics and the selected sanitized text logs…' }
    );
    if (!plan) return;
    if (!plan.planToken) {
      toast('This support ZIP preview expired. Prepare it again.', 'error');
      return;
    }
    const files = plan.files || [];
    modal(
      'Review the support ZIP',
      `<p>Only the files listed below will be included. The report contains fresh diagnostics and mod details; text logs and crash summaries are included only when you selected them.</p>${plan.summary ? `<p>${escape(typeof plan.summary === 'string' ? plan.summary : plan.summary.message || 'Review the prepared files before exporting.')}</p>` : ''}<ul class="bundle-files">${files.map((file) => `<li><code>${escape(file.name || 'Report file')}</code><span>${formatBytes(file.bytes)}</span></li>`).join('')}</ul>${plan.warnings?.length ? `<div class="banner warning"><p>${plan.warnings.map((item) => escape(typeof item === 'string' ? item : item.message || 'Review required')).join('<br>')}</p></div>` : ''}<p>Personal paths and known secret patterns are removed. Review the material before sharing it. The toolkit does not upload the ZIP. Raw memory dumps are never included; a selected dump adds only its crash summary.</p>`,
      `<button class="button" data-modal-close>Cancel</button><button class="button primary" id="support-export-confirm">${icon('report')}Save support ZIP</button>`
    );
    $('#support-export-confirm').onclick = async () => {
      $('#modal').close();
      const result = await run(
        'supportBundleExport',
        { ids, planToken: plan.planToken, confirmed: true },
        { refresh: false, progress: 'Saving the reviewed support ZIP…' }
      );
      if (result && !result.cancelled) toast('Support ZIP saved. Review it before sharing.');
    };
  }
  async function reviewDuplicateCleanup() {
    if (state.preview) { toast('Duplicate checks need the desktop application.', 'info'); return; }
    const review = await run('diagnosticsReviewDuplicates', {}, { refresh: false, progress: 'Checking exact duplicate files…' });
    if (!review) return;
    const groups = Array.isArray(review.groups) ? review.groups : [];
    const count = Number(review.count) || 0;
    const skipped = Array.isArray(review.skipped) ? review.skipped : [];
    const fileLabel = (file) => `<strong>${escape(file?.file || 'PAK file')}</strong><small>${escape(file?.location || 'Installed package')} · ${file?.owned ? 'Managed' : 'Installed outside the toolkit'}</small>`;
    modal('Review duplicate files', `<p>${count ? 'These files have identical contents. Review the copy that stays and each extra file to remove.' : 'No verified extra copies can be removed in this review.'} Unknown files and the game’s base packages stay in place.</p>${groups.map((group) => `<section class="duplicate-group"><h3>Keep this copy</h3><div class="duplicate-file keep">${fileLabel(group.keep)}</div><h3>Remove these extra copies</h3><ul>${(group.remove || []).map((file) => `<li class="duplicate-file">${fileLabel(file)}</li>`).join('')}</ul></section>`).join('')}${skipped.length ? `<p class="field-note">${skipped.length} group${skipped.length === 1 ? '' : 's'} need a manual check.</p>` : ''}<p class="field-note">The toolkit checks the files again before removal. If a later file changes, earlier removals may already be complete; the result will say what happened. The verified copy shown under Keep stays in place.</p>`, `<button class="button" data-modal-close>Cancel</button>${count && review.token ? '<button class="button danger" id="duplicate-clean-confirm">Remove extra copies</button>' : ''}`);
    const confirm = $('#duplicate-clean-confirm');
    if (confirm) confirm.onclick = async () => {
      $('#modal').close();
      const result = await run('diagnosticsCleanDuplicates', { token: review.token, confirmed: true }, { refresh: true, progress: 'Removing the reviewed extra copies…' });
      if (!result) return;
      state.diagnosticStale = true;
      modal(result.completed === true ? 'Duplicate cleanup finished' : 'Duplicate cleanup stopped', `<p>${escape(result.count ?? result.removed?.length ?? 0)} extra file${Number(result.count ?? result.removed?.length) === 1 ? '' : 's'} removed.</p>${result.stoppedReason ? `<p>${escape(result.stoppedReason)}</p>` : ''}<p>${result.completed !== true || result.needsReview ? 'Review duplicates again before removing anything else.' : 'The verified copies kept in your setup are unchanged.'}</p>`, '<button class="button" data-modal-close>Close</button><button class="button primary" id="duplicate-check-again">Run diagnostics</button>');
      $('#duplicate-check-again').onclick = () => { $('#modal').close(); loadDiagnosticReport(); };
    };
  }
  async function loadDiagnosticReport() {
    if (state.diagnosticsLoading) return;
    const context = state.snapshot.accountContext;
    state.diagnosticsLoading = true;
    state.view = 'diagnostics';
    render();
    try {
      const result = await run(
        'diagnosticsRun',
        {},
        { refresh: false, progress: 'Checking your game folder, mods, mod loading and profiles…' }
      );
      if (result && context === state.snapshot.accountContext) {
        state.diagnosticReport = result;
        state.diagnosticStale = false;
        render();
        if (!state.crashLogs) loadCrashLogs();
        setStatus('Diagnostics finished. The results are below.');
        return result;
      }
    } finally {
      if (context === state.snapshot.accountContext) state.diagnosticsLoading = false;
    }
  }
  function openGuideArticle(key) {
    state.view = 'help';
    state.helpSection = 'help';
    state.article = (window.FIELD_GUIDE || []).some((article) => article.id === key) ? key : 'reporting';
    render();
    window.scrollTo?.(0, 0);
    $('#main-content').focus({ preventScroll: true });
  }
  function reviewDiagnosticFix(id) {
    const report = state.diagnosticReport,
      check = report?.checks?.find((item) => String(item.id) === String(id));
    if (!check?.fix || !report.planToken || state.diagnosticStale) {
      toast('Run fresh diagnostics before reviewing this repair.', 'warning');
      return;
    }
    const fix = check.fix,
      choices = fix.choices || [];
    modal(
      'Review this repair',
      `<div class="detail-category">${escape(check.title || id)}</div><p>${escape(check.message || '')}</p><p><strong>${escape(fix.label || 'Review the proposed action')}</strong></p>${choices.length ? `<p>Choose the managed mod you want to disable. The toolkit does not pick a winner or merge their edits.</p><label class="field-label" for="diagnostic-choice">Your choice</label><select id="diagnostic-choice" class="input full-width"><option value="">Choose a mod to disable…</option>${choices.map((choice) => `<option value="${escape(choice.id)}">${escape(choice.label || modName(choice.id))}</option>`).join('')}</select>` : ''}${diagnosticEvidence(check, true)}<p class="mt-16">The toolkit checks everything again right before the fix and stops if something changed. Close Days Gone first.</p>`,
      `<button class="button" data-modal-close>Cancel</button><button id="diagnostic-repair-confirm" class="button primary" ${choices.length ? 'disabled' : ''}>Apply fix</button>`
    );
    if (choices.length)
      $('#diagnostic-choice').onchange = () => {
        $('#diagnostic-repair-confirm').disabled = !choices.some(
          (choice) => String(choice.id) === $('#diagnostic-choice').value
        );
      };
    $('#diagnostic-repair-confirm').onclick = async () => {
      const choiceId = choices.length ? $('#diagnostic-choice').value : undefined;
      if (choices.length && !choices.some((choice) => String(choice.id) === choiceId)) return;
      $('#modal').close();
      const result = await run(
        'diagnosticsRepair',
        { id: check.id, planToken: report.planToken, ...(choiceId ? { choiceId } : {}), confirmed: true },
        { refresh: false, progress: 'Checking again and applying the fix…' }
      );
      if (result) {
        await refresh({ quiet: true });
        await loadDiagnosticReport();
        toast('Repair finished. The findings have been checked again.');
      }
    };
  }
  function externalChangeReason(m) {
    return state.preview
      ? 'Open the desktop application to manage files.'
      : state.profileSession.active
        ? 'Wait for the profile session to restore original files.'
        : state.snapshot.gameRunning
          ? 'Close Days Gone before changing mods.'
          : state.snapshot.recoveryRequired
            ? 'Finish the pending file recovery in Diagnostics first.'
            : '';
  }
  function externalList() {
    return `<section class="external-list"><div class="section-heading"><div><h2>Installed outside the toolkit</h2><p>These files still take part in conflict checks. Review an exact file before managing or removing it.</p></div><div class="inline-actions">${manageableExternal().length > 1 ? button('Manage all', 'external-manage-all', 'check', 'small') : ''}${button('Open mods folder', 'open-folder', 'folder', 'small')}</div></div>${externalMods()
      .map((m) => {
        const reason = externalChangeReason(m),
          locked = state.busy || !!reason || !m.id;
        return `<div class="external-row">${modArtwork({ ...m, name: m.name || m.filename || m.file || '' }, { compact: true })}<div class="external-copy"><strong>${escape(m.name || m.filename || m.file || m.path?.split(/[\\/]/).pop() || 'External PAK')}</strong><small>${escape(m.path || m.relativePath || 'Package location not supplied')}</small>${reason || (m.canRemoveExternal !== true && m.externalRemovalReason) ? `<small class="external-reason">${escape(reason || m.externalRemovalReason)}</small>` : ''}</div><div class="external-actions"><span class="badge tone-badge info">Installed by hand</span><button class="button small" data-external-manage="${escape(m.id || '')}" ${locked || m.canReviewAdoption !== true ? 'disabled' : ''} title="${escape(reason || m.adoptionReason || 'Inspect this installed package for local management')}">${icon('plus')}Manage</button><button class="button small danger" data-external-remove="${escape(m.id || '')}" ${locked || m.canRemoveExternal !== true ? 'disabled' : ''} title="${escape(reason || m.externalRemovalReason || 'Review this exact installed file before removal')}">${icon('trash')}Delete</button></div></div>`;
      })
      .join('')}</section>`;
  }
  async function reviewExternalAdoption(id) {
    if (state.busy || state.preview) return false;
    if (!(await refresh({ quiet: true, preserveForm: true, background: true }))) {
      toast('Could not check your mods folder. Try again in a moment.', 'warning');
      return false;
    }
    const installed = externalMods().find((m) => String(m.id) === String(id));
    if (!installed || installed.canReviewAdoption !== true || externalChangeReason(installed)) {
      toast(
        externalChangeReason(installed) ||
          installed?.adoptionReason ||
          'This file is not eligible for local management. Refresh the package list.',
        'warning'
      );
      return false;
    }
    const plan = await run(
      'reviewExternalAdoption',
      { id },
      { refresh: false, progress: 'Checking the installed file…' }
    );
    return plan ? reviewDownloadedPackage(plan) : false;
  }
  // Files directly in Saved/Paks that can be managed in place (the same check as a single Manage).
  function manageableExternal() {
    return externalMods().filter(
      (m) => m.id && m.canReviewAdoption === true && !externalChangeReason(m) && (!m.location || m.location === 'saved')
    );
  }
  function releasableMods() {
    return state.snapshot.mods.filter((m) => m.enabled && m.owned);
  }
  function bulkNames(list) {
    return `<ul class="finder-mods">${list.map((m) => `<li>${escape(m.name || m.file || 'PAK file')}</li>`).join('')}</ul>`;
  }
  // Takes over each file through the same native review and token confirmation as a single Manage.
  async function adoptFiles(files, label) {
    const failed = [];
    let done = 0;
    for (const [index, file] of files.entries()) {
      const name = file.name || file.file || 'PAK file';
      let problem = '';
      const quiet = { refresh: false, onError: (error) => ((problem = errorText(error)), true) };
      const plan = await run(
        'reviewExternalAdoption',
        { id: file.id },
        { ...quiet, progress: `${label} ${index + 1} of ${files.length}: ${name}…` }
      );
      if (!plan?.reviewToken || plan.adoption?.mode !== 'take-ownership') {
        failed.push(`${name}: ${problem || 'it can only be added as a separate library copy. Use Manage on it.'}`);
        continue;
      }
      const confirmed = await run(
        'confirmReviewedImport',
        { reviewToken: plan.reviewToken, confirmed: true },
        { ...quiet, progress: `${label} ${index + 1} of ${files.length}: ${name}…` }
      );
      if (confirmed) done += 1;
      else failed.push(`${name}: ${problem || 'it could not be managed.'}`);
    }
    return { done, failed };
  }
  // A managed mod whose installed file was deleted or replaced outside the toolkit: its old "installed by the
  // toolkit" record is dropped automatically through the native review and token (no file is touched). A deleted
  // mod then simply shows as disabled with its library copy kept; a replaced file shows under "Installed outside
  // the toolkit". Each mod is handled once per change, never while the game or a profile session runs.
  const reconcileTried = new Set();
  async function reconcileInstallationRecords() {
    if (
      state.preview ||
      state.busy ||
      state.profileSession.active ||
      state.snapshot.gameRunning ||
      state.snapshot.finder?.active
    )
      return false;
    const mods = state.snapshot.mods.filter(
      (m) =>
        brokenInstallationRecord(m) &&
        m.canForgetInstallation === true &&
        !installationRecordReason(m) &&
        !reconcileTried.has(`${m.id}|${m.status}`)
    );
    if (!mods.length) return false;
    mods.forEach((m) => reconcileTried.add(`${m.id}|${m.status}`));
    const removed = [],
      replaced = [];
    for (const mod of mods) {
      const quiet = { refresh: false, onError: () => true };
      const plan = await run('reviewForgetInstallation', { id: mod.id }, quiet);
      if (!plan?.planToken) continue;
      const done = await run('forgetInstallation', { id: mod.id, planToken: plan.planToken, confirmed: true }, quiet);
      if (done) (plan.current?.present === true ? replaced : removed).push(mod.name);
    }
    if (!removed.length && !replaced.length) return true;
    await refresh({ quiet: true });
    if (removed.length)
      toast(
        `${removed.join(', ')} ${removed.length === 1 ? 'was' : 'were'} deleted outside the toolkit and ${removed.length === 1 ? 'is' : 'are'} now disabled. The library copy is kept.`
      );
    if (replaced.length)
      toast(
        `${replaced.join(', ')} ${replaced.length === 1 ? 'was' : 'were'} replaced outside the toolkit. The new file is listed under Installed outside the toolkit.`
      );
    return true;
  }
  // Runs after every scan (the folder watcher triggers scans when PAK folders change): first old records of
  // files changed outside the toolkit, then optional automatic management of hand-installed PAKs.
  let maintenanceRunning = false;
  async function afterScanMaintenance() {
    if (maintenanceRunning) return;
    maintenanceRunning = true;
    try {
      await reconcileInstallationRecords();
      if (state.snapshot.settings?.autoManageExternal !== false) await autoManageExternal();
    } finally {
      maintenanceRunning = false;
    }
  }
  // With "Manage hand-installed PAKs automatically" on, new eligible files are taken over after a scan
  // without asking. Each file is tried once per session, so a file that cannot be managed never loops.
  const autoManageTried = new Set();
  let autoManageRunning = false;
  async function autoManageExternal() {
    if (
      autoManageRunning ||
      state.preview ||
      state.busy ||
      state.snapshot.settings?.autoManageExternal === false ||
      state.profileSession.active ||
      state.snapshot.gameRunning ||
      state.snapshot.finder?.active
    )
      return false;
    const files = manageableExternal().filter(
      (file) => !autoManageTried.has(`${file.id}|${file.sha256 || file.bytes || ''}`)
    );
    if (!files.length) return false;
    files.forEach((file) => autoManageTried.add(`${file.id}|${file.sha256 || file.bytes || ''}`));
    autoManageRunning = true;
    try {
      const { done, failed } = await adoptFiles(files, 'Managing automatically');
      await refresh({ quiet: true });
      if (done) toast(`${done} hand-installed PAK${done === 1 ? ' is' : 's are'} now managed automatically.`);
      if (failed.length)
        toast(
          `${failed.length} hand-installed PAK${failed.length === 1 ? '' : 's'} could not be managed automatically. Use Manage on ${failed.length === 1 ? 'it' : 'them'} to see why.`,
          'warning'
        );
    } finally {
      autoManageRunning = false;
    }
    return true;
  }
  async function manageAllExternal() {
    if (state.busy || state.preview) return false;
    if (!(await refresh({ quiet: true, preserveForm: true, background: true }))) {
      toast('Could not check your mods folder. Try again in a moment.', 'warning');
      return false;
    }
    const files = manageableExternal();
    if (!files.length) {
      toast('No installed file outside the toolkit can be managed right now.', 'warning');
      return false;
    }
    modal(
      `Manage all ${files.length} files?`,
      `<p>The toolkit takes over these files. They stay installed and enabled, and the toolkit keeps a library copy of each. You can disable or remove them later like any managed mod.</p>${bulkNames(files)}<p class="field-note">Each file is checked the same way as a single Manage before it is taken over.</p>`,
      `<button class="button" data-modal-close>Cancel</button><button class="button primary" id="manage-all-confirm">${icon('check')}Manage all</button>`
    );
    $('#manage-all-confirm').onclick = async () => {
      $('#modal').close();
      const { done, failed } = await adoptFiles(files, 'Managing');
      await refresh({ quiet: true });
      if (failed.length)
        modal(
          done ? `${done} of ${files.length} files are now managed` : 'No file was managed',
          `<p>These files were left as they were:</p><ul class="finder-mods">${failed.map((line) => `<li>${escape(line)}</li>`).join('')}</ul>`,
          '<button class="button primary" data-modal-close>Close</button>'
        );
      else toast(`${done} file${done === 1 ? ' is' : 's are'} now managed and still enabled.`);
    };
    return true;
  }
  async function releaseAllManaged() {
    if (state.busy || state.preview) return false;
    if (!(await refresh({ quiet: true, preserveForm: true, background: true }))) {
      toast('The installed files could not be refreshed. Run Diagnostics first.', 'warning');
      return false;
    }
    const mods = releasableMods();
    if (!mods.length) {
      toast('No enabled mod is managed by the toolkit right now.', 'warning');
      return false;
    }
    modal(
      `Stop managing ${mods.length} mod${mods.length === 1 ? '' : 's'}?`,
      `<p>Their files stay installed and keep working in the game. The toolkit stops managing them and deletes only its own library copies; they move to <strong>Installed outside the toolkit</strong>. You can manage them again any time.</p>${bulkNames(mods)}`,
      `<button class="button" data-modal-close>Cancel</button><button class="button primary" id="release-all-confirm">${icon('folder')}Stop managing all</button>`
    );
    $('#release-all-confirm').onclick = async () => {
      $('#modal').close();
      await run(
        'releaseManaged',
        { ids: mods.map((m) => m.id), confirmed: true },
        {
          progress: 'Handing the installed files back…',
          success: `${mods.length} mod${mods.length === 1 ? ' is' : 's are'} no longer managed. The files are still installed.`
        }
      );
    };
    return true;
  }
  async function reviewExternalRemoval(id) {
    if (state.busy || state.preview) return false;
    const current = await refresh({ quiet: true, preserveForm: true, background: true });
    if (!current) {
      toast('Could not check your mods folder. Try again in a moment.', 'warning');
      return false;
    }
    const installed = externalMods().find((m) => String(m.id) === String(id));
    if (!installed || installed.canRemoveExternal !== true || externalChangeReason(installed)) {
      toast(
        externalChangeReason(installed) ||
          installed?.externalRemovalReason ||
          'This file is not eligible for removal. Refresh the package list.',
        'warning'
      );
      return false;
    }
    const plan = await run(
      'reviewExternalRemoval',
      { id },
      { refresh: false, progress: 'Checking the file before deleting it…' }
    );
    if (!plan?.planToken || !plan.file) return false;
    const file = plan.file;
    modal(
      'Delete this PAK?',
      `<p>Delete <strong>${escape(file.name || file.file || 'this PAK')}</strong> from the mods folder? It was installed by hand, so the toolkit has no copy of it. Keep your own copy if you may want it again.</p><details class="finding-evidence"><summary>File details</summary><dl class="detail-grid"><dt>File</dt><dd>${escape(file.name || file.file || 'Installed PAK')}</dd><dt>Exact path</dt><dd><code>${escape(file.path || 'Path not supplied')}</code></dd><dt>Location</dt><dd>${escape(file.location || 'Installed package folder')}</dd><dt>Size</dt><dd>${formatBytes(file.bytes)}</dd><dt>SHA-256</dt><dd><code>${escape(file.sha256 || '')}</code></dd></dl></details>${plan.warnings?.length ? `<div class="banner warning"><ul>${plan.warnings.map((w) => `<li>${escape(typeof w === 'string' ? w : w.message || 'Review required')}</li>`).join('')}</ul></div>` : ''}<p class="field-note">The toolkit checks the file again right before deleting it and stops if it changed. Other mods and saves are not touched.</p>`,
      `<button class="button" data-modal-close>Cancel</button><button class="button danger" id="external-remove-confirm">${icon('trash')}Delete file</button>`
    );
    $('#external-remove-confirm').onclick = async () => {
      $('#modal').close();
      await run(
        'removeExternal',
        { id, planToken: plan.planToken, confirmed: true },
        {
          progress: 'Deleting the file…',
          success: 'The reviewed external PAK was removed.'
        }
      );
    };
    return true;
  }
  function renderDiagnostics() {
    const items = state.snapshot.diagnostics || [];
    if (!items.length) return '';
    return `<section class="mt-28"><div class="section-heading"><div><h2>Diagnostic checks</h2><p>Folder and mod checks from the last run.</p></div>${button('Export report', 'export', 'report', 'small')}</div><div class="diagnostic-list">${items
      .map((d) => {
        const severity = d.severity || d.level || 'info';
        return `<div class="diagnostic-row ${escape(severity)}">${icon(severity === 'error' ? 'error' : severity === 'warning' ? 'alert' : severity === 'success' || severity === 'ok' ? 'check' : 'info')}<div><strong>${escape(d.title || d.name || d.code || 'Check')}</strong><p>${escape(d.message || d.detail || d.description || '')}</p></div></div>`;
      })
      .join('')}</div></section>`;
  }
  function profileList() {
    return Array.isArray(state.snapshot.profiles)
      ? state.snapshot.profiles
      : Object.entries(state.snapshot.profiles || {}).map(([name, p]) => ({ name, ...p }));
  }
  // Profiles: one slim row each. With many profiles a search (by profile or mod name) and a sort appear; a row's
  // full mod list opens on demand. Profiles that need a look always come first.
  const profileSorts = { name: 'Name A–Z', mods: 'Most mods', size: 'Largest' };
  function profileMatchesCurrent(p) {
    const on = enabledMods().map((m) => m.id),
      ids = (p.mods || []).map((m) => m.id);
    return ids.length > 0 && ids.length === on.length && ids.every((id) => on.includes(id));
  }
  function visibleProfiles() {
    const profiles = state.portableProfiles?.profiles || [],
      query = (state.profileSearch || '').trim().toLowerCase(),
      sort = profileSorts[state.profileSort] ? state.profileSort : 'name';
    const found = query
      ? profiles.filter(
          (p) =>
            String(p.name || '')
              .toLowerCase()
              .includes(query) ||
            (p.mods || []).some((m) =>
              String(m.name || modName(m.id))
                .toLowerCase()
                .includes(query)
            )
        )
      : profiles.slice();
    const count = (p) => p.count ?? p.mods?.length ?? 0;
    return found.sort(
      (a, b) =>
        (a.status === 'ready' ? 1 : 0) - (b.status === 'ready' ? 1 : 0) ||
        (sort === 'mods' ? count(b) - count(a) : sort === 'size' ? (b.bytes || 0) - (a.bytes || 0) : 0) ||
        String(a.name).localeCompare(String(b.name))
    );
  }
  function renderProfileRows() {
    const profiles = visibleProfiles();
    if (!profiles.length)
      return `<div class="empty-state compact"><h3>No profile matches “${escape(state.profileSearch || '')}”.</h3><p>Search looks at profile names and the mods inside them.</p><button type="button" class="button small" data-profile-clear-search>Clear search</button></div>`;
    const busy = state.profileSession.active;
    return profiles
      .map((p) => {
        const ready = p.status === 'ready',
          mods = p.mods || [],
          count = p.count ?? mods.length,
          names = mods.map((m) => m.name || modName(m.id)),
          open = state.openProfiles?.has(p.name),
          inUse = busy && state.profileSession.profileName === p.name;
        const chips = `${inUse ? '<span class="profile-chip live">In use</span>' : ''}${ready && !inUse && profileMatchesCurrent(p) ? '<span class="profile-chip">Same as your mods now</span>' : ''}${ready ? '' : '<span class="profile-chip problem">Needs a look</span>'}`;
        const summary = ready
          ? names.length
            ? `${escape(names.slice(0, 3).join(', '))}${names.length > 3 ? ` <span class="inline-muted">+${names.length - 3} more</span>` : ''}`
            : 'No mods in this profile.'
          : `<span class="profile-problem">${escape(p.error || 'The mod copies in this profile could not be checked. Make the profile again from your current mods.')}</span>`;
        return `<article class="profile-row${ready ? '' : ' needs-look'}${open ? ' open' : ''}"><span class="profile-row-icon">${icon(ready ? 'layers' : 'alert')}</span><div class="profile-row-main"><div class="profile-row-title"><h3>${escape(p.name)}</h3><span class="profile-row-meta">${ready ? `${count} mod${count === 1 ? '' : 's'} · ${formatBytes(p.bytes)}` : 'Not checked'}</span>${chips}</div><p class="profile-row-mods">${summary}</p>${
          open
            ? `<div class="profile-row-details"><ul class="profile-mod-names">${mods.map((m) => `<li><span>${escape(m.name || modName(m.id))}</span><small>${escape(m.filename || '')}${typeof m.bytes === 'number' ? ' · ' + formatBytes(m.bytes) : ''}</small></li>`).join('')}</ul>${p.path ? `<p class="field-note">Stored in <code>${escape(p.path)}</code></p>` : ''}</div>`
            : ''
        }</div><div class="profile-row-actions"><button class="button small primary" data-portable-launch="${escape(p.name)}" ${busy || !ready ? 'disabled' : ''}>${icon('play')}Launch</button>${ready && /^[0-9a-f]{32}$/.test(p.id) ? `<button type="button" class="button small ghost" data-profile-save-backups="${escape(p.id)}">${icon('save')}View save backups</button>` : ''}${mods.length || p.path ? `<button class="button small ghost" data-profile-expand="${escape(p.name)}" aria-expanded="${open ? 'true' : 'false'}">${open ? 'Hide mods' : 'Show mods'}</button>` : ''}${p.owned && /^[0-9a-f]{32}$/.test(p.id) ? `<button type="button" class="button small ghost" data-portable-rename="${escape(p.id)}" ${busy ? 'disabled' : ''}>Rename</button>` : ''}<button class="icon-button" data-portable-delete="${escape(p.name)}" aria-label="Delete profile ${escape(p.name)}" title="${p.canDelete === false ? 'This profile was not made by the toolkit, so it is not deleted here' : 'Delete profile'}" ${busy || p.canDelete === false ? 'disabled' : ''}>${icon('trash')}</button></div></article>`;
      })
      .join('');
  }
  function renderProfiles() {
    const profiles = state.portableProfiles?.profiles || [],
      legacy = profileList(),
      many = profiles.length > 4;
    return `${header('PROFILES', 'Keep separate mod setups.', 'Launch a profile to play with exactly its mods. Your own mods come back when the game closes, so keep the toolkit open while you play.', button('Refresh', 'portable-refresh', 'refresh') + button('Save current mods', 'portable-save', 'plus', 'primary'))}${previewNote()}${
      profiles.length
        ? `${
            many
              ? `<div class="library-toolbar profile-toolbar"><p class="inline-muted">${profiles.length} profiles</p><div class="toolbar-right"><select class="category-select" id="profile-sort" aria-label="Sort profiles">${Object.entries(
                  profileSorts
                )
                  .map(
                    ([value, label]) =>
                      `<option value="${value}" ${(state.profileSort || 'name') === value ? 'selected' : ''}>${label}</option>`
                  )
                  .join(
                    ''
                  )}</select><div class="search-wrap">${icon('search')}<input type="search" class="search-input" id="profile-search" placeholder="Find a profile or mod…" value="${escape(state.profileSearch || '')}" aria-label="Find a profile or mod"></div></div></div>`
              : ''
          }<div class="profile-rows" id="profile-rows">${renderProfileRows()}</div>`
        : `<div class="empty-state">${icon('layers')}<h3>${state.portableProfiles ? 'No profiles saved yet.' : 'Checking your profiles…'}</h3><p>${state.portableProfiles ? 'Switch on the mods you want, then choose Save current mods. A profile keeps its own copies of the mods, so it keeps working even if you remove a mod from your library later.' : 'Reading your saved profiles…'}</p>${button('Save current mods', 'portable-save', 'plus', 'primary')}</div>`
    }${
      legacy.length
        ? `<details class="legacy-profiles mt-24"><summary>Older saved selections · ${legacy.length}</summary><p class="field-note">These older profiles only remember which mods were on. Apply one, then choose Save current mods to turn it into a full profile.</p><div class="profile-rows">${legacy.map((p) => `<article class="profile-row legacy-selection"><span class="profile-row-icon">${icon('clock')}</span><div class="profile-row-main"><h3>${escape(p.name)}</h3><p class="profile-row-mods">${(p.enabled || p.modIds || p.mods || []).map((id) => escape(modName(id))).join(', ') || 'No managed mods'}</p></div><div class="profile-row-actions"><button class="button small" data-profile-apply="${escape(p.name)}">Apply selection</button><button class="icon-button" data-profile-delete="${escape(p.name)}" aria-label="Delete saved selection">${icon('trash')}</button></div></article>`).join('')}</div></details>`
        : ''
    }`;
  }
  function bindProfileRows() {
    $$('[data-portable-rename]').forEach((el) => (el.onclick = () => {
      const profile = state.portableProfiles?.profiles?.find((p) => p.id === el.dataset.portableRename && p.owned);
      if (profile) savePortableProfile(profile);
    }));
    $$('[data-profile-save-backups]').forEach((el) => (el.onclick = () => openProfileSaveBackups(el.dataset.profileSaveBackups)));
    $$('[data-portable-launch]').forEach((el) => (el.onclick = () => reviewPortableProfile(el.dataset.portableLaunch)));
    $$('[data-profile-expand]').forEach(
      (el) =>
        (el.onclick = () => {
          state.openProfiles = state.openProfiles || new Set();
          const name = el.dataset.profileExpand;
          if (state.openProfiles.has(name)) state.openProfiles.delete(name);
          else state.openProfiles.add(name);
          refreshProfileRows();
        })
    );
    $$('[data-profile-clear-search]').forEach(
      (el) =>
        (el.onclick = () => {
          state.profileSearch = '';
          const search = $('#profile-search');
          if (search) search.value = '';
          refreshProfileRows();
        })
    );
    $$('[data-portable-delete]').forEach(
      (el) =>
        (el.onclick = () =>
          confirmModal(
            'Delete this profile?',
            `<p>Delete <strong>${escape(el.dataset.portableDelete)}</strong> and its mod copies? Your mods, saves and other profiles are not touched.</p>`,
            'Delete profile',
            async () => {
              const result = await run('profilesDelete', { name: el.dataset.portableDelete }, { refresh: false });
              if (result) {
                await loadPortableProfiles();
                toast('Profile deleted.');
              }
            },
            true
          ))
    );
  }
  function refreshProfileRows() {
    const rows = $('#profile-rows');
    if (!rows) return render();
    rows.innerHTML = renderProfileRows();
    bindProfileRows();
  }

  // ---- Edition and toolkit updates --------------------------------------------
  // The local edition is the public build while Nexus API access is pending: no
  // sign-in, release checks, in-app downloads or Nexus-hosted pictures.
  function localEdition() {
    return state.snapshot.edition === 'local';
  }
  function localEditionInfo() {
    modal(
      'Download in your browser',
      '<p>Open a mod’s Nexus page in your browser and use <strong>Manual download</strong>. Sign in on the website if it asks. The toolkit does not need your Nexus account or API key.</p><p>Keep the toolkit open to detect completed downloads, or choose <strong>Import mod</strong> and select the file. You can also drop it onto My mods. Drop a PAK onto Workshop to edit a draft instead.</p>',
      `<button class="button" data-modal-close>Close</button><button class="button primary" id="local-open-nexus">${icon('external')}Open Nexus Mods</button>`
    );
    $('#local-open-nexus').onclick = () => run('openNexus', {}, { refresh: false });
  }
  function renderLocalNexusPanel() {
    return `<section class="panel settings-full"><div class="panel-heading"><div><h2>Browser downloads</h2><p>Download on Nexus, then import here.</p></div>${icon('lock')}</div><div class="panel-body"><p>Use Manual download on the mod’s Nexus page. Completed local downloads can be detected by the toolkit, or imported with the button below. Your browser handles website sign-in; no Nexus account connection is needed in the toolkit.</p><div class="inline-actions">${button('Open Nexus Mods', 'nexus', 'external', 'small')}${button('Import mod', 'import', 'plus', 'small')}</div></div></section>`;
  }
  function renderLocalDownloads() {
    return `${header('NEXUS DOWNLOADS', 'Download on Nexus, import here.', 'Use Manual download on a mod’s page, then import the completed file. No toolkit sign-in is needed.', button('Open Nexus page', 'nexus', 'external') + button('Import mod', 'import', 'plus', 'primary'))}${previewNote()}<div class="banner neutral">${icon('info')}<p><strong>Downloads use your browser.</strong> Open a mod’s page and use <strong>Manual download</strong> on a mod’s Files tab, then <strong>Import mod</strong>. The toolkit checks each file before it can be enabled.</p></div><div class="empty-state">${icon('download')}<h3>Import what you downloaded.</h3><p>Choose the PAK file or the ZIP you downloaded. The author’s releases are recognised automatically; other authors’ PAK mods are added as local imports.</p><div class="inline-actions justify-center">${button('Open Nexus page', 'nexus', 'external')}${button('Import mod', 'import', 'plus', 'primary')}</div></div>`;
  }
  function switchValue(selector, fallback) {
    const control = $(selector);
    return control ? control.getAttribute('aria-checked') === 'true' : fallback;
  }
  function renderUpdatePanel() {
    const status = state.update.status || {};
    const version = status.version || state.snapshot.appVersion || '';
    const checkButton = button(state.update.checking ? 'Checking Downloads…' : 'Look for a downloaded update',
      'update-check', 'search', 'small').replace('<button ', '<button ' +
        (state.update.checking ? 'disabled aria-busy="true" ' : ''));
    const onlineStatus = localEdition()
      ? 'Online update notices will be available after Nexus approves the toolkit’s API access. Downloaded update ZIPs are still checked automatically.'
      : state.toolkitRelease.status?.status === 'unavailable'
        ? 'The online check could not finish. It will try again later; downloaded updates still work.'
        : 'Checks for a newer release for your operating system while the toolkit is open. No Nexus sign-in is needed. It checks about every 15 minutes and waits longer if the connection fails.';
    return `<section class="panel settings-full"><div class="panel-heading"><div><h2>Toolkit updates</h2><p>${version ? `You have version ${escape(version)}.` : 'Your installed version.'}</p></div>${icon('download')}</div><div class="panel-body"><p>${
      status.supported === false
        ? escape(status.reason || 'Updates cannot be installed into this copy.')
        : 'When a new version is published, download it from the toolkit’s Nexus page. The toolkit finds the ZIP in your Downloads folder, installs it and restarts. Only updates signed by the author are accepted, and your mods, profiles and settings stay as they are.'
    }</p><div class="setting-row"><div class="setting-copy"><label for="toolkit-updates-setting">Notify me about new toolkit versions</label><p>${escape(onlineStatus)}</p></div><button type="button" id="toolkit-updates-setting" class="switch" role="switch" aria-checked="${state.snapshot.settings.autoCheckToolkitUpdates !== false}" aria-label="Notify me about new toolkit versions"></button></div><div class="inline-actions">${button('What’s new in this version', 'whats-new', 'report', 'small')}${button('Changelog', 'changelog', 'book', 'small')}${checkButton}${button('Install from a file…', 'update-browse', 'plus', 'small')}${button('Open the toolkit’s Nexus page', 'update-page', 'external', 'small')}</div></div></section>`;
  }
  function newerToolkitVersion(next, previous) {
    const pattern = /^(0|[1-9]\d{0,5})\.(0|[1-9]\d{0,5})\.(0|[1-9]\d{0,5})$/;
    if (typeof next !== 'string' || typeof previous !== 'string' || !pattern.test(next) || !pattern.test(previous)) return false;
    const left = next.split('.').map(Number), right = previous.split('.').map(Number);
    return (left[0] - right[0] || left[1] - right[1] || left[2] - right[2]) > 0;
  }
  function renderToolkitReleaseBanner() {
    const notice = state.toolkitRelease.status;
    if (localEdition() || state.snapshot.settings.autoCheckToolkitUpdates === false || !notice?.available ||
        !newerToolkitVersion(notice.version, state.snapshot.appVersion) ||
        notice.version === state.toolkitRelease.dismissedVersion ||
        (state.update.offer && !newerToolkitVersion(notice.version, state.update.offer.version))) return '';
    return `<div class="update-card" role="status"><span class="update-emblem">${icon('download')}</span><div class="update-copy"><span class="update-kicker">New version available</span><strong>Drifter’s Toolkit ${escape(notice.version)} is out</strong><p>Download the ${state.snapshot.platform === 'linux' ? 'Linux' : 'Windows'} ZIP from Nexus. Once it’s in Downloads, the toolkit can check it and offer installation.</p></div><div class="update-actions"><button class="button small" data-action="toolkit-release-dismiss">Not now</button><button class="button small primary" data-action="update-page">${icon('external')}Open download page</button></div></div>`;
  }
  async function checkToolkitRelease(quiet = true) {
    const notice = state.toolkitRelease;
    const platform = state.snapshot.platform, version = state.snapshot.appVersion;
    const current = () => notice === state.toolkitRelease && platform === state.snapshot.platform &&
      version === state.snapshot.appVersion;
    if (state.preview || state.snapshot.sandbox || localEdition() ||
        state.snapshot.settings.autoCheckToolkitUpdates === false || state.busy ||
        state.nativeDropActivity?.running || state.profileSession.active || state.loading ||
        document.hidden || state.nativeWindowHidden || notice.checking || Date.now() < notice.nextCheckAt) return;
    notice.checking = true;
    notice.nextCheckAt = Date.now() + 900000;
    try {
      const response = await request('toolkitUpdateCheck');
      if (!current() || state.snapshot.settings.autoCheckToolkitUpdates === false) return;
      if (!response || typeof response.supported !== 'boolean' || typeof response.available !== 'boolean' ||
          !['pending-registration', 'checking', 'available', 'up-to-date', 'unavailable'].includes(response.status) ||
          (response.available && (response.status !== 'available' || !response.supported ||
            !newerToolkitVersion(response.version, state.snapshot.appVersion)))) return;
      const before = renderToolkitReleaseBanner();
      notice.status = { supported: response.supported, status: response.status,
        available: response.available, version: response.available ? response.version : '' };
      const delay = typeof response.retryAfterMs === 'number' && Number.isFinite(response.retryAfterMs)
        ? Math.min(3660000, Math.max(60000, response.retryAfterMs)) : 900000;
      notice.nextCheckAt = Date.now() + delay;
      if (before !== renderToolkitReleaseBanner()) render();
    } catch {
      // A missed online check never blocks local mod management or signed ZIP updates.
      if (current() && !quiet) toast('The online update check could not finish. It will try again later.', 'warning');
    } finally {
      notice.checking = false;
    }
  }
  function catalogOffer(result) {
    const offer = result?.candidate;
    return offer && typeof offer.token === 'string' && /^[a-f0-9]{64}$/.test(offer.token) &&
      Number.isSafeInteger(offer.revision) && offer.revision > 0 &&
      Number.isSafeInteger(offer.modCount) && offer.modCount >= 0 ? offer : null;
  }
  function renderCatalogPanel() {
    const status = state.catalogUpdate.status || state.snapshot.catalogUpdate || {};
    return `<section class="panel settings-full"><div class="panel-heading"><div><h2>Mod catalog</h2><p>${status.usingPack ? `Catalog revision ${escape(status.revision)}.` : 'Using the catalog included with the toolkit.'}</p></div>${icon('grid')}</div><div class="panel-body"><p>A catalog pack adds new releases to the mod list without replacing the toolkit. Download it from the toolkit’s Nexus page and leave the ZIP in Downloads, or choose it here. It updates the list; mod files are downloaded and imported separately.</p>${status.warning ? `<p class="field-note" role="status">${escape(status.warning)}</p>` : ''}<div class="inline-actions">${button('Look for a catalog download', 'catalog-check', 'search', 'small')}${button('Choose a catalog pack…', 'catalog-browse', 'plus', 'small')}${button('Open the toolkit’s Nexus page', 'update-page', 'external', 'small')}</div></div></section>`;
  }
  function renderCatalogBanner() {
    const offer = state.catalogUpdate.offer;
    if (!offer) return '';
    return `<div class="update-card" role="status"><span class="update-emblem">${icon('grid')}</span><div class="update-copy"><span class="update-kicker">Mod list update</span><strong>Catalog revision ${escape(offer.revision)} is ready</strong><p>${escape(offer.modCount)} releases in the updated catalog.</p>${updateFacts(offer)}</div><div class="update-actions">${button('Not now', 'catalog-dismiss', '', 'small')}${button('Review catalog', 'catalog-review', 'grid', 'small primary')}</div></div>`;
  }
  async function checkForCatalog(quiet = true) {
    const update = state.catalogUpdate;
    if (state.preview || state.busy || state.profileSession.active || update.checking) return;
    const now = Date.now();
    if (quiet && now - update.lastCheck < 300000) return;
    update.lastCheck = now;
    update.checking = true;
    try {
      const result = await request('catalogCheckDownloads');
      update.status = result;
      const offer = catalogOffer(result);
      const changed = (offer?.revision || 0) !== (update.offer?.revision || 0);
      update.offer = offer;
      if (changed || state.view === 'settings') render();
      if (!quiet && !offer) toast('No newer signed catalog was found in your Downloads folder.');
    } catch (error) {
      if (!quiet) toast(errorText(error), 'error');
    } finally {
      update.checking = false;
    }
  }
  function confirmCatalog(offer) {
    confirmModal(
      'Update the mod catalog',
      `<p>Use catalog revision <strong>${escape(offer.revision)}</strong>, with ${escape(offer.modCount)} releases?</p>${updateFacts(offer)}<p>Your installed mods and profiles stay as they are. New releases appear in the list; this does not download or enable them.</p>`,
      'Update catalog',
      async () => {
        state.catalogUpdate.offer = null;
        const result = await run('catalogInstall', { token: offer.token }, { refresh: false, progress: 'Checking the signed catalog…' });
        if (result?.installed) {
          state.catalogUpdate.status = result;
          await refresh({ quiet: true, preserveForm: true });
          toast('Mod catalog updated.');
        }
        render();
      }, false, { contextIndependent: true }
    );
  }
  // "v1.0.3 → v1.0.4": the installed version next to the new one.
  function updateJump(to, from = state.update.status?.version || state.snapshot.appVersion || '') {
    return `<div class="update-jump">${from ? `<span class="old">v${escape(from)}</span><span class="arrow">${icon('arrow')}</span>` : ''}<span class="new">v${escape(to)}</span></div>`;
  }
  function updateFacts(offer) {
    return `<p class="update-facts"><span class="signed">${icon('check')}Signed by the author</span>${Number(offer.bytes) > 0 ? `<span>${formatBytes(Number(offer.bytes))}</span>` : ''}${offer.name ? `<span>${escape(offer.name)}</span>` : ''}</p>`;
  }
  function updateReleaseNotes(offer) {
    const notes = offer?.releaseNotes;
    if (!notes || notes.version !== offer.version || !Array.isArray(notes.items)) return '';
    const items = notes.items.filter((item) => typeof item === 'string').slice(0, 20);
    return items.length ? `<section class="update-highlights"><h3>What’s new</h3><ul>${items.map((item) => `<li>${escape(item)}</li>`).join('')}</ul></section>` : '';
  }
  function renderUpdateBanner() {
    const offer = state.update.offer;
    if (!offer) return '';
    return `<div class="update-card" role="status"><span class="update-emblem">${icon('download')}</span><div class="update-copy"><span class="update-kicker">Update ready</span><strong>Drifter’s Toolkit ${escape(offer.version)} is ready to install</strong>${updateJump(offer.version)}${updateFacts(offer)}</div><div class="update-actions"><button class="button small" data-action="update-page">${icon('external')}Changelog</button><button class="button small" data-action="update-dismiss">Not now</button><button class="button small primary" data-action="update-install">${icon('download')}Install and restart</button></div></div>`;
  }
  async function checkForUpdate(quiet = true) {
    if (state.preview || state.snapshot.sandbox) return;
    const update = state.update, context = state.snapshot.accountContext;
    const current = () => update === state.update && context === state.snapshot.accountContext;
    if (update.checking || (quiet && (state.busy || state.nativeDropActivity?.running ||
        state.profileSession.active || document.hidden || state.nativeWindowHidden || $('#modal')?.open))) return;
    const now = Date.now();
    if (quiet ? now - update.lastCheck < 300000 : now < (update.manualRetryAt || 0)) return;
    update.lastCheck = now;
    if (!quiet) update.manualRetryAt = now + 2000;
    update.checking = true;
    const syncButton = () => {
      if (!current()) return;
      for (const control of $$('[data-action="update-check"]')) {
        control.disabled = update.checking;
        control.setAttribute('aria-busy', String(update.checking));
      }
    };
    syncButton();
    try {
      const found = await request('updateCheckDownloads');
      if (!current()) return;
      const offer =
        found && found.found && typeof found.token === 'string' && /^\d+\.\d+\.\d+$/.test(found.version) ? found : null;
      const display = (value) => JSON.stringify(value ? [value.version, value.name, value.bytes, value.releaseNotes] : null);
      const changed = display(offer) !== display(update.offer);
      update.offer = offer;
      if (changed) render();
      if (!quiet && !offer) toast('No newer Drifter’s Toolkit download was found in your Downloads folder.');
    } catch (error) {
      if (current() && !quiet) toast(errorText(error), 'error');
    } finally {
      update.checking = false;
      syncButton();
    }
  }
  // One window: it starts as a summary of the update and turns into live progress once Install is pressed.
  // Native checks precede every replacement; the restart follows a successful install.
  const updateSteps = [
    ['verify', 'Check the release', 'Confirms the author’s signature, operating system and version.'],
    ['unpack', 'Unpack and check every file', 'Each file must match its signed checksum.'],
    ['preflight', 'Check this installation', 'Checks free space and existing update files before replacing anything.'],
    ['swap', 'Replace the program files', 'The old files stay until the new version has started.'],
    ['restart', 'Start the new version', 'Your mods, profiles, settings and saves stay as they are.']
  ];
  let updateInstalling = false, activeUpdateInstallation = null;
  function confirmUpdate(offer) {
    modal(
      `Update to ${offer.version}`,
      `<div class="updater" id="updater"><div class="updater-hero"><span class="updater-emblem">${icon('download')}</span>${updateJump(offer.version)}${updateFacts({ ...offer, name: '' })}</div>${updateReleaseNotes(offer)}<ol class="updater-steps">${updateSteps.map(([id, title, note], index) => `<li data-step="${id}"><span class="step-mark"><span class="step-number">${index + 1}</span>${icon('check')}</span><div><strong>${title}</strong><small>${note}</small></div></li>`).join('')}</ol><progress class="updater-progress" id="updater-progress" aria-label="Toolkit update progress" max="100" value="0"></progress><p class="updater-status" id="updater-status" aria-live="polite">The toolkit closes for a moment and starts again with version ${escape(offer.version)}. The update is signed by the author, and every file is checked before anything is replaced.</p></div>`,
      `<button class="button" data-modal-close id="update-not-now">Not now</button><button class="button primary" id="update-install-confirm">${icon('download')}Install and restart</button>`
    );
    const generation = modalContentGeneration, dialog = $('#modal'), confirm = $('#update-install-confirm'),
      updateState = state.update, shownOffer = state.update.offer?.token === offer.token ? state.update.offer : null,
      context = JSON.stringify([state.snapshot.accountContext || '', state.snapshot.settings?.gamePath || '', state.snapshot.activePath || '']);
    const reviewedOffer = { ...offer };
    const review = { generation, dialog, confirm, updateState, used: false, finished: false,
      visible: () => dialog.open && generation === modalContentGeneration && $('#update-install-confirm') === confirm,
      current: () => review.visible() && state.update === updateState &&
        (!shownOffer || state.update.offer === shownOffer && state.update.offer.token === reviewedOffer.token) &&
        context === JSON.stringify([state.snapshot.accountContext || '', state.snapshot.settings?.gamePath || '', state.snapshot.activePath || '']) };
    confirm.onclick = () => installUpdate(reviewedOffer, review);
  }
  function showUpdateStep(step, fraction = 0) {
    const order = updateSteps.map(([id]) => id),
      index = order.indexOf(step);
    $$('#updater [data-step]').forEach((item) => {
      const at = order.indexOf(item.dataset.step);
      item.classList.toggle('done', at < index || (step === 'restart' && fraction >= 1 && at === index));
      item.classList.toggle('active', at === index);
    });
    const ranges = { verify: [0, 10], unpack: [10, 70], preflight: [70, 80], swap: [80, 95], restart: [95, 100] },
      bar = $('#updater-progress');
    if (bar && ranges[step]) {
      const [from, to] = ranges[step];
      bar.value = Math.round(from + (to - from) * Math.min(1, Math.max(0, Number(fraction) || 0)));
    }
  }
  function handleUpdateProgress(data) {
    if (!updateInstalling || !activeUpdateInstallation?.visible() || activeUpdateInstallation.finished ||
        !data || typeof data.step !== 'string') return;
    const fraction = Number(data.total) > 0 ? Number(data.done) / Number(data.total) : 1,
      status = $('#updater-status');
    if (data.step === 'verify') showUpdateStep('unpack', 0);
    else if (data.step === 'unpack') showUpdateStep('unpack', fraction);
    else if (data.step === 'preflight') showUpdateStep('preflight', fraction);
    else if (data.step === 'swap') showUpdateStep('swap', fraction);
    else return;
    if (status)
      status.textContent =
        data.step === 'verify'
          ? 'Signature checked. Unpacking the files…'
          : data.step === 'unpack'
            ? `Unpacking and checking every file… ${Math.round(fraction * 100)}%`
            : data.step === 'preflight'
              ? (fraction >= 1 ? 'Installation checks passed. Preparing the replacement…' : 'Checking free space and existing update files…')
              : 'Replacing the program files…';
  }
  async function installUpdate(offer, review) {
    if (!review?.visible() || review.used || updateInstalling || state.busy) return false;
    if (!review.current()) {
      const status = $('#updater-status');
      if (status) status.textContent = 'This update review is no longer current. Close it and review the update again.';
      review.confirm.disabled = true;
      return false;
    }
    review.used = true;
    updateInstalling = true;
    activeUpdateInstallation = review;
    state.busy = true;
    const dialog = $('#modal'),
      confirm = $('#update-install-confirm'),
      later = $('#update-not-now'),
      status = $('#updater-status'),
      holdOpen = (event) => event.preventDefault();
    // While files are replaced the window cannot be closed; Escape and the close button wait.
    dialog?.addEventListener?.('cancel', holdOpen);
    $$('#modal [data-modal-close]').forEach((button) => (button.disabled = true));
    $('#updater')?.classList.add('installing');
    if (confirm) {
      confirm.disabled = true;
      confirm.innerHTML = `${icon('refresh')}Installing…`;
    }
    showUpdateStep('verify', 0);
    if (status) status.textContent = 'Checking the signature, operating system and version…';
    let result = null;
    try {
      result = await request('updateInstall', { token: offer.token }, null, true);
    } catch (error) {
      updateInstalling = false;
      if (activeUpdateInstallation === review) activeUpdateInstallation = null;
      state.busy = false;
      dialog?.removeEventListener?.('cancel', holdOpen);
      if (!review.visible()) return false;
      $$('#modal [data-modal-close]').forEach((button) => (button.disabled = false));
      $$('#updater [data-step].active').forEach((item) => item.classList.add('failed'));
      $('#updater')?.classList.add('failed');
      if (status)
        status.textContent = `The update stopped: ${errorText(error)} If recovery needs a restart, reopen the toolkit before retrying.`;
      if (confirm) confirm.hidden = true;
      if (later) later.textContent = 'Close';
      return false;
    }
    state.busy = false;
    review.finished = true;
    if (state.update === review.updateState && state.update.offer?.token === offer.token) state.update.offer = null;
    if (!review.visible()) return true;
    showUpdateStep('restart', 0.5);
    $('#updater')?.classList.add('finished');
    if (status) status.textContent = `Version ${result?.version || offer.version} is installed. Starting it now…`;
    if (confirm) confirm.innerHTML = `${icon('refresh')}Restarting…`;
    return true;
  }
  // After the restart: one "Updated" window that also carries What's new (never both).
  function showUpdated(from, to) {
    state.justUpdated = { from, to };
    storeValue('drifter.whatsNewSeen', to);
    state.noticeQueue = state.noticeQueue.filter(
      (notice) => !(notice.type === 'whats-new' && notice.id === 'whats-new-' + to)
    );
    if (state.busy || $('#modal')?.open)
      enqueueNotice({ type: 'updated', id: 'updated-' + to, fingerprint: 'updated-' + to });
    else updatedModal();
  }
  function updatedModal() {
    const { from = '', to = '' } = state.justUpdated || {};
    const notes =
      window.WHATS_NEW?.version === to && Array.isArray(window.WHATS_NEW.items) ? window.WHATS_NEW.items : [];
    modal(
      `Updated to ${to}`,
      `<div class="updater updated"><div class="updater-hero"><span class="updater-emblem done">${icon('check')}</span>${updateJump(to, from)}<p class="update-facts"><span class="signed">${icon('check')}Installed and checked</span></p></div>${notes.length ? `<h3 class="updated-title">What’s new</h3><ul class="whats-new">${notes.map((item) => `<li>${escape(item)}</li>`).join('')}</ul>` : ''}<p class="field-note">Your mods, profiles, settings and saves stayed as they were. What’s new is always under the version number in the sidebar.</p></div>`,
      `<button class="button" id="updated-changelog">${icon('book')}All versions</button><button class="button primary" data-modal-close>Let’s go</button>`
    );
    $('#updated-changelog').onclick = () => showChangelog(to, updatedModal);
    return true;
  }

  // ---- Guided bad-mod finder ----------------------------------------------------
  // The native core owns every step (finderStart / finderAnswer / finderFinish) and
  // switches mods only through its journaled package transaction; this is the view.
  function finderState() {
    return state.snapshot.finder || { active: false };
  }
  function finderTestingText(finder) {
    const testing = Array.isArray(finder.testing) ? finder.testing : [];
    const total = Number(finder.originalCount) || 0;
    return testing.length
      ? `${testing.length} of your ${total} mod${total === 1 ? '' : 's'} ${testing.length === 1 ? 'is' : 'are'} enabled for this round.`
      : 'Every mod managed by the toolkit is disabled for this round.';
  }
  function finderNamesList(items) {
    const list = Array.isArray(items) ? items : [];
    return list.length
      ? `<ul class="finder-mods">${list.map((item) => `<li>${escape(typeof item?.name === 'string' ? item.name : modName(item?.id))}</li>`).join('')}</ul>`
      : '';
  }
  function unmanagedPackages() {
    return (state.snapshot.installed || []).filter((entry) => !entry.owned);
  }
  function renderFinderBanner() {
    const finder = finderState();
    if (!finder.active) return '';
    const done = ['found', 'inconclusive', 'not-mods'].includes(finder.stage);
    const text = finder.damaged
      ? 'The finder record is damaged. Clear it, then check which mods are enabled.'
      : finder.canContinue === false
        ? 'Some original packages are unavailable. Review the restore options or stop without restoring.'
      : done
        ? 'The finder has a result. Open it to choose which mods stay enabled.'
        : `Round ${Number(finder.round) || 1}: ${finderTestingText(finder)} Test in the game, then answer.`;
    return `<div class="banner neutral finder-banner" role="status">${icon('search')}<div><strong>Finding the mod causing a problem</strong><p>${escape(text)}</p></div><button class="button small primary" data-action="finder-open">${done || finder.damaged ? 'Show result' : 'Continue'}</button><button class="button small" data-action="finder-stop">Stop finder…</button></div>`;
  }
  function openFinder() {
    const finder = finderState();
    if (!finder.active) {
      const enabled = state.snapshot.mods.filter((m) => m.enabled);
      if (!enabled.length) {
        modal(
          'Find the mod causing a problem',
          '<p>Enable the mods you normally play with first. The finder switches the mods that Drifter’s Toolkit manages off and on to narrow down which one causes a crash or another problem.</p>',
          '<button class="button primary" data-modal-close>OK</button>'
        );
        return;
      }
      const rounds = 1 + Math.ceil(Math.log2(Math.max(1, enabled.length)));
      const unmanaged = unmanagedPackages();
      modal(
        'Find the mod causing a problem',
        `<p>The finder narrows down which of your ${enabled.length} enabled mod${enabled.length === 1 ? '' : 's'} causes a crash or another problem by switching them off and on in rounds.</p><ol class="finder-steps"><li>Each round, the toolkit enables a different group of your mods.</li><li>You start Days Gone and do what usually causes the problem.</li><li>You close the game and answer whether it still happened.</li></ol><p>This takes about ${rounds} round${rounds === 1 ? '' : 's'}. The first round turns all of them off, to check whether the problem comes from these mods at all.</p><p>When you stop, choose whether to restore the available original packages or keep the current test setup. Missing or changed library files are reported instead of keeping the finder locked.</p>${
          unmanaged.length
            ? `<div class="banner neutral">${icon('info')}<p><strong>${unmanaged.length} PAK file${unmanaged.length === 1 ? ' is' : 's are'} not managed by the toolkit and stay${unmanaged.length === 1 ? 's' : ''} active.</strong> If the finder does not find the cause, check ${unmanaged.length === 1 ? 'that file' : 'those files'} too.</p></div>`
            : ''
        }`,
        `<button class="button" data-modal-close>Cancel</button><button class="button primary" id="finder-start">${icon('search')}Start round 1</button>`
      );
      $('#finder-start').onclick = async () => {
        $('#modal').close();
        const result = await run('finderStart', {}, { progress: 'Switching off the managed mods for round 1…' });
        if (result) openFinder();
      };
      return;
    }
    if (finder.canContinue === false && !finder.damaged && !['found', 'inconclusive', 'not-mods'].includes(finder.stage)) { reviewFinderStop(); return; }
    if (finder.damaged) {
      modal(
        'The finder record is damaged',
        '<p>The toolkit cannot tell which mods were enabled before the finder started. Clear the finder, then check your mods and enable the ones you want.</p>',
        '<button class="button" data-modal-close>Not now</button><button class="button primary" id="finder-clear">Clear the finder</button>'
      );
      $('#finder-clear').onclick = () => reviewFinderKeepCurrent();
      return;
    }
    if (finder.stage === 'found') {
      const name = finder.culprit?.name || modName(finder.culprit?.id);
      modal(
        'Found it',
        `<p class="crash-headline">${escape(name)} causes the problem.</p><p>The problem happened with only this managed mod enabled and stopped without it. Check its page for a newer version or known issues, and tell its author what happens.</p><p>You can leave it disabled and restore the available original packages, or review how to stop the finder.</p>`,
        `<button class="button" id="finder-restore-all">Restore available mods</button><button class="button primary" id="finder-keep-off">${icon('check')}Keep it disabled</button>`
      );
      $('#finder-keep-off').onclick = () => finishFinder(true);
      $('#finder-restore-all').onclick = () => finishFinder(false);
      return;
    }
    if (finder.stage === 'inconclusive' || finder.stage === 'not-mods') {
      const unmanaged = unmanagedPackages();
      const body =
        finder.stage === 'not-mods'
          ? `<p class="crash-headline">Your managed mods are not the cause.</p><p>The problem still happened with every mod managed by the toolkit switched off.</p>${
              unmanaged.length
                ? `<p>These PAK files are not managed by the toolkit and stayed active during the test. Check them next:</p><ul class="finder-mods">${unmanaged.map((entry) => `<li>${escape(entry.name || entry.file || 'PAK file')}</li>`).join('')}</ul>`
                : '<p>Next, verify the game files in Steam and check overlays such as ReShade or recording tools and your graphics driver.</p>'
            }`
          : `<p class="crash-headline">No single mod causes it.</p><p>The problem only happens when some of these mods are enabled together:</p>${finderNamesList(finder.combination)}<p>Try them in pairs, or check whether they change the same game files.</p>`;
      modal(
        'Finder result',
        body,
        '<button class="button primary" id="finder-restore-all">Restore available mods</button>'
      );
      $('#finder-restore-all').onclick = () => finishFinder(false);
      return;
    }
    const left = Math.max(1, Number(finder.roundsLeft) || 1);
    modal(
      `Round ${Number(finder.round) || 1} · about ${left} to go`,
      `<p>${escape(finderTestingText(finder))}</p>${finderNamesList(finder.testing)}<ol class="finder-steps"><li>Start Days Gone.</li><li>Do what usually causes the problem, for example load the same save or go to the same place.</li><li>Close the game, then answer below.</li></ol>`,
      `<button class="button" id="finder-stop">Stop and restore</button><button class="button" id="finder-launch">${icon('play')}Launch game</button><button class="button" id="finder-gone">${icon('check')}Problem is gone</button><button class="button primary" id="finder-still">${icon('alert')}Problem still happens</button>`
    );
    $('#finder-launch').onclick = () => launchOrReviewRestart();
    $('#finder-stop').onclick = () => reviewFinderStop();
    $('#finder-gone').onclick = () => answerFinder(false);
    $('#finder-still').onclick = () => answerFinder(true);
  }
  async function answerFinder(problem) {
    if (finderState().canContinue === false) { reviewFinderStop(); return false; }
    $('#modal').close();
    const result = await run('finderAnswer', { problem }, { progress: 'Switching mods for the next round…' });
    if (result) openFinder();
  }
  function finderContext() {
    return JSON.stringify([state.snapshot.accountContext || '', state.snapshot.settings?.gamePath || '', state.snapshot.activePath || '']);
  }
  function finderUnavailable(finder = finderState()) {
    return (Array.isArray(finder.unavailable) ? finder.unavailable : []).filter((item) => item && typeof item.id === 'string' && typeof item.name === 'string').slice(0, 128);
  }
  function renderFinderUnavailable(items) {
    return items.length ? `<ul class="finder-mods">${items.map((item) => `<li><strong>${escape(item.name)}</strong>${typeof item.reason === 'string' ? `<small>${escape(item.reason)}</small>` : ''}</li>`).join('')}</ul>` : '';
  }
  function reviewFinderKeepCurrent() {
    const context = finderContext(), fingerprint = JSON.stringify(finderState());
    if (!finderState().active || state.busy || finderState().canKeepCurrent === false) return false;
    confirmModal('Stop without restoring?', '<p>End the finder and keep the PAK files exactly as they are now. The PAKs currently enabled stay enabled; stopping does not restore earlier selections.</p><p>This only clears the finder record. It does not restore, remove or change a PAK. You can manage your mods and profiles normally afterward.</p>', 'Stop without restoring', async () => {
      if (context !== finderContext() || fingerprint !== JSON.stringify(finderState()) || !finderState().active) {
        toast('The finder changed. Review its current state before stopping.', 'warning'); return false;
      }
      return finishFinder(false, true);
    });
    return true;
  }
  function reviewFinderStop() {
    const finder = finderState(), context = finderContext(), fingerprint = JSON.stringify(finder), unavailable = finderUnavailable(finder);
    if (!finder.active || state.busy) return false;
    modal('Stop the mod finder', `<div class="finder-stop-review"><p>Restore the original packages that can still be verified, or stop without changing the current test setup.</p>${unavailable.length ? `<div class="banner warning"><p><strong>${unavailable.length} original package${unavailable.length === 1 ? ' is' : 's are'} unavailable.</strong> These cannot be restored from the current library. Other installed files stay protected.</p></div>${renderFinderUnavailable(unavailable)}` : '<p>Restoring checks the current package files again. Missing or changed sources are reported; unrelated installed mods stay in place.</p>'}<p class="field-note">Stop without restoring only clears the finder record. It does not switch or delete any mods.</p></div>`, `<button type="button" class="button" data-modal-close>Cancel</button><button type="button" class="button" id="finder-stop-keep">Stop without restoring…</button><button type="button" class="button primary" id="finder-stop-restore" ${state.snapshot.gameRunning !== false ? 'disabled' : ''}>Restore available mods</button>`);
    const generation = modalContentGeneration;
    const current = () => context === finderContext() && fingerprint === JSON.stringify(finderState()) && generation === modalContentGeneration && finderState().active && !state.busy;
    $('#finder-stop-keep').onclick = () => current() ? reviewFinderKeepCurrent() : false;
    $('#finder-stop-restore').onclick = () => current() && state.snapshot.gameRunning === false ? finishFinder(false) : false;
    return true;
  }
  async function finishFinder(disableCulprit, keepCurrent = false) {
    const context = finderContext(), view = state.view;
    if ($('#modal').open) $('#modal').close();
    const generation = modalContentGeneration;
    const result = await run('finderFinish', keepCurrent ? { keepCurrent: true, confirmed: true } : { disableCulprit }, {
      progress: keepCurrent ? 'Stopping the finder without changing PAK files…' : 'Restoring the available original mods…',
      success: (value) => value?.finderResult?.keptCurrent ? 'Finder stopped. Your current PAK files were left in place.'
        : value?.finderResult?.skipped?.length ? 'Finder stopped. Some original packages could not be restored; check the details.'
          : disableCulprit ? 'Finder finished. The available original mods were restored; the suspected mod stays off.'
            : 'Finder stopped. The available original mods were restored.'
    });
    if (!result || context !== finderContext()) return result;
    const skipped = finderUnavailable({ unavailable: result.finderResult?.skipped });
    if (skipped.length && view === state.view && generation === modalContentGeneration)
      modal('Finder stopped with some packages unavailable', `<p>The finder is no longer blocking your setup. These original packages could not be restored from verified library copies:</p>${renderFinderUnavailable(skipped)}<p>Restore their library copies from the original downloads if you need them later. Check the Enabled list for their current state. Changed and unrelated installed files were left in place.</p>`);
    return result;
  }
  function renderCrashMods(activeMods) {
    const text = (value) => (typeof value === 'string' ? value : '');
    const title = '<h3 class="crash-advice-title">Mods during this crash</h3>';
    if (activeMods.quality !== 'launch' && activeMods.quality !== 'recorded-before')
      return `${title}<p class="field-note">Not recorded. Keep Drifter’s Toolkit open while you play so it can record which mods were active.</p>`;
    const packages = Array.isArray(activeMods.packages) ? activeMods.packages : [];
    const when =
      activeMods.quality === 'launch'
        ? `when this game session started (${logTime(activeMods.launchStartedAt)})`
        : `as last recorded before this crash (${logTime(activeMods.setRecordedAt)})`;
    const names = (list) =>
      Array.isArray(list)
        ? list
            .filter((name) => typeof name === 'string')
            .map((name) => escape(name))
            .join(', ')
        : '';
    const comparison = activeMods.comparison;
    let compare = '';
    if (comparison && typeof comparison === 'object') {
      const base =
        comparison.against === 'clean-run'
          ? `the last run without a crash (${logTime(comparison.at)})`
          : `the previous setup (${logTime(comparison.at)})`;
      compare = comparison.unchanged
        ? `<p>No mods were added, removed or updated since ${escape(base)}, so a recent mod change is unlikely to be the cause.</p>`
        : `<p>Changed since ${escape(base)}:</p><ul class="crash-advice">${comparison.added?.length ? `<li><strong>Added:</strong> ${names(comparison.added)}</li>` : ''}${comparison.removed?.length ? `<li><strong>Removed:</strong> ${names(comparison.removed)}</li>` : ''}${comparison.updated?.length ? `<li><strong>Updated:</strong> ${names(comparison.updated)}</li>` : ''}</ul>`;
    }
    return `${title}<p>${
      packages.length
        ? `${packages.length} mod file${packages.length === 1 ? ' was' : 's were'} active ${escape(when)}.`
        : `No mod PAK files were active ${escape(when)}.`
    }${activeMods.quality === 'recorded-before' ? ' The start of this game session itself was not recorded, so changes made while the toolkit was closed would not show here.' : ''}</p>${
      packages.length
        ? `<details class="finding-evidence" open><summary>Active mod files · ${packages.length}</summary><ul>${packages.map((p) => `<li>${escape(text(p.name))}${p.managed ? '' : ' <span class="badge">not managed by the toolkit</span>'}</li>`).join('')}</ul></details>`
        : ''
    }${compare}`;
  }

  let restartNoticeNumber = 0,
    lastRestartFingerprint = '';
  function renderGameRestartBanner() {
    const restart = state.gameRestart;
    if (!restart.pending) return '';
    return `<div class="banner neutral game-restart-banner" role="status">${icon('clock')}<div><strong>Restart queued</strong><p>${escape(restart.message || 'Close Days Gone normally when you are ready. The toolkit will check that it has closed before starting it again.')}</p><p>The toolkit will not force the game to close. This request expires after 15 minutes.</p></div><button class="button small" data-action="restart-cancel">Cancel restart</button></div>`;
  }

  let launchDecisionRunning = false;
  async function launchOrReviewRestart() {
    if (state.busy || launchDecisionRunning) return false;
    launchDecisionRunning = true;
    try {
      const refreshed = await refresh({ quiet: true, preserveForm: true, background: true });
      if (!refreshed) {
        toast('The game status could not be checked. Refresh your library before launching.', 'warning');
        return false;
      }
      if (state.gameRestart.pending) {
        toast('A restart is already queued. Cancel it before starting another launch.', 'warning');
        return false;
      }
      if (state.profileSession.active && !state.snapshot.gameRunning) {
        navigate('profiles');
        toast(
          'The game has closed, but its profile session still needs attention. Review restoration before launching again.',
          'warning'
        );
        return false;
      }
      if (!state.snapshot.settings.gamePath) {
        navigate('folders');
        toast('Choose your Days Gone installation before launching.', 'warning');
        return false;
      }
      if (state.snapshot.gameRunning) {
        reviewGameRestart();
        return true;
      }
      return !!(await run('launchGame', {}, { refresh: false }));
    } catch {
      toast('The game status could not be checked. Review Diagnostics before launching.', 'warning');
      return false;
    } finally {
      launchDecisionRunning = false;
    }
  }
  function reviewGameRestart() {
    modal(
      'Restart after closing Days Gone?',
      `<p>Close the game normally when you are ready. The toolkit will wait for it to finish, check the selected installation again and launch it once. It will not stop the game or repeat a mod change.</p><div class="startup-path"><span>Selected installation</span><code>${escape(state.snapshot.settings.gamePath || 'No game selected')}</code></div>${state.profileSession.active ? '<p><strong>A profile session is active.</strong> Its original files must finish restoring first. The toolkit will then show a fresh profile launch review; it will not reuse the previous file plan.</p>' : ''}<p class="field-note">Keep the toolkit open. The queued request expires in 15 minutes and is cancelled if the selected installation changes or its state cannot be verified.</p>`,
      '<button class="button" data-modal-close>Cancel</button><button id="restart-queue-confirm" class="button primary">' +
        icon('refresh') +
        'Queue restart</button>'
    );
    $('#restart-queue-confirm').onclick = async () => {
      $('#modal').close();
      const status = await run(
        'gameRestartQueue',
        { confirmed: true },
        { refresh: false, progress: 'Checking the selected game before queuing a restart…' }
      );
      if (status) {
        if (status.pending && !state.gameRestart.pending) restartNoticeNumber++;
        state.gameRestart = status;
        render();
      }
    };
  }
  function handleGameRestartEvent(result) {
    const data = result.data;
    if (!data || typeof data !== 'object') return;
    const fingerprint = JSON.stringify([data.pending, data.phase, data.message, data.profileName]);
    if (fingerprint === lastRestartFingerprint) return;
    lastRestartFingerprint = fingerprint;
    if (data.pending && !state.gameRestart.pending) restartNoticeNumber++;
    state.gameRestart = data;
    if (data.phase === 'profile-review-required' && data.profileName)
      enqueueNotice({
        type: 'restart-profile',
        id: 'restart-profile',
        profileName: data.profileName,
        message:
          'The game closed and original files were restored. Review the profile setup before launching it again.',
        fingerprint: `restart-profile:${restartNoticeNumber}:${data.profileName}`
      });
    else if (!data.pending && data.phase !== 'idle' && data.message)
      toast(data.message, ['failed', 'expired', 'blocked'].includes(data.phase) ? 'warning' : 'success');
    if (state.view !== 'settings') render();
    else {
      const banner = $('.game-restart-banner');
      if (banner && !data.pending) banner.remove();
    }
    scheduleNoticeDelivery();
  }
  function sessionBlocksAction(action) {
    return (
      !!state.profileSession.active &&
      new Set([
        'nexusDownload',
        'confirmReviewedImport',
        'restoreLibraryCopy',
        'enable',
        'disable',
        'remove',
        'browseImport',
        'browseImports',
        'browseImportFolder',
        'importDropped',
        'batchImportConfirm',
        'libraryBackupRestore',
        'libraryBackupRecover',
        'import',
        'reviewExternalRemoval',
        'reviewExternalAdoption',
        'releaseManaged',
        'removeExternal',
        'reviewForgetInstallation',
        'forgetInstallation',
        'saveProfile',
        'applyProfile',
        'deleteProfile',
        'profilesSave',
        'profilesRename',
        'profilesDelete',
        'directoriesPrepare',
        'directoriesRestore',
        'profileLaunch',
        'saveSettings',
        'browseGame'
      ]).has(action)
    );
  }
  function renderProfileSessionBanner() {
    const session = state.profileSession;
    if (!session.active) return '';
    return `<div class="banner warning profile-session-banner" role="status">${icon('layers')}<div><strong>Profile session: ${escape(session.profileName || 'active setup')}</strong><p>${escape(session.message || `Status: ${String(session.status || 'active').replace(/[-_]/g, ' ')}. Package changes are paused until the game closes and the original files are restored.`)}</p><p>Keep the toolkit open until restoration finishes. If it was interrupted, open it again before changing mods.</p></div><div class="inline-actions"><button class="button small" data-action="portable-refresh">Check status</button>${session.canRestore ? '<button class="button small" data-action="profile-restore">Restore original files</button>' : ''}</div></div>`;
  }
  function applyExternalLocks() {
    $$('[data-external-remove],[data-external-manage]').forEach((el) => {
      const adoption = 'externalManage' in el.dataset,
        id = adoption ? el.dataset.externalManage : el.dataset.externalRemove,
        file = externalMods().find((m) => String(m.id) === id);
      const reason = externalChangeReason(file),
        eligible = adoption ? file?.canReviewAdoption === true : file?.canRemoveExternal === true;
      el.disabled = state.busy || !file || !eligible || !!reason;
      el.title = state.busy
        ? 'Wait for the current request to finish.'
        : reason ||
          (adoption ? file?.adoptionReason : file?.externalRemovalReason) ||
          (adoption
            ? 'Inspect this installed package for local management'
            : 'Review this exact installed file before removal');
    });
  }
  function applyRecordLocks() {
    $$('[data-record-review]').forEach((el) => {
      const mod = getMod(el.dataset.recordReview),
        reason = installationRecordReason(mod);
      el.disabled = state.busy || !brokenInstallationRecord(mod) || mod?.canForgetInstallation !== true || !!reason;
      el.title = state.busy
        ? 'Wait for the current request to finish.'
        : reason || 'Review the old record and the current file before forgetting ownership';
    });
  }
  function bindRecordReviewControls() {
    $$('[data-record-review]').forEach(
      (el) =>
        (el.onclick = () => {
          $('#modal').close();
          closeModDetails(false);
          reviewForgetInstallation(el.dataset.recordReview);
        })
    );
    applyRecordLocks();
  }
  function applySessionLocks() {
    applyExternalLocks();
    applyRecordLocks();
    applyModActionLocks();
    if (!state.profileSession.active) return;
    $$(
      '[data-toggle],[data-remove-mod],[data-profile-apply],[data-profile-delete],[data-portable-launch],[data-portable-delete],[data-portable-rename],[data-action="import"],[data-action="import-folder"],[data-action="library-backup-restore"],[data-action="portable-save"],[data-action="directories-prepare"],[data-action="directories-restore"],#game-path,[data-action="browse-game"]'
    ).forEach((el) => {
      el.disabled = true;
      el.title = 'Wait for the profile session to finish restoring original files.';
    });
  }
  let profileReadGeneration = 0;
  async function loadPortableProfiles() {
    if (state.profilesLoading) return;
    const context = state.snapshot.accountContext;
    const generation = ++profileReadGeneration;
    state.profilesLoading = true;
    try {
      const result = await request('profilesList');
      if (context !== state.snapshot.accountContext || generation !== profileReadGeneration) return null;
      state.portableProfiles = result;
      state.profileSession = result.session || state.profileSession;
      if (state.view === 'profiles') render();
      else applySessionLocks();
      return result;
    } catch (error) {
      if (context === state.snapshot.accountContext && generation === profileReadGeneration) toast(errorText(error), 'error');
      return null;
    } finally {
      if (context === state.snapshot.accountContext && generation === profileReadGeneration) state.profilesLoading = false;
    }
  }
  function portableProfileNameProblem(name, exceptId = '') {
    if (!name) return 'Enter a profile name.';
    if (/[<>:"/\\|?*\u0000-\u001f]/.test(name) || /[. ]$/.test(name) || name === '.' || name === '..')
      return 'Use a name without filename symbols or a dot at the end.';
    if (new TextEncoder().encode(name).length > 64) return 'This name is too long. Shorten it and try again.';
    if ((state.portableProfiles?.profiles || []).some((p) => p.id !== exceptId && String(p.name).toLowerCase() === name.toLowerCase()))
      return 'A profile with this name already exists. Choose a different name.';
    return '';
  }
  function savePortableProfile(profile = null) {
    const renaming = !!profile, context = state.snapshot.accountContext;
    modal(
      renaming ? 'Rename profile' : 'Save a portable profile',
      `${renaming ? '<p>Change the name of this saved setup. Its mod copies and linked save backups stay with the same profile.</p>' : `<p>Keep your current mod setup, or start a blank profile without switching your current mods off.</p><label class="field-label" for="portable-profile-source">Mods to include</label><select id="portable-profile-source" class="input full-width"><option value="current">Current enabled mods (${enabledMods().length})</option><option value="empty">Blank profile (no mods)</option></select><p class="field-note">Current mods includes copies of enabled managed PAKs. Saves and PAKs installed by hand are excluded. A blank profile launches without mods for that session. To move a profile to another PC, copy its whole folder.</p>`}<label class="field-label" for="portable-profile-name">Profile name</label><input id="portable-profile-name" class="input full-width" maxlength="64" placeholder="e.g. First playthrough" autocomplete="off" aria-describedby="portable-profile-error"><p id="portable-profile-error" class="field-note field-error" role="alert" hidden></p>`,
      `<button type="button" class="button" data-modal-close>Cancel</button><button type="button" id="portable-profile-save" class="button primary">${renaming ? 'Save name' : 'Create profile'}</button>`
    );
    const nameField = $('#portable-profile-name'), submit = $('#portable-profile-save'), source = $('#portable-profile-source'), errorField = $('#portable-profile-error');
    nameField.value = profile?.name || '';
    nameField.focus();
    let saving = false;
    const showError = (message) => {
      errorField.textContent = message;
      errorField.hidden = !message;
      nameField.setAttribute('aria-invalid', message ? 'true' : 'false');
      if (message && $('#portable-profile-name') === nameField && $('#modal').open)
        errorField.scrollIntoView?.({ block: 'nearest', behavior: 'instant' });
    };
    nameField.oninput = () => showError('');
    submit.onclick = async () => {
      if (saving) return;
      if (context !== state.snapshot.accountContext) {
        showError('The selected installation changed. Close this dialog and reopen it for the current installation.');
        nameField.focus();
        return;
      }
      const name = nameField.value.trim(), problem = portableProfileNameProblem(name, profile?.id);
      if (problem) {
        showError(problem);
        nameField.focus();
        return;
      }
      if (state.busy || state.nativeDropActivity?.running) {
        showError('Wait for the current action to finish, then try again.');
        return;
      }
      if (state.profileSession.active) {
        showError('Close Days Gone and wait for your original mods to return before changing profiles.');
        return;
      }
      saving = true;
      submit.disabled = nameField.disabled = true;
      if (!renaming) source.disabled = true;
      showError('');
      const result = await run(renaming ? 'profilesRename' : 'profilesSave', renaming ? { id: profile.id, name } : { name, empty: source.value === 'empty' }, {
        refresh: false,
        progress: renaming ? 'Saving the profile name…' : 'Saving your profile…',
        onError: (error) => { showError(errorText(error)); return true; }
      });
      saving = false;
      submit.disabled = nameField.disabled = false;
      if (!renaming) source.disabled = false;
      if (!result || context !== state.snapshot.accountContext) return;
      // Use the mutation's complete list. A concurrent older list read must not hide the new profile.
      ++profileReadGeneration;
      state.profilesLoading = false;
      state.portableProfiles = result;
      state.profileSession = result.session || state.profileSession;
      state.profileSearch = '';
      if ($('#portable-profile-name') === nameField) $('#modal').close();
      if (state.view === 'profiles') render();
      toast(renaming ? 'Profile renamed.' : 'Profile created.');
    };
    nameField.onkeydown = (e) => {
      if (e.key === 'Enter' && !e.isComposing) { e.preventDefault(); submit.click(); }
    };
  }
  function planPaths(items, label) {
    if (!items?.length) return `<p class="inline-muted">${escape(label)}: none.</p>`;
    return `<div class="launch-file-list">${items.map((item) => `<div class="launch-file"><strong>${escape(label)} · ${formatBytes(item.bytes)}</strong><span>From</span><code>${escape(item.source || 'Not supplied')}</code><span>To</span><code>${escape(item.destination || 'Not supplied')}</code></div>`).join('')}</div>`;
  }
  // The launch review names each PAK (the mod name when it is known); the exact paths sit in a folded list.
  function planFileName(item) {
    const mod = item.sha256 && state.snapshot.mods.find((m) => m.sha256 === item.sha256);
    return (
      mod?.name ||
      String(item.source || '')
        .split(/[\\/]/)
        .pop() ||
      'PAK file'
    );
  }
  function planFiles(items, label) {
    if (!items?.length) return `<p class="inline-muted">${escape(label)}: none.</p>`;
    return `<ul class="launch-name-list">${items.map((item) => `<li><strong>${escape(planFileName(item))}</strong><span>${formatBytes(item.bytes)}</span></li>`).join('')}</ul><details class="finding-evidence"><summary>File paths</summary>${planPaths(items, label)}</details>`;
  }
  async function reviewPortableProfile(name) {
    const plan = await run(
      'profileLaunchPlan',
      { name },
      { refresh: false, progress: 'Checking the profile and your mods folders…' }
    );
    if (!plan) return;
    const blockers = plan.blockers || [],
      warnings = plan.warnings || [];
    const profileMods = (plan.additions || []).length;
    modal(
      `Play with ${name}?`,
      `<p>For this session the toolkit sets your current mods aside, puts the ${profileMods} mod${profileMods === 1 ? '' : 's'} of <strong>${escape(name)}</strong> in their place and starts Days Gone. When you close the game, your own mods come back.</p>${blockers.length ? `<div class="banner warning"><div><strong>This profile cannot launch yet.</strong><ul>${blockers.map((b) => `<li>${escape(typeof b === 'string' ? b : b.message || b.reason || b.code)}</li>`).join('')}</ul>${plan.loadingSetupNeeded ? `<div class="inline-actions">${button('Set up mod loading', 'profile-loading-setup', 'wrench', 'small primary')}</div>` : ''}</div></div>` : ''}<h3 class="launch-section-title">Your mods set aside for this session · ${(plan.moves || []).length}</h3>${planFiles(plan.moves, 'Set aside for this session')}<h3 class="launch-section-title">Mods of this profile · ${profileMods}</h3>${planFiles(plan.additions, 'Profile copy')}${warnings.length ? `<ul class="launch-notes">${warnings.map((w) => `<li>${escape(typeof w === 'string' ? w : w.message || w.reason)}</li>`).join('')}</ul>` : ''}${plan.gameExe ? `<details class="finding-evidence"><summary>Game that starts</summary><div class="startup-path"><span>Game executable</span><code>${escape(plan.gameExe)}</code></div></details>` : ''}<p class="mt-16"><strong>Keep the toolkit open while you play.</strong> It puts your own mods back when the game closes. The game’s own files are not changed.</p>`,
      `<button class="button" data-modal-close>Cancel</button>${
        plan.canLaunch && plan.planToken
          ? '<button id="portable-launch-confirm" class="button primary">' + icon('play') + 'Launch' + '</button>'
          : ''
      }`
    );
    // Setting up mod loading from here returns to a fresh review of the same profile.
    const loadingSetup = plan.loadingSetupNeeded && document.querySelector('[data-action="profile-loading-setup"]');
    if (loadingSetup)
      loadingSetup.onclick = async () => {
        $('#modal').close();
        await loadDirectories();
        confirmDirectoryChange('prepare', () => reviewPortableProfile(name));
      };
    const confirm = $('#portable-launch-confirm');
    if (plan.canLaunch && plan.planToken && confirm)
      confirm.onclick = async () => {
        $('#modal').close();
        const result = await run(
          'profileLaunch',
          { name, planToken: plan.planToken, confirmed: true },
          { refresh: false, progress: 'Setting up the profile and starting the game…' }
        );
        if (result) {
          state.profileSession = result.session || { active: true, status: 'launching', profileName: name };
          await loadPortableProfiles();
          render();
          if (!showProfileSaveBackupWarning(result))
            toast('Profile launch requested. Keep the toolkit open for restoration after the game closes.');
        }
      };
  }
  function restoreProfileSession() {
    const session = state.profileSession;
    if (!session.active || !session.canRestore) {
      toast('Restoration is available only after Days Gone has closed. Check the session status.', 'warning');
      return;
    }
    confirmModal(
      'Put your own mods back?',
      `<p>The toolkit removes the mods of <strong>${escape(session.profileName || 'this profile')}</strong> and puts your own mods back where they were. <strong>Days Gone must be closed.</strong></p><p class="field-note">It stops if a file changed during the session, so nothing gets lost. Saves are not touched. Do not delete the toolkit’s holding folder.</p>${session.gamePath ? `<details class="finding-evidence"><summary>Game folder</summary><div class="startup-path"><span>Session game folder</span><code>${escape(session.gamePath)}</code></div></details>` : ''}`,
      'Put my mods back',
      async () => {
        const result = await run(
          'profileFinishSession',
          { confirmed: true },
          { refresh: false, progress: 'Putting your own mods back…' }
        );
        if (result) {
          const backupWarning = showProfileSaveBackupWarning(result);
          state.profileSession = result.session || result;
          await loadPortableProfiles();
          await refresh({ quiet: true });
          if (!backupWarning) toast(
            state.profileSession.active ? 'Restoration requested. Check the session status.' : 'Your own mods are back.'
          );
        }
      }
    );
  }
  let lastProfileSessionFingerprint = '';
  const profileBackupWarningKeys = new Set();
  function profileSaveBackupWarning(result) {
    const warning = result?.warning || result?.saveBackup?.warning || result?.session?.warning ||
      result?.data?.warning || result?.data?.saveBackup?.warning || result?.data?.session?.warning;
    return typeof warning === 'string' ? warning.trim().slice(0, 2000) : '';
  }
  function showProfileSaveBackupWarning(result) {
    const warning = profileSaveBackupWarning(result);
    if (!warning) return false;
    const session = result?.session || result?.data?.session || result?.data || {},
      key = `${state.snapshot.accountContext}:${session.sessionId || state.profileSession?.sessionId || ''}:${warning}`;
    if (!profileBackupWarningKeys.has(key)) {
      profileBackupWarningKeys.add(key);
      while (profileBackupWarningKeys.size > 16) profileBackupWarningKeys.delete(profileBackupWarningKeys.values().next().value);
      toast(warning, 'warning', { label: 'Open saves', run: () => setSavesTab('backups') });
    }
    return true;
  }
  function handleProfileSessionEvent(result) {
    const session = result.session || result.data?.session || result.data;
    if (!session || typeof session !== 'object') return;
    const fingerprint = JSON.stringify([
      session.active,
      session.status,
      session.profileName,
      session.canRestore,
      session.message,
      session.sessionId,
      profileSaveBackupWarning(result)
    ]);
    if (fingerprint === lastProfileSessionFingerprint) return;
    lastProfileSessionFingerprint = fingerprint;
    showProfileSaveBackupWarning(result);
    state.profileSession = session;
    loadPortableProfiles().then(() => {
      render();
      scheduleFilesystemRefresh();
    });
  }
  function formatDate(value) {
    if (!value) return 'Not checked';
    const d = new Date(typeof value === 'number' && value < 1e12 ? value * 1000 : value);
    return Number.isNaN(d.getTime())
      ? String(value)
      : d.toLocaleDateString(undefined, { year: 'numeric', month: 'short', day: 'numeric' });
  }
  function guideBody(article) {
    let body = article.body;
    if (state.snapshot.platform === 'linux') {
      body = body.replace(/%LOCALAPPDATA%\\BendGame\\Saved\\Paks/g, escape(modsPathLabel()));
      if (['getting-started', 'not-working', 'external-mods', 'directories', 'launcher-path'].includes(article.id))
        body =
          `<aside><strong>Linux / Proton:</strong> Packages go into the selected Steam installation’s Windows user folders inside its existing Proton prefix. Use the actual <strong>Managed mod PAKs</strong> path in Folders; a native Linux home folder is not a replacement. Windows-specific account advice below applies to the Windows version.</aside>` +
          body;
    }
    return body;
  }
  function visibleGuideArticles() {
    return (
      (window.FIELD_GUIDE || [])
        .filter(
          (article) =>
            !!article.modding === (state.helpSection === 'modding') &&
            (!article.edition || article.edition === (localEdition() ? 'local' : 'nexus')) &&
            `${article.title} ${article.summary} ${article.body}`.toLowerCase().includes(state.helpSearch.toLowerCase())
        )
        // The command line is for advanced users, so it comes last in the help list instead of first.
        .sort((a, b) =>
          state.helpSection === 'modding'
            ? (a.chapter || 0) - (b.chapter || 0)
            : (a.id === 'command-line') - (b.id === 'command-line')
        )
    );
  }
  function guideSectionTabs() {
    return `<div class="guide-section-tabs" role="group" aria-label="Field guide section"><button class="tab ${state.helpSection !== 'modding' ? 'active' : ''}" data-help-section="help">${icon('wrench')}Help & troubleshooting</button><button class="tab ${state.helpSection === 'modding' ? 'active' : ''}" data-help-section="modding">${icon('layers')}Make mods</button></div>`;
  }
  function renderModdingChapters(articles) {
    if (!articles.length)
      return '<div class="empty-state compact"><h3>No matching chapters</h3><p>Try a tool name, “package” or “assets”.</p></div>';
    return `<div class="modding-chapters">${articles.map((article) => `<button class="modding-chapter" data-article="${escape(article.id)}"><span class="chapter-number">${String(article.chapter || 0).padStart(2, '0')}</span><span class="chapter-copy"><span class="guide-category">${escape(article.level || 'DAYS GONE MODDING')}</span><h2>${escape(article.title)}</h2><p>${escape(article.summary)}</p></span><span class="chapter-arrow">${icon('arrow')}</span></button>`).join('')}</div>`;
  }
  async function openModdingResource(resourceId) {
    return run('openModdingResource', { resourceId }, { refresh: false });
  }
  function renderResourceButtons(article) {
    const resources = article.resources || [];
    return resources.length
      ? `<section class="learning-resources"><h2>Tool sources & reference</h2><p>These buttons open the verified project or documentation page in your browser. The toolkit does not download or run modding tools.</p><div class="resource-buttons">${resources.map((resource) => `<button class="button small" data-modding-resource="${escape(resource.id)}">${icon('external')}${escape(resource.label)}</button>`).join('')}</div>`
      : '';
  }
  let workshopGeneration = 0, workshopAssetGeneration = 0, workshopEditTimer = null;
  const workshopDrafts = new Map(), workshopMergeChoices = new Map();
  function workshopContext() {
    return JSON.stringify([state.snapshot.accountContext || '', state.snapshot.settings?.gamePath || '', state.snapshot.activePath || '']);
  }
  function workshopSession(value) {
    if (!value || typeof value.id !== 'string' || !value.id || value.id.length > 512 ||
        !Number.isSafeInteger(value.revision) || value.revision < 0 || !Array.isArray(value.assets) || value.assets.length > 4096)
      return null;
    if (value.assets.some((asset) => !asset || typeof asset.id !== 'string' || !asset.id || asset.id.length > 512 ||
        typeof asset.path !== 'string' || asset.path.length > 4096)) return null;
    return value;
  }
  function workshopRequiredAssets() {
    const assets = state.workshop?.assets || [], families = new Set();
    return assets.filter((asset) => {
      if (asset.removed === true) return false;
      const needsChoice = asset.needsChoice === true || asset.conflict === true ||
        state.workshopMerge?.asset === asset.id && state.workshopMerge.conflicts?.length > 0;
      if (!needsChoice) return false;
      const family = typeof asset.family === 'string' ? asset.family : asset.id;
      if (families.has(family)) return false;
      families.add(family); return true;
    });
  }
  function workshopAssets() {
    const search = state.workshopSearch.trim().toLocaleLowerCase(),
      assets = state.workshopChoicesOnly ? workshopRequiredAssets() : state.workshop?.assets || [];
    return assets.filter((asset) => !search || asset.path.toLocaleLowerCase().includes(search));
  }
  async function nextWorkshopChoice() {
    if (workshopBlocked() || workshopPending()) return false;
    const choices = workshopRequiredAssets(), next = choices.find((asset) => asset.id !== state.workshopSelection) || choices[0];
    if (!next) return false;
    state.workshopChoicesOnly = true; state.workshopSearch = '';
    paintWorkshop();
    const selected = await selectWorkshopAsset(next.id);
    if (selected && state.view === 'workshop') {
      const detail = $('#workshop-detail'), reduced = state.snapshot.settings?.reducedMotion || window.matchMedia?.('(prefers-reduced-motion: reduce)')?.matches;
      detail?.scrollIntoView?.({ block: 'start', behavior: reduced ? 'instant' : 'smooth' });
      detail?.focus?.({ preventScroll: true });
    }
    return selected;
  }
  function renderWorkshopSummary() {
    const session = state.workshop;
    if (!session || Number(session.sourceCount || session.sources?.length || 0) < 2) return '';
    const analysis = session.analysis && typeof session.analysis === 'object' ? session.analysis : {},
      count = (key) => Number.isSafeInteger(analysis[key]) && analysis[key] >= 0 ? analysis[key] : null,
      shared = count('sharedFamilies'), identical = count('identicalFamilies'), conflicting = count('conflictingFamilies'),
      readOnly = count('readOnlyFamilies'), unchecked = count('uncheckedFamilies'), unresolved = Number(session.unresolvedCount) || 0,
      sharedPaths = session.assets.filter((asset) => asset.sourceIds?.length > 1).length;
    return `<section class="workshop-summary" aria-label="Combination steps"><ol class="workshop-progress"><li class="complete"><span>1</span>Choose mods</li><li class="complete"><span>2</span>Review summary</li><li class="${unresolved ? 'current' : 'complete'}"><span>3</span>Required choices</li><li class="${!unresolved ? 'current' : ''}"><span>4</span>Check export</li></ol><div class="workshop-summary-heading"><div><h2>${unresolved ? 'Review the overlaps before exporting.' : 'Your draft is ready for its export check.'}</h2><p>Separate assets are kept together automatically. Identical copies need no decision. Different versions of one asset need a source choice or a supported property comparison.</p></div><div class="inline-actions">${unresolved ? `<button type="button" class="button primary" data-action="workshop-next-choice" ${workshopBlocked() || workshopPending() || !workshopRequiredAssets().length ? 'disabled' : ''}>Review next required choice ${icon('arrow')}</button>` : `<button type="button" class="button primary" data-action="workshop-build" ${workshopBlocked() || workshopPending() || session.canBuild !== true ? 'disabled' : ''}>${icon('cube')}Check and export…</button>`}</div></div><dl class="workshop-summary-counts"><div><dt>${shared === null ? 'Shared asset paths' : 'Shared asset families'}</dt><dd>${shared === null ? sharedPaths : shared}</dd></div><div><dt>Identical families</dt><dd>${identical === null ? 'Not reported' : identical}</dd></div><div><dt>Different families</dt><dd>${conflicting === null ? 'Not reported' : conflicting}</dd></div><div><dt>${readOnly === null ? 'Property support' : 'Read-only families'}</dt><dd>${readOnly === null ? 'Checked on selection' : readOnly}</dd></div><div><dt>Choices left</dt><dd>${unresolved}</dd></div></dl>${unchecked !== null && unchecked > 0 ? `<p class="field-note">The initial package check did not inspect property layouts for ${unchecked} asset famil${unchecked === 1 ? 'y' : 'ies'}. Select an asset to check editing and merge support.</p>` : ''}<p class="field-note">A clean file check does not guarantee gameplay compatibility. Export a separate PAK, then test it with a backed-up save.</p></section>`;
  }
  function workshopPending() { return workshopDrafts.size > 0 || workshopEditTimer !== null || workshopMergeChoices.size > 0; }
  function workshopBlocked() {
    return state.preview || state.busy || state.workshopWorking || state.workshopLoading || !!state.nativeDropActivity?.running;
  }
  function workshopWarnings(warnings) {
    return Array.isArray(warnings) && warnings.length
      ? `<ul class="workshop-notes">${warnings.filter((item) => typeof item === 'string').slice(0, 16).map((item) => `<li>${escape(item)}</li>`).join('')}</ul>` : '';
  }
  function workshopProjectRecord(value) {
    if (!value || typeof value.id !== 'string' || !value.id || value.id.length > 128 || /[\x00-\x20\x7f]/.test(value.id) ||
        typeof value.name !== 'string' || !value.name.trim() || value.name.length > 256 || /[\x00-\x1f\x7f]/.test(value.name)) return null;
    return value;
  }
  function workshopProjectBinding() { return workshopProjectRecord(state.workshop?.project); }
  function workshopNeedsProjectSave() {
    return typeof state.workshop?.projectDirty === 'boolean' ? state.workshop.projectDirty : state.workshop?.dirty === true;
  }
  function renderWorkshopProjectStatus() {
    const project = workshopProjectBinding();
    return `<div class="workshop-project-status"><div><span class="field-label">WORKSHOP PROJECT</span><strong>${escape(project?.name || 'Not saved as a project')}</strong><p role="status">${project ? workshopNeedsProjectSave() ? 'Changes need saving.' : 'Saved on this computer.' : 'Save a project to keep working after closing the toolkit.'}</p></div><button type="button" class="button small" data-action="workshop-project-save" ${workshopBlocked() || workshopPending() ? 'disabled' : ''}>${icon('save')}Save project…</button></div>`;
  }
  async function saveWorkshopProject(afterSave = null, forceNew = false) {
    if (workshopBlocked() || workshopPending() || !state.workshop) return false;
    const project = forceNew ? null : workshopProjectBinding(), context = workshopContext(),
      session = state.workshop.id, revision = state.workshop.revision,
      initial = project?.name || (state.workshop.sources?.[0]?.name || 'Workshop project').replace(/\.pak$/i, '').slice(0, 80);
    modal(project ? 'Save this Workshop project' : 'Save a Workshop project',
      `<div class="workshop-project-save workshop-project-dialog"><p>Keep this draft so you can reopen it later, including its edits, source choices and unfinished merge decisions.</p><label class="field-label" for="workshop-project-name">Project name</label><input class="input full-width" id="workshop-project-name" maxlength="80" value="${escape(initial)}" autocomplete="off" spellcheck="false"><p class="field-note">Projects stay with your toolkit data. Saving a project does not install or export a PAK.</p><p class="field-error" id="workshop-project-error" role="alert"></p></div>`,
      `<button type="button" class="button" data-modal-close>Cancel</button>${project ? '<button type="button" class="button" id="workshop-project-save-copy">Save as new project</button>' : ''}<button type="button" class="button primary" id="workshop-project-save-confirm">${icon('save')}Save project</button>`);
    const generation = modalContentGeneration;
    let saving = false;
    const save = async (asNew) => {
      if (saving || !$('#modal').open || context !== workshopContext() || generation !== modalContentGeneration || state.workshop?.id !== session ||
          state.workshop.revision !== revision || workshopBlocked() || workshopPending()) return false;
      const name = $('#workshop-project-name').value.trim();
      if (!name || /[\x00-\x1f\x7f]/.test(name)) { $('#workshop-project-error').textContent = 'Give the project a name without control characters.'; return false; }
      saving = true; $('#workshop-project-save-confirm').disabled = true;
      if (project) $('#workshop-project-save-copy').disabled = true;
      const args = { session, revision, name, ...(project && !asNew ? { projectId: project.id } : {}) };
      const result = await run('workshopProjectSave', args, { refresh: false, activity: true, progress: 'Saving the Workshop project…',
        onError: (error) => { if ($('#modal').open && context === workshopContext() && generation === modalContentGeneration) $('#workshop-project-error').textContent = errorText(error); return true; } });
      if (!$('#modal').open || context !== workshopContext() || generation !== modalContentGeneration || state.workshop?.id !== session || state.workshop.revision !== revision) return false;
      saving = false; $('#workshop-project-save-confirm').disabled = false;
      if (project) $('#workshop-project-save-copy').disabled = false;
      if (!result) return false;
      if (!workshopSession(result.session) || result.session.id !== session || result.session.revision !== revision || !workshopProjectRecord(result.project) ||
          project && !asNew && result.project.id !== project.id || project && asNew && result.project.id === project.id ||
          result.session.project?.id !== result.project.id || result.session.projectDirty !== false) {
        $('#workshop-project-error').textContent = 'The project did not return a confirmed saved record. Your draft is still open.'; return false;
      }
      acceptWorkshopSession(result.session); $('#modal').close(); paintWorkshop();
      toast(`Project saved: ${result.project.name}`, 'success');
      if (typeof afterSave === 'function' && context === workshopContext()) await afterSave();
      return true;
    };
    $('#workshop-project-save-confirm').onclick = () => save(false);
    if (project) $('#workshop-project-save-copy').onclick = () => save(true);
    return true;
  }
  function reviewWorkshopReplacement(proceed, title = 'Leave this draft?') {
    if (!workshopNeedsProjectSave()) return proceed();
    const context = workshopContext(), id = state.workshop.id, revision = state.workshop.revision;
    modal(title, '<div class="workshop-project-replace workshop-project-dialog"><p>This draft has changes that have not been saved to a project. Save them before continuing, or continue without keeping them.</p><p class="field-note">Original files and previously saved projects stay in place.</p></div>',
      '<button type="button" class="button" data-modal-close>Cancel</button><button type="button" class="button" id="workshop-project-discard">Continue without saving</button><button type="button" class="button primary" id="workshop-project-keep">Save project…</button>');
    const generation = modalContentGeneration;
    const valid = () => $('#modal').open && context === workshopContext() && generation === modalContentGeneration && state.workshop?.id === id && state.workshop.revision === revision && !workshopBlocked() && !workshopPending();
    $('#workshop-project-discard').onclick = async () => { if (!valid()) return false; $('#modal').close(); return proceed(); };
    $('#workshop-project-keep').onclick = () => valid() ? saveWorkshopProject(proceed) : false;
    return true;
  }
  async function openWorkshopProject(project) {
    if (!workshopProjectRecord(project) || workshopBlocked() || workshopPending()) return false;
    const open = async () => {
      const context = workshopContext(), generation = ++workshopGeneration;
      state.workshopWorking = true; state.workshopError = ''; paintWorkshop();
      try {
        const result = await run('workshopProjectOpen', { id: project.id }, { refresh: false, activity: true, progress: 'Checking and reopening the saved project…',
          onError: (error) => { if (context === workshopContext() && generation === workshopGeneration) state.workshopError = errorText(error); return true; } });
        if (context !== workshopContext() || generation !== workshopGeneration || !result || !workshopSession(result.session) || !workshopProjectRecord(result.project)) return false;
        if (result.project.id !== project.id || result.session.project?.id !== project.id) {
          state.workshopError = 'The reopened project did not match the selected saved record.'; return false;
        }
        acceptWorkshopSession(result.session); state.workshopLoaded = true; return true;
      } finally { if (context === workshopContext() && generation === workshopGeneration) { state.workshopWorking = false; paintWorkshop(); } }
    };
    return reviewWorkshopReplacement(open, 'Open this saved project?');
  }
  async function deleteWorkshopProject(project, onDeleted = null) {
    if (!workshopProjectRecord(project) || workshopBlocked() || workshopPending()) return false;
    const context = workshopContext(), originalGeneration = modalContentGeneration;
    const review = await run('workshopProjectDeleteReview', { id: project.id }, { refresh: false, progress: 'Checking the saved project…',
      onError: () => context !== workshopContext() || originalGeneration !== modalContentGeneration });
    const reviewed = workshopProjectRecord(review?.project);
    if (context !== workshopContext() || originalGeneration !== modalContentGeneration || typeof review?.token !== 'string' || !review.token || review.token.length > 512 || reviewed?.id !== project.id) return false;
    modal('Delete this saved project?', `<div class="workshop-project-delete workshop-project-dialog"><h3>${escape(reviewed.name)}</h3><p>This removes this saved project. Exported PAKs, installed mods and game saves stay in place.</p>${workshopProjectBinding()?.id === project.id ? '<p>The current temporary draft stays open. Save it as a new project if you want to keep working on it later.</p>' : ''}<p class="field-error" id="workshop-project-delete-error" role="alert"></p></div>`,
      '<button type="button" class="button" data-modal-close>Cancel</button><button type="button" class="button danger" id="workshop-project-delete-confirm">Delete project</button>');
    const generation = modalContentGeneration;
    let used = false;
    $('#workshop-project-delete-confirm').onclick = async () => {
      if (used || !$('#modal').open || context !== workshopContext() || generation !== modalContentGeneration || workshopBlocked() || workshopPending()) return false;
      used = true; $('#workshop-project-delete-confirm').disabled = true;
      const result = await run('workshopProjectDelete', { token: review.token, confirmed: true }, { refresh: false, activity: true, progress: 'Deleting the reviewed saved project…',
        onError: (error) => { if ($('#modal').open && context === workshopContext() && generation === modalContentGeneration) $('#workshop-project-delete-error').textContent = errorText(error) + ' Close this review and check the project again before retrying.'; return true; } });
      if (!$('#modal').open || context !== workshopContext() || generation !== modalContentGeneration || result?.deleted !== true || result.id !== project.id) return false;
      if (Object.hasOwn(result, 'session') && (result.session === null || workshopSession(result.session))) acceptWorkshopSession(result.session);
      $('#modal').close(); paintWorkshop(); toast('Saved project deleted.', 'success');
      if (typeof onDeleted === 'function') await onDeleted();
      return true;
    };
    return true;
  }
  async function showWorkshopProjects() {
    if (workshopBlocked() || workshopPending() || state.workshopProjectsLoading) return false;
    const context = workshopContext(), originalGeneration = modalContentGeneration, workshopRequestGeneration = workshopGeneration;
    state.workshopProjectsLoading = true;
    let result;
    try { result = await run('workshopProjectsList', {}, { refresh: false, progress: 'Reading saved Workshop projects…',
      onError: () => context !== workshopContext() || originalGeneration !== modalContentGeneration }); }
    finally { if (context === workshopContext()) state.workshopProjectsLoading = false; }
    if (context !== workshopContext() || originalGeneration !== modalContentGeneration || workshopRequestGeneration !== workshopGeneration || !result || !Array.isArray(result.projects)) return false;
    const projects = result.projects.slice(0, 128).map(workshopProjectRecord).filter(Boolean);
    modal('Saved Workshop projects', `<div class="workshop-projects workshop-project-dialog"><p>Reopen a project to continue its supported edits and source choices. Its source copies are checked before the current draft is replaced. You can reopen it even if the original downloads have moved or been deleted.</p>${workshopWarnings(result.warnings)}${projects.length ? `<div class="workshop-project-list">${projects.map((project) => `<article class="workshop-project-row"><div><h3>${escape(project.name)}</h3><p>${escape(logTime(project.modifiedAt || project.updatedAt || project.savedAt))} · ${Math.max(0, Number(project.sourceCount) || 0)} sources${Number.isSafeInteger(project.changedProperties) ? ' · ' + project.changedProperties + ' changed properties' : ''}${Number.isSafeInteger(project.unresolvedCount) && project.unresolvedCount > 0 ? ' · ' + project.unresolvedCount + ' choice' + (project.unresolvedCount === 1 ? '' : 's') + ' left' : ''}</p></div><div class="inline-actions"><button type="button" class="button small" data-workshop-project-open="${escape(project.id)}" aria-label="Open ${escape(project.name)}">Open</button><button type="button" class="button small" data-workshop-project-delete="${escape(project.id)}" aria-label="Delete ${escape(project.name)}">${icon('trash')}Delete</button></div></article>`).join('')}</div>` : '<div class="empty-state compact"><h3>No saved projects yet</h3><p>Open a PAK or exported asset, then choose Save project.</p></div>'}</div>`, '<button type="button" class="button" data-modal-close>Close</button>');
    const generation = modalContentGeneration;
    $$('[data-workshop-project-open]').forEach((control) => { control.onclick = async () => {
      if (!$('#modal').open || context !== workshopContext() || generation !== modalContentGeneration) return false;
      const project = projects.find((item) => item.id === control.dataset.workshopProjectOpen);
      if (!project) return false;
      $('#modal').close(); return openWorkshopProject(project);
    }; });
    $$('[data-workshop-project-delete]').forEach((control) => { control.onclick = () => {
      if (!$('#modal').open || context !== workshopContext() || generation !== modalContentGeneration) return false;
      const project = projects.find((item) => item.id === control.dataset.workshopProjectDelete);
      return project ? deleteWorkshopProject(project, showWorkshopProjects) : false;
    }; });
    return true;
  }
  function workshopAssetRows() {
    const assets = workshopAssets(), shown = assets.slice(0, 100);
    return shown.length ? shown.map((asset) => `<button type="button" class="workshop-asset ${asset.removed ? 'workshop-asset-removed' : ''}" data-workshop-asset="${escape(asset.id)}" title="${escape(asset.path)}" aria-label="${escape(asset.path)}" aria-pressed="${asset.id === state.workshopSelection}" ${workshopPending() || state.workshopWorking ? 'disabled' : ''}><span class="workshop-asset-icon">${icon(asset.removed ? 'trash' : asset.needsChoice || asset.conflict ? 'alert' : asset.edited || asset.replaced ? 'wrench' : 'report')}</span><span><strong>${escape(asset.path)}</strong><small>${asset.removed ? 'Removed from draft · Can be restored' : formatBytes(Number(asset.bytes) || 0) + (asset.needsChoice || asset.conflict ? ' · Choose a version or review a merge' : asset.replaced ? ' · Replaced file' : asset.edited ? ' · Edited draft' : asset.added ? ' · Added file' : '')}</small></span></button>`).join('') + (assets.length > shown.length ? '<p class="field-note">Showing the first 100 paths. Use search to narrow the list.</p>' : '')
      : `<p class="field-note">${state.workshopChoicesOnly ? 'No required choices match this view. Use All assets to inspect the complete draft.' : 'No matching asset paths.'}</p>`;
  }
  function workshopFieldValue(field) {
    const draft = workshopDrafts.get(field.id);
    return draft && draft.asset === state.workshopSelection ? draft.raw : field.value;
  }
  function workshopFieldEditor(field) {
    const editor = field?.editor || ({ IntProperty: 'integer', FloatProperty: 'number', BoolProperty: 'boolean' })[field?.type];
    if (editor === 'boolean') return typeof field.value === 'boolean' ? editor : null;
    if (editor === 'integer') return Number.isSafeInteger(field.value) ? editor : null;
    if (editor === 'number') return Number.isFinite(field.value) ? editor : null;
    if (editor === 'text') return typeof field.value === 'string' && Number.isSafeInteger(field.maxLength) && field.maxLength >= 0 && field.maxLength <= 65536 && field.value.length <= field.maxLength ? editor : null;
    if (editor === 'enum') {
      const choices = field.choices;
      if (!Array.isArray(choices) || !choices.length || choices.length > 256 || choices.some((choice) => !choice || typeof choice.label !== 'string' || choice.label.length > 512 ||
          !['string', 'number', 'boolean'].includes(typeof choice.value) || typeof choice.value === 'number' && !Number.isFinite(choice.value) || typeof choice.value === 'string' && choice.value.length > 512)) return null;
      return choices.some((choice) => choice.value === field.value) ? editor : null;
    }
    return null;
  }
  function renderWorkshopFieldControl(field, locked) {
    const editor = workshopFieldEditor(field), id = escape(field.id), disabled = state.preview || locked || workshopBlocked() ? 'disabled' : '',
      current = workshopDrafts.get(field.id), value = current?.asset === state.workshopSelection && current.valid ? current.value : field.value;
    if (editor === 'boolean') return `<input type="checkbox" id="workshop-field-${id}" data-workshop-field="${id}" ${workshopFieldValue(field) ? 'checked' : ''} ${disabled}>`;
    if (editor === 'enum') return `<select class="input" id="workshop-field-${id}" data-workshop-field="${id}" ${disabled}>${field.choices.map((choice, index) => `<option value="${index}" ${choice.value === value ? 'selected' : ''}>${escape(choice.label)}</option>`).join('')}</select>`;
    return `<input class="input" type="text" inputmode="${editor === 'integer' ? 'numeric' : editor === 'number' ? 'decimal' : 'text'}" id="workshop-field-${id}" data-workshop-field="${id}" value="${escape(workshopFieldValue(field))}" maxlength="${editor === 'text' ? field.maxLength : 80}" spellcheck="false" autocomplete="off" ${disabled}>`;
  }
  function workshopPropertyHelp(field) {
    const type = field.type || '', name = String(field.name || '').replace(/([a-z])([A-Z])/g, '$1 $2').replace(/[_/]+/g, ' '),
      kind = type === 'BoolProperty' ? 'Bool is an on/off switch: checked is true, unchecked is false.'
        : /Float|Double/.test(type) ? 'A decimal number. It may store an amount, duration, scale or multiplier.'
        : /Int|Byte/.test(type) ? 'A whole number. It may store a count, index or numbered choice.'
        : field.editor === 'enum' ? 'A named choice already stored in this asset.'
        : field.editor === 'text' ? 'Text stored in this asset; its encoded length must stay the same.' : 'A component of the selected property.',
      clue = /damage/i.test(name) ? 'The name suggests a damage setting.'
        : /cooldown|duration|delay|interval/i.test(name) ? 'The name suggests timing; the asset does not establish its units.'
        : /recoil|spread/i.test(name) ? 'The name suggests weapon recoil or shot spread.'
        : /speed/i.test(name) ? 'The name suggests a speed setting; check whether this is a base value or multiplier.'
        : /health|stamina|focus/i.test(name) ? 'The name refers to a player or actor attribute.'
        : /distance|radius|range/i.test(name) ? 'The name suggests a distance or area setting; its units depend on the game code.' : '';
    const rawName = String(field.name || ''), leaf = rawName.split('/').at(-1),
      knownWeaponFields = {
        MinAccuracyRadius: 'Lower bound of the weapon’s shot-accuracy cone.',
        MaxAccuracyRadius: 'Upper bound of the weapon’s shot-accuracy cone.',
        NumDegreesIncRadiusPerShot: 'How much the shot cone widens with each shot (firing bloom).',
        fWeaponSway: 'Weapon aim sway for the normal stance.',
        fWeaponSwayCrouching: 'Weapon aim sway while crouching.',
        fSwayPitchMagMult: 'Vertical aim-sway multiplier for the normal stance.',
        fSwayPitchMagMultCrouching: 'Vertical aim-sway multiplier while crouching.',
        MinRecoilAngle: 'Lower bound of shot-recoil movement for this direction.',
        MaxRecoilAngle: 'Upper bound of shot-recoil movement for this direction.',
        CameraShakeRightLeftAngle: 'Horizontal camera-shake angle when firing.',
        CameraShakeUpDownAngle: 'Vertical camera-shake angle when firing.'
      },
      known = /(?:^|\/)WeaponConfig(?:Override)?\//.test(rawName) ? knownWeaponFields[leaf] :
        leaf === 'VehicleMoveSlowlySpeed' && /BillsBike\.uasset$/i.test(state.workshopAsset?.path || '')
          ? 'Bike speed used when slow movement is requested. Scripted riding can use it as well as camp riding.' : '';
    return [known || clue, kind, known ? '' : 'A name is a clue; exact behaviour needs a game test.', field.hint || ''].filter(Boolean).join(' ');
  }
  function renderWorkshopDetail() {
    const asset = state.workshopAsset;
    if (!asset || asset.id !== state.workshopSelection) return `<div class="empty-state compact">${icon('report')}<h2>${state.workshopLoading ? 'Reading the selected asset' : 'Choose an asset'}</h2><p>Select a package path to see its supported properties and source files.</p></div>`;
    const allFields = Array.isArray(asset.fields) ? asset.fields.filter((field) => field && typeof field.id === 'string' && typeof field.type === 'string' && workshopFieldEditor(field)) : [],
      propertySearch = state.workshopPropertySearch.trim().toLocaleLowerCase(),
      matchingFields = allFields.filter((field) => !propertySearch || [field.name, field.label, field.object, field.type].some((value) => String(value || '').toLocaleLowerCase().includes(propertySearch))),
      fields = matchingFields.slice(0, 256),
      ids = Array.isArray(asset.sourceIds) ? asset.sourceIds : [], names = Array.isArray(asset.sourceNames) ? asset.sourceNames : [],
      sources = ids.map((id, index) => ({ id, name: names[index] || 'Source package' })),
      blocked = workshopBlocked(), mergeBlocked = state.workshopMerge?.asset === asset.id && state.workshopMerge.conflicts?.length > 0;
    const sourceChoice = sources.length > 1 && !asset.removed ? `<section class="workshop-source-choice"><h3>Source versions</h3><p>Choosing a source keeps that complete asset family, including its companion data. It does not combine the other version’s edits.</p><div class="workshop-source-buttons">${sources.map((source) => `<button type="button" class="button small" data-workshop-source="${escape(source.id)}" ${blocked || workshopPending() || asset.selectedSource === source.id ? 'disabled' : ''}>${icon(asset.selectedSource === source.id ? 'check' : 'layers')}${escape(source.name)}${asset.selectedSource === source.id ? ' · Selected' : ''}</button>`).join('')}</div>${asset.selectedSource ? `<p class="field-note"><strong>Kept:</strong> ${escape(sources.find((source) => source.id === asset.selectedSource)?.name || 'Selected source')}. <strong>Other versions excluded:</strong> ${sources.filter((source) => source.id !== asset.selectedSource).map((source) => escape(source.name)).join(', ')}. Their edits are not included by this source choice.</p>` : ''}${/\.uasset$/i.test(asset.path) ? `<button type="button" class="button small" id="workshop-baseline" ${blocked || workshopPending() || asset.replaced ? 'disabled' : ''}>${icon('layers')}Compare with original asset…</button><p class="field-note">Choose the matching unmodified .uasset exported from your game. The comparison checks all ${sources.length} opened versions against that original. Opaque changes and separate companion data need a complete source choice.</p>` : ''}</section>` : '';
    const properties = asset.editable === true && allFields.length ? `<label class="field-label" for="workshop-property-search">Find a property</label><input id="workshop-property-search" class="input full-width" type="search" placeholder="Search names, objects or types…" maxlength="240" value="${escape(state.workshopPropertySearch)}"><p class="field-note">${matchingFields.length} matching properties${matchingFields.length > fields.length ? ' · Showing the first 256; narrow the search to find more' : ''}</p><div class="workshop-properties" id="workshop-properties">${fields.map((field) => `<div class="workshop-property"><div class="workshop-property-copy"><label for="workshop-field-${escape(field.id)}"><strong>${escape(field.label || field.name || field.id)}</strong><small>${escape(field.type.replace('Property', ''))}</small></label><details class="workshop-property-help"><summary>What does this do?</summary><p>${escape(workshopPropertyHelp(field))}</p></details></div><div class="workshop-property-input">${renderWorkshopFieldControl(field, mergeBlocked)}<button type="button" class="button small" data-workshop-reset="${escape(field.id)}" title="${escape(Object.hasOwn(field, 'originalValue') ? 'Opened source: ' + String(field.originalValue) : 'Reset to the opened source value')}" aria-label="Reset ${escape(field.name || field.id)} to its opened source value" ${blocked || mergeBlocked || workshopPending() || field.changed !== true ? 'disabled' : ''}>Reset</button></div></div>`).join('') || '<p class="field-note">No properties match that search.</p>'}</div><p class="field-note">${mergeBlocked ? 'Resolve the conflicting values below before editing properties.' : 'Values are validated in the temporary draft as you type. Reset restores the opened source value; Undo reverses the last draft change.'}</p>`
      : `<div class="workshop-readonly"><h3>Property editing is unavailable for this asset</h3><p>${escape(asset.reason || 'Its data layout has not been verified by the supported asset reader. You can inspect its package path and choose a complete source version.')}</p></div>`;
    return `<div class="workshop-detail-heading"><span class="eyebrow">SELECTED ASSET</span><h2>${escape(asset.path)}</h2><span class="workshop-meta">${asset.removed ? 'Removed from this draft' : formatBytes(Number(asset.bytes) || 0)}${asset.replaced ? ' · Replaced file' : asset.edited ? ' · Edited draft' : asset.added ? ' · Added file' : ''}</span></div>${renderWorkshopFileTools(asset)}${sourceChoice}${properties}${renderWorkshopMerge()}`;
  }
  function renderWorkshopFileTools(asset) {
    const locked = workshopBlocked() || workshopPending(), hasBytes = !!asset.selectedSource && !asset.omitted;
    return `<section class="workshop-file-tools"><h3>Package file</h3><div class="inline-actions"><button type="button" class="button small" data-action="workshop-file-replace" ${locked || asset.removed || !hasBytes ? 'disabled' : ''}>${icon('folder')}Replace file…</button><button type="button" class="button small" data-action="workshop-file-extract" ${locked || !hasBytes ? 'disabled' : ''}>${icon('download')}Export file…</button><button type="button" class="button small" data-action="workshop-file-rename" ${locked ? 'disabled' : ''}>Rename path…</button><button type="button" class="button small" data-action="${asset.removed ? 'workshop-file-restore' : 'workshop-file-remove'}" ${locked ? 'disabled' : ''}>${icon(asset.removed ? 'refresh' : 'trash')}${asset.removed ? 'Restore file' : 'Remove from draft'}</button>${asset.replaced || asset.edited ? `<button type="button" class="button small" data-action="workshop-file-reset" ${locked || asset.removed ? 'disabled' : ''}>${icon('back')}Use opened bytes</button>` : ''}</div><p class="field-note">File tools also work with opaque data. Property editing still needs a supported layout. Removed files stay listed so you can restore them.</p></section>`;
  }
  function workshopMergeOptions(field) {
    const names = new Map((state.workshop?.sources || []).map((source) => [source.id, source.name]));
    if (Array.isArray(field?.options)) return [...field.options.filter((option) => option && typeof option.source === 'string').map((option) => ({ id: option.source,
      label: names.get(option.source) || option.source, value: option.value })), { id: 'base', label: 'Original', value: field.base }];
    return ['left', 'right', 'base'].map((id) => ({ id, label: id === 'left' ? 'First mod' : id === 'right' ? 'Second mod' : 'Original', value: field?.[id] }));
  }
  function renderWorkshopMerge() {
    const merge = state.workshopMerge;
    if (!merge || merge.asset !== state.workshopSelection) return '';
    if (merge.supported !== true) return `<div class="banner neutral"><p>${escape(merge.reason || 'The original and modified assets could not be compared safely. Their bytes have been preserved.')}</p></div>`;
    const conflicts = []; let optionCount = 0;
    for (const field of Array.isArray(merge.conflicts) ? merge.conflicts : []) {
      const values = Array.isArray(field?.options) ? field.options.length + 1 : 3,
        cost = values + (values > 6 ? 1 : 0);
      if (conflicts.length && (conflicts.length >= 128 || optionCount + cost > 2048)) break;
      conflicts.push(field); optionCount += cost;
    }
    return `<section class="workshop-merge"><h3>${conflicts.length ? 'Choose the values to keep' : 'Property comparison finished'}</h3><p>${conflicts.length ? 'These mods change the same properties differently. Apply the values you have chosen; you can save the draft with other decisions unfinished.' : 'The comparison checked the original and every selected version. Review the draft before building a separate PAK.'}</p>${(merge.conflicts?.length || 0) > conflicts.length ? `<p class="field-note">Showing the next ${conflicts.length} conflicting properties. Apply these choices to continue with the remaining ones.</p>` : ''}${conflicts.map((field, index) => {
      const options = workshopMergeOptions(field), value = workshopMergeChoices.get(field.property), disabled = workshopBlocked() ? 'disabled' : '';
      return `<fieldset class="workshop-merge-row"><legend>${escape(field.name || field.property || 'Property')}</legend>${options.length > 6 ? `<label class="workshop-merge-select"><span>Source value</span><select class="input" data-workshop-merge="${escape(field.property)}" ${disabled}><option value="">Choose a value…</option>${options.map((option) => `<option value="${escape(option.id)}" ${value === option.id ? 'selected' : ''}>${escape(option.label)} · ${escape(String(option.value))}</option>`).join('')}</select></label>` : options.map((option) => `<label><input type="radio" name="workshop-merge-${index}" data-workshop-merge="${escape(field.property)}" value="${escape(option.id)}" ${value === option.id ? 'checked' : ''} ${disabled}><span><strong>${escape(option.label)}</strong><code>${escape(String(option.value))}</code></span></label>`).join('')}</fieldset>`;
    }).join('')}${conflicts.length ? `<button type="button" class="button" id="workshop-merge-apply" ${workshopBlocked() ? 'disabled' : ''}>Apply selected values</button>` : ''}<p class="field-note">Apply selected values to keep these decisions in the draft. Unsupported binary data, meshes and Blueprint behavior are not guessed or merged.</p></section>`;
  }
  function renderWorkshopStarters(blocked) {
    return `<div class="workshop-starters"><article><span class="learning-emblem">${icon('wrench')}</span><h2>Edit a mod</h2><p>Open a local PAK to edit supported properties or add, replace, remove and export package files. Original packages stay in place.</p><button type="button" class="button" data-action="workshop-open" ${blocked ? 'disabled' : ''}>${icon('folder')}Choose a PAK…</button></article><article><span class="learning-emblem">${icon('layers')}</span><h2>Combine mods</h2><p>Choose your mod PAKs. Keep complete asset families, or compare every supported version against its matching original and choose conflicting values.</p><button type="button" class="button primary" data-action="workshop-open" ${blocked ? 'disabled' : ''}>${icon('layers')}Choose PAKs to combine…</button></article></div><p class="field-note">Create an empty package to start with your own exported files. Opaque assets keep their original bytes; their cooked logic is not guessed or merged. Build creates a separate PAK for testing.</p><button type="button" class="button" data-action="workshop-empty" ${blocked ? 'disabled' : ''}>${icon('plus')}New empty package</button>`;
  }
  function renderWorkshop() {
    const session = state.workshop, blocked = workshopBlocked(), pendingEdits = workshopPending(),
      actions = `<button type="button" class="button" data-action="workshop-projects" ${blocked || pendingEdits || state.workshopProjectsLoading ? 'disabled' : ''}>${icon('folder')}Saved projects…</button><button type="button" class="button" data-action="workshop-open" ${blocked || pendingEdits ? 'disabled' : ''}>${icon('folder')}Open PAKs…</button><button type="button" class="button" data-action="workshop-new" ${blocked || pendingEdits ? 'disabled' : ''}>${icon('plus')}Start from exported asset…</button>`;
    return `<button type="button" class="back-link" data-action="workshop-guide">${icon('book')}Workshop help &amp; modding guide</button>${header('PAK WORKSHOP', 'Work on a separate draft.', 'Inspect local packages, edit supported properties and build a new PAK for testing.', actions)}<div class="banner neutral workshop-scope">${icon('info')}<p>Edits update a temporary draft, not the running game. Original packages stay in place. Cooked assets need a verified layout before their properties can be changed or merged.</p></div>${state.preview ? '<p class="field-note">Open the desktop app to select actual PAKs. This interface preview cannot read or edit files.</p>' : ''}<p class="field-error" id="workshop-error" role="alert">${escape(state.workshopError)}</p>${session ? `${renderWorkshopSummary()}${renderWorkshopProjectStatus()}<div class="workshop-toolbar"><div><strong>Included files: ${Number.isSafeInteger(session.assetCount) ? session.assetCount : session.assets.length}</strong><span>${Number.isSafeInteger(session.sourceCount) ? session.sourceCount : (session.sources?.length || 0)} source package${Number(session.sourceCount ?? session.sources?.length ?? 0) === 1 ? '' : 's'}</span><span id="workshop-draft-status" role="status">${state.workshopWorking ? 'Checking the draft…' : pendingEdits ? 'Waiting to validate changes…' : session.dirty ? 'Draft updated' : 'Unchanged draft'}</span></div><div class="inline-actions"><button type="button" class="button small" data-action="workshop-file-add" ${blocked || pendingEdits ? 'disabled' : ''}>${icon('plus')}Add file…</button><button type="button" class="button small" data-action="workshop-revert-inputs" ${!pendingEdits ? 'hidden' : ''} ${state.workshopWorking ? 'disabled' : ''}>${icon('close')}Revert pending values</button><button type="button" class="button small" data-action="workshop-undo" ${blocked || pendingEdits || session.canUndo !== true ? 'disabled' : ''}>${icon('back')}Undo</button><button type="button" class="button small" data-action="workshop-close" ${blocked || pendingEdits ? 'disabled' : ''}>${icon('close')}Close draft</button><button type="button" class="button small" data-action="workshop-changes" ${blocked || pendingEdits ? 'disabled' : ''}>${icon('report')}Review changes</button>${Number(session.sourceCount || session.sources?.length || 0) < 2 ? `<button type="button" class="button primary" data-action="workshop-build" ${blocked || pendingEdits || session.canBuild !== true ? 'disabled' : ''}>${icon('cube')}Review build…</button>` : ''}</div></div>${session.unresolvedCount ? `<p class="field-note workshop-attention">${Number(session.unresolvedCount)} choice${Number(session.unresolvedCount) === 1 ? ' needs' : 's need'} review before this draft can be built. Only required choices are shown first; All assets keeps the complete draft available.</p>` : ''}<div class="workshop-layout"><section class="workshop-browser" aria-label="Package assets"><div class="workshop-browser-tabs" role="group" aria-label="Show package assets"><button type="button" class="button small" data-workshop-filter="choices" aria-pressed="${state.workshopChoicesOnly}">Choices needed <span>${workshopRequiredAssets().length}</span></button><button type="button" class="button small" data-workshop-filter="all" aria-pressed="${!state.workshopChoicesOnly}">All assets <span>${session.assets.length}</span></button></div><label class="field-label" for="workshop-search">Find a package path</label><div class="search-wrap">${icon('search')}<input id="workshop-search" class="search-input" type="search" value="${escape(state.workshopSearch)}" placeholder="Search asset paths…" maxlength="240"></div><div class="workshop-asset-list" id="workshop-assets">${workshopAssetRows()}</div></section><section class="workshop-detail" id="workshop-detail" aria-label="Selected package asset" tabindex="-1">${renderWorkshopDetail()}</section></div>${workshopWarnings(session.warnings)}` : renderWorkshopStarters(blocked || pendingEdits)}`;
  }
  function paintWorkshop() {
    if (state.view !== 'workshop') return;
    const focus = rememberMainFocus(); render(); restoreMainFocus(focus);
  }
  function acceptWorkshopSession(value) {
    if (value === null) {
      state.workshop = null; state.workshopAsset = null; state.workshopSelection = ''; state.workshopMerge = null; state.workshopPropertySearch = ''; workshopMergeChoices.clear();
      return true;
    }
    const session = workshopSession(value);
    if (!session) throw new Error('The workshop did not return a usable draft. Reopen the source files to try again.');
    if (state.workshop?.id !== session.id) {
      state.workshopChoicesOnly = Number(session.sourceCount) > 1 && session.unresolvedCount > 0; state.workshopSearch = '';
      workshopDrafts.clear(); workshopMergeChoices.clear(); state.workshopAsset = null; state.workshopSelection = ''; state.workshopMerge = null;
    }
    state.workshop = session;
    if (state.workshopSelection && !session.assets.some((asset) => asset.id === state.workshopSelection)) {
      state.workshopSelection = ''; state.workshopAsset = null; state.workshopMerge = null;
    }
    return true;
  }
  async function loadWorkshopStatus() {
    if (state.preview || state.workshopLoading || state.workshopWorking) return false;
    const context = workshopContext(), generation = ++workshopGeneration;
    state.workshopLoading = true; state.workshopError = '';
    try {
      const result = await request('workshopStatus');
      if (context !== workshopContext() || generation !== workshopGeneration) return false;
      if (result?.available !== true || !Object.hasOwn(result, 'session')) throw new Error('The workshop is unavailable in this build.');
      acceptWorkshopSession(result.session); state.workshopLoaded = true; return true;
    } catch (error) { if (context === workshopContext() && generation === workshopGeneration) state.workshopError = errorText(error); return false; }
    finally { if (context === workshopContext() && generation === workshopGeneration) { state.workshopLoading = false; paintWorkshop(); } }
  }
  async function selectWorkshopAsset(id) {
    if (workshopBlocked() || workshopPending() || !state.workshop?.assets.some((asset) => asset.id === id)) return false;
    const session = state.workshop.id, revision = state.workshop.revision, context = workshopContext(), generation = ++workshopAssetGeneration;
    state.workshopSelection = id; state.workshopPropertySearch = ''; state.workshopAsset = null; state.workshopMerge = null; workshopMergeChoices.clear(); state.workshopError = '';
    state.workshopLoading = true; paintWorkshop();
    try {
      const result = await request('workshopAsset', { session, asset: id });
      if (context !== workshopContext() || generation !== workshopAssetGeneration || state.workshop?.id !== session ||
          state.workshop.revision !== revision || state.workshopSelection !== id) return false;
      if (result?.session !== session || result.revision !== revision || result.asset?.id !== id) throw new Error('The selected draft changed. Select the current asset again.');
      state.workshopAsset = result.asset;
      if (result.asset.merge?.supported) state.workshopMerge = { ...result.asset.merge, asset: id };
      return true;
    } catch (error) { if (context === workshopContext() && generation === workshopAssetGeneration) state.workshopError = errorText(error); return false; }
    finally { if (context === workshopContext() && generation === workshopAssetGeneration) { state.workshopLoading = false; paintWorkshop(); } }
  }
  function clearWorkshopDraftInputs() {
    if (workshopEditTimer !== null) clearTimeout(workshopEditTimer);
    workshopEditTimer = null; workshopDrafts.clear(); workshopMergeChoices.clear();
  }
  function updateWorkshopLocks() {
    if (state.view !== 'workshop') return;
    const blocked = workshopBlocked(), pendingEdits = workshopPending();
    $$('[data-action^="workshop-"]').forEach((control) => {
      const action = control.dataset.action;
      if (action === 'workshop-guide' || action === 'workshop-enter') return;
      if (action === 'workshop-revert-inputs') {
        control.hidden = !pendingEdits; control.disabled = state.workshopWorking; return;
      }
      control.disabled = blocked || pendingEdits || action === 'workshop-build' && state.workshop?.canBuild !== true ||
        action === 'workshop-undo' && state.workshop?.canUndo !== true || action === 'workshop-next-choice' && !workshopRequiredAssets().length ||
        action.startsWith('workshop-file-') && action !== 'workshop-file-add' && !state.workshopAsset ||
        action === 'workshop-file-replace' && (state.workshopAsset?.removed || !state.workshopAsset?.selectedSource || state.workshopAsset?.omitted) ||
        action === 'workshop-file-extract' && (!state.workshopAsset?.selectedSource || state.workshopAsset?.omitted) ||
        action === 'workshop-file-reset' && state.workshopAsset?.removed === true;
    });
    $$('[data-workshop-asset], [data-workshop-source], #workshop-baseline').forEach((control) => {
      control.disabled = blocked || pendingEdits || typeof control.dataset.workshopSource === 'string' &&
        control.dataset.workshopSource === state.workshopAsset?.selectedSource;
    });
  }
  async function workshopMutation(action, args = {}, progress = 'Checking the draft…') {
    if (workshopBlocked() || !state.workshop) return null;
    const session = state.workshop.id, revision = state.workshop.revision, context = workshopContext(), generation = workshopGeneration;
    state.workshopWorking = true; state.workshopError = ''; paintWorkshop();
    try {
      const result = await run(action, { session, revision, ...args }, { refresh: false, activity: false, progress,
        onError: (error) => { if (context === workshopContext() && generation === workshopGeneration) state.workshopError = errorText(error); return true; } });
      if (context !== workshopContext() || generation !== workshopGeneration || state.workshop?.id !== session) return null;
      if (!result) return null;
      if (Object.hasOwn(result, 'session') && (result.session === null || typeof result.session === 'object')) {
        if (result.session === null && action !== 'workshopClose' || result.session &&
            (result.session.id !== session || !Number.isSafeInteger(result.session.revision) || result.session.revision < revision))
          throw new Error('The draft response belongs to a different or older session. Refresh the workshop before retrying.');
        acceptWorkshopSession(result.session);
      }
      if (result.asset && result.asset.id === state.workshopSelection) {
        state.workshopAsset = result.asset;
        const merge = result.merge || result.asset.merge;
        state.workshopMerge = merge?.supported ? { ...merge, asset: result.asset.id } : null;
      }
      return result;
    } catch (error) {
      if (context === workshopContext() && generation === workshopGeneration) state.workshopError = errorText(error);
      return null;
    } finally { if (context === workshopContext() && generation === workshopGeneration) { state.workshopWorking = false; paintWorkshop(); } }
  }
  function queueWorkshopEdit(property, input) {
    const field = state.workshopAsset?.fields?.find((item) => item.id === property);
    if (!field || state.workshopAsset.editable !== true || !state.workshop || state.preview ||
        state.workshopMerge?.asset === state.workshopSelection && state.workshopMerge.conflicts?.length > 0) return false;
    const editor = workshopFieldEditor(field);
    if (!editor) return false;
    const raw = editor === 'boolean' ? input.checked === true : input.value;
    let value = raw, valid = true, message = '';
    if (editor === 'enum') {
      const index = /^(0|[1-9]\d*)$/.test(String(raw)) ? Number(raw) : -1;
      valid = Number.isSafeInteger(index) && index >= 0 && index < field.choices.length;
      value = valid ? field.choices[index].value : field.value;
      message = 'Choose a value supplied by this asset.';
    } else if (editor === 'text') {
      valid = typeof raw === 'string' && raw.length <= field.maxLength && !raw.includes('\0');
      message = `Keep this value within ${field.maxLength} characters. The native reader checks its exact format and encoded length.`;
    } else if (editor !== 'boolean') {
      value = Number(String(raw).trim());
      valid = String(raw).trim() !== '' && Number.isFinite(value) && (editor !== 'integer' || Number.isSafeInteger(value));
      const minimum = typeof field.min === 'number' ? field.min : field.type === 'IntProperty' ? -2147483648 : null,
        maximum = typeof field.max === 'number' ? field.max : field.type === 'IntProperty' ? 2147483647 : null;
      if (minimum !== null) valid &&= value >= minimum;
      if (maximum !== null) valid &&= value <= maximum;
      message = editor === 'integer' ? 'Enter a whole number within the field’s allowed range.' : 'Enter a finite number within the field’s allowed range.';
    }
    workshopDrafts.set(property, { asset: state.workshopSelection, property, raw, value, valid });
    state.workshopError = valid ? '' : message;
    $('#workshop-error').textContent = state.workshopError;
    $('#workshop-draft-status').textContent = valid ? 'Waiting to validate changes…' : 'This value needs attention';
    updateWorkshopLocks();
    if (workshopEditTimer !== null) clearTimeout(workshopEditTimer);
    workshopEditTimer = setTimeout(flushWorkshopEdits, 350);
    return valid;
  }
  async function flushWorkshopEdits() {
    if (workshopEditTimer !== null) clearTimeout(workshopEditTimer);
    workshopEditTimer = null;
    if (state.workshopWorking || state.busy) { workshopEditTimer = setTimeout(flushWorkshopEdits, 350); return false; }
    const entry = [...workshopDrafts.values()].find((draft) => draft.valid);
    if (!entry || !state.workshop) return false;
    const result = await workshopMutation('workshopEdit', { asset: entry.asset, property: entry.property, value: entry.value }, 'Validating the changed property…');
    if (result) {
      if (workshopDrafts.get(entry.property) === entry) workshopDrafts.delete(entry.property);
      if (workshopDrafts.size && [...workshopDrafts.values()].some((draft) => draft.valid)) workshopEditTimer = setTimeout(flushWorkshopEdits, 0);
      else paintWorkshop();
      return true;
    }
    const current = workshopDrafts.get(entry.property);
    if (current === entry) entry.valid = false;
    paintWorkshop(); return false;
  }
  async function browseWorkshop(files = null, dropToken = null) {
    if (workshopBlocked() || workshopPending()) return false;
    const open = async () => {
      const context = workshopContext(), generation = ++workshopGeneration;
      state.workshopWorking = true; state.workshopError = ''; paintWorkshop();
      try {
        const result = await run(files ? 'importDropped' : 'workshopBrowse', files ? { target: 'workshop', ...(dropToken ? { dropToken } : {}) } : {}, { objects: files && !dropToken ? files : undefined, refresh: false, activity: true, progress: 'Reading the selected PAK files…',
          onError: (error) => { if (context === workshopContext() && generation === workshopGeneration) state.workshopError = errorText(error); return true; } });
        if (context !== workshopContext() || generation !== workshopGeneration || !result || result.cancelled) return false;
        acceptWorkshopSession(result.session); state.workshopLoaded = true; return true;
      } finally { if (context === workshopContext() && generation === workshopGeneration) { state.workshopWorking = false; paintWorkshop(); } }
    };
    if (workshopNeedsProjectSave()) return reviewWorkshopReplacement(open, 'Open another draft?');
    return open();
  }
  function startWorkshopAsset(skipSaveReview = false) {
    if (workshopBlocked() || workshopPending()) return false;
    if (!skipSaveReview && workshopNeedsProjectSave()) return reviewWorkshopReplacement(() => startWorkshopAsset(true), 'Start another draft?');
    const context = workshopContext();
    modal('Start from an exported asset', '<p>Choose a .uasset exported from your own game, then enter its original game asset path. The path must match the export exactly for the game to load the new PAK.</p><label class="field-label" for="workshop-new-path">Game asset path</label><input class="input full-width" id="workshop-new-path" placeholder="BendGame/Content/…/YourAsset.uasset" maxlength="4096" spellcheck="false"><p class="field-note">Use the virtual path shown by your export tool, not a drive or a local folder path. Assets with separate .uexp or .ubulk data need a supported complete package.</p>' + (state.workshop?.dirty ? '<p>The existing temporary draft is replaced only after this asset has opened successfully.</p>' : '') + '<p class="field-error" id="workshop-new-error" role="alert"></p>', '<button type="button" class="button" data-modal-close>Cancel</button><button type="button" class="button primary" id="workshop-new-choose">Choose exported asset…</button>');
    const generation = modalContentGeneration;
    $('#workshop-new-choose').onclick = async () => {
      if (!$('#modal').open || context !== workshopContext() || generation !== modalContentGeneration || workshopBlocked()) return false;
      const virtualPath = $('#workshop-new-path').value.trim().replace(/\\/g, '/');
      if (!/^BendGame\/Content\/.+\.uasset$/i.test(virtualPath) || virtualPath.includes(':') || /[\x00-\x1f<>"|?*]/.test(virtualPath) || virtualPath.split('/').some((part) => !part || part === '.' || part === '..')) {
        $('#workshop-new-error').textContent = 'Enter its full game path, starting with BendGame/Content/ and ending in .uasset.'; return false;
      }
      const result = await run('workshopNewAssetBrowse', { virtualPath }, { refresh: false, activity: true, progress: 'Reading the exported asset…', onError: (error) => { if (generation === modalContentGeneration) $('#workshop-new-error').textContent = errorText(error); return true; } });
      if (!$('#modal').open || context !== workshopContext() || generation !== modalContentGeneration || !result || result.cancelled) return false;
      acceptWorkshopSession(result.session); state.workshopLoaded = true; $('#modal').close(); paintWorkshop(); return true;
    };
    return true;
  }
  async function newWorkshopPackage(skipSaveReview = false) {
    if (workshopBlocked() || workshopPending()) return false;
    if (!skipSaveReview && workshopNeedsProjectSave()) return reviewWorkshopReplacement(() => newWorkshopPackage(true), 'Start an empty package?');
    const context = workshopContext(), generation = ++workshopGeneration, previous = state.workshop,
      args = previous ? { session: previous.id, revision: previous.revision } : {};
    state.workshopWorking = true; state.workshopError = ''; paintWorkshop();
    try {
      const result = await run('workshopNew', args, { refresh: false, progress: 'Starting a separate package draft…',
        onError: (error) => { if (context === workshopContext() && generation === workshopGeneration) state.workshopError = errorText(error); return true; } });
      if (!result || context !== workshopContext() || generation !== workshopGeneration) return false;
      acceptWorkshopSession(result.session); state.workshopLoaded = true; return true;
    } catch (error) { if (context === workshopContext() && generation === workshopGeneration) state.workshopError = errorText(error); return false; }
    finally { if (context === workshopContext() && generation === workshopGeneration) { state.workshopWorking = false; paintWorkshop(); } }
  }
  function validWorkshopVirtualPath(path) {
    return typeof path === 'string' && path.length > 0 && path.length <= 4096 && !/[\x00-\x1f\x7f\\:<>"|?*]/.test(path) &&
      !path.startsWith('/') && path.split('/').every((part) => part && part !== '.' && part !== '..');
  }
  function openWorkshopFileInput(mode) {
    if (workshopBlocked() || workshopPending() || !state.workshop || !['add', 'replace', 'rename'].includes(mode)) return false;
    const asset = state.workshopAsset, session = state.workshop.id, revision = state.workshop.revision,
      context = workshopContext(), requestGeneration = workshopGeneration;
    if (mode !== 'add' && (!asset || asset.id !== state.workshopSelection)) return false;
    const rename = mode === 'rename', add = mode === 'add', title = add ? 'Add a package file' : rename ? 'Rename this asset family' : 'Replace this package file';
    modal(title, `<div class="workshop-file-dialog"><p>${add ? 'Choose a file exported from your own game or prepared for this mod. Enter its exact virtual package path; this is not a folder on your computer.' : rename ? 'Move this file and its companion paths together in the draft. This does not rewrite references inside cooked assets, so the new path still needs to match the content you are making.' : `Replace the current bytes of <strong>${escape(asset.path)}</strong>. Its package path stays the same. Companion files are left in place, so choose matching data.`}</p>${add || rename ? `<label class="field-label" for="workshop-file-path">Game-relative package path</label><input class="input full-width" id="workshop-file-path" maxlength="4096" value="${escape(rename ? asset.path : '')}" placeholder="BendGame/Content/…/YourFile.uasset" spellcheck="false" autocomplete="off">` : ''}<p class="field-note">${rename ? 'Keep its file extension. You can undo the change or reopen the original package.' : 'Raw files can be up to128MiB each, within the draft’s256MiB working-data budget. Unsupported layouts can be kept as whole files; they do not become editable properties.'}</p><p class="field-error" id="workshop-file-error" role="alert"></p></div>`, `<button type="button" class="button" data-modal-close>Cancel</button><button type="button" class="button primary" id="workshop-file-confirm">${rename ? 'Rename family' : 'Choose file…'}</button>`);
    const generation = modalContentGeneration, button = $('#workshop-file-confirm');
    let used = false;
    const ownsDialog = () => $('#modal').open === true && context === workshopContext() && generation === modalContentGeneration && $('#workshop-file-confirm') === button;
    button.onclick = async () => {
      if (used || !ownsDialog() || requestGeneration !== workshopGeneration || workshopBlocked() || workshopPending() ||
          state.workshop?.id !== session || state.workshop.revision !== revision || mode !== 'add' && state.workshopSelection !== asset.id) return false;
      const path = add || rename ? $('#workshop-file-path').value.trim().replace(/\\/g, '/') : '';
      if ((add || rename) && !validWorkshopVirtualPath(path)) { $('#workshop-file-error').textContent = 'Use a complete virtual file path with forward slashes, without drive letters or parent folders.'; return false; }
      used = true; button.disabled = true;
      const action = add ? 'workshopAddFileBrowse' : rename ? 'workshopRenameFile' : 'workshopReplaceFileBrowse',
        args = add ? { virtualPath: path } : rename ? { asset: asset.id, virtualPath: path } : { asset: asset.id };
      const result = await workshopMutation(action, args, rename ? 'Checking the renamed asset family…' : 'Checking the selected package file…');
      if (!result || result.cancelled) {
        used = false;
        if (ownsDialog()) { button.disabled = false; if (!result) $('#workshop-file-error').textContent = state.workshopError || 'The draft was not changed. Check the file and try again.'; }
        return false;
      }
      if (context === workshopContext() && requestGeneration === workshopGeneration && state.workshop?.id === session && result.asset) {
        state.workshopSelection = result.asset.id; state.workshopAsset = result.asset;
        if (add) { state.workshopChoicesOnly = false; state.workshopSearch = ''; }
        if (!result.asset.merge?.supported) state.workshopMerge = null;
      }
      const reveal = ownsDialog();
      if (reveal) $('#modal').close();
      paintWorkshop();
      if (reveal && state.view === 'workshop') {
        const detail = $('#workshop-detail'), reduced = state.snapshot.settings?.reducedMotion || window.matchMedia?.('(prefers-reduced-motion: reduce)')?.matches;
        detail?.focus?.({ preventScroll: true }); detail?.scrollIntoView?.({ block: 'nearest', behavior: reduced ? 'auto' : 'smooth' });
      }
      return true;
    };
    return true;
  }
  function changeWorkshopFile(operation) {
    const asset = state.workshopAsset;
    if (workshopBlocked() || workshopPending() || !state.workshop || !asset || asset.id !== state.workshopSelection ||
        !['remove', 'restore', 'reset'].includes(operation)) return false;
    const context = workshopContext(), session = state.workshop.id, revision = state.workshop.revision, id = asset.id,
      remove = operation === 'remove', restore = operation === 'restore';
    confirmModal(remove ? 'Remove this file from the draft?' : restore ? 'Restore this draft file?' : 'Use the opened file bytes again?',
      `<p><strong>${escape(asset.path)}</strong></p><p>${remove ? 'Exclude only this file from the exported PAK. Its companion data may also need removing. The entry stays available for Restore file.' : restore ? 'Include this file again, with its current draft bytes. Its output path is checked for duplicates before anything changes.' : 'Discard this file’s replacement and property edits, using the bytes originally opened or added to this draft. Its current package path stays the same.'}</p><p>Original PAKs and installed mods stay unchanged.</p>`, remove ? 'Remove from draft' : restore ? 'Restore file' : 'Use opened bytes', async () => {
        if (context !== workshopContext() || state.workshop?.id !== session || state.workshop.revision !== revision || state.workshopSelection !== id || workshopBlocked() || workshopPending()) return false;
        return !!await workshopMutation(restore ? 'workshopRestoreFile' : remove ? 'workshopRemoveFile' : 'workshopResetFile', { asset: id }, 'Checking the draft file…');
      });
    return true;
  }
  async function extractWorkshopFile() {
    if (workshopBlocked() || workshopPending() || !state.workshopAsset || !state.workshop) return false;
    const context = workshopContext(), session = state.workshop.id, revision = state.workshop.revision, asset = state.workshopAsset.id,
      originalModal = modalContentGeneration;
    const review = await workshopMutation('workshopExtractReview', { asset }, 'Checking this file for export…');
    if (!review || context !== workshopContext() || state.view !== 'workshop' || originalModal !== modalContentGeneration ||
        state.workshop?.id !== session || state.workshop.revision !== revision || state.workshopSelection !== asset || review.session !== session ||
        review.revision !== revision || review.asset !== asset || typeof review.token !== 'string' || !review.token || review.token.length > 512 ||
        !Number.isSafeInteger(review.bytes) || review.bytes < 0 || !/^[a-f0-9]{64}$/.test(review.sha256 || '')) return false;
    const displayName = typeof review.displayName === 'string' && review.displayName.length > 0 && review.displayName.length <= 255 &&
      !/[\x00-\x1f\x7f/\\:<>"|?*]/.test(review.displayName) ? review.displayName : 'ExportedAsset.bin';
    modal('Export this package file?', `<div class="workshop-file-dialog"><p><strong>${escape(displayName)}</strong> · ${formatBytes(review.bytes)}</p><p>This exports the current draft bytes as a separate file. Existing files, source PAKs and the game stay in place.</p><p class="field-note">SHA256: <code>${escape(review.sha256)}</code></p><p class="field-error" id="workshop-file-export-error" role="alert"></p></div>`, '<button type="button" class="button" data-modal-close>Cancel</button><button type="button" class="button primary" id="workshop-file-export-confirm">Choose where to save…</button>');
    const generation = modalContentGeneration, button = $('#workshop-file-export-confirm'); let used = false;
    const current = () => $('#modal').open === true && context === workshopContext() && generation === modalContentGeneration && $('#workshop-file-export-confirm') === button;
    button.onclick = async () => {
      if (used || !current() || state.workshop?.id !== session || state.workshop.revision !== revision || workshopBlocked() || workshopPending()) return false;
      used = true; button.disabled = true;
      const result = await run('workshopExtractFileBrowse', { token: review.token, displayName }, { refresh: false, activity: true, progress: 'Exporting the checked package file…',
        onError: (error) => { if (current()) $('#workshop-file-export-error').textContent = errorText(error) + ' Close this review and check the current file before trying again.'; return true; } });
      if (result?.cancelled) { used = false; if (current()) button.disabled = false; return false; }
      if (!result?.exported) return false;
      if (current()) $('#modal').close();
      return true;
    };
    return true;
  }
  async function chooseWorkshopSource(source) {
    const asset = state.workshopAsset, session = state.workshop;
    if (!asset || !session || workshopBlocked() || workshopPending() || !asset.sourceIds?.includes(source)) return false;
    const context = workshopContext(), id = session.id, revision = session.revision, assetId = asset.id,
      index = asset.sourceIds.indexOf(source), name = asset.sourceNames?.[index] || 'this source package';
    confirmModal('Keep this complete asset version?', `<p>Use <strong>${escape(name)}</strong> for ${escape(asset.path)} and its companion data.</p><p>This chooses the entire asset family. Changes from the other versions of this asset are discarded from the draft. Any edits already made to this asset in the draft are discarded too; original PAKs stay in place. Undo is available after the change.</p>`, 'Use this source', async () => {
      if (context !== workshopContext() || state.workshop?.id !== id || state.workshop.revision !== revision || state.workshopSelection !== assetId) { toast('The draft changed. Review the source choice again.', 'warning'); return; }
      await workshopMutation('workshopChooseSource', { asset: assetId, source }, 'Checking the chosen asset family…');
    });
    return true;
  }
  async function compareWorkshopBaseline() {
    if (workshopBlocked() || workshopPending() || !state.workshopAsset || (state.workshopAsset.sourceIds?.length || 0) < 2 || state.workshopAsset.removed || state.workshopAsset.replaced) return false;
    const asset = state.workshopAsset.id;
    const result = await workshopMutation('workshopMergeBaselineBrowse', { asset }, 'Comparing all source versions with the original asset…');
    if (!result || result.cancelled) return false;
    workshopMergeChoices.clear(); state.workshopMerge = { ...result.merge, asset }; paintWorkshop(); return true;
  }
  async function resolveWorkshopMerge() {
    const merge = state.workshopMerge;
    if (!merge?.supported || workshopBlocked() || workshopDrafts.size > 0 || workshopEditTimer !== null || merge.asset !== state.workshopSelection) return false;
    const conflicts = Array.isArray(merge.conflicts) ? merge.conflicts : [], choices = Object.create(null), inputs = new Map();
    for (const input of $$('[data-workshop-merge]')) if (input.checked || input.tagName === 'SELECT') inputs.set(input.dataset.workshopMerge, input);
    for (const field of conflicts) {
      const selected = inputs.get(field.property);
      if (selected && workshopMergeOptions(field).some((option) => option.id === selected.value)) choices[field.property] = selected.value;
    }
    if (!Object.keys(choices).length) { state.workshopError = 'Choose at least one conflicting value to apply.'; $('#workshop-error').textContent = state.workshopError; return false; }
    const result = await workshopMutation('workshopResolveMerge', { asset: merge.asset, choices }, 'Checking the chosen property values…');
    if (result) { workshopMergeChoices.clear(); const remaining = result.merge || result.asset?.merge; state.workshopMerge = remaining?.supported ? { ...remaining, asset: merge.asset } : null; paintWorkshop(); return true; }
    return false;
  }
  async function reviewWorkshopBuild() {
    if (workshopBlocked() || workshopPending() || state.workshop?.canBuild !== true) return false;
    const context = workshopContext(), id = state.workshop.id, revision = state.workshop.revision,
      modalGeneration = modalContentGeneration;
    const review = await workshopMutation('workshopBuildReview', {}, 'Building and checking a separate PAK draft…');
    if (!review || state.view !== 'workshop' || context !== workshopContext() || state.workshop?.id !== id || state.workshop.revision !== revision || modalGeneration !== modalContentGeneration ||
        review.session !== id || review.revision !== revision || typeof review.token !== 'string' || !review.token || review.token.length > 512) return false;
    const changes = Array.isArray(review.changedProperties) ? review.changedProperties.length : Number(review.changedProperties) || 0;
    modal('Export this PAK?', `<div class="workshop-build-review"><p>Checked ${Number(review.assetCount) || 0} assets in this draft.</p><dl class="detail-grid"><dt>New package</dt><dd>${formatBytes(Number(review.bytes) || 0)}</dd><dt>Changed properties</dt><dd>${changes}</dd><dt>Sources</dt><dd>${escape((Array.isArray(review.sourceNames) ? review.sourceNames : []).join(', '))}</dd></dl>${workshopWarnings(review.warnings)}<p>Export writes a separate PAK. Original packages, installed mods and game saves stay in place. Import and review the new package in My mods when you want to test it.</p><p class="field-error" id="workshop-export-error" role="alert"></p></div>`, '<button type="button" class="button" data-modal-close>Cancel</button><button type="button" class="button primary" id="workshop-export">Choose where to save…</button>');
    const generation = modalContentGeneration;
    let used = false;
    $('#workshop-export').onclick = async () => {
      if (used || !$('#modal').open || context !== workshopContext() || generation !== modalContentGeneration || state.workshop?.id !== id || state.workshop.revision !== revision || workshopPending()) return false;
      used = true; $('#workshop-export').disabled = true;
      const result = await run('workshopExportBrowse', { token: review.token }, { refresh: false, activity: true, progress: 'Saving the verified PAK…',
        onError: (error) => { if (generation === modalContentGeneration) $('#workshop-export-error').textContent = errorText(error) + ' Close this window and review the build again before retrying.'; return true; } });
      if (context !== workshopContext() || generation !== modalContentGeneration) return false;
      if (result?.cancelled) { used = false; $('#workshop-export').disabled = false; return false; }
      if (result?.exported !== true) return false;
      const saved = workshopSession(result.session);
      if (saved && saved.id === id && saved.revision === revision) acceptWorkshopSession(saved);
      $('#modal').close(); paintWorkshop();
      toast(`PAK exported${result.name ? ': ' + result.name : '.'} Import it in My mods to review installation.`, 'success'); return true;
    };
    return true;
  }
  async function resetWorkshopValue(property) {
    const field = state.workshopAsset?.fields?.find((item) => item.id === property);
    if (!field || field.changed !== true || workshopBlocked() || workshopPending() ||
        state.workshopMerge?.conflicts?.length > 0) return false;
    return !!await workshopMutation('workshopResetValue', { asset: state.workshopSelection, property }, 'Restoring the opened source value…');
  }
  async function reviewWorkshopChanges() {
    if (workshopBlocked() || workshopPending() || !state.workshop) return false;
    const context = workshopContext(), id = state.workshop.id, revision = state.workshop.revision, generation = modalContentGeneration;
    state.workshopWorking = true; state.workshopError = ''; paintWorkshop();
    let result;
    try {
      result = await run('workshopChanges', { session: id }, { refresh: false, activity: false, progress: 'Reading draft changes…',
        onError: (error) => { if (context === workshopContext()) state.workshopError = errorText(error); return true; } });
    } finally { if (context === workshopContext()) { state.workshopWorking = false; paintWorkshop(); } }
    if (!result || context !== workshopContext() || state.view !== 'workshop' || generation !== modalContentGeneration ||
        state.workshop?.id !== id || state.workshop.revision !== revision || result.session !== id || result.revision !== revision ||
        !Array.isArray(result.changes) || !Array.isArray(result.families)) return false;
    const changes = result.changes.slice(0, 256), families = result.families.slice(0, 256), files = Array.isArray(result.files) ? result.files.slice(0, 256) : [];
    const fileMarkup = files.length ? `<h3>Package files</h3><div class="workshop-file-changes">${files.map((file) => `<article><strong>${escape(file.path || 'Package file')}</strong><p>${[file.added ? 'Added file' : '', file.replaced ? 'Replaced bytes' : '', file.removed ? 'Removed from output' : '', file.renamed ? 'Renamed path' : ''].filter(Boolean).join(' · ')}</p>${file.renamed ? `<p>Opened path: <code>${escape(file.originalPath || '')}</code></p>` : ''}</article>`).join('')}</div>` : '';
    modal('Changes in this draft', `<div class="workshop-changes-review"><p>${Number(result.changeCount) || 0} changed properties. Original packages are kept unchanged. Export creates a separate PAK.</p>${fileMarkup}${result.truncated ? '<p class="field-note">This display is limited to the first 256 entries. The count covers the complete draft.</p>' : ''}${changes.length ? `<div class="workshop-change-list">${changes.map((change) => `<article><h3>${escape(change.name || change.property)}</h3><p>${escape(change.path)}</p><dl><div><dt>Opened source</dt><dd>${escape(String(change.before))}</dd></div><div><dt>Draft</dt><dd>${escape(String(change.after))}</dd></div></dl></article>`).join('')}</div>` : '<p>No changed scalar properties are recorded.</p>'}${families.length ? `<h3>Complete asset choices</h3>${families.map((family) => `<div class="workshop-family-change"><strong>${escape(family.path)}</strong><p>Kept: ${escape(family.keptSource || 'Source package')}</p>${Array.isArray(family.discardedSources) && family.discardedSources.length ? `<p>Other versions excluded from this draft: ${escape(family.discardedSources.join(', '))}</p>` : ''}</div>`).join('')}` : ''}<p class="field-note">An asset choice keeps the whole family. It is separate from merging individual properties.</p></div>`, '<button type="button" class="button" data-modal-close>Close</button>');
    return true;
  }
  function closeWorkshop() {
    if (!state.workshop || workshopBlocked() || workshopPending()) return false;
    const context = workshopContext(), session = state.workshop.id, revision = state.workshop.revision;
    const close = async () => {
      if (context !== workshopContext() || state.workshop?.id !== session || state.workshop.revision !== revision) return false;
      const result = await workshopMutation('workshopClose', {}, 'Closing the temporary draft…');
      if (result) { clearWorkshopDraftInputs(); paintWorkshop(); return true; } return false;
    };
    if (workshopNeedsProjectSave()) reviewWorkshopReplacement(close, 'Close this draft?');
    else close();
    return true;
  }
  function bindWorkshop() {
    if (state.view !== 'workshop') return;
    $$('[data-workshop-filter]').forEach((control) => { control.onclick = () => {
      if (workshopPending() || workshopBlocked()) return;
      state.workshopChoicesOnly = control.dataset.workshopFilter === 'choices'; state.workshopSearch = ''; paintWorkshop();
    }; });
    const search = $('#workshop-search');
    if (search) search.oninput = () => { state.workshopSearch = search.value; $('#workshop-assets').innerHTML = workshopAssetRows(); bindWorkshopAssets(); };
    const propertySearch = $('#workshop-property-search');
    if (propertySearch) propertySearch.oninput = () => {
      state.workshopPropertySearch = propertySearch.value;
      const focus = rememberMainFocus(); $('#workshop-detail').innerHTML = renderWorkshopDetail(); bindWorkshop(); restoreMainFocus(focus);
    };
    bindWorkshopAssets();
    $$('[data-workshop-field]').forEach((input) => { input.oninput = () => queueWorkshopEdit(input.dataset.workshopField, input); });
    $$('[data-workshop-reset]').forEach((control) => { control.onclick = () => resetWorkshopValue(control.dataset.workshopReset); });
    $$('[data-workshop-merge]').forEach((input) => { input.onchange = () => { if (input.checked || input.tagName === 'SELECT') { if (input.value) workshopMergeChoices.set(input.dataset.workshopMerge, input.value); else workshopMergeChoices.delete(input.dataset.workshopMerge); } updateWorkshopLocks(); }; });
    $$('[data-workshop-source]').forEach((control) => { control.onclick = () => chooseWorkshopSource(control.dataset.workshopSource); });
    if ($('#workshop-baseline')) $('#workshop-baseline').onclick = compareWorkshopBaseline;
    if ($('#workshop-merge-apply')) $('#workshop-merge-apply').onclick = resolveWorkshopMerge;
    updateWorkshopLocks();
  }
  function bindWorkshopAssets() {
    $$('[data-workshop-asset]').forEach((control) => { control.onclick = () => selectWorkshopAsset(control.dataset.workshopAsset); });
  }
  function renderHelp() {
    const articles = window.FIELD_GUIDE || [];
    if (state.article) {
      const article = articles.find((item) => item.id === state.article);
      if (article) {
        const chapters = articles.filter((item) => item.modding).sort((a, b) => (a.chapter || 0) - (b.chapter || 0)),
          index = chapters.findIndex((item) => item.id === article.id),
          previous = chapters[index - 1],
          next = chapters[index + 1];
        return `<div class="article-wrap"><button class="back-link" data-action="guide-back">${icon('back')}Back to ${article.modding ? 'Make mods' : 'the field guide'}</button><article class="article"><div class="eyebrow">${icon(article.icon)}${article.modding ? `CHAPTER ${escape(article.chapter)} · ` : ''}${escape(article.category)}</div><h1>${escape(article.title)}</h1><p class="article-intro">${escape(article.summary)}</p><div class="article-body">${guideBody(article)}</div>${renderResourceButtons(article)}</article>${article.modding ? `<div class="chapter-navigation">${previous ? `<button class="button" data-article="${escape(previous.id)}">${icon('back')}Previous chapter</button>` : '<span></span>'}${next ? `<button class="button primary" data-article="${escape(next.id)}">Next: ${escape(next.title)}${icon('arrow')}</button>` : '<button class="button" data-help-section="modding">All chapters</button>'}</div>` : `<div class="action-strip">${button('Run diagnostics', 'diagnose', 'wrench')}${button('Export report', 'export', 'report')}</div>`}</div>`;
      }
    }
    const learning = state.helpSection === 'modding',
      filtered = visibleGuideArticles();
    return `${header('THE FIELD GUIDE', learning ? 'Make mods for Days Gone.' : 'Help and troubleshooting.', learning ? 'A beginner’s guide to the actual file workflow, from finding an asset to testing a standalone package.' : 'Installation steps, known mod conflicts and answers to common questions.')}${guideSectionTabs()}${learning ? `<div class="modding-intro"><span class="learning-emblem">${icon('layers')}</span><div><h2>Start with one small change.</h2><p>Read the chapters in order if you’re new. Existing cooked assets, uncooked projects and new content need different tools; the guide explains those limits before you start.</p><p class="field-note">This guide explains the workflow. Open Workshop to edit supported properties or prepare a separate combined PAK.</p><div class="inline-actions mt-16"><button type="button" class="button" data-action="workshop-enter">${icon('wrench')}Open workshop</button></div></div></div>` : `<section class="learning-feature"><div class="learning-feature-art" aria-hidden="true">${icon('layers')}${icon('wrench')}</div><div><span class="eyebrow">NEW TO MAKING MODS?</span><h2>Make mods for Days Gone</h2><p>Learn how the files fit together, where to get the tools and how to check a small standalone change.</p><button class="button primary" data-help-section="modding">Open the beginner guide ${icon('arrow')}</button></div></section>`}<div class="search-wrap guide-search">${icon('search')}<input id="guide-search" class="search-input" type="search" placeholder="${learning ? 'Search the modding chapters…' : 'Search the field guide…'}" aria-label="${learning ? 'Search modding chapters' : 'Search help articles'}" value="${escape(state.helpSearch)}"></div><div class="${learning ? 'modding-grid' : 'guide-grid'}" id="guide-grid">${learning ? renderModdingChapters(filtered) : renderGuideCards(filtered)}</div>${learning ? '' : `<div class="info-card mt-24"><div class="info-card-head">${icon('wrench')}Need to report a problem?</div><p>Run diagnostics and export the report. Include what you tried and the exact error when you contact the author on Nexus Mods.</p><div class="inline-actions mt-16">${button('Run diagnostics', 'diagnose', 'wrench', 'small')}${button('Open mod page', 'nexus', 'external', 'small')}</div></div>`}`;
  }
  function renderGuideCards(articles) {
    return articles.length
      ? articles
          .map(
            (article) =>
              `<button class="guide-card" data-article="${escape(article.id)}"><span class="guide-icon">${icon(article.icon)}</span><span class="guide-category">${escape(article.category)}</span><span class="guide-arrow">${icon('arrow')}</span><h2>${escape(article.title)}</h2><p>${escape(article.summary)}</p></button>`
          )
          .join('')
      : '<div class="empty-state compact"><h3>No matching articles</h3><p>Try “conflict”, “install” or “antivirus”.</p></div>';
  }

  async function loadStorageStatus() {
    if (state.preview || state.storageLoading) return;
    if (state.snapshot.sandbox) { state.storage = { unavailable: true }; if (state.view === 'settings') render(); return; }
    state.storageLoading = true;
    try { state.storage = await request('storageStatus'); }
    catch (error) { state.storage = { error: errorText(error), unavailable: true }; }
    finally { state.storageLoading = false; if (state.view === 'settings') render(); }
  }
  function storageMoveBlocked() {
    return !!(state.preview || state.busy || state.nativeDropActivity?.running || state.profileSession?.active ||
      state.snapshot.recoveryRequired || state.snapshot.finder?.active || state.storage?.pending);
  }
  function renderStoragePanel() {
    const value = state.storage, pendingMove = value?.pending;
    const message = value?.error ? `<p class="field-note" role="status">${escape(value.error)}</p>` : '';
    return `<section class="panel settings-full"><div class="panel-heading"><div><h2>Toolkit data</h2><p>Keep your collection on the drive that has room for it.</p></div>${icon('folder')}</div><div class="panel-body"><div class="setting-row"><div class="setting-copy"><strong>Data folder</strong><p class="path-text">${escape(value?.path || (state.preview ? 'Your local user data folder' : 'Reading the current location…'))}</p><p>Library packages, profiles, saved Workshop projects, save backups, history and interface data are stored here. Moving them leaves installed game mods and game saves in place.</p></div><div class="inline-actions"><button type="button" class="button small" data-action="storage-open" ${!value?.path || state.preview ? 'disabled' : ''}>${icon('folder')}Open</button><button type="button" class="button small" data-action="storage-browse" ${!value?.path || storageMoveBlocked() ? 'disabled' : ''}>${icon('folder')}Move data</button></div></div>${message}${pendingMove ? `<div class="banner neutral"><p><strong>${pendingMove.phase === 'activated' ? 'The new data folder is active.' : 'Data move waiting for restart.'}</strong> ${pendingMove.phase === 'activated' ? 'Some original files need review before they can be removed. Your new collection remains available.' : 'Close every toolkit window and command, then open it again. The copy is checked before the new location is used.'}<br><span class="path-text">${escape(pendingMove.phase === 'activated' ? pendingMove.source || '' : pendingMove.destination || '')}</span></p><button type="button" class="button small" data-action="storage-cancel" ${state.busy || pendingMove.phase === 'activated' ? 'disabled' : ''}>Cancel move</button></div>` : ''}<p class="field-note">A small location record stays in your user data folder. Temporary files follow your operating system’s temp location. Use this loader version or newer with a relocated collection.</p><details><summary>Portable use and removing the toolkit</summary><p>The app runs from its extracted folder and has no traditional uninstall process. Its data is separate. Close Days Gone and finish any active profile session so your original mod setup is restored. Then close the loader and delete the extracted app folder to remove the program. Installed game mods and game saves remain in place.</p><p>If you also want to remove its library, profiles and backups, keep anything you need first and remove the data folder shown above after closing the loader. Disconnect Nexus and unregister Nexus download links first if you enabled those features. Use the toolkit’s normal mod and mod-loading controls to undo game changes before removing the app.</p></details></div></section>`;
  }
  async function chooseStorageLocation() {
    if (storageMoveBlocked()) return false;
    const plan = await run('storageBrowse', {}, { refresh: false, progress: 'Checking the data location…' });
    if (!plan || plan.cancelled || typeof plan.token !== 'string') return false;
    confirmModal('Move toolkit data?', `<p>Move ${Number(plan.files) || 0} files (${escape(formatBytes(Number(plan.bytes) || 0))}) after the toolkit closes.</p><p><strong>From</strong><br><span class="path-text">${escape(plan.from || '')}</span></p><p><strong>To</strong><br><span class="path-text">${escape(plan.to || '')}</span></p><p>The original data stays until the new copy has passed verification. Installed game mods and game saves stay where they are. Close and reopen the loader to finish.</p>`, 'Move on next start', async () => {
      if (storageMoveBlocked()) { toast('Finish the current operation before moving toolkit data.', 'warning'); return; }
      const result = await run('storageSchedule', { token: plan.token, confirmed: true }, { refresh: false,
        progress: 'Saving the data move…', success: 'Data move saved. Close and reopen the toolkit to finish.' });
      if (result) { state.storage = result; if (state.view === 'settings') render(); }
    });
    return true;
  }
  function renderSettings() {
    if (!state.storage && !state.storageLoading && !state.preview) setTimeout(loadStorageStatus, 0);
    const s = { ...state.snapshot.settings, ...pendingAutoSavePreferences, ...appearancePreview };
    return `${header('SETTINGS', 'Set up the toolkit.', 'Choose your game folder, display size, themes and download preferences.')}${renderSettingsBrowser()}<form id="settings-form"><div class="page-grid"><section class="panel settings-full"><div class="panel-heading"><div><h2>Game & folders</h2><p>The toolkit manages local PAK packages for this installation.</p></div>${icon('folder')}</div><div class="panel-body"><label class="field-label" for="game-path">Days Gone game folder</label><div class="path-field"><input class="input" id="game-path" name="gamePath" aria-describedby="game-path-hint game-path-error" aria-invalid="false" placeholder="Choose the folder containing DaysGone.exe" value="${escape(gamePathFieldValue())}" spellcheck="false">${button('Browse', 'browse-game', 'folder')}<button type="button" class="button primary" id="game-path-save" data-save-setting hidden>${icon('check')}Save</button></div><p class="field-error" id="game-path-error" role="alert" hidden></p><p class="field-note" id="game-path-hint">Mods folder: <code>${escape(modsPathLabel())}</code>. The game folder above is used to launch Days Gone.</p>${state.snapshot.steamCopy ? `<div class="setting-row"><div class="setting-copy"><label for="steam-launch-setting">Start the game through Steam</label><p>On: starts like Steam’s Play button, with Steam Cloud saves and the overlay. Off: starts DaysGone.exe directly.</p></div><span class="inline-save" id="steam-launch-save" hidden><small>Not saved yet</small><button type="button" class="button small primary" data-save-setting>Save</button></span><button type="button" id="steam-launch-setting" class="switch" role="switch" aria-checked="${s.steamLaunch !== false}" aria-label="Start the game through Steam"></button></div>` : ''}</div></section>${renderAppearanceSettings(s)}<section class="panel"><div class="panel-heading"><div><h2>Atmosphere</h2><p>Seasonal lighting, corner details and optional falling emoji.</p></div>${icon('moon')}</div><div class="panel-body"><div class="setting-row"><div class="setting-copy"><label for="seasonal-setting">Seasonal themes</label><p>Automatically follow the season using your local date. Enabled by default.</p></div><button type="button" id="seasonal-setting" class="switch" role="switch" aria-checked="${s.seasonalThemes !== false}" aria-label="Seasonal themes"></button></div><div class="setting-row"><div class="setting-copy"><label for="effects-setting">Falling seasonal emoji</label><p>A few seasonal emoji drift across the window.</p><p id="seasonal-effects-status" role="status">${escape(seasonalEffectsStatus(s, activeSeason(s)))}</p></div><button type="button" id="effects-setting" class="switch" role="switch" aria-checked="${s.seasonalEffects !== false}" aria-label="Falling seasonal emoji"></button></div><div class="setting-row"><div class="setting-copy"><label for="theme-setting">Theme</label><p>Choose a favorite atmosphere or the original rust-and-ink palette.</p></div><select id="theme-setting" name="theme">${[
      ['auto', 'Follow the season'],
      ['default', 'Original'],
      ['halloween', 'Late October'],
      ['winter', 'Winter'],
      ['newyear', 'New Year’s Eve'],
      ['spring', 'Spring'],
      ['summer', 'Summer'],
      ['autumn', 'Autumn']
    ]
      .map(([v, l]) => `<option value="${v}" ${s.theme === v ? 'selected' : ''}>${l}</option>`)
      .join(
        ''
      )}</select></div></div></section><section class="panel"><div class="panel-heading"><div><h2>Comfort & accessibility</h2><p>Keep the important things easy to read.</p></div>${icon('user')}</div><div class="panel-body"><div class="setting-row"><div class="setting-copy"><label for="motion-setting">Reduce motion</label><p>Disable interface animation. Your system motion preference is always respected.</p></div><button type="button" id="motion-setting" class="switch" role="switch" aria-checked="${!!s.reducedMotion}" aria-label="Reduce motion"></button></div><div class="setting-row"><div class="setting-copy"><label for="contrast-setting">Higher contrast</label><p>Brighter text and clearer panel boundaries. Seasonal decoration is hidden.</p></div><button type="button" id="contrast-setting" class="switch" role="switch" aria-checked="${!!s.highContrast}" aria-label="Higher contrast"></button></div><div class="setting-row"><div class="setting-copy"><label for="auto-scale-setting">Automatic interface size</label><p>Size controls and text for the current monitor, with room to scroll in smaller windows. System scaling is already included.</p><div class="effective-scale" id="effective-scale-value">${escape(scaleStatus(s))}</div></div><button type="button" id="auto-scale-setting" class="switch" role="switch" aria-checked="${s.autoScale !== false}" aria-label="Automatic interface size"></button></div><div class="setting-row"><div class="setting-copy"><label for="interface-scale-setting">Manual interface size</label><p>Choose a size to turn off Auto. Your manual choice stays saved while Auto is on. Ctrl + + / − adjusts size; Ctrl + 0 selects manual 100%.</p></div><select id="interface-scale-setting" name="interfaceScale">${interfacePresets.map((v) => `<option value="${v}" ${Number(s.interfaceScale || 100) === v ? 'selected' : ''}>${v}%${v === 100 ? ' · Default' : ''}</option>`).join('')}</select></div><div class="setting-row"><div class="setting-copy"><label for="scale-setting">Text size</label><p>Fine-tune text after automatic sizing. Interface sizing also scales every label and paragraph.</p></div><select id="scale-setting" name="fontScale">${[
      [90, 'Smaller · 90%'],
      [100, 'Default · 100%'],
      [110, 'Larger · 110%'],
      [125, 'Large · 125%'],
      [150, 'Largest · 150%']
    ]
      .map(([v, l]) => `<option value="${v}" ${Number(s.fontScale || 100) === v ? 'selected' : ''}>${l}</option>`)
      .join(
        ''
      )}</select></div></div></section>${localEdition() ? renderLocalNexusPanel() : `<section class="panel settings-full"><div class="panel-heading"><div><h2>Nexus Mods connection</h2><p>Sign in through the official Nexus website. Your operating system protects your saved authorization.</p></div>${icon('lock')}</div><div class="panel-body"><div class="setting-row"><div class="setting-copy"><strong>${state.nexus.connected ? (state.nexus.tierVerified === false || state.nexus.offline ? 'Saved connection · verification unavailable' : 'Connected to Nexus Mods') : 'Connect when you’re ready'}</strong><p>${state.nexus.connected ? `${escape(state.nexus.name || 'Nexus account')} · ${nexusTier()}${state.nexus.tierVerified === false ? ' · retry to verify membership' : state.nexus.premium ? ' · API downloads available' : ' · browser authorization required for downloads'}` : 'A connection allows release checks. Nexus Mods Premium supports automated downloads; free accounts use Nexus’s browser download flow.'}</p></div>${state.nexus.connected ? `<div class="inline-actions">${button('Refresh connection', 'nexus-status', 'refresh', 'small')}${button('Disconnect', 'nexus-disconnect', 'lock', 'small')}</div>` : button(state.nexus.requiresReconnect ? 'Reconnect account' : 'Connect account', 'nexus-connect', 'lock', 'small')}</div>${renderNxmSetting()}<div class="setting-row"><div class="setting-copy"><label for="updates-setting">Check for mod updates on startup</label><p>Check once when the toolkit starts and after reconnecting. New packages still need inspection before installation.</p></div><button type="button" id="updates-setting" class="switch" role="switch" aria-checked="${!!s.autoCheckUpdates}" aria-label="Check for mod updates on startup"></button></div></div></section>`}<section class="panel settings-full"><div class="panel-heading"><div><h2>Library preferences</h2><p>Keep the collection comfortable to browse.</p></div>${icon('grid')}</div><div class="panel-body"><div class="setting-row"><div class="setting-copy"><label for="compact-setting">Compact mod rows</label><p>Show more of your collection at once with smaller row spacing.</p></div><button type="button" id="compact-setting" class="switch" role="switch" aria-checked="${!!s.compactMode}" aria-label="Compact mod rows"></button></div><div class="setting-row"><div class="setting-copy"><label for="auto-manage-setting">Manage hand-installed PAKs automatically</label><p>New PAK files in Saved/Paks are taken over without asking, as if you clicked Manage all. The toolkit can then disable or remove them like its own mods. Switch this off if another tool looks after those files.</p></div><span class="inline-save" id="auto-manage-save" hidden><small>Not saved yet</small><button type="button" class="button small primary" data-save-setting>Save</button></span><button type="button" id="auto-manage-setting" class="switch" role="switch" aria-checked="${s.autoManageExternal !== false}" aria-label="Manage hand-installed PAKs automatically"></button></div></div></section>${renderStoragePanel()}${renderSavesSettingsLink()}${renderLibraryBackupPanel()}${renderUpdatePanel()}${renderCatalogPanel()}<section class="panel settings-full"><div class="panel-heading"><div><h2>Maintenance</h2><p>Check your setup and gather information when something goes wrong.</p></div>${icon('wrench')}</div><div class="panel-body"><div class="inline-actions">${button('Run diagnostics', 'diagnose', 'wrench')}${button('Export report', 'export', 'report')}${button('Open mods folder', 'open-folder', 'folder')}${button('Open Saves', 'save-backups', 'save')}</div><p class="field-note">Personal paths are removed. Review the report before sharing.</p></div></section></div><div class="settings-actions"><p id="settings-note">Settings save by themselves. A changed game folder, Steam start or automatic managing shows its own Save button.</p><button type="button" class="text-action" id="settings-error-link" hidden>Go to game folder</button></div></form>`;
  }
  const settingsGroups = [
    ['all', 'All settings'], ['appearance', 'Appearance'], ['game', 'Game & mods'],
    ['backups', 'Backups'], ['downloads', 'Downloads'], ['toolkit', 'Toolkit']
  ];
  function renderSettingsBrowser() {
    return `<div class="settings-browser"><label class="settings-search" for="settings-search">${icon('search')}<span class="sr-only">Find a setting</span><input id="settings-search" class="input" type="search" placeholder="Find a setting…" value="${escape(state.settingsSearch)}" autocomplete="off"></label><nav class="settings-sections" aria-label="Settings sections">${settingsGroups.map(([key, label]) => `<button type="button" class="button small" data-settings-section="${key}" aria-pressed="${state.settingsSection === key}">${label}</button>`).join('')}</nav><p id="settings-search-status" class="field-note" role="status"></p></div>`;
  }
  function filterSettingsSections() {
    const search = state.settingsSearch.trim().toLocaleLowerCase(),
      panels = $$('#settings-form > .page-grid > section');
    let shown = 0;
    for (const panel of panels) {
      const heading = $('h2', panel)?.textContent || '',
        key = /appearance|atmosphere|comfort/i.test(heading) ? 'appearance' : /backup/i.test(heading) ? 'backups' : /nexus|download/i.test(heading) ? 'downloads' : /game|library preferences/i.test(heading) ? 'game' : 'toolkit';
      panel.dataset.settingsGroup = key;
      const matches = (!search || panel.textContent.toLocaleLowerCase().includes(search)) && (state.settingsSection === 'all' || key === state.settingsSection);
      panel.hidden = !matches;
      if (matches) shown++;
    }
    $$('[data-settings-section]').forEach((control) => control.setAttribute('aria-pressed', String(control.dataset.settingsSection === state.settingsSection)));
    const status = $('#settings-search-status');
    if (status) status.textContent = !shown ? 'No matching settings. Try another word or choose All settings.' : search || state.settingsSection !== 'all' ? `${shown} section${shown === 1 ? '' : 's'} shown. Your other settings stay saved.` : '';
  }
  function bindSettingsBrowser() {
    const search = $('#settings-search');
    if (search) search.oninput = () => { state.settingsSearch = search.value; filterSettingsSections(); };
    $$('[data-settings-section]').forEach((control) => control.onclick = () => {
      state.settingsSection = control.dataset.settingsSection;
      filterSettingsSections();
    });
    filterSettingsSections();
  }
  function showGamePathError(message, path) {
    const current = state.view === 'settings' ? $('#game-path')?.value : null;
    state.gamePathError = { message, path, draft: typeof current === 'string' ? current : path,
      context: String(state.snapshot.accountContext || '') };
    updateGamePathError();
  }
  function gamePathFieldValue() {
    const error = state.gamePathError;
    return error?.context === String(state.snapshot.accountContext || '') ? error.draft ?? error.path : state.snapshot.settings.gamePath || '';
  }
  function updateGamePathError() {
    const input = $('#game-path'), message = $('#game-path-error'),
      error = state.gamePathError?.context === String(state.snapshot.accountContext || '') ? state.gamePathError : null;
    if (!input || !message) return;
    if (error) error.draft = input.value;
    const changed = error && input.value.trim() !== error.path;
    input.setAttribute('aria-invalid', String(!!error && !changed));
    message.hidden = !error;
    const text = error ? changed ? 'The previous folder was not saved. Save this folder to check it.' : error.message : '';
    if (message.textContent !== text) message.textContent = text;
    const link = $('#settings-error-link');
    if (link) link.hidden = !error;
  }
  function focusGamePathError() {
    state.settingsSection = 'game'; state.settingsSearch = '';
    if (state.view !== 'settings') navigate('settings');
    else { const search = $('#settings-search'); if (search) search.value = ''; filterSettingsSections(); }
    const input = $('#game-path'); input?.scrollIntoView?.({ block: 'center', behavior: 'instant' }); input?.focus?.();
  }
  function fileCatalogMod(fileId, modId = 994) {
    const update = (state.downloads.updates || []).find(
      (u) => !isLocalMod(getMod(u.id)) && nexusModId(u) === Number(modId) && nexusFileId(u) === Number(fileId)
    );
    return update
      ? getMod(update.id)
      : state.snapshot.mods.find(
          (mod) => !isLocalMod(mod) && nexusModId(mod) === Number(modId) && nexusFileId(mod) === Number(fileId)
        );
  }
  function renderDownloads() {
    if (localEdition()) return renderLocalDownloads();
    const files = (state.downloads.inventory?.length ? state.downloads.inventory : state.downloads.files || []).filter(
      (f) => ![6, 7].includes(Number(f.category_id))
    );
    const shown = files.slice(0, state.downloadLimit);
    return `${header('NEXUS DOWNLOADS', 'Browse the author’s releases.', 'Check current files on Nexus Mods. New packages need inspection before they can be managed.', button('Downloads folder', 'downloads-folder', 'folder') + button('Use Nexus link', 'nexus-link', 'lock') + button('Check releases', 'nexus-check', 'refresh', 'primary'))}${previewNote()}<div class="banner ${state.nexus.connected ? '' : 'neutral'}">${icon(state.nexus.connected ? 'lock' : 'info')}<p><strong>${state.nexus.connected ? (state.nexus.tierVerified === false || state.nexus.offline ? 'Saved connection · verification unavailable.' : 'Connected to Nexus Mods.') : 'Connect your Nexus Mods account to check releases.'}</strong> ${state.nexus.connected ? (state.nexus.tierVerified === false || state.nexus.premium === null ? 'Your saved connection could not be verified. Membership is unavailable; retry when Nexus is reachable.' : nexusCanDownload() ? 'Your Premium account supports API downloads. Packages are verified before installation.' : 'Free accounts authorize each download on Nexus. Use browser download integration or paste the authorized link; the toolkit downloads and inspects the package for review.') : browserLoginAvailable() ? 'Sign in on the official Nexus website. Your saved authorization is protected locally by your operating system.' : 'Browser sign-in is not registered for this build yet. Connect with your own personal API key instead (testing and personal use); it is protected locally by your operating system.'}</p>${!state.nexus.connected ? button('Connect', 'nexus-connect', 'lock', 'small') : ''}</div>${renderNxmBanner()}${state.downloads.verificationUnavailable ? `<div class="banner warning"><p><strong>Nexus check incomplete.</strong> ${escape(state.downloads.releaseError || 'Nexus could not verify the current releases.')} Any earlier results below are from the last successful check.</p></div>` : ''}${state.downloads.inventoryTruncated ? '<div class="banner warning"><p>The Nexus discovery result reached its limit. Some pages or files may be missing; check the author’s Nexus profile for the full list.</p></div>' : ''}${
      files.length
        ? `<div class="section-heading"><div><h2>Published files <span>${files.length}</span></h2><p>Last checked ${escape(formatDate(state.downloads.checkedAt))}. Match the file’s variant to the mod you want.</p></div>${button('Open Nexus page', 'nexus', 'external', 'small')}</div><div class="mod-list">${shown
            .map((f) => {
              const fileId = f.file_id ?? f.fileId,
                modId = nexusModId(f),
                mod = fileCatalogMod(fileId, modId),
                update = (state.downloads.updates || []).find(
                  (u) => u.id === mod?.id && Number(u.fileId) === Number(fileId)
                );
              return `<article class="mod-row">${modArtwork(mod ? { ...f, ...mod } : f)}<div><div class="mod-name">${escape(f.name || 'Nexus file')}</div><p class="mod-summary">${escape(f.description || f.summary || 'Check the file notes on Nexus Mods before choosing this release.')}</p><div class="mod-meta"><span>${f.version ? 'v' + escape(f.version) : 'Version not supplied'}</span><span>· ${escape(formatDate(f.uploadedTimestamp || f.uploaded_timestamp || f.uploaded_time))}</span><span>· ${escape(f.modName || f.pageName || `Nexus page ${modId}`)}</span></div></div><div class="mod-status ${update?.updateAvailable ? 'conflict' : 'available'}"><span class="status-dot"></span><div>${update?.updateAvailable ? (mod && installedUpdate(mod) ? 'Update available' : 'Newer release') : mod?.installationPolicy === 'review-required' ? 'Inspection required' : mod?.available === false ? 'Unavailable build' : 'Published file'}<small>${update?.updateAvailable ? `Installed ${escape(update.currentVersion || mod?.version || '?')} → latest ${escape(update.version || '?')} · ${update.auditedInstallable ? 'Audited package' : 'Awaiting package inspection'}` : mod?.installationPolicy === 'review-required' ? 'Inspect the downloaded package' : mod?.available === false ? 'See catalog notes' : 'Installation needs verification'}</small></div></div><div class="mod-row-actions"><button class="button small" data-nexus-file="${escape(fileId)}" data-nexus-mod-id="${escape(modId)}" ${mod?.available === false && mod.installationPolicy !== 'review-required' ? 'disabled' : ''}>${icon('download')}Download</button></div></article>`;
            })
            .join(
              ''
            )}</div>${shown.length < files.length ? `<p class="field-note">Showing ${shown.length} of ${files.length} published files.</p>${button('Show 50 more files', 'downloads-more', 'plus')}` : ''}`
        : `<div class="empty-state">${icon('download')}<h3>No release list loaded yet.</h3><p>${state.nexus.connected ? 'Check releases to fetch the current file list from Nexus Mods. You choose which variant to download.' : 'Connect an account for release checks, or visit Nexus Mods and import your existing downloads.'}</p><div class="inline-actions justify-center">${button('Open Nexus page', 'nexus', 'external')}${button('Import existing download', 'import', 'plus')}</div></div>`
    }<div class="info-card mt-24"><div class="info-card-head">${icon('shield')}A new download is not automatically a trusted package</div><p>New official releases can appear without a loader update. The native toolkit inspects the downloaded PAK and shows its identity, asset paths and overlaps for review before adding it. Downloads never enable themselves.</p></div>`;
  }
  function formatBytes(bytes) {
    if (typeof bytes !== 'number' || !Number.isFinite(bytes) || bytes < 0) return 'Not reported';
    const units = ['B', 'KB', 'MB', 'GB', 'TB'];
    let value = bytes,
      index = 0;
    while (value >= 1024 && index < units.length - 1) {
      value /= 1024;
      index++;
    }
    return `${value.toLocaleString(undefined, { maximumFractionDigits: index > 1 ? 1 : 0 })} ${units[index]}`;
  }
  function samePath(a, b) {
    if (typeof a !== 'string' || typeof b !== 'string') return false;
    const normalize = state.snapshot.platform === 'linux'
      ? (value) => value.replace(/\/+$/, '')
      : (value) => value.replace(/[\\/]+$/, '').replace(/\\/g, '/').toLowerCase();
    return normalize(a) === normalize(b);
  }
  async function loadDirectories() {
    if (state.directoriesLoading) return;
    const context = state.snapshot.accountContext;
    state.directoriesLoading = true;
    try {
      const result = await run(
        'directoriesScan',
        {},
        { refresh: false, progress: 'Finding game installations and checking your folders…' }
      );
      if (result && context === state.snapshot.accountContext) {
        state.directories = result.directories || result;
        if (state.view === 'folders') render();
        setStatus('Folders checked.');
      }
    } finally {
      if (context === state.snapshot.accountContext) state.directoriesLoading = false;
    }
  }
  // Plain names for the known folders: [title, what it is].
  const locationNames = {
    managedPaks: ['Mods folder', 'Where Days Gone loads your PAK mods'],
    gamePaks: ['Game files', 'Read only'],
    library: ['Toolkit library', 'Copies of the mods you imported'],
    downloads: ['Downloads', 'Files downloaded inside the toolkit'],
    saves: ['Save files', 'Read only'],
    saveBackups: ['Save backups', 'Copies made with Back up saves']
  };
  function modsPathLabel() {
    return state.snapshot.activePath || (state.snapshot.platform === 'linux' ? 'Not located — select the Days Gone Steam installation' : '%LOCALAPPDATA%\\BendGame\\Saved\\Paks');
  }
  function renderFolders() {
    const directories = state.directories,
      installations = directories?.installations || [],
      locations = directories?.locations || [],
      startup = directories?.startup || {},
      duplicates = directories?.duplicates || [];
    return `${header('GAME & MOD FOLDERS', 'Check your file locations.', 'See where your game and your mods are, open those folders and set up mod loading.', button('Scan locations', 'directories-scan', 'refresh', 'primary'))}${previewNote()}<div class="banner neutral">${icon('folder')}<p><strong>Your mods go into one folder.</strong> Mods you enable are placed in <code>${escape(modsPathLabel())}</code>, where Days Gone loads them. The game’s own files and your saves stay where they are.</p></div>${
      !directories
        ? `<div class="empty-state">${icon('compass')}<h3>Scan your game folders.</h3><p>Scan local game registrations and known folders to see the actual installation choices, package locations and available disk space.</p>${button('Scan locations', 'directories-scan', 'refresh', 'primary')}</div>`
        : `<section><div class="section-heading"><div><h2>Game installations <span>${installations.length} found</span></h2><p>Choose the copy you launch. Selecting an installation updates the game path.</p></div>${button('Browse manually', 'browse-game', 'folder', 'small')}</div>${
            installations.length
              ? `<div class="installation-list">${installations
                  .map((item) => {
                    const selected = samePath(item.path, state.snapshot.settings.gamePath);
                    return `<article class="installation-row"><div class="folder-emblem">${icon('play')}</div><div class="installation-copy"><h3>${escape(item.label || 'Days Gone')}</h3><p>${escape(item.source || 'Detected installation')}</p><code>${escape(item.path)}</code></div>${selected ? '<span class="badge selected-badge">SELECTED</span>' : `<button class="button small" data-use-installation="${escape(item.path)}">Use this installation</button>`}</article>`;
                  })
                  .join('')}</div>`
              : `<div class="empty-state compact"><h3>${state.preview ? 'Installation discovery needs the desktop app' : 'No installation was found automatically'}</h3><p>${state.preview ? 'Your real game locations will appear here after a local scan.' : 'Choose Browse manually and select the folder containing DaysGone.exe. The toolkit checks the executable before accepting the path.'}</p></div>`
          }</section><section class="mt-28"><div class="section-heading"><div><h2>Your locations</h2><p>Open a known folder without typing or guessing a path.</p></div></div>${locations.length ? `<div class="location-grid">${locations.map((location) => `<article class="location-card"><div class="location-card-heading"><span class="folder-emblem">${icon(location.id === 'saves' ? 'shield' : location.id === 'downloads' ? 'download' : 'folder')}</span><div><h3>${escape(locationNames[location.id]?.[0] || location.label || location.id)}</h3><span class="location-role">${escape(locationNames[location.id]?.[1] || (location.managed ? 'Toolkit location' : 'Read only'))}</span></div><span class="location-exists ${location.exists ? 'exists' : ''}">${location.exists ? `${icon('check')}Found` : 'Not created yet'}</span></div><code class="location-path">${escape(location.path || 'Path not available')}</code><div class="location-details">${typeof location.pakCount === 'number' ? `<span>${location.pakCount} PAK file${location.pakCount === 1 ? '' : 's'}</span>` : '<span>Known location</span>'}<span>${typeof location.freeBytes === 'number' ? `${formatBytes(location.freeBytes)} free` : 'Disk space not reported'}</span></div><div class="location-footer"><p>${location.id === 'saves' ? 'Back up your saves here. Restore a reviewed backup on the Saves page.' : location.id === 'saveBackups' ? 'Dated copies of your saves. Review and restore a backup on the Saves page.' : location.id === 'gamePaks' ? 'The game’s own files stay here; the toolkit only reads them.' : location.id === 'managedPaks' ? 'The toolkit changes only the mods it manages here.' : location.id === 'library' ? 'Imported mods stay ready to switch on again.' : location.id === 'downloads' ? 'Nexus downloads are staged here for verification.' : 'The selected game installation.'}</p>${location.id === 'saves' && location.exists ? button('Back up now', 'backup-saves', 'download', 'small') : ''}<button class="button small" data-open-location="${escape(location.id)}" ${!location.exists ? 'disabled' : ''}>${icon('external')}Open</button></div></article>`).join('')}</div>` : `<div class="empty-state compact"><h3>No local paths reported yet</h3><p>Run the folder scan in the desktop application to see your actual locations.</p></div>`}</section><section class="panel mt-28"><div class="panel-heading"><div><h2>Mod-loading setup</h2><p>Lets Days Gone load your PAK mods.</p></div>${icon('wrench')}</div><div class="panel-body"><div class="startup-status">${startupBadge(startup.status)}<p>${escape(startup.message || 'Select a detected installation and scan its mod-loading setup.')}</p></div>${startup.source ? `<div class="startup-path"><span>Startup file</span><code>${escape(startup.source)}</code></div>` : ''}${startup.backup ? `<div class="startup-path"><span>Toolkit backup</span><code>${escape(startup.backup)}</code></div>` : ''}<div class="inline-actions mt-16">${startup.canPrepare ? button('Set up mod loading', 'directories-prepare', 'wrench', 'primary') : ''}${startup.canRestore ? button('Restore original setup', 'directories-restore', 'back', '') : ''}</div><p class="field-note">Days Gone skips PAK mods while its startup file is in place. Setting up renames that one file; Restore original setup puts it back. A few mission and map mods need extra steps; their mod pages say so.</p></div></section>${
            duplicates.length
              ? `<section class="mt-28"><div class="section-heading"><div><h2>Duplicate mod files <span>${duplicates.length} group${duplicates.length === 1 ? '' : 's'}</span></h2><p>Review these copies before enabling more mods.</p></div></div>${duplicates
                  .map((item) => {
                    const entries = Array.isArray(item) ? item : item.paths || item.files || item.packages || [];
                    return `<div class="banner warning duplicate-warning">${icon('alert')}<div><strong>${escape(item.name || item.file || item.label || `${item.count ?? entries.length} matching PAK copies found`)}</strong>${item.message ? `<p>${escape(item.message)}</p>` : ''}<ul>${entries.map((entry) => `<li><code>${escape(typeof entry === 'string' ? entry : entry.path || entry.file || entry.name)}</code></li>`).join('')}</ul><p>Keep a backup and identify which copy you installed before removing anything. The toolkit leaves these files in place.</p></div></div>`;
                  })
                  .join('')}</section>`
              : ''
          }`
    }`;
  }
  function confirmDirectoryChange(kind, after) {
    const startup = state.directories?.startup;
    if (!startup || !(kind === 'prepare' ? startup.canPrepare : startup.canRestore)) {
      toast('Run a fresh folder scan before changing the mod-loading setup.', 'warning');
      return;
    }
    const prepare = kind === 'prepare';
    confirmModal(
      prepare ? 'Set up mod loading?' : 'Restore the original mod-loading setup?',
      `${prepare ? '<p>Days Gone skips PAK mods while one of its startup files is in place. The toolkit renames that one file so your mods load. Nothing is deleted, and you can put it back any time under Folders.</p><p class="field-note">A few mission and map mods need extra steps; their mod pages say so.</p>' : '<p>The toolkit puts the original startup file back. Mods will not load until you set it up again. It stops if the files no longer match its record.</p>'}<details class="finding-evidence"><summary>File paths</summary><div class="startup-path"><span>${prepare ? 'Startup file' : 'Restore destination'}</span><code>${escape(startup.source || 'Not supplied')}</code></div><div class="startup-path"><span>${prepare ? 'Renamed to' : 'Kept copy'}</span><code>${escape(startup.backup || 'Not supplied')}</code></div></details><p class="mt-16"><strong>Close Days Gone first.</strong></p>`,
      prepare ? 'Set up mod loading' : 'Restore original',
      async () => {
        const result = await run(
          prepare ? 'directoriesPrepare' : 'directoriesRestore',
          { confirmed: true },
          {
            refresh: false,
            progress: prepare ? 'Setting up mod loading…' : 'Putting the startup file back…'
          }
        );
        if (result) {
          state.directories = result.directories || result;
          await loadDirectories();
          toast(prepare ? 'Mod-loading preparation finished.' : 'Original mod-loading setup restored.');
          render();
          if (after) after();
        }
      }
    );
  }
  function navigate(view, options = {}) {
    if (options.characterAppearance !== true) leaveCharacterAppearance();
    const enteringLibrary = view === 'mods' && state.view !== 'mods';
    closeModDetails(false);
    if (view === 'conflicts' && state.view !== 'conflicts') state.conflictLimit = 50;
    applyAppearance();
    state.view = view;
    state.article = null;
    render();
    window.scrollTo({ top: 0, behavior: 'instant' });
    $('#main-content').focus({ preventScroll: true });
    if (enteringLibrary && libraryDocked() && !$('#modal').open) {
      const first = filteredMods()[0];
      if (first) showModDetails(first.id, { focus: false });
    }
    if (view === 'settings') loadStorageStatus();
    if (view === 'folders' && !state.directories) loadDirectories();
    if (view === 'profiles') loadPortableProfiles();
    if (view === 'diagnostics' && !state.crashLogs) loadCrashLogs();
    if (view === 'saves' && state.savesTab === 'backups' && !state.snapshot.libraryBackupRecovery?.pending) loadSaveBackups();
    if (view === 'saves' && state.savesTab === 'current') loadCurrentSaves();
    if (view === 'mods') loadLibraryMeta();
    if (view === 'workshop' && !state.workshopLoaded) loadWorkshopStatus();
    scheduleNoticeDelivery();
  }
  function bindContent() {
    bindWorkshop();
    bindSaveBackupControls($('#save-backups-panel'));
    bindCurrentSaveControls($('#current-saves-panel'));
    $$('[data-saves-tab]', $('#main-content')).forEach((el) => (el.onclick = () => setSavesTab(el.dataset.savesTab)));
    updateSavesGameStatus();
    $$('[data-view]', $('#main-content')).forEach((el) => (el.onclick = () => navigate(el.dataset.view)));
    $$('[data-action]', $('#main-content')).forEach(
      (el) =>
        (el.onclick = (e) => {
          e.preventDefault();
          handleAction(el.dataset.action);
        })
    );
    $$('[data-filter]', $('#main-content')).forEach(
      (el) =>
        (el.onclick = () => {
          state.filter = el.dataset.filter;
          state.filterChosen = true;
          state.modLimit = 50;
          render();
        })
    );
    const search = $('#mod-search');
    if (search)
      search.oninput = () => {
        state.search = search.value;
        state.modLimit = 50;
        $('#mod-list').innerHTML = renderModRows(state.view === 'overview');
        bindModRows();
      };
    const sortFilter = $('#sort-filter');
    if (sortFilter)
      sortFilter.onchange = () => {
        state.sort = sortOrders[sortFilter.value] ? sortFilter.value : 'recommended';
        storeValue('drifter.sort', state.sort);
        $('#mod-list').innerHTML = renderModRows();
        bindModRows();
      };
    const category = $('#category-filter');
    if (category)
      category.onchange = () => {
        state.category = category.value;
        state.modLimit = 50;
        $('#mod-list').innerHTML = renderModRows();
        bindModRows();
      };
    bindModRows();
    bindProfileRows();
    const profileSearch = $('#profile-search');
    if (profileSearch)
      profileSearch.oninput = () => {
        state.profileSearch = profileSearch.value;
        refreshProfileRows();
      };
    const profileSort = $('#profile-sort');
    if (profileSort)
      profileSort.onchange = () => {
        state.profileSort = profileSorts[profileSort.value] ? profileSort.value : 'name';
        storeValue('drifter.profileSort', state.profileSort);
        refreshProfileRows();
      };
    $$('[data-profile-apply]').forEach(
      (el) =>
        (el.onclick = () =>
          confirmModal(
            'Apply this profile?',
            `<p>Applying <strong>${escape(el.dataset.profileApply)}</strong> switches your mods on and off to match it. Keep Days Gone closed while the files change.</p><p>The toolkit checks for conflicts before applying it.</p>`,
            'Apply profile',
            () => run('applyProfile', { name: el.dataset.profileApply }, { success: 'Profile applied.' })
          ))
    );
    $$('[data-profile-delete]').forEach(
      (el) =>
        (el.onclick = () =>
          confirmModal(
            'Delete this profile?',
            `<p>Delete <strong>${escape(el.dataset.profileDelete)}</strong>? Your installed mods and library packages stay as they are.</p>`,
            'Delete profile',
            () => run('deleteProfile', { name: el.dataset.profileDelete }, { success: 'Profile deleted.' }),
            true
          ))
    );
    $$('[data-help-section]').forEach(
      (el) =>
        (el.onclick = () => {
          state.helpSection = el.dataset.helpSection === 'modding' ? 'modding' : 'help';
          state.article = null;
          state.helpSearch = '';
          render();
          window.scrollTo?.(0, 0);
        })
    );
    $$('[data-modding-resource]').forEach((el) => (el.onclick = () => openModdingResource(el.dataset.moddingResource)));
    $$('[data-article]').forEach(
      (el) =>
        (el.onclick = () => {
          state.article = el.dataset.article;
          const article = (window.FIELD_GUIDE || []).find((item) => item.id === state.article);
          state.helpSection = article?.modding ? 'modding' : 'help';
          render();
          window.scrollTo?.(0, 0);
          $('#main-content').focus();
        })
    );
    const guide = $('#guide-search');
    if (guide)
      guide.oninput = () => {
        state.helpSearch = guide.value;
        const articles = visibleGuideArticles();
        $('#guide-grid').innerHTML =
          state.helpSection === 'modding' ? renderModdingChapters(articles) : renderGuideCards(articles);
        $$('[data-article]').forEach(
          (el) =>
            (el.onclick = () => {
              state.article = el.dataset.article;
              render();
              window.scrollTo?.(0, 0);
            })
        );
      };
    const previewAppearance = (change) => {
      appearancePreview = { ...(appearancePreview || {}), ...change };
      applyAppearance();
      scheduleAutoSave();
    };
    const appearanceSwitches = {
      'system-theme-setting': 'useSystemTheme',
      'seasonal-setting': 'seasonalThemes',
      'effects-setting': 'seasonalEffects',
      'auto-scale-setting': 'autoScale',
      'motion-setting': 'reducedMotion',
      'contrast-setting': 'highContrast',
      'compact-setting': 'compactMode'
    };
    for (const [id, key] of Object.entries(appearanceSwitches)) {
      const control = $('#' + id);
      if (control)
        control.onclick = () => {
          const checked = control.getAttribute('aria-checked') !== 'true';
          control.setAttribute('aria-checked', String(checked));
          previewAppearance({ [key]: checked });
        };
    }
    for (const id of ['updates-setting', 'toolkit-updates-setting', 'steam-launch-setting', 'auto-manage-setting']) {
      const control = $('#' + id);
      if (control)
        control.onclick = () => {
          control.setAttribute('aria-checked', String(control.getAttribute('aria-checked') !== 'true'));
          if (id === 'updates-setting' || id === 'toolkit-updates-setting') scheduleAutoSave();
          else markPendingSave(id.replace('-setting', ''));
        };
    }
    const gamePathField = $('#game-path');
    if (gamePathField) gamePathField.oninput = () => { markPendingSave('game-path'); updateGamePathError(); };
    if (state.view === 'settings') {
      bindSettingsBrowser(); updateGamePathError();
      if (state.gamePathError) markPendingSave('game-path');
      const errorLink = $('#settings-error-link'); if (errorLink) errorLink.onclick = focusGamePathError;
    }
    $$('[data-save-setting]').forEach((el) => (el.onclick = () => saveLoaderSettings({ preventDefault() {} })));
    const colorSelect = $('#color-mode-setting');
    if (colorSelect) colorSelect.onchange = () => previewAppearance({ colorMode: colorSelect.value });
    const themeSelect = $('#theme-setting');
    if (themeSelect)
      themeSelect.onchange = () => {
        const theme = themeSelect.value;
        if (theme !== 'default') $('#seasonal-setting').setAttribute('aria-checked', 'true');
        previewAppearance({ theme, seasonalThemes: $('#seasonal-setting').getAttribute('aria-checked') === 'true' });
      };
    const interfaceScaleSelect = $('#interface-scale-setting'),
      fontScaleSelect = $('#scale-setting');
    if (interfaceScaleSelect)
      interfaceScaleSelect.onchange = () => {
        $('#auto-scale-setting').setAttribute('aria-checked', 'false');
        appearancePreview = {
          ...(appearancePreview || {}),
          autoScale: false,
          interfaceScale: Number(interfaceScaleSelect.value)
        };
        applyAppearance();
        scheduleAutoSave();
      };
    if (fontScaleSelect)
      fontScaleSelect.onchange = () => {
        appearancePreview = { ...(appearancePreview || {}), fontScale: Number(fontScaleSelect.value) };
        applyAppearance();
        scheduleAutoSave();
      };
    const form = $('#settings-form');
    if (form) form.onsubmit = saveLoaderSettings;
    $$('[data-crash-read]').forEach((el) => (el.onclick = () => previewCrashLog(el.dataset.crashRead)));
    $$('[data-crash-summary]').forEach((el) => (el.onclick = () => summarizeCrashDump(el.dataset.crashSummary)));
    $$('[data-crash-select]').forEach(
      (el) =>
        (el.onchange = () => {
          if (el.checked) state.selectedCrashLogs.add(el.dataset.crashSelect);
          else state.selectedCrashLogs.delete(el.dataset.crashSelect);
          render();
        })
    );
    $$('[data-diagnostic-fix]').forEach((el) => (el.onclick = () => reviewDiagnosticFix(el.dataset.diagnosticFix)));
    $$('[data-diagnostic-help]').forEach((el) => (el.onclick = () => openGuideArticle(el.dataset.diagnosticHelp)));
    $$('[data-open-location]').forEach(
      (el) => (el.onclick = () => run('openLocation', { id: el.dataset.openLocation }, { refresh: false }))
    );
    $$('[data-use-installation]').forEach(
      (el) =>
        (el.onclick = async () => {
          const result = await run(
            'saveSettings',
            { settings: { gamePath: el.dataset.useInstallation } },
            { success: 'Game installation selected.' }
          );
          if (result) await loadDirectories();
        })
    );
    $$('[data-external-manage]').forEach(
      (el) => (el.onclick = () => reviewExternalAdoption(el.dataset.externalManage))
    );
    $$('[data-external-remove]').forEach((el) => (el.onclick = () => reviewExternalRemoval(el.dataset.externalRemove)));
    $$('[data-nexus-file]').forEach(
      (el) => (el.onclick = () => downloadNexusFile(el.dataset.nexusFile, el.dataset.nexusModId))
    );
  }
  function bindModRows() {
    syncModPanelNavigation();
    bindRecordReviewControls();
    bindPagePreviews();
    $$('[data-action="mods-more"]').forEach((el) => (el.onclick = () => handleAction('mods-more')));
    $$('[data-spotted-add]').forEach((el) => (el.onclick = () => addSpottedDownload(el.dataset.spottedAdd)));
    $$('[data-keep-one]').forEach((el) => (el.onclick = () => keepOnlyMod(el.dataset.keepOne)));
    $$('[data-spotted-ignore]').forEach((el) => (el.onclick = () => ignoreSpottedDownload(el.dataset.spottedIgnore)));
    $$('[data-clear-filters]').forEach(
      (el) =>
        (el.onclick = () => {
          state.search = '';
          state.category = state.characterAppearanceView ? 'Character appearance' : 'all';
          if (state.characterAppearanceView) state.filter = 'all';
          state.favoritesOnly = false;
          state.modLimit = 50;
          render();
        })
    );
    $$('[data-character-preview]').forEach((el) => {
      el.onclick = () => showCharacterModelPreview(el.dataset.characterPreview);
      const image = el.querySelector('img');
      if (image) image.onerror = () => {
        image.hidden = true; el.disabled = true;
        const label = el.querySelector('small'); if (label) label.textContent = 'Preview unavailable';
      };
    });
    $$('[data-details]').forEach((el) => (el.onclick = () => showModDetails(el.dataset.details)));
    $$('[data-toggle]').forEach((el) => (el.onclick = () => toggleMod(el.dataset.toggle)));
    $$('[data-remove-mod]').forEach((el) => (el.onclick = () => reviewRemoveMod(el.dataset.removeMod)));
    $$('[data-download]').forEach(
      (el) =>
        (el.onclick = () => {
          const m = getMod(el.dataset.download);
          if (isLocalMod(m)) {
            handleAction('import');
            return;
          }
          if (m && localEdition()) return showModDetails(m.id);
          if (m && nexusFileId(m) > 0) return downloadNexusFile(nexusFileId(m), nexusModId(m));
          navigate('downloads');
          if (state.nexus.connected) handleAction('nexus-check');
        })
    );
  }
  let undoSet = null, undoContext = null;
  function rememberUndo() {
    undoSet = enabledMods().map((m) => m.id);
    undoContext = state.snapshot.accountContext;
  }
  function undoAction() {
    const target = undoSet, context = undoContext;
    return target && context === state.snapshot.accountContext
      ? { label: 'Undo', run: () => undoLastChange(target, context) } : null;
  }
  async function undoLastChange(expected = undoSet, context = undoContext) {
    if (!expected || expected !== undoSet || context !== state.snapshot.accountContext || state.busy) return false;
    const target = new Set(expected),
      current = enabledMods().map((m) => m.id);
    undoSet = null; undoContext = null;
    for (const id of current) {
      if (context !== state.snapshot.accountContext) return false;
      if (!target.has(id)) {
        const result = await run('disable', { id }, { refresh: false, progress: `Switching ${modName(id)} off…` });
        if (context !== state.snapshot.accountContext) return false;
        if (result == null) return refresh({ quiet: true });
      }
    }
    for (const id of target) {
      if (context !== state.snapshot.accountContext) return false;
      if (!current.includes(id)) {
        const result = await run('enable', { id }, { refresh: false, progress: `Switching ${modName(id)} on…` });
        if (context !== state.snapshot.accountContext) return false;
        if (result == null) return refresh({ quiet: true });
      }
    }
    await refresh({ quiet: true });
    if (context !== state.snapshot.accountContext) return false;
    toast('Undone. Your mods are back the way they were.');
    return true;
  }
  function modListText() {
    const enabled = enabledMods(),
      byHand = externalMods(),
      fromNexus = enabled.filter((m) => !isLocalMod(m) || m.catalogMatch),
      fromFiles = enabled.filter((m) => isLocalMod(m) && !m.catalogMatch);
    const sections = [[`My Days Gone mods (Drifter’s Toolkit ${state.snapshot.appVersion || ''})`.replace(' )', ')')]];
    if (!enabled.length) sections.push(['No mods switched on.']);
    if (fromNexus.length) {
      const link = (m) =>
        `https://www.nexusmods.com/daysgone/mods/${nexusModId(m)}${nexusFileId(m) ? `?tab=files&file_id=${nexusFileId(m)}` : ''}`;
      const groups = new Map();
      for (const m of fromNexus) {
        const key = m.download ? `${nexusModId(m)}:${nexusFileId(m)}` : `mod:${m.id}`;
        groups.set(key, [...(groups.get(key) || []), m]);
      }
      const lines = ['From Nexus Mods:'];
      for (const group of groups.values()) {
        const m = group[0],
          version = versionLabel(m.version) ? ' ' + versionLabel(m.version) : '';
        lines.push(
          group.length > 1
            ? `- ${m.download}${version}: ${group.map((other) => other.name.replace(/^[^:]{1,24}:\s+/, '')).join(', ')} · ${link(m)}`
            : `- ${m.name}${version} · ${link(m)}`
        );
      }
      sections.push(lines);
    }
    if (fromFiles.length) sections.push(['Added from files:', ...fromFiles.map((m) => `- ${m.name}`)]);
    if (byHand.length)
      sections.push([
        'Installed by hand:',
        ...byHand.map(
          (file) => `- ${readableModName(String(file.name || file.file || 'PAK file').replace(/\.pak$/i, ''))}`
        )
      ]);
    return sections.map((lines) => lines.join('\n')).join('\n\n');
  }
  function parseSharedModList(text) {
    if (typeof text !== 'string' || text.length > 131072) throw new Error('Keep the list under 128 KB.');
    const rows = text.replace(/\r/g, '').split('\n');
    if (rows.length > 1000) throw new Error('This list has too many lines.');
    const result = [], seen = new Set();
    let section = 'unknown';
    for (const line of rows) {
      if (/^From Nexus Mods:/i.test(line.trim())) { section = 'nexus'; continue; }
      if (/^(Added from files|Installed by hand):/i.test(line.trim())) { section = 'local'; continue; }
      if (!/^\s*[-*]\s+/.test(line)) continue;
      if (result.length >= 256) throw new Error('This list has more than 256 entries.');
      const raw = line.replace(/^\s*[-*]\s+/, '').trim();
      if (!raw || /[\u0000-\u001f\u007f]/.test(raw)) continue;
      const links = raw.match(/https?:\/\/\S+/g) || [];
      let page = null;
      if (links.length === 1) {
        try {
          const url = new URL(links[0]), match = url.pathname.match(/^\/daysgone\/mods\/([1-9]\d{0,8})\/?$/);
          const keys = [...url.searchParams.keys()];
          if (url.protocol === 'https:' && url.hostname === 'www.nexusmods.com' && !url.port && !url.username && !url.password && !url.hash && match &&
              keys.every((key) => key === 'tab' || key === 'file_id') && new Set(keys).size === keys.length &&
              (!url.searchParams.has('tab') || url.searchParams.get('tab') === 'files')) {
            const file = url.searchParams.get('file_id');
            if (file === null || /^[1-9]\d{0,9}$/.test(file) && Number(file) <= 2147483647)
              page = { modId: Number(match[1]), fileId: file === null ? 0 : Number(file) };
          }
        } catch { /* Text from a shared list never becomes an arbitrary navigation target. */ }
      }
      const name = raw.split(/\s+·\s+https?:\/\//)[0].slice(0, 240);
      const key = page ? `nexus:${page.modId}:${page.fileId}` : `name:${name.toLowerCase()}`;
      if (seen.has(key)) continue;
      seen.add(key);
      const matches = page ? state.snapshot.mods.filter((mod) => nexusModId(mod) === page.modId &&
        (!page.fileId || nexusFileId(mod) === page.fileId) && !isLocalMod(mod)) :
        state.snapshot.mods.filter((mod) => String(mod.name).toLowerCase() === name.toLowerCase());
      const held = matches.filter(hasMod);
      const partial = held.length > 0 && held.length < matches.length;
      result.push({ name, page, ids: matches.map((mod) => mod.id),
        status: partial ? 'partial' : held.length ? 'have' : page ? 'download' : 'unknown',
        note: partial ? 'Part of this download is in your library — check the listed variants' : held.length ? 'Already in your library' : page ? 'Download from the Files tab, then import it' :
          section === 'local' ? 'Local file — ask the person who shared the list' : 'Not matched to a known release' });
    }
    return result;
  }
  function openSharedModList() {
    modal('Open a shared mod list', '<p>Paste a list made with Share my mod list. You’ll get a checklist; this does not download or enable anything.</p><label class="field-label" for="shared-mod-list">Mod list</label><textarea class="input full-width" id="shared-mod-list" rows="10" maxlength="131072"></textarea><p class="field-error" id="shared-list-error" role="alert"></p>',
      '<button class="button" data-modal-close>Close</button><button class="button primary" id="shared-list-check">Make checklist</button>');
    $('#shared-list-check').onclick = () => {
      try {
        const rows = parseSharedModList($('#shared-mod-list').value);
        if (!rows.length) throw new Error('No mod entries were found. Paste the complete shared list, including its bullet lines.');
        modal('Shared mod checklist', `<p>${rows.filter((row) => row.status === 'have').length} of ${rows.length} entries are already in your library. A grouped download can contain several variants; check the actual choices before enabling them.</p><ul class="shared-list-checklist">${rows.map((row, index) => `<li><span class="tone-badge ${row.status === 'have' ? 'success' : row.status === 'download' ? 'info' : 'warning'}">${row.status === 'have' ? 'Have it' : row.status === 'partial' ? 'Check variants' : row.status === 'download' ? 'Download' : 'Check file'}</span><div><strong>${escape(row.name)}</strong><small>${escape(row.note)}</small></div>${row.page ? `<button type="button" class="button small" data-shared-nexus="${index}">Nexus page</button>` : ''}</li>`).join('')}</ul>`, '<button class="button" data-modal-close>Close</button>');
        $$('[data-shared-nexus]').forEach((el) => (el.onclick = () => {
          const page = rows[Number(el.dataset.sharedNexus)]?.page;
          if (page) run(page.fileId ? 'openNexusFile' : 'openNexus',
            page.fileId ? { modId: page.modId, fileId: page.fileId } : { modId: page.modId }, { refresh: false });
        }));
      } catch (error) { $('#shared-list-error').textContent = errorText(error); }
    };
  }
  function shareModList() {
    const text = modListText();
    modal(
      'Share my mod list',
      `<p>Your mods with their links, ready to paste into a chat, a forum post or a bug report. No folders or personal paths are included.</p><textarea id="mod-list-text" class="input full-width share-list" rows="10" readonly>${escape(text)}</textarea>`,
      `<button class="button" data-modal-close>Close</button><button class="button primary" id="mod-list-copy">${icon('report')}Copy</button>`
    );
    $('#mod-list-copy').onclick = async () => {
      try {
        await navigator.clipboard.writeText(text);
        toast('Mod list copied.');
      } catch {
        const area = $('#mod-list-text');
        area.focus?.();
        area.select?.();
        toast('Press Ctrl+C to copy the selected list.', 'warning');
      }
    };
  }
  async function toggleMod(id) {
    const m = getMod(id);
    if (!m) return;
    if (m.available === false && !m.enabled) {
      toast(m.unsupportedReason || 'The toolkit cannot install this mod. Open its details to see why.', 'warning');
      return;
    }
    if (!m.enabled && characterAppearance(m)?.explicit && m.variantGroup && appearancePartners(m).length)
      return offerAppearanceSwitch(m);
    if (!m.enabled && m.conflictsWithInstalled?.length) return offerSwitch(m, m.conflictsWithInstalled);
    rememberUndo();
    const wasEnabled = m.enabled;
    const changed = await run(
      wasEnabled ? 'disable' : 'enable',
      { id },
      { progress: `${wasEnabled ? 'Disabling' : 'Enabling'} ${m.name}…` }
    );
    if (changed != null)
      toast(
        `${m.name} ${wasEnabled ? 'disabled' : 'enabled'}.${state.snapshot.gameRunning ? ' Restart Days Gone to load the change.' : ''}`,
        'success',
        undoAction()
      );
  }
  function appearancePartners(mod) {
    if (!mod?.variantGroup) return [];
    return state.snapshot.mods.filter((other) => other.id !== mod.id && other.enabled && other.variantGroup === mod.variantGroup);
  }
  function appearanceSwitchIdentity(mod) {
    return JSON.stringify([mod?.sha256, mod?.variantGroup, appearancePartners(mod).map((other) => [other.id, other.sha256]).sort(),
      (mod?.conflictsWithInstalled || []).map((other) => [other.id, getMod(other.id)?.sha256 || '', !!getMod(other.id)?.enabled]).sort()]);
  }
  function offerAppearanceSwitch(mod) {
    const appearance = characterAppearance(mod), partners = appearancePartners(mod), fingerprint = appearanceSwitchIdentity(mod);
    const conflicts = mod.conflictsWithInstalled || [], outside = conflicts.filter((other) => !getMod(other.id)?.enabled);
    if (outside.length) {
      modal('An installed PAK needs review', `<p>${escape(mod.name)} also overlaps with ${outside.map((other) => `<strong>${escape(other.name || modName(other.id))}</strong>`).join(', ')}. Review or manage those installed files before switching character appearance.</p>`,
        '<button type="button" class="button" data-modal-close>Close</button>');
      return;
    }
    const affected = new Map(partners.map((other) => [other.id, other]));
    for (const other of conflicts) if (getMod(other.id)?.enabled) affected.set(other.id, getMod(other.id));
    confirmModal(`Switch ${characterName(appearance?.character)}’s appearance?`,
      `<p>Enable <strong>${escape(mod.name)}</strong> and switch off these currently enabled alternatives or overlapping packages:</p><ul class="finder-mods">${[...affected.values()].map((other) => `<li>${escape(other.name)}</li>`).join('')}</ul><p class="field-note">Their library copies stay available. The toolkit checks the packages again before changing them.</p>`,
      'Switch appearance', async () => {
        const current = getMod(mod.id);
        if (!current || current.enabled || appearanceSwitchIdentity(current) !== fingerprint) {
          toast('Your mod selection changed. Review the appearance switch again.', 'warning'); return;
        }
        rememberUndo();
        const changed = await run('switchMod', { id: mod.id }, { progress: `Switching to ${mod.name}…` });
        if (changed != null) toast(`Switched to ${mod.name}. The other library copies are kept.`, 'success', undoAction());
      });
  }
  // Only one of two mods that change the same game files can work. Mods the toolkit manages are switched off for
  // the new one after a single confirmation; a file installed by hand has to be handled first.
  function offerSwitch(m, partners) {
    const names = (list) => list.map((c) => `<strong>${escape(c.name || modName(c.id))}</strong>`).join(', '),
      managed = partners.filter((c) => getMod(c.id)?.enabled),
      outside = partners.filter((c) => !getMod(c.id)?.enabled);
    const intro = `<p>${escape(m.name)} changes the same game files as ${names(partners)}. Only one of them can work at a time.</p>`;
    if (outside.length) {
      modal(
        `${m.name} conflicts with another mod`,
        `${intro}<p>${names(outside)} ${outside.length === 1 ? 'was' : 'were'} installed outside the toolkit. Remove ${outside.length === 1 ? 'it' : 'them'} or choose Manage all first, then switch ${escape(m.name)} on again.</p>`,
        '<button class="button" data-modal-close>Close</button>'
      );
      return;
    }
    confirmModal(
      `Switch to ${m.name}?`,
      `${intro}<p>Switch ${names(managed)} off and ${escape(m.name)} on?</p>`,
      `Switch to ${m.name}`,
      async () => {
        rememberUndo();
        const enabled = await run('switchMod', { id: m.id }, { progress: `Switching to ${m.name}…` });
        if (enabled != null)
          toast(`Switched to ${m.name}. The previous versions stay in your library.`, 'success', undoAction());
      }
    );
  }
  let modalOpener = null;
  function handleModalBackdrop(event) {
    const dialog = $('#modal');
    if (event.target !== dialog) return;
    const rect = dialog.getBoundingClientRect();
    if (event.clientX >= rect.left && event.clientX <= rect.right && event.clientY >= rect.top && event.clientY <= rect.bottom) return;
    if (updateInstalling) return;
    const record = state.saveEditor;
    if (saveEditorVisible(record)) {
      if (record.mode === 'discard') showSaveEditor(record);
      else requestCloseSaveEditor();
    } else dialog.close();
  }
  function handleModalClosed() {
    const dialog = $('#modal');
    // close is queued. A newer dialog may already be open by the time it
    // arrives; none of that dialog's tokens, credentials or focus belong here.
    if (dialog.open) return;
    ++modalContentGeneration;
    state.saveRestoreReview = null;
    state.saveDeleteReview = null;
    state.currentSaveReplaceReview = null;
    discardPackageReview();
    activeIssueSummary = false;
    discardBatchReview();
    $$('input[type="password"]', dialog).forEach((el) => (el.value = ''));
    if (!document.activeElement || document.activeElement === document.body || dialog.contains?.(document.activeElement)) {
      const target = modalOpener?.isConnected ? modalOpener : $('#main-content');
      target?.focus?.({ preventScroll: true });
    }
    cancelNexusLogin();
    scheduleNoticeDelivery();
    syncActivityDismissal();
    syncHeroSlideshow();
    resumeSaveThumbnails();
  }
  let detailOpener = null;
  function libraryDocked() {
    const settings = state.snapshot.settings;
    return state.view === 'mods' && window.innerWidth /
      (effectiveInterfaceScale(settings) / 100) / Math.max(1, textPreference(settings)) >= 1280;
  }
  function syncLibraryDock() {
    document.body.classList.toggle('library-view', state.view === 'mods');
    document.body.classList.toggle('library-docked', libraryDocked());
  }
  function syncLibrarySelection() {
    $$('[data-mod-row]').forEach((row) => row.classList.toggle('is-selected', row.dataset.modRow === state.detailId));
    $$('[data-details]').forEach((control) => control.setAttribute('aria-expanded', String(control.dataset.details === state.detailId)));
  }
  function handleDetailsShortcut(event) {
    if (event.key !== 'Escape' || $('#modal').open || $('#mod-details-panel').hidden) return false;
    event.preventDefault(); closeModDetails(); return true;
  }
  function closeModDetails(restoreFocus = true) {
    const panel = $('#mod-details-panel');
    if (!panel || panel.hidden) return;
    panel.hidden = true;
    state.detailId = null;
    document.body.classList.remove('details-open');
    syncLibrarySelection();
    if (restoreFocus) {
      const target = detailOpener?.isConnected ? detailOpener : $('#main-content');
      target?.focus?.({ preventScroll: true });
    }
  }
  function showModPanel(mod, body, actions, options = {}) {
    const panel = $('#mod-details-panel');
    if (panel.hidden) {
      detailOpener = document.activeElement;
    }
    state.detailId = mod.id;
    syncLibraryDock();
    panel.innerHTML = `<header class="mod-panel-head"><div><span class="eyebrow">MOD DETAILS</span><h2 id="mod-panel-title">${escape(mod.name)}</h2></div><button type="button" class="icon-button" id="mod-panel-close" aria-label="Close mod details">${icon('close')}</button></header><nav class="mod-panel-nav" aria-label="Browse mod details"><button type="button" class="button small" id="mod-panel-previous">${icon('back')}Previous</button><span id="mod-panel-position"></span><button type="button" class="button small" id="mod-panel-next">Next${icon('arrow')}</button></nav><div class="mod-panel-body">${body}</div><div class="mod-panel-actions">${actions}<button type="button" class="button" id="mod-panel-return">Back to library</button></div>`;
    panel.hidden = false;
    document.body.classList.add('details-open');
    $('#mod-panel-close').onclick = () => closeModDetails();
    $('#mod-panel-return').onclick = () => closeModDetails();
    syncModPanelNavigation();
    panel.scrollTop = 0;
    if (options.focus !== false) panel.focus?.({ preventScroll: true });
    bindPagePreviews();
  }
  function syncModPanelNavigation() {
    syncLibrarySelection();
    if (!state.detailId || $('#mod-details-panel').hidden) return;
    const sequence = filteredMods().map((item) => item.id), index = sequence.indexOf(state.detailId),
      previous = $('#mod-panel-previous'), next = $('#mod-panel-next'), label = $('#mod-panel-position');
    if (label) label.textContent = index < 0 ? 'Outside this filter' : `${index + 1} of ${sequence.length}`;
    if (previous) {
      previous.disabled = index <= 0;
      previous.onclick = () => { if (!previous.disabled) showModDetails(sequence[index - 1]); };
    }
    if (next) {
      next.disabled = index < 0 || index >= sequence.length - 1;
      next.onclick = () => { if (!next.disabled) showModDetails(sequence[index + 1]); };
    }
  }
  function modal(title, body, actions = '') {
    ++modalContentGeneration;
    const dialog = $('#modal');
    if (pendingBatchReview) discardBatchReview();
    discardPackageReview();
    activeIssueSummary = false;
    $('#modal-content').onclick = null;
    if (!dialog.open) modalOpener = document.activeElement;
    dialog.setAttribute('aria-labelledby', 'modal-title');
    dialog.setAttribute('aria-modal', 'true');
    $('#modal-content').innerHTML =
      `<div class="modal-head"><h2 id="modal-title">${escape(title)}</h2><button class="icon-button" data-modal-close aria-label="Close dialog">${icon('close')}</button></div><div class="modal-body" tabindex="-1">${body}</div>${actions ? `<div class="modal-actions">${actions}</div>` : ''}`;
    const generation = modalContentGeneration;
    $$('[data-modal-close]', dialog).forEach((el) => (el.onclick = () => {
      if (!dialog.open || generation !== modalContentGeneration || !dialog.contains(el)) return false;
      dialog.close();
      return true;
    }));
    if (!dialog.open) dialog.showModal();
    syncActivityDismissal();
    syncHeroSlideshow();
    // A pop-up that opens by itself should not start on a highlighted close button.
    dialog.querySelector?.('.modal-body')?.focus?.({ preventScroll: true });
    bindPagePreviews();
    return dialog;
  }
  function confirmModal(title, body, label, callback, danger = false, options = {}) {
    const context = state.snapshot.accountContext;
    modal(
      title,
      body,
      `<button class="button" data-modal-close>Cancel</button><button id="confirm-action" class="button ${danger ? 'danger' : 'primary'}">${escape(label)}</button>`
    );
    const generation = modalContentGeneration, dialog = $('#modal'), control = $('#confirm-action');
    let used = false;
    control.disabled = false;
    control.onclick = () => {
      if (used || control.disabled || !dialog.open || generation !== modalContentGeneration || $('#confirm-action') !== control)
        return false;
      used = true;
      dialog.close();
      if (!options.contextIndependent && context !== state.snapshot.accountContext) {
        toast('The selected installation changed. Review this action again for the current installation.', 'warning');
        return false;
      }
      return callback();
    };
  }
  function bindModAssetDetails(id) {
    const details = $('#detail-assets');
    if (!details) return;
    let loaded = false;
    details.ontoggle = async () => {
      if (!details.open || loaded) return;
      loaded = true;
      const content = $('#detail-assets-content');
      try {
        const result = state.preview ? { assets: getMod(id)?.assets || [], assetCount: getMod(id)?.assetCount || 0 }
          : await request('modAssetDetails', { id });
        if ($('#detail-assets') !== details) return;
        const assets = Array.isArray(result.assets) ? result.assets.slice(0, 64) : [];
        content.innerHTML = `<p class="field-note">${assets.length} shown of ${escape(result.assetCount ?? assets.length)} paths. Overlap checks use the complete native index.</p>${assets.length ? `<ul class="asset-paths">${assets.map((asset) => `<li><code>${escape(asset)}</code></li>`).join('')}</ul>` : '<p>No readable paths are available for this package.</p>'}`;
      } catch {
        if ($('#detail-assets') === details) content.textContent = 'The package index could not be read. Close and reopen Details to retry.';
      }
    };
  }
  function showModDetails(id, options = {}) {
    const m = getMod(id);
    if (!m) return;
    const manualNative = manualNativeMod(m), broken = brokenInstallationRecord(m),
      notes = Array.isArray(m.notes) ? m.notes : m.notes ? [m.notes] : [];
    const incomplete = compatibilityCheckReason();
    const compatibility =
      typeof m.compatibility === 'string'
        ? m.compatibility
        : m.compatibility?.summary ||
          m.compatibilityNotes ||
          (manualNative ? 'PAK conflict scans and profiles do not inspect or switch off native plugins. Compatibility must be checked against the plugin instructions.' : m.imported || m.assetCount || m.assets?.length
            ? `Compatibility depends on the game files each mod replaces.${m.conflictsWithInstalled?.length ? '' : incomplete ? ' Checks are incomplete. Run Diagnostics before relying on the overlap result.' : ' No conflicts with your installed mods.'}`
            : 'Compatibility depends on the game files each mod replaces. Import it to check it against your installed mods.');
    const details = `<div class="mod-detail-heading">${modArtwork(m, { detail: true })}<div><div class="detail-category">${escape(modCategory(m))}</div><div class="detail-metadata">${isLocalMod(m) && !m.catalogMatch ? '<span class="badge local-origin">Added from a file</span>' : versionLabel(m.version) ? `<span class="badge">${escape(versionLabel(m.version))}</span>` : ''}<span class="badge" id="detail-activation-state">${m.imported || m.enabled ? activationLabel(m) : escape(statusFor(m).label)}</span>${m.imported && statusFor(m).label !== activationLabel(m) ? `<span class="badge status-warning">${escape(statusFor(m).label)}</span>` : ''}${m.variant ? `<span class="badge">${escape(m.variant)}</span>` : ''}</div></div></div><div class="detail-body"><p>${escape(m.description || m.summary)}</p>${broken ? `<div class="banner warning"><div><strong>${m.status === 'missing' ? 'The recorded installed file is missing.' : 'The current file differs from the recorded installation.'}</strong><p>Review the old installation record before trying another file change. Forgetting a record only removes ownership metadata; it does not delete or replace a PAK.</p>${m.forgetInstallationReason ? `<p>${escape(m.forgetInstallationReason)}</p>` : ''}</div></div>` : ''}${notes.length ? `<h3>Before you install</h3><ul>${notes.map((n) => `<li>${escape(n)}</li>`).join('')}</ul>` : ''}${
      m.variantGroup && state.snapshot.mods.filter((other) => other.variantGroup === m.variantGroup).length > 1
        ? `<h3>Choose one variant</h3><p>${state.snapshot.mods
            .filter((other) => other.variantGroup === m.variantGroup)
            .map((other) => escape(other.name))
            .join(' · ')}. These are alternatives.</p>`
        : ''
    }${isLocalMod(m) ? `<p class="inline-muted">${m.catalogMatch ? 'Added from a file. It is exactly the published release, so it is shown as that release.' : 'Added from a file on your computer. The toolkit checked the file itself, but cannot confirm who made it or look for updates.'}</p>` : ''}${m.readme ? `<details class="finding-evidence mod-readme"><summary>Notes from the download</summary><pre>${escape(m.readme)}</pre></details>` : ''}<section id="detail-personal-notes">${renderPersonalModNotes(m)}</section><h3>Compatibility</h3><p>${escape(compatibility)}</p>${m.conflictsWithInstalled?.length ? `<div class="banner warning">${icon('alert')}<p><strong>Conflicts with your current mods.</strong> ${m.conflictsWithInstalled.map((c) => `${escape(c.name || modName(c.id))} (${escape(c.count)} shared game file${Number(c.count) === 1 ? '' : 's'})`).join('; ')}. ${m.enabled ? 'Keep only one of them enabled, unless the authors made this combination work together.' : 'Switching it on offers to swap them.'}</p></div>` : ''}${m.requires ? `<h3>Requirements</h3><p>${escape(Array.isArray(m.requires) ? m.requires.join(' · ') : m.requires)}</p>` : ''}${manualNative ? '' : `<p class="inline-muted">${m.available === false ? escape(m.unsupportedReason || 'This release is not available for managed installation.') : m.imported ? 'The package is stored in your local library. Disabling keeps that copy available.' : 'Choose Import mod to add it to your library.'}</p>`}</div>`;
    const nexusAction =
      isLocalMod(m) && !m.catalogMatch
        ? ''
        : `<button class="button" id="detail-nexus">${icon('external')}${manualNative ? 'Setup on Nexus' : 'Nexus page'}</button>`;
    const removeAction = canRemoveMod(m) ? `<button class="button danger" id="detail-remove">${icon('trash')}Remove mod</button>` : '';
    const restoreAction = m.canRestoreLibraryCopy === true ? `<button class="button" id="detail-restore-library">${icon('folder')}Restore library copy</button>` : '';
    const packageActions = manualNative ? '' : broken
      ? `${removeAction}<button class="button primary" data-record-review="${escape(m.id)}">${icon('report')}Review installation record</button>`
      : m.imported || m.enabled
        ? `${removeAction}${restoreAction}${m.enabled || m.available !== false ? `<button class="button primary" id="detail-toggle">${icon(m.enabled ? 'close' : 'check')}${m.enabled ? 'Disable' : 'Enable'}</button>` : ''}`
        : `${removeAction}${m.available === false ? '' : '<button class="button primary" id="detail-import">' + icon('plus') + 'Import mod</button>'}`;
    const howToGet = manualNative
      ? `<div class="banner neutral">${icon('info')}<div><p><strong>Manual native plugin setup.</strong> Follow the instructions for this file on Nexus with Days Gone closed. The toolkit cannot tell whether the ASI is installed or running, and does not install or remove its loader.</p><button type="button" class="button small" id="detail-asi-guide">${icon('book')}Read the ASI loader guide</button></div></div>`
      : localEdition() && getModRow(m)
        ? m.download
          ? `<div class="banner neutral">${icon('download')}<p>This mod is one of several PAKs in the <strong>${escape(m.download)}</strong> download. Open it on Nexus Mods, use <strong>Manual download</strong> and save the ZIP. Then choose <strong>Import mod</strong> here, pick the ZIP and tick the parts you want.</p></div>`
          : `<div class="banner neutral">${icon('download')}<p>This version downloads mods through your browser. Open the file on Nexus Mods, use <strong>Manual download</strong> and save it. Then choose <strong>Import mod</strong> here and pick the PAK or ZIP you downloaded.</p></div>`
        : '';
    const libraryNote = m.canRestoreLibraryCopy === true
      ? '<div class="banner neutral"><p>The installed PAK is intact. Restore its library copy to keep it ready for later; the installed file stays in place.</p></div>'
      : m.canRemoveLibrary === true && !m.imported && !m.enabled
        ? '<p class="field-note">The library file is gone. Import the original download to use it again, or remove the saved entry.</p>' : '';
    const assetEvidence = manualNative ? '' : `<details class="finding-evidence" id="detail-assets"><summary>Game files · ${escape(m.assetCount ?? m.assets?.length ?? 0)} paths</summary><div id="detail-assets-content">Expand to read a sample of the package index.</div></details>`;
    showModPanel(m, details + libraryNote + renderCharacterAppearance(m) + assetEvidence + howToGet + (manualNative ? '' : '<p id="detail-action-reason" class="field-note" hidden></p>'), nexusAction + packageActions, options);
    bindCharacterPreview(m);
    if (!manualNative) bindModAssetDetails(id);
    if (manualNative && $('#detail-asi-guide')) $('#detail-asi-guide').onclick = () => {
      if (state.detailId !== id || $('#mod-details-panel').hidden) return false;
      closeModDetails(); state.view = 'help'; state.helpSection = 'modding'; state.article = 'modding-asi'; state.helpSearch = '';
      render(); window.scrollTo?.(0, 0); $('#main-content').focus({ preventScroll: true });
    };
    const personal = $('#detail-personal-notes');
    if (personal) personal.dataset.modId = id;
    bindPersonalModNotes(id);
    if (!state.libraryMetaLoaded && !state.preview) loadLibraryMeta().then(() => {
      const current = $('#detail-personal-notes');
      if (state.detailId === id && !$('#mod-details-panel').hidden && current?.dataset.modId === id) {
        current.innerHTML = renderPersonalModNotes(m);
        bindPersonalModNotes(id);
      }
    });
    bindRecordReviewControls();
    if ($('#detail-nexus') && (!isLocalMod(m) || m.catalogMatch))
      $('#detail-nexus').onclick = () =>
        run('openNexusFile', { modId: nexusModId(m), fileId: nexusFileId(m) }, { refresh: false });
    if (!broken && (m.imported || m.enabled)) {
      const toggle = $('#detail-toggle');
      if (toggle) toggle.onclick = () => {
        if (!libraryDocked()) closeModDetails();
        return toggleMod(id);
      };
    } else if (!broken && $('#detail-import'))
      $('#detail-import').onclick = () => {
        closeModDetails();
        handleAction('import');
      };
    const remove = $('#detail-remove');
    if (canRemoveMod(m) && remove) remove.onclick = () => reviewRemoveMod(id);
    const restore = $('#detail-restore-library');
    if (m.canRestoreLibraryCopy === true && restore) restore.onclick = async () => {
      const current = getMod(id);
      if (current?.canRestoreLibraryCopy !== true || modActionBlockReason(current)) return;
      await run('restoreLibraryCopy', { id }, { progress: 'Restoring the library copy…', success: 'Library copy restored. The installed PAK stayed in place.' });
      if (getMod(id)) showModDetails(id);
    };
    if (state.profileSession.active) {
      ['#detail-toggle', '#detail-remove', '#detail-import', '#detail-restore-library'].forEach((selector) => {
        const control = $(selector);
        if (control) {
          control.disabled = true;
          control.title = 'Wait for the profile session to restore original files.';
        }
      });
    }
    applyModActionLocks();
  }
  function reviewRemoveMod(id) {
    const mod = getMod(id), context = state.snapshot.accountContext, hash = mod?.sha256;
    if (!canRemoveMod(mod) || modActionBlockReason(mod)) return false;
    const stale = !mod.imported && !mod.enabled;
    confirmModal(stale ? 'Remove this saved entry?' : 'Remove this mod?',
      `<p>${stale ? 'Remove the outdated library entry for' : 'Remove'} <strong>${escape(mod.name)}</strong>${stale ? '?' : ' from the game and from the toolkit? To use it again later, import it again.'}</p><p class="field-note">${stale ? 'Missing files are not needed to remove the record. ' : ''}Your original download, other mods, portable profile copies and saves stay as they are.</p>`,
      stale ? 'Remove saved entry' : 'Remove mod', async () => {
        const current = getMod(id);
        if (context !== state.snapshot.accountContext || current?.sha256 !== hash || !canRemoveMod(current)) return;
        closeModDetails(false);
        if (current.enabled) {
          const disabled = await run('disable', { id });
          if (disabled == null || context !== state.snapshot.accountContext) return;
        }
        const removed = await run('remove', { id }, { success: stale ? 'Saved entry removed.' : `${mod.name} removed from the toolkit.` });
        if (removed != null && context === state.snapshot.accountContext) {
          for (const value of [hash, mod.archiveSha256, mod.provenance?.sourceSha256, mod.provenance?.downloadSha256]) {
            const digest = contentDigest(value); if (digest) intentionallyRemovedDownloadHashes.add(digest);
          }
          while (intentionallyRemovedDownloadHashes.size > 1024)
            intentionallyRemovedDownloadHashes.delete(intentionallyRemovedDownloadHashes.values().next().value);
          reconcileImportNotices(); render(); scheduleDownloadContinuation();
        }
      }, true);
    return true;
  }
  async function reviewForgetInstallation(id) {
    if (state.busy || state.preview) return false;
    if (!(await refresh({ quiet: true, preserveForm: true, background: true }))) {
      toast('Could not check this mod’s file. Try again in a moment.', 'warning');
      return false;
    }
    const mod = getMod(id);
    if (!brokenInstallationRecord(mod) || mod.canForgetInstallation !== true || installationRecordReason(mod)) {
      toast(
        installationRecordReason(mod) ||
          'This installation record cannot be forgotten. Use Disable for an unchanged installed package.',
        'warning'
      );
      return false;
    }
    const plan = await run('reviewForgetInstallation', { id }, { refresh: false, progress: 'Checking the file…' });
    if (!plan?.planToken || !plan.mod || !plan.current) return false;
    const current = plan.current,
      original = plan.original || {},
      present = current.present === true;
    modal(
      'Forget the old installation record?',
      `<p>The toolkit still remembers installing <strong>${escape(plan.mod.name || mod.name)}</strong>, but that file was ${present ? 'replaced' : 'deleted'} outside the toolkit. Forgetting it lets you manage this mod normally again. <strong>No file is deleted, moved or replaced.</strong></p><details class="finding-evidence"><summary>File details</summary><dl class="detail-grid"><dt>Recorded package</dt><dd><code>${escape(original.file || plan.mod.packageFile || 'Not supplied')}</code></dd><dt>Recorded SHA-256</dt><dd><code>${escape(original.sha256 || plan.mod.sha256 || 'Not supplied')}</code></dd><dt>Installed path</dt><dd><code>${escape(current.path || 'Not supplied')}</code></dd><dt>Current file</dt><dd>${present ? 'Present · different from the recorded package · left untouched' : 'Missing · no file exists at the recorded path'}</dd>${present ? `<dt>Current SHA-256</dt><dd><code>${escape(current.sha256 || 'Not available')}</code></dd><dt>Current size</dt><dd>${formatBytes(current.bytes)}</dd>` : ''}</dl></details>${plan.warnings?.length ? `<div class="banner warning"><ul>${plan.warnings.map((w) => `<li>${escape(typeof w === 'string' ? w : w.message || 'Review required')}</li>`).join('')}</ul></div>` : ''}<p class="field-note">${present ? 'The new file then shows under Installed outside the toolkit, where you can manage it.' : 'Your library copy stays, so you can enable the mod again.'} The toolkit checks everything again right before it forgets the record.</p>`,
      `<button class="button" data-modal-close>Cancel</button><button class="button primary" id="forget-installation-confirm">${icon('report')}Forget old installation record</button>`
    );
    $('#forget-installation-confirm').onclick = async () => {
      $('#modal').close();
      const result = await run(
        'forgetInstallation',
        { id, planToken: plan.planToken, confirmed: true },
        {
          progress: 'Forgetting the old record…',
          success: 'Old installation record forgotten. Existing files were left untouched.'
        }
      );
      if (result) navigate('mods');
    };
    return true;
  }
  async function handleAction(action) {
    if (action === 'character-appearance') { openCharacterAppearance(); return; }
    if (action === 'character-all-mods') { navigate('mods'); return; }
    if (action === 'workshop-enter') { state.workshopError = ''; navigate('workshop'); return; }
    if (action === 'workshop-guide') { state.helpSection = 'modding'; navigate('help'); return; }
    if (action === 'workshop-next-choice') { await nextWorkshopChoice(); return; }
    if (action === 'workshop-open') { await browseWorkshop(); return; }
    if (action === 'workshop-new') { startWorkshopAsset(); return; }
    if (action === 'workshop-empty') { await newWorkshopPackage(); return; }
    if (action === 'workshop-file-add') { openWorkshopFileInput('add'); return; }
    if (action === 'workshop-file-replace') { openWorkshopFileInput('replace'); return; }
    if (action === 'workshop-file-rename') { openWorkshopFileInput('rename'); return; }
    if (action === 'workshop-file-remove') { changeWorkshopFile('remove'); return; }
    if (action === 'workshop-file-restore') { changeWorkshopFile('restore'); return; }
    if (action === 'workshop-file-reset') { changeWorkshopFile('reset'); return; }
    if (action === 'workshop-file-extract') { await extractWorkshopFile(); return; }
    if (action === 'workshop-projects') { await showWorkshopProjects(); return; }
    if (action === 'workshop-project-save') { await saveWorkshopProject(); return; }
    if (action === 'workshop-close') { closeWorkshop(); return; }
    if (action === 'workshop-build') { await reviewWorkshopBuild(); return; }
    if (action === 'workshop-changes') { await reviewWorkshopChanges(); return; }
    if (action === 'workshop-revert-inputs') { if (!state.workshopWorking) { clearWorkshopDraftInputs(); state.workshopError = ''; paintWorkshop(); } return; }
    if (action === 'workshop-undo') {
      if (!workshopPending() && state.workshop?.canUndo === true) {
        const selected = state.workshopSelection, result = await workshopMutation('workshopUndo', {}, 'Undoing the last draft change…');
        if (result) { state.workshopMerge = null; if (selected) await selectWorkshopAsset(selected); }
      }
      return;
    }
    if (localEdition() && /^(nexus-(check|link|connect|status|disconnect)|nxm-|api-keys)/.test(action)) {
      localEditionInfo();
      return;
    }
    if (action === 'storage-browse') { await chooseStorageLocation(); return; }
    if (action === 'storage-open') { await run('openDataFolder', {}, { refresh: false }); return; }
    if (action === 'storage-cancel') {
      const result = await run('storageCancel', { confirmed: true }, { refresh: false, success: 'Pending data move cancelled.' });
      if (result) {
        state.storage = result;
        if (result.copyRetainedAt) toast('Move cancelled. Your original data is unchanged; a partial copy may remain in the chosen folder.', 'warning');
        if (state.view === 'settings') render();
      }
      return;
    }
    if (action === 'external-manage-all') {
      await manageAllExternal();
      return;
    }
    if (action === 'release-all') {
      await releaseAllManaged();
      return;
    }
    if (action === 'update-install') {
      if (state.update.offer) confirmUpdate(state.update.offer);
    } else if (action === 'update-dismiss') {
      const offer = state.update.offer;
      state.update.offer = null;
      render();
      if (offer) request('updateDismiss', { version: offer.version }).catch(() => {});
    } else if (action === 'update-check') {
      await checkForUpdate(false);
    } else if (action === 'update-browse') {
      const found = await run('updateBrowse', {}, { refresh: false, progress: 'Checking the update file…' });
      if (found && found.found && typeof found.token === 'string') confirmUpdate(found);
    } else if (action === 'catalog-check') {
      await checkForCatalog(false);
    } else if (action === 'catalog-browse') {
      const result = await run('catalogBrowse', {}, { refresh: false, progress: 'Checking the signed catalog…' });
      const offer = catalogOffer(result);
      if (offer) confirmCatalog(offer);
    } else if (action === 'catalog-review') {
      if (state.catalogUpdate.offer) confirmCatalog(state.catalogUpdate.offer);
    } else if (action === 'catalog-dismiss') {
      const offer = state.catalogUpdate.offer;
      state.catalogUpdate.offer = null;
      render();
      if (offer) request('catalogDismiss', { revision: offer.revision }).catch(() => {});
    } else if (action === 'update-page') {
      run('openToolkitPage', {}, { refresh: false });
    } else if (action === 'finder-open') {
      openFinder();
    } else if (action === 'finder-stop') {
      reviewFinderStop();
    } else if (action === 'mods-more') {
      state.modLimit += 50;
      render();
    } else if (action === 'downloads-more') {
      state.downloadLimit += 50;
      render();
    } else if (action === 'discoveries-more') {
      state.discoveryLimit += 50;
      render();
    } else if (action === 'conflicts-more') {
      state.conflictLimit = Math.min(state.snapshot.conflicts.length, state.conflictLimit + 50);
      render();
    } else if (action === 'portable-refresh') {
      await loadPortableProfiles();
      render();
    } else if (action === 'portable-save') {
      savePortableProfile();
    } else if (action === 'profile-restore') {
      restoreProfileSession();
    } else if (action === 'directories-scan') {
      await loadDirectories();
    } else if (action === 'save-files') {
      setSavesTab('current');
    } else if (action === 'save-import') {
      await importSaveFiles();
    } else if (action === 'save-backups-refresh') {
      if (state.savesTab === 'current') { resetCurrentPreviewFailures(true); await loadCurrentSaves(); }
      else await loadSaveBackups();
    } else if (action === 'save-backups') {
      setSavesTab('backups');
    } else if (action === 'save-backups-toggle') {
      await saveBackupOptions(state.saveBackups?.settings?.automatic !== true, Number(state.saveBackups?.settings?.keep) || 5);
    } else if (action === 'save-backups-recovery') {
      await reviewSaveBackup('', true);
    } else if (action === 'backup-saves') {
      const result = await run(
        'backupSaves',
        {},
        { refresh: false, progress: 'Copying your saves to a backup folder…' }
      );
      if (result)
        toast(
          `Saves backed up: ${result.files} file${result.files === 1 ? '' : 's'}, ${formatBytes(result.bytes)}.`,
          'success',
          {
            label: 'Show',
            run: async () => {
              setSavesTab('backups');
            }
          }
        );
      if (result) await loadSaveBackups();
    } else if (action === 'whats-new') {
      showWhatsNew();
    } else if (action === 'toolkit-release-dismiss') {
      const version = state.toolkitRelease.status?.version;
      if (newerToolkitVersion(version, state.snapshot.appVersion)) {
        state.toolkitRelease.dismissedVersion = version;
        storeValue('drifter.toolkitUpdateDismissed', version);
        render();
      }
    } else if (action === 'changelog') {
      showChangelog();
    } else if (action === 'library-backup-export') {
      confirmLibraryBackupExport();
    } else if (action === 'library-backup-restore') {
      await reviewLibraryBackup();
    } else if (action === 'library-backup-recovery') {
      reviewLibraryBackupRecovery();
    } else if (action === 'library-tools') {
      openLibraryTools();
    } else if (action === 'combination-review') {
      openCombinationReview();
    } else if (action === 'conflict-guide') {
      await startConflictGuide();
    } else if (action === 'open-shared-list') {
      openSharedModList();
    } else if (action === 'share-list') {
      shareModList();
    } else if (action === 'undo') {
      await undoLastChange();
    } else if (action === 'loading-setup') {
      await loadDirectories();
      confirmDirectoryChange('prepare');
    } else if (action === 'directories-prepare' || action === 'directories-restore') {
      confirmDirectoryChange(action === 'directories-prepare' ? 'prepare' : 'restore');
    } else if (action === 'import' || action === 'import-folder') {
      const result = await run(
        action === 'import-folder' ? 'browseImportFolder' : 'browseImports',
        {},
        { refresh: false, progress: action === 'import-folder' ? 'Choose a folder of mod downloads…' : 'Choose mod files or archives…' }
      );
      if (!result) return;
      if (result.cancelled) {
        toast('Import cancelled.');
        return;
      }
      await showImportResult(result);
    } else if (action === 'open-folder') await run('openModsFolder', {}, { refresh: false });
    else if (action === 'scan')
      await run(
        'scan',
        {},
        {
          progress: 'Checking your mods…',
          success: 'Check finished. Conflicts and files that could not be read are listed on the Conflicts page.'
        }
      );
    else if (action === 'crash-refresh') {
      await loadCrashLogs();
    } else if (action === 'support-preview') {
      await reviewSupportBundle();
    } else if (action === 'diagnose') {
      await loadDiagnosticReport();
    } else if (action === 'diagnostics-duplicates') {
      await reviewDuplicateCleanup();
    } else if (action === 'diagnostics-export') {
      const result = await run('diagnosticsExport', {}, { refresh: false });
      if (result && !result.cancelled)
        toast(result.path ? `Findings saved to ${result.path}` : 'Diagnostic findings exported.');
    } else if (action === 'export') {
      const r = await run('exportReport', {}, { refresh: false });
      if (r && !r.cancelled) toast(r.path ? `Report saved to ${r.path}` : 'Diagnostic report exported.');
    } else if (action === 'author-profile') await run('openAuthorProfile', {}, { refresh: false });
    else if (action === 'nexus') await run('openNexus', {}, { refresh: false });
    else if (action === 'launch') await launchOrReviewRestart();
    else if (action === 'restart-cancel') {
      const status = await run('gameRestartCancel', {}, { refresh: false });
      if (status) {
        state.gameRestart = status;
        render();
      }
    } else if (action === 'browse-game') {
      const result = await run('browseGame', {}, { refresh: false });
      const path = typeof result === 'string' ? result : result?.path || result?.gamePath || result?.settings?.gamePath;
      if (path) {
        state.gamePathError = null; updateGamePathError();
        state.snapshot.settings.gamePath = path;
        if (state.view === 'settings' && $('#game-path')) {
          $('#game-path').value = path;
          $('#settings-note').textContent = 'Game folder saved.';
          markPendingSave('game-path');
          flashSaved('Game folder saved');
        } else if (state.view === 'folders') await loadDirectories();
        else await refresh({ quiet: true });
      }
    } else if (action === 'guide-back') {
      const article = (window.FIELD_GUIDE || []).find((item) => item.id === state.article);
      state.helpSection = article?.modding ? 'modding' : 'help';
      state.article = null;
      render();
    } else if (action === 'save-profile') {
      modal(
        'Save your current setup',
        `<p>This profile will remember ${enabledMods().length} enabled managed mod${enabledMods().length === 1 ? '' : 's'}. It does not include PAKs installed by hand or saves.</p><label class="field-label" for="profile-name">Profile name</label><input id="profile-name" class="input full-width" maxlength="64" placeholder="e.g. First playthrough" autocomplete="off">`,
        '<button class="button" data-modal-close>Cancel</button><button id="save-profile-confirm" class="button primary">Save profile</button>'
      );
      $('#profile-name').focus();
      $('#save-profile-confirm').onclick = () => {
        const name = $('#profile-name').value.trim();
        if (!name) {
          $('#profile-name').focus();
          return;
        }
        if (profileList().some((p) => p.name === name)) {
          toast('A profile with that name already exists. Choose a different name.', 'warning');
          return;
        }
        $('#modal').close();
        run('saveProfile', { name }, { success: 'Current setup saved as a profile.' });
      };
      $('#profile-name').onkeydown = (e) => {
        if (e.key === 'Enter') $('#save-profile-confirm').click();
      };
    } else if (action === 'nexus-connect') connectNexus();
    else if (action === 'nexus-link') authorizedNexusLink();
    else if (action === 'nxm-register' || action === 'nxm-unregister') configureNxm(action === 'nxm-register');
    else if (action === 'api-keys') await run('openApiKeys', {}, { refresh: false });
    else if (action === 'downloads-folder') await run('openDownloadsFolder', {}, { refresh: false });
    else if (action === 'nexus-disconnect')
      confirmModal(
        'Disconnect Nexus Mods?',
        `<p>Remove the saved Nexus authorization from this computer? Your downloaded packages and managed mods will remain available.</p>`,
        'Disconnect',
        async () => {
          const r = await run('nexusDisconnect', {}, { refresh: false });
          if (r !== null) {
            clearLiveNexusContent();
            state.nexus = { connected: false, premium: false, browserLoginAvailable: r?.browserLoginAvailable };
            render();
            toast('Nexus Mods disconnected.');
          }
        }
      );
    else if (action === 'nexus-status') {
      const status = await run(
        'nexusStatus',
        {},
        { refresh: false, progress: 'Checking your saved Nexus connection…' }
      );
      if (status) {
        state.nexus = status;
        render();
        toast(
          status.tierVerified === false
            ? status.message || 'Nexus could not verify membership yet.'
            : status.connected
              ? 'Nexus connection checked.'
              : 'Reconnect your Nexus account to continue.',
          status.tierVerified === false ? 'warning' : 'success'
        );
        await checkAutomaticReleases();
      }
    } else if (action === 'nexus-check') {
      clearLiveNexusContent();
      render();
      const r = await run(
        'nexusCheckUpdates',
        {},
        { refresh: false, progress: 'Checking the author’s Nexus releases…' }
      );
      if (r) {
        try {
          await loadLiveInventory();
        } catch (error) {
          toast(errorText(error), 'warning');
        }
        acceptReleaseCheck(r);
        render();
        toast('Nexus release list updated.');
      }
    }
  }
  function renderNxmSetting() {
    const status = state.nxm;
    return `<div class="setting-row"><div class="setting-copy"><strong>Browser download integration</strong><p>${status.registered ? 'Nexus Mod Manager Download links can open this toolkit. Nexus account authorization and package verification still apply.' : status.available ? 'Optionally let Nexus Mod Manager Download links open the toolkit. Existing handlers, including Vortex, are never replaced.' : escape(status.reason || 'Another application owns Nexus links, or integration is unavailable. Use the authorized-link paste fallback.')}</p></div>${status.registered ? button('Disable integration', 'nxm-unregister', 'close', 'small') : status.available ? button('Enable integration', 'nxm-register', 'external', 'small') : '<span class="badge">PASTE LINK AVAILABLE</span>'}</div>`;
  }
  function renderNxmBanner() {
    if (state.nxm.registered)
      return `<div class="banner neutral">${icon('external')}<p><strong>Browser download integration is enabled.</strong> Nexus Mod Manager Download links can open the toolkit. Complete Nexus’s authorization in your browser; package checks still run before import. You can also use the link-paste fallback.</p></div>`;
    return `<div class="info-card mb-20"><div class="info-card-head">${icon('external')}Prefer to download from your browser?</div><p>${state.nxm.available ? 'Optional browser integration can receive Nexus Mod Manager Download links. Enable it in Settings after reviewing the confirmation.' : escape(state.nxm.reason || 'Browser integration is not enabled. Existing mod-manager associations are preserved.')}</p><div class="inline-actions mt-16">${state.nxm.available ? button('Enable browser integration', 'nxm-register', 'external', 'small') : ''}${button('Use an authorized link', 'nexus-link', 'lock', 'small')}</div></div>`;
  }
  async function configureNxm(register) {
    if (register && !state.nxm.available) {
      toast(state.nxm.reason || 'Nexus links are already assigned to another application.', 'warning');
      return;
    }
    confirmModal(
      register ? 'Open Nexus download links with the toolkit?' : 'Disable browser download integration?',
      register
        ? '<p>This registers the toolkit for <strong>Nexus Mod Manager Download</strong> links on this user account. A website-authorized Nexus link can open the toolkit and start the download checks.</p><p>Integration is offered only when no existing handler owns these links. The toolkit will not replace Vortex or another mod manager. Your browser may ask before opening an application.</p><p>You still need to connect your Nexus account. You can disable this integration later and keep using the authorized-link paste fallback.</p>'
        : '<p>Remove the browser download association owned by this toolkit? Your downloaded packages, imported mods and Nexus account connection remain available. You can keep using the authorized-link paste fallback.</p>',
      register ? 'Enable integration' : 'Disable integration',
      async () => {
        const result = await run(register ? 'nxmRegister' : 'nxmUnregister', { confirmed: true }, { refresh: false });
        if (result) {
          try {
            state.nxm = await request('nxmStatus');
          } catch {
            state.nxm = result;
          }
          render();
          toast(
            register
              ? 'Browser download integration enabled. Your browser may ask before opening the toolkit.'
              : 'Browser download integration disabled.'
          );
        }
      }, false, { contextIndependent: true }
    );
  }
  async function handleNativeDownloadResult(result) {
    if (result.ok === false) {
      toast(errorText(result.error), 'error');
      return;
    }
    const data = result.data || {};
    if (reviewDownloadedPackage(data)) return;
    toast(
      data.imported
        ? 'Verified Nexus download imported into your library.'
        : data.message || 'Nexus download received. Review its package verification before importing.',
      data.imported ? 'success' : 'warning'
    );
    scheduleFilesystemRefresh();
  }
  let loginPollTimer = null,
    loginPending = false,
    loginPollRunning = false,
    loginAttempt = null;
  function currentLoginAttempt(attempt) {
    return !!attempt && loginAttempt === attempt && attempt.context === state.snapshot.accountContext &&
      attempt.modalGeneration === modalContentGeneration && $('#modal').open;
  }
  function cancelNexusLogin() {
    loginAttempt = null;
    if (loginPollTimer) {
      clearTimeout(loginPollTimer);
      loginPollTimer = null;
    }
    if (loginPending) {
      loginPending = false;
      request('nexusCancelLogin').catch(() => {});
    }
  }
  let automaticReleaseCheck = null;
  async function checkAutomaticReleases() {
    if (localEdition() || !state.nexus.connected || state.snapshot.settings.autoCheckUpdates === false) return;
    if (automaticReleaseCheck) return automaticReleaseCheck;
    const context = state.snapshot.accountContext;
    automaticReleaseCheck = (async () => {
      let releases = null;
      clearLiveNexusContent();
      if (state.view !== 'settings') render();
      try {
        releases = await request('nexusCheckUpdates');
        if (context !== state.snapshot.accountContext) return;
        await loadLiveInventory();
        if (context !== state.snapshot.accountContext) return;
        state.downloads.verificationUnavailable = false;
      } catch (error) {
        if (context === state.snapshot.accountContext) {
          state.downloads.verificationUnavailable = true;
          state.downloads.releaseError = errorText(error);
        }
      } finally {
        if (context === state.snapshot.accountContext) {
          if (releases) acceptReleaseCheck(releases);
          if (state.view !== 'settings') render();
        }
        automaticReleaseCheck = null;
      }
    })();
    return automaticReleaseCheck;
  }

  function finishNexusLogin(result, attempt = loginAttempt) {
    if (!currentLoginAttempt(attempt)) return false;
    loginPending = false;
    if (loginPollTimer) {
      clearTimeout(loginPollTimer);
      loginPollTimer = null;
    }
    state.nexus = { ...result, connected: true };
    $('#modal').close();
    render();
    toast('You’re connected to Nexus Mods.');
    checkAutomaticReleases();
    return true;
  }
  async function pollNexusLogin() {
    loginPollTimer = null;
    const attempt = loginAttempt;
    if (!loginPending || !currentLoginAttempt(attempt)) return;
    if (loginPollRunning) { loginPollTimer = setTimeout(pollNexusLogin, 2000); return; }
    loginPollRunning = true;
    try {
      const result = await request('nexusPollLogin');
      if (!loginPending || !currentLoginAttempt(attempt)) return;
      if (result?.connected) {
        finishNexusLogin(result, attempt);
        return;
      }
      if (result?.pending) {
        loginPollTimer = setTimeout(pollNexusLogin, 2000);
      } else {
        loginPending = false;
        toast(result?.message || 'The Nexus sign-in request ended. You can try connecting again.', 'warning');
        if ($('#nexus-login-state'))
          $('#nexus-login-state').textContent = 'Sign-in ended. Close this window to try again.';
      }
    } catch (error) {
      if (loginPending && currentLoginAttempt(attempt)) {
        loginPending = false;
        toast(errorText(error), 'error');
        if ($('#nexus-login-state'))
          $('#nexus-login-state').textContent = 'Could not complete sign-in. Close this window and try again.';
      }
    } finally {
      loginPollRunning = false;
    }
  }
  function connectNexus() {
    if (localEdition()) return localEditionInfo();
    cancelNexusLogin();
    const browser = browserLoginAvailable();
    modal(
      'Connect to Nexus Mods',
      `<div class="health-summary"><div class="health-emblem">${icon('lock')}</div><div><h2>Connect through Nexus Mods.</h2><p>${browser ? 'Continue in your browser on the official Nexus website.' : 'Browser sign-in is not available in this build yet.'}</p></div></div><p>${browser ? 'Sign in to your Nexus account, then approve the toolkit’s connection. Your password stays in your browser.' : 'Browser sign-in needs an application reference that Nexus has not issued for this build yet, so that button is unavailable. Your password is never needed.'} The toolkit protects its saved authorization with your operating system.</p><p>Premium accounts support automated downloads. Free accounts use Nexus’s authorized download links.</p><div class="banner neutral"><span id="nexus-login-state">${browser ? 'When you’re ready, open Nexus Mods below. This window will update when you finish.' : 'Until registration arrives, connect with your own personal API key below.'}</span></div><details class="disclosure" id="nexus-key-disclosure"${browser ? '' : ' open'}><summary>Use my personal API key instead</summary><p class="field-note">Advanced option for testing and personal use, which the Nexus API acceptable use policy allows for your own personal key. Create or copy the key on the Nexus API keys page. The toolkit checks it with Nexus, keeps it in your operating system’s credential store (Windows DPAPI or the Linux keyring) and never shows it again.</p><label class="field-label mt-16" for="nexus-api-key">Personal API key</label><input class="input full-width" id="nexus-api-key" type="password" autocomplete="off" spellcheck="false" maxlength="4096" placeholder="Paste your own key"><p class="field-note field-error" id="nexus-key-error" hidden></p><div class="inline-actions mt-16"><button type="button" class="button small" id="nexus-key-page" data-action="api-keys">${icon('external')}Open Nexus API keys</button><button type="button" class="button primary small" id="nexus-key-submit">${icon('lock')}Connect with key</button></div></details>`,
      `<button class="button" data-modal-close>Cancel</button><button id="nexus-login-start" class="button primary"${browser ? '' : ' disabled'}>${icon('external')}Continue on Nexus Mods</button>`
    );
    const attempt = { context: state.snapshot.accountContext, modalGeneration: modalContentGeneration };
    loginAttempt = attempt;
    $('#nexus-key-page').onclick = (e) => {
      e.preventDefault();
      handleAction('api-keys');
    };
    const keyInput = $('#nexus-api-key'),
      keyError = $('#nexus-key-error');
    const showKeyError = (message) => {
      if (!currentLoginAttempt(attempt)) return;
      keyError.textContent = message;
      keyError.hidden = false;
    };
    // The field is blanked before the request leaves so the key exists only in the one native request.
    let keySubmitting = false;
    const submitKey = async () => {
      if (!currentLoginAttempt(attempt) || keySubmitting) return false;
      if (state.busy) { showKeyError('Wait for the current action to finish, then connect.'); return false; }
      let apiKey = keyInput.value.trim();
      keyInput.value = '';
      keyError.hidden = true;
      keyError.textContent = '';
      if (!apiKey) {
        showKeyError('Paste your own Nexus API key first.');
        keyInput.focus();
        return;
      }
      const submit = $('#nexus-key-submit');
      keySubmitting = true;
      submit.disabled = true;
      const result = await run(
        'nexusConnect',
        { apiKey },
        {
          refresh: false,
          progress: 'Checking your personal API key with Nexus…',
          onError: (error) => {
            showKeyError(errorText(error));
            return true;
          }
        }
      );
      apiKey = '';
      keySubmitting = false;
      if (!currentLoginAttempt(attempt)) return false;
      submit.disabled = false;
      if (result?.connected) {
        finishNexusLogin(result, attempt);
        return;
      }
      if (result) showKeyError(result.message || 'Nexus did not confirm this key. Check it and try again.');
      keyInput.focus();
    };
    $('#nexus-key-submit').onclick = submitKey;
    keyInput.onkeydown = (e) => {
      if (e.key === 'Enter') {
        e.preventDefault();
        submitKey();
      }
    };
    $('#nexus-login-start').onclick = async () => {
      if (!browser || !currentLoginAttempt(attempt) || state.busy || loginPending) return;
      const startButton = $('#nexus-login-start');
      startButton.disabled = true;
      loginPending = true;
      const result = await run(
        'nexusBeginLogin',
        {},
        { refresh: false, progress: 'Opening secure Nexus sign-in in your browser…' }
      );
      if (!loginPending || !currentLoginAttempt(attempt)) return;
      if (!result) {
        loginPending = false;
        startButton.disabled = false;
        return;
      }
      if (result.connected) {
        finishNexusLogin(result, attempt);
        return;
      }
      startButton.textContent = 'Waiting for Nexus Mods…';
      $('#nexus-login-state').innerHTML =
        '<span class="loading-inline"><span class="spinner"></span>Finish signing in and authorize the toolkit in your browser. You can cancel here at any time.</span>';
      loginPollTimer = setTimeout(pollNexusLogin, 2000);
    };
  }
  function authorizedNexusLink() {
    const mods = state.snapshot.mods.filter(
      (m) => !isLocalMod(m) && (m.available !== false || m.installationPolicy === 'review-required')
    );
    modal(
      'Use an authorized Nexus link',
      `<p>For a free Nexus account, start the manager download on the official Nexus file page, then copy its <code>nxm://</code> link here. The link expires and only works for the account that authorized it.</p><label class="field-label" for="nxm-mod">Known mod, if applicable</label><select class="input full-width" id="nxm-mod"><option value="">New release · detect from Nexus</option>${mods.map((m) => `<option value="${escape(m.id)}">${escape(m.name)}</option>`).join('')}</select><label class="field-label mt-16" for="nxm-link">Authorized download link</label><input class="input full-width" id="nxm-link" type="password" autocomplete="off" spellcheck="false" placeholder="nxm://daysgone/mods/…/files/…"><p class="field-note">Treat this link like a temporary download token. It is cleared on submission and is not saved by the interface. You can enable optional browser download integration in Settings when no other mod manager owns Nexus links.</p>`,
      `<button class="button" data-modal-close>Cancel</button><button class="button primary" id="nxm-submit">${icon('download')}Download securely</button>`
    );
    $('#nxm-link').focus();
    $('#nxm-submit').onclick = async () => {
      let nxmUrl = $('#nxm-link').value.trim();
      const id = $('#nxm-mod').value;
      $('#nxm-link').value = '';
      if (!nxmUrl) return;
      $('#modal').close();
      const r = await run(
        'nexusDownload',
        {
          ...(id && !isLocalMod(getMod(id)) && getMod(id)?.identityStatus !== 'nexus-inspected' ? { id } : {}),
          nxmUrl
        },
        { progress: 'Validating authorized link and downloading…' }
      );
      nxmUrl = '';
      if (r && !reviewDownloadedPackage(r))
        toast(
          r.imported
            ? 'Verified package imported into your library.'
            : r.message || 'Download staged for review. Open the downloads folder to inspect it.',
          r.imported ? 'success' : 'warning'
        );
    };
  }
  function showDownloadFallback(fileId, modId, message, { authorization = false, connection = false } = {}) {
    if (localEdition()) {
      modal(
        'Download on Nexus, then import',
        '<p>This version downloads mods through your browser. Open the file on Nexus Mods, use <strong>Manual download</strong> and save it. Then choose <strong>Import mod</strong> here and pick the PAK or ZIP you downloaded.</p><p class="field-note">Downloading inside the toolkit arrives with an update once Nexus Mods approves the toolkit’s API access.</p>',
        `<button class="button" data-modal-close>Close</button><button class="button" id="download-fallback-browser">${icon('external')}Open Nexus file</button><button class="button primary" id="download-import">${icon('plus')}Import mod</button>`
      );
      $('#download-fallback-browser').onclick = () => run('openNexusFile', { modId, fileId }, { refresh: false });
      $('#download-import').onclick = () => {
        $('#modal').close();
        handleAction('import');
      };
      return true;
    }
    const title = authorization
      ? 'Authorize this download'
      : connection
        ? 'Connect before downloading'
        : 'Download could not be completed';
    const explanation = authorization
      ? 'Nexus requires Free accounts to authorize each file on its website. After authorization, the toolkit downloads and inspects the file here.'
      : connection
        ? 'Connect your own Nexus account to download through the toolkit. If connection is unavailable, you can download this file from Nexus and import it.'
        : 'The in-app download stopped. You can retry here or use the Nexus file page and import the downloaded PAK or ZIP.';
    modal(
      title,
      `<p>${explanation}</p>${message ? `<p class="field-note">${escape(message)}</p>` : ''}${authorization ? '<p>With browser integration enabled, use Mod Manager Download on Nexus to return the authorized transfer to the toolkit. Otherwise, paste the authorized link here.</p>' : ''}`,
      `<button class="button" data-modal-close>Close</button><button class="button" id="download-fallback-browser">${icon('external')}${authorization ? 'Authorize on Nexus' : 'Open Nexus file'}</button>${authorization ? '<button class="button primary" id="download-authorized-link">Use authorized link</button>' : connection ? '<button class="button primary" id="download-connect">Connect to Nexus</button>' : '<button class="button primary" id="download-retry">Try again here</button>'}`
    );
    $('#download-fallback-browser').onclick = () => {
      $('#modal').close();
      return run('openNexusFile', { modId, fileId }, { refresh: false });
    };
    if (authorization)
      $('#download-authorized-link').onclick = () => {
        $('#modal').close();
        authorizedNexusLink();
      };
    else if (connection)
      $('#download-connect').onclick = () => {
        $('#modal').close();
        connectNexus();
      };
    else
      $('#download-retry').onclick = () => {
        $('#modal').close();
        return downloadNexusFile(fileId, modId);
      };
    return true;
  }
  async function downloadNexusFile(fileId, modId = 994) {
    fileId = Number(fileId);
    modId = Number(modId);
    if (!Number.isSafeInteger(fileId) || fileId < 1 || !Number.isSafeInteger(modId) || modId < 1) {
      toast('Choose a valid Nexus file before downloading.', 'warning');
      return;
    }
    if (state.busy) return;
    const match = fileCatalogMod(fileId, modId);
    if (match?.available === false && match.installationPolicy !== 'review-required') {
      toast(match.unsupportedReason || 'The toolkit cannot install this mod. Open its details to see why.', 'warning');
      return;
    }
    if (!state.nexus.connected) {
      showDownloadFallback(fileId, modId, '', { connection: true });
      return;
    }
    if (state.nexus.tierVerified !== false && !state.nexus.offline && state.nexus.premium === false) {
      showDownloadFallback(fileId, modId, '', { authorization: true });
      return;
    }
    const r = await run(
      'nexusDownload',
      { ...(match && match.identityStatus !== 'nexus-inspected' ? { id: match.id } : {}), modId, fileId },
      {
        progress: 'Downloading the Nexus release and inspecting its actual package…',
        onError: (error) => showDownloadFallback(fileId, modId, errorText(error))
      }
    );
    if (r && !reviewDownloadedPackage(r))
      toast(
        r.imported
          ? 'Verified package imported into your library.'
          : r.message || 'Download staged. Review its package inspection before installation.',
        r.imported ? 'success' : 'warning'
      );
  }

  // Look-and-comfort settings save themselves a moment after they change. Settings that change how the game and
  // its files are handled (game folder, Steam start, automatic managing) wait for Save changes, and a bar stays in
  // view until they are saved.
  let autoSaveTimer = null,
    pendingAutoSavePreferences = null;
  function markPendingSave(key) {
    const saved = state.snapshot.settings || {},
      changed =
        key === 'game-path'
          ? ($('#game-path')?.value || '').trim() !== (saved.gamePath || '')
          : key === 'steam-launch'
            ? switchValue('#steam-launch-setting', saved.steamLaunch !== false) !== (saved.steamLaunch !== false)
            : switchValue('#auto-manage-setting', saved.autoManageExternal !== false) !==
              (saved.autoManageExternal !== false);
    const save = $('#' + key + '-save');
    if (save) save.hidden = !changed;
  }
  function pendingSaves() {
    return ['game-path', 'steam-launch', 'auto-manage'].some((key) => $('#' + key + '-save')?.hidden === false);
  }
  // A small "Saved" pill in the corner, wherever the change was made on the page. One pill, never a stack.
  let savedPillTimer = null;
  function flashSaved(text = 'Saved') {
    const region = $('#toasts');
    if (!region) return;
    let pill = $('#save-pill');
    if (!pill) {
      pill = document.createElement('div');
      pill.id = 'save-pill';
      pill.className = 'save-pill';
      pill.setAttribute('role', 'status');
      region.append(pill);
    }
    pill.innerHTML = `${icon('check')}<span>${escape(text)}</span>`;
    pill.hidden = false;
    if (savedPillTimer) clearTimeout(savedPillTimer);
    savedPillTimer = setTimeout(() => (pill.hidden = true), 1800);
  }
  function scheduleAutoSave() {
    // Capture while the Settings controls exist. The user can leave this page before the debounce finishes.
    // Game settings with their own Save button must retain their saved values.
    pendingAutoSavePreferences = {
      ...collectPreferences(),
      steamLaunch: state.snapshot.settings.steamLaunch !== false,
      autoManageExternal: state.snapshot.settings.autoManageExternal !== false
    };
    if (autoSaveTimer) clearTimeout(autoSaveTimer);
    autoSaveTimer = setTimeout(autoSavePreferences, 450);
    const note = $('#settings-note');
    if (note) note.textContent = 'Saving…';
  }
  async function autoSavePreferences() {
    autoSaveTimer = null;
    if (!pendingAutoSavePreferences) return;
    if (state.busy || state.nativeDropActivity?.running) {
      autoSaveTimer = setTimeout(autoSavePreferences, 450);
      return;
    }
    const preferences = pendingAutoSavePreferences,
      context = state.snapshot.accountContext, preview = appearancePreview;
    pendingAutoSavePreferences = null;
    const saved = await run(
      'savePreferences',
      { preferences },
      {
        refresh: false,
        onError: (error) => {
          const note = $('#settings-note');
          if (note) note.textContent = `This change could not be saved: ${errorText(error)}.`;
          toast(`This change could not be saved: ${errorText(error)}.`, 'error');
          return true;
        }
      }
    );
    if (!saved || context !== state.snapshot.accountContext) return;
    const checkedToolkitUpdates = state.snapshot.settings.autoCheckToolkitUpdates !== false;
    Object.assign(state.snapshot.settings, saved.settings || preferences);
    // A control changed during the request can already have a newer preview and its own queued save.
    if (appearancePreview === preview) appearancePreview = null;
    applyAppearance();
    const note = $('#settings-note');
    if (note)
      note.textContent = pendingAutoSavePreferences
        ? 'Saving…'
        : pendingSaves()
          ? 'Saved. Use Save next to a changed game setting to apply it.'
          : 'Saved.';
    flashSaved();
    if (checkedToolkitUpdates !== (state.snapshot.settings.autoCheckToolkitUpdates !== false)) {
      render();
      checkToolkitRelease(true);
    }
  }
  async function saveLoaderSettings(event) {
    event.preventDefault();
    if (state.busy || state.nativeDropActivity?.running) return;
    if (autoSaveTimer) clearTimeout(autoSaveTimer);
    autoSaveTimer = null;
    pendingAutoSavePreferences = null;
    const gamePath = $('#game-path').value.trim(),
      oldPath = state.snapshot.settings.gamePath || '';
    const preferences = collectPreferences(), preview = appearancePreview, context = state.snapshot.accountContext;
    const saved = await run(
      'savePreferences',
      { preferences },
      {
        refresh: false,
        onError: (error) => {
          const note = $('#settings-note');
          if (note) note.textContent =
            `Loader settings could not be saved: ${errorText(error)}. Your preview is still active.`;
          return true;
        }
      }
    );
    if (context !== state.snapshot.accountContext) {
      toast('The selected installation changed. Review its game folder before saving it.', 'warning');
      return;
    }
    return finishSettingsSave(saved, preferences, gamePath, oldPath, preview);
  }
  function collectPreferences() {
    return {
      colorMode: $('#color-mode-setting').value,
      useSystemTheme:
        state.snapshot.platform === 'linux'
          ? $('#system-theme-setting').getAttribute('aria-checked') === 'true'
          : state.snapshot.settings.useSystemTheme === true,
      seasonalThemes: $('#seasonal-setting').getAttribute('aria-checked') === 'true',
      seasonalEffects: $('#effects-setting').getAttribute('aria-checked') === 'true',
      theme: $('#theme-setting').value,
      reducedMotion: $('#motion-setting').getAttribute('aria-checked') === 'true',
      highContrast: $('#contrast-setting').getAttribute('aria-checked') === 'true',
      fontScale: Number($('#scale-setting').value),
      interfaceScale: Number($('#interface-scale-setting').value),
      autoScale: $('#auto-scale-setting').getAttribute('aria-checked') === 'true',
      compactMode: $('#compact-setting').getAttribute('aria-checked') === 'true',
      autoCheckUpdates: switchValue('#updates-setting', state.snapshot.settings.autoCheckUpdates !== false),
      autoCheckToolkitUpdates: switchValue('#toolkit-updates-setting', state.snapshot.settings.autoCheckToolkitUpdates !== false),
      steamLaunch: switchValue('#steam-launch-setting', state.snapshot.settings.steamLaunch !== false),
      autoManageExternal: switchValue('#auto-manage-setting', state.snapshot.settings.autoManageExternal !== false)
    };
  }
  async function finishSettingsSave(saved, preferences, gamePath, oldPath, preview = appearancePreview) {
    if (!saved) return;
    Object.assign(state.snapshot.settings, saved.settings || preferences);
    if (appearancePreview === preview) appearancePreview = null;
    applyAppearance();
    const note = $('#settings-note');
    if (note) note.textContent = 'Loader settings saved.';
    if (gamePath === oldPath) {
      if ($('#game-path')?.value.trim() === oldPath) { state.gamePathError = null; updateGamePathError(); }
      for (const key of ['game-path', 'steam-launch', 'auto-manage']) markPendingSave(key);
      toast('Loader settings saved.');
      return;
    }
    if (state.profileSession.active || state.snapshot.gameRunning || state.snapshot.recoveryRequired) {
      showGamePathError('The game folder was not changed. Close Days Gone and finish profile restoration or file recovery, then save the folder again.', gamePath);
      if (note) note.textContent =
        'Loader settings saved. The game folder was left unchanged. Close Days Gone and finish any pending profile restoration or file recovery before changing it.';
      markPendingSave('game-path');
      toast('Loader settings saved. The game folder still needs attention.', 'warning');
      return;
    }
    const selected = await run(
      'saveSettings',
      { settings: { gamePath } },
      {
        refresh: false,
        onError: (error) => {
          showGamePathError(`The game folder was not changed: ${errorText(error)}`, gamePath);
          const currentNote = $('#settings-note');
          if (currentNote) currentNote.textContent =
            `Loader settings saved. The game folder was left unchanged: ${errorText(error)}`;
          markPendingSave('game-path');
          toast('Loader settings saved. The game folder needs attention.', 'warning', { label: 'Go to setting', run: focusGamePathError });
          return true;
        }
      }
    );
    if (!selected) return;
    state.gamePathError = null;
    await acceptSnapshot(selected);
    await refresh({ quiet: true, preserveForm: true });
    if (state.view === 'settings') {
      // A user may have typed another folder while the native validation was running.
      markPendingSave('game-path');
      markPendingSave('steam-launch');
      markPendingSave('auto-manage');
      updateGamePathError();
    }
    const currentNote = $('#settings-note');
    if (currentNote) currentNote.textContent = pendingSaves() ? 'Saved. Your newer game setting still needs Save.' : 'Loader settings and game folder saved.';
    toast('Settings saved.');
  }
  function handleSearchShortcut(event) {
    if (event.defaultPrevented || event.isComposing || event.altKey || $('#modal').open) return false;
    const active = document.activeElement,
      editing = ['INPUT', 'TEXTAREA', 'SELECT'].includes(active?.tagName) || active?.isContentEditable,
      selectors = { overview: '#mod-search', mods: '#mod-search', profiles: '#profile-search',
        help: '#guide-search', settings: '#settings-search', workshop: '#workshop-search' },
      current = selectors[state.view] ? $(selectors[state.view]) : null;
    if (event.key === 'Escape' && active === current && current?.value && !event.ctrlKey && !event.metaKey) {
      event.preventDefault();
      current.value = '';
      current.oninput?.();
      current.focus?.();
      return true;
    }
    if (event.key === '?' && !editing && !event.ctrlKey && !event.metaKey) {
      event.preventDefault(); showKeyboardShortcuts(); return true;
    }
    if (!((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'f') &&
      !(event.key === '/' && !editing && !event.ctrlKey && !event.metaKey)) return false;
    event.preventDefault();
    // A profile page with only a few entries may not need a search control.
    if (!current) navigate('mods');
    const input = current || $('#mod-search');
    input?.focus?.(); input?.select?.();
    return true;
  }
  function handleInterfaceShortcut(e) {
    if (!(e.ctrlKey || e.metaKey) || !['+', '=', '-', '_', '0'].includes(e.key)) return false;
    e.preventDefault();
    if (state.busy) return true;
    const current = effectiveInterfaceScale({ ...state.snapshot.settings, ...appearancePreview });
    let next = 100;
    if (e.key === '+' || e.key === '=') next = interfacePresets.find((v) => v > current) || 200;
    else if (e.key === '-' || e.key === '_') next = [...interfacePresets].reverse().find((v) => v < current) || 80;
    const previous = {
        interfaceScale: state.snapshot.settings.interfaceScale,
        autoScale: state.snapshot.settings.autoScale
      },
      previousPreview = appearancePreview,
      previousQueuedScale = pendingAutoSavePreferences
        ? {
            autoScale: pendingAutoSavePreferences.autoScale,
            interfaceScale: pendingAutoSavePreferences.interfaceScale
          }
        : null;
    appearancePreview = appearancePreview ? { ...appearancePreview, autoScale: false, interfaceScale: next } : null;
    if (pendingAutoSavePreferences)
      pendingAutoSavePreferences = { ...pendingAutoSavePreferences, autoScale: false, interfaceScale: next };
    state.snapshot.settings.interfaceScale = next;
    state.snapshot.settings.autoScale = false;
    applyAppearance();
    const select = $('#interface-scale-setting');
    if (select) select.value = String(next);
    const auto = $('#auto-scale-setting');
    if (auto) auto.setAttribute('aria-checked', 'false');
    return run(
      'savePreferences',
      { preferences: { interfaceScale: next, autoScale: false } },
      { refresh: false, success: `Manual interface size: ${next}%.` }
    ).then((result) => {
      if (result === null) {
        Object.assign(state.snapshot.settings, previous);
        appearancePreview = previousPreview;
        if (previousQueuedScale && pendingAutoSavePreferences)
          Object.assign(pendingAutoSavePreferences, previousQueuedScale);
        applyAppearance();
        if (select) select.value = String(previous.interfaceScale || 100);
        if (auto) auto.setAttribute('aria-checked', String(previous.autoScale !== false));
      }
    });
  }
  let scrollActivityTimer = null;
  function markScrollActivity() {
    document.documentElement.classList.add('is-scrolling');
    if (scrollActivityTimer) clearTimeout(scrollActivityTimer);
    scrollActivityTimer = setTimeout(() => {
      document.documentElement.classList.remove('is-scrolling');
      scrollActivityTimer = null;
    }, 900);
  }
  function markKeyboardNavigation(event) {
    if (
      ['Tab', 'ArrowDown', 'ArrowUp', 'ArrowLeft', 'ArrowRight', 'PageDown', 'PageUp', 'Home', 'End'].includes(
        event.key
      )
    )
      document.documentElement.classList.add('keyboard-navigation');
  }
  function configureSandboxSmoke(options) {
    if (!state.snapshot.sandbox || state.loading) return false;
    const fontScale = Number(options?.fontScale),
      view = options?.view;
    if (
      !Number.isInteger(fontScale) ||
      fontScale < 90 ||
      fontScale > 150 ||
      !['overview', 'mods', 'help', 'settings', 'diagnostics', 'saves', 'profiles'].includes(view)
    )
      return false;
    if (options.colorMode !== undefined && !['auto', 'dark', 'light'].includes(options.colorMode)) return false;
    if (
      options.theme !== undefined &&
      !['auto', 'default', 'halloween', 'winter', 'newyear', 'spring', 'summer', 'autumn'].includes(options.theme)
    )
      return false;
    for (const key of ['reducedMotion', 'highContrast'])
      if (options[key] !== undefined && typeof options[key] !== 'boolean') return false;
    sandboxSmokeAppearance = {
      fontScale,
      ...Object.fromEntries(
        ['colorMode', 'theme', 'reducedMotion', 'highContrast']
          .filter((key) => options[key] !== undefined)
          .map((key) => [key, options[key]])
      )
    };
    state.setupPrompted = true;
    state.noticeQueue = state.noticeQueue.filter((notice) => !notice.type.startsWith('setup-'));
    const setupDialog = $('#modal');
    if (setupDialog.open && setupDialog.textContent.includes('Help the toolkit find Days Gone')) setupDialog.close();
    state.snapshot.settings.fontScale = fontScale;
    if (options.colorMode) state.snapshot.settings.colorMode = options.colorMode;
    if (options.theme) state.snapshot.settings.theme = options.theme;
    for (const key of ['reducedMotion', 'highContrast'])
      if (options[key] !== undefined) state.snapshot.settings[key] = options[key];
    appearancePreview = null;
    state.view = view;
    state.article = null;
    state.helpSection = 'help';
    state.helpSearch = '';
    applyAppearance();
    render();
    if (view === 'diagnostics' && !state.crashLogs) loadCrashLogs();
    if (view === 'saves' && !state.snapshot.libraryBackupRecovery?.pending) loadSaveBackups();
    return true;
  }
  if (window.__DRIFTER_SANDBOX_SMOKE__) {
    let profileWorkflow = null, changelogWorkflow = null;
    window.__toolkitSmoke = Object.freeze({
      configure: configureSandboxSmoke,
      changelogWorkflowStatus: () => changelogWorkflow,
      changelogWorkflowStart: () => {
        if (!state.snapshot.sandbox || state.loading || state.busy || profileWorkflow?.running) return false;
        if (changelogWorkflow) return true;
        const report = changelogWorkflow = { running: true, passed: false, selections: 0, checks: {} };
        (async () => {
          let ownedGeneration = null;
          const check = (name, passed) => {
            report.checks[name] = passed === true;
            if (!passed) throw new Error('Changelog interface fixture failed: ' + name);
          };
          try {
            state.setupPrompted = true;
            state.noticeQueue = [];
            $('#modal').close();
            showChangelog();
            ownedGeneration = modalContentGeneration;
            const entry = $('#changelog-entry'), panels = [...entry.children],
              choices = [...document.querySelectorAll('.changelog-option')],
              picker = $('#changelog-picker'), summary = $('#changelog-version');
            check('currentNotes', window.WHATS_NEW?.version === state.snapshot.appVersion);
            check('boundedVersions', choices.length > 1 && choices.length <= 100 && panels.length === choices.length);
            const parsed = panels.map((panel) => panel.innerHTML);
            for (let offset = 0; offset < 128; offset++) {
              const choice = choices[offset % choices.length];
              picker.open = true;
              choice.focus();
              choice.click();
              report.selections++;
              check('dialogOpen', $('#modal').open && ownedGeneration === modalContentGeneration);
              check('focusReturned', document.activeElement === summary && !picker.open);
              check('stableNodes', panels.every((panel, index) => panel === entry.children[index]));
              check('selectedNotes', panels.filter((panel) => !panel.hidden).length === 1 &&
                entry.dataset.version === choice.dataset.changelogVersion);
              if (offset % 32 === 31) await new Promise((resolve) => setTimeout(resolve, 0));
            }
            check('parsedNotesUnchanged', panels.every((panel, index) => panel.innerHTML === parsed[index]));
            check('plainParagraphs', !entry.querySelector('ul,ol,li') && !!entry.querySelector('.changelog-note'));
            const staleChoice = choices[0].onclick;
            showWhatsNew();
            ownedGeneration = modalContentGeneration;
            const replacement = $('#modal-content').innerHTML;
            staleChoice();
            check('replacementProtected', $('#modal').open && $('#modal-content').innerHTML === replacement);
            showChangelog();
            ownedGeneration = modalContentGeneration;
            const lateChoice = $('#changelog-pick-0').onclick;
            $('#modal').close();
            lateChoice();
            check('closedProtected', !$('#modal').open);
            report.passed = true;
          } catch (error) {
            report.error = errorText(error);
          } finally {
            report.running = false;
            if (ownedGeneration === modalContentGeneration) $('#modal').close();
          }
        })();
        return true;
      },
      profileWorkflowStatus: () => profileWorkflow,
      profileWorkflowStart: () => {
        if (!state.snapshot.sandbox || state.loading || state.busy) return false;
        if (profileWorkflow) return true;
        profileWorkflow = { running: true, passed: false, checks: {} };
        const originalView = state.view;
        (async () => {
          const check = (name, passed) => {
            profileWorkflow.checks[name] = passed === true;
            if (!passed) throw new Error('Profile interface fixture failed: ' + name);
          };
          const saveClick = async (name, empty) => {
            const input = $('#portable-profile-name'), submit = $('#portable-profile-save');
            check('dialogControls', !!input && !!submit);
            input.value = name;
            if (empty !== undefined) $('#portable-profile-source').value = empty ? 'empty' : 'current';
            submit.click();
            const deadline = Date.now() + 8000;
            while (state.busy || submit.disabled) {
              if (Date.now() > deadline) throw new Error('The native profile save did not return before its fixture deadline.');
              await new Promise((resolve) => setTimeout(resolve, 25));
            }
            // The native reply resumes the production save callback before this task's next timer.
            await new Promise((resolve) => setTimeout(resolve, 0));
          };
          try {
            state.setupPrompted = true;
            state.noticeQueue = [];
            $('#modal').close();
            const initial = await request('profilesList');
            check('isolatedEmptyState', Array.isArray(initial.profiles) && initial.profiles.length === 0);
            state.portableProfiles = initial;
            state.view = 'profiles'; render();
            $('[data-action="portable-save"]').click();
            await saveClick('Interface blank fixture', true);
            check('blankDialogClosed', !$('#modal').open);
            const blank = state.portableProfiles?.profiles?.find((p) => p.name === 'Interface blank fixture');
            check('blankShown', !!blank && blank.count === 0 && blank.owned === true);
            const storedBlank = await request('profilesList');
            check('blankNativeList', storedBlank.profiles.some((p) => p.id === blank.id && p.count === 0));
            $('[data-action="portable-save"]').click();
            await saveClick('Interface current fixture', false);
            check('currentDialogClosed', !$('#modal').open);
            check('currentShown', state.portableProfiles?.profiles?.some((p) => p.name === 'Interface current fixture'));
            // Bypass only the renderer's duplicate precheck to exercise the real service's refusal and inline error.
            const savedProfiles = state.portableProfiles;
            state.portableProfiles = { ...savedProfiles, profiles: [] };
            $('[data-action="portable-save"]').click();
            await saveClick('Interface blank fixture', true);
            check('nativeErrorKeepsDialog', $('#modal').open && $('#portable-profile-name').value === 'Interface blank fixture');
            check('nativeDuplicateVisible', !$('#portable-profile-error').hidden && $('#portable-profile-error').textContent.includes('already exists'));
            check('nativeErrorAllowsRetry', !$('#portable-profile-save').disabled);
            state.portableProfiles = savedProfiles;
            $('#modal').close(); render();
            const rename = document.querySelector(`[data-portable-rename="${blank.id}"]`);
            check('renameAvailable', !!rename);
            rename.click();
            await saveClick('Interface renamed fixture');
            check('renameDialogClosed', !$('#modal').open);
            const renamed = state.portableProfiles?.profiles?.find((p) => p.id === blank.id);
            check('renameIdentity', !!renamed && renamed.name === 'Interface renamed fixture' && renamed.owned === true);
            const final = await request('profilesList');
            check('finalNativeList', final.profiles.length === 2 && final.profiles.some((p) => p.id === blank.id && p.name === 'Interface renamed fixture'));
            if (state.snapshot.platform === 'linux') {
              profileWorkflow.files = await request('profileInterfaceFiles');
              check('nativeFixtureFiles', profileWorkflow.files.ready === true);
            }
            profileWorkflow.passed = true;
          } catch (error) {
            profileWorkflow.error = errorText(error);
          } finally {
            profileWorkflow.running = false;
            $('#modal').close();
            state.view = originalView; render();
          }
        })();
        return true;
      },
      ready: () => !state.loading && state.snapshot.sandbox === true && state.snapshot.mods.length >= 35,
      scaling: () => ({
        ...automaticScaleDecision(),
        automatic: state.snapshot.settings.autoScale !== false,
        interfaceScale: effectiveInterfaceScale(state.snapshot.settings),
        fontPreference: textPreference(state.snapshot.settings),
        textScale: effectiveInterfaceScale(state.snapshot.settings) * textPreference(state.snapshot.settings)
      }),
      seasonal: () => ({
        season: activeSeason(),
        particles: document.querySelectorAll('.seasonal-particle').length,
        artwork: 'local-svg-sprite',
        glyphs: document.querySelectorAll('.seasonal-glyph').length,
        documentHidden: document.hidden,
        systemReducedMotion: !!window.matchMedia?.('(prefers-reduced-motion: reduce)')?.matches,
        paused: document.documentElement.classList.contains('app-hidden'),
        reduced: document.documentElement.classList.contains('reduce-motion'),
        scenery: document.querySelectorAll('.seasonal-corner').length
      })
    });
  }
  window.addEventListener('error', handleInterfaceFault);
  window.addEventListener('unhandledrejection', handleInterfaceFault);
  $('#interface-recovery-refresh').onclick = recoverInterfaceView;
  $('#interface-recovery-dismiss').onclick = () => {
    $('#interface-recovery').hidden = true;
  };
  $$('.sidebar [data-view]').forEach((el) => (el.onclick = () => navigate(el.dataset.view)));
  $('#attention-button').onclick = () => showNextNotice(true) || showIssueSummary();
  $('#recent-activity-button').onclick = showRecentActivity;
  $('#launch-button').onclick = () => handleAction('launch');
  $('#author-link').onclick = () => handleAction('author-profile');
  $('#refresh-button').onclick = async () => {
    if (state.busy) return;
    await refresh();
    refreshAuthorAvatar();
  };
  document.addEventListener('scroll', markScrollActivity, { passive: true, capture: true });
  document.addEventListener('keydown', markKeyboardNavigation);
  document.addEventListener('pointerdown', () => document.documentElement.classList.remove('keyboard-navigation'));
  // Drag and drop: a hint while a file is over the window; the drop never navigates the page away.
  let dropHintTimer = null;
  const fileDrag = (event) => [...(event.dataTransfer?.types || [])].includes('Files');
  document.addEventListener('dragover', (event) => {
    if (!fileDrag(event)) return;
    event.preventDefault();
    event.dataTransfer.dropEffect = 'copy';
    document.body.classList.add('drop-ready');
    clearTimeout(dropHintTimer);
    dropHintTimer = setTimeout(() => document.body.classList.remove('drop-ready'), 250);
  });
  document.addEventListener('drop', (event) => {
    if (!fileDrag(event)) return;
    event.preventDefault();
    document.body.classList.remove('drop-ready');
    importDroppedFile(event.dataTransfer.files);
  });
  document.addEventListener('visibilitychange', () => {
    syncWindowVisibility();
    refreshAuthorAvatar();
    if (!document.hidden) {
      backgroundCheck(true);
      if (state.view === 'saves' && state.savesTab === 'backups' && state.saveBackups) loadSavePictures();
    } else cancelDownloadContinuation();
  });
  window.addEventListener('blur', () => { syncActivityDismissal(); syncHeroSlideshow(); });
  window.addEventListener('focus', () => { syncActivityDismissal(); syncHeroSlideshow(); backgroundCheck(true); });
  scheduleBackgroundCheck();
  setTimeout(checkDownloads, 3000);
  setInterval(pollCrashNotice, 2000);
  window.matchMedia?.('(prefers-color-scheme: light)')?.addEventListener?.('change', () => applyAppearance());
  window.matchMedia?.('(prefers-reduced-motion: reduce)')?.addEventListener?.('change', () => applyAppearance());
  window.addEventListener('resize', () => applyAppearance());
  document.addEventListener('keydown', (e) => {
    if (handleSearchShortcut(e)) return;
    if (handleDetailsShortcut(e)) return;
    if (handleInterfaceShortcut(e)) return;
  });
  $('#modal').addEventListener('close', handleModalClosed);
  $('#modal').addEventListener('cancel', (event) => {
    const record = state.saveEditor;
    if (!saveEditorVisible(record)) return;
    event.preventDefault();
    if (record.mode === 'discard') showSaveEditor(record); else requestCloseSaveEditor();
  });
  $('#modal').addEventListener('click', handleModalBackdrop);
  const brandImage = $('.brand-emblem img');
  brandImage.addEventListener('error', () => {
    if (!brandImage.src.endsWith('icon.svg')) brandImage.src = 'assets/icon.svg';
  });
  populateIcons();
  applyAppearance();
  render();
  refreshAuthorAvatar();
  refresh().then(async () => {
    const statuses = await Promise.allSettled([
      request('nexusStatus'),
      request('nxmStatus'),
      request('profileSessionStatus')
    ]);
    if (statuses[0].status === 'fulfilled') state.nexus = statuses[0].value;
    if (statuses[1].status === 'fulfilled') state.nxm = statuses[1].value;
    if (statuses[2].status === 'fulfilled') state.profileSession = statuses[2].value.session || statuses[2].value;
    renderNexusAccount();
    if (state.view === 'settings' || state.view === 'downloads') render();
    applySessionLocks();
    request('updateStatus')
      .then((status) => {
        state.update.status = status;
        if (state.view === 'settings') render();
      })
      .catch(() => {});
    checkForUpdate(true);
    checkToolkitRelease(true);
    checkForCatalog(true);
    await checkAutomaticReleases();
  });
})();
