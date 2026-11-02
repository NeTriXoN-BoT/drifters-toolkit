'use strict';
// Finder metadata and renderer replies are fixtures; no actual PAKs are touched.
const assert = require('node:assert/strict'), fs = require('node:fs'), path = require('node:path');
let harness = fs.readFileSync(path.join(__dirname, 'ui_smoke.js'), 'utf8');
harness = harness.slice(0, harness.indexOf('async function testProfileCreation()')).replace('state, normalizeSnapshot, render,',
  'reviewFinderStop, reviewFinderKeepCurrent, finishFinder, answerFinder, state, normalizeSnapshot, render,');
let checks = 0;
async function test(platform) {
  const { ui, node, fixture, nativeRequests, actionFixtures } = new Function('require', '__dirname', harness +
    '\nreturn {ui,node,fixture,nativeRequests,actionFixtures};')(require, __dirname);
  const finder = { active: true, stage: 'testing', round: 2, originalCount: 2, canContinue: false, canKeepCurrent: true,
    testing: [], unavailable: [{ id: 'missing-mod', name: 'Missing <mod>', reason: 'The library copy is missing.' }] };
  const snapshot = (value = finder) => ({ ...fixture, platform, edition: 'local', accountContext: 'finder-recovery-' + platform,
    gameRunning: false, finder: value, settings: { ...fixture.settings, gamePath: '/Synthetic/DaysGone' } });
  const reset = () => {
    Object.assign(ui.state, { preview: false, busy: false, loading: false, view: 'diagnostics', profileSession: { active: false }, nativeDropActivity: null,
      snapshot: ui.normalizeSnapshot(snapshot()) });
    actionFixtures.set('snapshot', snapshot()); node('#modal').close(); ui.handleModalClosed();
  };
  reset(); ui.openFinder();
  let html = node('#modal-content').innerHTML;
  assert(html.includes('Stop the mod finder') && html.includes('Missing &lt;mod&gt;'));
  assert(html.includes('Restore available mods') && html.includes('Stop without restoring'));
  const before = nativeRequests.length;
  await ui.answerFinder(true);
  assert(!nativeRequests.slice(before).some((request) => request.action === 'finderAnswer'), 'Unavailable sources cannot advance another round');
  node('#finder-stop-keep').onclick();
  assert(node('#modal-content').innerHTML.includes('Stop without restoring?'));
  assert(node('#modal-content').innerHTML.includes('only clears the finder record'));
  assert(!nativeRequests.slice(before).some((request) => request.action === 'finderFinish'), 'Metadata-only stop still needs explicit confirmation');
  const stopped = snapshot({ active: false });
  actionFixtures.set('snapshot', stopped);
  actionFixtures.set('finderFinish', { ...stopped, finderResult: { stopped: true, restored: false, keptCurrent: true, skipped: [] } });
  await node('#confirm-action').onclick();
  const keep = nativeRequests.slice(before).find((request) => request.action === 'finderFinish');
  assert(keep && keep.keepCurrent === true && keep.confirmed === true);
  assert(!('disableCulprit' in keep) && !('path' in keep));
  checks += 8;

  reset(); ui.reviewFinderKeepCurrent();
  const held = nativeRequests.length;
  ui.state.snapshot.settings.gamePath = '/Other/DaysGone';
  await node('#confirm-action').onclick();
  assert.equal(nativeRequests.filter((request) => request.action === 'finderFinish').length,
    nativeRequests.slice(0, held).filter((request) => request.action === 'finderFinish').length, 'A stale installation review cannot clear its replacement finder');
  checks += 1;

  reset();
  const restored = snapshot({ active: false });
  actionFixtures.set('snapshot', restored);
  actionFixtures.set('finderFinish', { ...restored, finderResult: { stopped: true, restored: true, keptCurrent: false, skipped: finder.unavailable } });
  ui.reviewFinderStop(); await node('#finder-stop-restore').onclick();
  html = node('#modal-content').innerHTML;
  assert(html.includes('Finder stopped with some packages unavailable'));
  assert(html.includes('Missing &lt;mod&gt;') && html.includes('Enabled list for their current state'));
  assert(!html.includes('Every other mod is enabled again') && !html.includes('all mods were restored'));
  assert(nativeRequests.filter((request) => request.action === 'finderFinish').at(-1).disableCulprit === false);
  checks += 4;

  reset(); ui.state.snapshot.gameRunning = true; ui.reviewFinderStop();
  assert(node('#modal-content').innerHTML.includes('id="finder-stop-restore" disabled'));
  assert(!node('#modal-content').innerHTML.includes('id="finder-stop-keep" disabled'), 'Keeping PAKs in place does not require changing an in-use file');
  reset(); ui.state.snapshot.finder = { active: true, damaged: true, canContinue: false, canKeepCurrent: true };
  ui.openFinder(); node('#finder-clear').onclick();
  assert(node('#modal-content').innerHTML.includes('Stop without restoring?'), 'A damaged record uses the safe metadata-only escape');
  checks += 3;
}
(async () => { for (const platform of ['windows', 'linux']) await test(platform); console.log(`Finder missing-package recovery UI passed (${checks} Windows/Linux checks).`); })()
  .catch((error) => { console.error(error); process.exitCode = 1; });
