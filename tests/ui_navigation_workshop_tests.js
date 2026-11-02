'use strict';

// Shared navigation and current-state labels; no native game or project writes.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
let harness = fs.readFileSync(path.join(__dirname, 'ui_smoke.js'), 'utf8');
harness = harness.slice(0, harness.indexOf('async function testProfileCreation()')).replace('state, normalizeSnapshot, render,',
  'openCharacterAppearance, renderCharacterLibraryIntro, renderCharacterAppearance, showCharacterModelPreview, renderMods, renderConflicts, renderWorkshop, renderWorkshopDetail, navigate, state, normalizeSnapshot, render,');
const html = fs.readFileSync(path.join(__dirname, '../ui/index.html'), 'utf8');
assert.equal((html.match(/data-view="workshop"/g) || []).length, 1, 'Workshop has one dedicated sidebar entry');
assert(html.includes('data-view="workshop"><span data-icon="cube"></span>Workshop'));
let checks = 2;
async function test(platform) {
  const { ui, node, fixture, nativeRequests, actionFixtures } = new Function('require', '__dirname', harness +
    '\nreturn {ui,node,fixture,nativeRequests,actionFixtures};')(require, __dirname);
  const art = { ...fixture.mods.find((mod) => mod.id === 'art-the-clown-player-model'), imported: true, enabled: true, owned: true },
    stored = { ...art, id: 'local-deacon-outfit', name: '<Local outfit>', enabled: false, owned: false,
      appearance: undefined, detectedCharacter: 'deacon', hasBeenEnabled: true },
    available = { ...art, id: 'available-outfit', name: 'Another outfit', imported: false, enabled: false, owned: false,
      appearance: { character: 'deacon', type: 'outfit', label: 'Another outfit' } },
    ordinary = { ...fixture.mods.find((mod) => mod.id === 'no-recoil'), imported: true, enabled: false, owned: false, hasBeenEnabled: false },
    broken = { ...ordinary, id: 'changed-record', status: 'changed', owned: true };
  const snapshot = { ...fixture, platform, mods: [art, stored, available, ordinary, broken], accountContext: 'nav-' + platform,
    edition: 'local', gameRunning: false, settings: { ...fixture.settings, gamePath: '/Synthetic/DaysGone' } };
  Object.assign(ui.state, { snapshot: ui.normalizeSnapshot(snapshot), preview: false, loading: false, busy: false,
    profileSession: { active: false }, libraryMetaLoaded: true, libraryMeta: { entries: {}, history: [] },
    workshopLoaded: true, workshopLoading: false, workshopWorking: false, workshop: null,
    view: 'mods', filter: 'stored', filterChosen: true, category: 'all', search: 'old search', favoritesOnly: true,
    characterAppearanceView: false, characterLibraryReturn: null, nativeDropActivity: null });
  actionFixtures.set('snapshot', snapshot);
  node('#modal').close();
  assert(ui.renderMods().includes('data-action="character-appearance"'));
  assert(ui.renderConflicts().includes('data-action="workshop-enter"') && ui.renderConflicts().includes('Open workshop'));
  ui.openLibraryTools();
  assert(node('#modal-content').innerHTML.includes('id="library-tool-workshop"'));
  assert(node('#modal-content').innerHTML.includes('id="library-tool-characters"'));
  node('#library-tool-characters').onclick();
  assert.equal(ui.state.view, 'mods'); assert.equal(ui.state.characterAppearanceView, true);
  assert.equal(ui.state.filter, 'all'); assert.equal(ui.state.category, 'Character appearance');
  assert.equal(ui.state.search, ''); assert.equal(ui.state.favoritesOnly, false);
  assert.deepEqual(Array.from(ui.filteredMods(), (mod) => mod.id).sort(), [art.id, stored.id, available.id].sort());
  const markup = ui.renderMods();
  assert(markup.includes('Character appearance.') && markup.includes('data-action="character-all-mods"'));
  assert(markup.includes('All character mods <span>3</span>'));
  assert(markup.includes('Enabled <span>1</span>') && markup.includes('Stored only <span>1</span>'));
  assert(!markup.includes('id="category-filter"') && !markup.includes('data-filter="disabled"'));
  assert(markup.includes('data-character-preview="' + art.id + '"') && markup.includes('not a 3D character viewer'));
  assert(ui.renderCharacterAppearance(stored).includes('No model preview is supplied'));
  assert(!ui.renderCharacterAppearance(stored).includes('<img'));
  assert(ui.showCharacterModelPreview(art.id));
  assert(node('#modal-content').innerHTML.includes('static reference image'));
  node('#modal').close(); ui.handleModalClosed();
  assert.equal(ui.showCharacterModelPreview(stored.id), false);
  checks += 21;

  await ui.handleAction('character-all-mods');
  assert.equal(ui.state.characterAppearanceView, false); assert.equal(ui.state.filter, 'stored');
  assert.equal(ui.state.search, 'old search'); assert.equal(ui.state.favoritesOnly, true);
  ui.state.search = ''; ui.state.favoritesOnly = false;
  assert.equal(ui.storedMods().length, 2, 'Inactive history does not split the storage set; changed records stay separate');
  assert.equal(ui.activationLabel(stored), 'Stored only');
  assert.equal(ui.activationLabel(ordinary), 'Stored only');
  ui.state.filter = 'disabled';
  assert.equal(ui.filteredMods().length, 2); assert.equal(ui.state.filter, 'stored');
  ui.openLibraryTools(); node('#library-tool-workshop').onclick();
  assert.equal(ui.state.view, 'workshop');
  const workshop = ui.renderWorkshop();
  assert(workshop.includes('Edit a mod') && workshop.includes('Combine mods'));
  assert(workshop.includes('Choose your mod PAKs') && workshop.includes('compare every supported version against its matching original'));
  assert(workshop.includes('Workshop help &amp; modding guide'));
  assert(!nativeRequests.some((request) => ['enable', 'disable', 'switchMod', 'workshopBrowse', 'workshopProjectSave'].includes(request.action)),
    'Finding the tools or preview does not change installed mods or open a file picker');
  checks += 14;

  ui.state.workshopSelection = 'many-sources';
  ui.state.workshopAsset = { id: 'many-sources', path: 'BendGame/Content/Test.uasset', fields: [],
    sourceIds: ['one', 'two', 'three'], sourceNames: ['First', 'Second', 'Third'] };
  const detail = ui.renderWorkshopDetail();
  assert(detail.includes('checks all 3 opened versions against that original'));
  assert(detail.includes('id="workshop-baseline"') && ['one', 'two', 'three'].every((id) => detail.includes('data-workshop-source="' + id + '"')),
    'Every source remains available alongside the supported comparison against one matching original');
  checks += 2;

  ui.state.snapshot.mods = [ordinary]; ui.openCharacterAppearance();
  const empty = ui.renderMods();
  assert(empty.includes('No character packages found'));
  assert(empty.includes('data-action="import"') && empty.includes('data-action="character-all-mods"'));
  assert(!empty.includes('data-clear-filters'), 'An absent character collection does not offer a reset that cannot find anything');
  checks += 3;
}
(async () => { for (const platform of ['windows', 'linux']) await test(platform);
  console.log(`Workshop navigation and storage UI passed (${checks} Windows/Linux checks).`);
})().catch((error) => { console.error(error); process.exitCode = 1; });
