const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');

const root = path.join(__dirname, '..');
const html = fs.readFileSync(path.join(root, 'main/web/calibration.html'), 'utf8');
const original = html.split('<script>')[1].split('</script>')[0]
  .split("addEventListener('beforeunload'")[0];
const shim = fs.readFileSync(path.join(root,
  'android_lock_beacon/app/src/main/assets/lock_wizard_speech.js'), 'utf8');
const timers = [], spoken = [];
let cancelled = 0;
const c = {
  instruction: {textContent: 'Запуск…'}, status: {textContent: '', className: ''},
  bar: {style: {}}, repeat: {disabled: false}, cancelBtn: {textContent: ''},
  fetch: async () => ({ok: true, json: async () => ({}), text: async () => ''}),
  setTimeout: fn => timers.push(fn), setInterval: () => 1, clearInterval() {},
  addEventListener() {}, URLSearchParams, Promise, Error,
  BleLockWizardSpeech: {
    speak: (text, token) => spoken.push({text, token}), cancel: () => cancelled++
  }, closed: false, close() {}, location: {replace() {}}
};
c.document = {getElementById: id => id === 'status' ? c.status : {className: ''}};
c.window = c;
vm.createContext(c);
vm.runInContext(original, c);
vm.runInContext('setStage(0)', c);
vm.runInContext(shim, c);
assert.equal(spoken.length, 1);
assert.equal(vm.runInContext('collecting', c), false);
timers.forEach(fn => fn());
assert.equal(vm.runInContext('collecting', c), false, 'Old browser timer must not start measurements');
c.onBleLockSpeechDone(spoken[0].token, null);
assert.equal(vm.runInContext('collecting', c), true);
vm.runInContext('repeatInstruction()', c);
assert.equal(spoken.length, 2);
assert.equal(vm.runInContext('collecting', c), false);
c.onBleLockSpeechDone(spoken[0].token, null);
assert.equal(vm.runInContext('collecting', c), false, 'Stale completion must not start measurements');
c.onBleLockSpeechDone(spoken[1].token, null);
assert.equal(vm.runInContext('collecting', c), true);
vm.runInContext('repeatInstruction()', c);
c.onBleLockSpeechDone(spoken[2].token, 'TTS unavailable');
assert.equal(vm.runInContext('collecting', c), false);
assert.equal(c.status.textContent, 'TTS unavailable');
vm.runInContext('expired()', c);
assert.equal(cancelled, 1);
c.onBleLockSpeechDone(spoken[2].token, null);
assert.equal(vm.runInContext('collecting', c), false);
vm.runInContext('finished=true; setStage(2)', c);
const finalSpeech = spoken[3];
assert.ok(finalSpeech);
vm.runInContext('expired()', c);
assert.equal(cancelled, 1, 'Stale tick after save must not cancel the final instruction');
let closedAfterSpeech = false;
c.close = () => { closedAfterSpeech = true; };
c.onBleLockSpeechDone(finalSpeech.token, null);
assert.equal(closedAfterSpeech, true, 'Successful final speech still closes the wizard');
vm.runInContext(shim, c);
assert.equal(spoken.length, 4, 'Installing the adapter twice must not repeat speech');
console.log('PASS: speech completion, repeat, stale timers/callbacks, error, real expiry, stale expiry after save');
