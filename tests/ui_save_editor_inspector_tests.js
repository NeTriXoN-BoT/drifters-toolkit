'use strict';

// Synthetic native metadata only. No real save, decoder, account or game is used.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
let harness = fs.readFileSync(path.join(__dirname, 'ui_smoke.js'), 'utf8');
harness = harness.slice(0, harness.indexOf('async function testProfileCreation()')).replace('state, normalizeSnapshot, render,',
  'saveEditorSession,saveEditorEntries,saveEditorInspector,saveEditorInspectorBody,updateSaveEditorInspector,openSaveEditor,showSaveEditor,queueSaveEditorEdit,flushSaveEditorEdit,reviewSaveEditor,state,normalizeSnapshot,render,');
let checks = 0;
async function test(platform) {
  const env = new Function('require', '__dirname', harness + '\nreturn {ui,node,fixture,nativeRequests,actionFixtures};')(require, __dirname);
  const { ui, node, fixture, nativeRequests, actionFixtures } = env;
  const id = 'a'.repeat(64), snapshot = { ...fixture, platform, accountContext: 'inspector-' + platform,
    gameRunning: false, settings: { ...fixture.settings, gamePath: '/Synthetic/DaysGone' } };
  const protectedFields = Array.from({ length: 77 }, (_, index) => ({ id: 'checked-' + index,
    label: index === 46 ? '<Saved health>' : 'Recorded value ' + index,
    group: index < 40 ? 'Player attributes' : 'Saved data', type: index % 2 ? 'IntProperty' : 'ArrayProperty',
    value: index % 2 ? index : '12 entries', reason: 'Linked state is preserved; this entry is read-only.' }));
  protectedFields[46].value = '<img src=x onerror="window.inspectorInjected=true"><a href="https://example.invalid/">local text</a>';
  const session = (revision = 1, value = 2) => ({ id: 'inspector-session', revision, sourceId: id, saveName: 'Synthetic save.sav', dirty: revision > 1,
    fields: [{ id: 'player-skill-points', label: 'Unspent skill points', group: 'Player progression', editor: 'integer',
      type: 'IntProperty', min: 0, max: 2147483647, value, originalValue: 2 },
    { id: 'synthetic-checked-number', label: 'Synthetic approved decimal', group: 'Synthetic fixture', editor: 'number',
      type: 'FloatProperty', min: 0, max: 10, value: 1.5, originalValue: 1.5 }], protectedFields, inspectorTruncated: true });
  const current = { saveEditorAvailable: true, context: 'synthetic-current', status: 'ready', saves: [{ id,
    name: 'Synthetic save.sav', size: 512, replaceEligible: true, exportEligible: true, deleteEligible: true }] };
  Object.assign(ui.state, { preview: false, loading: false, busy: false, view: 'saves', savesTab: 'current',
    snapshot: ui.normalizeSnapshot(snapshot), currentSaves: current,
    currentSaveSelection: { context: current.context, id }, profileSession: { active: false }, saveEditor: null });
  actionFixtures.set('saveEditorOpen', { supported: true, session: session() });
  actionFixtures.set('saveEditorClose', { closed: true, session: null });
  actionFixtures.set('saveEditorSet', { session: session(2, 4) });
  actionFixtures.set('saveEditorReview', { token: 'inspector-plan', id, operation: 'edit-current-save', safetyBackupRequired: true,
    changes: [{ label: 'Unspent skill points', before: 2, after: 4 }] });
  assert(await ui.openSaveEditor(id));
  const record = ui.state.saveEditor;
  assert.equal(ui.saveEditorInspector().filter, 'editable');
  assert.equal(ui.saveEditorEntries().length, 79);
  assert.equal(ui.saveEditorInspector().shown.length, 2);
  assert(node('#modal-content').innerHTML.includes('bounded inspector'));
  assert(node('#save-editor-inspector').innerHTML.includes('inputmode="decimal"'));
  checks += 6;

  const requestsBeforeSearch = nativeRequests.length;
  node('#save-editor-filter-read-only').onclick();
  let view = ui.saveEditorInspector(), html = node('#save-editor-inspector').innerHTML;
  assert.equal(view.filtered.length, 77); assert.equal(view.shown.length, 24);
  assert.equal(view.pages, 4); assert(!html.includes('data-save-editor-field='));
  assert(html.includes('12 entries') && html.includes('Read-only') && html.includes('Linked state'));
  node('#save-editor-page-next').onclick();
  assert.equal(ui.saveEditorInspector().page, 1);
  assert.equal(ui.saveEditorInspector().shown.length, 24);
  const search = node('#save-editor-search'); search.value = 'saved health'; search.oninput();
  view = ui.saveEditorInspector(); html = node('#save-editor-inspector').innerHTML;
  assert.equal(view.page, 0); assert.equal(view.filtered.length, 1);
  assert(html.includes('&lt;Saved health&gt;') && !html.includes('<Saved health>'));
  assert(html.includes('&lt;img src=x onerror=') && !html.includes('<img src=') && !html.includes('<a href='));
  assert.equal(nativeRequests.length, requestsBeforeSearch, 'Search/filter/page never decode or edit a save');
  checks += 12;

  let prevented = 0; search.onkeydown({ key: 'Escape', preventDefault: () => ++prevented, stopPropagation: () => ++prevented });
  assert.equal(search.value, ''); assert.equal(prevented, 2);
  search.value = 'does not exist'; search.oninput();
  assert.equal(ui.saveEditorInspector().shown.length, 0);
  assert(node('#save-editor-inspector').innerHTML.includes('No values match'));
  node('#save-editor-filter-all').onclick(); search.value = 'synthetic approved'; search.oninput();
  assert.equal(ui.saveEditorInspector().shown.length, 1);
  assert(ui.queueSaveEditorEdit('synthetic-checked-number', { value: '2.75' }));
  assert.equal(record.pending.value, 2.75);
  assert.equal(ui.queueSaveEditorEdit('synthetic-checked-number', { value: '11' }), false);
  assert.equal(ui.queueSaveEditorEdit('checked-46', { value: '10' }), false, 'Read-only metadata cannot authorize a write');
  checks += 9;

  record.pending = null; record.error = '';
  record.inspectorQuery = 'skill'; record.inspectorFilter = 'all'; record.inspectorPage = 0;
  assert(ui.queueSaveEditorEdit('player-skill-points', { value: '4' }));
  assert(await ui.flushSaveEditorEdit());
  assert.equal(record.inspectorQuery, 'skill'); assert.equal(record.inspectorFilter, 'all');
  assert(node('#save-editor-inspector').innerHTML.includes('Saved value: 2'));
  assert(await ui.reviewSaveEditor()); node('#save-editor-back').onclick();
  assert.equal(record.inspectorQuery, 'skill'); assert.equal(record.inspectorFilter, 'all');
  // Advanced editing is discoverable but requires an explicit opt-in. The
  // native bridge receives that decision for both draft changes and review.
  record.session.fields.push({ id: 'health-use-effect-level-0', label: 'Health / UseEffectLevels[0]',
    group: 'Player attributes', type: 'FloatProperty', editor: 'number', min: -3.4e38, max: 3.4e38,
    value: 1.5, originalValue: 1.5, advanced: true, gameplayVerified: false },
  { id: 'controller-can-be-damaged', label: 'bCanBeDamaged', group: 'Controller flags', type: 'BoolProperty', editor: 'boolean',
    value: true, originalValue: true, advanced: true, gameplayVerified: false });
  record.inspectorQuery = ''; record.inspectorFilter = 'editable'; ui.showSaveEditor(record);
  assert.equal(ui.saveEditorInspector().shown.length, 2);
  assert(node('#modal-content').innerHTML.includes('Advanced fields'));
  assert.equal(ui.queueSaveEditorEdit('health-use-effect-level-0', { value: '3.5' }), false);
  node('#save-editor-advanced').checked = true; node('#save-editor-advanced').onchange();
  assert.equal(ui.saveEditorInspector().shown.length, 4);
  assert(node('#save-editor-inspector').innerHTML.includes('Advanced · Not game-tested'));
  assert(ui.queueSaveEditorEdit('controller-can-be-damaged', { checked: false }));
  assert.equal(record.pending.value, false);
  const advancedSession = JSON.parse(JSON.stringify(record.session));
  advancedSession.revision += 1; advancedSession.dirty = true;
  advancedSession.fields.find((field) => field.id === 'controller-can-be-damaged').value = false;
  actionFixtures.set('saveEditorSet', { session: advancedSession });
  assert(await ui.flushSaveEditorEdit());
  const advancedRequest = nativeRequests.filter((request) => request.action === 'saveEditorSet').at(-1);
  assert.equal(advancedRequest.advanced, true); assert.equal(advancedRequest.value, false);
  node('#save-editor-advanced').checked = false; node('#save-editor-advanced').onchange();
  assert(node('#save-editor-review').disabled);
  const beforeBlockedReview = nativeRequests.length;
  assert.equal(await ui.reviewSaveEditor(), false); assert.equal(nativeRequests.length, beforeBlockedReview);
  assert(node('#save-editor-status').textContent.includes('Advanced changes are still in this draft'));
  node('#save-editor-advanced').checked = true; node('#save-editor-advanced').onchange();
  actionFixtures.set('saveEditorReview', { token: 'advanced-plan', id, operation: 'edit-current-save', safetyBackupRequired: true,
    advancedChangeCount: 1, advancedWarning: 'Check these changes in the game before keeping them.',
    changes: [{ label: 'bCanBeDamaged', before: true, after: false, advanced: true, gameplayVerified: false }] });
  assert(await ui.reviewSaveEditor());
  assert.equal(nativeRequests.filter((request) => request.action === 'saveEditorReview').at(-1).advanced, true);
  assert(node('#modal-content').innerHTML.includes('Advanced changes included'));
  node('#save-editor-back').onclick();
  assert.equal(record.advancedEnabled, true);
  checks += 18;
  const staleSearch = node('#save-editor-search').oninput;
  ui.showChangelog('1.0.7');
  const newer = node('#modal-content').innerHTML;
  assert.equal(staleSearch(), false); assert.equal(node('#modal-content').innerHTML, newer);
  checks += 10;

  assert.equal(ui.saveEditorSession({ ...session(), fields: [...session().fields, session().fields[0]] }), null);
  assert.equal(ui.saveEditorSession({ ...session(), protectedFields: Array(513).fill({}) }), null);
  assert.equal(ui.saveEditorSession({ ...session(), protectedFields: {} }), null);
  assert.equal(ui.saveEditorSession({ ...session(), fields: Array.from({ length: 513 }, (_, index) => ({ id: String(index) })) }), null);
  const bounded = { session: { fields: [], protectedFields: Array(1000).fill({ label: 'Count', value: { raw: ['not exposed'] } }) }, inspectorFilter: 'all' };
  assert.equal(ui.saveEditorEntries(bounded).length, 512);
  assert.equal(ui.saveEditorInspector(bounded).shown.length, 24);
  assert(ui.saveEditorInspectorBody(bounded).includes('Not exposed'));
  const completeText = 'x'.repeat(4096), oversizedText = completeText + 'x';
  assert(ui.saveEditorInspectorBody({ session: { fields: [], protectedFields: [{ label: 'Text', value: completeText }] }, inspectorFilter: 'all' }).includes(completeText));
  assert(ui.saveEditorInspectorBody({ session: { fields: [], protectedFields: [{ label: 'Text', value: oversizedText }] }, inspectorFilter: 'all' }).includes('(display shortened)'));
  checks += 9;

  node('#modal').close(); ui.handleModalClosed(); ui.state.saveEditor = null;
  actionFixtures.set('saveEditorOpen', { supported: false, inspectionAvailable: true,
    session: { ...session(), fields: [], readOnly: true } });
  assert(await ui.openSaveEditor(id));
  assert.equal(ui.saveEditorInspector().filter, 'read-only');
  assert(node('#modal-content').innerHTML.includes('Inspect save'));
  assert(node('#save-editor-review').hidden && node('#save-editor-review').disabled);
  const readOnlyRequests = nativeRequests.length;
  assert.equal(ui.queueSaveEditorEdit('checked-46', { value: '2' }), false);
  assert.equal(await ui.reviewSaveEditor(), false);
  assert.equal(nativeRequests.length, readOnlyRequests);
  checks += 7;

  // Running-game inspection is a separate immutable native session, never a
  // promoted editor. It also remains usable during a portable PAK session.
  node('#modal').close(); ui.handleModalClosed(); ui.state.saveEditor = null;
  ui.state.snapshot.gameRunning = true; ui.state.profileSession = { active: true };
  actionFixtures.set('saveEditorOpen', { supported: true, inspectionAvailable: true,
    session: { ...session(), id: 'inspection-only-session', dirty: false, inspectionOnly: true, readOnly: true } });
  const normalBefore = nativeRequests.length;
  assert.equal(await ui.openSaveEditor(id), false);
  assert.equal(nativeRequests.length, normalBefore);
  assert(await ui.openSaveEditor(id, true));
  assert.equal(nativeRequests.filter((request) => request.action === 'saveEditorOpen').at(-1).inspectOnly, true);
  assert.equal(ui.saveEditorInspector().filter, 'read-only');
  assert(node('#save-editor-review').hidden);
  assert.equal(ui.queueSaveEditorEdit('player-skill-points', { value: '5' }), false);
  assert.equal(await ui.reviewSaveEditor(), false);
  ui.state.saveEditor.session.dirty = true;
  assert.equal(await ui.reviewSaveEditor(), false, 'Inspection stays non-writable even if an unrelated dirty marker appears');
  ui.state.saveEditor.session.dirty = false;
  const inspection = ui.state.saveEditor, beforePromotion = nativeRequests.length;
  assert.equal(await ui.openSaveEditor(id, false), false);
  assert.equal(ui.state.saveEditor, inspection); assert.equal(nativeRequests.length, beforePromotion);
  ui.state.snapshot.gameRunning = false; ui.state.profileSession = { active: false };
  actionFixtures.set('saveEditorOpen', { supported: true, session: session() });
  assert(await ui.openSaveEditor(id, false));
  assert.notEqual(ui.state.saveEditor, inspection);
  assert.equal(ui.state.saveEditor.session.id, 'inspector-session');
  assert(nativeRequests.some((request) => request.action === 'saveEditorClose' && request.session === 'inspection-only-session'));
  assert.equal(nativeRequests.filter((request) => request.action === 'saveEditorOpen').at(-1).inspectOnly, undefined);
  assert.equal(ui.saveEditorSession({ ...session(), inspectionOnly: true, readOnly: false }), null);
  checks += 18;
}
(async () => { for (const platform of ['windows', 'linux']) await test(platform); console.log(`Save editor inspector passed (${checks} Windows/Linux checks).`);
  await require('./ui_save_editor_queue_tests.js')(); })()
  .catch((error) => { console.error(error); process.exitCode = 1; });
