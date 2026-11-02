'use strict';
// Synthetic native replies only. No saved projects, PAKs or game files are read.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
let harness = fs.readFileSync(path.join(__dirname, 'ui_smoke.js'), 'utf8');
harness = harness.slice(0, harness.indexOf('async function testProfileCreation()')).replace('state, normalizeSnapshot, render,',
  'workshopProjectRecord, workshopProjectBinding, workshopNeedsProjectSave, renderWorkshopProjectStatus, saveWorkshopProject, reviewWorkshopReplacement, openWorkshopProject, deleteWorkshopProject, showWorkshopProjects, renderWorkshop, loadWorkshopStatus, workshopPending, queueWorkshopEdit, clearWorkshopDraftInputs, closeWorkshop, startWorkshopAsset, state, normalizeSnapshot, render,');
let checks = 0;
async function test(platform) {
  const { ui, node, context, fixture, nativeRequests, actionFixtures, actionErrors, reply } = new Function('require', '__dirname', harness +
    '\nreturn {ui,node,context,fixture,nativeRequests,actionFixtures,actionErrors,reply:(data)=>nativeMessageListener({data})};')(require, __dirname);
  const project = (id = 'a'.repeat(32), name = 'Silencer <project>', generation = 1) => ({ id, name, generation,
    modifiedAt: 1793185200, sourceCount: 2, assetCount: 1, changedProperties: 1, unresolvedCount: 1 });
  const session = (changes = {}) => ({ id: 'native-draft', revision: 3, dirty: true, projectDirty: true,
    project: null, canBuild: false, canUndo: true, unresolvedCount: 1, sourceCount: 2, assetCount: 1,
    sources: [{ id: 'source-1', name: 'First.pak' }, { id: 'source-2', name: 'Second.pak' }],
    assets: [{ id: 'asset-one', path: 'BendGame/Content/Test.uasset', bytes: 512 }], warnings: [], ...changes });
  const snapshot = { ...fixture, platform, edition: 'local', accountContext: 'project-' + platform, gameRunning: false,
    settings: { ...fixture.settings, gamePath: '/Synthetic/DaysGone' } };
  const reset = (changes = {}) => {
    Object.assign(ui.state, { preview: false, loading: false, busy: false, view: 'workshop', workshop: session(changes),
      workshopWorking: false, workshopLoading: false, workshopProjectsLoading: false, workshopLoaded: true,
      workshopError: '', workshopAsset: null, workshopSelection: '', profileSession: { active: false },
      nativeDropActivity: null, snapshot: ui.normalizeSnapshot(snapshot) });
    node('#modal').close(); ui.handleModalClosed(); ui.clearWorkshopDraftInputs();
  };
  actionFixtures.set('snapshot', snapshot);
  const saved = project();
  actionFixtures.set('workshopProjectsList', { projects: [saved], warnings: ['Native <warning>'] });
  actionFixtures.set('workshopProjectSave', { session: session({ project: saved, projectDirty: false }), project: saved });
  actionFixtures.set('workshopProjectOpen', { session: session({ id: 'reopened-draft', project: saved, projectDirty: false }), project: saved });
  actionFixtures.set('workshopProjectDeleteReview', { token: 'native-delete-token', project: saved });
  actionFixtures.set('workshopProjectDelete', { deleted: true, id: saved.id,
    session: session({ project: null, projectDirty: true }) });
  reset();
  assert.equal(ui.workshopProjectRecord({ ...saved, id: '../wrong id' }), null);
  assert.equal(ui.workshopProjectRecord({ ...saved, name: '\u0000name' }), null);
  assert(ui.renderWorkshop().includes('Saved projects…'));
  assert(ui.renderWorkshopProjectStatus().includes('Save project…'));
  checks += 4;

  assert(await ui.saveWorkshopProject());
  assert(node('#modal-content').innerHTML.includes('unfinished merge decisions'));
  node('#workshop-project-name').value = 'Silencer <project>';
  assert(await node('#workshop-project-save-confirm').onclick());
  const request = nativeRequests.find((item) => item.action === 'workshopProjectSave');
  assert.equal(request.session, 'native-draft'); assert.equal(request.revision, 3);
  assert.equal(request.name, 'Silencer <project>'); assert(!('projectId' in request));
  assert.equal(ui.state.workshop.projectDirty, false);
  assert.equal(ui.state.workshop.dirty, true, 'Saving a project does not pretend a PAK was exported');
  assert.equal(ui.workshopNeedsProjectSave(), false);
  assert(!node('#modal').open); assert(ui.renderWorkshopProjectStatus().includes('Saved on this computer'));
  checks += 10;

  // Saving an existing project uses only the current native binding.
  reset({ project: saved });
  assert(await ui.saveWorkshopProject()); node('#workshop-project-name').value = 'Renamed project';
  const renamed = project(saved.id, 'Renamed project', 2);
  actionFixtures.set('workshopProjectSave', { session: session({ project: renamed, projectDirty: false }), project: renamed });
  assert(await node('#workshop-project-save-confirm').onclick());
  assert.equal(nativeRequests.filter((item) => item.action === 'workshopProjectSave').at(-1).projectId, saved.id);
  assert.equal(ui.state.workshop.project.name, 'Renamed project');
  checks += 3;
  reset({ project: saved });
  assert(await ui.saveWorkshopProject()); node('#workshop-project-name').value = 'Separate copy';
  const copy = project('b'.repeat(32), 'Separate copy');
  actionFixtures.set('workshopProjectSave', { session: session({ project: copy, projectDirty: false }), project: copy });
  assert(await node('#workshop-project-save-copy').onclick());
  assert(!('projectId' in nativeRequests.filter((item) => item.action === 'workshopProjectSave').at(-1)));
  assert.equal(ui.state.workshop.project.id, copy.id);
  checks += 3;

  // Failed or malformed confirmations keep the name and draft available.
  reset({ project: saved }); assert(await ui.saveWorkshopProject());
  node('#workshop-project-name').value = 'Keep my name';
  actionErrors.set('workshopProjectSave', 'This project was changed by another process. Save as a new project.');
  assert.equal(await node('#workshop-project-save-confirm').onclick(), false);
  assert(node('#modal').open); assert.equal(node('#workshop-project-name').value, 'Keep my name');
  assert(node('#workshop-project-error').textContent.includes('changed by another process'));
  assert(!node('#workshop-project-save-confirm').disabled);
  actionErrors.delete('workshopProjectSave');
  actionFixtures.set('workshopProjectSave', { session: session(), project: saved });
  assert.equal(await node('#workshop-project-save-confirm').onclick(), false);
  assert(node('#modal').open && node('#workshop-project-error').textContent.includes('confirmed saved record'));
  assert.equal(ui.state.workshop.projectDirty, true);
  checks += 8;

  reset({ project: saved, projectDirty: false });
  assert(await ui.showWorkshopProjects());
  let markup = node('#modal-content').innerHTML;
  assert(markup.includes('Silencer &lt;project&gt;') && markup.includes('Native &lt;warning&gt;'));
  assert(markup.includes('original downloads have moved or been deleted'));
  assert(markup.includes('data-workshop-project-open') && markup.includes('data-workshop-project-delete'));
  checks += 4;

  node('#modal').close(); ui.handleModalClosed();
  assert(await ui.openWorkshopProject(saved));
  assert.equal(ui.state.workshop.id, 'reopened-draft');
  assert.equal(ui.state.workshop.project.id, saved.id);
  const opened = nativeRequests.filter((item) => item.action === 'workshopProjectOpen').at(-1);
  assert.equal(opened.id, saved.id); assert(!('path' in opened));
  checks += 4;

  reset({ project: saved });
  assert(await ui.openWorkshopProject(saved));
  assert(node('#modal-content').innerHTML.includes('Save project…'));
  assert(node('#modal-content').innerHTML.includes('Continue without saving'));
  const openCount = nativeRequests.filter((item) => item.action === 'workshopProjectOpen').length;
  assert(await node('#workshop-project-keep').onclick());
  actionFixtures.set('workshopProjectSave', { session: session({ project: saved, projectDirty: false }), project: saved });
  node('#workshop-project-name').value = saved.name;
  assert(await node('#workshop-project-save-confirm').onclick());
  assert.equal(nativeRequests.filter((item) => item.action === 'workshopProjectOpen').length, openCount + 1,
    'Opening continues only after successful native project save');
  checks += 5;

  reset({ project: saved, projectDirty: false });
  actionFixtures.set('workshopClose', { session: null });
  assert(ui.closeWorkshop());
  await new Promise(setImmediate);
  assert(!node('#modal').open, 'An unexported but project-saved draft closes without a false discard prompt');
  checks += 2;

  reset({ project: saved });
  actionFixtures.set('workshopProjectDeleteReview', { token: 'native-delete-token', project: project(saved.id, 'Freshly renamed project', 2) });
  assert(await ui.deleteWorkshopProject(saved));
  assert(node('#modal-content').innerHTML.includes('Freshly renamed project'));
  assert(!node('#modal-content').innerHTML.includes('Silencer &lt;project&gt;'), 'Deletion uses the fresh native review name');
  assert(node('#modal-content').innerHTML.includes('current temporary draft stays open'));
  const deleteCount = nativeRequests.filter((item) => item.action === 'workshopProjectDelete').length;
  assert.equal(deleteCount, 0, 'Project deletion waits for confirmation');
  assert(await node('#workshop-project-delete-confirm').onclick());
  const deleted = nativeRequests.filter((item) => item.action === 'workshopProjectDelete').at(-1);
  assert.equal(deleted.token, 'native-delete-token'); assert.equal(deleted.confirmed, true);
  assert(!('path' in deleted) && !('id' in deleted));
  assert.equal(ui.state.workshop.project, null); assert.equal(ui.state.workshop.projectDirty, true);
  checks += 9;

  reset();
  ui.state.workshopAsset = { id: 'asset-one', editable: true, fields: [{ id: 'property-one', type: 'IntProperty', value: 2 }] };
  ui.state.workshopSelection = 'asset-one';
  assert.equal(ui.queueWorkshopEdit('property-one', { value: 'invalid' }), false);
  const count = nativeRequests.length;
  assert.equal(await ui.saveWorkshopProject(), false); assert.equal(await ui.showWorkshopProjects(), false);
  assert.equal(nativeRequests.length, count, 'Unapplied invalid values cannot be saved or silently discarded');
  ui.clearWorkshopDraftInputs();
  checks += 3;

  const bridge = context.window.chrome.webview, post = bridge.postMessage;
  let held, heldAction;
  bridge.postMessage = (request) => {
    if (request.action === heldAction && !held) { held = request; nativeRequests.push(request); }
    else post.call(bridge, request);
  };
  async function waitHeld() { for (let i = 0; i < 40 && !held; ++i) await new Promise(setImmediate); assert(held); }
  reset(); actionFixtures.set('workshopProjectSave', { session: session({ project: saved, projectDirty: false }), project: saved });
  assert(await ui.saveWorkshopProject()); node('#workshop-project-name').value = saved.name;
  heldAction = 'workshopProjectSave'; held = null;
  let work = node('#workshop-project-save-confirm').onclick(); await waitHeld();
  ui.showChangelog('1.0.7'); const newer = node('#modal-content').innerHTML;
  reply({ requestId: held.requestId, ok: true, data: { session: session({ project: saved, projectDirty: false }), project: saved } });
  assert.equal(await work, false); assert.equal(node('#modal-content').innerHTML, newer);
  assert.equal(ui.state.workshop.projectDirty, true, 'A stale save response cannot change a newer client draft');
  checks += 3;

  reset(); heldAction = 'workshopProjectsList'; held = null;
  work = ui.showWorkshopProjects(); await waitHeld(); ui.showChangelog('1.0.7');
  const newerList = node('#modal-content').innerHTML;
  reply({ requestId: held.requestId, ok: true, data: { projects: [saved] } });
  assert.equal(await work, false); assert.equal(node('#modal-content').innerHTML, newerList);
  assert.equal(ui.state.workshopProjectsLoading, false);
  checks += 3;

  reset({ projectDirty: false }); heldAction = 'workshopProjectOpen'; held = null;
  work = ui.openWorkshopProject(saved); await waitHeld();
  await ui.acceptSnapshot({ ...snapshot, settings: { ...snapshot.settings, gamePath: '/Different/DaysGone' } });
  reply({ requestId: held.requestId, ok: true, data: { session: session({ id: 'old-project-open', project: saved, projectDirty: false }), project: saved } });
  assert.equal(await work, false); assert.notEqual(ui.state.workshop?.id, 'old-project-open');
  assert.equal(ui.state.workshopWorking, false);
  checks += 3;

  assert(!nativeRequests.filter((item) => item.action.startsWith('workshopProject')).some((item) =>
    ['path', 'sourcePath', 'root', 'bytes'].some((key) => Object.hasOwn(item, key))), 'Renderer project operations contain no filesystem plans or raw bytes');
  checks += 1;
}
(async () => { for (const platform of ['windows', 'linux']) await test(platform);
  console.log(`Workshop projects UI passed (${checks} Windows/Linux checks).`);
})().catch((error) => { console.error(error); process.exitCode = 1; });
