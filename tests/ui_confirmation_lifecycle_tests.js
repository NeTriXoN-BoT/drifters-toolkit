'use strict';

// Synthetic callbacks and refused updater requests only. No user files or real
// update process is accessed. Closing a prompt must revoke its UI admission.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
let harness = fs.readFileSync(path.join(__dirname, 'ui_smoke.js'), 'utf8');
const boundary = harness.indexOf('async function testProfileCreation()');
assert(boundary > 0);
harness = harness.slice(0, boundary).replace('state, normalizeSnapshot, render,',
  'confirmModal, openSaveEditor, queueSaveEditorEdit, flushSaveEditorEdit, reviewSaveEditor, state, normalizeSnapshot, render,');
function create(platform) {
  const env = new Function('require', '__dirname', harness +
    '\nreturn {ui,node,context,fixture,nativeRequests,actionFixtures,actionErrors,reply:(data)=>nativeMessageListener({data})};')(require, __dirname);
  const { ui, fixture, node } = env;
  Object.assign(ui.state, { preview: false, loading: false, busy: false,
    snapshot: ui.normalizeSnapshot({ ...fixture, platform, accountContext: 'confirmation-' + platform }),
    profileSession: { active: false } });
  node('#modal').close();
  return env;
}
async function confirmations(platform) {
  for (const replacement of [false, true]) for (const contextIndependent of [false, true]) {
    const { ui, node } = create(platform);
    let calls = 0;
    ui.confirmModal('Review test operation', '<p>Synthetic fixture</p>', 'Confirm', () => { calls++; }, false,
      { contextIndependent });
    const stale = node('#confirm-action').onclick;
    node('#modal').close();
    if (replacement) ui.showChangelog();
    const markup = node('#modal-content').innerHTML, open = node('#modal').open;
    await stale();
    assert.equal(calls, 0, 'A cancelled/replaced confirmation must not execute its callback');
    assert.equal(node('#modal').open, open, 'A stale confirmation must not close a newer dialog');
    assert.equal(node('#modal-content').innerHTML, markup);
  }
  for (const contextIndependent of [false, true]) {
    const { ui, node } = create(platform);
    let calls = 0;
    ui.confirmModal('Review test operation', '<p>Synthetic fixture</p>', 'Confirm', () => { calls++; }, false,
      { contextIndependent });
    const current = node('#confirm-action').onclick;
    await current(); await current();
    assert.equal(calls, 1, 'A live confirmation runs once, even with repeated queued clicks');
  }
}
async function defaultCloseLifecycle(platform) {
  const { ui, node, fixture, nativeRequests, actionFixtures } = create(platform);
  const dialog = node('#modal');
  let controls = [node('#old-modal-header-close'), node('#old-modal-cancel')];
  dialog.querySelectorAll = (selector) => selector === '[data-modal-close]' ? controls : [];
  dialog.contains = (target) => target === dialog || controls.includes(target);
  ui.confirmModal('Old review', '<p>Fixture</p>', 'Confirm', () => {});
  const stale = controls.map((control) => control.onclick);
  controls = [node('#current-modal-header-close'), node('#current-modal-close')];
  ui.showChangelog();
  const markup = node('#modal-content').innerHTML;
  for (const close of stale) {
    assert.equal(close(), false);
    assert(dialog.open, 'An old header Close or Cancel cannot close replacement content');
    assert.equal(node('#modal-content').innerHTML, markup);
  }
  const live = controls[0].onclick;
  assert.equal(live(), true, 'The current default close remains usable');
  assert.equal(live(), false, 'Closing the same default dialog twice is inert');

  const id = 'a'.repeat(64);
  const snapshot = { ...fixture, platform, edition: 'local', accountContext: 'new-editor-' + platform,
    gameRunning: false, sandbox: false, settings: { ...fixture.settings, gamePath: '/Synthetic/DaysGone' } };
  const current = { saveEditorAvailable: true, context: 'current-save-scope', status: 'ready', signature: 'same',
    previewsSupported: false, saves: [{ id, name: 'Save01.sav', size: 512, modifiedUtc: 1000,
      exportEligible: true, replaceEligible: true, deleteEligible: true }] };
  const session = (revision, value) => ({ id: 'session', sourceId: id, saveName: 'Save01.sav', revision,
    dirty: revision > 0, fields: [{ id: 'player-skill-points', label: 'Unspent skill points', editor: 'integer',
      type: 'IntProperty', min: 0, max: 2147483647, value }], protectedFields: [] });
  Object.assign(ui.state, { preview: false, loading: false, busy: false, view: 'saves', savesTab: 'current',
    snapshot: ui.normalizeSnapshot(snapshot), profileSession: { active: false }, nativeDropActivity: null,
    currentSaves: current, currentSaveSelection: { context: current.context, id }, saveEditor: null,
    saveBackups: { available: true, backups: [], recovery: { pending: false } } });
  actionFixtures.set('saveEditorOpen', { supported: true, session: session(0, 2) });
  actionFixtures.set('saveEditorSet', { session: session(1, 4) });
  actionFixtures.set('saveEditorReview', { token: 'current-review-token', id, operation: 'edit-current-save',
    targetName: 'Save01.sav', safetyBackupRequired: true, beforeBytes: 512, afterBytes: 512,
    changes: [{ label: 'Unspent skill points', before: 2, after: 4 }] });
  assert(await ui.openSaveEditor(id));
  assert(ui.queueSaveEditorEdit('player-skill-points', { value: '4' }));
  assert(await ui.flushSaveEditorEdit());
  assert(await ui.reviewSaveEditor());
  const editor = ui.state.saveEditor, review = editor.review, editorMarkup = node('#modal-content').innerHTML;
  for (const close of stale) {
    assert.equal(close(), false);
    assert(dialog.open, 'A default Close from another dialog cannot dismiss the new save review');
    assert.equal(ui.state.saveEditor, editor);
    assert.equal(editor.review, review);
    assert.equal(node('#modal-content').innerHTML, editorMarkup);
  }
  assert.equal(nativeRequests.filter((request) => request.action === 'saveEditorClose').length, 0);
  assert(controls[0].onclick(), 'The tailored editor close hook still handles the current button');
  assert.equal(editor.mode, 'discard', 'A dirty draft still opens its discard/keep-editing prompt');
  assert(dialog.open);
}
async function updater(platform) {
  for (const replacement of [false, true]) {
    const { ui, node, nativeRequests, actionErrors } = create(platform);
    actionErrors.set('updateInstall', 'Synthetic audit refusal: no update is installed.');
    ui.confirmUpdate({ version: '9.0.0', token: 'synthetic-native-reviewed-token', bytes: 1024 });
    const stale = node('#update-install-confirm').onclick;
    node('#modal').close();
    if (replacement) ui.showChangelog();
    const markup = node('#modal-content').innerHTML, open = node('#modal').open;
    const before = nativeRequests.filter((request) => request.action === 'updateInstall').length;
    await stale();
    assert.equal(nativeRequests.filter((request) => request.action === 'updateInstall').length, before,
      'Closing/replacing an update prompt must prevent its old Install callback from reaching native replacement');
    assert.equal(node('#modal').open, open);
    assert.equal(node('#modal-content').innerHTML, markup);
    assert.equal(ui.state.busy, false);
  }
  for (const change of ['installation', 'offer', 'offer-token']) {
    const { ui, node, nativeRequests } = create(platform);
    const offer = { version: '9.0.0', token: 'synthetic-current-token', bytes: 1024 };
    ui.state.update.offer = offer;
    ui.confirmUpdate(offer);
    if (change === 'installation') ui.state.snapshot.settings.gamePath = '/Another/Installation';
    else if (change === 'offer') ui.state.update.offer = { ...offer, token: 'newer-token' };
    else offer.token = 'changed-token';
    await node('#update-install-confirm').onclick();
    assert.equal(nativeRequests.filter((request) => request.action === 'updateInstall').length, 0,
      'An installation/offer change must revoke earlier installation consent');
    assert(node('#update-install-confirm').disabled);
    assert.match(node('#updater-status').textContent, /no longer current/);
  }
  {
    const { ui, node, context, nativeRequests, reply } = create(platform);
    const firstOffer = { version: '9.0.0', token: 'first-token', bytes: 1024 };
    ui.state.update.offer = firstOffer; ui.confirmUpdate(firstOffer);
    const bridge = context.window.chrome.webview, post = bridge.postMessage;
    let held;
    bridge.postMessage = (request) => {
      if (request.action === 'updateInstall' && !held) { held = request; nativeRequests.push(request); }
      else post.call(bridge, request);
    };
    const work = node('#update-install-confirm').onclick();
    for (let i = 0; i < 40 && !held; ++i) await new Promise(setImmediate);
    assert(held);
    const replacement = { version: '10.0.0', token: 'replacement-token', bytes: 2048 };
    ui.state.update.offer = replacement; ui.confirmUpdate(replacement);
    const markup = node('#modal-content').innerHTML;
    node('#updater-status').textContent = 'Current replacement review';
    node('#update-install-confirm').hidden = false;
    node('#update-not-now').textContent = 'Not now';
    reply({ requestId: held.requestId, ok: false, error: 'Old update check stopped.' });
    assert.equal(await work, false);
    assert.equal(node('#modal-content').innerHTML, markup);
    assert.equal(node('#updater-status').textContent, 'Current replacement review', 'Late errors do not replace newer status');
    assert.equal(node('#update-install-confirm').hidden, false, 'Late errors do not hide a newer Install control');
    assert.equal(node('#update-not-now').textContent, 'Not now', 'Late errors do not relabel a newer Cancel control');
    assert.equal(ui.state.update.offer, replacement);
    assert.equal(ui.state.busy, false);
  }
}
(async () => {
  for (const platform of ['windows', 'linux']) {
    await confirmations(platform);
    await defaultCloseLifecycle(platform);
    await updater(platform);
  }
  console.log('Cancelled/replaced generic confirmations and update prompts stay inert on Windows/Linux states.');
})().catch((error) => { console.error(error); process.exitCode = 1; });
