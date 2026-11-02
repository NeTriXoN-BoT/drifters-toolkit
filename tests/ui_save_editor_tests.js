'use strict';
// Synthetic current-save/editor APIs only: no actual save or game is read or written.
const assert = require('node:assert/strict'), fs = require('node:fs'), path = require('node:path');
let harness = fs.readFileSync(path.join(__dirname, 'ui_smoke.js'), 'utf8');
harness = harness.slice(0, harness.indexOf('async function testProfileCreation()')).replace('state, normalizeSnapshot, render,',
  'handleModalBackdrop, exportCurrentSave, importCurrentSave, updateCurrentSaveReplaceStatus, openSaveEditor, showSaveEditor, queueSaveEditorEdit, flushSaveEditorEdit, reviewSaveEditor, requestCloseSaveEditor, updateSaveEditorStatus, state, normalizeSnapshot, render,');
let checks = 0;
async function test(platform) {
  const { ui, node, context, fixture, nativeRequests, actionFixtures, actionErrors, reply } = new Function('require', '__dirname', harness +
    '\nreturn {ui,node,context,fixture,nativeRequests,actionFixtures,actionErrors,reply:(data)=>nativeMessageListener({data})};')(require, __dirname);
  const sourceId = 'a'.repeat(64), safetyId = 'b'.repeat(64), snapshot = { ...fixture, platform, edition: 'local',
    accountContext: 'save-editor-' + platform, gameRunning: false, settings: { ...fixture.settings, gamePath: '/Synthetic/DaysGone' } },
    current = { saveEditorAvailable: true, context: 'current-save-scope', status: 'ready', signature: 'current-files', previewsSupported: false,
      saves: [{ id: sourceId, name: 'Save <01>.sav', size: 512, modifiedUtc: 1000, deleteEligible: true, exportEligible: true, replaceEligible: true }] },
    backups = { available: true, location: { resolved: true }, settings: { automatic: false, keep: 5 }, backups: [], recovery: { pending: false } };
  const session = (revision = 0, value = 2) => ({ id: 'native-editor-session', revision, sourceId, saveName: 'Save <01>.sav', dirty: revision > 0,
    fields: [{ id: 'player-skill-points', label: 'Unspent skill points', group: 'Progression', type: 'IntProperty', editor: 'integer', min: 0, max: 2147483647, value }],
    protectedFields: [{ label: 'Player level', value: 12, reason: 'Progress and unlocks are linked; this field stays unchanged.' }] });
  const plan = { token: 'native-slot-review', id: sourceId, operation: 'import-current-save', targetName: 'Save <01>.sav', sourceName: 'Imported <save>.sav',
    beforeBytes: 512, afterBytes: 600, safetyBackupRequired: true, warnings: ['Check the game account.'] };
  const reset = () => {
    node('#modal').close(); ui.handleModalClosed();
    Object.assign(ui.state, { preview: false, busy: false, loading: false, view: 'saves', savesTab: 'current', nativeDropActivity: null,
      profileSession: { active: false }, snapshot: ui.normalizeSnapshot(snapshot), currentSaves: current, saveBackups: backups,
      currentSaveSelection: { context: current.context, id: sourceId }, saveEditor: null, currentSaveReplaceReview: null });
    actionFixtures.set('snapshot', snapshot); actionFixtures.set('currentSavesList', current); actionFixtures.set('saveBackupsList', backups);
    actionFixtures.set('saveEditorOpen', { supported: true, session: session() }); actionFixtures.set('saveEditorClose', { closed: true, session: null });
    actionFixtures.set('saveEditorSet', { session: session(1, 4) });
    actionFixtures.set('saveEditorReview', { ...plan, operation: 'edit-current-save', token: 'native-editor-review', changes: [{ label: 'Unspent skill points', before: 2, after: 4 }] });
    actionFixtures.set('saveEditorApply', { replaced: true, safetyBackupId: safetyId });
  };
  reset();
  // Builds without a verified decoder fail closed on either platform.
  for (const capability of [false, undefined]) {
    ui.state.currentSaves = { ...current, saveEditorAvailable: capability };
    const disabled = ui.renderCurrentSavesPanel();
    assert(/data-current-save-edit="[^"]+"[^>]*disabled/.test(disabled));
    assert(disabled.includes('Save editing is not available in this build. Use the complete loader download with its verified save decoder.'));
    for (const control of ['data-current-save-export', 'data-current-save-import', 'data-current-save-delete']) assert(disabled.includes(control));
    const before = nativeRequests.length;
    assert.equal(await ui.openSaveEditor(sourceId), false);
    assert.equal(ui.state.saveEditor, null);
    assert.equal(nativeRequests.length, before, 'Disabled entry does not call the native editor');
    checks += 8;
  }
  reset(); assert(await ui.openSaveEditor(sourceId));
  assert(ui.queueSaveEditorEdit('player-skill-points', { value: '4' }));
  ui.state.currentSaves = { ...current, saveEditorAvailable: false };
  let beforeDisabled = nativeRequests.length;
  assert.equal(ui.queueSaveEditorEdit('player-skill-points', { value: '5' }), false);
  assert.equal(await ui.flushSaveEditorEdit(), false);
  assert.equal(await ui.reviewSaveEditor(), false);
  assert.equal(nativeRequests.length, beforeDisabled, 'A stale draft cannot set/review after capability is disabled');
  checks += 4;
  reset(); assert(await ui.openSaveEditor(sourceId));
  assert(ui.queueSaveEditorEdit('player-skill-points', { value: '4' }));
  assert(await ui.flushSaveEditorEdit()); assert(await ui.reviewSaveEditor());
  ui.state.currentSaves = { ...current, saveEditorAvailable: false };
  beforeDisabled = nativeRequests.length;
  assert.equal(await node('#save-editor-apply').onclick(), false);
  assert.equal(nativeRequests.length, beforeDisabled, 'A stale apply callback never reaches the native editor');
  checks += 2;
  reset();
  let html = ui.renderCurrentSavesPanel();
  for (const control of ['data-current-save-edit', 'data-current-save-export', 'data-current-save-import', 'data-current-save-delete']) assert(html.includes(control));
  actionFixtures.set('currentSaveExportBrowse', { exported: true });
  assert(await ui.exportCurrentSave(sourceId));
  assert.equal(nativeRequests.filter((request) => request.action === 'currentSaveExportBrowse').at(-1).id, sourceId);
  assert(!nativeRequests.some((request) => request.action === 'currentSaveReplace'));
  checks += 7;

  ui.state.currentSaves = { ...current, saves: [{ ...current.saves[0], deleteEligible: false, exportEligible: true, replaceEligible: false }] };
  html = ui.renderCurrentSavesPanel();
  assert(html.includes('data-current-save-export') && !html.includes('data-current-save-delete'));
  assert(html.includes('data-current-save-inspect') && html.includes('Inspect save') && !html.includes('data-current-save-edit') && !html.includes('data-current-save-import'));
  assert(await ui.exportCurrentSave(sourceId), 'Read-only export does not depend on a deletion callback');
  ui.state.currentSaves = { ...current, saves: [{ ...current.saves[0], size: 81 * 1024 * 1024, deleteEligible: true, exportEligible: false, replaceEligible: false }] };
  html = ui.renderCurrentSavesPanel();
  assert(html.includes('data-current-save-delete') && !html.includes('data-current-save-export') && !html.includes('data-current-save-edit'));
  assert.equal(await ui.exportCurrentSave(sourceId), false);
  assert.equal(await ui.importCurrentSave(sourceId), false);
  checks += 6; reset();

  actionFixtures.set('currentSaveImportBrowse', plan); actionFixtures.set('currentSaveReplace', { replaced: true, safetyBackupId: safetyId });
  assert(await ui.importCurrentSave(sourceId));
  html = node('#modal-content').innerHTML;
  assert(html.includes('Save &lt;01&gt;.sav') && html.includes('Imported &lt;save&gt;.sav'));
  assert(html.includes('Other save slots') && html.includes('even when automatic backups are off'));
  assert(!nativeRequests.some((request) => request.action === 'currentSaveReplace'));
  ui.state.snapshot.gameRunning = true; ui.updateCurrentSaveReplaceStatus();
  assert(node('#save-slot-confirm').disabled); await node('#save-slot-confirm').onclick();
  assert(!nativeRequests.some((request) => request.action === 'currentSaveReplace'));
  ui.state.snapshot.gameRunning = false;
  assert(await node('#save-slot-confirm').onclick());
  const replaced = nativeRequests.filter((request) => request.action === 'currentSaveReplace').at(-1);
  assert.equal(replaced.token, plan.token); assert.equal(replaced.confirmed, true); assert(!('path' in replaced) && !('id' in replaced));
  checks += 10;

  reset(); assert(await ui.openSaveEditor(sourceId));
  html = node('#modal-content').innerHTML;
  assert(html.includes('Unspent skill points') && html.includes('Protected and read-only progress'));
  node('#save-editor-filter-read-only').onclick();
  const protectedHtml = node('#save-editor-inspector').innerHTML;
  assert(protectedHtml.includes('Player level') && !protectedHtml.includes('data-save-editor-field="player-level"'));
  node('#save-editor-filter-editable').onclick();
  assert(!html.includes('Current health') && !html.includes('Experience points'), 'No unsupported gameplay fields are invented');
  assert.equal(ui.queueSaveEditorEdit('player-skill-points', { value: '-1' }), false);
  assert.equal(await ui.reviewSaveEditor(), false);
  assert.equal(await ui.flushSaveEditorEdit(), false);
  assert(ui.queueSaveEditorEdit('player-skill-points', { value: '4' }));
  assert(await ui.flushSaveEditorEdit());
  const edited = nativeRequests.filter((request) => request.action === 'saveEditorSet').at(-1);
  assert.equal(edited.session, session().id); assert.equal(edited.revision, 0); assert.equal(edited.field, 'player-skill-points'); assert.equal(edited.value, 4);
  assert(!('path' in edited) && !('offset' in edited));
  assert.equal(ui.state.saveEditor.session.revision, 1); assert.equal(ui.state.saveEditor.pending, null);
  assert(await ui.reviewSaveEditor());
  html = node('#modal-content').innerHTML;
  assert(html.includes('Unspent skill points') && html.includes('Back up and apply changes') && html.includes('Protected progress fields are left unchanged'));
  const applyBefore = nativeRequests.filter((request) => request.action === 'saveEditorApply').length;
  ui.state.snapshot.gameRunning = true; ui.updateSaveEditorStatus();
  assert(node('#save-editor-apply').disabled); await node('#save-editor-apply').onclick();
  assert.equal(nativeRequests.filter((request) => request.action === 'saveEditorApply').length, applyBefore);
  ui.state.snapshot.gameRunning = false;
  assert(await node('#save-editor-apply').onclick());
  const applied = nativeRequests.filter((request) => request.action === 'saveEditorApply').at(-1);
  assert.equal(applied.token, 'native-editor-review'); assert.equal(applied.confirmed, true);
  assert(!('value' in applied) && !('path' in applied) && !('session' in applied));
  assert.equal(ui.state.saveEditor, null);
  checks += 25;

  reset(); assert(await ui.openSaveEditor(sourceId));
  assert(ui.queueSaveEditorEdit('player-skill-points', { value: '4' })); assert(await ui.flushSaveEditorEdit());
  const closeBefore = nativeRequests.filter((request) => request.action === 'saveEditorClose').length;
  assert(ui.requestCloseSaveEditor());
  assert(node('#modal-content').innerHTML.includes('Discard this save draft?'));
  assert.equal(nativeRequests.filter((request) => request.action === 'saveEditorClose').length, closeBefore);
  node('#save-editor-keep-editing').onclick();
  assert(node('#modal-content').innerHTML.includes('Unspent skill points'));
  ui.requestCloseSaveEditor(); assert(await node('#save-editor-discard').onclick());
  assert.equal(ui.state.saveEditor, null); assert.equal(nativeRequests.filter((request) => request.action === 'saveEditorClose').length, closeBefore + 1);
  checks += 10;

  reset(); assert(await ui.openSaveEditor(sourceId));
  assert(ui.queueSaveEditorEdit('player-skill-points', { value: '4' })); assert(await ui.flushSaveEditorEdit());
  const dialog = node('#modal'), backdrop = ui.handleModalBackdrop;
  dialog.getBoundingClientRect = () => ({ left: 100, top: 100, right: 400, bottom: 400 });
  backdrop({ target: dialog, clientX: 10, clientY: 10 });
  assert(dialog.open && ui.state.saveEditor.mode === 'discard', 'A backdrop click reviews draft discard instead of hiding it');
  assert(ui.state.saveEditor.session.dirty);
  backdrop({ target: dialog, clientX: 10, clientY: 10 });
  assert(dialog.open && ui.state.saveEditor.mode === 'edit', 'Backdrop dismissal of discard keeps the pending draft');
  checks += 6;

  reset(); actionFixtures.set('saveEditorOpen', { supported: false, reason: 'This gameplay layout is not verified.' });
  assert.equal(await ui.openSaveEditor(sourceId), false);
  assert(node('#modal-content').innerHTML.includes('This gameplay layout is not verified'));
  assert.equal(ui.state.saveEditor, null);
  checks += 3;

  reset(); actionFixtures.set('saveEditorOpen', { supported: true, session: { ...session(), fields: [], protectedFields: [] } });
  assert.equal(await ui.openSaveEditor(sourceId), false);
  assert.equal(ui.state.saveEditor, null);
  assert(!node('#modal-content').innerHTML.includes('id="save-editor-review"'));
  checks += 3;

  for (const wrong of [{ operation: 'import-current-save' }, { id: 'c'.repeat(64) }, { safetyBackupRequired: false }]) {
    reset(); assert(await ui.openSaveEditor(sourceId));
    assert(ui.queueSaveEditorEdit('player-skill-points', { value: '4' })); assert(await ui.flushSaveEditorEdit());
    actionFixtures.set('saveEditorReview', { ...plan, operation: 'edit-current-save', token: 'native-editor-review', changes: [{ label: 'Unspent skill points', before: 2, after: 4 }], ...wrong });
    assert.equal(await ui.reviewSaveEditor(), false);
    assert(ui.state.saveEditor.error.includes('verified plan'));
    assert(!node('#modal-content').innerHTML.includes('id="save-editor-apply"'));
    checks += 6;
  }

  reset(); assert(await ui.openSaveEditor(sourceId));
  actionErrors.set('saveEditorSet', 'The checked save field is not writable.');
  assert(ui.queueSaveEditorEdit('player-skill-points', { value: '4' }));
  assert.equal(await ui.flushSaveEditorEdit(), false);
  assert.equal(ui.state.saveEditor.pending.raw, '4'); assert(ui.state.saveEditor.error.includes('not writable'));
  assert.equal(ui.state.saveEditor.session.revision, 0);
  actionErrors.delete('saveEditorSet');
  checks += 6;

  reset();
  const bridge = context.window.chrome.webview, post = bridge.postMessage;
  let held;
  bridge.postMessage = (request) => { if (request.action === 'saveEditorOpen') { held = request; nativeRequests.push(request); } else post.call(bridge, request); };
  const work = ui.openSaveEditor(sourceId);
  for (let i = 0; i < 40 && !held; ++i) await new Promise(setImmediate);
  assert(held); ui.showChangelog('1.0.7'); const newer = node('#modal-content').innerHTML;
  reply({ requestId: held.requestId, ok: true, data: { supported: true, session: session() } });
  assert.equal(await work, false); assert.equal(node('#modal-content').innerHTML, newer); assert.equal(ui.state.saveEditor, null);
  checks += 4;

  bridge.postMessage = post;
  reset(); assert(await ui.openSaveEditor(sourceId));
  const hold = (action) => { held = null; bridge.postMessage = (request) => { if (request.action === action && !held) { held = request; nativeRequests.push(request); } else post.call(bridge, request); }; };
  const waitHeld = async () => { for (let i = 0; i < 40 && !held; ++i) await new Promise(setImmediate); assert(held); };
  hold('saveEditorSet');
  assert(ui.queueSaveEditorEdit('player-skill-points', { value: '4' }));
  const lateSet = ui.flushSaveEditorEdit(); await waitHeld();
  ui.state.snapshot.settings.gamePath = '/Another/DaysGone';
  reply({ requestId: held.requestId, ok: true, data: { session: session(1, 4) } });
  assert.equal(await lateSet, false);
  assert.equal(ui.state.saveEditor.working, false, 'A changed installation does not leave an uncloseable busy draft');
  assert.equal(ui.state.saveEditor.session.revision, 0);
  assert.equal(ui.state.saveEditor.pending.raw, '4');
  checks += 6;

  bridge.postMessage = post;
  reset(); assert(await ui.openSaveEditor(sourceId)); hold('saveEditorClose');
  const lateClose = ui.requestCloseSaveEditor(); await waitHeld();
  ui.showChangelog('1.0.7'); const afterCloseDialog = node('#modal-content').innerHTML;
  reply({ requestId: held.requestId, ok: true, data: { closed: true, session: null } });
  assert(await lateClose);
  assert.equal(node('#modal-content').innerHTML, afterCloseDialog, 'A completed draft close cannot close a newer dialog');
  assert.equal(ui.state.saveEditor, null);
  checks += 4;

  bridge.postMessage = post;
  reset(); hold('currentSaveImportBrowse');
  const replacesBefore = nativeRequests.filter((request) => request.action === 'currentSaveReplace').length;
  const lateImport = ui.importCurrentSave(sourceId); await waitHeld();
  ui.showChangelog('1.0.7'); const afterImportDialog = node('#modal-content').innerHTML;
  reply({ requestId: held.requestId, ok: true, data: plan });
  assert.equal(await lateImport, false);
  assert.equal(node('#modal-content').innerHTML, afterImportDialog, 'A late import review cannot replace a newer dialog');
  assert.equal(nativeRequests.filter((request) => request.action === 'currentSaveReplace').length, replacesBefore);
  checks += 3;
}
(async () => { for (const platform of ['windows', 'linux']) await test(platform); console.log(`Current-slot and gameplay editor UI passed (${checks} Windows/Linux checks).`); })()
  .catch((error) => { console.error(error); process.exitCode = 1; });
