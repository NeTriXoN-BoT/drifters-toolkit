'use strict';
// Synthetic native replies only; no PAKs, projects or game files are touched.
const assert = require('node:assert/strict'), fs = require('node:fs'), path = require('node:path');
let harness = fs.readFileSync(path.join(__dirname, 'ui_smoke.js'), 'utf8');
harness = harness.slice(0, harness.indexOf('async function testProfileCreation()')).replace('state, normalizeSnapshot, render,',
  'newWorkshopPackage, openWorkshopFileInput, changeWorkshopFile, extractWorkshopFile, validWorkshopVirtualPath, renderWorkshop, renderWorkshopDetail, renderWorkshopMerge, workshopRequiredAssets, workshopMergeChoices, resolveWorkshopMerge, compareWorkshopBaseline, reviewWorkshopChanges, acceptWorkshopSession, clearWorkshopDraftInputs, saveWorkshopProject, reviewWorkshopReplacement, startWorkshopAsset, reviewWorkshopBuild, deleteWorkshopProject, showWorkshopProjects, modal, handleModalClosed, state, normalizeSnapshot, render,');
let checks = 0;
async function test(platform) {
  const { ui, node, context, fixture, nativeRequests, actionFixtures, actionErrors, reply } = new Function('require', '__dirname', harness +
    '\nreturn {ui,node,context,fixture,nativeRequests,actionFixtures,actionErrors,reply:(data)=>nativeMessageListener({data})};')(require, __dirname);
  const snapshot = { ...fixture, platform, edition: 'local', accountContext: 'file-tools-' + platform, gameRunning: false,
    settings: { ...fixture.settings, gamePath: '/Synthetic/DaysGone' } };
  const asset = (extra = {}) => ({ id: 'file-one', path: 'BendGame/Content/Example.uasset', originalPath: 'BendGame/Content/Example.uasset',
    bytes: 1024, selectedSource: 'source-1', sourceIds: ['source-1'], sourceNames: ['Opened.pak'], fields: [], editable: false, ...extra });
  const session = (revision = 0, extra = {}) => ({ id: 'file-draft', revision, sourceCount: 1, assetCount: 1, canBuild: true,
    canUndo: revision > 0, dirty: revision > 0, projectDirty: false, sources: [{ id: 'source-1', name: 'Opened.pak' }],
    assets: [asset()], warnings: [], ...extra });
  const reset = (current = session(), selected = asset()) => {
    Object.assign(ui.state, { preview: false, loading: false, busy: false, view: 'workshop', workshop: current,
      workshopLoaded: true, workshopAsset: selected, workshopSelection: selected?.id || '', workshopSearch: '',
      workshopPropertySearch: '', workshopError: '', workshopWorking: false, workshopLoading: false, workshopChoicesOnly: false,
      profileSession: { active: false }, nativeDropActivity: null, libraryMetaLoaded: true, snapshot: ui.normalizeSnapshot(snapshot) });
    ui.clearWorkshopDraftInputs(); node('#modal').close(); ui.handleModalClosed();
  };
  actionFixtures.set('snapshot', snapshot);
  reset(null, null);
  actionFixtures.set('workshopNew', { session: session(0, { sourceCount: 0, assetCount: 0, assets: [], sources: [], canBuild: false }) });
  assert(await ui.newWorkshopPackage());
  assert(ui.renderWorkshop().includes('Included files: 0') && ui.renderWorkshop().includes('0 source packages'));
  assert(ui.renderWorkshop().includes('Add file…')); checks += 3;
  for (const value of ['../outside', '/absolute/file.bin', 'BendGame//file.bin', 'C:/secret.bin', 'BendGame/a\0.bin']) assert(!ui.validWorkshopVirtualPath(value));
  assert(ui.validWorkshopVirtualPath('BendGame/Content/My exported file.uasset')); checks += 6;

  assert(ui.openWorkshopFileInput('add'));
  const staleAdd = node('#workshop-file-confirm').onclick;
  node('#workshop-file-path').value = 'BendGame/Content/Added.bin';
  node('#modal').close();
  const closedCount = nativeRequests.length;
  assert.equal(await staleAdd(), false); assert.equal(nativeRequests.length, closedCount);
  ui.modal('Replacement dialog', '<p>Current content</p>', '<button data-modal-close>Close</button>');
  const before = nativeRequests.length;
  assert.equal(await staleAdd(), false); assert.equal(nativeRequests.length, before); checks += 5;

  reset();
  actionFixtures.set('workshopAddFileBrowse', { session: session(1, { assetCount: 2, assets: [asset(), asset({ id: 'added', path: 'BendGame/Content/Added.bin', added: true, selectedSource: 'added', sourceIds: [] })] }),
    asset: asset({ id: 'added', path: 'BendGame/Content/Added.bin', added: true, selectedSource: 'added', sourceIds: [] }) });
  assert(ui.openWorkshopFileInput('add')); node('#workshop-file-path').value = 'BendGame/Content/Added.bin';
  const add = node('#workshop-file-confirm').onclick;
  const pending = add(); assert.equal(await add(), false); assert(await pending);
  assert.equal(ui.state.workshopSelection, 'added');
  const added = nativeRequests.filter((request) => request.action === 'workshopAddFileBrowse');
  assert.equal(added.length, 1); assert.equal(added[0].virtualPath, 'BendGame/Content/Added.bin');
  assert(!('path' in added[0]) && !('destination' in added[0])); checks += 6;

  reset();
  actionFixtures.set('workshopRenameFile', { session: session(1), asset: asset({ path: 'BendGame/Content/Renamed.uasset' }) });
  assert(ui.openWorkshopFileInput('rename')); node('#workshop-file-path').value = '../outside.uasset';
  assert.equal(await node('#workshop-file-confirm').onclick(), false);
  assert(node('#workshop-file-error').textContent.includes('virtual file path'));
  node('#workshop-file-path').value = 'BendGame/Content/Renamed.uasset';
  assert(await node('#workshop-file-confirm').onclick());
  assert.equal(ui.state.workshopAsset.path, 'BendGame/Content/Renamed.uasset'); checks += 5;

  reset();
  actionFixtures.set('workshopRemoveFile', { session: session(1, { assetCount: 0, assets: [asset({ removed: true })] }), asset: asset({ removed: true }) });
  assert(ui.changeWorkshopFile('remove')); assert(node('#modal-content').innerHTML.includes('companion data'));
  await node('#confirm-action').onclick();
  assert(ui.renderWorkshopDetail().includes('Restore file'));
  assert(ui.renderWorkshop().includes('Included files: 0'));
  assert.equal(ui.workshopRequiredAssets().length, 0); checks += 5;

  reset();
  actionFixtures.set('workshopExtractReview', { token: 'extract-native', session: 'file-draft', revision: 0, asset: 'file-one',
    displayName: 'Example.uasset', bytes: 1024, sha256: 'a'.repeat(64) });
  actionFixtures.set('workshopExtractFileBrowse', { exported: true });
  assert(await ui.extractWorkshopFile());
  const staleExport = node('#workshop-file-export-confirm').onclick;
  node('#modal').close();
  const closedExports = nativeRequests.length;
  assert.equal(await staleExport(), false); assert.equal(nativeRequests.length, closedExports);
  ui.modal('Current dialog', '<p>Keep this open</p>');
  const count = nativeRequests.length; assert.equal(await staleExport(), false); assert.equal(nativeRequests.length, count);
  assert(await ui.extractWorkshopFile()); assert(await node('#workshop-file-export-confirm').onclick());
  const exported = nativeRequests.filter((request) => request.action === 'workshopExtractFileBrowse').at(-1);
  assert.equal(exported.token, 'extract-native'); assert.equal(exported.displayName, 'Example.uasset');
  assert(!('path' in exported) && !('assetPath' in exported)); checks += 10;

  reset();
  const sources = Array.from({ length: 24 }, (_, index) => ({ id: 'source-' + (index + 1), name: `Mod ${index + 1}.pak` }));
  ui.state.workshop.sources = sources; ui.state.workshop.sourceCount = 24;
  ui.state.workshopAsset = asset({ sourceIds: sources.map((source) => source.id), sourceNames: sources.map((source) => source.name) });
  actionFixtures.set('workshopMergeBaselineBrowse', { session: session(1, { sourceCount: 24, sources, canBuild: false }),
    asset: ui.state.workshopAsset, merge: { supported: true, conflicts: [{ property: 'count', name: 'Count', base: 7,
      options: sources.map((source, index) => ({ source: source.id, value: index })) }] } });
  assert(ui.renderWorkshopDetail().includes('all 24 opened versions'));
  assert(await ui.compareWorkshopBaseline());
  assert(ui.renderWorkshopMerge().includes('<select') && ui.renderWorkshopMerge().includes('Mod 24.pak'));
  const oldQuery = context.document.querySelectorAll;
  context.document.querySelectorAll = (selector) => selector === '[data-workshop-merge]'
    ? [{ tagName: 'SELECT', dataset: { workshopMerge: 'count' }, value: 'source-24' }] : oldQuery(selector);
  actionFixtures.set('workshopResolveMerge', { session: session(2, { sourceCount: 24, sources }), asset: asset() });
  assert(await ui.resolveWorkshopMerge());
  assert.equal(nativeRequests.filter((request) => request.action === 'workshopResolveMerge').at(-1).choices.count, 'source-24');
  context.document.querySelectorAll = oldQuery; checks += 5;

  const field = { property: 'many', name: 'Count', base: 7, options: sources.map((source, index) => ({ source: source.id, value: index })) };
  ui.state.workshopMerge = { supported: true, asset: ui.state.workshopSelection,
    conflicts: Array.from({ length: 120 }, (_, index) => ({ ...field, property: 'field-' + index })) };
  let markup = ui.renderWorkshopMerge();
  assert((markup.match(/<option /g) || []).length <= 2048 && markup.includes('continue with the remaining ones'));
  const numerous = Array.from({ length: 2100 }, (_, index) => ({ source: 'source-' + (index + 1), value: index }));
  ui.state.workshopMerge = { supported: true, asset: ui.state.workshopSelection, conflicts: [{ ...field, options: numerous }] };
  markup = ui.renderWorkshopMerge();
  assert(markup.includes('value="source-2100"'), 'Every source remains selectable for an individual property; the render budget is not a source-count cap');
  ui.state.workshop.assets = [asset({ removed: true, needsChoice: true, conflict: true })];
  assert.equal(ui.workshopRequiredAssets().length, 0, 'A removed conflict must not block the remaining package'); checks += 3;

  reset();
  actionFixtures.set('workshopChanges', { session: 'file-draft', revision: 0, changes: [], families: [], changeCount: 0,
    files: [{ path: 'BendGame/Content/<new>.bin', originalPath: 'BendGame/Content/Old.bin', added: true, renamed: true }] });
  assert(await ui.reviewWorkshopChanges());
  assert(node('#modal-content').innerHTML.includes('Package files') && node('#modal-content').innerHTML.includes('&lt;new&gt;'));
  assert(!nativeRequests.some((request) => ['import', 'enable', 'disable', 'remove', 'confirmReviewedImport'].includes(request.action)));
  checks += 3;

  // A native dialog's close event is asynchronous. Already-queued clicks must
  // refuse immediately even before that event advances the modal generation.
  reset(); await ui.saveWorkshopProject();
  let callback = node('#workshop-project-save-confirm').onclick;
  node('#modal').close(); let initial = nativeRequests.length;
  assert.equal(await callback(), false); assert.equal(nativeRequests.length, initial); checks += 2;
  reset(session(0, { projectDirty: true })); let proceeded = 0;
  ui.reviewWorkshopReplacement(() => { ++proceeded; });
  callback = node('#workshop-project-discard').onclick; node('#modal').close();
  assert.equal(await callback(), false); assert.equal(proceeded, 0); checks += 2;
  reset(); ui.startWorkshopAsset(true); callback = node('#workshop-new-choose').onclick;
  node('#workshop-new-path').value = 'BendGame/Content/Example.uasset'; node('#modal').close(); initial = nativeRequests.length;
  assert.equal(await callback(), false); assert.equal(nativeRequests.length, initial); checks += 2;
  reset(); actionFixtures.set('workshopBuildReview', { token: 'checked-pak', session: 'file-draft', revision: 0, assetCount: 1,
    bytes: 1024, changedProperties: 0, sourceNames: ['Opened.pak'], warnings: [] });
  assert(await ui.reviewWorkshopBuild()); callback = node('#workshop-export').onclick;
  node('#modal').close(); initial = nativeRequests.length;
  assert.equal(await callback(), false); assert.equal(nativeRequests.length, initial); checks += 3;
  const project = { id: 'a'.repeat(32), name: 'Project', generation: 1, modifiedAt: 1793185200, sourceCount: 1, assetCount: 1 };
  reset(); actionFixtures.set('workshopProjectDeleteReview', { token: 'checked-project', project });
  assert(await ui.deleteWorkshopProject(project)); callback = node('#workshop-project-delete-confirm').onclick;
  node('#modal').close(); initial = nativeRequests.length;
  assert.equal(await callback(), false); assert.equal(nativeRequests.length, initial); checks += 3;
  reset(); actionFixtures.set('workshopProjectsList', { projects: [project] });
  const openControl = { dataset: { workshopProjectOpen: project.id } }, deleteControl = { dataset: { workshopProjectDelete: project.id } },
    previousQuery = context.document.querySelectorAll;
  context.document.querySelectorAll = (selector) => selector === '[data-workshop-project-open]' ? [openControl]
    : selector === '[data-workshop-project-delete]' ? [deleteControl] : previousQuery(selector);
  assert(await ui.showWorkshopProjects()); node('#modal').close(); initial = nativeRequests.length;
  assert.equal(await openControl.onclick(), false); assert.equal(await deleteControl.onclick(), false);
  assert.equal(nativeRequests.length, initial); context.document.querySelectorAll = previousQuery; checks += 4;
}
(async () => { for (const platform of ['windows', 'linux']) await test(platform);
  console.log(`Workshop file tools passed (${checks} Windows/Linux checks).`);
})().catch((error) => { console.error(error); process.exitCode = 1; });
