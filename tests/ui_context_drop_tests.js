'use strict';
// Real shared UI with synthetic native replies; no real files or accounts.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
let harness = fs.readFileSync(path.join(__dirname, 'ui_smoke.js'), 'utf8');
harness = harness.slice(0, harness.indexOf('async function testProfileCreation()')).replace('state, normalizeSnapshot, render,',
  'workshopPropertyHelp, state, normalizeSnapshot, render,');
let checks = 0;
async function test(platform) {
  const { ui, node, fixture, nativeRequests, actionFixtures } = new Function('require', '__dirname', harness +
    '\nreturn {ui,node,fixture,nativeRequests,actionFixtures};')(require, __dirname);
  const snapshot = { ...fixture, platform, edition: 'local', accountContext: 'drop-fixture', gameRunning: false,
    settings: { ...fixture.settings, gamePath: '/Synthetic/DaysGone' } };
  Object.assign(ui.state, { snapshot: ui.normalizeSnapshot(snapshot), preview: false, busy: false, loading: false,
    view: 'workshop', workshop: null, workshopWorking: false, workshopLoading: false, nativeDropActivity: null,
    profileSession: { active: false } });
  node('#modal').close();
  const session = { id: 'native-draft', revision: 0, dirty: false, sources: [], assets: [], warnings: [] };
  actionFixtures.set('importDropped', { session });
  assert(await ui.importDroppedFile([{ name: 'Edit.PAK' }], platform === 'linux' ? 'native-token' : null));
  const request = nativeRequests.find((item) => item.action === 'importDropped');
  assert.equal(request.target, 'workshop'); assert(!('path' in request) && !('paths' in request));
  if (platform === 'linux') assert.equal(request.dropToken, 'native-token');
  assert.equal(ui.state.workshop.id, session.id);
  assert(!nativeRequests.some((item) => /batchImportConfirm|enable|importConfirm/.test(item.action)));
  checks += 5;
  const before = nativeRequests.length;
  assert.equal(await ui.importDroppedFile([{ name: 'Edit.pak' }, { name: 'Installer.exe' }]), false);
  assert.equal(nativeRequests.length, before, 'Mixed unsupported files are not silently imported as a subset');
  node('#modal').showModal();
  assert.equal(await ui.importDroppedFile([{ name: 'Edit.pak' }]), false);
  assert.equal(nativeRequests.length, before, 'A drop cannot replace an open review'); node('#modal').close();
  checks += 4;
  ui.state.view = 'saves';
  actionFixtures.set('importDropped', { imported: true, id: 'a'.repeat(64) });
  actionFixtures.set('saveBackupsList', { backups: [], recovery: { pending: false } });
  assert(await ui.importDroppedFile([{ name: 'Friend.sav' }], platform === 'linux' ? 'save-token' : null));
  assert.equal(nativeRequests.filter((item) => item.action === 'importDropped').at(-1).target, 'saves');
  assert.equal(ui.state.savesTab, 'backups'); assert(!node('#modal').open);
  assert(!nativeRequests.some((item) => /saveBackupRestore|currentSaveReplace/.test(item.action)), 'Dropping shared saves never overwrites progress');
  checks += 5;
  assert(ui.workshopPropertyHelp({ type: 'BoolProperty', name: 'UnknownFlag' }).includes('on/off'));
  assert(ui.workshopPropertyHelp({ type: 'FloatProperty', name: 'WeaponConfig/AimedAccuracy/NumDegreesIncRadiusPerShot' }).includes('firing bloom'));
  assert(ui.workshopPropertyHelp({ type: 'FloatProperty', name: 'MysteryDamage' }).includes('suggests'));
  checks += 3;
}
(async () => { for (const platform of ['windows', 'linux']) await test(platform);
  console.log(`Contextual file drops passed (${checks} Windows/Linux checks).`);
})().catch((error) => { console.error(error); process.exitCode = 1; });
