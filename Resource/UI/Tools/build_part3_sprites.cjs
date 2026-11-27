// Compatibility entry point. Both previews and sprites now share one authoring source.
const {spawnSync}=require('node:child_process');
const path=require('node:path');
for(const [program,args] of [
  [process.env.UI_PYTHON||'python',[path.join(__dirname,'build_ui.py')]],
  [process.execPath,[path.join(__dirname,'render_ui.cjs'),'--sprites-only']],
  [process.env.UI_PYTHON||'python',[path.join(__dirname,'build_complete_atlas.py')]]
]){
  const result=spawnSync(program,args,{stdio:'inherit'});
  if(result.error)throw result.error;
  if(result.status!==0)process.exit(result.status||1);
}
