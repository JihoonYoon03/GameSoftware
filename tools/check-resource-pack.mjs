// CPU-only asset validation. Does not compile or start the game or create a graphics context.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { maxAnimationClips, maxPackBytes, resourcePackStatus } from './resource-pack-inputs.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const folder = path.join(root, 'Resource', 'Runtime');
const data = fs.readFileSync(path.join(folder, 'game-assets.bin'));
const manifest = JSON.parse(fs.readFileSync(path.join(folder, 'manifest.json'), 'utf8'));
const recipes = JSON.parse(fs.readFileSync(path.join(root, 'Resource/Documentation/assembly_recipes.json')));
const characterDetail = JSON.parse(fs.readFileSync(path.join(root, 'Resource/Documentation/character_detail_assemblies.json')));
const documents = new Map();
function glbDocument(relative) {
    if (!documents.has(relative)) {
        const glb = fs.readFileSync(path.join(root, 'Resource/Export', relative + '.glb'));
        documents.set(relative, JSON.parse(glb.toString('utf8', 20, 20 + glb.readUInt32LE(12))));
    }
    return documents.get(relative);
}
function sourceTriangles(relative) {
    const source = glbDocument(relative);
    let triangles = 0;
    function visit(index) {
        const node = source.nodes[index];
        if (node.mesh !== undefined) for (const primitive of source.meshes[node.mesh].primitives)
            triangles += source.accessors[primitive.indices ?? primitive.attributes.POSITION].count / 3;
        for (const child of node.children ?? []) visit(child);
    }
    source.scenes[source.scene ?? 0].nodes.forEach(visit);
    return triangles;
}
assert.equal(manifest.schema, 'game_resource_pack_v4');
assert.ok(data.length <= maxPackBytes);
assert.equal(data.length, manifest.bytes);
assert.ok(data.length >= 16 && data.length <= 512 * 1024 * 1024, 'Resource pack exceeds runtime size limit');
assert.equal(crypto.createHash('sha256').update(data).digest('hex'), manifest.sha256);
let offset = 0, vertices = 0, uniqueVerticesTotal = 0, lod0IndicesTotal = 0, shadowIndicesTotal = 0, animationFrames = 0;
function u32() { const value = data.readUInt32LE(offset); offset += 4; return value; }
function u16() { const value = data.readUInt16LE(offset); offset += 2; return value; }
function i16() { const value = data.readInt16LE(offset); offset += 2; return value; }
function u8() { const value = data[offset]; offset += 1; return value; }
function decodeOct(ix, iy) {
    let x = Math.max(-1, ix / 32767), y = Math.max(-1, iy / 32767);
    let z = 1 - Math.abs(x) - Math.abs(y);
    if (z < 0) {
        const ox = (1 - Math.abs(y)) * (x >= 0 ? 1 : -1);
        const oy = (1 - Math.abs(x)) * (y >= 0 ? 1 : -1);
        x = ox; y = oy;
    }
    const len = Math.hypot(x, y, z);
    return len > 1e-8 ? [x / len, y / len, z / len] : [0, 0, 1];
}
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
assert.equal(u32(), 4);
const count = u32(), names = new Set();
const bankCount = u32(), banks = [];
assert.equal(bankCount, manifest.animationBanks);
assert.ok(bankCount > 0 && bankCount <= 512);
for (let bank = 0; bank < bankCount; ++bank) {
    const bones = u32(), clips = u32(), clipNames = [], gear = new Map();
    assert.ok(bones > 0 && bones < 72 && clips > 0 && clips <= maxAnimationClips);
    const boneNames = [];
    for (let j = 0; j < bones; ++j) {
        boneNames.push(text()); const parent = u32() - 1; floats(16);
        assert.ok(parent >= -1 && parent < bones && parent !== j);
    }
    const sampleCount = u32(), samples = [];
    for (let j = 0; j < sampleCount; ++j) { samples.push(text()); floats(2); }
    const triangleCount = u32();
    for (let j = 0; j < triangleCount * 3; ++j) assert.ok(u32() < sampleCount);
    const soleCount = u32();
    for (let j = 0; j < soleCount; ++j) { assert.ok(u32() < bones); floats(3); }
    if (bones === 23) { assert.equal(sampleCount, 17); assert.equal(triangleCount, 24); assert.equal(soleCount, 4); }
    for (let clip = 0; clip < clips; ++clip) {
        const name = text(); clipNames.push(name);
        const frames = u32(), [duration] = floats(1);
        assert.ok(frames >= 2 && frames <= 10000 && duration > 0);
        const matrices = floats(frames * bones * 16); floats(frames * 3);
        if (bones === 17 && ['Aircraft_Reference', 'Cruise'].includes(name))
            gear.set(name, [...matrices.slice(11 * 16, 12 * 16), ...matrices.slice(16 * 16, 17 * 16)]);
        animationFrames += frames;
    }
    assert.equal(new Set(clipNames).size, clips);
    for (const sample of samples) assert.ok(clipNames.includes(sample));
    banks.push({ bones, clipNames, gear });
}
const signalDigests = new Set();
assert.equal(count, manifest.assets.length);
assert.ok(count <= 512, 'Resource pack exceeds runtime asset limit');
for (let asset = 0; asset < count; ++asset) {
    const name = text(), source = text();
    const uniqueVertexCount = u32(), totalIndexCount = u32(), indexBytes = u32();
    const bones = u32(), bank = u32();
    const lodOffsets = [u32(), u32(), u32(), u32()];
    const lodCounts = [u32(), u32(), u32(), u32()];
    assert.ok(bank <= bankCount);
    assert.ok(indexBytes === 2 || indexBytes === 4);
    assert.ok(uniqueVertexCount > 0 && uniqueVertexCount <= 2000000);
    assert.ok(totalIndexCount > 0 && totalIndexCount % 3 === 0);
    if (indexBytes === 2) assert.ok(uniqueVertexCount <= 65535);
    for (let k = 0; k < 4; ++k) {
        assert.ok(lodCounts[k] > 0 && lodCounts[k] % 3 === 0, 'Invalid LOD index count: ' + name);
        assert.ok(lodOffsets[k] + lodCounts[k] <= totalIndexCount, 'LOD range out of bounds: ' + name);
    }
    const { clipNames, gear } = bank ? banks[bank - 1] : { clipNames: [], gear: new Map() };
    if (bank) assert.equal(bones, banks[bank - 1].bones);
    const report = manifest.assets[asset];
    const recipe = name.startsWith('character/') ? recipes[path.basename(source)] : undefined;
    const skinColor = recipe?.skin ? glbDocument(source).materials.find(material => material.name === recipe.skin)
        ?.pbrMetallicRoughness?.baseColorFactor : undefined;
    assert.ok(!names.has(name)); names.add(name);
    assert.equal(name, report.name); assert.equal(source, report.source);
    assert.equal(lodCounts[0], report.triangles * 3); assert.equal(bones, report.bones);
    assert.equal(uniqueVertexCount, report.uniqueVertices);
    assert.equal(indexBytes, report.indexBytes);
    assert.ok(bones < 72);
    assert.equal(bank, report.animationBank);
    const bounds = floats(6), physical = floats(3), sockets = floats(18);
    for (let axis = 0; axis < 3; ++axis) {
        assert.ok(bounds[axis] <= bounds[axis + 3]);
        const flatRoadSurface = name.startsWith('transit/') && axis === 2 &&
            Math.abs(bounds[5] - bounds[2]) < 1e-6;
        assert.ok(physical[axis] > 0 || (flatRoadSurface && physical[axis] === 0));
    }
    if (name.startsWith('vehicle/')) {
        assert.equal(bones, 16);
        assert.ok(sockets[0] < sockets[6] && sockets[3] < sockets[9], 'Reversed vehicle attachments');
        assert.ok(sockets[1] > sockets[4] && sockets[7] > sockets[10], 'Reversed left/right sockets');
    }
    if (name.startsWith('character/')) assert.equal(bones, 23);
    if (name.startsWith('aircraft/')) {
        assert.equal(bones, 17);
        assert.ok(physical[0] > 2 && physical[1] > 3 && physical[2] > .8, 'Aircraft must retain meter-derived scale');
    }
    if (name.startsWith('scenery/City_TrafficSignal')) assert.ok(bounds[2] > 2.3, 'Traffic head must hover above the road');
    if (name === 'scenery/City_PedestrianHologram_6m') assert.ok(bounds[5] < .076, 'Pedestrian signal must remain at floor level');
    const vertexStart = offset;
    for (let vertex = 0; vertex < uniqueVertexCount; ++vertex) {
        const pos = floats(3);
        const onx = i16(), ony = i16();
        const hu = u16(), hv = u16();
        assert.ok((hu & 0x7c00) !== 0x7c00 && (hv & 0x7c00) !== 0x7c00, 'Nonfinite half UV');
        const rgba = [u8() / 255, u8() / 255, u8() / 255, u8() / 255];
        const palette = u8(), visibility = u8(), surface = u8(), flags = u8();
        const joints = [u8(), u8(), u8(), u8()];
        const weights = [u8(), u8(), u8(), u8()];
        for (let axis = 0; axis < 3; ++axis) {
            assert.ok(pos[axis] >= bounds[axis] - 1e-5 && pos[axis] <= bounds[axis + 3] + 1e-5, 'Bounds mismatch');
        }
        const normal = decodeOct(onx, ony);
        assert.ok(Math.abs(Math.hypot(...normal) - 1) < 1e-4, 'Invalid normal');
        assert.ok([0, 1, 2, 3].includes(palette), 'Invalid palette slot');
        assert.ok([0, 1, 2, 3, 4].includes(visibility), 'Invalid visibility');
        assert.ok(flags <= 1, 'Invalid vertex flags');
        let sum = 0;
        for (let weight = 0; weight < 4; ++weight) {
            const joint = joints[weight], amount = weights[weight];
            assert.ok(joint < bones || joint === 31 || joint === 71, 'Invalid skin joint');
            sum += amount;
        }
        assert.equal(sum, 255, 'Unnormalized packed skin weights');
        if (name.startsWith('ground/')) assert.equal(pos[2], 0, 'Ground must meet actor floor');
        if (skinColor && surface === 7) for (let channel = 0; channel < 4; ++channel)
            assert.ok(Math.abs(rgba[channel] - skinColor[channel]) <= (1 / 255) + 1e-6,
                'Authored character skin color was replaced: ' + name);
    }
    const indexStart = offset;
    for (let i = 0; i < totalIndexCount; ++i) {
        const idx = indexBytes === 2 ? u16() : u32();
        assert.ok(idx < uniqueVertexCount, 'Index out of vertex bounds: ' + name);
    }
    if (name.startsWith('scenery/City_TrafficSignal'))
        signalDigests.add(crypto.createHash('sha256').update(data.subarray(vertexStart, offset)).digest('hex'));
    assert.deepEqual(clipNames, report.clips);
    if (name.startsWith('character/')) for (const clip of ['Idle', 'Run', 'Aim_Base', 'Attack_Fire', 'Ride']) assert.ok(clipNames.includes(clip));
    if (recipe) {
        const sourceClips = glbDocument(source).animations.map(clip => clip.name);
        const supportClips = glbDocument('AnimationBanks/HUMAN_7H_Support').animations.map(clip => clip.name);
        const transitionClips = glbDocument('AnimationBanks/HUMAN_7H_Transitions').animations.map(clip => clip.name);
        for (const clip of ['Idle', 'Walk', 'Run', 'Aim_Base', 'Attack_Fire', 'Attack_Melee', 'Jump_Air'])
            assert.ok(sourceClips.includes(clip), 'Required HUMAN_7H source clip missing: ' + clip);
        for (const clip of ['Support_Treat_Enter', 'Support_Treat_Loop', 'Support_Treat_Exit',
            'Support_Inspect_Enter', 'Support_Inspect_Loop', 'Support_Inspect_Exit',
            'Support_Carry_Idle', 'Support_Carry_Walk'])
            assert.ok(supportClips.includes(clip), 'Required support bank clip missing: ' + clip);
        for (const clip of ['Run_Turn180_Left', 'Run_Turn180_Right', 'Turn90_Left', 'Turn90_Right',
            'Aim_Strafe_Start_Forward', 'Aim_Strafe_Stop_Forward'])
            assert.ok(transitionClips.includes(clip), 'Required transition bank clip missing: ' + clip);
        assert.deepEqual(clipNames, [...sourceClips, 'Ride', ...supportClips, ...transitionClips],
            'Human source and shared bank animations must all survive conversion');
    }
    if (name.startsWith('vehicle/')) for (const clip of ['Hover_Idle', 'Cruise']) assert.ok(clipNames.includes(clip));
    if (name.startsWith('aircraft/')) for (const clip of ['Aircraft_Reference', 'Cruise', 'LandingGear_Retract', 'LandingGear_Deploy', 'Canopy_Open', 'Cargo_Open']) assert.ok(clipNames.includes(clip));
    if (name.startsWith('aircraft/')) assert.notDeepEqual(gear.get('Cruise'), gear.get('Aircraft_Reference'), 'Cruising landing gear must retract');
    vertices += lodCounts[0];
    uniqueVerticesTotal += uniqueVertexCount;
    lod0IndicesTotal += lodCounts[0];
    shadowIndicesTotal += lodCounts[3];
}
assert.equal(offset, data.length, 'Unexpected trailing data');
const mixamoCatalogPath = path.join(root, 'Resource/Documentation/mixamo_characters.json');
if (fs.existsSync(mixamoCatalogPath)) {
    const mixamo = JSON.parse(fs.readFileSync(mixamoCatalogPath, 'utf8'));
    assert.equal(mixamo.schema, 'mixamo_3tier_v1');
    for (const [prefix, list, expectSkinned] of [
        ['modular/body/', mixamo.bodies ?? [], true],
        ['modular/outfit/', mixamo.outfits ?? [], true],
        ['modular/part/', mixamo.attachments ?? [], null]
    ]) {
        for (const item of list) {
            const report = manifest.assets.find(asset => asset.name === prefix + item.id);
            assert.ok(report, 'Modular Mixamo asset missing from pack: ' + prefix + item.id);
            assert.equal(report.source, item.source);
            const rigid = expectSkinned === null ? (item.skinned === false || Boolean(item.socket)) : !expectSkinned;
            if (rigid) {
                assert.equal(report.bones, 0, 'Rigid Mixamo socket part must have 0 bones: ' + item.id);
            } else {
                assert.ok(report.bones > 0 && report.bones <= (mixamo.max_bones ?? 71),
                    'Skinned Mixamo part bone count out of range: ' + item.id);
                assert.ok(report.animationBank > 0, 'Skinned Mixamo part missing rig bank: ' + item.id);
            }
            if (item.lod0_max_tris) {
                assert.ok(report.triangles <= item.lod0_max_tris,
                    `Mixamo LOD0 budget exceeded for ${item.id}: ${report.triangles}/${item.lod0_max_tris}`);
            }
            if (report.triangles >= 250) {
                assert.ok(report.lods.lod1 < report.lods.lod0, 'Mixamo LOD1 must simplify LOD0: ' + item.id);
                assert.ok(report.lods.shadow < report.lods.lod0, 'Mixamo shadow proxy must simplify LOD0: ' + item.id);
            }
        }
    }
}
assert.ok(resourcePackStatus(root).current, 'Runtime pack must match current sources and converter');
assert.equal([...names].filter(n => n.startsWith('aircraft/')).length, 8);
assert.equal([...names].filter(n => n.startsWith('scenery/')).length, 36);
assert.equal(signalDigests.size, 3, 'Red/amber/green traffic heads must differ');
for (const [name, file] of [['SpireHelix', 'Spire_Helix_12F'], ['TwinBridge', 'Tower_TwinBridge_16F'],
    ['ArcologyTerrace', 'Arcology_Terrace_10F'], ['TransitAtrium', 'Civic_TransitAtrium_4F'],
    ['CourtyardHabitat', 'Habitat_Courtyard_3F']]) {
    const report = manifest.assets.find(asset => asset.name === 'building/' + name);
    assert.ok(report, 'New architecture assembly missing: ' + name);
    assert.equal(report.source, 'Buildings/' + file);
    assert.equal(report.triangles, sourceTriangles('Buildings/' + file), 'Architecture conversion lost authored detail: ' + name);
    assert.deepEqual(report.bounds[0], [0, 0, 0]);
    assert.equal(report.bounds[1][0], 1);
    assert.equal(report.bounds[1][1], 1);
    assert.ok(report.physical_world_units.every(size => size > 2));
}
const measuredBuildings = JSON.parse(fs.readFileSync(path.join(root, 'Resource/Documentation/measured_buildings.json')));
for (const name of Object.keys(measuredBuildings.prefabs)) {
    const report = manifest.assets.find(asset => asset.name === 'building/' + name);
    assert.ok(report, 'Measured building missing: ' + name);
    assert.equal(report.source, 'MeasuredBuildings/' + name);
    assert.equal(report.triangles, sourceTriangles('MeasuredBuildings/' + name), 'Measured building lost authored detail: ' + name);
    assert.deepEqual(report.bounds[0], [0, 0, 0]);
    assert.equal(report.bounds[1][0], 1);
    assert.equal(report.bounds[1][1], 1);
    assert.equal(report.bounds[1][2], 1);
    assert.ok(report.physical_world_units.every(size => size > 1.5));
}
for (const report of manifest.assets.filter(asset => asset.name.startsWith('character/'))) {
    const gender = report.name.split('/')[1], recipe = recipes[path.basename(report.source)];
    const body = gender + '_' + recipe.body.split('_')[1];
    const parts = new Set(recipe.parts.filter(part => !part.startsWith('Hair'))
        .map(part => part.replace(/(?:Male|Female)_(?:Lean|Normal|Heavy)/g, body)));
    for (const part of [...parts]) {
        const outer = characterDetail.outer_assemblies[part];
        if (/_Torso$/.test(part)) assert.ok(outer, 'Missing matching outer assembly: ' + part);
        if (outer) parts.add(outer);
    }
    let triangles = 0;
    for (const part of parts) {
        assert.ok(manifest.sources['Modules/' + part], 'Character module not included: ' + part);
        triangles += sourceTriangles('Modules/' + part);
    }
    assert.equal(report.triangles, triangles, 'Character gender/profile/outer assembly is incomplete or duplicated: ' + report.name);
}
// Road delivery is kept at authored scale and every independent part survives conversion.
const roadPlan = JSON.parse(fs.readFileSync(path.join(root, 'Resource/Documentation/road_plan_modules.json')));
for (const [name, module] of Object.entries(roadPlan.modules)) {
    const report = manifest.assets.find(asset => asset.name === 'transit/' + name);
    assert.ok(report, 'Road-plan module missing: ' + name);
    assert.equal(report.triangles, sourceTriangles('RoadPlan/' + name), 'Road detail was lost: ' + name);
    for (let axis = 0; axis < 3; ++axis) {
        // Optimized visible surfaces may omit buried faces; the collision/port envelope stays unchanged.
        const visible = Object.values(module.render_bounds ?? { original: module.bounds });
        const minimum = Math.min(...visible.map(b => b.min[axis])) * .5;
        const maximum = Math.max(...visible.map(b => b.max[axis])) * .5;
        assert.ok(Math.abs(report.bounds[0][axis] - minimum) < .00001, 'Road render minimum: ' + name);
        assert.ok(Math.abs(report.bounds[1][axis] - maximum) < .00001, 'Road render maximum: ' + name);
    }
}
console.log(JSON.stringify({ event: 'resource_pack_validated', schema: 'v4', assets: count,
    lod0_triangles: lod0IndicesTotal / 3, shadow_triangles: shadowIndicesTotal / 3,
    unique_vertices: uniqueVerticesTotal, animation_frames: animationFrames, bytes: data.length }));
