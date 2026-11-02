'use strict';

// Held native replies exercise installation switches without a running loader,
// native filesystem access, account or network. Reuses only the common harness.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
let harness = fs.readFileSync(path.join(__dirname, 'ui_smoke.js'), 'utf8');
const boundary = harness.indexOf('async function testProfileCreation()');
assert(boundary > 0);
harness = harness.slice(0, boundary).replace('state, normalizeSnapshot, render,',
  'pollNexusLogin, bindProfileRows, confirmModal, rememberUndo, undoAction, undoLastChange, flushFilesystemRefresh, scheduleFilesystemRefresh, setLastRefreshAt: (value) => { lastRefreshAt = value; }, state, normalizeSnapshot, render,');
const { ui, node, context, nativeRequests, actionFixtures, fixture, reply } =
  new Function('require', '__dirname', harness + '\nreturn {ui,node,context,nativeRequests,actionFixtures,fixture,reply: (data) => nativeMessageListener({data})};')(require, __dirname);
const bridge = context.window.chrome.webview, post = bridge.postMessage;

function reset(platform, view = 'overview') {
  const snapshot = { ...fixture, platform, edition: 'local', accountContext: `old-${platform}`,
    gameRunning: false, settings: { ...fixture.settings, autoCheckUpdates: false } };
  Object.assign(ui.state, { preview: false, loading: false, busy: false, view,
    snapshot: ui.normalizeSnapshot(snapshot), directories: { locations: [], startup: {} },
    diagnosticReport: { checks: [] }, diagnosticsLoading: false, diagnosticStale: false,
    portableProfiles: null, profilesLoading: false, profileSession: { active: false }, nativeDropActivity: null,
    update: { lastCheck: Date.now() }, catalogUpdate: { lastCheck: Date.now() }, nexus: { connected: false } });
  actionFixtures.set('snapshot', snapshot);
  actionFixtures.set('nexusStatus', { connected: false });
  ui.restoreWatchState({ dirty: false, running: false });
  node('#modal').close();
  return { ...snapshot, accountContext: `new-${platform}` };
}
function hold(action) {
  let held;
  bridge.postMessage = (request) => {
    if (request.action === action && !held) { held = request; nativeRequests.push(request); }
    else post.call(bridge, request);
  };
  return {
    wait: async () => { for (let i = 0; i < 50 && !held; i++) await new Promise(setImmediate); assert(held, `${action} must be pending; recent actions: ${nativeRequests.slice(-8).map(r=>r.action).join(", ")}; readiness: ${ui.importEnableBlockReason?.()}`); },
    release: (data, error) => { reply({ requestId: held.requestId, ok: !error, ...(error ? { error } : { data }) }); bridge.postMessage = post; }
  };
}
async function main() {
  for (const platform of ['windows', 'linux']) {
    {
      reset(platform, 'folders');
      const path = platform === 'linux' ? '/mnt/Games/Days Gone' : 'C:/Games/Days Gone';
      ui.state.snapshot.settings.gamePath = path;
      ui.state.directories = { installations: [{ path, label: 'Current installation' },
        { path: path.toLowerCase(), label: 'Different case' }], locations: [], duplicates: [], startup: {} };
      const markup = ui.renderFolders(), selected = (markup.match(/selected-badge/g) || []).length;
      assert.equal(selected, platform === 'linux' ? 1 : 2, 'Linux installation selection preserves filesystem case; Windows matches case-insensitively');
      if (platform === 'linux') assert(markup.includes('data-use-installation="/mnt/games/days gone"'), 'A distinct Linux installation remains selectable');
    }
    for (const failed of [false, true]) {
      const next = reset(platform, 'folders'), pending = hold('directoriesScan');
      ui.restoreWatchState({ dirty: true, running: false });
      const work = ui.flushFilesystemRefresh(); await pending.wait();
      await ui.acceptSnapshot(next);
      ui.state.directories = { locations: [{ id: 'managedPaks', path: 'new installation path' }] };
      node('#footer-status').textContent = 'Current installation status';
      pending.release({ locations: [{ id: 'managedPaks', path: 'old installation path' }] }, failed ? 'old scan failure' : null);
      await work;
      assert.equal(ui.state.directories.locations[0].path, 'new installation path', 'Watcher ignores old directory data');
      assert.equal(node('#footer-status').textContent, 'Current installation status', 'Watcher ignores old success and failure status');
    }
    {
      const next = reset(platform, 'diagnostics'), pending = hold('diagnosticsRun');
      ui.restoreWatchState({ dirty: true, running: false });
      const work = ui.flushFilesystemRefresh(); await pending.wait();
      await ui.acceptSnapshot(next);
      const current = { checks: [{ id: 'current-report' }], planToken: 'current-token' };
      ui.state.diagnosticReport = current; ui.state.diagnosticsLoading = true;
      pending.release({ checks: [{ id: 'old-report' }], planToken: 'old-token' }); await work;
      assert.equal(ui.state.diagnosticReport, current, 'Watcher cannot replace current findings or repair tokens');
      assert.equal(ui.state.diagnosticsLoading, true, 'Old watcher completion cannot unlock a newer diagnostic read');
    }
    for (const failed of [false, true]) {
      const next = reset(platform), pending = hold('snapshot');
      const work = ui.refresh({ quiet: true, background: true }); await pending.wait();
      await ui.acceptSnapshot(next); ui.state.scanFailed = true;
      pending.release({ ...fixture, accountContext: `old-${platform}` }, failed ? 'old snapshot failure' : null);
      assert.equal(await work, false);
      assert.equal(ui.state.snapshot.accountContext, next.accountContext, 'An old refresh snapshot never switches the installation back');
      assert.equal(ui.state.scanFailed, true, 'An old refresh result never clears current scan status');
    }
    for (const failed of [false, true]) {
      const next = reset(platform), pending = hold('snapshot');
      ui.setLastRefreshAt(0);
      const work = ui.backgroundCheck(); await pending.wait();
      await ui.acceptSnapshot(next); ui.state.scanFailed = false;
      pending.release({ ...fixture, installed: [{ id: 'stale-file' }] }, failed ? 'old background failure' : null);
      assert.equal(await work, false);
      assert.equal(ui.state.scanFailed, false, 'An old background result never marks a different installation failed');
      assert.equal(ui.watchState().dirty, false, 'An old background reply cannot schedule a current installation refresh');
    }
    {
      const next = reset(platform), pending = hold('nexusStatus');
      const first = ui.acceptSnapshot(next); await pending.wait();
      const latest = { ...next, accountContext: `latest-${platform}` };
      actionFixtures.set('nexusStatus', { connected: false, name: 'Current account status' });
      await ui.acceptSnapshot(latest);
      pending.release({ connected: true, name: 'Old account status' }); await first;
      assert.equal(ui.state.nexus.name, 'Current account status', 'A previous installation account reply never overwrites current status');
    }
    {
      const next = reset(platform), pending = hold('diagnosticsRun');
      const work = ui.loadDiagnosticReport(); await pending.wait();
      await ui.acceptSnapshot(next);
      assert.equal(ui.state.diagnosticsLoading, false, 'Switching installation resets the previous loading flag');
      ui.state.diagnosticsLoading = true;
      pending.release({ checks: [], planToken: 'stale' }); await work;
      assert.equal(ui.state.diagnosticsLoading, true, 'A previous direct diagnostic reply never clears newer loading');
      assert.equal(ui.state.diagnosticReport, null);
    }
    {
      const next = reset(platform);
      ui.state.snapshot.mods = fixture.mods.slice(0, 2).map((mod, index) => ({ ...mod, imported: true, enabled: index === 0 }));
      ui.rememberUndo();
      const oldToast = ui.undoAction();
      const different = { ...next, mods: ui.state.snapshot.mods.map((mod, index) => ({ ...mod, enabled: index === 1 })) };
      await ui.acceptSnapshot(different);
      const before = nativeRequests.length;
      assert.equal(await ui.undoLastChange(), false, 'The library undo is cleared after switching installation');
      assert.equal(await oldToast.run(), false, 'A surviving toast cannot restore an old installation setup');
      assert.equal(nativeRequests.length, before, 'Cross-installation undo sends no native request');
      ui.rememberUndo(); const olderToast = ui.undoAction();
      ui.rememberUndo();
      assert.equal(await olderToast.run(), false, 'An earlier toast cannot consume a newer change in the same installation');
    }
    {
      const next = reset(platform);
      const mods = fixture.mods.slice(0, 2).map((mod, index) => ({ ...mod, imported: true, enabled: index === 0 }));
      ui.state.snapshot.mods = mods; ui.rememberUndo();
      ui.state.snapshot.mods = mods.map((mod, index) => ({ ...mod, enabled: index === 1 }));
      const pending = hold('disable'), before = nativeRequests.length;
      const work = ui.undoLastChange(); await pending.wait();
      await ui.acceptSnapshot({ ...next, mods });
      pending.release({ completed: true });
      assert.equal(await work, false, 'An in-progress undo stops after an installation switch');
      assert.equal(nativeRequests.slice(before).filter((request) => request.action === 'enable').length, 0, 'Undo never sends its next mutation against a different installation');
    }
    {
      reset(platform);
      const mods = fixture.mods.slice(0, 2).map((mod, index) => ({ ...mod, imported: true, enabled: index === 0 }));
      ui.state.snapshot.mods = mods; ui.rememberUndo();
      const undo = ui.undoAction();
      ui.state.snapshot.mods = mods.map((mod, index) => ({ ...mod, enabled: index === 1 }));
      actionFixtures.set('snapshot', { ...ui.state.snapshot, mods });
      const before = nativeRequests.length;
      assert.equal(await undo.run(), true, 'The matching current-installation toast still restores its setup');
      assert.deepEqual(nativeRequests.slice(before).filter((request) => ['enable', 'disable'].includes(request.action)).map(({ action, id }) => ({ action, id })),
        [{ action: 'disable', id: mods[1].id }, { action: 'enable', id: mods[0].id }]);
    }
    {
      const next = reset(platform), control = node('delete-profile-control'), query = context.document.querySelectorAll;
      control.dataset.portableDelete = 'Same profile name';
      context.document.querySelectorAll = (selector) => selector === '[data-portable-delete]' ? [control] : [];
      ui.bindProfileRows(); control.onclick(); context.document.querySelectorAll = query;
      await ui.acceptSnapshot(next);
      const before = nativeRequests.length;
      assert.equal(await node('#confirm-action').onclick(), false);
      assert.equal(nativeRequests.length, before, 'A stale profile-delete confirmation sends no request for the new installation');
      assert(!node('#modal').open);
    }
    {
      const next = reset(platform), mod = { ...fixture.mods[0], imported: true, owned: true, enabled: true };
      ui.state.snapshot.mods = [mod]; ui.showModDetails(mod.id); node('#detail-remove').onclick();
      await ui.acceptSnapshot({ ...next, mods: [mod] });
      const before = nativeRequests.length;
      assert.equal(await node('#confirm-action').onclick(), false);
      assert.equal(nativeRequests.length, before, 'A stale mod-remove confirmation sends no disable/remove requests');
    }
    {
      const next = reset(platform), mod = { ...fixture.mods[0], imported: true, owned: true, enabled: true };
      ui.state.snapshot.mods = [mod]; ui.showModDetails(mod.id); node('#detail-remove').onclick();
      const pending = hold('disable'), before = nativeRequests.length;
      const work = node('#confirm-action').onclick(); await pending.wait();
      actionFixtures.set('snapshot', { ...next, mods: [mod] });
      await ui.acceptSnapshot({ ...next, mods: [mod] });
      pending.release({ completed: true }); await work;
      assert.equal(nativeRequests.slice(before).filter((request) => request.action === 'remove').length, 0, 'An in-progress mod removal never removes a package from a different installation');
    }
    {
      const next = reset(platform); let called = 0;
      ui.confirmModal('Current action', '<p>Synthetic</p>', 'Confirm', () => { called++; });
      node('#confirm-action').onclick(); assert.equal(called, 1, 'Current-installation confirmation still works');
      ui.confirmModal('Global action', '<p>Synthetic</p>', 'Confirm', () => { called++; }, false, { contextIndependent: true });
      await ui.acceptSnapshot(next);
      node('#confirm-action').onclick(); assert.equal(called, 2, 'Explicitly app-global confirmations survive installation changes');
    }
    for (const action of ['disable', 'enable']) {
      const next = reset(platform), old = { ...fixture.mods[0], id: 'older-local', name: 'Synthetic Weapon v1', packageFile: 'SyntheticWeapon_v1_P.pak',
        imported: true, owned: true, enabled: true, sha256: 'a'.repeat(64) },
        added = { ...old, id: 'newer-local', name: 'Synthetic Weapon v2', packageFile: 'SyntheticWeapon_v2_P.pak', enabled: false, sha256: 'b'.repeat(64) };
      ui.state.snapshot.mods = [old, added];
      ui.state.snapshot.settings.gamePath = 'C:/Synthetic/Days Gone';
      actionFixtures.set('snapshot', { ...ui.state.snapshot, mods: [old, added] });
      ui.reviewDownloadedPackage({ reviewRequired: true, reviewToken: 'synthetic-import', source: 'local-import', mod: added, conflicts: [], warnings: [] });
      const pending = hold(action), before = nativeRequests.length;
      const work = node('#review-import-replace').onclick(); await pending.wait();
      await ui.acceptSnapshot({ ...next, mods: [old, added] });
      pending.release({ completed: true }, action === 'enable' ? 'Synthetic enable refusal' : null);
      await work;
      const enables = nativeRequests.slice(before).filter((request) => request.action === 'enable');
      assert.equal(enables.length, action === 'enable' ? 1 : 0, 'Replacement stops before the next enable or rollback after switching installation');
      assert.equal(enables.some((request) => request.id === old.id), false, 'Replacement never re-enables an old package in the new installation');
      node('#modal').close();
    }
    for (const changeContext of [false, true]) {
      for (const failed of [false, true]) {
        const next = reset(platform); ui.state.snapshot.edition = 'reviewer';
        ui.connectNexus(); node('#nexus-api-key').value = 'synthetic-key-held-for-context-test';
        const pending = hold('nexusConnect'), work = node('#nexus-key-submit').onclick(); await pending.wait();
        assert.equal(node('#nexus-api-key').value, '', 'The key is cleared before a pending native request');
        if (changeContext) await ui.acceptSnapshot({ ...next, edition: 'reviewer' });
        const currentAccount = { connected: false, name: 'Current account state' }; ui.state.nexus = currentAccount;
        ui.showChangelog('1.0.7'); const currentDialog = node('#modal-content').innerHTML;
        pending.release({ connected: true, name: 'Old key result' }, failed ? 'Old key failure' : null); await work;
        assert.equal(ui.state.nexus, currentAccount, 'A stale key reply does not replace current account state');
        assert(node('#modal').open && node('#modal-content').innerHTML === currentDialog, 'A stale key reply cannot close or replace a newer dialog');
      }
    }
    {
      reset(platform); ui.state.snapshot.edition = 'reviewer'; ui.connectNexus();
      actionFixtures.set('nexusBeginLogin', { pending: true });
      await node('#nexus-login-start').onclick();
      const pending = hold('nexusPollLogin'), work = ui.pollNexusLogin(); await pending.wait();
      ui.connectNexus(); await node('#nexus-login-start').onclick();
      const currentDialog = node('#modal-content').innerHTML;
      pending.release({ connected: true, name: 'Obsolete browser attempt' }); await work;
      assert(!ui.state.nexus.connected && node('#modal').open && node('#modal-content').innerHTML === currentDialog,
        'An old browser poll cannot finish a new login attempt');
      actionFixtures.set('nexusPollLogin', { connected: true, name: 'Current browser attempt' }); await ui.pollNexusLogin();
      assert(ui.state.nexus.connected && ui.state.nexus.name === 'Current browser attempt', 'The current browser attempt still completes');
    }
    {
      reset(platform); ui.state.snapshot.edition = 'reviewer'; ui.connectNexus();
      const pending = hold('nexusBeginLogin'), work = node('#nexus-login-start').onclick(); await pending.wait();
      ui.connectNexus(); const currentDialog = node('#modal-content').innerHTML,
        cancellations = nativeRequests.filter((request) => request.action === 'nexusCancelLogin').length;
      pending.release({ connected: true, name: 'Closed browser attempt' }); await work;
      assert(!ui.state.nexus.connected && node('#modal').open && node('#modal-content').innerHTML === currentDialog,
        'A late browser-start reply cannot close a newer dialog');
      assert.equal(nativeRequests.filter((request) => request.action === 'nexusCancelLogin').length, cancellations,
        'A late browser-start reply never cancels the replacement attempt');
    }
  }
  console.log('Installation-context async, path-case handling, undo, confirmation and isolated Nexus-login regressions passed (50 Windows/Linux scenarios).');
}
main().catch((error) => { console.error(error); process.exitCode = 1; }).finally(() => { bridge.postMessage = post; });
