const fs=require('node:fs'),path=require('node:path');
const {pathToFileURL}=require('node:url');
const root=path.resolve(__dirname,'..');
const bundle=process.env.UI_NODE_MODULES||path.resolve(path.dirname(process.execPath),'..','node_modules');
const {chromium}=require(require.resolve('playwright',{paths:[bundle]}));
(async()=>{
 const browser=await chromium.launch({headless:true,executablePath:process.env.UI_BROWSER_PATH||'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 try{
  const page=await browser.newPage();await page.goto(pathToFileURL(path.join(root,'preview.html')).href);
  await page.selectOption('#scene','05_dialogue');await page.click('#flowNext');
  const next=await page.locator('#scene').inputValue();
  await page.keyboard.press('PageUp');const previous=await page.locator('#scene').inputValue();
  await page.selectOption('#scene','47_notice_page_01');await page.click('#flowNext');
  const noticeNext=await page.locator('#scene').inputValue();
  await page.selectOption('#scene','62_settlement_hold_01');const settlementFitsOnePage=await page.locator('#flowNext').isDisabled();
  await page.selectOption('#scene','63_planet_failure_01');await page.click('#flowNext');
  const failureNext=await page.locator('#scene').inputValue();
  await page.keyboard.press('PageUp');const failurePrevious=await page.locator('#scene').inputValue();
  await page.selectOption('#scene','50_story_tracking');
  await page.locator('#opacity').fill('80');await page.locator('#opacity').dispatchEvent('input');
  const journalOpacity=await page.locator('.panel-fill').first().evaluate(e=>getComputedStyle(e).opacity);
  await page.click('#background');const journalBackgroundHidden=await page.locator('.world-background').first().evaluate(e=>getComputedStyle(e).display==='none');
  const result={status:next.startsWith('45_dialogue_page_')&&previous==='05_dialogue'&&noticeNext==='47_notice_page_02'&&settlementFitsOnePage&&failureNext==='63_planet_failure_02'&&failurePrevious==='63_planet_failure_01'&&journalOpacity==='0.8'&&journalBackgroundHidden?'PASS':'FAIL',dialogueNext:next,dialoguePrevious:previous,noticeNext,settlementFitsOnePage,failureNext,failurePrevious,journalOpacity,journalBackgroundHidden};
  fs.writeFileSync(path.join(root,'Validation/preview_navigation.json'),JSON.stringify(result,null,2)+'\n');
  console.log(result);if(result.status!=='PASS')process.exitCode=1;
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exit(1)});
