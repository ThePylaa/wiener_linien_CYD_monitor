// Setup web app served by the display on the home network
// (http://wien-monitor.local or the IP shown on the display's setup screen).
// Plain HTML/CSS/JS, no external resources: the page must work without
// internet access for the phone and stays small enough to live in flash.
//
// The stop list and line data are downloaded from the display once, cached in
// the browser and searched/browsed locally (falls back to /api/search while
// they are not available yet).
#pragma once

static const char kSetupPageHtml[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#ffffff" media="(prefers-color-scheme: light)">
<meta name="theme-color" content="#0b0b0b" media="(prefers-color-scheme: dark)">
<title>Wien Monitor</title>
<style>
:root{--paper:#fff;--ink:#0a0a0a;--mute:#6e6e6e;--hair:#dadada;--soft:#f3f3f1;--red:#e30613;--g:16px;--col:52px}
@media(prefers-color-scheme:dark){:root{--paper:#0b0b0b;--ink:#f2f2f0;--mute:#8e8e8e;--hair:#272727;--soft:#161616;--red:#ff3b30}}
*{box-sizing:border-box;margin:0;padding:0}
html{-webkit-text-size-adjust:100%;scroll-padding-top:56px}
body{background:var(--paper);color:var(--ink);font:16px/1.35 "Helvetica Neue",Helvetica,Arial,"Nimbus Sans","Liberation Sans",sans-serif;font-variant-numeric:tabular-nums;-webkit-font-smoothing:antialiased;-webkit-tap-highlight-color:transparent}
button{font:inherit;color:inherit;background:none;border:0;cursor:pointer;border-radius:0}
ul,ol{list-style:none}
[hidden]{display:none!important}
.wrap{max-width:760px;margin:0 auto;padding:0 var(--g)}
/* masthead */
.mast{padding-top:20px}
.mast-top{display:flex;align-items:center;gap:10px;font-size:11px;font-weight:700;letter-spacing:.09em;text-transform:uppercase}
.sq{width:14px;height:14px;background:var(--red);flex:none}
.mast-row{display:flex;align-items:flex-end;justify-content:space-between;gap:12px;margin-top:22px}
h1{font-size:clamp(58px,19vw,128px);font-weight:700;letter-spacing:-.05em;line-height:.84}
#clk{width:clamp(60px,17vw,96px);height:auto;flex:none}
.ck-face{fill:var(--paper);stroke:var(--ink);stroke-width:2}
.ck-t{fill:var(--ink)}.ck-h{stroke:var(--ink)}.ck-s{stroke:var(--red);fill:var(--red)}
.conn{border-top:2px solid var(--ink);margin-top:18px;padding:9px 0 0;font-size:13px;color:var(--mute);display:flex;flex-wrap:wrap;gap:4px 14px}
.conn b{color:var(--ink);font-weight:700}
.banner{background:var(--red);color:#fff;font-weight:700;padding:12px var(--g);font-size:15px}
/* index */
.toc{position:sticky;top:0;z-index:10;background:var(--paper);border-bottom:2px solid var(--ink);margin-top:28px}
.toc .wrap{display:flex}
.toc a{flex:1;padding:13px 0 11px;color:var(--ink);text-decoration:none;font-size:13px;font-weight:700;white-space:nowrap}
.toc a b{color:var(--red);margin-right:5px}
/* sections */
.sec{padding-top:44px;padding-bottom:8px}
.sec-h{display:grid;grid-template-columns:var(--col) 1fr auto;align-items:baseline;border-top:2px solid var(--ink);padding-top:10px;margin-bottom:22px}
.no{color:var(--red);font-weight:700;font-size:15px}
h2{font-size:32px;font-weight:700;letter-spacing:-.03em;line-height:1}
.aside{font-size:12px;color:var(--mute);text-align:right}
@media(min-width:620px){.in{margin-left:var(--col)}}
.lbl{display:flex;justify-content:space-between;align-items:baseline;gap:10px;font-size:11px;font-weight:700;letter-spacing:.09em;text-transform:uppercase;color:var(--mute);margin:28px 0 10px}
.lbl span{letter-spacing:0;text-transform:none;font-weight:400}
.hint{color:var(--mute);font-size:14px;margin:12px 0}
.sm{color:var(--mute);font-size:13px}
.spin{width:14px;height:14px;border:2px solid var(--hair);border-top-color:var(--red);border-radius:50%;animation:sp .8s linear infinite;display:inline-block;vertical-align:-2px;margin-right:8px}
@keyframes sp{to{transform:rotate(360deg)}}
/* controls */
.btn{display:inline-flex;align-items:center;justify-content:center;gap:8px;font-weight:700;font-size:15px;padding:13px 16px;border:2px solid var(--ink);color:var(--ink)}
.btn:active{background:var(--ink);color:var(--paper)}
.btn.pri{background:var(--red);border-color:var(--red);color:#fff;width:100%;font-size:18px;padding:16px}
.btn.sm{padding:8px 12px;font-size:13px;border-width:1.5px}
.btn:disabled{opacity:.4}
.link{font-weight:700;text-decoration:underline;text-underline-offset:3px;text-decoration-thickness:1.5px;padding:14px 0;text-align:left}
.link.sm{padding:2px 0;color:var(--mute);font-size:12px}
.danger{color:var(--red);border-color:var(--red)}
.acts{display:flex;flex-wrap:wrap;gap:8px;margin-top:20px}
.seg{display:flex;border:2px solid var(--ink)}
.seg button{flex:1 1 0;min-width:0;padding:12px 2px;font-weight:700;font-size:14px;border-right:1px solid var(--ink);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.seg button:last-child{border-right:0}
.seg button.on{background:var(--ink);color:var(--paper)}
.txt{width:100%;font:inherit;font-size:18px;padding:10px 0;border:0;border-bottom:2px solid var(--ink);background:transparent;color:var(--ink);outline:0;border-radius:0}
.txt:focus{border-color:var(--red)}
input[type=range]{width:100%;accent-color:var(--red);height:32px}
/* badges */
.bd{display:inline-flex;align-items:center;justify-content:center;min-width:28px;height:20px;padding:0 5px;margin:0 3px 3px 0;font-weight:700;font-size:12px;line-height:1;color:#fff;background:#6e6e6e;vertical-align:middle;letter-spacing:-.01em}
.bd.md{min-width:38px;height:26px;font-size:15px;margin:0}
.bd.lg{min-width:54px;height:46px;font-size:19px;margin:0}
.bd.xl{min-width:76px;height:64px;font-size:30px;margin:0;letter-spacing:-.03em}
.c-u1{background:#e3000f}.c-u2{background:#a862a4}.c-u3{background:#ee7d00}.c-u4{background:#319f49}.c-u5{background:#2aa3a3}.c-u6{background:#9d6830}
.c-tram{background:#c4161c}.c-wlb{background:#0d4c95}.c-bus{background:#1f4e8c}.c-night{background:#141b3c;color:#ffd200}.c-ruf{background:#5c6970}.c-s{background:#0089cf}
.more{font-size:12px;color:var(--mute);vertical-align:middle}
/* stops */
.stop{display:grid;grid-template-columns:var(--col) 1fr auto;gap:0 8px;padding:16px 0;border-bottom:1px solid var(--hair);position:relative}
.stop:first-child{border-top:1px solid var(--hair)}
.stop.on::before{content:"";position:absolute;left:calc(var(--g)*-1);top:-1px;bottom:-1px;width:5px;background:var(--red)}
.stop .n{font-weight:700;font-size:15px;color:var(--mute);padding-top:3px}
.stop.on .n{color:var(--red)}
.stop .b{min-width:0;cursor:pointer}
.stop .t{font-weight:700;font-size:21px;letter-spacing:-.02em;line-height:1.1;overflow-wrap:anywhere}
.stop .meta{margin-top:6px}
.tag{font-size:10px;font-weight:700;letter-spacing:.09em;text-transform:uppercase;color:var(--red);white-space:nowrap;padding-top:6px}
.dep{margin-top:10px;border-top:1px solid var(--hair)}
.dep li{display:grid;grid-template-columns:44px 1fr auto;align-items:center;gap:8px;padding:7px 0;border-bottom:1px solid var(--hair);font-size:14px}
.dep .to{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.mins{text-align:right;white-space:nowrap}
.mins b{font-size:20px;font-weight:700;letter-spacing:-.02em}
.mins small{color:var(--mute);font-size:13px;margin-left:6px}
.empty{padding:22px 0;border-top:1px solid var(--hair);border-bottom:1px solid var(--hair);color:var(--mute)}
.alert{border-left:4px solid var(--red);background:var(--soft);padding:9px 12px;margin:10px 0;font-size:13px;line-height:1.4}
.alert b{display:block;font-size:14px;margin-bottom:2px}
.stop .alert span{display:-webkit-box;-webkit-line-clamp:3;-webkit-box-orient:vertical;overflow:hidden}
/* add */
.tabs{display:flex;border-bottom:1px solid var(--hair);margin-bottom:18px}
.tabs button{padding:12px 0 10px;margin-right:24px;font-weight:700;font-size:16px;color:var(--mute);border-bottom:3px solid transparent;margin-bottom:-1px}
.tabs button.on{color:var(--ink);border-color:var(--red)}
.q{display:flex;align-items:center;gap:10px;border-bottom:3px solid var(--ink)}
.q:focus-within{border-color:var(--red)}
.q input{flex:1;min-width:0;border:0;background:transparent;color:var(--ink);font:inherit;font-size:24px;font-weight:500;letter-spacing:-.015em;padding:12px 0;outline:0;-webkit-appearance:none;border-radius:0}
.q input::placeholder{color:var(--mute);opacity:.7}
.row{display:grid;grid-template-columns:1fr auto;align-items:center;gap:12px;padding:13px 0;border-bottom:1px solid var(--hair);cursor:pointer}
.row:active,.route li:active,.dir:active{background:var(--soft)}
.row .t{font-weight:700;font-size:18px;letter-spacing:-.015em}
.row .t .sm{font-weight:400}
.row .b{margin-top:5px;line-height:1}
.go{font-size:22px;font-weight:700}
.lgrid{display:flex;flex-wrap:wrap;gap:6px}
.letters{display:grid;grid-template-columns:repeat(auto-fill,minmax(38px,1fr));border-top:2px solid var(--ink);border-left:1px solid var(--hair)}
.letters button{height:42px;font-weight:700;font-size:15px;border-right:1px solid var(--hair);border-bottom:1px solid var(--hair)}
.letters button.on{background:var(--ink);color:var(--paper)}
/* settings */
.big{font-size:64px;font-weight:700;letter-spacing:-.05em;line-height:1}
.big small{font-size:24px;letter-spacing:-.02em;margin-left:2px}
dl{display:grid;grid-template-columns:110px 1fr;border-top:1px solid var(--hair)}
dt,dd{padding:11px 0;border-bottom:1px solid var(--hair);font-size:15px}
dt{font-size:11px;font-weight:700;letter-spacing:.09em;text-transform:uppercase;color:var(--mute);padding-top:14px}
dd{overflow-wrap:anywhere}
footer{margin-top:56px;padding-top:12px;padding-bottom:40px;border-top:2px solid var(--ink);font-size:12px;color:var(--mute);display:flex;justify-content:space-between;gap:12px;flex-wrap:wrap}
/* sheet */
.sheet{position:fixed;inset:0;z-index:30;background:var(--paper);overflow-y:auto;-webkit-overflow-scrolling:touch;overscroll-behavior:contain}
.panel{min-height:100%;display:flex;flex-direction:column;animation:in .18s ease-out}
@keyframes in{from{transform:translateY(24px);opacity:0}}
@media(min-width:760px){.sheet{background:rgba(0,0,0,.55)}.panel{min-height:0;max-width:680px;margin:40px auto;background:var(--paper)}}
.bar{position:sticky;top:0;z-index:2;background:var(--paper);display:flex;align-items:center;justify-content:space-between;border-bottom:2px solid var(--ink);padding:0 var(--g);min-height:52px}
.bar .k{font-size:11px;font-weight:700;letter-spacing:.09em;text-transform:uppercase}
.bar button{font-weight:700;padding:14px 0;min-width:44px}
.bar .x{font-size:22px;text-align:right}
.body{flex:1;padding:22px var(--g) 32px}
.foot{position:sticky;bottom:0;background:var(--paper);border-top:2px solid var(--ink);padding:12px var(--g) calc(12px + env(safe-area-inset-bottom))}
.kick{font-size:11px;font-weight:700;letter-spacing:.09em;text-transform:uppercase;color:var(--red)}
.title{font-size:clamp(34px,10vw,54px);font-weight:700;letter-spacing:-.04em;line-height:.95;margin:6px 0 10px;overflow-wrap:anywhere}
.lhead{display:flex;gap:16px;align-items:center;margin-bottom:6px}
.lhead .sub{font-size:17px;font-weight:700;letter-spacing:-.01em;margin-top:4px}
.lgroup{border-top:2px solid var(--ink);margin-bottom:6px}
.lg-h{display:flex;align-items:center;justify-content:space-between;padding:10px 0 4px}
.dir{display:grid;grid-template-columns:30px 1fr auto;align-items:center;gap:10px;padding:11px 0;border-bottom:1px solid var(--hair);cursor:pointer;user-select:none}
.dir:last-child{border-bottom:0}
.box{width:22px;height:22px;border:2px solid var(--ink);display:flex;align-items:center;justify-content:center}
.dir.on .box{background:var(--red);border-color:var(--red)}
.dir.on .box::after{content:"";width:6px;height:11px;border:solid #fff;border-width:0 2.5px 2.5px 0;transform:rotate(45deg) translate(-1px,-1px)}
.dir .to{font-weight:700;font-size:17px;letter-spacing:-.01em}
.dir.off .to{color:var(--mute)}
.route{position:relative;border-top:2px solid var(--ink)}
.route li{position:relative;display:flex;flex-wrap:wrap;align-items:center;gap:4px 8px;padding:12px 0 12px 38px;min-height:48px;border-bottom:1px solid var(--hair);cursor:pointer}
.route li::before{content:"";position:absolute;left:10px;top:0;bottom:0;width:6px;background:var(--lc)}
.route li:first-child::before{top:50%}
.route li:last-child::before{bottom:50%}
.route .dot{position:absolute;left:5px;top:50%;width:16px;height:16px;margin-top:-8px;border-radius:50%;background:var(--paper);border:3.5px solid var(--lc)}
.route li:first-child .dot,.route li:last-child .dot{background:var(--lc)}
.route .sn{font-weight:700;font-size:16px;flex:1 1 auto}
.route .xf{line-height:1}
details summary{cursor:pointer;font-weight:700;padding:14px 0;list-style:none}
details summary::before{content:"+ ";color:var(--red)}
details[open] summary::before{content:"– "}
.tft{background:#000500;color:#fff05d;width:100%;max-width:320px;aspect-ratio:4/3;font:700 15px/1.15 Arial,Helvetica,sans-serif;border:10px solid #1b1b1b;display:flex;flex-direction:column;overflow:hidden}
.tft .th{display:flex;justify-content:space-between;padding:4px 6px;font-size:12px;border-bottom:1px solid #409e8d}
.tft .tr{flex:1;display:grid;grid-template-columns:40px 1fr auto;align-items:center;gap:6px;padding:0 8px;border-bottom:1px solid #1d2a1d}
.tft .tr:last-child{border-bottom:0}
.tft .tt{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.tft .tm{font-size:22px}
.toast{position:fixed;left:50%;bottom:calc(24px + env(safe-area-inset-bottom));transform:translate(-50%,10px);background:var(--ink);color:var(--paper);padding:12px 18px;font-size:14px;font-weight:700;opacity:0;transition:.2s;pointer-events:none;z-index:50;max-width:calc(100vw - 32px);text-align:center}
.toast.show{opacity:1;transform:translate(-50%,0)}
</style></head>
<body>
<div id="banner" class="banner" hidden></div>
<header class="mast wrap">
  <div class="mast-top"><span class="sq"></span>Abfahrtsmonitor · Departure display</div>
  <div class="mast-row">
    <h1>Wien<br>Monitor</h1>
    <svg id="clk" viewBox="-50 -50 100 100" aria-hidden="true"><circle class="ck-face" r="48"/><g id="ticks"></g>
      <line id="hh" class="ck-h" y1="9" y2="-25" stroke-width="7"/><line id="mh" class="ck-h" y1="11" y2="-40" stroke-width="5"/>
      <g id="sh" class="ck-s"><line y1="15" y2="-30" stroke-width="2"/><circle cy="-30" r="6.5"/></g></svg>
  </div>
  <p class="conn" id="conn"><span>Connecting to the display…</span></p>
</header>
<nav class="toc"><div class="wrap"><a href="#s1"><b>01</b>Stops</a><a href="#s2"><b>02</b>Add</a><a href="#s3"><b>03</b>Display</a><a href="#s4"><b>04</b>Device</a></div></nav>
<main>
<section id="s1" class="sec wrap">
  <div class="sec-h"><span class="no">01</span><h2>Your stops</h2><span class="aside" id="count"></span></div>
  <ul id="stops"></ul>
  <p class="hint in">On the display, tap the right or left half to switch stops. Hold a finger on it to come back to this page.</p>
</section>
<section id="s2" class="sec wrap">
  <div class="sec-h"><span class="no">02</span><h2>Add a stop</h2><span class="aside" id="dbinfo"></span></div>
  <div class="in">
    <div class="tabs" role="tablist">
      <button data-a="tab" data-t="search" role="tab">Search</button>
      <button data-a="tab" data-t="lines" role="tab">All lines</button>
      <button data-a="tab" data-t="az" role="tab">A–Z</button>
    </div>
    <div id="tab-search">
      <label class="q"><input id="q" type="search" placeholder="Stephansplatz, U1, 13A…" autocomplete="off" autocorrect="off" spellcheck="false" enterkeyhint="search" aria-label="Search a stop or line"></label>
      <ul id="results"></ul>
      <p class="hint" id="qhint">Type a stop or line. Then pick the directions you want to see.</p>
    </div>
    <div id="tab-lines" hidden></div>
    <div id="tab-az" hidden></div>
    <button class="link" data-a="manual">Know the stop ID (RBL)? Enter it manually →</button>
  </div>
</section>
<section id="s3" class="sec wrap">
  <div class="sec-h"><span class="no">03</span><h2>Display</h2><span class="aside">Saved instantly</span></div>
  <div class="in">
    <div class="lbl" style="margin-top:0">Brightness</div>
    <div class="big" id="brv"></div>
    <input id="br" type="range" min="10" max="100" step="5" aria-label="Brightness">
    <div class="lbl">Dim after</div>
    <div class="seg" id="dimSeg"></div>
    <div class="lbl">Screen off after</div>
    <div class="seg" id="offSeg"></div>
    <p class="hint">While the screen is off, Wi-Fi and the processor sleep too, which saves most power. Tap the display to wake it; the first tap only wakes it. “Never” keeps it on and uses much more power.</p>
  </div>
</section>
<section id="s4" class="sec wrap">
  <div class="sec-h"><span class="no">04</span><h2>Device</h2><span class="aside"></span></div>
  <div class="in">
    <dl id="dev"></dl>
    <div class="acts"><button class="btn" data-a="stops-update">Update stop &amp; line data</button><button class="btn" data-a="wifi">Change Wi-Fi network</button></div>
  </div>
</section>
<footer class="wrap"><span>Data: Stadt Wien – data.wien.gv.at (CC BY 4.0)</span><span>Wiener Linien open data</span></footer>
</main>
<div id="sheet" class="sheet" hidden></div>
<div id="toast" class="toast" role="status"></div>
<script>
const $=s=>document.querySelector(s);
const esc=s=>String(s==null?'':s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const pad=n=>(n<10?'0':'')+n;
const ncmp=(a,b)=>String(a).localeCompare(String(b),undefined,{numeric:true,sensitivity:'base'});
let S={profiles:[],settings:{},device:{},limits:{maxStops:20,maxRows:4},active:-1};
let DB=null,dbState='',dbErr='',tab='search',azKey='';
let E=null;      // stop being edited
const V=[];      // open sheet views, top last

async function api(path,opt){
  let r;
  try{r=await fetch(path,opt)}catch(e){throw new Error('Display not reachable. Tap it to wake it, then try again.')}
  let j={};try{j=await r.json()}catch(e){}
  if(!r.ok)throw new Error(j.error||('HTTP '+r.status));
  return j;
}
async function apiText(path){
  let r;
  try{r=await fetch(path)}catch(e){throw new Error('Display not reachable')}
  if(!r.ok)throw new Error('HTTP '+r.status);
  return r.text();
}
const post=(p,b)=>api(p,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(b||{})});
function toast(t){const e=$('#toast');e.textContent=t;e.classList.add('show');clearTimeout(e._t);e._t=setTimeout(()=>e.classList.remove('show'),2600)}
function banner(t){const b=$('#banner');b.hidden=!t;b.textContent=t||''}
const spinner=t=>'<p class="hint"><span class="spin"></span>'+t+'</p>';

// ---- lines & badges ----
const GROUPS=[['ptMetro','U-Bahn'],['ptTram','Tram'],['ptTramWLB','Badner Bahn'],['ptBusCity','Bus'],['ptBusNight','Night bus'],['ptRufBus','On-demand bus'],['','Other']];
const COL={'c-u1':'#e3000f','c-u2':'#a862a4','c-u3':'#ee7d00','c-u4':'#319f49','c-u5':'#2aa3a3','c-u6':'#9d6830','c-tram':'#c4161c','c-wlb':'#0d4c95','c-bus':'#1f4e8c','c-night':'#141b3c','c-ruf':'#5c6970','c-s':'#0089cf'};
const lineOf=n=>DB&&DB.ln.get(String(n).toUpperCase());
function lineType(n,t){if(t)return t;const L=lineOf(n);return L?L.t:''}
function cls(n,t){
  n=String(n);t=lineType(n,t);
  const m=/^U(\d)/.exec(n);if(m)return 'c-u'+m[1];
  if(t==='ptBusNight'||/^N\d/.test(n))return 'c-night';
  if(t==='ptTramWLB'||n==='WLB')return 'c-wlb';
  if(t==='ptTrainS'||/^S\d/.test(n))return 'c-s';
  if(t==='ptRufBus')return 'c-ruf';
  if(t.indexOf('Bus')>=0||/^\d+[AB]$/.test(n))return 'c-bus';
  return 'c-tram';
}
const badge=(n,t,sz)=>'<span class="bd '+cls(n,t)+(sz?' '+sz:'')+'">'+esc(n)+'</span>';
function groupIdx(n,t){t=lineType(n,t);const i=GROUPS.findIndex(g=>g[0]===t);return i<0?GROUPS.length-1:i}
const lineCmp=(a,b)=>groupIdx(a)-groupIdx(b)||ncmp(a,b);
function badgeList(names,max){
  names=[...names].sort(lineCmp);max=max||10;
  return names.slice(0,max).map(n=>badge(n)).join('')+(names.length>max?'<span class="more">+'+(names.length-max)+'</span>':'');
}
function mins(c){
  if(!c||!c.length)return '<span class="sm">—</span>';
  return '<b>'+(c[0]===0?'now':c[0]+'′')+'</b>'+(c.length>1?'<small>'+c.slice(1).map(x=>x+'′').join(' ')+'</small>':'');
}

// ---- offline data (stop list + line routes from the display) ----
function norm(s){
  return ' '+String(s||'').toLowerCase().replace(/ä/g,'a').replace(/ö/g,'o').replace(/ü/g,'u').replace(/ß/g,'ss')
    .normalize('NFD').replace(/[̀-ͯ]/g,'').replace(/[^a-z0-9]+/g,' ').trim()+' ';
}
function parseDB(stopsTxt,linesTxt){
  const st=new Map(),rb=new Map(),pl=new Map(),lines=[],ln=new Map();
  const ids=s=>s?s.split(','):[];
  const addPl=(r,e)=>{let a=pl.get(r);if(!a)pl.set(r,a=[]);a.push(e);const s=rb.get(r);if(s)s.L.add(e.l.n)};
  stopsTxt.split('\n').forEach(row=>{
    if(!row||row[0]==='#')return;
    const f=row.split(';');if(f.length<3)return;
    let s=st.get(f[0]);
    if(!s){s={d:f[0],n:f[2],m:f[3]||'',r:[],L:new Set()};s.h=norm(s.n+' '+s.m);st.set(f[0],s)}
    s.r.push(f[1]);rb.set(f[1],s);
  });
  (linesTxt||'').split('\n').forEach(row=>{
    if(!row||row[0]==='#')return;
    const f=row.split(';');if(f.length<5)return;
    const L={n:f[0],t:f[1],d:[ids(f[2]),ids(f[3])],o:ids(f[4])};
    const dup=ln.get(L.n.toUpperCase());
    if(dup){  // same line name under two IDs: fold the stops into the first
      const seen=new Set(dup.d[0].concat(dup.d[1],dup.o)),add=L.d[0].concat(L.d[1],L.o).filter(r=>!seen.has(r)&&seen.add(r));
      dup.o=dup.o.concat(add);add.forEach(r=>addPl(r,{l:dup,k:-1}));return;
    }
    lines.push(L);ln.set(L.n.toUpperCase(),L);
    L.d.forEach((a,k)=>a.forEach((r,i)=>addPl(r,{l:L,k:k,end:i===a.length-1})));
    L.o.forEach(r=>addPl(r,{l:L,k:-1}));
  });
  lines.sort((a,b)=>groupIdx(a.n,a.t)-groupIdx(b.n,b.t)||ncmp(a.n,b.n));
  return {st,rb,pl,lines,ln};
}
function terminal(L,k){const a=L.d[k];for(let i=a.length-1;i>=0;i--){const s=DB.rb.get(a[i]);if(s)return s.n}return ''}
function span(L){
  const a=L.d[0].length?0:1,s=DB.rb.get(L.d[a][0]);
  return (s?s.n:'')+' ↔ '+terminal(L,a);
}
function routeStations(ids){
  const out=[];
  ids.forEach(r=>{const s=DB.rb.get(r);if(s&&(!out.length||out[out.length-1]!==s))out.push(s)});
  return out;
}
async function ensureDB(force){
  const d=S.device||{};
  if(!d.stops){dbState='missing';renderAdd();return}
  const key=[d.stopsDate,d.stops,d.linesDate,d.lines].join(':');
  if(!force&&DB&&DB.key===key)return;
  if(dbState==='loading')return;
  let c=null;try{c=JSON.parse(localStorage.getItem('wm.db')||'null')}catch(e){}
  if(!force&&c&&c.k===key){DB=parseDB(c.s,c.l);DB.key=key;dbState='ready';renderAdd();render();return}
  dbState='loading';renderAdd();
  try{
    const s=await apiText('/api/data/stops');
    const l=d.lines?await apiText('/api/data/lines'):'';
    DB=parseDB(s,l);DB.key=key;dbState='ready';
    try{localStorage.setItem('wm.db',JSON.stringify({k:key,s:s,l:l}))}catch(e){}
  }catch(e){dbState='error';dbErr=e.message}
  renderAdd();render();
}
function dbNotice(needLines){
  if(dbState==='loading'||!dbState)return spinner('Loading stop and line data from the display…');
  if(dbState==='error')return '<p class="hint">Could not load the stop data ('+esc(dbErr)+').</p><button class="btn sm" data-a="db-retry">Try again</button>';
  if(dbState==='missing'||(needLines&&!DB.lines.length))return '<p class="hint">The display has not downloaded the '+(dbState==='missing'?'stop list':'line data')+' from Wiener Linien yet. It does this by itself shortly after starting, or now:</p><button class="btn sm" data-a="stops-update">Download now</button>';
  return '';
}

// ---- state ----
async function load(){
  try{S=await api('/api/state');banner('');render();ensureDB()}
  catch(e){banner(e.message)}
}
function render(){
  const d=S.device||{};
  const sig=d.rssi>-60?'good':d.rssi>-72?'fair':'weak';
  $('#conn').innerHTML=d.ip?'<span>Wi-Fi <b>'+esc(d.ssid)+'</b> · '+sig+'</span><span><b>'+esc(d.host)+'</b></span><span>'+esc(d.ip)+'</span>':'<span>Connecting to the display…</span>';
  $('#count').textContent=S.profiles.length+' / '+S.limits.maxStops;
  $('#stops').innerHTML=S.profiles.length?S.profiles.map(stopRow).join('')
    :'<li class="empty in">No stops yet. Add your first one below — search it, or browse all lines.</li>';
  renderSettings();renderDevice();
}
function stopRow(p,i){
  const on=i===S.active;
  const names=(p.badges||p.filter||'').split(',').map(s=>s.trim()).filter(Boolean);
  let h='<li class="stop'+(on?' on':'')+'"><div class="n">'+pad(i+1)+'</div><div class="b" data-a="edit" data-i="'+i+'">'+
    '<div class="t">'+esc(p.name||('Stop '+p.rbl))+'</div><div class="meta">'+(names.length?badgeList(names,8):'<span class="sm">All lines</span>')+
    ' <span class="sm">· '+p.rows+' row'+(p.rows>1?'s':'')+'</span></div>';
  if(p.live&&p.live.length){
    h+='<ul class="dep">'+p.live.slice(0,on?4:2).map(r=>'<li>'+badge(r.l)+'<span class="to">'+esc(r.t)+'</span><span class="mins">'+mins(r.c)+'</span></li>').join('')+'</ul>';
    if(on){const al=[...new Set(p.live.map(r=>r.d).filter(Boolean))];h+=al.slice(0,2).map(t=>'<p class="alert"><span>'+esc(t)+'</span></p>').join('')}
  }
  h+='</div><div>'+(on?'<div class="tag">On display</div>':'<button class="btn sm" data-a="show" data-i="'+i+'">Show</button>')+'</div></li>';
  return h;
}
const DIM=[[0,'Never'],[10,'10 s'],[20,'20 s'],[30,'30 s'],[60,'1 min']];
const OFF=[[30,'30 s'],[60,'1 min'],[120,'2 min'],[300,'5 min'],[600,'10 min'],[0,'Never']];
function segHtml(opts,cur,k){
  if(cur!=null&&!opts.some(o=>o[0]===cur))opts=opts.concat([[cur,cur+' s']]);
  return opts.map(o=>'<button data-a="set" data-k="'+k+'" data-v="'+o[0]+'" class="'+(o[0]===cur?'on':'')+'">'+o[1]+'</button>').join('');
}
function renderSettings(){
  const st=S.settings||{};
  if(document.activeElement!==$('#br')&&st.brightness!=null){$('#br').value=st.brightness;$('#brv').innerHTML=st.brightness+'<small>%</small>'}
  $('#dimSeg').innerHTML=segHtml(DIM,st.dim,'dim');
  $('#offSeg').innerHTML=segHtml(OFF,st.off,'off');
}
function renderDevice(){
  const d=S.device||{};
  const up=d.uptime||0,h=Math.floor(up/3600),m=Math.floor(up%3600/60);
  const date=t=>t>1600000000?new Date(t*1000).toLocaleDateString():'';
  const stations=DB&&dbState==='ready'?DB.st.size.toLocaleString()+' stations, ':'';
  $('#dev').innerHTML=
    '<dt>Network</dt><dd>'+esc(d.ssid||'–')+'<br><span class="sm">Signal '+(d.rssi||'–')+' dBm</span></dd>'+
    '<dt>Address</dt><dd>http://'+esc(d.host)+'<br><span class="sm">http://'+esc(d.ip)+'</span></dd>'+
    '<dt>Stops</dt><dd>'+(d.stops?stations+d.stops.toLocaleString()+' platforms<br><span class="sm">Updated '+date(d.stopsDate)+'</span>':'Not downloaded yet')+'</dd>'+
    '<dt>Lines</dt><dd>'+(d.lines?d.lines+' lines with routes<br><span class="sm">Updated '+date(d.linesDate)+'</span>':'Not downloaded yet')+'</dd>'+
    '<dt>Running</dt><dd>'+h+' h '+m+' min<br><span class="sm">'+Math.round((d.heap||0)/1024)+' kB memory free</span></dd>';
}

// ---- add: search / lines / A–Z ----
function renderAdd(){
  document.querySelectorAll('.tabs button').forEach(b=>{b.classList.toggle('on',b.dataset.t===tab);b.setAttribute('aria-selected',b.dataset.t===tab)});
  ['search','lines','az'].forEach(t=>$('#tab-'+t).hidden=t!==tab);
  $('#dbinfo').textContent=dbState==='ready'?DB.st.size.toLocaleString()+' stations · '+DB.lines.length+' lines':dbState==='loading'?'Loading data…':'';
  if(tab==='lines')renderLines();
  else if(tab==='az')renderAZ();
  else if($('#q').value.trim())search();
}
function stationRow(s,line){
  return '<li class="row" data-a="pick" data-d="'+esc(s.d)+'" data-n="'+esc(s.n)+'"'+(line?' data-l="'+esc(line)+'"':'')+'><div><div class="t">'+esc(s.n)+(s.m?' <span class="sm">'+esc(s.m)+'</span>':'')+'</div>'+
    (s.L&&s.L.size?'<div class="b">'+badgeList(s.L,12)+'</div>':'')+'</div><span class="go">→</span></li>';
}
function lineRow(L){
  return '<li class="row" data-a="line" data-l="'+esc(L.n)+'"><div><div class="t">'+badge(L.n,L.t,'md')+' &nbsp;Line '+esc(L.n)+'</div><div class="sm" style="margin-top:4px">'+esc(span(L))+'</div></div><span class="go">→</span></li>';
}
let qTimer=0,qSeq=0;
$('#q').addEventListener('input',()=>{clearTimeout(qTimer);qTimer=setTimeout(search,DB?60:250)});
$('#q').addEventListener('keydown',e=>{if(e.key==='Enter'){clearTimeout(qTimer);search();e.target.blur()}});
async function search(){
  const q=$('#q').value.trim(),seq=++qSeq,ul=$('#results'),hint=$('#qhint');
  if(!q){ul.innerHTML='';hint.textContent='Type a stop or line. Then pick the directions you want to see.';return}
  if(DB&&dbState==='ready'){
    const nq=norm(q),toks=nq.trim().split(' ').filter(Boolean),pre=nq.trimEnd(),hits=[];
    if(toks.length)for(const s of DB.st.values()){
      if(!toks.every(t=>s.h.indexOf(t)>=0))continue;
      hits.push([s.h.startsWith(pre)?0:s.h.indexOf(' '+toks[0])>=0?1:2,s]);
    }
    hits.sort((a,b)=>a[0]-b[0]||a[1].n.length-b[1].n.length||ncmp(a[1].n,b[1].n));
    const L=lineOf(q.replace(/\s+/g,''));
    ul.innerHTML=(L?lineRow(L):'')+hits.slice(0,40).map(h=>stationRow(h[1])).join('');
    hint.textContent=hits.length||L?(hits.length>40?'Showing 40 of '+hits.length+' stations. Type more to narrow it down.':''):'Nothing found. Try a shorter or different name.';
    return;
  }
  if(q.length<2)return;
  hint.innerHTML='<span class="spin"></span>Searching…';
  const slow=setTimeout(()=>{if(seq===qSeq)hint.innerHTML='<span class="spin"></span>Downloading the Vienna stop list (only needed once)…'},1500);
  try{
    const r=await api('/api/search?q='+encodeURIComponent(q));
    if(seq!==qSeq)return;
    ul.innerHTML=r.results.map(x=>stationRow({d:x.diva,n:x.name,m:x.muni})).join('');
    hint.textContent=r.results.length?'':'Nothing found. Try a shorter or different name.';
  }catch(e){if(seq===qSeq){ul.innerHTML='';hint.textContent=e.message}}
  finally{clearTimeout(slow)}
}
function renderLines(){
  const el=$('#tab-lines'),n=dbNotice(true);
  if(n){el.innerHTML=n;return}
  el.innerHTML='<p class="hint" style="margin-top:0">Every line with its full route. Pick a line, then a stop on it.</p>'+GROUPS.map((g,gi)=>{
    const ls=DB.lines.filter(L=>groupIdx(L.n,L.t)===gi);
    return ls.length?'<div class="lbl">'+g[1]+'<span>'+ls.length+'</span></div><div class="lgrid">'+ls.map(L=>'<button class="bd lg '+cls(L.n,L.t)+'" data-a="line" data-l="'+esc(L.n)+'" title="'+esc(span(L))+'">'+esc(L.n)+'</button>').join('')+'</div>':'';
  }).join('');
}
function renderAZ(){
  const el=$('#tab-az'),n=dbNotice(false);
  if(n){el.innerHTML=n;return}
  if(!DB.az){
    DB.az=new Map();
    for(const s of DB.st.values()){let k=norm(s.n).trim().charAt(0).toUpperCase();if(!/[A-Z]/.test(k))k='#';if(!DB.az.has(k))DB.az.set(k,[]);DB.az.get(k).push(s)}
    DB.az.forEach(a=>a.sort((x,y)=>ncmp(x.n,y.n)));
  }
  const keys=[...DB.az.keys()].sort();
  if(!DB.az.has(azKey))azKey=keys[0];
  const list=DB.az.get(azKey);
  el.innerHTML='<div class="letters">'+keys.map(k=>'<button data-a="az" data-k="'+k+'" class="'+(k===azKey?'on':'')+'">'+k+'</button>').join('')+'</div>'+
    '<div class="lbl">'+azKey+'<span>'+list.length+' stations</span></div><ul>'+list.map(s=>stationRow(s)).join('')+'</ul>';
}

// ---- sheet (line view + stop editor) ----
function openView(v){V.push(v);drawTop()}
function drawTop(){
  const v=V[V.length-1],sh=$('#sheet');
  if(!v){sh.hidden=true;sh.innerHTML='';E=null;document.body.style.overflow='';return}
  E=v.kind==='edit'?v.E:null;
  sh.innerHTML='<div class="panel">'+(v.kind==='line'?lineHtml(v):editorHtml())+'</div>';
  sh.hidden=false;document.body.style.overflow='hidden';
}
function barHtml(k){
  return '<div class="bar"><button data-a="back">'+(V.length>1?'← Back':'')+'</button><span class="k">'+k+'</span><button class="x" data-a="close" aria-label="Close">✕</button></div>';
}
function closeSheets(){V.length=0;drawTop()}
function openLine(name){
  const L=lineOf(name);if(!L)return;
  openView({kind:'line',L:L,k:L.d[0].length?0:1});
  $('#sheet').scrollTop=0;
}
function lineHtml(v){
  const L=v.L,k=v.k,col=COL[cls(L.n,L.t)],g=GROUPS[groupIdx(L.n,L.t)][1];
  const stops=routeStations(L.d[k]);
  let h=barHtml('Line')+'<div class="body"><div class="lhead">'+badge(L.n,L.t,'xl')+'<div><div class="kick">'+g+'</div><div class="sub">'+esc(span(L))+'</div></div></div>';
  if(L.d[0].length&&L.d[1].length)
    h+='<div class="lbl">Direction</div><div class="seg">'+[0,1].map(i=>'<button data-a="ldir" data-k="'+i+'" class="'+(i===k?'on':'')+'">→ '+esc(terminal(L,i))+'</button>').join('')+'</div>';
  h+='<div class="lbl">'+stops.length+' stops<span>Tap a stop to add it</span></div><ol class="route" style="--lc:'+col+'">'+
    stops.map(s=>{const x=[...s.L].filter(n=>n!==L.n);
      return '<li data-a="pick" data-d="'+esc(s.d)+'" data-n="'+esc(s.n)+'" data-l="'+esc(L.n)+'"><span class="dot"></span><span class="sn">'+esc(s.n)+'</span><span class="xf">'+badgeList(x,6)+'</span></li>'}).join('')+'</ol>';
  const extra=routeStations(L.o).filter(s=>stops.indexOf(s)<0);
  if(extra.length)h+='<details><summary>Also served by some trips ('+extra.length+')</summary><ul>'+extra.map(s=>stationRow(s,L.n)).join('')+'</ul></details>';
  return h+'</div>';
}
function openEditor(o){
  const p=Object.assign({name:'',rbl:'',filter:'',rows:2,diva:'',badges:''},o.profile||{});
  const ed={mode:o.mode,index:o.index,p:p,dirs:[],sel:new Set(),loading:!!p.diva,err:'',alerts:[],live:true,rowsTouched:o.mode==='edit',preLine:o.line||''};
  openView({kind:'edit',E:ed});$('#sheet').scrollTop=0;
  if(p.diva)loadStation(ed);
}
function stationOf(diva){return DB&&DB.st.get(String(diva))}
function buildDirs(r,diva){
  const dirs=[],seen=new Set(),rbls=[];
  const addR=x=>{x=String(x);if(rbls.indexOf(x)<0)rbls.push(x)};
  if(r)r.platforms.forEach(pl=>{addR(pl.rbl);pl.lines.forEach(l=>{seen.add(pl.rbl+'|'+l.name);dirs.push({rbl:String(pl.rbl),name:l.name,towards:l.towards,type:l.type,next:l.next||[],live:true})})});
  const s=stationOf(diva);if(s)s.r.forEach(addR);
  rbls.forEach(rbl=>{
    ((DB&&DB.pl.get(rbl))||[]).forEach(e=>{
      const key=rbl+'|'+e.l.n;
      if(e.end||seen.has(key))return;
      seen.add(key);
      dirs.push({rbl:rbl,name:e.l.n,towards:e.k>=0?terminal(e.l,e.k):'',type:e.l.t,next:[],live:false});
    });
    if(!dirs.some(d=>d.rbl===rbl))dirs.push({rbl:rbl,name:'',towards:'',next:[],live:false});
  });
  dirs.sort((a,b)=>(a.name?0:1)-(b.name?0:1)||lineCmp(a.name,b.name)||(b.live-a.live)||ncmp(a.towards,b.towards));
  return dirs;
}
async function loadStation(ed){
  let r=null;
  try{r=await api('/api/station?diva='+encodeURIComponent(ed.p.diva))}catch(e){ed.err=e.message}
  if(E===ed)syncInputs();  // keep what was typed meanwhile
  ed.dirs=buildDirs(r,ed.p.diva);ed.alerts=(r&&r.alerts)||[];ed.live=!!(r&&r.live);
  if(ed.mode==='edit'){
    const ids=ed.p.rbl.split(','),f=ed.p.filter.split(',').map(s=>s.trim()).filter(Boolean);
    ed.dirs.forEach((d,i)=>{if(ids.indexOf(d.rbl)>=0&&(!f.length||!d.name||f.indexOf(d.name)>=0))ed.sel.add(i)});
  }else if(ed.preLine){
    ed.dirs.forEach((d,i)=>{if(d.name===ed.preLine)ed.sel.add(i)});
    if(ed.sel.size)applySelection(ed);
  }
  ed.loading=false;
  if(E===ed)redrawEditor();
}
function syncInputs(){
  if(!E)return;
  const v=id=>{const el=$('#'+id);return el?el.value:null};
  if(v('e_name')!==null)E.p.name=v('e_name');
  if(v('e_rbl')!==null)E.p.rbl=v('e_rbl');
  if(v('e_filter')!==null)E.p.filter=v('e_filter');
}
function redrawEditor(){const sh=$('#sheet'),y=sh.scrollTop,adv=document.querySelector('details.adv');const open=adv&&adv.open;drawTop();sh.scrollTop=y;const a=document.querySelector('details.adv');if(a&&adv)a.open=open}
function applySelection(ed){
  ed=ed||E;
  const chosen=ed.dirs.filter((d,i)=>ed.sel.has(i));
  const ids=[...new Set(chosen.map(d=>d.rbl))];
  const lines=[...new Set(chosen.map(d=>d.name).filter(Boolean))];
  const all=[...new Set(ed.dirs.filter(d=>ids.indexOf(d.rbl)>=0).map(d=>d.name).filter(Boolean))];
  ed.p.rbl=ids.join(',');
  ed.p.filter=(!lines.length||lines.length===all.length)?'':lines.join(',');
  ed.p.badges=lines.join(',');
  if(!ed.rowsTouched)ed.p.rows=Math.min(S.limits.maxRows,Math.max(2,chosen.length));
}
function stationTitle(){const s=stationOf(E.p.diva);return s?s.n:(E.p.name||'New stop')}
function hhmm(){const d=new Date();return pad(d.getHours())+':'+pad(d.getMinutes())}
function previewHtml(){
  const chosen=E.dirs.filter((d,i)=>E.sel.has(i)&&d.name),rows=[];
  for(let i=0;i<E.p.rows;i++){
    const d=chosen[i];
    rows.push('<div class="tr">'+(d?'<span>'+esc(d.name)+'</span><span class="tt">'+esc(d.towards)+'</span><span class="tm">'+(d.next.length?d.next[0]:'')+'</span>':'')+'</div>');
  }
  return '<div class="tft"><div class="th"><span id="tftName">'+esc(E.p.name||stationTitle())+'</span><span>'+hhmm()+'</span></div>'+rows.join('')+'</div>';
}
function editorHtml(){
  const p=E.p,s=stationOf(p.diva);
  let h=barHtml(E.mode==='add'?'Add stop':'Edit stop')+'<div class="body">';
  h+='<div class="kick">'+(p.diva?'Station':'Stop by ID')+'</div><div class="title">'+esc(stationTitle())+'</div>';
  if(s&&s.L.size)h+='<div>'+badgeList(s.L,24)+'</div>';
  if(E.alerts.length){
    const al=E.alerts.map(a=>'<div class="alert"><b>'+(a.lines||[]).map(n=>badge(n)).join('')+esc(a.title)+'</b>'+esc(a.text)+'</div>').join('');
    h+=E.alerts.length>2?'<details><summary>'+E.alerts.length+' service notices</summary>'+al+'</details>':al;
  }
  if(p.diva){
    h+='<div class="lbl">Directions<span>'+(E.sel.size?E.sel.size+' selected':'Pick one or more')+'</span></div>';
    if(E.loading)h+=spinner('Loading live departures…');
    else if(!E.dirs.length)h+='<p class="hint">'+(E.err?'Could not load the directions ('+esc(E.err)+'). You can still enter stop IDs under Advanced.':'No platforms found for this stop.')+'</p>';
    else{
      if(!E.live)h+='<p class="hint">Live departures are unavailable right now; showing directions from the timetable.</p>';
      let last=null;
      E.dirs.forEach((d,i)=>{
        if(d.name!==last){
          if(last!==null)h+='</div>';
          h+='<div class="lgroup"><div class="lg-h">'+(d.name?badge(d.name,d.type,'md')+'<button class="link sm" data-a="selline" data-l="'+esc(d.name)+'">Select all</button>':'<span class="sm">Other platforms</span>')+'</div>';
          last=d.name;
        }
        const on=E.sel.has(i);
        h+='<div class="dir'+(on?' on':'')+(d.live?'':' off')+'" data-a="dir" data-i="'+i+'" role="checkbox" aria-checked="'+on+'"><span class="box"></span><div><div class="to">'+
          (d.name?(d.towards?'→ '+esc(d.towards):'Line '+esc(d.name)):'Platform '+esc(d.rbl))+'</div><div class="sm">'+(d.live?'':'No departures right now · ')+'ID '+esc(d.rbl)+'</div></div><div class="mins">'+mins(d.next)+'</div></div>';
      });
      h+='</div>';
    }
  }
  h+='<label class="lbl" for="e_name">Name on the display</label><input class="txt" id="e_name" maxlength="32" value="'+esc(p.name)+'" placeholder="e.g. Home, Work, '+esc(stationTitle())+'">';
  h+='<div class="lbl">Rows on the display</div><div class="seg">';
  for(let n=1;n<=S.limits.maxRows;n++)h+='<button data-a="rows" data-n="'+n+'" class="'+(p.rows==n?'on':'')+'">'+n+'</button>';
  h+='</div>';
  if(p.diva)h+='<div class="lbl">Preview<span>Approximate</span></div>'+previewHtml();
  h+='<details class="adv"'+(p.diva?'':' open')+'><summary>Advanced</summary>'+
    '<label class="lbl" for="e_rbl" style="margin-top:4px">Stop IDs (RBL), comma separated</label><input class="txt" id="e_rbl" inputmode="numeric" value="'+esc(p.rbl)+'" placeholder="e.g. 4111,4118">'+
    '<label class="lbl" for="e_filter">Only these lines (optional)</label><input class="txt" id="e_filter" value="'+esc(p.filter)+'" placeholder="e.g. U1,13A">'+
    '<p class="hint">'+(p.diva?'Picking directions above fills these in for you.':'Find stop IDs at till.mabe.at/rbl')+'</p></details>';
  if(E.mode==='edit')h+='<div class="acts"><button class="btn sm" data-a="up">↑ Move up</button><button class="btn sm" data-a="down">↓ Move down</button><button class="btn sm danger" data-a="del">Delete</button></div>';
  h+='</div><div class="foot"><button class="btn pri" data-a="save">'+(E.mode==='add'?'Add stop':'Save changes')+'</button></div>';
  return h;
}
async function saveProfiles(list,active,msg){
  try{S=await post('/api/profiles',{profiles:list,active:active});render();toast(msg||'Saved');return true}
  catch(e){toast(e.message);return false}
}
async function saveEditor(btn){
  syncInputs();const p=E.p;
  p.rbl=(p.rbl||'').replace(/[^0-9]+/g,',').replace(/^,|,$/g,'');
  if(!p.rbl){toast(p.diva?'Pick at least one direction':'Enter a stop ID');return}
  if(!p.name){const s=stationOf(p.diva);if(s)p.name=s.n}
  if(!p.badges&&p.filter)p.badges=p.filter;
  const list=S.profiles.slice();let active;
  if(E.mode==='add'){
    if(list.length>=S.limits.maxStops){toast('You can save up to '+S.limits.maxStops+' stops');return}
    list.push(p);active=list.length-1;
  }else{list[E.index]=p;active=E.index}
  btn.disabled=true;
  if(await saveProfiles(list,active,E.mode==='add'?'Added – now on the display':'Saved – now on the display')){
    closeSheets();$('#q').value='';$('#results').innerHTML='';
    location.hash='';document.getElementById('s1').scrollIntoView();
  }else btn.disabled=false;
}
async function moveOrDelete(action){
  const list=S.profiles.slice(),i=E.index,cur=S.profiles[S.active];
  if(action==='del'){if(!confirm('Delete "'+(list[i].name||'this stop')+'"?'))return;list.splice(i,1)}
  else{const j=action==='up'?i-1:i+1;if(j<0||j>=list.length)return;[list[i],list[j]]=[list[j],list[i]]}
  let active=list.indexOf(cur);if(active<0)active=0;
  if(await saveProfiles(list,active,action==='del'?'Deleted':'Moved'))closeSheets();
}

// ---- clicks ----
document.addEventListener('click',async e=>{
  const t=e.target.closest('[data-a]');if(!t)return;
  const a=t.dataset.a,i=+t.dataset.i;
  if(a==='pick')openEditor({mode:'add',line:t.dataset.l,profile:{name:t.dataset.n||'',diva:t.dataset.d}});
  else if(a==='manual')openEditor({mode:'add',profile:{}});
  else if(a==='edit')openEditor({mode:'edit',index:i,profile:S.profiles[i]});
  else if(a==='line')openLine(t.dataset.l);
  else if(a==='ldir'){V[V.length-1].k=+t.dataset.k;drawTop()}
  else if(a==='tab'){tab=t.dataset.t;renderAdd()}
  else if(a==='az'){azKey=t.dataset.k;renderAZ()}
  else if(a==='show'){try{S=await post('/api/show',{index:i});render();toast('Now on the display')}catch(err){toast(err.message)}}
  else if(a==='close')closeSheets();
  else if(a==='back'){if(V.length>1){V.pop();drawTop()}}
  else if(a==='dir'){syncInputs();E.sel.has(i)?E.sel.delete(i):E.sel.add(i);applySelection();redrawEditor()}
  else if(a==='selline'){syncInputs();const idx=E.dirs.map((d,j)=>d.name===t.dataset.l?j:-1).filter(j=>j>=0),all=idx.every(j=>E.sel.has(j));idx.forEach(j=>all?E.sel.delete(j):E.sel.add(j));applySelection();redrawEditor()}
  else if(a==='rows'){syncInputs();E.p.rows=+t.dataset.n;E.rowsTouched=true;redrawEditor()}
  else if(a==='save')saveEditor(t);
  else if(a==='up'||a==='down'||a==='del')moveOrDelete(a);
  else if(a==='set')saveSettings({[t.dataset.k]:+t.dataset.v});
  else if(a==='db-retry')ensureDB(true);
  else if(a==='stops-update'){
    t.disabled=true;const old=t.innerHTML;t.innerHTML='<span class="spin"></span>Downloading…';
    toast('Downloading from Wiener Linien – this can take a minute');
    try{S=await post('/api/stops/update');render();await ensureDB(true);toast('Stop and line data updated')}catch(err){toast(err.message)}
    t.disabled=false;t.innerHTML=old;
  }
  else if(a==='wifi'){
    if(!confirm('The display will open the Wi-Fi hotspot "Wien Transport". Connect your phone to it to choose a new network. Continue?'))return;
    try{await post('/api/wifi');banner('Now connect to the Wi-Fi "Wien Transport" and follow the steps (or open 192.168.4.1).')}catch(err){toast(err.message)}
  }
});
$('#sheet').addEventListener('click',e=>{if(e.target.id==='sheet')closeSheets()});
document.addEventListener('keydown',e=>{if(e.key==='Escape'&&V.length)closeSheets()});
document.addEventListener('input',e=>{if(e.target.id==='e_name'&&E){const n=$('#tftName');if(n)n.textContent=e.target.value||stationTitle()}});

// ---- settings ----
$('#br').addEventListener('input',e=>{$('#brv').innerHTML=e.target.value+'<small>%</small>'});
$('#br').addEventListener('change',e=>saveSettings({brightness:+e.target.value}));
async function saveSettings(change){
  const st=S.settings||{};
  try{S=await post('/api/settings',Object.assign({brightness:st.brightness,dim:st.dim,off:st.off},change));render();toast('Saved')}
  catch(e){toast(e.message)}
}

// ---- Swiss railway clock ----
(function(){
  let t='';
  for(let i=0;i<60;i++){const h=i%5===0;t+='<rect class="ck-t" x="'+(h?-1.8:-.8)+'" y="-46" width="'+(h?3.6:1.6)+'" height="'+(h?12:4)+'" transform="rotate('+i*6+')"/>'}
  $('#ticks').innerHTML=t;
  const set=(id,deg)=>$('#'+id).setAttribute('transform','rotate('+deg+')');
  function tick(){
    const d=new Date(),s=d.getSeconds(),m=d.getMinutes(),h=d.getHours()%12;
    set('sh',Math.min(s*6.15,360));set('mh',m*6);set('hh',h*30+m/2);
  }
  tick();setInterval(tick,1000);
})();

// Live departures refresh; also keeps the display (and its Wi-Fi) awake
// while this page is open.
setInterval(()=>{if(!document.hidden)api('/api/state').then(s=>{S=s;render();ensureDB()}).catch(()=>{})},20000);
document.addEventListener('visibilitychange',()=>{if(!document.hidden)load()});
renderAdd();
load();
</script>
</body></html>)HTML";
