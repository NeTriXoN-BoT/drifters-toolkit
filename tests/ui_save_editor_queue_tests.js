'use strict';

// Synthetic save sessions only. No save files, decoder or real account is used.
const assert = require('node:assert/strict'), fs = require('node:fs'), path = require('node:path');
let harness = fs.readFileSync(path.join(__dirname, 'ui_smoke.js'), 'utf8');
harness = harness.slice(0, harness.indexOf('async function testProfileCreation()')).replace('state, normalizeSnapshot, render,',
  'openSaveEditor,showSaveEditor,queueSaveEditorEdit,flushSaveEditorEdit,reviewSaveEditor,requestCloseSaveEditor,currentSavePlanBody,state,normalizeSnapshot,render,');
let checks = 0;
async function fixtureFor(platform, fieldCount = 3) {
  const env = new Function('require', '__dirname', harness +
    '\nreturn {ui,node,context,fixture,nativeRequests,timers,reply:(data)=>nativeMessageListener({data})};')(require, __dirname);
  const { ui, node, context, fixture, nativeRequests, reply } = env, id = 'a'.repeat(64), fields = [
    { id: 'player-skill-points', label: 'Unspent skill points', type: 'IntProperty', editor: 'integer', min: 0, max: 2147483647, value: 2, originalValue: 2 },
    { id: 'controller-flag-bCanBeDamaged', label: 'bCanBeDamaged', type: 'BoolProperty', editor: 'boolean', value: true, originalValue: true, advanced: true, gameplayVerified: false },
    { id: 'attribute-health-effect-0', label: 'Health / UseEffectLevels[0]', type: 'FloatProperty', editor: 'number', min: -3.4e38, max: 3.4e38, value: 1, originalValue: 1, advanced: true, gameplayVerified: false }
  ];
  while (fields.length < fieldCount) fields.push({ ...fields[0], id: 'extra-' + fields.length, label: 'Extra ' + fields.length });
  const session = { id: 'queue-session', revision: 0, sourceId: id, saveName: 'Synthetic.sav', dirty: false, fields, protectedFields: [] },
    snapshot = { ...fixture, platform, edition: 'local', accountContext: 'queue-' + platform, gameRunning: false,
      settings: { ...fixture.settings, gamePath: '/Synthetic/DaysGone' } },
    current = { saveEditorAvailable: true, context: 'queue-current', status: 'ready', saves: [{ id,
      name: 'Synthetic.sav', size: 512, replaceEligible: true, exportEligible: true }] };
  Object.assign(ui.state, { preview: false, loading: false, busy: false, view: 'saves', savesTab: 'current',
    snapshot: ui.normalizeSnapshot(snapshot), currentSaves: current, currentSaveSelection: { context: current.context, id },
    profileSession: { active: false }, saveEditor: null });
  const firstInput = node('#save-editor-field-0'); firstInput.id = 'save-editor-field-0';
  firstInput.dataset.saveEditorField = 'player-skill-points';
  node('#save-editor-inspector').querySelectorAll = (selector) => selector === '[data-save-editor-field]' ? [firstInput] : [];
  const bridge = context.window.chrome.webview, originalPost = bridge.postMessage, held = [];
  let holdSet = false, inFlight = 0, maxInFlight = 0;
  const finishSet = (request) => {
    assert.equal(request.revision, session.revision, 'Each native set uses the preceding acknowledged revision');
    const field = session.fields.find((entry) => entry.id === request.field); assert(field);
    if (field.advanced) assert.equal(request.advanced, true);
    field.value = request.value; session.revision += 1; session.dirty = true;
    --inFlight; reply({ requestId: request.requestId, ok: true, data: { session: JSON.parse(JSON.stringify(session)) } });
  };
  bridge.postMessage = (request) => {
    if (!request.action.startsWith('saveEditor')) return originalPost.call(bridge, request);
    nativeRequests.push(request);
    if (request.action === 'saveEditorOpen') queueMicrotask(() => reply({ requestId: request.requestId, ok: true,
      data: { supported: true, session: JSON.parse(JSON.stringify(session)) } }));
    else if (request.action === 'saveEditorSet') {
      maxInFlight = Math.max(maxInFlight, ++inFlight);
      if (holdSet) held.push(request); else queueMicrotask(() => finishSet(request));
    } else if (request.action === 'saveEditorReview') queueMicrotask(() => reply({ requestId: request.requestId, ok: true,
      data: { token: 'queue-review', id, operation: 'edit-current-save', safetyBackupRequired: true,
        advancedChangeCount: session.fields.filter((entry) => entry.advanced && entry.value !== entry.originalValue).length,
        changes: session.fields.filter((entry) => entry.value !== entry.originalValue).map((entry) => ({ label: entry.label,
          before: entry.originalValue, after: entry.value })) } }));
    else if (request.action === 'saveEditorClose') queueMicrotask(() => reply({ requestId: request.requestId, ok: true, data: { closed: true, session: null } }));
    else throw Error('Unexpected synthetic editor action ' + request.action);
  };
  assert(await ui.openSaveEditor(id));
  node('#save-editor-advanced').checked = true; node('#save-editor-advanced').onchange();
  return { ...env, session, id, sets: () => nativeRequests.filter((request) => request.action === 'saveEditorSet'),
    reviews: () => nativeRequests.filter((request) => request.action === 'saveEditorReview'),
    hold: () => { holdSet = true; }, release: () => { holdSet = false; held.splice(0).forEach(finishSet); },
    maximum: () => maxInFlight, held };
}
async function test(platform) {
  let f = await fixtureFor(platform), { ui, node } = f;
  assert(ui.queueSaveEditorEdit('player-skill-points', { value: '4' }));
  assert(ui.queueSaveEditorEdit('controller-flag-bCanBeDamaged', { checked: false }));
  assert(ui.queueSaveEditorEdit('attribute-health-effect-0', { value: '2.5' }));
  assert(await ui.reviewSaveEditor(), 'Review validates every queued field before requesting its change plan');
  assert.deepEqual(f.sets().map((request) => request.field), ['player-skill-points', 'controller-flag-bCanBeDamaged', 'attribute-health-effect-0']);
  assert.deepEqual(f.sets().map((request) => request.value), [4, false, 2.5]);
  assert.equal(f.reviews()[0].revision, 3); assert.equal(f.maximum(), 1);
  assert.equal(ui.state.saveEditor.pending, null);
  assert(node('#modal-content').innerHTML.includes('Unspent skill points') && node('#modal-content').innerHTML.includes('bCanBeDamaged'));
  checks += 10;

  f = await fixtureFor(platform); ({ ui, node } = f);
  ui.queueSaveEditorEdit('player-skill-points', { value: '4' });
  ui.queueSaveEditorEdit('controller-flag-bCanBeDamaged', { checked: false });
  ui.queueSaveEditorEdit('player-skill-points', { value: '7' });
  assert(await ui.reviewSaveEditor()); assert.equal(f.sets().length, 2);
  assert.equal(f.sets().find((request) => request.field === 'player-skill-points').value, 7);
  checks += 3;

  f = await fixtureFor(platform); ({ ui, node } = f);
  f.hold(); ui.queueSaveEditorEdit('player-skill-points', { value: '4' });
  const first = ui.flushSaveEditorEdit();
  for (let n = 0; n < 40 && !f.held.length; ++n) await new Promise(setImmediate);
  assert.equal(f.held.length, 1);
  ui.queueSaveEditorEdit('player-skill-points', { value: '8' });
  ui.queueSaveEditorEdit('controller-flag-bCanBeDamaged', { checked: false });
  assert.equal(await ui.flushSaveEditorEdit(), false, 'An in-flight set prevents a concurrent native mutation');
  f.release(); assert(await first); assert(await ui.reviewSaveEditor());
  assert.deepEqual(f.sets().map((request) => request.value), [4, 8, false]);
  assert.equal(f.maximum(), 1); checks += 6;

  f = await fixtureFor(platform); ({ ui, node } = f);
  ui.queueSaveEditorEdit('player-skill-points', { value: '4' });
  ui.queueSaveEditorEdit('controller-flag-bCanBeDamaged', { checked: false });
  node('#save-editor-advanced').checked = false; node('#save-editor-advanced').onchange();
  assert.equal(await ui.reviewSaveEditor(), false); assert.equal(f.sets().length, 0);
  node('#save-editor-advanced').checked = true; node('#save-editor-advanced').onchange();
  assert(await ui.reviewSaveEditor()); assert.equal(f.sets().length, 2); checks += 4;

  f = await fixtureFor(platform); ({ ui, node } = f);
  ui.queueSaveEditorEdit('player-skill-points', { value: '-1' });
  ui.queueSaveEditorEdit('controller-flag-bCanBeDamaged', { checked: false });
  assert.equal(await ui.reviewSaveEditor(), false); assert.equal(f.reviews().length, 0);
  assert(await ui.flushSaveEditorEdit()); assert.equal(f.sets()[0].value, false);
  assert.equal(await ui.reviewSaveEditor(), false);
  ui.showSaveEditor(); assert(node('#save-editor-inspector').innerHTML.includes('value="-1"'));
  node('#save-editor-revert').onclick(); assert.equal(ui.state.saveEditor.pending, null);
  assert.equal(await ui.flushSaveEditorEdit(), false); checks += 8;

  f = await fixtureFor(platform); ({ ui, node } = f);
  ui.queueSaveEditorEdit('player-skill-points', { value: '4' });
  ui.queueSaveEditorEdit('controller-flag-bCanBeDamaged', { checked: false });
  const staleInput = node('#save-editor-field-0').oninput, staleTimers = [...f.timers.values()].filter((timer) => timer.delay === 300);
  assert(ui.requestCloseSaveEditor()); assert(await node('#save-editor-discard').onclick());
  assert.equal(ui.state.saveEditor, null); assert.equal(staleInput(), false);
  assert.equal(await ui.flushSaveEditorEdit(), false); assert.equal(f.sets().length, 0);
  assert(await ui.openSaveEditor(f.id));
  ui.queueSaveEditorEdit('player-skill-points', { value: '8' });
  staleTimers.forEach((timer) => timer.callback()); await new Promise(setImmediate);
  assert.equal(f.sets().length, 0, 'A discarded draft timer cannot flush values from a newly opened draft');
  assert.equal(ui.state.saveEditor.pending.value, 8); checks += 9;

  f = await fixtureFor(platform); ({ ui, node } = f);
  f.hold(); ui.queueSaveEditorEdit('player-skill-points', { value: '4' });
  ui.queueSaveEditorEdit('controller-flag-bCanBeDamaged', { checked: false });
  const staleSet = ui.flushSaveEditorEdit();
  for (let n = 0; n < 40 && !f.held.length; ++n) await new Promise(setImmediate);
  ui.state.snapshot.settings.gamePath = '/Another/DaysGone';
  f.release(); assert.equal(await staleSet, false); assert.equal(await ui.flushSaveEditorEdit(), false);
  assert.equal(f.sets().length, 1); assert.equal(ui.state.saveEditor.session.revision, 0); checks += 4;

  f = await fixtureFor(platform, 512); ({ ui, node } = f);
  for (const field of f.session.fields) ui.queueSaveEditorEdit(field.id, field.type === 'BoolProperty' ? { checked: false } : { value: '4' });
  assert(await ui.reviewSaveEditor()); assert.equal(f.sets().length, 512); assert.equal(f.maximum(), 1);
  assert(node('#modal-content').innerHTML.includes('Extra 511'), 'The change review displays the last of all512 native-approved fields');
  checks += 4;
}
async function runQueueTests() {
  checks = 0;
  for (const platform of ['windows', 'linux']) await test(platform);
  console.log(`Save editor queued fields passed (${checks} Windows/Linux checks).`);
}
module.exports = runQueueTests;
if (require.main === module) runQueueTests().catch((error) => { console.error(error); process.exitCode = 1; });
