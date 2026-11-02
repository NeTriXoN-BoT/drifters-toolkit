'use strict';

// Queued handlers from superseded editor dialogs must never close a live draft
// or consume a newer review. All requests are synthetic and no save is touched.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
let harness = fs.readFileSync(path.join(__dirname, 'ui_smoke.js'), 'utf8');
const boundary = harness.indexOf('async function testProfileCreation()');
assert(boundary > 0);
harness = harness.slice(0, boundary).replace('state, normalizeSnapshot, render,',
  'openSaveEditor, showSaveEditor, queueSaveEditorEdit, flushSaveEditorEdit, reviewSaveEditor, requestCloseSaveEditor, state, normalizeSnapshot, render,');
async function create(platform, edit = true) {
  const env = new Function('require', '__dirname', harness +
    '\nreturn {ui,node,context,fixture,nativeRequests,actionFixtures,actionErrors};')(require, __dirname);
  const { ui, node, fixture, actionFixtures } = env;
  const id = 'a'.repeat(64);
  const snapshot = { ...fixture, platform, edition: 'local', accountContext: 'editor-lifecycle-' + platform,
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
  node('#modal').close();
  actionFixtures.set('saveEditorOpen', { supported: true, session: session(0, 2) });
  actionFixtures.set('saveEditorSet', { session: session(1, 4) });
  actionFixtures.set('saveEditorClose', { closed: true, session: null });
  env.plan = (token) => ({ token, id, operation: 'edit-current-save', targetName: 'Save01.sav',
    safetyBackupRequired: true, beforeBytes: 512, afterBytes: 512,
    changes: [{ label: 'Unspent skill points', before: 2, after: 4 }] });
  assert(await ui.openSaveEditor(id));
  if (edit) {
    assert(ui.queueSaveEditorEdit('player-skill-points', { value: '4' }));
    assert(await ui.flushSaveEditorEdit());
  }
  return env;
}
async function cleanContextTransition(platform, visible) {
  const { ui, node, nativeRequests, actionFixtures, actionErrors } = await create(platform, false);
  const oldRecord = ui.state.saveEditor, oldSnapshot = ui.state.snapshot;
  if (!visible) { node('#modal').close(); ui.handleModalClosed(); }
  // Linux has already discarded this clean native session when it installs
  // the new manager. The renderer must not require that expired Close to work.
  actionErrors.set('saveEditorClose', 'This save draft expired. Open the save again.');
  const closeCount = nativeRequests.filter((request) => request.action === 'saveEditorClose').length;
  await ui.acceptSnapshot({ ...oldSnapshot, accountContext: oldSnapshot.accountContext + '-next',
    settings: { ...oldSnapshot.settings, gamePath: '/Synthetic/OtherDaysGone' } });
  assert.equal(ui.state.saveEditor, null, 'An unchanged draft cannot block a new installation');
  assert.equal(node('#modal').open, false, 'Only the old editor dialog closes on a context change');
  assert.equal(nativeRequests.filter((request) => request.action === 'saveEditorClose').length, closeCount,
    'The old session is never sent to the newly rebound native editor');
  const nextId = 'b'.repeat(64), context = 'next-current-save-scope';
  ui.state.currentSaves = { saveEditorAvailable: true, context, status: 'ready', signature: 'next',
    previewsSupported: false, saves: [{ id: nextId, name: 'Save02.sav', size: 512,
      exportEligible: true, replaceEligible: true, deleteEligible: true }] };
  ui.state.currentSaveSelection = { context, id: nextId };
  actionFixtures.set('saveEditorOpen', { supported: true,
    session: { ...oldRecord.session, id: 'next-session', sourceId: nextId, saveName: 'Save02.sav' } });
  assert(await ui.openSaveEditor(nextId), 'The new installation opens its own draft immediately');
  assert.equal(ui.state.saveEditor.session.id, 'next-session');
  assert.equal(nativeRequests.filter((request) => request.action === 'saveEditorClose').length, closeCount);
}
async function changedDraftContextProtection(platform) {
  for (const pending of [false, true]) {
    const { ui } = await create(platform, !pending);
    if (pending) assert(ui.queueSaveEditorEdit('player-skill-points', { value: '8' }));
    const record = ui.state.saveEditor, snapshot = ui.state.snapshot;
    await ui.acceptSnapshot({ ...snapshot, accountContext: snapshot.accountContext + '-unexpected',
      settings: { ...snapshot.settings, gamePath: '/Synthetic/OtherDaysGone' } });
    assert.equal(ui.state.saveEditor, record, 'Dirty values and pending input are never silently discarded');
    if (pending) assert.equal(record.pending.raw, '8');
  }
}
async function staleDiscard(platform) {
  const { ui, node, nativeRequests } = await create(platform);
  assert(ui.requestCloseSaveEditor());
  const oldDiscard = node('#save-editor-discard').onclick;
  node('#save-editor-keep-editing').onclick();
  const record = ui.state.saveEditor, markup = node('#modal-content').innerHTML;
  const before = nativeRequests.filter((request) => request.action === 'saveEditorClose').length;
  await oldDiscard();
  assert.equal(nativeRequests.filter((request) => request.action === 'saveEditorClose').length, before,
    'A queued Discard from a cancelled confirmation must not close the current draft');
  assert.equal(ui.state.saveEditor, record);
  assert.equal(node('#modal-content').innerHTML, markup);
  assert(ui.requestCloseSaveEditor());
  assert(await node('#save-editor-discard').onclick(), 'The current Discard confirmation must still work');
  assert.equal(ui.state.saveEditor, null);
  assert.equal(nativeRequests.filter((request) => request.action === 'saveEditorClose').length, before + 1);
}
async function staleApply(platform) {
  const { ui, node, nativeRequests, actionFixtures, actionErrors, plan } = await create(platform);
  actionFixtures.set('saveEditorReview', plan('first-review-token'));
  assert(await ui.reviewSaveEditor());
  const oldApply = node('#save-editor-apply').onclick;
  const oldBack = node('#save-editor-back').onclick;
  node('#save-editor-back').onclick();
  actionFixtures.set('saveEditorReview', plan('second-review-token'));
  assert(await ui.reviewSaveEditor());
  const record = ui.state.saveEditor, markup = node('#modal-content').innerHTML;
  actionErrors.set('saveEditorApply', 'An older review token is no longer valid.');
  const before = nativeRequests.filter((request) => request.action === 'saveEditorApply').length;
  await oldApply();
  assert.equal(nativeRequests.filter((request) => request.action === 'saveEditorApply').length, before,
    'An older Apply handler cannot consume or fail a newer review at the same draft revision');
  assert.equal(record.reviewUsed, false);
  assert.equal(record.review.token, 'second-review-token');
  assert.equal(node('#modal-content').innerHTML, markup);
  assert.equal(oldBack(), false, 'An old Back action cannot hide the newer review');
  assert.equal(record.mode, 'review');
  assert.equal(node('#modal-content').innerHTML, markup);
  actionErrors.delete('saveEditorApply');
  actionFixtures.set('saveEditorApply', { replaced: false });
  await node('#save-editor-apply').onclick();
  const requested = nativeRequests.filter((request) => request.action === 'saveEditorApply');
  assert.equal(requested.length, before + 1, 'The current Apply is admitted exactly once');
  assert.equal(requested.at(-1).token, 'second-review-token', 'Only the currently displayed review token is used');
}
async function staleEditControls(platform) {
  const { ui, node, nativeRequests } = await create(platform);
  const oldClose = node('#save-editor-close').onclick;
  const oldRevert = node('#save-editor-revert').onclick;
  const oldReview = node('#save-editor-review').onclick;
  const before = nativeRequests.filter((request) => request.action === 'saveEditorReview').length;
  assert(ui.requestCloseSaveEditor());
  const discardMarkup = node('#modal-content').innerHTML;
  assert.equal(await oldReview(), false, 'An old Review cannot replace a newer discard prompt');
  assert.equal(nativeRequests.filter((request) => request.action === 'saveEditorReview').length, before);
  assert.equal(node('#modal-content').innerHTML, discardMarkup);
  node('#save-editor-keep-editing').onclick();
  assert(ui.queueSaveEditorEdit('player-skill-points', { value: '8' }));
  const currentRecord = ui.state.saveEditor, markup = node('#modal-content').innerHTML;
  assert.equal(oldClose(), false, 'An older Close button cannot open a new discard prompt');
  assert.equal(oldRevert(), false, 'An older Revert button cannot discard newer input');
  assert.equal(currentRecord.pending.raw, '8');
  assert.equal(node('#modal-content').innerHTML, markup);
  node('#save-editor-revert').onclick();
  assert.equal(currentRecord.pending, null, 'The current Revert button remains usable');
}
(async () => {
  for (const platform of ['windows', 'linux']) {
    await staleDiscard(platform);
    await staleApply(platform);
    await staleEditControls(platform);
    await cleanContextTransition(platform, false);
    await cleanContextTransition(platform, true);
    await changedDraftContextProtection(platform);
  }
  console.log('Save-editor superseded handlers and clean installation-transition checks passed on Windows/Linux states.');
})().catch((error) => { console.error(error); process.exitCode = 1; });
