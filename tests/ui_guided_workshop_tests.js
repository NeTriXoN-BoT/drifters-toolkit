'use strict';
// Synthetic native summaries/fields; never opens or changes game packages.
const assert = require('node:assert/strict'), fs = require('node:fs'), path = require('node:path');
let harness = fs.readFileSync(path.join(__dirname, 'ui_smoke.js'), 'utf8');
harness = harness.slice(0, harness.indexOf('async function testProfileCreation()')).replace('state, normalizeSnapshot, render,',
  'workshopFieldEditor, renderWorkshopSummary, renderWorkshopDetail, workshopRequiredAssets, workshopAssets, nextWorkshopChoice, acceptWorkshopSession, queueWorkshopEdit, flushWorkshopEdits, clearWorkshopDraftInputs, workshopMergeChoices, workshopPending, resolveWorkshopMerge, saveWorkshopProject, state, normalizeSnapshot, render,');
let checks = 0;
async function test(platform) {
  const { ui, node, context, fixture, nativeRequests, actionFixtures } = new Function('require', '__dirname', harness +
    '\nreturn {ui,node,context,fixture,nativeRequests,actionFixtures};')(require, __dirname);
  const fields = [
    { id: 'wide-int', name: 'Exact wide integer', type: 'UInt64Property', editor: 'text', value: '18446744073709551615', maxLength: 20, hint: 'Exact decimal integer' },
    { id: 'enum', name: 'Existing enum', type: 'ByteProperty', editor: 'enum', value: 'Mode::First', choices: [{ value: 'Mode::First', label: 'First <value>' }, { value: 'Mode::Second', label: 'Second' }] },
    { id: 'uint', name: 'Unsigned count', type: 'UInt32Property', editor: 'integer', value: 4294967295, min: 0, max: 4294967295 },
    { id: 'double', name: 'Double scalar', type: 'DoubleProperty', editor: 'number', value: 1.5, min: 0, max: 100 },
    { id: 'unknown', name: 'Unknown string', type: 'StrProperty', value: 'Do not infer an editor' }
  ];
  const assets = [
    { id: 'main', family: 'shared', path: 'BendGame/Content/Test.uasset', sourceIds: ['one', 'two'], sourceNames: ['One <PAK>', 'Two'], bytes: 512, conflict: true, needsChoice: true },
    { id: 'companion', family: 'shared', path: 'BendGame/Content/Test.uexp', sourceIds: ['one', 'two'], bytes: 16, conflict: true, needsChoice: true },
    { id: 'same', family: 'identical', path: 'BendGame/Content/Same.uasset', sourceIds: ['one', 'two'], bytes: 24, conflict: false },
    { id: 'single', family: 'single', path: 'BendGame/Content/Single.uasset', sourceIds: ['one'], bytes: 32, conflict: false }
  ];
  const session = (revision = 0, changes = {}) => ({ id: 'guided-native', revision, dirty: revision > 0, projectDirty: true,
    sourceCount: 2, assetCount: 4, unresolvedCount: 1, canBuild: false, canUndo: true, assets, sources: [{ id: 'one', name: 'One' }, { id: 'two', name: 'Two' }], warnings: [],
    analysis: { sharedFamilies: 2, identicalFamilies: 1, conflictingFamilies: 1, readOnlyFamilies: 0, uncheckedFamilies: 3 }, ...changes });
  Object.assign(ui.state, { preview: false, busy: false, loading: false, view: 'workshop', workshop: null, workshopLoading: false, workshopWorking: false,
    workshopSearch: '', workshopAsset: null, workshopSelection: '', nativeDropActivity: null, profileSession: { active: false },
    snapshot: ui.normalizeSnapshot({ ...fixture, platform, edition: 'local', accountContext: 'guided-' + platform, settings: { ...fixture.settings, gamePath: '/Synthetic/DaysGone' } }) });
  node('#modal').close(); ui.clearWorkshopDraftInputs(); ui.acceptWorkshopSession(session());
  assert.equal(ui.state.workshopChoicesOnly, true);
  assert.equal(ui.workshopRequiredAssets().length, 1, 'A complete native asset family is one required source decision');
  assert.deepEqual(Array.from(ui.workshopAssets(), (asset) => asset.id), ['main']);
  const summary = ui.renderWorkshopSummary();
  assert(summary.includes('Choose mods') && summary.includes('Review summary') && summary.includes('Required choices') && summary.includes('Check export'));
  assert(summary.includes('Identical families') && summary.includes('Different families'));
  assert(summary.includes('initial package check did not inspect property layouts for 3 asset families'));
  assert(summary.includes('does not guarantee gameplay compatibility'));
  ui.state.workshop.analysis = undefined;
  assert(ui.renderWorkshopSummary().includes('Not reported') && ui.renderWorkshopSummary().includes('Checked on selection'));
  ui.state.workshop = session();
  actionFixtures.set('workshopAsset', { session: 'guided-native', revision: 0, asset: { ...assets[0], editable: false, fields: [] } });
  assert(await ui.nextWorkshopChoice());
  assert.equal(nativeRequests.find((request) => request.action === 'workshopAsset').asset, 'main');
  checks += 10;

  ui.state.workshopAsset = { ...assets[0], selectedSource: 'one', editable: true, fields };
  const markup = ui.renderWorkshopDetail();
  assert(markup.includes('Exact wide integer') && markup.includes('18446744073709551615'));
  assert(markup.includes('First &lt;value&gt;') && markup.includes('<select'));
  assert(!markup.includes('data-workshop-field="unknown"'), 'Unknown string layouts do not gain a generic text editor');
  assert(markup.includes('Other versions excluded') && markup.includes('Their edits are not included'));
  assert.equal(ui.workshopFieldEditor({ ...fields[0], maxLength: undefined }), null);
  assert.equal(ui.workshopFieldEditor({ ...fields[1], choices: [{ value: {}, label: 'Object' }] }), null);
  assert(ui.queueWorkshopEdit('wide-int', { value: '18446744073709551614' }));
  actionFixtures.set('workshopEdit', { session: session(1), asset: { ...assets[0], selectedSource: 'one', editable: true, fields } });
  assert(await ui.flushWorkshopEdits());
  const exact = nativeRequests.filter((request) => request.action === 'workshopEdit').at(-1);
  assert.equal(exact.value, '18446744073709551614'); assert.equal(typeof exact.value, 'string');
  assert(ui.queueWorkshopEdit('enum', { value: '1' }));
  actionFixtures.set('workshopEdit', { session: session(2), asset: { ...assets[0], selectedSource: 'one', editable: true, fields } });
  assert(await ui.flushWorkshopEdits());
  assert.equal(nativeRequests.filter((request) => request.action === 'workshopEdit').at(-1).value, 'Mode::Second');
  assert.equal(ui.queueWorkshopEdit('enum', { value: '99' }), false); ui.clearWorkshopDraftInputs();
  assert.equal(ui.queueWorkshopEdit('uint', { value: '-1' }), false); ui.clearWorkshopDraftInputs();
  assert.equal(ui.queueWorkshopEdit('double', { value: '101' }), false); ui.clearWorkshopDraftInputs();
  assert.equal(ui.queueWorkshopEdit('wide-int', { value: '1'.repeat(21) }), false); ui.clearWorkshopDraftInputs();
  checks += 17;

  ui.state.workshopMerge = { asset: 'main', supported: true, conflicts: [{ property: 'choice-one' }, { property: 'choice-two' }] };
  ui.workshopMergeChoices.set('choice-one', 'left');
  assert(ui.workshopPending(), 'Unapplied merge selections count as pending input');
  assert.equal(await ui.saveWorkshopProject(), false, 'A project cannot silently omit a selected but unapplied value');
  const queryAll = context.document.querySelectorAll;
  context.document.querySelectorAll = (selector) => selector === '[data-workshop-merge]' ? [{ dataset: { workshopMerge: 'choice-one' }, checked: true, value: 'left' }] : queryAll(selector);
  actionFixtures.set('workshopResolveMerge', { session: session(3), asset: { ...assets[0], selectedSource: 'one', editable: true, fields,
    merge: { supported: true, conflicts: [{ property: 'choice-two' }] } } });
  assert(await ui.resolveWorkshopMerge(), 'A selected subset can be validated without deciding every conflict');
  const partial = nativeRequests.filter((request) => request.action === 'workshopResolveMerge').at(-1);
  assert.deepEqual(Object.keys(partial.choices), ['choice-one']);
  assert.equal(ui.state.workshopMerge.conflicts.length, 1, 'Remaining native conflicts stay visible');
  assert.equal(ui.workshopPending(), false, 'Applied decisions no longer block saving the unfinished project');
  context.document.querySelectorAll = queryAll;
  checks += 6;
}
(async () => { for (const platform of ['windows', 'linux']) await test(platform); console.log(`Guided Workshop and typed fields passed (${checks} Windows/Linux checks).`); })()
  .catch((error) => { console.error(error); process.exitCode = 1; });
