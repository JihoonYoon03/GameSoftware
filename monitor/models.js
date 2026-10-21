"use strict";
let catalog = null;
const grid = document.getElementById("model-grid");
const status = document.getElementById("catalog-status");
function rgb(id) {
  const hex = document.getElementById(id).value;
  return [1,3,5].map((offset) => parseInt(hex.slice(offset,offset+2),16)/255);
}
function color(part) {
  const tint = part.palette === 1 ? rgb("body-color") : part.palette === 2
    ? rgb("accent-color") : part.palette === 3 ? rgb("emission-color") : [1,1,1];
  return "rgba(" + part.color.slice(0,3).map((v,i)=>Math.round(v*tint[i]*255)).join(",") + "," + part.color[3] + ")";
}
function project(model, p) {
  const building = model.category === "building";
  const width = model.preview_scale?.[0] ?? (building ? (model.type === 4 ? 5 : 2.5) : 1);
  const depth = model.preview_scale?.[1] ?? (building ? (model.type === 4 ? 4 : 2.5) : 1);
  const height = model.preview_scale ? model.preview_scale[2]*44 : building ? (model.type === 4 ? 170 : 130) : 44;
  return model.space === "world" ? [(p[0]*width-p[1]*depth)*36,(p[0]*width+p[1]*depth)*18-p[2]*height] : p;
}
function draw(model, canvas) {
  const context = canvas.getContext("2d");
  context.clearRect(0,0,canvas.width,canvas.height);
  let points = model.parts.flatMap((part) => {
    if (part.kind === 1) {
      const [p,r] = part.points;
      return [[p[0]-r[0],p[1]-r[1]],[p[0]+r[0],p[1]+r[1]]];
    }
    return part.points.map((p)=>project(model,p));
  });
  const minX = Math.min(...points.map((p)=>p[0])), maxX = Math.max(...points.map((p)=>p[0]));
  const minY = Math.min(...points.map((p)=>p[1])), maxY = Math.max(...points.map((p)=>p[1]));
  const scale = Math.min((canvas.width-70)/(maxX-minX),(canvas.height-65)/(maxY-minY),3);
  context.save();
  context.translate(canvas.width/2-(minX+maxX)*scale/2,canvas.height/2-(minY+maxY)*scale/2);
  context.scale(scale,scale);
  const depth = (part) => part.points.reduce((sum,p)=>sum+p[0]+p[1]+p[2]*.81818,0)/part.points.length;
  const parts = model.space === "world" ? [...model.parts].sort((a,b)=>depth(a)-depth(b)) : model.parts;
  for (const part of parts) {
    context.fillStyle = context.strokeStyle = color(part);
    context.lineWidth = part.width;
    context.beginPath();
    const p = project(model,part.points[0]);
    if (part.kind === 1) context.ellipse(p[0],p[1],part.points[1][0],part.points[1][1],0,0,Math.PI*2);
    else {
      context.moveTo(p[0],p[1]);
      for (const point of part.points.slice(1)) {
        const q = project(model,point); context.lineTo(q[0],q[1]);
      }
      if (part.kind !== 2) context.closePath();
    }
    if (part.kind === 2) context.stroke(); else context.fill();
  }
  if (model.category === "vehicle") {
    for (const socket of model.exhausts) {
      const point = project(model,socket);
      const tail = project(model,[socket[0]-.5,socket[1],socket[2]]);
      context.fillStyle = "#ff6969"; context.beginPath(); context.arc(point[0],point[1],2,0,Math.PI*2); context.fill();
      context.strokeStyle = "#ff6969"; context.lineWidth = 1;
      context.beginPath(); context.moveTo(point[0],point[1]); context.lineTo(tail[0],tail[1]); context.stroke();
    }
    for (const lamp of model.headlights) {
      const point = project(model,lamp);
      context.fillStyle = "#ffe59a"; context.beginPath(); context.arc(point[0],point[1],2,0,Math.PI*2); context.fill();
    }
  }
  context.restore();
}
function render() {
  if (!catalog) return;
  grid.replaceChildren(...catalog.models.map((model) => {
    const card = document.createElement("article"); card.className = "panel model-card";
    const title = document.createElement("h3"); title.textContent = model.name + (model.gender ? " / " + (model.gender === "female" ? "여성형" : "남성형") : "");
    const validation = document.createElement("small");
    validation.textContent = model.validated ? "면·좌표 검사 통과" : "구조 검사 실패";
    title.append(validation);
    const canvas = document.createElement("canvas"); canvas.width = 420; canvas.height = 280;
    const note = document.createElement("p"); note.className = "muted small";
    note.textContent = (model.triangles ? model.triangles + "개 삼각형" : model.parts.length + "개 파츠") +
      " · " + (model.space==="world"?"쿼터뷰 월드 좌표":"화면 좌표") + (model.source ? " · " + model.source : "");
    card.append(title,canvas,note); draw(model,canvas);
    return card;
  }));
}
async function load() {
  const controller = new AbortController();
  const timeout = setTimeout(()=>controller.abort(),3000);
  try {
    const response = await fetch("/api/models",{cache:"no-store",signal:controller.signal});
    if (!response.ok) throw new Error("HTTP "+response.status);
    const data = await response.json();
    if (data.schema !== "game_models_v1" || !Array.isArray(data.models)) throw new Error("모델 스키마 불일치");
    catalog = data; render();
    status.textContent = data.models.length + "종 실제 캐시 모델을 불러왔습니다. 형태별 색상을 변경해 비교하세요.";
  } catch(error) {
    status.textContent = "게임 실행 후 http://127.0.0.1:17707/models.html 로 접속하세요. "+error.message;
    setTimeout(load,2000);
  } finally { clearTimeout(timeout); }
}
for (const id of ["body-color","accent-color","emission-color"]) document.getElementById(id).addEventListener("input",render);
if (location.protocol==="file:") status.textContent="게임 실행 후 http://127.0.0.1:17707/models.html 로 접속하세요.";
else load();
