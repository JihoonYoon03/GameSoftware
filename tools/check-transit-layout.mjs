// Static geometry/port checks only: no game, C++ regression executable or GPU is started.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const source = fs.readFileSync(path.join(root, 'SimpleGame/Overworld.cpp'), 'utf8');
const metadata = JSON.parse(fs.readFileSync(path.join(root, 'Resource/Documentation/transit_modules.json'))).modules;
const assets = JSON.parse(fs.readFileSync(path.join(root, 'Resource/Runtime/manifest.json'))).assets;
const names = new Set(assets.map(a => a.name));
for (const name of Object.keys(metadata)) assert.ok(names.has('transit/' + name), name);
const cities = [[0, 0]];
let seed = 0x458a91;
for (let i = 1; i < 5; ++i) {
    seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
    const angle = .25 + (i - 1) * 1.57 + (seed % 350) / 1000, radius = 260 + ((seed >>> 12) % 85);
    cities.push([Math.round(Math.cos(angle) * radius), Math.round(Math.sin(angle) * radius)]);
}
const sources = [];
const add = (a, b, lanes) => sources.push({ a, b, lanes });
const offset = (p, d, t) => p.map((v, i) => v + d[i] * t);
const localCalls = [...source.matchAll(/add\(p\((-?[\d.]+),\s*(-?[\d.]+)\),\s*p\((-?[\d.]+),\s*(-?[\d.]+)\),\s*lanes,/g)];
assert.equal(localCalls.length, 6, 'Update this checker when the city road source changes');
for (let city = 0; city < 5; ++city) {
    const center = cities[city], lanes = city === 1 ? 6 : city >= 3 ? 4 : 2;
    for (const call of localCalls) add(offset(center, [+call[1], +call[2]], 1), offset(center, [+call[3], +call[4]], 1), lanes);
    if (!city) continue;
    let d, gateway;
    if (Math.abs(center[0]) > Math.abs(center[1])) {
        d = [Math.sign(center[0]), 0]; gateway = offset(center, [d[0] * 36, 10], 1);
    } else {
        d = [0, Math.sign(center[1])]; gateway = offset(center, [-12, d[1] > 0 ? 33 : -17], 1);
    }
    const taper = offset(gateway, d, 24);
    add(gateway, taper, lanes);
    add(taper, d[0] ? [d[0] * 390, gateway[1]] : [gateway[0], d[1] * 390], 4);
}
const regional = [...source.matchAll(/add\(\{(-?[\d.]+),\s*(-?[\d.]+)\},\s*\{(-?[\d.]+),\s*(-?[\d.]+)\},\s*(\d+),/g)];
assert.equal(regional.length, 6);
for (const call of regional) add([+call[1], +call[2]], [+call[3], +call[4]], +call[5]);
add([128, -17], [128, 18], 4);
const nodes = [], roads = [];
function node(p) {
    let index = nodes.findIndex(n => Math.hypot(n.p[0] - p[0], n.p[1] - p[1]) < .1);
    if (index < 0) { index = nodes.length; nodes.push({ p, arms: [], radius: 0 }); }
    return index;
}
function on(p, road) {
    const horizontal = road.a[1] === road.b[1];
    return horizontal ? Math.abs(p[1] - road.a[1]) < .01 && p[0] >= Math.min(road.a[0], road.b[0]) && p[0] <= Math.max(road.a[0], road.b[0]) :
        Math.abs(p[0] - road.a[0]) < .01 && p[1] >= Math.min(road.a[1], road.b[1]) && p[1] <= Math.max(road.a[1], road.b[1]);
}
for (const road of sources) {
    assert.ok(road.a[0] === road.b[0] || road.a[1] === road.b[1], 'Diagonal spoke remains');
    const cuts = [road.a, road.b];
    for (const other of sources) {
        const p = road.a[1] === road.b[1] ? [other.a[0], road.a[1]] : [road.a[0], other.a[1]];
        if (on(p, road) && on(p, other)) cuts.push(p);
    }
    cuts.sort((a, b) => Math.hypot(a[0] - road.a[0], a[1] - road.a[1]) - Math.hypot(b[0] - road.a[0], b[1] - road.a[1]));
    for (let i = 1; i < cuts.length; ++i) {
        if (Math.hypot(cuts[i][0] - cuts[i - 1][0], cuts[i][1] - cuts[i - 1][1]) < .1) continue;
        const a = node(cuts[i - 1]), b = node(cuts[i]);
        roads.push({ a, b, lanes: road.lanes });
        nodes[a].arms.push({ to: b, lanes: road.lanes }); nodes[b].arms.push({ to: a, lanes: road.lanes });
    }
}
for (const n of nodes) {
    if (n.arms.length >= 3) {
        assert.ok(n.arms.every(a => a.lanes === n.arms[0].lanes), 'Mixed widths meet without taper');
        assert.ok(n.arms.length <= 4, 'Overlapping roads create an invalid intersection');
        n.radius = n.arms[0].lanes * .75 + 2;
    } else if (n.arms.length === 2) {
        const [a, b] = n.arms;
        const pa = nodes[a.to].p, pb = nodes[b.to].p;
        const straight = Math.abs((pa[0] - n.p[0]) * (pb[1] - n.p[1]) - (pa[1] - n.p[1]) * (pb[0] - n.p[0])) < .01;
        n.radius = straight ? a.lanes === b.lanes ? 0 : Math.min(a.lanes, b.lanes) === 2 && Math.max(a.lanes, b.lanes) === 6 ? 9 : 6 : 9;
        if (!straight) assert.equal(a.lanes, b.lanes);
    } else n.radius = 2;
}
let minimumSpan = Infinity;
for (const road of roads) {
    const a = nodes[road.a], b = nodes[road.b];
    const span = Math.hypot(a.p[0] - b.p[0], a.p[1] - b.p[1]) - a.radius - b.radius;
    assert.ok(span >= 4, 'Module ports overlap: ' + JSON.stringify([a.p, b.p, span]));
    assert.ok(Math.abs(span * 2 - Math.round(span * 2)) < 1e-6, 'Non-native straight remainder');
    minimumSpan = Math.min(minimumSpan, span);
}
const reached = new Set([0]), queue = [0];
for (let i = 0; i < queue.length; ++i) for (const arm of nodes[queue[i]].arms) if (!reached.has(arm.to)) { reached.add(arm.to); queue.push(arm.to); }
assert.equal(reached.size, nodes.length, 'Disconnected city or mine');
console.log(JSON.stringify({ event: 'transit_layout_static_validated', modules: Object.keys(metadata).length, junctions: nodes.length, roads: roads.length, minimum_span: minimumSpan, connected: true }));
