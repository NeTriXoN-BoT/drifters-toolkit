'use strict';

// Shared production UI with synthetic native snapshots and a controlled clock.
// No game/account files, native application, network or real timers are used.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
let harness = fs.readFileSync(path.join(__dirname, 'ui_smoke.js'), 'utf8');
const boundary = harness.indexOf('async function testChangelogHistory()');
assert(boundary > 0);
harness = harness.slice(0, boundary);
const { ui, node, context, timers, fixture } = new Function('require', '__dirname',
  harness + '\nreturn { ui, node, context, timers, fixture };')(require, __dirname);
const originalDate = context.Date;
let now = 1000;
context.Date = class extends originalDate { static now() { return now; } };
context.performance = { now: () => now };
const card = node('#operation-activity');
card.contains = (target) => target === card || target === node('#activity-dismiss');
let focused = true;
context.document.hasFocus = () => focused;

function visible() {
  focused = true;
  context.document.hidden = false;
  context.document.activeElement = context.document.body;
  ui.state.nativeWindowHidden = false;
  context.document.documentElement.classList.remove('app-hidden');
  node('#modal').close();
  card.matches = () => false;
}
function finish(outcome = 'complete', extra = {}) {
  visible();
  ui.state.activity = { running: false, outcome, message: outcome === 'cancelled' ? 'Cancelled' : 'Finished', ...extra };
  ui.renderActivity();
  return ui.state.activity;
}
function currentTimer() {
  const countdown = ui.activityDismissalState();
  const timer = countdown && timers.get(countdown.timer);
  assert(timer, 'A timed notice has one pending deadline/visual timeout');
  return { countdown, timer };
}
function fire(milliseconds) {
  const { countdown, timer } = currentTimer();
  timers.delete(countdown.timer);
  now += milliseconds;
  timer.callback();
}
function ringOffset(expected, message) {
  assert(Math.abs(Number(node('#activity-countdown-ring').style['--activity-countdown-offset']) - expected) < 1e-9,
    message || 'The ring uses the same elapsed deadline as the displayed seconds');
}
const modes = {
  blurred: () => { focused = false; },
  'document-hidden': () => { context.document.hidden = true; },
  'native-minimized': () => { ui.state.nativeWindowHidden = true; },
  'native-hidden-class': () => { context.document.documentElement.classList.add('app-hidden'); }
};
let checks = 0;
for (const platform of ['windows', 'linux']) {
  ui.state.snapshot = ui.normalizeSnapshot({ ...fixture, platform });
  ui.state.preview = true;
  for (const outcome of ['complete', 'cancelled']) {
    for (const [mode, background] of Object.entries(modes)) {
      finish(outcome);
      fire(2000);
      background();
      ui.syncActivityDismissal();
      const visibleWithoutFocus = mode === 'blurred';
      assert.equal(currentTimer().timer.delay, visibleWithoutFocus ? 1000 : 8000,
        `${platform}/${outcome}/${mode}: visible seconds update; hidden windows retain one expiry timeout`);
      if (visibleWithoutFocus) {
        const announcement = node('#activity-live').textContent;
        for (let seconds = 7; seconds >= 1; seconds--) {
          fire(1000);
          assert.equal(node('#activity-countdown-seconds').textContent, String(seconds),
            `${platform}/${outcome}: visible-unfocused seconds keep draining`);
          ringOffset(100 * (1 - seconds / 10), `${platform}/${outcome}: ring and number use the same remaining time`);
          assert.equal(node('#activity-live').textContent, announcement, 'Visible ticks do not repeat live announcements');
        }
        fire(1000);
      } else fire(8000);
      assert.equal(ui.state.activity, null, `${platform}/${outcome}/${mode}: expires without returning to the foreground`);
      assert.equal(ui.activityDismissalState(), null);
      assert.equal(card.hidden, true);
      checks++;
    }
  }
  finish();
  focused = false;
  ui.syncActivityDismissal();
  now += 60000;
  visible();
  ui.syncActivityDismissal();
  assert.equal(ui.state.activity, null, 'A browser-delayed callback cannot leave expired content visible after restore');
  checks++;

  for (const interaction of ['hover', 'keyboard', 'dialog']) {
    finish();
    fire(2000);
    if (interaction === 'hover') { card.matches = () => true; card.onpointerenter(); }
    if (interaction === 'keyboard') context.document.activeElement = node('#activity-dismiss');
    if (interaction === 'dialog') node('#modal').open = true;
    ui.syncActivityDismissal();
    assert.equal(ui.activityDismissalState().timer, null, `${interaction}: deliberate foreground interaction preserves reading time`);
    now += 60000;
    ui.syncActivityDismissal();
    assert.equal(node('#activity-countdown-seconds').textContent, '8');
    focused = false;
    ui.syncActivityDismissal();
    assert.equal(currentTimer().timer.delay, 1000, `${interaction}: stale interaction does not freeze a visible unfocused counter`);
    fire(8000);
    assert.equal(ui.state.activity, null);
    checks++;
  }

  finish();
  focused = false;
  ui.syncActivityDismissal();
  now += 4000;
  visible();
  ui.syncActivityDismissal();
  assert.equal(node('#activity-countdown-seconds').textContent, '6');
  assert.equal(currentTimer().timer.delay, 1000, 'Visible seconds resume with the actual remaining time');
  const old = currentTimer().timer;
  const oldClose = node('#activity-dismiss').onclick;
  ui.state.activity = { running: true, message: 'New operation' };
  ui.renderActivity();
  now += 60000;
  old.callback();
  assert.equal(ui.state.activity.message, 'New operation', 'A late expiry cannot dismiss a replacement operation');
  oldClose();
  assert.equal(ui.state.activity.message, 'New operation', 'A queued close from the old card cannot dismiss replacement work');
  checks++;

  for (const hide of Object.values(modes).slice(1)) {
    finish('complete', { message: 'Imported package successfully' });
    fire(2000);
    hide();
    focused = false;
    ui.syncActivityDismissal();
    assert.equal(currentTimer().timer.delay, 8000, 'Hidden notices retain one expiry timer');
    now += 2500;
    visible(); focused = false;
    ui.syncActivityDismissal();
    assert.equal(node('#activity-countdown-seconds').textContent, '6', 'Restoring catches up before the window receives focus');
    ringOffset(45, 'Restoring catches the ring up to the same elapsed deadline');
    assert.equal(currentTimer().timer.delay, 1000, 'Visible-unfocused ticks resume after restore');
    fire(1000);
    assert.equal(node('#activity-countdown-seconds').textContent, '5');
    context.document.documentElement.classList.add('reduce-motion');
    ui.syncActivityDismissal();
    fire(1000);
    assert.equal(node('#activity-countdown-seconds').textContent, '4', 'Reduced motion still updates unfocused numeric seconds');
    ringOffset(65);
    context.document.documentElement.classList.remove('reduce-motion');
    fire(3500);
    assert.equal(ui.state.activity, null, 'A delayed visible timer expires at the original wall-clock deadline');
    checks++;
  }

  finish('complete', { message: 'Save backup created' });
  fire(2000);
  ui.handleWindowVisibility({ data: { hidden: true } });
  assert.equal(currentTimer().timer.delay, 8000, 'The native hide hook switches to efficient expiry');
  now += 2500; focused = false;
  ui.handleWindowVisibility({ data: { hidden: false } });
  assert.equal(node('#activity-countdown-seconds').textContent, '6', 'Native restore redraws without needing a focus event');
  assert.equal(currentTimer().timer.delay, 1000);
  fire(5500);
  assert.equal(ui.state.activity, null);
  checks++;

  for (const extra of [{ outcome: 'failed' }, { requiresAttention: true }, { running: true }]) {
    finish('complete', extra);
    focused = false;
    ui.syncActivityDismissal();
    now += 60000;
    ui.syncActivityDismissal();
    assert(ui.state.activity);
    assert.equal(ui.activityDismissalState(), null, 'Errors/reviews/active work stay persistent in the background');
    checks++;
  }
}
delete context.performance;
finish('cancelled'); focused = false; ui.syncActivityDismissal();
fire(2500);
assert.equal(node('#activity-countdown-seconds').textContent, '8', 'Date clock fallback still refreshes visible-unfocused seconds');
fire(7500);
assert.equal(ui.state.activity, null);
checks++;
ui.state.activity = null;
ui.renderActivity();
visible();
const toast = node('deadline-test-toast');
const previousCreate = context.document.createElement;
context.document.createElement = () => toast;
toast.contains = (target) => target === toast;
context.document.activeElement = toast;
let before = new Set(timers.keys());
ui.toast('Timed result');
let added = [...timers].filter(([id]) => !before.has(id));
assert.equal(added.length, 1);
timers.delete(added[0][0]);
before = new Set(timers.keys());
added[0][1].callback();
assert.equal(toast.removed, false, 'Foreground toast focus keeps its existing interaction exception');
added = [...timers].filter(([id]) => !before.has(id));
assert.equal(added.length, 1);
focused = false;
timers.delete(added[0][0]);
before = new Set(timers.keys());
added[0][1].callback();
assert.equal(toast.removed, true, 'A toast cannot retry forever because its old close button retained focus in the background');
assert.deepEqual([...timers.keys()], [...before], 'Background expiry adds no retry timer');
context.document.createElement = previousCreate;
console.log(`Notice deadline tests passed (${checks} Windows/Linux scenarios plus foreground/background toast focus).`);
