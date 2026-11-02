'use strict';

// Reuse the synthetic DOM/native bridge harness without running the general UI suite.
// No installed application, game files, account, real browser or network is used here.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
let harness = fs.readFileSync(path.join(__dirname, 'ui_smoke.js'), 'utf8');
const boundary = harness.indexOf('async function testChangelogHistory()');
assert(boundary > 0, 'The shared UI harness boundary must exist');
harness = harness.slice(0, boundary).replace('state, normalizeSnapshot, render,',
  'recentActivity, recordRecentActivity, recordCompletedActivity, recentActivityMarkup, showRecentActivity, showKeyboardShortcuts, handleSearchShortcut, state, normalizeSnapshot, render,');
const { ui, node, context, nativeRequests, actionFixtures, actionErrors, fixture } =
  new Function('require', '__dirname', harness + '\nreturn { ui, node, context, nativeRequests, actionFixtures, actionErrors, fixture };')(require, __dirname);

function key(key, options = {}) {
  const event = { key, ...options, prevented: false, preventDefault() { this.prevented = true; } };
  const handled = ui.handleSearchShortcut(event);
  return { ...event, handled };
}
function closeDialog() { node('#modal').open = false; }

async function main() {
  ui.state.snapshot = ui.normalizeSnapshot(fixture);
  ui.state.loading = false;
  const requestsBefore = nativeRequests.length;
  let storageWrites = 0;
  context.window.localStorage = { getItem: () => null, setItem: () => { storageWrites++; } };
  assert(ui.recentActivityMarkup().includes('No results yet'));
  assert(ui.showRecentActivity());
  assert.equal(node('#recent-activity-clear').disabled, true);
  closeDialog();
  ui.recordRecentActivity('<img src=x onerror=bad()>', { label: '<script>bad()</script>', type: 'error', token: 'must-not-retain' });
  const markup = ui.recentActivityMarkup();
  assert(markup.includes('&lt;img') && markup.includes('&lt;script&gt;'));
  assert(!markup.includes('<img') && !markup.includes('<script>'));
  assert.deepEqual(Object.keys(ui.recentActivity[0]).sort(), ['at', 'label', 'message', 'type']);
  assert.equal(storageWrites, 0, 'Messages stay in memory without saving personal paths or tokens');

  ui.recentActivity.length = 0;
  let staleActionCalled = false;
  ui.toast('Backup finished', 'success', { label: 'Undo', run: () => { staleActionCalled = true; } });
  const complete = { running: false, outcome: 'complete', message: 'Backup finished', label: 'Backing up saves' };
  ui.recordCompletedActivity(complete);
  ui.recordCompletedActivity(complete);
  assert.equal(ui.recentActivity.length, 1, 'Toast and matching operation result share one entry');
  assert.equal(ui.recentActivity[0].label, 'Backing up saves');
  ui.state.activity = null;
  assert(ui.recentActivityMarkup().includes('Backup finished'), 'Dismissal never loses the retained summary');
  assert(!ui.recentActivityMarkup().includes('Undo'), 'Stale toast actions are never replayable');
  assert.equal(staleActionCalled, false);
  ui.recordCompletedActivity({ running: true, message: 'Not done yet' });
  assert.equal(ui.recentActivity.length, 1);
  for (const [outcome, requiresAttention, expected] of [['failed', false, 'error'], ['complete', true, 'warning'], ['cancelled', false, 'cancelled']]) {
    ui.recordCompletedActivity({ running: false, outcome, requiresAttention, message: `Fixture ${expected}` });
    assert.equal(ui.recentActivity[0].type, expected);
  }
  for (let i = 0; i < 65; i++) ui.recordRecentActivity(`Bounded ${i}`);
  assert.equal(ui.recentActivity.length, 50);
  assert.equal(ui.recentActivity[0].message, 'Bounded 64');
  ui.recordRecentActivity('x'.repeat(3000), { label: 'l'.repeat(600) });
  assert.equal(ui.recentActivity[0].message.length, 2048);
  assert.equal(ui.recentActivity[0].label.length, 256);
  assert(ui.showRecentActivity());
  const existing = node('#modal-content').innerHTML;
  assert.equal(ui.showRecentActivity(), false, 'Opening history cannot replace a current confirmation');
  assert.equal(node('#modal-content').innerHTML, existing);
  node('#recent-keyboard-help').onclick();
  assert(node('#modal-content').innerHTML.includes('Keyboard shortcuts'));
  node('#keyboard-activity-back').onclick();
  assert(node('#modal-content').innerHTML.includes('Recent activity'));
  node('#recent-activity-clear').onclick();
  assert.equal(ui.recentActivity.length, 0);
  assert(node('#recent-activity-content').innerHTML.includes('No results yet'));
  assert.equal(node('#recent-activity-clear').disabled, true);
  assert.equal(nativeRequests.length, requestsBefore, 'History and help never call the native bridge');
  closeDialog();

  for (const platform of ['windows', 'linux']) {
    ui.state.snapshot.platform = platform;
    for (const [view, selector] of [['overview', '#mod-search'], ['mods', '#mod-search'], ['profiles', '#profile-search'], ['help', '#guide-search'], ['settings', '#settings-search']]) {
      ui.state.view = view;
      context.document.activeElement = node('body');
      assert(key('/').handled);
      assert.equal(context.document.activeElement, node(selector), `${platform}: slash keeps search on ${view}`);
      assert(key('f', { ctrlKey: true }).prevented);
      assert.equal(ui.state.view, view);
      const input = node(selector);
      input.tagName = 'INPUT'; input.value = 'sample';
      let inputEvents = 0;
      input.oninput = () => { inputEvents++; };
      assert(key('Escape').handled);
      assert.equal(input.value, ''); assert.equal(inputEvents, 1);
      assert.equal(key('Escape').handled, false, 'An empty search leaves Escape available to close details');
      assert.equal(key('/').handled, false, 'Typing slash into search remains possible');
      assert.equal(key('?').handled, false, 'Typing question marks into search remains possible');
    }
    context.document.activeElement = node('editable');
    context.document.activeElement.isContentEditable = true;
    assert.equal(key('/').handled, false);
    assert.equal(key('?').handled, false);
    context.document.activeElement = node('body');
    assert.equal(key('/', { isComposing: true }).handled, false);
    assert.equal(key('/', { altKey: true }).handled, false);
    assert.equal(key('/', { defaultPrevented: true }).handled, false);
    node('#modal').open = true;
    assert.equal(key('f', { ctrlKey: true }).handled, false);
    assert.equal(key('?').handled, false);
    closeDialog();
    ui.state.view = 'folders';
    assert(key('/').handled);
    assert.equal(ui.state.view, 'mods', 'Pages without search retain the established library-search fallback');
    context.document.activeElement = node('body');
    assert(key('?').handled);
    assert(node('#modal-content').innerHTML.includes('Keyboard shortcuts'));
    closeDialog();
  }

  ui.recentActivity.length = 0;
  ui.state.view = 'mods'; ui.state.busy = false;
  actionFixtures.set('scan', { message: 'Synthetic scan finished' });
  await ui.run('scan', {}, { refresh: false, progress: 'Reading synthetic files…', success: 'Synthetic scan finished' });
  assert.equal(ui.recentActivity.length, 1, 'Actual run completion is captured once');
  assert.equal(ui.recentActivity[0].label, 'Reading synthetic files');
  assert.equal(ui.recentActivity[0].type, 'success');
  ui.recentActivity.length = 0;
  actionFixtures.set('scan', { count: 3 });
  let formatted = 0;
  await ui.run('scan', {}, { refresh: false, success: (result) => { formatted++; return `${result.count} synthetic files checked`; } });
  assert.equal(formatted, 1, 'Computed success text is evaluated once');
  assert.equal(ui.recentActivity.length, 1, 'Computed success and activity share one result');
  assert.equal(ui.recentActivity[0].message, '3 synthetic files checked');
  actionFixtures.set('scan', { cancelled: true });
  await ui.run('scan', {}, { refresh: false });
  assert.equal(ui.recentActivity[0].type, 'cancelled');
  actionErrors.set('scan', 'Synthetic denied operation');
  await ui.run('scan', {}, { refresh: false });
  assert.equal(ui.recentActivity[0].type, 'error');
  assert.equal(ui.recentActivity[0].message, 'Synthetic denied operation');
  actionErrors.delete('scan'); actionFixtures.delete('scan'); closeDialog();
  ui.state.nativeDropActivity = { running: true };
  ui.handleDroppedImport({ ok: false, error: 'Synthetic drop rejected' });
  assert.equal(ui.recentActivity[0].label, 'Dropped files', 'Native Linux drops retain their final result too');
  assert.equal(ui.recentActivity[0].type, 'error');
  assert.equal(storageWrites, 0, 'Activity tests never write a preference or persisted message');
  console.log('Recent activity and keyboard shortcut focused tests passed (Windows/Linux fixtures).');
}
main().catch((error) => { console.error(error); process.exitCode = 1; });
