'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const full = fs.readFileSync(path.join(__dirname, 'ui_smoke.js'), 'utf8');
const end = full.indexOf('async function testProfileCreation()');
assert(end > 0);
const harness = full.slice(0, end).replace('state, normalizeSnapshot, render,',
  'state, normalizeSnapshot, render, characterAppearance, renderCharacterAppearance, appearancePartners, modCategory, categoryOptions,');
let scenarios = 0;

async function test(platform) {
  const { ui, node, context, fixture, nativeRequests, actionFixtures } = new Function('require', '__dirname', harness +
    '\nreturn {ui,node,context,fixture,nativeRequests,actionFixtures};')(require, __dirname);
  const art = { ...fixture.mods.find((mod) => mod.id === 'art-the-clown-player-model'),
    imported: true, enabled: false, owned: false, conflictsWithInstalled: [] };
  assert(art.appearance && art.variantGroup, 'The exact new catalog skin carries explicit appearance metadata');
  const old = { ...art, id: 'old-character-outfit', name: 'Older outfit', sha256: 'b'.repeat(64), enabled: true, owned: true };
  const hat = { ...art, id: 'independent-hat', name: 'Hat', sha256: 'c'.repeat(64), enabled: true, owned: true,
    variantGroup: 'independent-hats', appearance: { character: 'deacon', type: 'accessory', label: 'Hat' } };
  const hinted = { id: 'third-party-character', name: 'External custom PAK', sha256: 'd'.repeat(64),
    imported: true, enabled: false, owned: false, detectedCharacter: 'deacon', category: 'Local mods' };
  const misleadingName = { id: 'name-alone', name: 'Deacon skin outfit', sha256: 'e'.repeat(64),
    imported: true, enabled: false, owned: false, category: 'Local mods' };
  const snapshot = (mods) => ({ ...fixture, platform, edition: 'local', mods, accountContext: 'appearance-' + platform,
    gameRunning: false, settings: { ...fixture.settings, gamePath: '/Synthetic/DaysGone' } });
  Object.assign(ui.state, { preview: false, loading: false, busy: false, view: 'mods', nativeDropActivity: null,
    profileSession: { active: false }, libraryMetaLoaded: true, libraryMeta: { entries: {}, history: [] },
    filter: 'stored', category: 'all', search: '', favoritesOnly: false,
    snapshot: ui.normalizeSnapshot(snapshot([art, old, hat, hinted, misleadingName])) });
  actionFixtures.set('snapshot', snapshot([art, old, hat, hinted, misleadingName]));
  node('#modal').close();
  assert.equal(ui.modCategory(art), 'Character appearance');
  assert.equal(ui.modCategory(hinted), 'Character appearance');
  assert.equal(ui.modCategory(misleadingName), 'Local mods', 'A filename or name cannot grant character metadata');
  ui.state.category = 'Character appearance';
  assert.deepEqual(Array.from(ui.filteredMods(), (mod) => mod.id).sort(), [art.id, hinted.id].sort(),
    'The ordinary category filter includes imported detected character packages');
  ui.state.search = 'deacon';
  assert.deepEqual(Array.from(ui.filteredMods(), (mod) => mod.id).sort(), [art.id, hinted.id].sort(),
    'Character search uses actual metadata or native hints, including mods with unrelated display names');
  ui.state.search = '';
  scenarios += 2;

  ui.showModDetails(art.id);
  const markup = node('#mod-details-panel').innerHTML;
  assert(markup.includes('Author model preview') && markup.includes('static reference image'));
  assert(markup.includes('assets/mod-art-the-clown.png'));
  assert(!markup.includes('live 3D'), 'A supplied image is not presented as a live game renderer');
  node('#detail-character-preview').onclick();
  assert(node('#modal').open && node('#modal-content').innerHTML.includes('assets/mod-art-the-clown.png'));
  assert(!nativeRequests.some((request) => request.action === 'openLocation'), 'Opening a preview stays inside the interface');
  node('#modal').close(); ui.handleModalClosed();
  for (const preview of ['https://example.test/x.png', '../secret.png', 'assets/mod-../secret.png', 'data:image/svg+xml,bad']) {
    assert(!ui.renderCharacterAppearance({ ...art, appearance: { ...art.appearance, preview } }).includes('<img'),
      'An unsafe or external preview location is not rendered');
  }
  assert(!ui.renderCharacterAppearance(hinted).includes('<img'), 'A detected third-party mod gets no invented preview image');
  scenarios += 3;

  assert.deepEqual(Array.from(ui.appearancePartners(art), (mod) => mod.id), [old.id],
    'Sharing a character does not make a hat exclusive with a full skin');
  const before = nativeRequests.length;
  await ui.toggleMod(art.id);
  assert(node('#modal').open && node('#modal-content').innerHTML.includes('Switch Deacon’s appearance?'));
  assert(node('#modal-content').innerHTML.includes('Older outfit') && !node('#modal-content').innerHTML.includes('Independent hats'));
  assert(!nativeRequests.slice(before).some((request) => ['enable', 'disable', 'switchMod'].includes(request.action)),
    'The appearance switch waits for a confirmation');
  const resulting = [ { ...art, enabled: true, owned: true }, { ...old, enabled: false, owned: false }, hat, hinted, misleadingName ];
  actionFixtures.set('snapshot', snapshot(resulting));
  await node('#confirm-action').onclick();
  const switched = nativeRequests.slice(before).find((request) => request.action === 'switchMod');
  assert(switched && switched.id === art.id && !('path' in switched));
  assert(!nativeRequests.slice(before).some((request) => request.action === 'disable'),
    'The native reviewed switch handles the transaction rather than a partial sequence of UI disables');
  scenarios += 2;

  ui.state.snapshot = ui.normalizeSnapshot(snapshot([art, old, hat]));
  await ui.toggleMod(art.id);
  ui.state.snapshot.mods.find((mod) => mod.id === old.id).sha256 = 'f'.repeat(64);
  const count = nativeRequests.filter((request) => request.action === 'switchMod').length;
  await node('#confirm-action').onclick();
  assert.equal(nativeRequests.filter((request) => request.action === 'switchMod').length, count,
    'A changed enabled appearance requires a new review');
  node('#modal').close(); ui.handleModalClosed();
  ui.state.snapshot = ui.normalizeSnapshot(snapshot([{ ...art, conflictsWithInstalled: [{ id: 'external:manual', name: 'Unmanaged PAK' }] }, old]));
  await ui.toggleMod(art.id);
  assert(node('#modal-content').innerHTML.includes('An installed PAK needs review'));
  assert(!node('#modal-content').innerHTML.includes('id="confirm-action"'),
    'A skin switch cannot remove an external package without its own native review');
  scenarios += 2;
}

(async () => {
  for (const platform of ['windows', 'linux']) await test(platform);
  console.log(`Character appearance checks passed (${scenarios} Windows/Linux scenarios).`);
})().catch((error) => { console.error(error); process.exitCode = 1; });
