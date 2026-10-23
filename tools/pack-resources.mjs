// Convert the project's GLB pack into immutable, GPU-ready geometry and 30 Hz skin palettes.
// Run from any directory: node tools/pack-resources.mjs [--check]
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import zlib from 'node:zlib';
import { fileURLToPath } from 'node:url';
import { roadPlanCatalog } from './road-plan-catalog.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const resources = path.join(root, 'Resource');
const output = path.join(resources, 'Runtime');
const checking = process.argv.includes('--check');
const identity = () => [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1];
function multiply(a, b) {
    const r = Array(16).fill(0);
    for (let c = 0; c < 4; ++c) for (let k = 0; k < 4; ++k)
        for (let row = 0; row < 4; ++row) r[c * 4 + row] += a[k * 4 + row] * b[c * 4 + k];
    return r;
}
function inverse(a) {
    const rows = Array.from({ length: 4 }, (_, r) => [...Array.from({ length: 4 }, (_, c) => a[c * 4 + r]),
        ...Array.from({ length: 4 }, (_, c) => Number(c === r))]);
    for (let c = 0; c < 4; ++c) {
        let pivot = c;
        for (let r = c + 1; r < 4; ++r) if (Math.abs(rows[r][c]) > Math.abs(rows[pivot][c])) pivot = r;
        if (Math.abs(rows[pivot][c]) < 1e-10) throw Error('Singular transform');
        [rows[c], rows[pivot]] = [rows[pivot], rows[c]];
        const divisor = rows[c][c];
        rows[c] = rows[c].map(v => v / divisor);
        for (let r = 0; r < 4; ++r) if (r !== c) {
            const factor = rows[r][c];
            rows[r] = rows[r].map((v, k) => v - factor * rows[c][k]);
        }
    }
    return Array.from({ length: 16 }, (_, n) => rows[n % 4][4 + Math.floor(n / 4)]);
}
function point(m, p, w = 1) {
    return [0, 1, 2].map(r => m[r] * p[0] + m[r + 4] * p[1] + m[r + 8] * p[2] + m[r + 12] * w);
}
function trs(t = [0, 0, 0], q = [0, 0, 0, 1], s = [1, 1, 1]) {
    const [x, y, z, w] = q, xx = x * x, yy = y * y, zz = z * z;
    return [(1 - 2 * (yy + zz)) * s[0], 2 * (x * y + z * w) * s[0], 2 * (x * z - y * w) * s[0], 0,
        2 * (x * y - z * w) * s[1], (1 - 2 * (xx + zz)) * s[1], 2 * (y * z + x * w) * s[1], 0,
        2 * (x * z + y * w) * s[2], 2 * (y * z - x * w) * s[2], (1 - 2 * (xx + yy)) * s[2], 0, ...t, 1];
}
function slerp(a, b, t) {
    let dot = a.reduce((sum, v, i) => sum + v * b[i], 0);
    if (dot < 0) { b = b.map(v => -v); dot = -dot; }
    let r;
    if (dot > .9995) r = a.map((v, i) => v + t * (b[i] - v));
    else {
        const angle = Math.acos(Math.min(1, dot));
        r = a.map((v, i) => (v * Math.sin((1 - t) * angle) + b[i] * Math.sin(t * angle)) / Math.sin(angle));
    }
    const length = Math.hypot(...r);
    return r.map(v => v / length);
}
const loaded = new Map(), inputs = new Map();
function load(relative) {
    if (loaded.has(relative)) return loaded.get(relative);
    const buffer = fs.readFileSync(path.join(resources, 'Export', relative + '.glb'));
    if (buffer.readUInt32LE(0) !== 0x46546c67 || buffer.readUInt32LE(4) !== 2 || buffer.readUInt32LE(8) !== buffer.length)
        throw Error('Invalid GLB: ' + relative);
    inputs.set(relative, crypto.createHash('sha256').update(buffer).digest('hex'));
    let json, binary;
    for (let offset = 12; offset < buffer.length;) {
        const length = buffer.readUInt32LE(offset), type = buffer.readUInt32LE(offset + 4);
        if (offset + 8 + length > buffer.length) throw Error('Invalid chunk');
        if (type === 0x4e4f534a) json = JSON.parse(buffer.subarray(offset + 8, offset + 8 + length).toString('utf8'));
        if (type === 0x004e4942) binary = buffer.subarray(offset + 8, offset + 8 + length);
        offset += 8 + length;
    }
    if (!json || !binary || json.extensionsRequired?.length) throw Error('Unsupported GLB: ' + relative);
    const data = { json, binary, relative, accessors: new Map(), images: new Map() };
    loaded.set(relative, data);
    return data;
}
function accessor(data, index) {
    if (data.accessors.has(index)) return data.accessors.get(index);
    const a = data.json.accessors[index], v = data.json.bufferViews[a.bufferView];
    if (!v || a.sparse || (v.buffer ?? 0) !== 0) throw Error('Unsupported accessor');
    const sizes = { SCALAR: 1, VEC2: 2, VEC3: 3, VEC4: 4, MAT4: 16 };
    const formats = { 5121: [1, 'readUInt8', 255], 5123: [2, 'readUInt16LE', 65535],
        5125: [4, 'readUInt32LE', 4294967295], 5126: [4, 'readFloatLE', 1] };
    const count = sizes[a.type], format = formats[a.componentType];
    if (!count || !format || a.count > 10_000_000) throw Error('Accessor format/limit');
    const start = (v.byteOffset ?? 0) + (a.byteOffset ?? 0), stride = v.byteStride ?? count * format[0];
    if (stride < count * format[0] || start + (a.count - 1) * stride + count * format[0] > (v.byteOffset ?? 0) + v.byteLength)
        throw Error('Accessor bounds');
    const result = Array.from({ length: a.count }, (_, i) => Array.from({ length: count }, (_, j) => {
        const number = data.binary[format[1]](start + i * stride + j * format[0]);
        if (!Number.isFinite(number)) throw Error('Nonfinite accessor');
        return a.normalized ? number / format[2] : number;
    }));
    data.accessors.set(index, result);
    return result;
}
function globals(data, overrides = new Map()) {
    const result = [], parents = new Map();
    data.json.nodes.forEach((n, i) => (n.children ?? []).forEach(c => parents.set(c, i)));
    const active = new Set();
    function visit(i) {
        if (result[i]) return result[i];
        if (active.has(i)) throw Error('Cyclic nodes');
        active.add(i);
        const n = { ...data.json.nodes[i], ...overrides.get(i) };
        const local = overrides.has(i) || !n.matrix ? trs(n.translation, n.rotation, n.scale) : n.matrix;
        result[i] = parents.has(i) ? multiply(visit(parents.get(i)), local) : local;
        active.delete(i);
        return result[i];
    }
    data.json.nodes.forEach((_, i) => visit(i));
    return result;
}
function sampleOverrides(data, animation, time) {
    const result = new Map();
    for (const channel of animation?.channels ?? []) {
        const sampler = animation.samplers[channel.sampler], times = accessor(data, sampler.input).map(v => v[0]);
        const values = accessor(data, sampler.output), mode = sampler.interpolation ?? 'LINEAR';
        if (!['LINEAR', 'STEP'].includes(mode) || channel.target.path === 'weights') throw Error('Unsupported animation');
        let b = times.findIndex(t => t >= time); if (b < 0) b = times.length - 1;
        const a = Math.max(0, b - 1), t = a === b || mode === 'STEP' ? 0 : Math.max(0, Math.min(1, (time - times[a]) / (times[b] - times[a])));
        const value = channel.target.path === 'rotation' ? slerp(values[a], values[b], t)
            : values[a].map((v, i) => v + (values[b][i] - v) * t);
        if (!result.has(channel.target.node)) result.set(channel.target.node, {});
        result.get(channel.target.node)[channel.target.path] = value;
    }
    return result;
}
// Read the pack's RGB/RGBA PNGs without introducing a runtime image dependency.
function png(bytes) {
    if (bytes.subarray(0, 8).toString('hex') !== '89504e470d0a1a0a') throw Error('PNG required');
    let width, height, channels; const chunks = [];
    for (let p = 8; p < bytes.length;) {
        const length = bytes.readUInt32BE(p), type = bytes.toString('ascii', p + 4, p + 8), b = bytes.subarray(p + 8, p + 8 + length);
        if (type === 'IHDR') {
            width = b.readUInt32BE(0); height = b.readUInt32BE(4); channels = b[9] === 6 ? 4 : b[9] === 2 ? 3 : 0;
            if (b[8] !== 8 || !channels || b[12] !== 0 || width * height > 4096 * 4096) throw Error('PNG format');
        }
        if (type === 'IDAT') chunks.push(b);
        p += length + 12;
    }
    const raw = zlib.inflateSync(Buffer.concat(chunks)), pixels = Buffer.alloc(width * height * channels), stride = width * channels;
    if (raw.length !== height * (stride + 1)) throw Error('PNG size');
    function paeth(a, b, c) { const p = a + b - c, x = Math.abs(p - a), y = Math.abs(p - b), z = Math.abs(p - c); return x <= y && x <= z ? a : y <= z ? b : c; }
    for (let y = 0; y < height; ++y) {
        const filter = raw[y * (stride + 1)];
        if (filter > 4) throw Error('PNG filter');
        for (let x = 0; x < stride; ++x) {
            const a = x >= channels ? pixels[y * stride + x - channels] : 0, b = y ? pixels[(y - 1) * stride + x] : 0;
            const c = y && x >= channels ? pixels[(y - 1) * stride + x - channels] : 0;
            pixels[y * stride + x] = raw[y * (stride + 1) + 1 + x] + [0, a, b, (a + b) >> 1, paeth(a, b, c)][filter];
        }
    }
    return { width, height, channels, pixels };
}
function textureColor(data, texture, uv) {
    const index = data.json.textures[texture].source;
    if (!data.images.has(index)) {
        const image = data.json.images[index], view = data.json.bufferViews[image.bufferView];
        if (!view || image.mimeType !== 'image/png') throw Error('Embedded PNG required');
        data.images.set(index, png(data.binary.subarray(view.byteOffset ?? 0, (view.byteOffset ?? 0) + view.byteLength)));
    }
    const image = data.images.get(index), x = Math.floor(((uv[0] % 1 + 1) % 1) * image.width), y = Math.floor(((uv[1] % 1 + 1) % 1) * image.height);
    return Array.from({ length: 4 }, (_, c) => c === 3 && image.channels === 3 ? 1 : image.pixels[(y * image.width + x) * image.channels + c] / 255);
}
const characterScale = 1.8 / (2 * 1.26);
function basis(kind) {
    const s = kind === 'human' ? .5 / characterScale : .5;
    return kind === 'human' || kind === 'creature'
        ? [0, s, 0, 0, 0, 0, s, 0, s, 0, 0, 0, 0, 0, 0, 1]
        : [s, 0, 0, 0, 0, 0, s, 0, 0, -s, 0, 0, 0, 0, 0, 1];
}
function bounds(vertices) {
    return [0, 1].map(which => [0, 1, 2].map(axis => vertices.reduce((v, p) => which ? Math.max(v, p[axis]) : Math.min(v, p[axis]), which ? -Infinity : Infinity)));
}
const assets = [];
function convert(name, relative, kind = 'prop', options = {}) {
    const template = load(relative), skin = template.json.skins?.[0], jointNames = skin?.joints.map(i => template.json.nodes[i].name) ?? ['root'];
    if (jointNames.length >= 32) throw Error('Skin palette exceeds 31 joints: ' + relative);
    const vertices = [], c = basis(kind); let invC = inverse(c);
    const attachments = { 'engine.L': [], 'engine.R': [], nose: [] };
    const engineSurfaces = { 'engine.L': [], 'engine.R': [] };
    if (kind === 'human' && JSON.stringify(jointNames) !== JSON.stringify(load('Characters/Player_Explorer').json.skins[0].joints.map(i => load('Characters/Player_Explorer').json.nodes[i].name)))
        throw Error('HUMAN_7H palette order changed');
    const components = options.components ?? [{ data: template }];
    for (const { data, offset = [0, 0, 0], skinOverride } of components) {
        const world = globals(data), scene = data.json.scenes[data.json.scene ?? 0];
        const visible = new Set();
        function visit(i) { visible.add(i); (data.json.nodes[i].children ?? []).forEach(visit); }
        scene.nodes.forEach(visit);
        for (const i of visible) {
            const node = data.json.nodes[i]; if (node.mesh === undefined) continue;
            const nodeSkin = node.skin === undefined ? null : data.json.skins[node.skin];
            const remap = nodeSkin?.joints.map(index => {
                const j = jointNames.indexOf(data.json.nodes[index].name); if (j < 0) throw Error('Joint mismatch'); return j;
            });
            const nodeName = node.extras?.asset_id ?? node.name;
            for (const primitive of data.json.meshes[node.mesh].primitives) {
                if ((primitive.mode ?? 4) !== 4) throw Error('Triangles required');
                const positions = accessor(data, primitive.attributes.POSITION), normals = accessor(data, primitive.attributes.NORMAL);
                const joints = primitive.attributes.JOINTS_0 === undefined ? null : accessor(data, primitive.attributes.JOINTS_0);
                const weights = primitive.attributes.WEIGHTS_0 === undefined ? null : accessor(data, primitive.attributes.WEIGHTS_0);
                const uv = primitive.attributes.TEXCOORD_0 === undefined ? null : accessor(data, primitive.attributes.TEXCOORD_0);
                const material = data.json.materials?.[primitive.material] ?? {}, materialName = material.name ?? '', pbr = material.pbrMetallicRoughness ?? {};
                const base = [...(pbr.baseColorFactor ?? [1, 1, 1, 1])];
                if (skinOverride && /Skin/.test(materialName)) base.splice(0, 4, ...skinOverride);
                let palette = /Glow|Emission|Emissive/.test(materialName) || material.emissiveFactor?.some(x => x > 0) ? 3
                    : /Cloth|Vehicle_|Hair/.test(materialName) ? 1 : /Accent|Trim/.test(materialName) ? 2 : 0;
                if (options.neutralEmission && palette === 3) base.splice(0, 4, 1, 1, 1, 1);
                if (options.signalState !== undefined && /StopGlyph|WaitGlyph|GoGlyph/.test(nodeName)) {
                    const active = ['StopGlyph', 'WaitGlyph', 'GoGlyph'][options.signalState];
                    const colors = [[1, .12, .16, 1], [1, .65, .1, 1], [.15, 1, .8, 1]];
                    base.splice(0, 4, ...(nodeName.includes(active) ? colors[options.signalState] : [.025, .045, .055, 1]));
                    palette = nodeName.includes(active) ? 3 : 0;
                }
                const surface = palette === 3 ? 4 : /Skin/.test(materialName) ? 7 : /Glass/.test(materialName) ? 3
                    : /Cloth|Fabric|Hair/.test(materialName) ? 1 : /Rubber/.test(materialName) ? 5 : /Rock|Soil|Grass|Sand|Mine/.test(materialName) ? 6 : 2;
                const visibility = /Weapon_CoilPistol/.test(nodeName) ? 1 : /Hair/.test(nodeName) ? 2 : 0;
                for (const [index] of accessor(data, primitive.indices)) {
                    const transform = nodeSkin ? identity() : world[i];
                    const source = point(transform, positions[index]);
                    const pos = point(c, source.map((v, axis) => v + offset[axis]));
                    if (kind === 'vehicle' || kind === 'aircraft') {
                        const slot = /Engine_.*_L$/.test(nodeName) ? 'engine.L' : /Engine_.*_R$/.test(nodeName) ? 'engine.R' : /Nose_/.test(nodeName) ? 'nose' : null;
                        if (slot && palette === 3) attachments[slot].push(pos);
                        if (slot && slot !== 'nose') engineSurfaces[slot].push(pos);
                    }
                    const normalMatrix = inverse(multiply(c, transform));
                    const n = [0, 1, 2].map(r => normals[index].reduce((sum, v, axis) => sum + normalMatrix[r * 4 + axis] * v, 0));
                    const length = Math.hypot(...n); if (length < 1e-10) throw Error('Zero normal');
                    let color = base;
                    if (pbr.baseColorTexture && uv) color = base.map((v, axis) => v * textureColor(data, pbr.baseColorTexture.index, uv[index])[axis]);
                    let js = joints && remap ? joints[index].map(j => remap[j]) : [31, 31, 31, 31];
                    let ws = weights && remap ? weights[index] : [1, 0, 0, 0];
                    const total = ws.reduce((a, b) => a + b, 0); if (total < .99 || total > 1.01) throw Error('Skin weights');
                    vertices.push([...pos, ...n.map(v => v / length), ...color, palette, visibility, surface, ...js, ...ws.map(v => v / total)]);
                }
            }
        }
    }
    if (options.trimLength) {
        // Trim only a final straight segment; never stretch lane widths, markings or curved modules.
        let triangles = vertices.slice();
        for (const sign of [-1, 1]) {
            const clipped = [];
            for (let i = 0; i < triangles.length; i += 3) {
                const input = triangles.slice(i, i + 3), polygon = [];
                for (let edge = 0; edge < input.length; ++edge) {
                    const a = input[edge], b = input[(edge + 1) % input.length];
                    const da = options.trimLength / 2 - sign * a[0], db = options.trimLength / 2 - sign * b[0];
                    if (da >= 0) polygon.push(a);
                    if ((da >= 0) !== (db >= 0)) {
                        const t = da / (da - db);
                        const v = a.map((value, axis) => value + (b[axis] - value) * t);
                        const normal = Math.hypot(...v.slice(3, 6));
                        for (let axis = 3; axis < 6; ++axis) v[axis] /= normal;
                        polygon.push(v);
                    }
                }
                for (let j = 1; j + 1 < polygon.length; ++j) clipped.push(polygon[0], polygon[j], polygon[j + 1]);
            }
            triangles = clipped;
        }
        vertices.splice(0, vertices.length, ...triangles);
    }
    let [lo, hi] = bounds(vertices), physical = hi.map((v, i) => (v - lo[i]) * (kind === 'human' ? characterScale : 1));
    if (kind === 'vehicle') {
        const ground = lo[2];
        for (const v of vertices) v[2] -= ground;
        for (const list of Object.values(attachments)) for (const v of list) v[2] -= ground;
        for (const list of Object.values(engineSurfaces)) for (const v of list) v[2] -= ground;
        c[14] -= ground; invC = inverse(c);
        [lo, hi] = bounds(vertices);
    }
    if (options.normalize) {
        const divisor = [hi[0] - lo[0], hi[1] - lo[1], options.height ? options.height * .5 : hi[2] - lo[2]];
        for (const v of vertices) {
            for (let axis = 0; axis < 3; ++axis) v[axis] = (v[axis] - lo[axis]) / Math.max(divisor[axis], 1e-5);
            const length = Math.hypot(...v.slice(3, 6).map((n, i) => n * divisor[i]));
            for (let axis = 0; axis < 3; ++axis) v[3 + axis] *= divisor[axis] / length;
        }
        [lo, hi] = bounds(vertices);
    }
    if (name.startsWith('ground/')) {
        // Actors and collision use z=0: retain imported top faces, not a normalized thick slab.
        const top = [];
        for (let i = 0; i < vertices.length; i += 3) if (vertices[i][5] + vertices[i + 1][5] + vertices[i + 2][5] > 1.5) {
            for (let j = 0; j < 3; ++j) {
                const v = vertices[i + j]; v[2] = 0; v[3] = 0; v[4] = 0; v[5] = 1; top.push(v);
            }
        }
        vertices.splice(0, vertices.length, ...top);
        [lo, hi] = bounds(vertices);
    }
    const sockets = Array.from({ length: 6 }, () => [0, 0, 0]);
    const baseWorld = globals(template);
    const socketNode = name => template.json.nodes.findIndex(n => n.name === name);
    function socket(name, world = baseWorld) { const n = socketNode(name); return n < 0 ? [0, 0, 0] : point(c, point(world[n], [0, 0, 0])); }
    if (kind === 'vehicle' || kind === 'aircraft') {
        // Engine and lamp geometry determines attachment points, not guessed sprite coordinates.
        for (let side = 0; side < 2; ++side) {
            const slot = side ? 'engine.R' : 'engine.L';
            // Refined metal exhausts may have no emissive faces. Keep the socket on the actual rear port.
            let engine = attachments[slot];
            if (!engine.length) {
                const surface = engineSurfaces[slot];
                const rear = Math.min(...surface.map(p => p[0]));
                engine = surface.filter(p => p[0] <= rear + .0001);
            }
            if (!engine.length) throw Error('Missing engine geometry: ' + name);
            sockets[side] = [Math.min(...engine.map(p => p[0])) - .005,
                ...[1, 2].map(axis => engine.reduce((sum, p) => sum + p[axis], 0) / engine.length)];
            if (kind === 'aircraft') continue;
            const lamp = attachments.nose.filter(p => side ? p[1] < 0 : p[1] >= 0);
            if (!lamp.length) throw Error('Missing nose lamp geometry: ' + name);
            sockets[side + 2] = [Math.max(...lamp.map(p => p[0])) + .005,
                ...[1, 2].map(axis => lamp.reduce((sum, p) => sum + p[axis], 0) / lamp.length)];
        }
        sockets[4] = socket('driver_socket');
        if (kind === 'aircraft') sockets[5] = ['engine.L', 'engine.R'].map(n => jointNames.indexOf(n)).concat(0);
    }
    const clips = [];
    if (skin) {
        const inverseBinds = accessor(template, skin.inverseBindMatrices);
        const sourceClips = template.json.animations ?? [];
        const selected = kind === 'human' ? [...sourceClips] : sourceClips.filter(a => ['Reference_A', 'Idle', 'Walk', 'Run', 'Aim_Base', 'Attack_Fire', 'Attack_Melee', 'Jump_Air',
            'Vehicle_Reference', 'Hover_Idle', 'Cruise', 'Boost', 'Steer_Left', 'Steer_Right',
            'Aircraft_Reference', 'Flight_Idle', 'Bank_Left', 'Bank_Right', 'LandingGear_Deploy',
            'LandingGear_Retract', 'Canopy_Open', 'Canopy_Close', 'Cargo_Open', 'Cargo_Close'].includes(a.name));
        if (kind === 'human') selected.push({ name: 'Ride' });
        for (const clip of selected) {
            let duration = Math.max(1 / 30, ...((clip.samplers ?? []).map(s => accessor(template, s.input).at(-1)[0])));
            if (clip.name === 'Ride') duration = 1 / 30;
            const frames = Math.max(2, Math.round(duration * 30) + 1), matrices = [], muzzles = [];
            for (let frame = 0; frame < frames; ++frame) {
                let overrides = sampleOverrides(template, clip, Math.min(duration, frame / 30));
                if (kind === 'aircraft' && ['Cruise', 'Boost', 'Flight_Idle'].includes(clip.name)) {
                    const gear = sourceClips.find(a => a.name === 'LandingGear_Retract');
                    for (const [node, transform] of sampleOverrides(template, gear, 1))
                        if (/gear/i.test(template.json.nodes[node].name)) overrides.set(node, transform);
                }
                if (clip.name === 'Ride') {
                    overrides = sampleOverrides(template, sourceClips.find(a => a.name === 'Jump_Air'), .22);
                    const aim = sampleOverrides(template, sourceClips.find(a => a.name === 'Aim_Base'), 0);
                    for (const [node, pose] of aim) if (/spine|chest|neck|head|clavicle|arm|hand|weapon|muzzle/.test(template.json.nodes[node].name)) overrides.set(node, pose);
                }
                const world = globals(template, overrides);
                const pelvis = socket('pelvis', world);
                for (let j = 0; j < skin.joints.length; ++j) {
                    let m = multiply(c, multiply(world[skin.joints[j]], multiply(inverseBinds[j], invC)));
                    if (clip.name === 'Ride') m[14] -= pelvis[2] - .17;
                    matrices.push(...m);
                }
                muzzles.push(...socket('muzzle', world));
            }
            clips.push({ name: clip.name, duration, frames, matrices, muzzles });
        }
    }
    if (!vertices.length || vertices.length % 3 || vertices.some(v => v.length !== 21 || v.some(n => !Number.isFinite(n)))) throw Error('Invalid converted mesh: ' + name);
    assets.push({ name, relative, vertices, boneCount: skin?.joints.length ?? 0, clips, lo, hi, physical, sockets });
}

const recipes = JSON.parse(fs.readFileSync(path.join(resources, 'Documentation/assembly_recipes.json')));
const fitting = JSON.parse(fs.readFileSync(path.join(resources, 'Documentation/accessory_fitting_offsets.json')));
const characterDetail = JSON.parse(fs.readFileSync(path.join(resources, 'Documentation/character_detail_assemblies.json')));
if (characterDetail.skeleton !== 'HUMAN_7H') throw Error('Character detail skeleton changed');
const roles = ['Explorer', 'Engineer', 'Guard', 'Medic', 'Researcher', 'Miner', 'HeavyCombat', 'Pilot', 'Civilian'];
const examples = ['Player_Explorer', 'Ria_Engineer', 'Kain_Guard', 'Mara_Medic', 'NPC_Researcher', 'NPC_Miner', 'NPC_HeavyCombat', 'NPC_Pilot', 'NPC_Civilian'];
for (let role = 0; role < roles.length; ++role) for (const gender of ['Male', 'Female']) {
    const recipe = recipes[examples[role]], body = gender + '_' + recipe.body.split('_')[1];
    if (!characterDetail.body_profiles.includes(body)) throw Error('Character body profile missing: ' + body);
    const originalParts = new Map();
    for (const original of recipe.parts.filter(part => !part.startsWith('Hair'))) {
        const part = original.replace(/(?:Male|Female)_(?:Lean|Normal|Heavy)/g, body);
        if (!originalParts.has(part)) originalParts.set(part, original);
        const outer = characterDetail.outer_assemblies[part];
        if (/_Torso$/.test(part) && !outer) throw Error('Character outer assembly missing: ' + part);
        if (outer && !originalParts.has(outer)) {
            originalParts.set(outer, characterDetail.outer_assemblies[original] ?? outer);
        }
    }
    const template = load('Characters/' + examples[role]);
    const skinColor = recipe.skin ? template.json.materials.find(material => material.name === recipe.skin)
        ?.pbrMetallicRoughness?.baseColorFactor : undefined;
    if (recipe.skin && !skinColor) throw Error('Authored character skin missing: ' + recipe.skin);
    const components = [...originalParts].map(([part, original]) => {
        // An outer harness shares its torso's bind coordinates and fitting offset, exactly once.
        const torso = part.endsWith('_OuterAssembly') ? part.slice(0, -'_OuterAssembly'.length) : part;
        const originalTorso = original.endsWith('_OuterAssembly') ? original.slice(0, -'_OuterAssembly'.length) : original;
        const offset = fitting[body]?.[roles[role]]?.[part] ?? recipe.offsets?.[original]
            ?? fitting[body]?.[roles[role]]?.[torso] ?? recipe.offsets?.[originalTorso] ?? [0, 0, 0];
        // Blender offset (x,y,z) -> GLB (x,z,-y).
        return { data: load('Modules/' + part), offset: [offset[0], offset[2], -offset[1]], skinOverride: skinColor };
    });
    convert(`character/${gender}/${roles[role]}`, 'Characters/' + examples[role], 'human', { components });
}
for (const hair of ['Buzz', 'SidePart', 'Curls', 'Bob', 'Ponytail', 'Bun']) {
    for (const covered of [false, true]) {
        const part = covered ? ['Ponytail', 'Bun'].includes(hair) ? 'Hair_HatClearance_' + hair : 'Hair_HatClearance_Buzz' : 'Hair_' + hair;
        convert(`hair/${covered ? 'covered/' : ''}${hair}`, 'Characters/Player_Explorer', 'human', { components: [{ data: load('Modules/' + part) }] });
    }
}
for (const vehicle of ['Coupe', 'Shuttle', 'Hauler', 'Patrol', 'JetBike']) convert('vehicle/' + vehicle, 'Vehicles/' + vehicle, 'vehicle');
for (const [name, file, height] of [['Habitat', 'Habitat_7p2m', 7.2], ['Terraced', 'TerracedPods_10p8m', 10.8],
    ['Workshop', 'Workshop_5p4m', 5.4], ['Dome', 'RibDome_6p5m', 6.5], ['Observatory', 'Observatory_11p5m', 11.5]])
    convert('building/' + name, 'Buildings/' + file, 'prop', { normalize: true, height });
const architecture = JSON.parse(fs.readFileSync(path.join(resources, 'Documentation/architecture_styles.json')));
for (const [name, file] of [['SpireHelix', 'Spire_Helix_12F'], ['TwinBridge', 'Tower_TwinBridge_16F'],
    ['ArcologyTerrace', 'Arcology_Terrace_10F'], ['TransitAtrium', 'Civic_TransitAtrium_4F'],
    ['CourtyardHabitat', 'Habitat_Courtyard_3F']]) {
    const style = architecture.styles[file];
    if (!style || !(style.floor_count > 0) || !(style.storey_height_m > 0))
        throw Error('Architecture dimensions missing: ' + file);
    // Match the existing normalized building contract while retaining every authored façade,
    // glazing and roof component. Physical dimensions remain derived from the assembled GLB.
    convert('building/' + name, 'Buildings/' + file, 'prop',
        { normalize: true, height: style.floor_count * style.storey_height_m });
}
for (const name of ['RelaySentinel', 'RiftGrazer', 'VeilWalker']) for (const variant of ['', '_Variant']) convert('creature/' + name + variant, 'Creatures/' + name + variant, 'creature');
for (const [name, file, normalize] of [['box', 'Interior_Crate', true], ['salvage', 'Gear_Engineer_Powerpack', false],
    ['recoveryKit', 'Interior_Med_Kit', false], ['console', 'Interior_Terminal', false], ['telescope', 'Hab_Sensor_Mast', false],
    ['bulkhead', 'Street_Bulkhead_2m', true], ['trafficGate', 'Street_Gate_Frame_4m', true], ['bed', 'Interior_Bed', false],
    ['chair', 'Interior_Chair', false], ['table', 'Interior_Table', false], ['lamp', 'Street_Lamp_3p2m', false],
    ['generator', 'Field_Generator', false], ['breaker', 'Field_Breaker', false], ['beacon', 'Field_Survey_Beacon', false],
    ['relief', 'Ruin_Observer_Relief', false], ['wall', 'Hab_Wall_Solid_2m', true], ['door', 'Hab_Pressure_Door', false],
    ['lantern', 'Mine_Lantern', false], ['support', 'Mine_Support', false], ['ruinPillar', 'Ruin_Pillar', false]])
    convert('prop/' + name, 'Modules/' + file, 'prop', { normalize });
for (const [name, file] of [['concrete', 'Ground_Soil_2m'], ['asphalt', 'Ground_Road_2m'], ['mine', 'Ground_Mine_2m'],
    ['glass', 'Ground_Sand_2m'], ['grass', 'Ground_Grass_2m'], ['sand', 'Ground_Sand_2m']]) convert('ground/' + name, 'Environment/' + file, 'prop', { normalize: true });
convert('ground/metal', 'Modules/Hab_Floor_2m', 'prop', { normalize: true });
for (const [name, file, normalize] of [['rock', 'Nature_Rock_01', true], ['tree', 'Nature_Alien_Tree_01', false],
    ['fungus', 'Nature_Fungal_Cluster', false], ['crystal', 'Nature_Crystal_Cluster', false], ['reeds', 'Nature_Glow_Reeds_01', false]])
    convert('nature/' + name, 'Environment/' + file, 'prop', { normalize });

for (const type of ['Kite_Courier', 'Manta_AirTaxi', 'Mule_Skycrane', 'Warden_Survey'])
    for (const variant of ['', '_Variant']) convert('aircraft/' + type + variant, 'Aircraft/' + type + variant, 'aircraft');
const scenery = JSON.parse(fs.readFileSync(path.join(resources, 'Documentation/scenery_assemblies.json')));
for (const name of Object.keys(scenery.assemblies)) {
    convert('scenery/' + name, 'Scenery/' + name, 'prop',
        name === 'City_TrafficSignal' ? { signalState: 0 } : name === 'City_PedestrianHologram_6m' ? { neutralEmission: true } : {});
}
for (const [state, index] of [['Amber', 1], ['Green', 2]])
    convert('scenery/City_TrafficSignal_' + state, 'Scenery/City_TrafficSignal', 'prop', { signalState: index });

const transit = JSON.parse(fs.readFileSync(path.join(resources, 'Documentation/transit_modules.json')));
for (const name of Object.keys(transit.modules)) convert('transit/' + name, 'Transit/' + name, 'prop');
for (const lanes of [2, 4, 6]) for (const length of [.5, 1, 1.5])
    convert(`transit/Trim_Road_${lanes}L_${length}`, `Transit/Transit_Road_${lanes}L_Straight_4m`, 'prop', { trimLength: length });
convert('transit/Trim_Walk_W2_0.5', 'Transit/Transit_Walk_W2_Straight_2m', 'prop', { trimLength: .5 });

const roadPlan = roadPlanCatalog(root, checking);
for (const name of Object.keys(roadPlan.modules)) convert('transit/' + name, 'RoadPlan/' + name, 'prop');

const chunks = [];
function u32(value) { const b = Buffer.alloc(4); b.writeUInt32LE(value); chunks.push(b); }
function f32(values) { const b = Buffer.alloc(values.length * 4); values.forEach((v, i) => b.writeFloatLE(v, i * 4)); chunks.push(b); }
function text(value) { const b = Buffer.from(value, 'utf8'); u32(b.length); chunks.push(b); }
u32(0x41524d45); u32(1); u32(assets.length);
for (const asset of assets) {
    text(asset.name); text(asset.relative); u32(asset.vertices.length); u32(asset.boneCount); u32(asset.clips.length);
    f32([...asset.lo, ...asset.hi, ...asset.physical, ...asset.sockets.flat()]);
    f32(asset.vertices.flat());
    for (const clip of asset.clips) { text(clip.name); u32(clip.frames); f32([clip.duration]); f32(clip.matrices); f32(clip.muzzles); }
}
const binary = Buffer.concat(chunks), digest = crypto.createHash('sha256').update(binary).digest('hex');
const report = { schema: 'game_resource_pack_v1', converter: 'tools/pack-resources.mjs', bytes: binary.length, sha256: digest,
    sources: Object.fromEntries([...inputs].sort()), assets: assets.map(a => ({ name: a.name, source: a.relative, triangles: a.vertices.length / 3,
        bones: a.boneCount, physical_world_units: a.physical, bounds: [a.lo, a.hi], clips: a.clips.map(c => c.name) })) };
if (checking) {
    if (!fs.existsSync(path.join(output, 'game-assets.bin')) || !fs.readFileSync(path.join(output, 'game-assets.bin')).equals(binary)) throw Error('Resource pack is stale; regenerate it.');
    if (JSON.stringify(JSON.parse(fs.readFileSync(path.join(output, 'manifest.json'))).sources) !== JSON.stringify(report.sources)) throw Error('Resource source hashes changed');
} else {
    fs.mkdirSync(output, { recursive: true });
    fs.writeFileSync(path.join(output, 'game-assets.bin'), binary);
    fs.writeFileSync(path.join(output, 'manifest.json'), JSON.stringify(report, null, 2) + '\n');
}
console.log(JSON.stringify({ mode: checking ? 'check' : 'convert', assets: assets.length, bytes: binary.length, sha256: digest }));
