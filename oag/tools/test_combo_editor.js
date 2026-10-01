// Run the shipped combo editor against a minimal DOM, without hardware/network.
const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '../firmware/src/diamond_wifi_portal.cpp'), 'utf8');
class Element {
  constructor() {
    this.children = []; this.options = []; this.fields = new Map();
    this.value = ''; this.classList = {toggle() {}};
    this.parentElement = {classList: this.classList};
  }
  set innerHTML(value) {
    this.children = []; this.options = [];
    if (value.includes('class=actionfields')) {
      for (const [name, initial] of Object.entries({sact:'', slife:'ms', sms:100, smode:'press', srel:0, releaselabel:''})) {
        const e = new Element(); e.value = initial; this.fields.set('.'+name, e);
      }
    }
  }
  querySelector(name) {
    if (!this.fields.has(name)) this.fields.set(name, new Element());
    return this.fields.get(name);
  }
  add(option) { this.options.push(option); }
  appendChild(child) { this.children.push(child); }
}
const elements = new Map();
const get = id => { if (!elements.has(id)) elements.set(id, new Element()); return elements.get(id); };
const context = vm.createContext({
  document: {createElement: () => new Element()}, $: get,
  Option: function(text, value) { this.text = text; this.value = value; },
});
for (const name of ['kGwcJs', 'kGwcEditorJs']) {
  const match = source.match(new RegExp('constexpr char '+name+'\\[\\] = R"JS\\(([\\s\\S]*?)\\)JS"'));
  assert(match, name); vm.runInContext(match[1], context);
}
const verify = select => {
  assert.equal(select.options.filter(o => /^c(18|19|2[0-5])$/.test(o.value)).length, 8);
  const r3 = select.options.findIndex(o => o.value === 'c10');
  assert.deepEqual(select.options.slice(r3+1, r3+9).map(o => o.value),
    ['c18','c19','c20','c21','c22','c23','c24','c25']);
  for (const option of select.options.slice(r3+1, r3+9)) assert.match(option.text, /الأنالوج الأيمن/);
};
verify(get('ctr'));
let rows = get('csteps').children.filter(e => e.className === 'actionrow');
assert.equal(rows.length, 8);
rows.forEach(row => verify(row.querySelector('.sact')));
context.saved = Array.from({length:8}, (_,i) => ({kind:3,logicalMask:2**(18+i),durationMs:200,intervalMs:750}));
vm.runInContext('buildEight(saved)', context);
rows = get('csteps').children.filter(e => e.className === 'actionrow');
rows.forEach((row,i) => {
  verify(row.querySelector('.sact'));
  assert.equal(row.querySelector('.sact').value, 'c'+(18+i));
  assert.equal(row.querySelector('.sms').value, 200);
  assert.equal(row.querySelector('.srel').value, 750);
});
assert.match(source, /OAG RS8 V2/);
assert.match(source, /app\.js\?v=oag-rs8-pro-v1/);
console.log('OAG RS8: all eight actions and trigger menus, order, Arabic labels and saved values PASS');
