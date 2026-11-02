'use strict';

// Production card/detail UI, real bundled metadata and synthetic native replies.
// No native application, account, game files or network is used.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, 'ui_smoke.js'), 'utf8');
const boundary = source.indexOf('async function testProfileCreation()');
assert(boundary > 0);
const harness = source.slice(0, boundary);

async function scenario(platform, width, fontScale) {
  const { ui, node, context, nativeRequests, fixture } = new Function('require', '__dirname', harness +
    '\nreturn {ui,node,context,nativeRequests,fixture};')(require, __dirname);
  Object.assign(ui.state, { preview: false, loading: false, busy: false, view: 'mods', filter: 'available',
    category: 'all', search: '', detailId: null, profileSession: { active: false }, nativeDropActivity: null,
    snapshot: ui.normalizeSnapshot({ ...fixture, platform, edition: 'local', gameRunning: false,
      accountContext: 'manual-catalog-' + platform, settings: { ...fixture.settings,
        gamePath: platform === 'linux' ? '/synthetic/DaysGone' : 'C:/Synthetic/DaysGone',
        autoScale: false, interfaceScale: 100, fontScale } }) });
  context.window.innerWidth = width; ui.syncLibraryDock();
  const native = ui.state.snapshot.mods.find((mod) => mod.id === 'bleed-out');
  const headshot = ui.state.snapshot.mods.find((mod) => mod.id === 'headshot-only-freakers');
  assert(native && headshot);
  const expected = ['bleed-out', 'bike-wheelie', 'headshot-only-freakers', 'ragdoll-deaths'];
  for (const id of expected) {
    assert(ui.libraryMods().some((mod) => mod.id === id), id + ' stays in the catalogue');
    assert(ui.filteredMods().some((mod) => mod.id === id), id + ' stays in the Available view');
    const mod = ui.state.snapshot.mods.find((item) => item.id === id);
    assert(ui.modArtwork(mod).includes('data-mod-icon='), id + ' uses the existing icon system');
  }
  ui.state.search = 'bleed';
  assert.deepEqual(Array.from(ui.filteredMods(), (mod) => mod.id), ['bleed-out'], 'Manual metadata remains searchable');
  assert(ui.renderMods().includes('Bleed Out'), 'The native record is rendered in the real collection view');
  assert.equal(ui.statusFor(native).label, 'Manual setup');
  const actions = ui.renderModRowActions(native);
  assert(actions.includes('View setup') && !/data-toggle|data-download|data-remove-mod|Import file/.test(actions),
    'A manual plugin has setup instructions, never PAK install/enable/delete controls');
  ui.showModDetails(native.id, { focus: false });
  const panel = node('#mod-details-panel');
  assert(panel.innerHTML.includes('Manual setup') && panel.innerHTML.includes('Manual native plugin setup.'));
  assert(panel.innerHTML.includes('PAK conflict scans, profiles and Find the bad mod do not inspect'));
  assert(panel.innerHTML.includes('Installed ASI and DLL plugins stay active across PAK profiles'),
    'Manual setup explicitly warns that PAK profiles leave native plugins active');
  assert(panel.innerHTML.includes('Setup on Nexus') && panel.innerHTML.includes('detail-asi-guide'));
  assert(!panel.innerHTML.includes('id="detail-action-reason"') && !panel.innerHTML.includes(native.unsupportedReason),
    'The dedicated manual setup banner replaces duplicate generic PAK action guidance');
  assert(!/detail-toggle|detail-import|id="detail-assets"|No conflicts with your installed mods/.test(panel.innerHTML),
    'Manual setup cannot imply a clean PAK scan or expose PAK actions');
  const before = nativeRequests.length;
  await ui.toggleMod(native.id);
  assert(!nativeRequests.slice(before).some((request) => /enable|disable|import|Profile/.test(request.action)),
    'A programmatic toggle attempt cannot issue a PAK write');
  await node('#detail-nexus').onclick();
  const open = nativeRequests.findLast((request) => request.action === 'openNexusFile');
  assert(open && open.modId === 994 && open.fileId === 3302, 'The setup link uses the exact checked public file');
  const oldGuide = node('#detail-asi-guide').onclick;
  oldGuide();
  assert.equal(ui.state.view, 'help'); assert.equal(ui.state.helpSection, 'modding'); assert.equal(ui.state.article, 'modding-asi');
  assert(ui.renderHelp().includes('ASI loaders: when a mod needs one'), 'The setup guide opens the real existing chapter');
  ui.state.view = 'mods'; ui.state.search = ''; ui.showModDetails(headshot.id, { focus: false });
  const headshotMarkup = panel.innerHTML;
  assert(headshotMarkup.includes('detail-import') && headshotMarkup.includes('Horde support'));
  assert(headshotMarkup.includes('does not install, toggle or remove the optional ASI'),
    'Headshot keeps its managed PAK actions and explains the separate optional plugin');
  assert(headshotMarkup.includes('Disabling this PAK in the toolkit does not switch off the separate plugin'),
    'The optional horde plugin is not falsely covered by the PAK switch');
  const view = ui.state.view;
  assert.equal(oldGuide(), false); assert.equal(ui.state.view, view, 'Queued setup clicks cannot replace newer details');
  headshot.imported = true;
  assert(ui.renderModRowActions(headshot).includes('>Enable</button>'), 'A stored verified Headshot PAK stays enableable');
  assert(!nativeRequests.some((request) => request.action.startsWith('nexus') || request.action === 'modAssetDetails'),
    'Manual native guidance stays local and makes no account API or native asset-scan requests');
}

(async () => {
  await scenario('windows', 1440, 100);
  await scenario('linux', 900, 150);
  await scenario('linux', 640, 150);
  console.log('Manual native catalogue UI: Windows/Linux catalogue visibility, limits and guide navigation passed.');
})().catch((error) => { console.error(error); process.exitCode = 1; });
