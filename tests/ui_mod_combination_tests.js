'use strict';

// Synthetic native replies only. No PAK, game, account or save is opened.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const full = fs.readFileSync(path.join(__dirname, 'ui_smoke.js'), 'utf8');
const end = full.indexOf('async function testProfileCreation()');
assert(end > 0);
const harness = full.slice(0, end).replace('state, normalizeSnapshot, render,',
  'state, normalizeSnapshot, render, combinationCandidates, openCombinationReview, showCombinationReport,');
let scenarios = 0;

async function test(platform) {
  const env = new Function('require', '__dirname', harness +
    '\nreturn {ui,node,context,fixture,nativeRequests,actionFixtures,actionErrors,reply:(data)=>nativeMessageListener({data})};')(require, __dirname);
  const { ui, node, context, fixture, nativeRequests, actionFixtures, actionErrors, reply } = env;
  const mods = Array.from({ length: 9 }, (_, index) => ({ id: 'local-' + index, name: 'Local mod ' + index,
    imported: true, enabled: index === 0, owned: index === 0, sha256: String(index + 1).repeat(64), status: 'imported',
    cacheIntegrity: 'verified', assetCount: 4 }));
  const installed = [{ id: 'external-id', name: 'Third-party silencer', readable: true, sha256: 'a'.repeat(64), external: true },
    { ...mods[0], readable: true }, { name: 'No native identity.pak', readable: true }];
  const snapshot = { ...fixture, platform, edition: 'local', accountContext: 'combination-' + platform,
    mods: [...mods, { id: 'ghost', name: 'Missing old mod', cacheRecorded: true, cacheIntegrity: 'missing' },
      { id: 'catalog-only', name: 'Not downloaded', imported: false }], installed, gameRunning: false };
  Object.assign(ui.state, { preview: false, loading: false, busy: false, view: 'conflicts', snapshot: ui.normalizeSnapshot(snapshot),
    nativeDropActivity: null, profileSession: { active: false }, libraryMetaLoaded: true,
    libraryMeta: { entries: {}, history: [] } });
  node('#modal').close();
  const candidates = ui.combinationCandidates();
  assert.equal(candidates.length, 10);
  assert(candidates.some((candidate) => candidate.id === 'external-id'));
  assert(!candidates.some((candidate) => ['ghost', 'catalog-only'].includes(candidate.id)),
    'A missing record or catalog download is not a local package');
  assert(ui.renderConflicts().includes('data-action="combination-review"'));
  scenarios++;

  const choose = (id, checked = true) => node('#combination-list').onchange({ target: { name: 'combination-package', value: id, checked } });
  const result = (ids, changes = {}) => ({ operation: 'mod-merge-review', status: 'needs-asset-merge', canAutoMerge: false,
    sources: ids.map((id) => ({ id, name: id === 'external-id' ? 'Third-party silencer' : 'Library mod ' + id,
      available: true, kind: id === 'external-id' ? 'installed' : 'library', assetCount: 4 })),
    families: [{ path: 'BendGame/Content/Weapons/Silencer', kind: 'asset', sources: ids.slice(0, 2).map((id) =>
      ({ id, name: id, paths: ['BendGame/Content/Weapons/Silencer.uasset', 'BendGame/Content/Weapons/Silencer.uexp'] })) }],
    overlapCount: 1, truncated: false, warnings: ['These files are reviewed without changing them.'], ...changes });
  assert(ui.openCombinationReview());
  assert.equal(node('#combination-check').disabled, true);
  await node('#combination-check').onclick();
  assert(!nativeRequests.some((request) => request.action === 'modMergeReview'));
  choose('local-0'); assert.equal(node('#combination-check').disabled, true);
  choose('external-id'); assert.equal(node('#combination-check').disabled, false);
  choose('not-a-native-id');
  actionFixtures.set('modMergeReview', result(['local-0', 'external-id']));
  assert(await node('#combination-check').onclick());
  let sent = nativeRequests.filter((request) => request.action === 'modMergeReview').at(-1);
  assert.deepEqual(Array.from(sent.ids), ['local-0', 'external-id']);
  assert(!('path' in sent) && !('files' in sent));
  let markup = node('#modal-content').innerHTML;
  assert(markup.includes('These packages share cooked assets') && markup.includes('dedicated asset merge'));
  assert(markup.includes('Silencer.uasset') && markup.includes('Silencer.uexp'));
  assert(!/<button[^>]*>\s*(?:Install|Merge)(?:\s|<)/i.test(markup), 'A read-only path review has no pretend Merge or Install action');
  scenarios += 2;

  ui.openCombinationReview();
  for (let index = 0; index < 9; ++index) choose('local-' + index);
  assert(node('#combination-count').textContent.startsWith('9 selected'));
  assert(!node('#combination-list').innerHTML.includes('value="local-8" checked disabled'));
  const all = mods.map((mod) => mod.id);
  actionFixtures.set('modMergeReview', result(all));
  await node('#combination-check').onclick();
  sent = nativeRequests.filter((request) => request.action === 'modMergeReview').at(-1);
  assert.equal(sent.ids.length, 9, 'A ninth package is included; native data limits replace the old arbitrary source cap');
  scenarios++;

  const many = Array.from({ length: 102 }, (_, i) => ({ id: 'large-' + i, name: 'Large selection ' + i, available: i !== 101 }));
  ui.showCombinationReport(result(all, { sources: many, status: 'no-path-overlap', families: [], overlapCount: 0 }), many.map((s) => s.id));
  markup = node('#modal-content').innerHTML;
  assert(markup.includes('Some packages could not be checked'), 'Unchecked inputs beyond visible rows still make the complete review incomplete');
  assert(markup.includes('102 selected packages') && markup.includes('first 100'), 'A large source list names its display limit explicitly');
  scenarios++;

  ui.showCombinationReport(result(['local-0', 'external-id'], { status: 'no-path-overlap', families: [], overlapCount: 0 }), ['local-0', 'external-id']);
  markup = node('#modal-content').innerHTML;
  assert(markup.includes('No shared package paths found') && markup.includes('does not guarantee'));
  assert(!markup.includes('class="banner success"'), 'No path overlap is not a gameplay compatibility guarantee');
  ui.showCombinationReport(result(['local-0', 'external-id'], {
    families: [{ path: 'BendGame/Config/Game.ini', kind: 'package', sources: [] }] }), ['local-0', 'external-id']);
  markup = node('#modal-content').innerHTML;
  assert(markup.includes('These packages share game files') && markup.includes('understands those file formats'));
  assert(markup.includes('Shared package paths') && !markup.includes('These packages share cooked assets'),
    'An overlap of non-cooked files is not mislabeled as a cooked asset');
  scenarios++;
  ui.showCombinationReport(result(['local-0', 'external-id'], { status: 'no-path-overlap',
    sources: [{ id: 'local-0', name: 'Library mod', available: true },
      { id: 'external-id', name: '<img src=x onerror=bad()>', available: false, reason: '<b>Unreadable</b>' }],
    warnings: ['<script>bad()</script>'], truncated: true }), ['local-0', 'external-id']);
  markup = node('#modal-content').innerHTML;
  assert(markup.includes('Some packages could not be checked') && markup.includes('still unknown'));
  assert(markup.includes('&lt;img') && markup.includes('&lt;b&gt;Unreadable') && markup.includes('&lt;script&gt;'));
  assert(!markup.includes('<img src=x') && !markup.includes('<script>bad'));
  assert(markup.includes('Only part of the overlap evidence is shown'));
  scenarios += 2;

  ui.openCombinationReview(['local-0', 'external-id']);
  actionErrors.set('modMergeReview', 'Synthetic unreadable package');
  assert.equal(await node('#combination-check').onclick(), false);
  assert(node('#combination-error').textContent.includes('Synthetic unreadable package'));
  assert.equal(node('#combination-check').disabled, false, 'A failed read keeps the selection available to retry');
  actionErrors.clear();
  scenarios++;

  const bridge = context.window.chrome.webview, originalPost = bridge.postMessage;
  let held;
  bridge.postMessage = (request) => {
    if (request.action === 'modMergeReview') { held = request; nativeRequests.push(request); }
    else originalPost.call(bridge, request);
  };
  const start = async () => {
    held = null; ui.openCombinationReview(['local-0', 'external-id']);
    const pending = node('#combination-check').onclick();
    for (let count = 0; !held && count < 20; ++count) await new Promise(setImmediate);
    assert(held); return { pending };
  };
  let operation = await start();
  node('#modal').close(); ui.handleModalClosed();
  reply({ requestId: held.requestId, ok: true, data: result(held.ids) });
  assert.equal(await operation.pending, false);
  assert.equal(node('#modal').open, false, 'A completed review cannot reopen a closed dialog');
  scenarios++;

  operation = await start();
  ui.showChangelog('1.0.6'); const newer = node('#modal-content').innerHTML;
  reply({ requestId: held.requestId, ok: true, data: result(held.ids) });
  assert.equal(await operation.pending, false);
  assert.equal(node('#modal-content').innerHTML, newer, 'A stale review cannot replace a newer unrelated dialog');
  scenarios++;

  operation = await start();
  ui.state.snapshot.accountContext = 'different-installation';
  reply({ requestId: held.requestId, ok: true, data: result(held.ids) });
  assert.equal(await operation.pending, false);
  assert(!node('#modal-content').innerHTML.includes('These packages share cooked assets'), 'An old installation’s response is discarded');
  ui.state.snapshot.accountContext = snapshot.accountContext;
  scenarios++;

  operation = await start();
  ui.state.snapshot.mods[0].sha256 = 'f'.repeat(64);
  reply({ requestId: held.requestId, ok: true, data: result(held.ids) });
  assert.equal(await operation.pending, false);
  assert(node('#combination-error').textContent.includes('selected package changed'));
  assert(!node('#modal-content').innerHTML.includes('These packages share cooked assets'));
  scenarios++;
  bridge.postMessage = originalPost;
  assert(!nativeRequests.some((request) => ['enable', 'disable', 'switchMod', 'remove', 'confirmReviewedImport'].includes(request.action)),
    'The entire combination workflow remains read-only');
}

(async () => {
  for (const platform of ['windows', 'linux']) await test(platform);
  console.log(`Combination review checks passed (${scenarios} Windows/Linux scenarios).`);
})().catch((error) => { console.error(error); process.exitCode = 1; });
