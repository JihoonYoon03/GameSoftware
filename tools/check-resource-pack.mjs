// CPU-only asset validation. Does not compile or start the game or create a graphics context.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const folder = path.join(root, 'Resource', 'Runtime');
const data = fs.readFileSync(path.join(folder, 'game-assets.bin'));
const manifest = JSON.parse(fs.readFileSync(path.join(folder, 'manifest.json'), 'utf8'));
assert.equal(manifest.schema, 'game_resource_pack_v1');
assert.equal(data.length, manifest.bytes);
assert.equal(crypto.createHash('sha256').update(data).digest('hex'), manifest.sha256);
let offset = 0, vertices = 0, animationFrames = 0;
function u32() { const value = data.readUInt32LE(offset); offset += 4; return value; }
function floats(count) {
    assert.ok(count >= 0 && offset + count * 4 <= data.length, 'Truncated float array');
    const result = Array.from({ length: count }, () => {
        const value = data.readFloatLE(offset); offset += 4;
        assert.ok(Number.isFinite(value), 'Nonfinite float');
        return value;
    });
    return result;
}
function text() {
    const size = u32();
    assert.ok(size <= 256 && offset + size <= data.length, 'Invalid text length');
    const result = data.toString('utf8', offset, offset + size); offset += size;
    return result;
}
assert.equal(u32(), 0x41524d45);
assert.equal(u32(), 1);
const count = u32(), names = new Set();
assert.equal(count, manifest.assets.length);
for (let asset = 0; asset < count; ++asset) {
    const name = text(), source = text(), vertexCount = u32(), bones = u32(), clips = u32();
    const report = manifest.assets[asset];
    assert.ok(!names.has(name)); names.add(name);
    assert.equal(name, report.name); assert.equal(source, report.source);
    assert.equal(vertexCount, report.triangles * 3); assert.equal(bones, report.bones);
    assert.ok(vertexCount > 0 && vertexCount % 3 === 0 && bones < 32 && clips <= 32);
    const bounds = floats(6), physical = floats(3), sockets = floats(18);
    for (let axis = 0; axis < 3; ++axis) {
        assert.ok(bounds[axis] <= bounds[axis + 3]);
        assert.ok(physical[axis] > 0);
    }
    if (name.startsWith('vehicle/')) {
        assert.equal(bones, 16);
        assert.ok(sockets[0] < sockets[6] && sockets[3] < sockets[9], 'Reversed vehicle attachments');
        assert.ok(sockets[1] > sockets[4] && sockets[7] > sockets[10], 'Reversed left/right sockets');
    }
    if (name.startsWith('character/')) assert.equal(bones, 23);
    for (let vertex = 0; vertex < vertexCount; ++vertex) {
        const values = floats(21);
        for (let axis = 0; axis < 3; ++axis) {
            assert.ok(values[axis] >= bounds[axis] - 1e-5 && values[axis] <= bounds[axis + 3] + 1e-5, 'Bounds mismatch');
        }
        assert.ok(Math.abs(Math.hypot(...values.slice(3, 6)) - 1) < 1e-4, 'Invalid normal');
        assert.ok([0, 1, 2, 3].includes(values[10]), 'Invalid palette slot');
        assert.ok([0, 1, 2].includes(values[11]), 'Invalid visibility');
        let sum = 0;
        for (let weight = 0; weight < 4; ++weight) {
            const joint = values[13 + weight], amount = values[17 + weight];
            assert.ok(Number.isInteger(joint) && joint >= 0 && (joint < bones || joint === 31));
            assert.ok(amount >= 0 && amount <= 1); sum += amount;
        }
        assert.ok(Math.abs(sum - 1) < .001, 'Unnormalized skin');
        if (name.startsWith('ground/')) assert.equal(values[2], 0, 'Ground must meet actor floor');
    }
    const clipNames = [];
    for (let clip = 0; clip < clips; ++clip) {
        clipNames.push(text());
        const frames = u32(), [duration] = floats(1);
        assert.ok(bones > 0 && frames >= 2 && frames <= 10000 && duration > 0);
        floats(frames * bones * 16); floats(frames * 3);
        animationFrames += frames;
    }
    assert.deepEqual(clipNames, report.clips);
    if (name.startsWith('character/')) for (const clip of ['Idle', 'Run', 'Aim_Base', 'Attack_Fire', 'Ride']) assert.ok(clipNames.includes(clip));
    if (name.startsWith('vehicle/')) for (const clip of ['Hover_Idle', 'Cruise']) assert.ok(clipNames.includes(clip));
    vertices += vertexCount;
}
assert.equal(offset, data.length, 'Unexpected trailing data');
console.log(JSON.stringify({ event: 'resource_pack_validated', assets: count, vertices, animation_frames: animationFrames, bytes: data.length }));
