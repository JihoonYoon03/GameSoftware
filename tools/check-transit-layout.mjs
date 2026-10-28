// Validate the actual road-first source plan, independently of rendering and mesh ports.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const source = fs.readFileSync(path.join(root, 'SimpleGame/Overworld.cpp'), 'utf8');
const transit = fs.readFileSync(path.join(root, 'SimpleGame/TransitLayout.cpp'), 'utf8');
const metadata = JSON.parse(fs.readFileSync(path.join(root, 'Resource/Documentation/road_plan_modules.json'))).modules;
const names = new Set(JSON.parse(fs.readFileSync(path.join(root, 'Resource/Runtime/manifest.json'))).assets.map(a => a.name));
for (const name of Object.keys(metadata)) assert(names.has('transit/' + name));
assert(transit.includes('RoadPlan_Ramp_2L_Rise8p4_L110'));
assert(!transit.includes('procedural/road_plan_v2'));
assert(!source.includes('bool merged = true'), 'Never merge close crossings into a mega-junction');
const cities = [[0, 0]];
let seed = 0x458a91;
for (let i = 1; i < 5; ++i) {
    seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
    const angle = .25 + (i - 1) * 1.57 + (seed % 350) / 1000, radius = 260 + ((seed >>> 12) % 85);
    cities.push([Math.round(Math.cos(angle) * radius), Math.round(Math.sin(angle) * radius)]);
}
const add = (a, b) => a.map((v, i) => v + b[i]), sub = (a, b) => a.map((v, i) => v - b[i]);
const mul = (a, t) => a.map(v => v * t), cross = (a, b) => a[0] * b[1] - a[1] * b[0];
const length = a => Math.hypot(...a), dot = (a,b) => a.reduce((s,v,i)=>s+v*b[i],0);
const number = value => Number(value.trim().replace(/f$/, ''));
function argumentsOf(text) {
    let depth = 0, start = 0; const args = [];
    for (let i=0;i<text.length;++i) {
        if ('({['.includes(text[i])) ++depth;
        if (')}]'.includes(text[i])) --depth;
        if (text[i] === ',' && depth === 0) { args.push(text.slice(start,i).trim());start=i+1; }
    }
    args.push(text.slice(start).trim());return args;
}
function point(text, center) {
    if (text === 'Overworld::kMineEntrance') return [128,18];
    if (text.startsWith('p(')) return add(center, argumentsOf(text.slice(2,-1)).map(number));
    if (text.startsWith('Add(')) {
        const [city, offset] = argumentsOf(text.slice(4,-1));
        return add(cities[{cinder:1,south:2,west:3,north:4}[city]],point(offset,center));
    }
    assert(text.startsWith('{') && text.endsWith('}'), 'Unknown plan coordinate: '+text);
    return argumentsOf(text.slice(1,-1)).map(number);
}
const block = source.slice(source.indexOf('        int collector ='), source.indexOf('    auto halo ='));
const sources = [];
for (const match of block.matchAll(/add\(([\s\S]*?)\);/g)) {
    const args = argumentsOf(match[1]), local = args[0].startsWith('p(');
    for (const city of local ? [0,1,2,3,4] : [0]) {
        const lanes = args[2] === 'collector' ? (city <= 1 || city === 4 ? 4 : 2) : number(args[2]);
        sources.push({a:point(args[0],cities[city]),b:point(args[1],cities[city]),lanes,
                      za:args.length>4?number(args[4]):0,zb:args.length>5?number(args[5]):0});
    }
}
assert.equal(sources.filter(s=>s.za || s.zb).length,3,'Two ramps and one independent bridge');
const nodes=[], roads=[];
function node(p,z) {
    let index=nodes.findIndex(n=>length(sub(n.p,p))<.12 && Math.abs(n.z-z)<.05);
    if(index<0) { index=nodes.length;nodes.push({p,z}); } return index;
}
for(const s of sources) {
    const d=sub(s.b,s.a),cuts=[0,1];
    for(const other of sources) {
        const e=sub(other.b,other.a),den=cross(d,e);if(Math.abs(den)<.001)continue;
        const q=sub(other.a,s.a),t=cross(q,e)/den,u=cross(q,d)/den;
        if(t>=-.0001 && t<=1.0001 && u>=-.0001 && u<=1.0001 &&
           Math.abs(s.za+(s.zb-s.za)*t-other.za-(other.zb-other.za)*u)<.05) cuts.push(Math.max(0,Math.min(1,t)));
    }
    cuts.sort((a,b)=>a-b);
    for(let i=1;i<cuts.length;++i) {
        const a=add(s.a,mul(d,cuts[i-1])),b=add(s.a,mul(d,cuts[i]));
        if(length(sub(a,b))<.2)continue;
        roads.push({from:node(a,s.za+(s.zb-s.za)*cuts[i-1]),to:node(b,s.za+(s.zb-s.za)*cuts[i]),lanes:s.lanes});
    }
}
let maxArms=0,maxLanes=0,minimumSpan=Infinity;
for(let i=0;i<nodes.length;++i) {
    const arms=roads.filter(r=>r.from===i || r.to===i),widest=Math.max(...arms.map(r=>r.lanes));
    maxArms=Math.max(maxArms,arms.length);maxLanes=Math.max(maxLanes,arms.reduce((s,r)=>s+r.lanes,0));
    const closest=Math.min(...arms.map(r=>length(sub(nodes[r.from].p,nodes[r.to].p))));
    let radius=Math.min(widest*.75+2,(closest-8)*.5);
    if(arms.length===2 && arms[0].lanes===arms[1].lanes) {
        const ds=arms.map(r=>sub(nodes[r.from===i?r.to:r.from].p,nodes[i].p));
        if(dot(ds[0],ds[1])/length(ds[0])/length(ds[1])<-.9999) radius=0;
    }
    if(arms.length===2 && arms[0].lanes!==arms[1].lanes) radius=6;
    nodes[i].radius=radius;
}
assert(maxArms<=4 && maxLanes<=10,`Overloaded junction: ${maxArms} arms/${maxLanes} incident lanes`);
for(const r of roads) {
    const span=length(sub(nodes[r.from].p,nodes[r.to].p))-nodes[r.from].radius-nodes[r.to].radius;
    assert(span>=7.99);minimumSpan=Math.min(minimumSpan,span);
    assert(Math.abs(nodes[r.from].z-nodes[r.to].z)/span<=.09);
}
assert(!nodes.some(n=>length(sub(n.p,[48,-17]))<.1),'Grade-separated crossing must have no shared node');
const reached=new Set([0]),queue=[0];
for(let i=0;i<queue.length;++i)for(const r of roads) {
    const next=r.from===queue[i]?r.to:r.to===queue[i]?r.from:-1;
    if(next>=0 && !reached.has(next)) {reached.add(next);queue.push(next);}
}
assert.equal(reached.size,nodes.length);
console.log(JSON.stringify({event:'road_first_plan_validated',modules:Object.keys(metadata).length,
    junctions:nodes.length,roads:roads.length,max_arms:maxArms,max_incident_lanes:maxLanes,
    minimum_span:minimumSpan,connected:true,grade_separated:true}));
