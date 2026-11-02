'use strict';

// Native replies and content identities are synthetic. No account, game, mod
// directory, browser or filesystem mutation is used by these UI regressions.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const full = fs.readFileSync(path.join(__dirname, 'ui_smoke.js'), 'utf8');
const end = full.indexOf('async function testProfileCreation()');
assert(end > 0);
const harness = full.slice(0, end).replace('state, normalizeSnapshot, render,',
  'state, normalizeSnapshot, render, presentDownloadHashes, reconcileImportNotices, reviewRemoveMod, cancelDownloadContinuation,');
const A = 'a'.repeat(64), B = 'b'.repeat(64), C = 'c'.repeat(64), D = 'd'.repeat(64);
let scenarios = 0;

function environment(platform) {
  const env = new Function('require', '__dirname', harness +
    '\nreturn {ui,node,context,nativeRequests,actionFixtures,fixture,timers,reply: (data) => nativeMessageListener({data})};')(require, __dirname);
  const { ui, node, context, fixture, actionFixtures } = env;
  const mod = { ...fixture.mods.find((item) => item.available !== false), id: 'local-fixture', name: 'Synthetic mod',
    sha256: A, imported: false, enabled: false, owned: false, cacheRecorded: false, cacheIntegrity: 'not-recorded',
    status: 'not-imported', conflictsWithInstalled: [], provenance: { kind: 'local-import', sourceSha256: C } };
  const snapshot = (changes = {}, mods = [mod]) => ({ ...fixture, platform, edition: 'local', mods,
    accountContext: 'library-lifecycle-' + platform, gameRunning: false,
    settings: { ...fixture.settings, gamePath: '/Synthetic/DaysGone' }, ...changes });
  Object.assign(ui.state, { preview: false, loading: false, busy: false, view: 'mods', snapshot: ui.normalizeSnapshot(snapshot()),
    profileSession: { active: false }, nativeDropActivity: null, nativeWindowHidden: false, downloadDiscoveryChecking: false,
    downloadOffers: [], noticeQueue: [], issues: [], setupIssues: [], libraryMetaLoaded: true,
    libraryMeta: { entries: {}, history: [] }, update: { lastCheck: Date.now() }, catalogUpdate: { lastCheck: Date.now() } });
  context.document.hidden = false;
  actionFixtures.set('snapshot', snapshot());
  actionFixtures.set('nexusStatus', { connected: false });
  node('#modal').close();
  return { ...env, mod, snapshot };
}

async function run(platform) {
  const { ui, node, context, nativeRequests, actionFixtures, mod, snapshot, timers, reply } = environment(platform);
  const mutations = () => nativeRequests.filter((item) => ['enable', 'disable', 'remove', 'confirmReviewedImport'].includes(item.action));
  ui.state.downloadOffers = [
    { id: 'same-content', name: 'Different filename.zip', sha256: A },
    { id: 'source-archive', name: 'Original archive.zip', sha256: C },
    { id: 'same-name-new-content', name: 'Synthetic mod.zip', sha256: B },
    { id: 'installed-outside', name: 'External.pak', sha256: D },
    { id: 'unverified', name: 'Synthetic mod.zip' }
  ];
  await ui.acceptSnapshot(snapshot({ installed: [{ sha256: D, readable: true }] }, [{ ...mod, imported: true }]));
  assert.deepEqual(Array.from(ui.state.downloadOffers, (offer) => offer.id), ['same-name-new-content', 'unverified']);
  assert.equal(mutations().length, 0, 'Notice reconciliation cannot install, disable or remove anything');
  ui.state.downloadOffers = [{ id: 'repair-download', name: 'Synthetic mod.zip', sha256: A }];
  await ui.acceptSnapshot(snapshot({}, [{ ...mod, cacheRecorded: true, cacheIntegrity: 'missing' }]));
  assert.equal(ui.state.downloadOffers.length, 1, 'A missing record cannot hide the download needed for repair');
  scenarios += 2;

  const queued = { reviewRequired: true, reviewToken: '1'.repeat(64), source: 'local-import', mod };
  ui.state.busy = true;
  ui.reviewDownloadedPackage(queued);
  ui.state.busy = false;
  assert(ui.state.noticeQueue.some((notice) => notice.type === 'package-review'));
  await ui.acceptSnapshot(snapshot({}, [{ ...mod, imported: true }]));
  assert(!ui.state.noticeQueue.some((notice) => notice.type === 'package-review'));
  assert(nativeRequests.some((request) => request.action === 'discardReviewedImport' && request.reviewToken === queued.reviewToken));
  scenarios++;

  await ui.acceptSnapshot(snapshot());
  ui.reviewDownloadedPackage({ ...queued, reviewToken: '2'.repeat(64) });
  assert(node('#modal').open);
  await ui.acceptSnapshot(snapshot({}, [{ ...mod, imported: true }]));
  assert.equal(node('#modal').open, false, 'An open review closes after its exact package is added elsewhere');
  await ui.acceptSnapshot(snapshot());
  ui.reviewDownloadedPackage({ ...queued, reviewToken: '3'.repeat(64) });
  ui.showChangelog('1.0.6');
  await ui.acceptSnapshot(snapshot({}, [{ ...mod, imported: true }]));
  assert(node('#modal').open && node('#modal-content').innerHTML.includes('changelog'),
    'A resolved old review cannot close a newer unrelated dialog');
  node('#modal').close(); ui.handleModalClosed();
  scenarios += 2;

  const takeover = { ...queued, reviewToken: '4'.repeat(64), adoption: { mode: 'take-ownership' } };
  ui.state.busy = true; ui.reviewDownloadedPackage(takeover); ui.state.busy = false;
  await ui.acceptSnapshot(snapshot({}, [{ ...mod, imported: true }]));
  assert(ui.state.noticeQueue.some((notice) => notice.review?.reviewToken === takeover.reviewToken),
    'Having a library copy cannot silently authorize ownership of an external installed file');
  await ui.acceptSnapshot(snapshot({}, [{ ...mod, imported: true, enabled: true, owned: true }]));
  assert(!ui.state.noticeQueue.some((notice) => notice.review?.reviewToken === takeover.reviewToken));
  scenarios++;

  ui.resetNotices();
  ui.collectActionNotices(snapshot({ diagnostics: [{ code: 'owned-file-changed', modId: mod.id, message: 'Synthetic changed file' }] }));
  await ui.showNextNotice(true);
  assert(node('#modal').open);
  ui.collectActionNotices(snapshot({ diagnostics: [] }));
  assert.equal(node('#modal').open, false, 'Resolved findings close their old automatic summary');
  scenarios++;

  const bridge = context.window.chrome.webview, originalPost = bridge.postMessage;
  let held;
  bridge.postMessage = (request) => {
    if (request.action === 'downloadCandidates') { held = request; nativeRequests.push(request); }
    else originalPost.call(bridge, request);
  };
  ui.state.downloadOffers = [{ id: 'old-offer', name: 'Old.zip' }];
  const poll = ui.checkDownloads();
  for (let count = 0; !held && count < 20; ++count) await new Promise(setImmediate);
  assert(held);
  await ui.acceptSnapshot(snapshot({ accountContext: 'other-installation-' + platform }));
  assert.equal(ui.state.downloadOffers.length, 0);
  reply({ requestId: held.requestId, ok: true, data: { candidates: [{ id: 'stale-result', name: 'Wrong installation.zip', sha256: B }] } });
  assert.equal(await poll, false);
  assert.equal(ui.state.downloadOffers.length, 0, 'An old discovery response cannot repopulate the new installation');
  bridge.postMessage = originalPost;
  scenarios++;

  await ui.acceptSnapshot(snapshot());
  actionFixtures.set('downloadCandidates', { candidates: [{ id: 'pending', name: 'Large.zip' }], contentCheckPending: true });
  await ui.checkDownloads();
  const continuation = [...timers.values()].find((timer) => timer.delay === 250);
  assert(continuation, 'Bounded native hashing requests another small job');
  const beforeHidden = nativeRequests.filter((request) => request.action === 'downloadCandidates').length;
  context.document.hidden = true;
  await continuation.callback();
  assert.equal(nativeRequests.filter((request) => request.action === 'downloadCandidates').length, beforeHidden,
    'Hiding the loader pauses hashing rather than retaining an open file or busy loop');
  context.document.hidden = false; ui.cancelDownloadContinuation();
  scenarios++;

  const missing = { ...mod, cacheRecorded: true, cacheIntegrity: 'missing', canRemoveLibrary: true };
  ui.state.snapshot = ui.normalizeSnapshot(snapshot({}, [missing]));
  assert(ui.renderModRowActions(missing).includes('data-remove-mod='), 'A missing library copy still has a visible removal action');
  ui.showModDetails(mod.id);
  assert(node('#mod-details-panel').innerHTML.includes('id="detail-remove"'));
  actionFixtures.set('snapshot', snapshot());
  const beforeRemove = nativeRequests.length;
  assert(ui.reviewRemoveMod(mod.id));
  assert(node('#modal-content').innerHTML.includes('Missing files are not needed'));
  await node('#confirm-action').onclick();
  const removal = nativeRequests.slice(beforeRemove).filter((request) => ['disable', 'remove'].includes(request.action));
  assert.equal(removal.length, 1); assert.equal(removal[0].action, 'remove');
  assert.equal(removal[0].id, mod.id);
  assert(!('path' in removal[0]), 'Only native mod identity is submitted for removal');
  scenarios++;

  actionFixtures.set('downloadCandidates', { candidates: [
    { id: 'removed-raw', name: 'Synthetic mod.pak', sha256: A },
    { id: 'removed-source', name: 'Original download.zip', sha256: C },
    { id: 'different-update', name: 'Synthetic mod.pak', sha256: B }
  ] });
  const beforeDismiss = nativeRequests.length;
  await ui.checkDownloads();
  const dismissals = nativeRequests.slice(beforeDismiss).filter((request) => request.action === 'downloadDismiss');
  assert.deepEqual(dismissals.map((request) => request.id).sort(), ['removed-raw', 'removed-source']);
  assert(dismissals.every((request) => [A, C].includes(request.sha256) && !('path' in request)),
    'Intentional removal dismisses only exact checked content, with the digest rechecked natively');
  assert.deepEqual(Array.from(ui.state.downloadOffers, (offer) => offer.id), ['different-update'],
    'Removing a mod does not immediately offer its original ZIP again, or hide a different update with the same name');
  scenarios++;

  await ui.acceptSnapshot(snapshot({}, [{ ...mod, imported: true }]));
  await ui.acceptSnapshot(snapshot({}, [{ ...mod, cacheRecorded: true, cacheIntegrity: 'missing' }]));
  const beforeReimport = nativeRequests.length;
  await ui.checkDownloads();
  assert(!nativeRequests.slice(beforeReimport).some((request) => request.action === 'downloadDismiss'),
    'Explicitly reimporting clears the pending removal intent');
  assert(ui.state.downloadOffers.some((offer) => offer.id === 'removed-source'),
    'A later accidentally missing library can offer its repair source again');
  scenarios++;

  const changed = { ...missing, owned: true, status: 'changed', canRemoveLibrary: false };
  ui.state.snapshot = ui.normalizeSnapshot(snapshot({}, [changed]));
  assert(!ui.renderModRowActions(changed).includes('data-remove-mod='));
  assert.equal(ui.reviewRemoveMod(mod.id), false, 'A replaced live file requires its separate ownership review');
  ui.state.snapshot = ui.normalizeSnapshot(snapshot({}, [missing]));
  ui.reviewRemoveMod(mod.id);
  ui.state.snapshot.mods[0].sha256 = B;
  const beforeChanged = mutations().length;
  await node('#confirm-action').onclick();
  assert.equal(mutations().length, beforeChanged, 'A confirmation cannot remove changed package identity');
  scenarios += 2;

  const live = { ...mod, enabled: true, owned: true, status: 'enabled', canRestoreLibraryCopy: true };
  ui.state.snapshot = ui.normalizeSnapshot(snapshot({}, [live]));
  actionFixtures.set('snapshot', snapshot({}, [{ ...live, imported: true, canRestoreLibraryCopy: false }]));
  ui.showModDetails(mod.id);
  assert(node('#mod-details-panel').innerHTML.includes('Restore library copy'));
  const beforeRestore = mutations().length;
  await node('#detail-restore-library').onclick();
  assert(nativeRequests.some((request) => request.action === 'restoreLibraryCopy' && request.id === mod.id));
  assert.equal(mutations().length, beforeRestore, 'Restoring a library copy does not toggle the live mod');
  scenarios++;
  ui.cancelDownloadContinuation();
}

async function importConfirmationGuards(platform) {
  for (const kind of ['single', 'batch']) {
    for (const condition of ['closed', 'replacement', 'installation', 'double', 'success']) {
      const { ui, node, context, nativeRequests, actionFixtures, mod, snapshot } = environment(platform),
        token = kind + '-' + condition, action = kind === 'single' ? 'confirmReviewedImport' : 'batchImportConfirm';
      const inputs = [{ value: 'one', checked: true, disabled: false }, { value: 'two', checked: true, disabled: false }],
        originalQuery = context.document.querySelectorAll;
      context.document.querySelectorAll = (selector) => selector === '#modal [name="batch-import-item"]' ? inputs : originalQuery(selector);
      actionFixtures.set('snapshot', snapshot({}, [{ ...mod, imported: true }]));
      actionFixtures.set(action, kind === 'single' ? {} : { batchResult: true, importedCount: 2, items: [
        { itemId: 'one', status: 'imported', name: 'One' }, { itemId: 'two', status: 'imported', name: 'Two' }] });
      if (kind === 'single') ui.reviewDownloadedPackage({ reviewRequired: true, reviewToken: token, source: 'local-import', mod });
      else ui.reviewBatchImport({ batchReview: true, batchToken: token, items: inputs.map((input) => ({ itemId: input.value, status: 'ready', name: input.value, bytes: 1 })) });
      const confirm = node(kind === 'single' ? '#review-import-confirm' : '#batch-import-confirm').onclick;
      if (condition === 'closed') node('#modal').close();
      if (condition === 'replacement') ui.showChangelog('1.0.7');
      if (condition === 'installation') ui.state.snapshot.settings.gamePath = '/Different/Installation';
      if (condition === 'double') { const first = confirm(); await confirm(); await first; }
      else await confirm();
      const committed = nativeRequests.filter((request) => request.action === action);
      assert.equal(committed.length, ['success', 'double'].includes(condition) ? 1 : 0,
        `${platform}/${kind}/${condition}: only one current open review can commit`);
      if (condition === 'replacement') assert(node('#modal').open && node('#modal-content').innerHTML.includes('changelog'));
      if (condition === 'closed') {
        ui.handleModalClosed();
        assert(nativeRequests.some((request) => request.action === (kind === 'single' ? 'discardReviewedImport' : 'batchImportDiscard') &&
          request[kind === 'single' ? 'reviewToken' : 'batchToken'] === token), 'Cancel still discards the native review token');
      }
      context.document.querySelectorAll = originalQuery; ui.cancelDownloadContinuation(); scenarios++;
    }
  }
  const { ui, node, context, nativeRequests, actionFixtures, snapshot, reply } = environment(platform),
    inputs = [{ value: 'one', checked: true, disabled: false }, { value: 'two', checked: true, disabled: false }],
    query = context.document.querySelectorAll;
  context.document.querySelectorAll = (selector) => selector === '#modal [name="batch-import-item"]' ? inputs : query(selector);
  actionFixtures.set('snapshot', snapshot());
  ui.reviewBatchImport({ batchReview: true, batchToken: 'deferred-result', items: inputs.map((input) => ({ itemId: input.value, status: 'ready', name: input.value, bytes: 1 })) });
  const bridge = context.window.chrome.webview, originalPost = bridge.postMessage; let held;
  bridge.postMessage = (request) => {
    if (request.action === 'batchImportConfirm') { held = request; nativeRequests.push(request); }
    else originalPost.call(bridge, request);
  };
  const pending = node('#batch-import-confirm').onclick();
  for (let n = 0; n < 50 && !held; ++n) await new Promise(setImmediate);
  assert(held); ui.showChangelog('1.0.7');
  reply({ requestId: held.requestId, ok: true, data: { batchResult: true, importedCount: 2,
    items: [{ itemId: 'one', status: 'imported', name: 'One' }, { itemId: 'two', status: 'imported', name: 'Two' }] } });
  await pending;
  assert(node('#modal').open && node('#modal-content').innerHTML.includes('changelog'), 'Import completion cannot replace a newer dialog');
  assert(ui.state.noticeQueue.some((notice) => notice.type === 'batch-result'), 'Completed per-file results remain available');
  bridge.postMessage = originalPost; node('#modal').close(); ui.handleModalClosed();
  await ui.showNextNotice(true);
  assert(node('#modal-content').innerHTML.includes('Import results'), 'Queued completed results can be reopened');
  context.document.querySelectorAll = query; ui.cancelDownloadContinuation(); scenarios++;
}

(async () => {
  for (const platform of ['windows', 'linux']) { await run(platform); await importConfirmationGuards(platform); }
  console.log(`Library notice/removal lifecycle checks passed (${scenarios} Windows/Linux scenarios).`);
})().catch((error) => { console.error(error); process.exitCode = 1; });
