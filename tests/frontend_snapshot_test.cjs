const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const ts = require('../frontend/node_modules/typescript');
const code = ts.transpileModule(fs.readFileSync('frontend/src/SnapshotView.ts', 'utf8'), {
    compilerOptions: { target: ts.ScriptTarget.ES2023, module: ts.ModuleKind.CommonJS },
}).outputText;
let pixels, draws = 0, savedBlob, link;
const arcs = [];
const status = { textContent: '' };
const button = { disabled: true, addEventListener(_, callback) { this.click = callback; } };
const canvas = { width: 1120, height: 560, getContext() {
    return { clearRect() {}, drawImage() { draws++; },
        save() {}, restore() {}, beginPath() {}, fill() {}, stroke() {},
        arc(x, y, radius) { arcs.push({ x, y, radius }); },
    };
} };
const classes = [];
const document = {
    title: '', body: { classList: { add(value) { classes.push(value); } } },
    querySelector(selector) {
        return ({ '#snapshot-heatmap': canvas, '#snapshot-status': status,
            '#download-snapshot': button })[selector];
    },
    createElement(tag) {
        if (tag === 'a') return (link = { click() {} });
        return { getContext() { return {
            createImageData(w, h) { return { data: new Uint8ClampedArray(w * h * 4) }; },
            putImageData(image) { pixels = image.data; },
        }; } };
    },
};
const centreModule = { exports: {} };
vm.runInNewContext(ts.transpileModule(fs.readFileSync('frontend/src/centreOfPressure.ts', 'utf8'), {
    compilerOptions: { target: ts.ScriptTarget.ES2023, module: ts.ModuleKind.CommonJS },
}).outputText, centreModule);
const context = { exports: {}, document, location: { search: '?snapshot=1' },
    require(name) { assert.equal(name, './centreOfPressure'); return centreModule.exports; },
    URLSearchParams, Uint8ClampedArray, Blob, console, setTimeout: callback => callback(),
    URL: { createObjectURL(blob) { savedBlob = blob; return 'blob:test'; }, revokeObjectURL() {} },
};
vm.runInNewContext(code, context);
const colors = new Uint8Array(256 * 3);
for (let i = 0; i < 256; ++i) colors[i * 3] = i;
const view = new context.exports.SnapshotView(colors);
assert(classes.includes('snapshot-only'));
assert.equal(view.handle('{invalid'), false);
assert.equal(view.handle('null'), false);
assert.equal(view.handle('{"type":"serial_status"}'), false);
const values = Array(1568).fill(0);
values[28] = 200.25; values[1] = 100.5;
const snapshot = { type: 'snapshot', id: '1', rows: 28, columns: 56,
    layout: 'column-major', gapMs: 20, alpha: .25, values };
assert.equal(view.handle(JSON.stringify(snapshot)), true);
assert.equal(draws, 1);
assert.equal(pixels[4], 200); // x=1, y=0
assert.equal(pixels[56 * 4], 101); // x=0, y=1
assert.equal(button.disabled, false);
assert.equal(arcs.length, 1);
// Fractional weights and asymmetric positions catch rounding and axis swaps.
const total = 200.25 + 100.5;
assert(Math.abs(arcs[0].x - (200.25 / total + .5) * 20) < 1e-9);
assert(Math.abs(arcs[0].y - (100.5 / total + .5) * 20) < 1e-9);
assert.equal(arcs[0].radius, 6);
view.handle('{"type":"snapshot_pending","id":"2"}');
view.handle('{"type":"snapshot_error","id":"2","message":"timeout"}');
view.disconnected();
assert.equal(draws, 1); // Failed captures and disconnection retain the image.
assert.equal(arcs.length, 1); // The frozen CoP is retained too.
view.handle(JSON.stringify({ ...snapshot, values: [1] }));
assert.equal(draws, 1);
button.click();
assert.equal(link.download, 'pressure-snapshot-1.json');
savedBlob.text().then(text => {
    assert.deepEqual(JSON.parse(text), snapshot); // Full precision survives export.
    view.handle(JSON.stringify({ ...snapshot, id: '2', values: Array(1568).fill(0) }));
    assert.equal(draws, 2);
    assert.equal(arcs.length, 1); // No CoP marker when there is no pressure.
    console.log('PASS: second-screen mode, protocol validation, wire orientation, fractional CoP, frozen retention, zero-pressure capture, JSON export');
});
