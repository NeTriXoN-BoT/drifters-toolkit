'use strict';

// Native replies are synthetic. These tests never open or write a real PAK.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
let harness = fs.readFileSync(path.join(__dirname, 'ui_smoke.js'), 'utf8');
harness = harness.slice(0, harness.indexOf('async function testProfileCreation()')).replace('state, normalizeSnapshot, render,',
  'renderWorkshop, renderWorkshopDetail, loadWorkshopStatus, workshopSession, selectWorkshopAsset, queueWorkshopEdit, flushWorkshopEdits, clearWorkshopDraftInputs, workshopPending, browseWorkshop, startWorkshopAsset, chooseWorkshopSource, compareWorkshopBaseline, resolveWorkshopMerge, reviewWorkshopBuild, reviewWorkshopChanges, resetWorkshopValue, closeWorkshop, workshopMutation, state, normalizeSnapshot, render,');
let checks = 0;

async function test(platform) {
  const { ui, node, context, fixture, nativeRequests, actionFixtures, actionErrors, timers, reply } = new Function('require', '__dirname', harness +
    '\nreturn {ui,node,context,fixture,nativeRequests,actionFixtures,actionErrors,timers,reply:(data)=>nativeMessageListener({data})};')(require, __dirname);
  const assetId = 'asset-one', sessionId = 'native-session', property = 'property-native-id';
  const session = (revision = 0, changes = {}) => ({ id: sessionId, revision, dirty: revision > 0, sourceCount: 2, assetCount: 2,
    canBuild: true, canUndo: revision > 0, unresolvedCount: 0, sources: [{ id: 'left', name: 'First mod' }, { id: 'right', name: 'Second mod' }],
    assets: [{ id: assetId, path: 'BendGame/Content/Weapons/Silencer.uasset', bytes: 512,
      sourceIds: ['left', 'right'], sourceNames: ['First mod', 'Second mod'], selectedSource: 'left', conflict: false, edited: revision > 0 },
      { id: 'unknown-asset', path: 'BendGame/Content/Unrecognized.uasset', bytes: 64, sourceIds: ['left'], sourceNames: ['First mod'] }],
    warnings: [], ...changes });
  const asset = (value = 10, changes = {}) => ({ ...session().assets[0], editable: true,
    fields: [{ id: property, name: 'Durability <amount>', type: 'IntProperty', value }], ...changes });
  const snapshot = { ...fixture, platform, edition: 'local', accountContext: 'workshop-' + platform,
    gameRunning: false, settings: { ...fixture.settings, gamePath: '/Synthetic/DaysGone' } };
  Object.assign(ui.state, { preview: false, loading: false, busy: false, view: 'workshop', workshop: null,
    workshopLoaded: false, workshopAsset: null, workshopSelection: '', workshopSearch: '', workshopError: '',
    workshopWorking: false, workshopLoading: false, snapshot: ui.normalizeSnapshot(snapshot),
    profileSession: { active: false }, nativeDropActivity: null, libraryMetaLoaded: true });
  actionFixtures.set('snapshot', snapshot);
  actionFixtures.set('workshopStatus', { available: true, session: session() });
  actionFixtures.set('workshopAsset', { session: sessionId, revision: 0, asset: asset() });
  node('#modal').close();

  assert.equal(ui.workshopSession({ ...session(), revision: -1 }), null);
  assert.equal(ui.workshopSession({ ...session(), assets: [{ path: '/untrusted' }] }), null);
  assert(await ui.loadWorkshopStatus());
  assert.equal(ui.state.workshop.id, sessionId);
  let markup = ui.renderWorkshop();
  assert(markup.includes('Open PAKs…') && markup.includes('Start from exported asset…'));
  assert(markup.includes('not the running game') && markup.includes('Original packages stay in place'));
  ui.state.helpSection = 'modding'; assert(ui.renderHelp().includes('data-action="workshop-enter"'));
  checks += 6;

  assert(await ui.selectWorkshopAsset(assetId));
  const selectedRequest = nativeRequests.find((request) => request.action === 'workshopAsset');
  assert.equal(selectedRequest.session, sessionId); assert.equal(selectedRequest.asset, assetId);
  assert(!('path' in selectedRequest) && !('filename' in selectedRequest));
  markup = ui.renderWorkshopDetail();
  assert(markup.includes('Durability &lt;amount&gt;') && markup.includes('data-workshop-field'));
  assert(markup.includes('does not combine the other version'));
  ui.state.workshopAsset = asset(10, { editable: false, reason: '<b>Unsupported layout</b>', fields: [] });
  markup = ui.renderWorkshopDetail();
  assert(markup.includes('&lt;b&gt;Unsupported layout') && !markup.includes('data-workshop-field'));
  ui.state.workshopAsset = asset();
  checks += 5;

  actionFixtures.set('workshopEdit', { session: session(1), asset: asset(17) });
  assert(ui.queueWorkshopEdit(property, { value: '17' }));
  assert(ui.workshopPending());
  assert([...timers.values()].some((timer) => timer.delay === 350));
  assert(await ui.flushWorkshopEdits());
  let edit = nativeRequests.filter((request) => request.action === 'workshopEdit').at(-1);
  assert.equal(edit.value, 17); assert.equal(typeof edit.value, 'number');
  assert.equal(edit.revision, 0); assert.equal(edit.property, property);
  assert.equal(ui.state.workshop.revision, 1); assert.equal(ui.workshopPending(), false);
  const editsBefore = nativeRequests.filter((request) => request.action === 'workshopEdit').length;
  assert.equal(ui.queueWorkshopEdit(property, { value: '1e999' }), false);
  assert.equal(await ui.flushWorkshopEdits(), false);
  assert.equal(nativeRequests.filter((request) => request.action === 'workshopEdit').length, editsBefore);
  assert.equal(await ui.reviewWorkshopBuild(), false, 'An unapplied invalid value blocks building');
  assert(ui.renderWorkshop().includes('Revert pending values'));
  await ui.handleAction('workshop-revert-inputs'); assert.equal(ui.workshopPending(), false);
  checks += 10;

  const bridge = context.window.chrome.webview, post = bridge.postMessage;
  let held, heldAction;
  bridge.postMessage = (request) => {
    if (request.action === heldAction && !held) { held = request; nativeRequests.push(request); }
    else post.call(bridge, request);
  };
  const waitHeld = async () => { for (let i = 0; i < 50 && !held; ++i) await new Promise(setImmediate); assert(held); };
  heldAction = 'workshopEdit';
  ui.queueWorkshopEdit(property, { value: '20' });
  let work = ui.flushWorkshopEdits(); await waitHeld();
  ui.queueWorkshopEdit(property, { value: '25' });
  reply({ requestId: held.requestId, ok: true, data: { session: session(2), asset: asset(20) } });
  assert(await work); assert(ui.workshopPending(), 'Typing during native validation preserves the newer value');
  heldAction = null;
  actionFixtures.set('workshopEdit', { session: session(3), asset: asset(25) });
  assert(await ui.flushWorkshopEdits());
  edit = nativeRequests.filter((request) => request.action === 'workshopEdit').at(-1);
  assert.equal(edit.value, 25); assert.equal(edit.revision, 2);
  assert.equal(ui.state.workshopAsset.fields[0].value, 25);
  checks += 5;

  actionFixtures.set('workshopChooseSource', { session: session(4), asset: asset(10, { selectedSource: 'right' }) });
  const beforeChoice = nativeRequests.filter((request) => request.action === 'workshopChooseSource').length;
  assert(await ui.chooseWorkshopSource('right'));
  assert(node('#modal-content').innerHTML.includes('entire asset family'));
  assert.equal(nativeRequests.filter((request) => request.action === 'workshopChooseSource').length, beforeChoice);
  await node('#confirm-action').onclick();
  const choice = nativeRequests.filter((request) => request.action === 'workshopChooseSource').at(-1);
  assert.equal(choice.source, 'right'); assert.equal(choice.asset, assetId); assert.equal(choice.revision, 3);
  checks += 4;

  actionFixtures.set('workshopMergeBaselineBrowse', { session: session(5, { canBuild: false, unresolvedCount: 1 }), asset: asset(10),
    merge: { supported: true, conflicts: [{ property, name: 'Durability', type: 'IntProperty', base: 10, left: 20, right: 30 }] } });
  assert(await ui.compareWorkshopBaseline());
  assert(ui.renderWorkshopDetail().includes('Choose the values to keep'));
  assert.equal(await ui.resolveWorkshopMerge(), false, 'Every conflicting property needs an explicit value');
  const oldQuery = context.document.querySelectorAll;
  context.document.querySelectorAll = (selector) => selector === '[data-workshop-merge]'
    ? [{ dataset: { workshopMerge: property }, checked: true, value: 'left' }] : oldQuery(selector);
  actionFixtures.set('workshopResolveMerge', { session: session(6), asset: asset(20) });
  assert(await ui.resolveWorkshopMerge());
  const merged = nativeRequests.filter((request) => request.action === 'workshopResolveMerge').at(-1);
  assert.equal(merged.choices[property], 'left'); assert(!('basePath' in merged));
  context.document.querySelectorAll = oldQuery;
  checks += 4;

  actionFixtures.set('workshopBuildReview', { token: 'native-build-token', session: sessionId, revision: 6,
    assetCount: 2, bytes: 1024, changedProperties: 1, sourceNames: ['First mod', '<source>'], warnings: ['Native <warning>'] });
  actionFixtures.set('workshopExportBrowse', { exported: true, name: 'NewMod_P.pak', bytes: 1024, sha256: 'a'.repeat(64),
    session: session(6, { dirty: false, exportedRevision: 6 }) });
  assert(await ui.reviewWorkshopBuild());
  markup = node('#modal-content').innerHTML;
  assert(markup.includes('Export this PAK?') && markup.includes('&lt;source&gt;') && markup.includes('Native &lt;warning&gt;'));
  assert(!nativeRequests.some((request) => request.action === 'workshopExportBrowse'));
  assert(await node('#workshop-export').onclick());
  const exported = nativeRequests.find((request) => request.action === 'workshopExportBrowse');
  assert.equal(exported.token, 'native-build-token'); assert(!('path' in exported) && !('destination' in exported));
  assert(!node('#modal').open);
  assert.equal(ui.state.workshop.dirty, false, 'A successful export adopts the native clean revision');
  assert.equal(ui.state.workshop.exportedRevision, 6);
  checks += 7;

  assert(await ui.reviewWorkshopBuild());
  actionErrors.set('workshopExportBrowse', 'The selected output is an original source file.');
  assert.equal(await node('#workshop-export').onclick(), false);
  assert(node('#modal').open && node('#workshop-export-error').textContent.includes('Close this window and review'));
  assert(node('#workshop-export').disabled, 'A failed one-use export requires a new review');
  actionErrors.delete('workshopExportBrowse'); node('#modal').close(); ui.handleModalClosed();
  checks += 3;

  ui.state.workshop = session(6);
  actionFixtures.set('workshopExportBrowse', { exported: true, name: 'LegacyResponse_P.pak' });
  assert(await ui.reviewWorkshopBuild()); assert(await node('#workshop-export').onclick());
  assert.equal(ui.state.workshop.dirty, true, 'An older response without a native session cannot invent a clean draft');
  checks += 2;

  assert(ui.startWorkshopAsset());
  assert(node('#modal-content').innerHTML.includes('Save project…'));
  assert(await node('#workshop-project-discard').onclick());
  checks += 2;
  node('#workshop-new-path').value = 'C:/Users/private/secret.uasset';
  assert.equal(await node('#workshop-new-choose').onclick(), false);
  assert(!nativeRequests.some((request) => request.action === 'workshopNewAssetBrowse'));
  node('#workshop-new-path').value = 'BendGame/Content/Weapons/Test.uasset';
  actionFixtures.set('workshopNewAssetBrowse', { session: { ...session(0), id: 'new-asset-draft' } });
  assert(await node('#workshop-new-choose').onclick());
  const newAsset = nativeRequests.find((request) => request.action === 'workshopNewAssetBrowse');
  assert.equal(newAsset.virtualPath, 'BendGame/Content/Weapons/Test.uasset'); assert(!('path' in newAsset));
  checks += 4;

  // An old inspection must not replace a later selection or installation.
  ui.state.workshop = session(0); ui.state.workshopAsset = null; ui.state.workshopSelection = '';
  heldAction = 'workshopAsset'; held = null;
  work = ui.selectWorkshopAsset(assetId); await waitHeld();
  ui.state.snapshot.accountContext = 'another-installation';
  reply({ requestId: held.requestId, ok: true, data: { session: sessionId, revision: 0, asset: asset(999) } });
  assert.equal(await work, false); assert.equal(ui.state.workshopAsset, null);
  ui.state.workshopLoading = false; ui.state.snapshot.accountContext = snapshot.accountContext;
  heldAction = null;
  checks += 2;

  ui.state.workshop = session(0); ui.state.workshopAsset = asset(); ui.state.workshopSelection = assetId;
  actionFixtures.set('workshopUndo', { session: { ...session(9), id: 'wrong-native-session' } });
  assert.equal(await ui.workshopMutation('workshopUndo'), null);
  assert.equal(ui.state.workshop.id, sessionId, 'A different native session cannot replace the current draft through an edit');
  assert(ui.state.workshopError.includes('different or older session'));
  checks += 3;

  ui.state.workshopAsset = asset(10, { fields: [
    { id: 'bool-field', name: 'Enabled', type: 'BoolProperty', value: true },
    { id: 'float-field', name: 'Multiplier', type: 'FloatProperty', value: 1.25 }
  ] });
  actionFixtures.set('workshopEdit', { session: session(1), asset: asset(10, { fields: [
    { id: 'bool-field', name: 'Enabled', type: 'BoolProperty', value: false },
    { id: 'float-field', name: 'Multiplier', type: 'FloatProperty', value: 1.25 }
  ] }) });
  assert(ui.queueWorkshopEdit('bool-field', { checked: false }));
  assert(await ui.flushWorkshopEdits());
  assert.equal(nativeRequests.filter((request) => request.action === 'workshopEdit').at(-1).value, false);
  actionFixtures.set('workshopEdit', { session: session(2), asset: asset(10, { fields: [
    { id: 'bool-field', name: 'Enabled', type: 'BoolProperty', value: false },
    { id: 'float-field', name: 'Multiplier', type: 'FloatProperty', value: 2.75 }
  ] }) });
  assert(ui.queueWorkshopEdit('float-field', { value: '2.75' })); assert(await ui.flushWorkshopEdits());
  assert.equal(nativeRequests.filter((request) => request.action === 'workshopEdit').at(-1).value, 2.75);
  checks += 6;

  ui.state.workshop = session(0); ui.state.workshopAsset = asset(); ui.state.workshopSelection = assetId;
  assert.equal(ui.queueWorkshopEdit(property, { value: '2147483648' }), false);
  assert.equal(await ui.selectWorkshopAsset('unknown-asset'), false, 'Pending invalid values cannot silently change selection');
  await ui.handleAction('workshop-revert-inputs');
  checks += 2;

  heldAction = 'workshopBuildReview'; held = null;
  work = ui.reviewWorkshopBuild(); await waitHeld();
  ui.showChangelog('1.0.7');
  const newerDialog = node('#modal-content').innerHTML;
  reply({ requestId: held.requestId, ok: true, data: { token: 'late-token', session: sessionId, revision: 0 } });
  assert.equal(await work, false); assert.equal(node('#modal-content').innerHTML, newerDialog);
  node('#modal').close(); ui.handleModalClosed();
  checks += 2;

  held = null; work = ui.reviewWorkshopBuild(); await waitHeld();
  ui.state.view = 'mods';
  reply({ requestId: held.requestId, ok: true, data: { token: 'late-token', session: sessionId, revision: 0 } });
  assert.equal(await work, false); assert(!node('#modal').open, 'An old build cannot pull the user back from another page');
  heldAction = null; ui.state.view = 'workshop';
  checks += 2;

  ui.state.workshopAsset = asset(17, { fields: Array.from({ length: 300 }, (_, index) => ({ id: 'field-' + index,
    name: 'Field ' + index, label: 'Object/Field ' + index, type: 'IntProperty', value: index })) });
  markup = ui.renderWorkshopDetail();
  assert(markup.includes('Showing the first 256'));
  assert.equal((markup.match(/data-workshop-field=/g) || []).length, 256, 'Large property lists keep the DOM bounded');
  ui.state.workshopPropertySearch = 'Object/Field 299';
  markup = ui.renderWorkshopDetail();
  assert.equal((markup.match(/data-workshop-field=/g) || []).length, 1);
  assert(markup.includes('field-299'), 'Search covers all native fields before applying the display bound');
  ui.state.workshopPropertySearch = '';
  checks += 4;

  ui.state.workshopAsset = asset(17, { fields: [{ id: property, name: 'Durability', type: 'IntProperty',
    value: 17, originalValue: 10, changed: true }] });
  actionFixtures.set('workshopResetValue', { session: session(1), asset: asset(10, { fields: [{ id: property,
    name: 'Durability', type: 'IntProperty', value: 10, originalValue: 10, changed: false }] }) });
  assert(await ui.resetWorkshopValue(property));
  const reset = nativeRequests.find((request) => request.action === 'workshopResetValue');
  assert.equal(reset.property, property); assert(!('value' in reset) && !('path' in reset));
  assert.equal(await ui.resetWorkshopValue(property), false, 'Only a native changed verdict enables Reset');
  actionFixtures.set('workshopChanges', { session: sessionId, revision: 1, changeCount: 1, truncated: false,
    changes: [{ asset: assetId, path: '<asset>', property, name: '<property>', type: 'IntProperty', before: 10, after: 17 }],
    families: [{ path: '<family>', keptSource: '<kept>', discardedSources: ['<discarded>'] }] });
  assert(await ui.reviewWorkshopChanges());
  markup = node('#modal-content').innerHTML;
  assert(markup.includes('Opened source') && markup.includes('Draft'));
  assert(markup.includes('&lt;property&gt;') && markup.includes('&lt;discarded&gt;') && !markup.includes('<property>'));
  const reportRequest = nativeRequests.find((request) => request.action === 'workshopChanges');
  assert(!('revision' in reportRequest) && !('path' in reportRequest));
  node('#modal').close(); ui.handleModalClosed();
  checks += 7;

  heldAction = 'workshopAsset'; held = null;
  work = ui.selectWorkshopAsset(assetId); await waitHeld();
  assert.equal(ui.state.workshopLoading, true);
  await ui.acceptSnapshot({ ...snapshot, accountContext: snapshot.accountContext,
    settings: { ...snapshot.settings, gamePath: '/Another/Installation' } });
  assert.equal(ui.state.workshopLoading, false); assert.equal(ui.state.workshopWorking, false);
  assert.equal(ui.state.workshopLoaded, false, 'A different game path invalidates reads even when the native account label is unchanged');
  reply({ requestId: held.requestId, ok: true, data: { session: sessionId, revision: 1, asset: asset(777) } });
  assert.equal(await work, false); assert.equal(ui.state.workshopAsset, null);
  heldAction = null;
  checks += 5;

  assert(!nativeRequests.some((request) => ['import', 'enable', 'disable', 'remove', 'confirmReviewedImport'].includes(request.action)),
    'The workshop never installs, enables or replaces a game mod by itself');
  ui.state.preview = true; assert.equal(await ui.browseWorkshop(), false);
  checks += 2;
}

(async () => { for (const platform of ['windows', 'linux']) await test(platform);
  console.log(`Workshop UI passed (${checks} Windows/Linux checks).`);
})().catch((error) => { console.error(error); process.exitCode = 1; });
