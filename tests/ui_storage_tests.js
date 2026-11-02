'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
let harness = fs.readFileSync(path.join(__dirname, 'ui_smoke.js'), 'utf8');
const boundary = harness.indexOf('async function testProfileCreation()');
assert(boundary > 0);
harness = harness.slice(0, boundary).replace('state, normalizeSnapshot, render,',
  'loadStorageStatus, renderStoragePanel, storageMoveBlocked, chooseStorageLocation, state, normalizeSnapshot, render,');
const { ui, node, nativeRequests, actionFixtures, fixture } = new Function('require', '__dirname', harness +
  '\nreturn {ui,node,nativeRequests,actionFixtures,fixture};')(require, __dirname);

async function main() {
  for (const platform of ['windows', 'linux']) {
    Object.assign(ui.state, { preview: false, busy: false, view: 'settings', storage: null, storageLoading: false,
      nativeDropActivity: null, profileSession: { active: false },
      snapshot: ui.normalizeSnapshot({ ...fixture, platform, settings: { ...fixture.settings }, sandbox: false }) });
    const root = platform === 'windows' ? 'G:\\Toolkit Data' : '/media/Collection/Toolkit Data';
    const status = { path: root, bootstrapPath: 'private user-data record', temporaryPath: 'OS temp', custom: true, pending: null };
    actionFixtures.set('storageStatus', status);
    await ui.loadStorageStatus();
    assert.equal(ui.state.storage.path, root);
    assert(ui.renderStoragePanel().includes(root), 'The actual native data location is shown');
    assert(ui.renderStoragePanel().includes('Portable use and removing the toolkit'), 'Portable/uninstall behavior is explained in the same settings panel');
    assert(ui.renderStoragePanel().includes('Installed game mods and game saves remain in place'), 'Removal guidance distinguishes program data from game data');
    assert.equal(ui.storageMoveBlocked(), false);
    const plan = { token: 'one-time-native-storage-review', from: root, to: '<changed destination>', files: 4, bytes: 1024 };
    actionFixtures.set('storageBrowse', plan);
    const before = nativeRequests.length;
    assert.equal(await ui.chooseStorageLocation(), true);
    assert(node('#modal-content').innerHTML.includes('&lt;changed destination&gt;'), 'Storage paths are escaped in the review');
    assert.equal(nativeRequests.slice(before).some((request) => request.action === 'storageSchedule'), false, 'Choosing a folder alone never schedules or moves data');
    actionFixtures.set('storageSchedule', { ...status, pending: { destination: '/new data', phase: 'queued' }, restartRequired: true });
    await node('#confirm-action').onclick();
    const request = nativeRequests.slice(before).find((request) => request.action === 'storageSchedule');
    assert(request && request.token === plan.token && request.confirmed === true && !request.path,
      'Confirmation passes only the reviewed native token');
    assert(ui.renderStoragePanel().includes('Data move waiting for restart'), 'Queued moves explain that data stays active until restart');
    assert(ui.storageMoveBlocked(), 'A queued move cannot be scheduled a second time');
    ui.state.storage = { ...status, pending: { destination: root, source: '/previous data', phase: 'activated' }, error: 'A changed original was preserved.' };
    assert(ui.renderStoragePanel().includes('The new data folder is active'), 'Partial old-copy cleanup never pretends the new data is unavailable');
    ui.state.storage = status;
    ui.state.profileSession = { active: true };
    assert(ui.storageMoveBlocked(), 'An active profile session blocks moving data');
    const requestsBefore = nativeRequests.length;
    assert.equal(await ui.chooseStorageLocation(), false);
    assert.equal(nativeRequests.length, requestsBefore, 'A blocked session does not even open a picker');
    ui.state.profileSession = { active: false };
    ui.state.snapshot.sandbox = true;
    ui.state.storage = null;
    await ui.loadStorageStatus();
    assert.equal(nativeRequests.length, requestsBefore, 'Sandbox settings do not start a persistent storage request');
  }
  console.log('Storage settings review, restart, blocked-session, safe-path and sandbox checks passed on Windows/Linux.');
}
main().catch((error) => { console.error(error); process.exitCode = 1; });
