'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
let harness = fs.readFileSync(path.join(__dirname, 'ui_smoke.js'), 'utf8');
harness = harness.slice(0, harness.indexOf('async function testProfileCreation()')).replace('state, normalizeSnapshot, render,',
  'reviewSaveDeletion, importSaveFiles, saveManagementContext, saveDeleteBlockReason, updateSaveDeleteStatus, state, normalizeSnapshot, render,');
let checks = 0;
async function test(platform) {
  const { ui, node, context, fixture, nativeRequests, actionFixtures, actionErrors, reply } = new Function('require', '__dirname', harness +
    '\nreturn {ui,node,context,fixture,nativeRequests,actionFixtures,actionErrors,reply:(data)=>nativeMessageListener({data})};')(require, __dirname);
  const account = `saves-${platform}`, id = 'a'.repeat(64), currentId = 'c'.repeat(64), safetyId = 'b'.repeat(64);
  const snapshot = { ...fixture, platform, edition: 'local', accountContext: account, gameRunning: false,
    settings: { ...fixture.settings, gamePath: '/Synthetic/DaysGone' } };
  const backups = { available: true, settings: { automatic: false, keep: 5 }, location: { resolved: true },
    backups: [{ id, label: 'Selected <backup>', files: 2, bytes: 1024, createdAt: 100, reason: 'manual', pinned: true,
      profiles: [{ id: 'd'.repeat(32), name: 'Linked profile', stage: 'before-launch' }] }], recovery: { pending: false } };
  const current = { context: 'native-save-account', signature: 'current-signature', status: 'ready', saves: [{ id: currentId, name: 'Save <one>.sav',
    size: 512, modifiedUtc: '2026-10-28T10:00:00Z', deleteEligible: true }] };
  Object.assign(ui.state, { snapshot: ui.normalizeSnapshot(snapshot), preview: false, busy: false, loading: false,
    view: 'saves', savesTab: 'current', saveBackups: backups, currentSaves: current,
    profileSession: { active: false }, nativeDropActivity: null, saveProfileFilter: null,
    currentSaveSelection: { context: current.context, id: currentId }, saveSelection: { context: account, id } });
  actionFixtures.set('snapshot', snapshot); actionFixtures.set('saveBackupsList', backups);
  actionFixtures.set('currentSavesList', current);
  actionFixtures.set('currentSaveDeleteReview', { token: 'native-current-token', id: currentId, name: 'Save <one>.sav',
    operation: 'delete-current-save', safetyBackupRequired: true, warnings: [] });
  actionFixtures.set('backupDeleteReview', { token: 'native-backup-token', id, operation: 'delete-backup', warnings: ['Pinned backup.'] });
  actionFixtures.set('currentSaveDelete', { deleted: true, safetyBackupId: safetyId, safetyBackup: { id: safetyId } });
  actionFixtures.set('backupDelete', { deleted: true, id });
  node('#modal').close();
  assert(ui.renderSaves().includes('Import saves as backup…'));
  assert(ui.renderCurrentSavesPanel().includes('data-current-save-delete'));
  assert(ui.renderSaveBackupsPanel().includes('data-backup-delete'));
  checks += 3;

  assert(await ui.reviewSaveDeletion(currentId, true));
  assert(node('#modal-content').innerHTML.includes('Back up and delete save'));
  assert(node('#modal-content').innerHTML.includes('Save &lt;one&gt;.sav'));
  assert(node('#modal-content').innerHTML.includes('even when automatic backups are off'));
  assert(!nativeRequests.some((request) => request.action === 'currentSaveDelete'));
  ui.state.snapshot.gameRunning = true; ui.updateSaveDeleteStatus();
  assert(node('#save-delete-confirm').disabled);
  await node('#save-delete-confirm').onclick();
  assert(!nativeRequests.some((request) => request.action === 'currentSaveDelete'));
  ui.state.snapshot.gameRunning = false; ui.updateSaveDeleteStatus();
  assert(!node('#save-delete-confirm').disabled);
  assert(await node('#save-delete-confirm').onclick());
  const deletion = nativeRequests.find((request) => request.action === 'currentSaveDelete');
  assert.equal(deletion.token, 'native-current-token'); assert.equal(deletion.confirmed, true);
  assert(!('id' in deletion) && !('path' in deletion));
  assert(!node('#modal').open);
  checks += 9;

  assert(await ui.reviewSaveDeletion(id, false));
  ui.state.snapshot.gameRunning = true; ui.updateSaveDeleteStatus();
  assert(!node('#save-delete-confirm').disabled, 'Deleting an inert backup does not need the game to stop');
  actionErrors.set('backupDelete', 'A backup file changed.');
  assert.equal(await node('#save-delete-confirm').onclick(), false);
  assert(node('#modal').open, 'A failed deletion leaves the review visible');
  assert(node('#save-delete-status').textContent.includes('A backup file changed.'));
  assert.equal(node('#save-delete-confirm').textContent, 'Review again');
  actionErrors.delete('backupDelete');
  const beforeReview = nativeRequests.filter((request) => request.action === 'backupDeleteReview').length;
  assert(await node('#save-delete-confirm').onclick());
  assert.equal(nativeRequests.filter((request) => request.action === 'backupDeleteReview').length, beforeReview + 1,
    'Retry obtains a new one-use native review');
  ui.state.savePictures.set(id, 'local-test-picture'); ui.state.saveDetailsDrafts.set(id, {});
  assert(await node('#save-delete-confirm').onclick());
  assert(!ui.state.savePictures.has(id) && !ui.state.saveDetailsDrafts.has(id));
  checks += 8;

  ui.state.snapshot.gameRunning = false;
  ui.state.currentSaves = current; ui.state.currentSaveSelection = { context: current.context, id: currentId };
  assert(await ui.reviewSaveDeletion(currentId, true));
  const count = nativeRequests.filter((request) => request.action === 'currentSaveDelete').length;
  ui.state.snapshot.settings.gamePath = '/Another/Installation';
  await node('#save-delete-confirm').onclick();
  assert.equal(nativeRequests.filter((request) => request.action === 'currentSaveDelete').length, count);
  assert(node('#save-delete-status').textContent.includes('installation changed'));
  node('#modal').close(); ui.handleModalClosed(); ui.state.snapshot.settings.gamePath = '/Synthetic/DaysGone';
  checks += 2;

  actionFixtures.set('saveImportBrowse', { imported: true, id: safetyId, files: 1, bytes: 512 });
  actionFixtures.set('saveBackupsList', { ...backups, backups: [...backups.backups, { id: safetyId, reason: 'imported', files: 1, bytes: 512 }] });
  ui.state.saveProfileFilter = { context: account, id: 'e'.repeat(32), name: 'Unrelated profile' };
  assert(await ui.importSaveFiles());
  assert.equal(ui.state.saveSelection.id, safetyId); assert.equal(ui.state.saveProfileFilter, null);
  assert.equal(ui.state.savesTab, 'backups');
  assert(node('#modal-content').innerHTML.includes('current game saves have not changed'));
  assert(!nativeRequests.some((request) => request.action === 'saveBackupRestore'));
  checks += 5;
  node('#modal').close(); ui.handleModalClosed();
  const bridge = context.window.chrome.webview, post = bridge.postMessage;
  let held;
  let heldAction = 'backupDeleteReview';
  bridge.postMessage = (request) => { if (request.action === heldAction) { held = request; nativeRequests.push(request); } else post.call(bridge, request); };
  ui.state.saveSelection = { context: account, id };
  let work = ui.reviewSaveDeletion(id, false);
  for (let i = 0; i < 40 && !held; ++i) await new Promise(setImmediate);
  assert(held); ui.showChangelog('1.0.6');
  let newer = node('#modal-content').innerHTML;
  reply({ requestId: held.requestId, ok: true, data: { token: 'old-backup-review', id } });
  assert.equal(await work, false);
  assert.equal(node('#modal-content').innerHTML, newer, 'A late delete review cannot replace a newer dialog');
  checks += 2;

  node('#modal').close(); ui.handleModalClosed();
  heldAction = 'backupDelete'; held = null;
  assert(await ui.reviewSaveDeletion(id, false));
  work = node('#save-delete-confirm').onclick();
  for (let i = 0; i < 40 && !held; ++i) await new Promise(setImmediate);
  assert(held); ui.showChangelog('1.0.6'); newer = node('#modal-content').innerHTML;
  reply({ requestId: held.requestId, ok: true, data: { deleted: true, id } });
  assert.equal(await work, false);
  assert.equal(node('#modal-content').innerHTML, newer, 'A late completed delete cannot close a newer dialog');
  checks += 3;

  node('#modal').close(); ui.handleModalClosed();
  heldAction = 'saveImportBrowse'; held = null;
  ui.state.view = 'saves';
  work = ui.importSaveFiles();
  for (let i = 0; i < 40 && !held; ++i) await new Promise(setImmediate);
  assert(held); ui.showChangelog('1.0.6'); newer = node('#modal-content').innerHTML;
  reply({ requestId: held.requestId, ok: true, data: { imported: true, id: safetyId } });
  assert.equal(await work, true);
  assert.equal(ui.state.saveSelection.id, safetyId);
  assert.equal(node('#modal-content').innerHTML, newer, 'A completed import keeps its selection without replacing a newer dialog');
  checks += 3;

  node('#modal').close(); ui.handleModalClosed();
  held = null; ui.state.view = 'saves';
  work = ui.importSaveFiles();
  for (let i = 0; i < 40 && !held; ++i) await new Promise(setImmediate);
  assert(held); ui.state.view = 'mods';
  reply({ requestId: held.requestId, ok: true, data: { imported: true, id: safetyId } });
  assert.equal(await work, true); assert.equal(ui.state.view, 'mods');
  assert(!node('#modal').open, 'A completed import does not pull the user back from another page');
  checks += 3;

  held = null;
  work = ui.importSaveFiles();
  for (let i = 0; i < 40 && !held; ++i) await new Promise(setImmediate);
  assert(held); ui.state.snapshot.accountContext = 'new-account';
  reply({ requestId: held.requestId, ok: true, data: { imported: true, id: 'f'.repeat(64) } });
  assert.equal(await work, false); assert(!node('#modal').open, 'An old import reply cannot open a dialog over a new installation');
  checks += 2;
}
(async () => { for (const platform of ['windows', 'linux']) await test(platform);
  console.log(`Save management UI passed (${checks} Windows/Linux checks).`);
})().catch((error) => { console.error(error); process.exitCode = 1; });
