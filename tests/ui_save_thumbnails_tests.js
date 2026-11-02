'use strict';

// Uses the existing metadata-only UI harness; no game, account or native process.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, 'ui_smoke.js'), 'utf8');
const boundary = source.indexOf('async function testProfileCreation()');
assert(boundary > 0);
const png = 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aGxkAAAAASUVORK5CYII=';
const settle = () => new Promise(setImmediate);
let checks = 0;
function check(value, message) { checks++; assert(value, message); }

function create(platform, kind = 'current') {
  const env = new Function('require', '__dirname', source.slice(0, boundary) +
    '\nreturn {ui,node,context,fixture,nativeRequests,timers,reply:(data)=>nativeMessageListener({data})};')(require, __dirname);
  const { ui, node, context, fixture } = env;
  Object.assign(ui.state, { preview: false, busy: false, loading: false, view: 'saves', savesTab: kind,
    nativeWindowHidden: false, nativeDropActivity: null, profileSession: { active: false },
    currentSaveSelection: null, currentSavePreviews: new Map(), savePictures: new Map(), saveSelection: null,
    snapshot: ui.normalizeSnapshot({ ...fixture, platform, accountContext: 'thumb-' + platform,
      gameRunning: false, libraryBackupRecovery: { pending: false }, saveRecoveryRequired: false }) });
  node('#modal').close(); context.document.hidden = false;
  let now = 1000000;
  context.Date = class extends Date { static now() { return now; } };
  env.advance = (milliseconds) => { now += milliseconds; };
  env.observers = [];
  context.IntersectionObserver = class {
    constructor(callback) { this.callback = callback; this.cards = []; this.disconnected = false; env.observers.push(this); }
    observe(card) { this.cards.push(card); }
    disconnect() { this.disconnected = true; }
    emit(cards, visible = true) { this.callback(cards.map((target) => ({ target, isIntersecting: visible }))); }
  };
  env.files = Array.from({ length: 40 }, (_, i) => ({ id: 'opaque-' + i, name: 'Save ' + i,
    size: 5120, modifiedUtc: '2026-10-30T12:00:00Z', previewEligible: true }));
  ui.state.currentSaves = { context: 'root-' + platform, signature: 'same', saves: env.files };
  ui.state.saveBackups = { available: true, backups: env.files.map((file, i) => ({ ...file,
    createdAt: 1793354400 - i, files: 1, bytes: 5120, pictureAvailable: true })) };
  const root = node(kind === 'current' ? '#current-saves-panel' : '#save-backups-panel');
  const field = kind === 'current' ? 'currentSaveSelect' : 'saveSelect';
  env.cards = env.files.map((file, i) => ({ ...node('card-' + i), dataset: { [field]: file.id } }));
  env.pictures = env.files.map((file, i) => ({ ...node('picture-' + i), dataset: {
    [kind === 'current' ? 'currentPreview' : 'backupPreview']: file.id } }));
  root.querySelector = () => null;
  root.querySelectorAll = (selector) => {
    if (selector === (kind === 'current' ? '[data-current-save-select]' : '[data-save-select]')) return env.cards;
    if (selector === (kind === 'current' ? '[data-current-preview]' : '[data-backup-preview]')) return env.pictures;
    return [];
  };
  const bridge = context.window.chrome.webview, original = bridge.postMessage;
  env.previewRequests = []; env.active = 0; env.maximum = 0; env.held = null;
  env.hold = false; env.unavailable = new Set();
  env.responses = new Map();
  bridge.postMessage = (request) => {
    if (request.action === 'currentSavesList') {
      env.nativeRequests.push(request);
      Promise.resolve().then(() => env.reply({ requestId: request.requestId, ok: true,
        data: structuredClone(ui.state.currentSaves) }));
      return;
    }
    if (request.action === 'thumbnailPauseFixture') {
      env.nativeRequests.push(request);
      env.finishOperation = () => env.reply({ requestId: request.requestId, ok: true, data: {} });
      return;
    }
    if (request.action !== 'currentSavePreview' && request.action !== 'saveBackupPicture') return original.call(bridge, request);
    env.nativeRequests.push(request); env.previewRequests.push(request); ++env.active; env.maximum = Math.max(env.maximum, env.active);
    const finish = () => { --env.active; env.reply({ requestId: request.requestId, ok: true, data: {
      id: request.id, context: ui.state.currentSaves.context, available: !env.unavailable.has(request.id), actualGamePreview: true,
      dataUrl: png, ...env.responses.get(request.id) } }); };
    if (env.hold) env.held = finish;
    else Promise.resolve().then(finish);
  };
  env.bind = () => kind === 'current' ? ui.bindCurrentSaveControls(root) : ui.bindSaveBackupControls(root);
  env.flush = async () => {
    for (let n = 0; n < 40; n++) {
      const timer = ui.saveThumbnailState().timer;
      if (timer !== null && env.timers.has(timer)) {
        const callback = env.timers.get(timer).callback; env.timers.delete(timer); callback();
      }
      await settle();
      const state = ui.saveThumbnailState();
      if (state.timer === null && !state.currentLoading && !state.backupLoading) return;
      if (env.hold && env.held) return;
    }
    throw Error('Thumbnail pump did not settle within its bounded batches');
  };
  return env;
}

async function scenario(platform, kind) {
  const env = create(platform, kind), { ui } = env;
  env.unavailable.add('opaque-3');
  env.bind();
  const observer = env.observers.at(-1);
  observer.emit(env.cards.slice(0, 8));
  await env.flush();
  check(env.previewRequests.length === 8, `${platform}/${kind}: visible pictures load without clicking a save`);
  check(env.maximum === 1, 'The thumbnail gallery admits only one native image request at a time');
  check(env.previewRequests.every((request) => Number(request.id.slice(7)) < 8), 'Offscreen saves are not decoded');
  check(env.pictures[7].innerHTML.includes('<img'), 'A non-selected visible card is painted in place');
  const before = env.previewRequests.length;
  observer.emit(env.cards.slice(0, 8)); await env.flush();
  check(env.previewRequests.length === before, 'Unchanged and unavailable pictures do not repeatedly decode');
  observer.emit(env.cards.slice(0, 8), false);
  observer.emit(env.cards.slice(12, 18)); await env.flush();
  check(env.previewRequests.slice(before).some((request) => request.id === 'opaque-17'), 'Scrolling loads newly visible pictures without selection');
  check(kind === 'current' ? ui.state.currentSaveSelection.id === 'opaque-0' : ui.state.saveSelection.id === 'opaque-0',
    'Automatic thumbnails leave the selected save unchanged');
  ui.state.nativeWindowHidden = true; ui.syncCurrentSavesPoll();
  const hiddenBefore = env.previewRequests.length;
  observer.emit(env.cards.slice(25, 29)); await env.flush();
  check(env.previewRequests.length === hiddenBefore, 'A hidden window does not start thumbnail work');
  ui.state.nativeWindowHidden = false; ui.syncCurrentSavesPoll();
  const resumed = env.observers.at(-1);
  resumed.emit(env.cards.slice(25, 29)); await env.flush();
  check(env.previewRequests.some((request) => request.id === 'opaque-28'), 'Pictures resume automatically after the window returns');
  for (let i = 0; i < 10; i++) ui.rememberSavePicture('large-' + i, { available: true,
    dataUrl: 'data:image/png;base64,' + 'A'.repeat(2400000) });
  check(ui.saveThumbnailState().bytes <= 16 * 1024 * 1024, 'Current and backup pictures share a fixed memory budget');
  const retainedBytes = [...ui.state.currentSavePreviews.values()].reduce((bytes, picture) => bytes + (picture?.url?.length || 0), 0) +
    [...ui.state.savePictures.values()].reduce((bytes, url) => bytes + url.length, 0);
  check(retainedBytes <= 16 * 1024 * 1024, 'The actual two thumbnail caches stay within the combined byte cap');
  ui.state.view = 'overview'; ui.syncCurrentSavesPoll();
}

async function stale(platform) {
  const env = create(platform), { ui } = env;
  env.hold = true; env.bind();
  env.observers.at(-1).emit(env.cards.slice(0, 8));
  await env.flush();
  check(env.previewRequests.length === 1, 'A held native preview prevents a queue burst');
  for (let n = 0; n < 100; n++) env.observers.at(-1).emit(env.cards.slice(0, 8));
  await env.flush();
  check(env.previewRequests.length === 1, 'Repeated visibility callbacks never duplicate an in-flight request');
  ui.state.snapshot.accountContext = 'different-installation';
  ui.state.currentSaves = { context: 'other-root', signature: 'other', saves: [] };
  env.hold = false; env.held(); await env.flush();
  check(ui.state.currentSavePreviews.size === 0, 'A late picture cannot repopulate a different installation');
  check(env.maximum === 1, 'Late results preserve the single native request bound');
  ui.state.view = 'overview'; ui.syncCurrentSavesPoll();
}

async function fallback(platform, kind) {
  const env = create(platform, kind), { ui, context } = env;
  delete context.IntersectionObserver;
  context.window.innerHeight = 800;
  let visible = new Set([5, 6, 7]);
  const listeners = new Map();
  context.document.addEventListener = (name, callback) => listeners.set(name, callback);
  env.cards.forEach((card, index) => { card.getBoundingClientRect = () => visible.has(index)
    ? { top: 100 + index, bottom: 200 + index } : { top: 2000, bottom: 2200 }; });
  env.bind(); await env.flush();
  check(env.previewRequests.length === 4 && env.previewRequests.some((request) => request.id === 'opaque-7'),
    'Geometry fallback automatically loads the selected and actually visible cards');
  check(!env.previewRequests.some((request) => request.id === 'opaque-1'), 'Fallback does not blindly decode the collection from the start');
  visible = new Set([20, 21, 22]);
  listeners.get('scroll')(); await env.flush();
  check(env.previewRequests.some((request) => request.id === 'opaque-22'), 'Fallback scroll events load new pictures automatically');
  ui.state.view = 'overview'; ui.syncCurrentSavesPoll();
}

async function tabSwitch(platform) {
  const env = create(platform), { ui } = env;
  env.hold = true; env.bind(); env.observers.at(-1).emit(env.cards.slice(0, 8)); await env.flush();
  ui.state.savesTab = 'backups';
  await ui.loadSavePictures();
  check(env.previewRequests.length === 1, 'Changing tabs cannot add a second native decode behind a held preview');
  env.hold = false; env.held(); await env.flush();
  check(env.previewRequests.some((request) => request.action === 'saveBackupPicture'), 'The new tab resumes when the old native request finishes');
  check(env.maximum === 1 && ui.state.currentSavePreviews.size === 0, 'An old-tab result is discarded and both galleries share one native worker');
  ui.state.view = 'overview'; ui.syncCurrentSavesPoll();
}

async function resumeAfterPause(platform, reason) {
  const env = create(platform, 'backups'), { ui } = env;
  const operation = reason === 'busy' ? ui.run('thumbnailPauseFixture', {}, { refresh: false, activity: false }) : null;
  if (reason === 'modal') env.node('#modal').showModal();
  env.bind(); env.observers.at(-1).emit(env.cards.slice(0, 8)); await env.flush();
  check(env.previewRequests.length === 0, `${reason}: paused backup galleries do not start image reads`);
  if (reason === 'modal') { env.node('#modal').close(); ui.handleModalClosed(); }
  else { env.finishOperation(); await operation; }
  await env.flush();
  check(env.previewRequests.length === 8, `${reason}: idle/close resumes visible backup pictures without a scroll or selection`);
  check(env.maximum === 1, `${reason}: resuming still admits only one native picture request`);
  ui.state.view = 'overview'; ui.syncCurrentSavesPoll();
}

async function failureReasons(platform) {
  const env = create(platform), { ui } = env;
  env.responses.set('opaque-0', { available: false, actualGamePreview: false, reasonCode: 'profile-data', retryable: false,
    reason: 'Profile data does not contain an in-game picture.' });
  env.responses.set('opaque-1', { available: false, actualGamePreview: false, reasonCode: 'no-picture', retryable: false,
    reason: 'No picture stored in this save.' });
  env.responses.set('opaque-2', { available: false, actualGamePreview: false, reasonCode: 'decoder-unavailable', retryable: true,
    reason: 'The picture reader is busy. Try again shortly.' });
  env.bind(); env.observers.at(-1).emit(env.cards.slice(0, 8)); await env.flush();
  let html = ui.renderCurrentSavesPanel();
  check(html.includes('Profile data · no in-game picture') && html.includes('No in-game picture stored'),
    'Profile data and legitimate saves without pictures have distinct, non-error labels');
  check(html.includes('The picture reader is busy. Try again shortly.') && html.includes('Use Refresh to try again.'),
    'A retryable native reason reaches the gallery with an explicit retry route');
  const before = env.previewRequests.length;
  await ui.loadCurrentSaves(); await env.flush();
  check(env.previewRequests.length === before, 'Ordinary save polling cannot repeatedly retry an unavailable decoder');
  env.responses.delete('opaque-2');
  await ui.handleAction('save-backups-refresh'); await env.flush();
  check(env.previewRequests.length === before + 1 && ui.state.currentSavePreviews.get(ui.currentSavePreviewKey('opaque-2')).url === png,
    'Explicit Refresh retries transient picture failures without reloading valid or terminal pictures');
  check(ui.state.currentSavePreviews.get(ui.currentSavePreviewKey('opaque-0')).reasonCode === 'profile-data',
    'Refresh never substitutes another slot picture for profile data');
  ui.state.view = 'overview'; ui.syncCurrentSavesPoll();
}

async function visibilityRetry(platform) {
  const env = create(platform), { ui } = env;
  env.responses.set('opaque-0', { available: false, actualGamePreview: false, reasonCode: 'decoder-unavailable',
    retryable: true, reason: 'The picture reader is temporarily busy.' });
  env.bind(); ui.syncCurrentSavesPoll(); env.observers.at(-1).emit(env.cards.slice(0, 8)); await env.flush();
  const before = env.previewRequests.length;
  ui.state.nativeWindowHidden = true; ui.syncCurrentSavesPoll(); ui.state.nativeWindowHidden = false; ui.syncCurrentSavesPoll();
  await env.flush();
  check(env.previewRequests.length === before, 'Rapid window toggles do not bypass the failed-picture cooldown');
  env.advance(6000); await ui.loadCurrentSaves(); await env.flush();
  check(env.previewRequests.length === before, 'Elapsed time alone does not start periodic decoder retries');
  env.responses.delete('opaque-0');
  ui.state.nativeWindowHidden = true; ui.syncCurrentSavesPoll(); ui.state.nativeWindowHidden = false; ui.syncCurrentSavesPoll();
  await env.flush();
  check(env.previewRequests.length === before + 1 && ui.state.currentSavePreviews.get(ui.currentSavePreviewKey('opaque-0')).url === png,
    'Returning to the window after cooldown can recover a transient decoder failure automatically');
  ui.state.view = 'overview'; ui.syncCurrentSavesPoll();
}

async function actionReasons(platform) {
  const env = create(platform), { ui } = env;
  Object.assign(env.files[0], { replaceEligible: true, deleteEligible: true, exportEligible: true });
  ui.state.currentSaves.saveEditorAvailable = true;
  ui.state.snapshot.settings.gamePath = ''; ui.state.snapshot.gameRunning = null;
  let html = ui.renderCurrentSavesPanel();
  check(html.includes('id="current-save-action-reason"') && html.includes('Choose your Days Gone game folder in Settings'),
    'The reason for grey save actions is printed beside the selected save controls');
  check(/data-current-save-edit="[^"]+"[^>]*disabled/.test(html) && !/data-current-save-export="[^"]+"[^>]*disabled/.test(html),
    'Missing game setup blocks editing while retaining read-only export');
  ui.state.snapshot.settings.gamePath = platform === 'linux' ? '/synthetic/DaysGone' : 'C:/Synthetic/DaysGone';
  ui.state.snapshot.gameRunning = true; ui.updateSavesGameStatus();
  check(env.node('#current-save-action-reason').textContent.includes('Close Days Gone'), 'A running game updates the adjacent explanation');
  ui.state.snapshot.gameRunning = false; ui.updateSavesGameStatus();
  check(env.node('#current-save-action-reason').hidden, 'The disabled-action explanation clears once setup and game status are ready');
  ui.state.busy = true; ui.updateSavesGameStatus();
  check(env.node('#current-save-action-reason').textContent.includes('Wait for the current operation'), 'Busy actions explain why they are paused');
  ui.state.view = 'overview'; ui.syncCurrentSavesPoll();
}

async function selectedPictureUnderPressure(platform, kind) {
  const env = create(platform, kind), { ui } = env;
  // Synthetic native-verdict data exercises accepted-size strings and cache
  // ownership, not browser PNG decoding (the real-renderer test covers that).
  const large = 'data:image/png;base64,' + 'A'.repeat(1400000);
  for (const file of env.files) env.responses.set(file.id, { dataUrl: large });
  env.bind(); const observer = env.observers.at(-1);
  observer.emit(env.cards.slice(0, 14)); await env.flush();
  const selectedKey = kind === 'current' ? ui.currentSavePreviewKey('opaque-0') : 'opaque-0',
    cache = kind === 'current' ? ui.state.currentSavePreviews : ui.state.savePictures;
  check(cache.has(selectedKey), `${kind}: byte-pressure eviction preserves the selected active picture`);
  check(ui.saveThumbnailState().bytes <= 16 * 1024 * 1024, `${kind}: pinning never expands the shared cache budget`);
  const evicted = env.files.slice(1, 14).findIndex((file) => !cache.has(kind === 'current' ? ui.currentSavePreviewKey(file.id) : file.id)) + 1;
  check(evicted > 0 && !env.pictures[evicted].innerHTML.includes('<img'),
    `${kind}: eviction also releases the data URL retained by its old DOM card`);
  check(!env.pictures[evicted].innerHTML.includes('Loading in-game picture'),
    `${kind}: an evicted picture is not shown as permanently loading`);
  const before = env.previewRequests.length;
  observer.emit([env.cards[evicted]], false); observer.emit([env.cards[evicted]], true); await env.flush();
  const key = kind === 'current' ? ui.currentSavePreviewKey(env.files[evicted].id) : env.files[evicted].id;
  check(env.previewRequests.length === before + 1 && cache.has(key),
    `${kind}: a revisited evicted card reloads once instead of being suppressed or spinning`);
  check(cache.has(selectedKey) && env.maximum === 1, `${kind}: revisiting preserves the selected viewer and single native worker`);
  ui.state.view = 'overview'; ui.syncCurrentSavesPoll();
}

async function negativeVerdictEviction(platform) {
  const env = create(platform), { ui } = env;
  for (const file of env.files) env.responses.set(file.id, { available: false, actualGamePreview: false,
    reasonCode: 'no-picture', retryable: false, reason: 'No picture stored in this save.' });
  env.bind(); env.observers.at(-1).emit(env.cards.slice(0, 30)); await env.flush();
  const victim = env.files.slice(1, 30).findIndex((file) => !ui.state.currentSavePreviews.has(ui.currentSavePreviewKey(file.id))) + 1;
  check(victim > 0 && env.pictures[victim].innerHTML.includes('No in-game picture stored') &&
    !env.pictures[victim].innerHTML.includes('Picture available'), 'Metadata eviction never invents a picture for an empty-PNG save');
  const before = env.previewRequests.length;
  await ui.loadCurrentSaves(); await env.flush();
  check(env.previewRequests.length === before, 'Evicted terminal verdicts do not cause repeated visible polling work');
  ui.state.view = 'overview'; ui.syncCurrentSavesPoll();
}

(async () => {
  for (const platform of ['windows', 'linux']) {
    await scenario(platform, 'current'); await scenario(platform, 'backups'); await stale(platform);
    await fallback(platform, 'current'); await fallback(platform, 'backups'); await tabSwitch(platform);
    await resumeAfterPause(platform, 'modal'); await resumeAfterPause(platform, 'busy');
    await failureReasons(platform); await visibilityRetry(platform); await actionReasons(platform);
    await selectedPictureUnderPressure(platform, 'current'); await selectedPictureUnderPressure(platform, 'backups');
    await negativeVerdictEviction(platform);
  }
  console.log(`Automatic save thumbnails passed: ${checks} checks across Windows/Linux current saves and backups.`);
})().catch((error) => { console.error(error); process.exitCode = 1; });
