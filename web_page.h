// Auto-embedded from web/index.html — do not edit by hand.
// Regenerate with:  python3 web/embed.py
#pragma once
static const char INDEX_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="theme-color" content="#0e0e14">
<title>GLOBO</title>
<style>
:root{--bgc:rgb(35,80,255);--inkrgb:12,12,16;--ink:rgb(12,12,16);--ink2:rgb(246,244,238)}
*{margin:0;padding:0;box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html{background:var(--bgc,#2350ff)}
body{
  font-family:-apple-system,BlinkMacSystemFont,"SF Pro Display",system-ui,sans-serif;
  background:var(--bgc);color:var(--ink);min-height:100vh;overflow-x:hidden;
  -webkit-font-smoothing:antialiased;text-rendering:optimizeLegibility;
  padding:calc(env(safe-area-inset-top) + 20px) calc(env(safe-area-inset-right) + 20px) calc(env(safe-area-inset-bottom) + 28px) calc(env(safe-area-inset-left) + 20px);
}
button{font:inherit;color:inherit;background:none;border:none;cursor:pointer;-webkit-user-select:none;user-select:none}
button:active{transform:scale(.97)}
/* ---------- background blobs ---------- */
.bg{display:none}
.blob{position:absolute;border-radius:50%;filter:blur(90px);will-change:transform;opacity:.5}
.b1{width:75vmax;height:75vmax;left:-30vmax;top:-25vmax;background:radial-gradient(circle at 35% 35%,rgba(255,160,60,.55),rgba(255,160,60,0) 68%);animation:drA 52s ease-in-out infinite alternate}
.b2{width:65vmax;height:65vmax;right:-28vmax;top:-8vmax;background:radial-gradient(circle at 40% 45%,rgba(240,80,180,.42),rgba(240,80,180,0) 68%);animation:drB 64s ease-in-out infinite alternate}
.b3{width:70vmax;height:70vmax;left:-22vmax;bottom:-30vmax;background:radial-gradient(circle at 50% 40%,rgba(70,140,255,.4),rgba(70,140,255,0) 68%);animation:drC 58s ease-in-out infinite alternate}
.b4{width:55vmax;height:55vmax;right:-18vmax;bottom:-12vmax;background:radial-gradient(circle at 45% 50%,rgba(30,210,190,.38),rgba(30,210,190,0) 68%);animation:drA 71s ease-in-out infinite alternate-reverse}
.b5{width:48vmax;height:48vmax;left:22vw;top:32vh;background:radial-gradient(circle at 50% 50%,rgba(180,120,220,.3),rgba(180,120,220,0) 68%);animation:drB 47s ease-in-out infinite alternate-reverse}
@keyframes drA{from{transform:translate(0,0) scale(1)}to{transform:translate(9vmax,7vmax) scale(1.14)}}
@keyframes drB{from{transform:translate(0,0) scale(1.08)}to{transform:translate(-8vmax,9vmax) scale(.94)}}
@keyframes drC{from{transform:translate(0,0) scale(.96)}to{transform:translate(10vmax,-7vmax) scale(1.1)}}
.grain{position:fixed;inset:-50%;width:200%;height:200%;z-index:50;pointer-events:none;opacity:.08;
  background-image:url("data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='140' height='140'><filter id='n'><feTurbulence type='fractalNoise' baseFrequency='0.85' numOctaves='2' stitchTiles='stitch'/></filter><rect width='140' height='140' filter='url(%23n)'/></svg>");
  background-size:140px 140px}
/* ---------- layout ---------- */
main{position:relative;z-index:1;max-width:620px;margin:0 auto;display:flex;flex-direction:column;gap:14px}
.card{background:rgba(var(--inkrgb),.04);border:2px solid var(--ink);border-radius:16px;padding:22px}
.lbl{font-size:11px;font-weight:700;letter-spacing:.18em;text-transform:uppercase;color:rgba(var(--inkrgb),.42)}
.dim{color:rgba(var(--inkrgb),.55)}
/* ---------- hero ---------- */
.hero{padding:30px 24px 26px;overflow:hidden}
.wordmark{display:flex;align-items:center;gap:10px;font-size:12px;font-weight:900;letter-spacing:.42em;color:rgba(var(--inkrgb),.5);margin-bottom:22px}
.wordmark b{font-weight:900;color:rgba(var(--inkrgb),.9)}
.wordmark canvas{width:22px;height:22px;flex:none}
.stwrap{transition:opacity .3s cubic-bezier(.2,.7,.3,1),transform .3s cubic-bezier(.2,.7,.3,1),filter .3s ease}
.hero.out .stwrap{opacity:0;transform:translateY(16px);filter:blur(8px)}
/* The identity is the device's stretched display type: the name is set at a
   base size, then transform-scaled to fill the card edge-to-edge — short
   names go wide, long names condense, exactly like the LCD. */
.stnamebox{height:62px;margin-bottom:16px;overflow:visible}
.stname{font-size:48px;font-weight:900;letter-spacing:-.045em;line-height:1;
  text-transform:uppercase;white-space:nowrap;display:inline-block;
  transform-origin:left top;color:var(--ink)}
.skel .stname{width:78%}
.stloc{font-size:12px;font-weight:700;letter-spacing:.24em;text-transform:uppercase;color:rgba(var(--inkrgb),.55);margin-bottom:16px;min-height:15px}
.ttl{font-size:16px;font-weight:500;color:rgba(var(--inkrgb),.78);line-height:1.4;min-height:22px;
  transition:opacity .22s ease;overflow-wrap:anywhere}
.herobot{display:flex;align-items:center;justify-content:space-between;margin-top:22px}
.live{display:flex;align-items:center;gap:8px;font-size:11px;font-weight:700;letter-spacing:.18em;text-transform:uppercase;color:rgba(var(--inkrgb),.55)}
.dot{width:8px;height:8px;border-radius:50%;background:#4a4a55;transition:background .3s ease;flex:none}
.dot.on{background:var(--ink);animation:pulse 1.9s ease-in-out infinite}
.dot.tune{background:rgba(var(--inkrgb),.45);animation:pulse .8s ease-in-out infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.35}}
.shuffle{width:66px;height:66px;border-radius:50%;flex:none;position:relative;
  background:var(--ink);
  color:var(--bgc);display:flex;align-items:center;justify-content:center;
  box-shadow:0 6px 20px rgba(0,0,0,.25);
  transition:transform .2s cubic-bezier(.34,1.56,.64,1),box-shadow .25s ease}
.shuffle:active{transform:scale(.9)}
.shuffle svg{width:26px;height:26px;transition:transform .5s cubic-bezier(.34,1.3,.5,1)}
.shuffle.spin svg{transform:rotate(360deg)}
/* ---------- skeleton ---------- */
.skel .stname,.skel .ttl,.skel .stloc{color:transparent!important;-webkit-text-fill-color:transparent;background:none}
.skel .stname::before{content:"";display:block;width:78%;height:.86em;border-radius:10px;background:linear-gradient(100deg,rgba(var(--inkrgb),.07) 35%,rgba(var(--inkrgb),.16) 50%,rgba(var(--inkrgb),.07) 65%);background-size:220% 100%;animation:shim 1.5s linear infinite}
.skel .stloc::before,.skel .ttl::before{content:"";display:block;width:46%;height:12px;border-radius:6px;background:linear-gradient(100deg,rgba(var(--inkrgb),.06) 35%,rgba(var(--inkrgb),.14) 50%,rgba(var(--inkrgb),.06) 65%);background-size:220% 100%;animation:shim 1.5s linear infinite}
.skel .ttl::before{width:62%;height:14px}
@keyframes shim{from{background-position:170% 0}to{background-position:-70% 0}}
/* ---------- volume ---------- */
.volhead{display:flex;justify-content:space-between;align-items:baseline;margin-bottom:16px}
.volpct{font-size:15px;font-weight:800;font-variant-numeric:tabular-nums;color:rgba(var(--inkrgb),.85)}
input[type=range]{-webkit-appearance:none;appearance:none;width:100%;height:44px;background:transparent;display:block}
input[type=range]::-webkit-slider-runnable-track{height:16px;border-radius:8px;background:var(--fill,rgba(var(--inkrgb),.12))}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:30px;height:30px;margin-top:-7px;border-radius:50%;
  background:var(--ink);box-shadow:0 2px 10px rgba(0,0,0,.45);transition:transform .15s ease}
input[type=range]:active::-webkit-slider-thumb{transform:scale(1.15)}
input[type=range]::-moz-range-track{height:16px;border-radius:8px;background:rgba(var(--inkrgb),.12)}
input[type=range]::-moz-range-progress{height:16px;border-radius:8px;background:var(--ink)}
input[type=range]::-moz-range-thumb{width:30px;height:30px;border:none;border-radius:50%;background:var(--ink);box-shadow:0 2px 10px rgba(0,0,0,.45)}
/* ---------- stations ---------- */
.sthead{display:flex;width:100%;justify-content:space-between;align-items:center;padding:2px 0;text-align:left}
.sthead .lbl{pointer-events:none}
.stcount{display:flex;align-items:center;gap:10px;font-size:13px;font-weight:600;color:rgba(var(--inkrgb),.6)}
.chev{width:9px;height:9px;border-right:2px solid rgba(var(--inkrgb),.45);border-bottom:2px solid rgba(var(--inkrgb),.45);
  transform:rotate(45deg) translate(-2px,-2px);transition:transform .3s cubic-bezier(.2,.7,.3,1)}
.open .chev{transform:rotate(225deg)}
.stbody{display:grid;grid-template-rows:0fr;transition:grid-template-rows .42s cubic-bezier(.2,.8,.25,1)}
.open .stbody{grid-template-rows:1fr}
.stbody>div{overflow:hidden}
.stlist{padding-top:16px;display:flex;flex-direction:column}
.strow{display:flex;align-items:center;gap:13px;width:100%;padding:12px 10px;border-radius:14px;text-align:left;
  transition:background .18s ease,transform .15s ease}
.strow:active{background:rgba(var(--inkrgb),.08)}
.strow .flag{font-size:22px;flex:none;width:28px;text-align:center}
.strow .nm{font-size:15px;font-weight:700;letter-spacing:-.01em}
.strow .cc{font-size:12px;font-weight:500;letter-spacing:.06em;text-transform:uppercase;color:rgba(var(--inkrgb),.45);margin-top:2px}
.strow .cur{margin-left:auto;flex:none;width:8px;height:8px;border-radius:50%;background:rgb(100,240,180);
  box-shadow:0 0 8px rgba(100,240,180,.7);opacity:0;transition:opacity .25s ease}
.strow.sel{background:rgba(var(--inkrgb),.07)}
.strow.sel .cur{opacity:1}
/* ---------- segmented ---------- */
.seg{position:relative;display:flex;background:rgba(var(--inkrgb),.06);border:1px solid rgba(var(--inkrgb),.07);border-radius:13px;padding:3px;margin-top:16px}
.seghl{position:absolute;top:3px;left:3px;bottom:3px;width:calc(20% - 3px);border-radius:10px;
  background:rgba(var(--inkrgb),.16);box-shadow:0 2px 8px rgba(0,0,0,.3);
  transition:transform .28s cubic-bezier(.3,1.2,.35,1)}
.seg button{flex:1;position:relative;z-index:1;padding:10px 0;font-size:11.5px;font-weight:700;letter-spacing:.02em;
  color:rgba(var(--inkrgb),.5);border-radius:10px;transition:color .25s ease}
.seg button.on{color:var(--ink)}
.seg.two .seghl{width:calc(50% - 3px)}
/* ---------- rows / toggles ---------- */
.row{display:flex;align-items:center;justify-content:space-between;gap:14px}
.row+.row{margin-top:18px}
.rowtxt .t{font-size:15px;font-weight:700}
.rowtxt .s{font-size:12.5px;color:rgba(var(--inkrgb),.45);margin-top:3px;line-height:1.35}
.tgl{width:52px;height:32px;flex:none;border-radius:16px;background:rgba(var(--inkrgb),.14);position:relative;
  transition:background .25s ease}
.tgl::after{content:"";position:absolute;top:2px;left:2px;width:28px;height:28px;border-radius:50%;background:var(--ink);
  box-shadow:0 2px 6px rgba(0,0,0,.4);transition:transform .25s cubic-bezier(.3,1.3,.4,1)}
.tgl.on{background:rgb(30,210,190)}
.tgl.on::after{transform:translateX(20px)}
.tgl:active{transform:none}
.tgl:active::after{transform:scale(.95)}
.tgl.on:active::after{transform:translateX(20px) scale(.95)}
/* ---------- alarm ---------- */
.altime{font-size:60px;font-weight:800;letter-spacing:-.02em;font-variant-numeric:tabular-nums;text-align:center;
  margin:14px 0 6px;transition:color .3s ease;color:rgba(var(--inkrgb),.35)}
.armed .altime{color:var(--ink)}
.steps{display:flex;justify-content:center;gap:26px;margin-bottom:20px}
.stepg{display:flex;align-items:center;gap:10px}
.stepg .lbl{width:38px;text-align:center}
.stepb{width:44px;height:44px;border-radius:50%;background:rgba(var(--inkrgb),.08);border:1px solid rgba(var(--inkrgb),.09);
  font-size:22px;font-weight:600;line-height:1;display:flex;align-items:center;justify-content:center;
  transition:background .15s ease,transform .15s ease;color:rgba(var(--inkrgb),.85);touch-action:none}
.stepb:active{background:rgba(var(--inkrgb),.16)}
.wakes{font-size:12.5px;color:rgba(var(--inkrgb),.5)}
/* ---------- sleep chips ---------- */
.chips{display:flex;gap:9px;margin-top:16px}
.scgrid{display:grid;grid-template-columns:repeat(3,1fr);gap:9px;margin-top:16px}
.scchip{padding:14px 4px;border-radius:14px;background:rgba(var(--inkrgb),.07);border:1px solid rgba(var(--inkrgb),.08);
  font-size:12.5px;font-weight:700;color:rgba(var(--inkrgb),.75);text-align:center;
  transition:background .18s,border-color .18s,color .18s}
.scchip .e{display:block;font-size:20px;margin-bottom:5px}
.scchip.on{background:var(--ink);border-color:var(--ink);color:var(--bgc)}
.chip{flex:1;padding:13px 0;border-radius:14px;background:rgba(var(--inkrgb),.07);border:1px solid rgba(var(--inkrgb),.08);
  font-size:14px;font-weight:700;text-align:center;transition:background .2s ease,color .2s ease,border-color .2s ease}
.chip.on{background:var(--ink);border-color:var(--ink);color:var(--bgc)}
.sleepst{display:none;align-items:center;justify-content:space-between;margin-top:16px;padding:13px 16px;
  border-radius:14px;background:rgba(180,120,220,.14);border:1px solid rgba(180,120,220,.3)}
.sleepst.show{display:flex}
.sleepst .t{font-size:14px;font-weight:700}
.cancel{font-size:13px;font-weight:700;color:rgba(var(--inkrgb),.65);padding:6px 12px;border-radius:10px;background:rgba(var(--inkrgb),.09)}
/* ---------- footer ---------- */
footer{display:flex;align-items:center;justify-content:center;gap:16px;margin-top:12px;padding-bottom:6px;
  font-size:12px;font-weight:600;letter-spacing:.08em;color:rgba(var(--inkrgb),.4)}
.batt{display:none;align-items:center;gap:7px;padding:6px 12px;border-radius:99px;background:rgba(var(--inkrgb),.06);border:1px solid rgba(var(--inkrgb),.08)}
.batt.show{display:flex}
.bshell{width:22px;height:11px;border:1.5px solid rgba(var(--inkrgb),.5);border-radius:3.5px;position:relative;padding:1.5px}
.bshell::after{content:"";position:absolute;right:-4px;top:2.5px;width:2px;height:4px;border-radius:1px;background:rgba(var(--inkrgb),.5)}
.bfill{height:100%;border-radius:1.5px;background:rgb(100,240,180);min-width:2px;transition:width .5s ease}
.bars{display:flex;align-items:flex-end;gap:2.5px;height:13px}
.bars i{width:3.5px;border-radius:2px;background:rgba(var(--inkrgb),.18);transition:background .4s ease}
.bars i:nth-child(1){height:4px}.bars i:nth-child(2){height:7px}.bars i:nth-child(3){height:10px}.bars i:nth-child(4){height:13px}
.bars i.on{background:rgba(var(--inkrgb),.75)}
/* ---------- reconnect pill ---------- */
.rec{position:fixed;top:calc(env(safe-area-inset-top) + 12px);left:50%;transform:translate(-50%,-56px);z-index:60;
  padding:9px 18px;border-radius:99px;background:rgba(30,30,40,.85);border:1px solid rgba(var(--inkrgb),.12);
  backdrop-filter:blur(14px);-webkit-backdrop-filter:blur(14px);
  font-size:12px;font-weight:700;letter-spacing:.06em;color:rgba(255,210,50,.95);
  transition:transform .35s cubic-bezier(.3,1.2,.4,1)}
.rec.show{transform:translate(-50%,0)}
@media (prefers-reduced-motion:reduce){.blob{animation:none}.dot{animation:none!important}}
</style>
</head>
<body class="skel">
<div class="bg">
  <div class="blob b1"></div><div class="blob b2"></div><div class="blob b3"></div>
  <div class="blob b4"></div><div class="blob b5"></div>
</div>
<div class="grain"></div>
<div class="rec" id="rec">reconnecting…</div>

<main>
  <!-- HERO -->
  <section class="card hero" id="hero">
    <div class="wordmark"><canvas id="glogo"></canvas><span>GL<b>O</b>BO</span></div>
    <div class="stwrap">
      <div class="stnamebox"><h1 class="stname" id="stname">&nbsp;</h1></div>
      <div class="stloc" id="stloc"></div>
      <div class="ttl" id="ttl"></div>
    </div>
    <div class="herobot">
      <div class="live"><div class="dot" id="dot"></div><span id="livetxt">off</span></div>
      <button class="shuffle" id="shufBtn" aria-label="Random station">
        <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round">
          <path d="M16 3h5v5"/><path d="M4 20L21 3"/><path d="M21 16v5h-5"/><path d="M15 15l6 6"/><path d="M4 4l5 5"/>
        </svg>
      </button>
    </div>
  </section>

  <!-- VOLUME -->
  <section class="card">
    <div class="volhead"><span class="lbl">Volume</span><span class="volpct" id="volpct">–</span></div>
    <input type="range" id="vol" min="0" max="21" step="1" value="0" aria-label="Volume">
  </section>

  <!-- STATIONS -->
  <section class="card" id="stcard">
    <button class="sthead" id="sthead" aria-expanded="false">
      <span class="lbl">Stations</span>
      <span class="stcount"><span id="stn">…</span><span class="chev"></span></span>
    </button>
    <div class="stbody"><div><div class="stlist" id="stlist"></div></div></div>
  </section>

  <!-- SOUNDSCAPE LAYER -->
  <section class="card">
    <span class="lbl">Soundscape</span>
    <div class="scgrid" id="scgrid">
      <button class="scchip" data-sc="0"><span class="e">&#8709;</span>Off</button>
      <button class="scchip" data-sc="1"><span class="e">⛵</span>Sailing</button>
      <button class="scchip" data-sc="2"><span class="e">🐚</span>Seaside</button>
      <button class="scchip" data-sc="3"><span class="e">🌧️</span>Rainy</button>
      <button class="scchip" data-sc="4"><span class="e">⛈️</span>Thunder</button>
      <button class="scchip" data-sc="5"><span class="e">🌲</span>Wind</button>
      <button class="scchip" data-sc="6"><span class="e">🌴</span>Jungle</button>
      <button class="scchip" data-sc="7"><span class="e">🌷</span>Garden</button>
      <button class="scchip" data-sc="8"><span class="e">🏙️</span>City</button>
      <button class="scchip" data-sc="9"><span class="e">☕</span>Barista</button>
      <button class="scchip" data-sc="10"><span class="e">🚶</span>Street</button>
      <button class="scchip" data-sc="11"><span class="e">🚂</span>Train</button>
    </div>
    <div class="row" style="margin-top:18px">
      <div class="rowtxt">
        <div class="t">Radio</div>
        <div class="s">Off = the soundscape plays alone</div>
      </div>
      <button class="tgl" id="radiotgl" role="switch" aria-checked="true" aria-label="Radio"></button>
    </div>
  </section>

  <!-- ALARM -->
  <section class="card" id="alcard">
    <span class="lbl">Alarm</span>
    <div class="altime" id="altime">--:--</div>
    <div class="steps">
      <div class="stepg">
        <button class="stepb" data-t="h" data-d="-1">−</button>
        <span class="lbl">hour</span>
        <button class="stepb" data-t="h" data-d="1">+</button>
      </div>
      <div class="stepg">
        <button class="stepb" data-t="m" data-d="-1">−</button>
        <span class="lbl">min</span>
        <button class="stepb" data-t="m" data-d="1">+</button>
      </div>
    </div>
    <div class="row">
      <div class="rowtxt">
        <div class="t">Wake me up</div>
        <div class="s wakes" id="wakes">Alarm is off</div>
      </div>
      <button class="tgl" id="alarm" role="switch" aria-checked="false" aria-label="Alarm armed"></button>
    </div>
  </section>

  <!-- SLEEP -->
  <section class="card">
    <span class="lbl">Sleep timer</span>
    <div class="chips" id="chips">
      <button class="chip" data-min="15">15m</button>
      <button class="chip" data-min="30">30m</button>
      <button class="chip" data-min="45">45m</button>
      <button class="chip" data-min="60">60m</button>
    </div>
    <div class="sleepst" id="sleepst">
      <span class="t" id="sleeptxt"></span>
      <button class="cancel" id="slcancel">Cancel</button>
    </div>
  </section>

  <footer>
    <span class="batt" id="batt"><span class="bshell"><span class="bfill" id="bfill" style="width:60%"></span></span><span id="bpct"></span></span>
    <span class="bars" id="bars"><i></i><i></i><i></i><i></i></span>
    <span>globo.local</span>
  </footer>
</main>

<script>
(function(){
"use strict";
var $=function(s){return document.querySelector(s)};
var FLAGS={"Italia":"🇮🇹","USA":"🇺🇸","UK":"🇬🇧","France":"🇫🇷","Deutschland":"🇩🇪","Suisse":"🇨🇭","Espana":"🇪🇸","Polska":"🇵🇱","Iceland":"🇮🇸","Georgia":"🇬🇪","Turkiye":"🇹🇷","Tunisia":"🇹🇳","Morocco":"🇲🇦","Senegal":"🇸🇳","Uganda":"🇺🇬","Lebanon":"🇱🇧","Japan":"🇯🇵","Canada":"🇨🇦","Mexico":"🇲🇽","Guatemala":"🇬🇹","Puerto Rico":"🇵🇷","Peru":"🇵🇪","Ecuador":"🇪🇨","Australia":"🇦🇺","Portugal":"🇵🇹","Argentina":"🇦🇷","Brasil":"🇧🇷","Congo":"🇨🇩"};
function flag(c){return FLAGS[c]||"🌍"}

var stations=[],cur=-1,fails=0,gotFirst=false;
var dirty={vol:0,sta:0,alarm:0,sleep:0,scape:0};
var fresh=function(k){return Date.now()-dirty[k]<3000};
var al={h:7,m:30,armed:false};
var volMax=21;

function api(p){
  return fetch(p,{cache:"no-store"}).then(function(r){
    if(!r.ok)throw 0;return r.json();
  });
}
function send(p){api(p).catch(function(){})}

/* ----- crossfade text ----- */
function xfade(el,txt){
  if(el.textContent===txt)return;
  el.style.opacity="0";
  setTimeout(function(){el.textContent=txt;if(el.id==="stname")fitName();el.style.opacity="1"},200);
}

/* ----- dynamic type: stretch the name to fill the card ----- */
function fitName(){
  var el=$("#stname");
  var box=el.parentNode;
  var txt=el.textContent.replace(/ /g,"").trim();
  if(!txt){el.style.transform="none";return}
  var w=el.offsetWidth;                 // natural width (transforms ignored)
  if(w<4)return;
  var sx=box.clientWidth/w;
  var sy=1.22;                          // the poster stretch
  el.style.transform="scale("+sx+","+sy+")";
}
window.addEventListener("resize",function(){fitName()});

/* ----- hero ----- */
function locStr(s){
  var parts=[];
  if(s.city)parts.push(s.city);
  if(s.country)parts.push(s.country);
  return flag(s.country)+" "+parts.join(" · ").toUpperCase();
}
function setHero(s){
  xfade($("#stname"),s.name||" ");
  xfade($("#stloc"),locStr(s));
}
function setLive(playing,loading){
  var d=$("#dot"),t=$("#livetxt");
  d.className="dot"+(loading?" tune":playing?" on":"");
  t.textContent=loading?"tuning…":playing?"live":"off";
}

/* ----- volume ----- */
var vol=$("#vol"),volT=null;
function paintVol(v){
  var pc=Math.round(v/volMax*100);
  $("#volpct").textContent=pc+"%";
  vol.style.setProperty("--fill",
    "linear-gradient(90deg,var(--ink) "+pc+"%,rgba(var(--inkrgb),.12) "+pc+"%)");
}
vol.addEventListener("input",function(){
  dirty.vol=Date.now();
  paintVol(+vol.value);
  clearTimeout(volT);
  volT=setTimeout(function(){send("/api/volume?v="+vol.value)},150);
});

/* ----- stations list ----- */
function buildList(){
  var box=$("#stlist");box.innerHTML="";
  stations.forEach(function(s,i){
    var b=document.createElement("button");
    b.className="strow";b.dataset.i=i;
    var f=document.createElement("span");f.className="flag";f.textContent=flag(s.country);
    var w=document.createElement("span");
    var n=document.createElement("div");n.className="nm";n.textContent=s.name;
    var c=document.createElement("div");c.className="cc";
    c.textContent=(s.city?s.city+" · ":"")+(s.country||"");
    w.appendChild(n);w.appendChild(c);
    var dot=document.createElement("span");dot.className="cur";
    b.appendChild(f);b.appendChild(w);b.appendChild(dot);
    b.addEventListener("click",function(){pickStation(i)});
    box.appendChild(b);
  });
  $("#stn").textContent=stations.length+" stations";
  markCurrent();
}
function markCurrent(){
  var rows=document.querySelectorAll(".strow");
  for(var i=0;i<rows.length;i++)rows[i].classList.toggle("sel",+rows[i].dataset.i===cur);
}
function pickStation(i){
  if(i===cur)return;
  cur=i;dirty.sta=Date.now();
  markCurrent();
  if(stations[i])setHero(stations[i]);
  xfade($("#ttl"),"");
  setLive(false,true);
  send("/api/station?i="+i);
}
$("#sthead").addEventListener("click",function(){
  var open=$("#stcard").classList.toggle("open");
  this.setAttribute("aria-expanded",open);
});

/* ----- shuffle ----- */
var shufBusy=false;
$("#shufBtn").addEventListener("click",function(){
  if(shufBusy)return;shufBusy=true;
  var btn=this,hero=$("#hero");
  btn.classList.add("spin");
  hero.classList.add("out");
  dirty.sta=Date.now();
  setLive(false,true);
  api("/api/station?shuffle=1").catch(function(){})
  .then(function(){return api("/api/status")}).catch(function(){return null})
  .then(function(s){
    setTimeout(function(){
      if(s&&s.station){cur=s.station.i;markCurrent();
        $("#stname").textContent=s.station.name;fitName();
        $("#stloc").textContent=locStr(s.station);
        $("#ttl").textContent=s.title||"";
      }
      hero.classList.remove("out");
      setTimeout(function(){btn.classList.remove("spin");shufBusy=false},480);
    },320);
  });
});

/* ----- soundscape layer ----- */
var scChips=Array.prototype.slice.call(document.querySelectorAll("#scgrid .scchip"));
function paintScape(i){
  scChips.forEach(function(c){c.classList.toggle("on",+c.getAttribute("data-sc")===i)});
}
scChips.forEach(function(c){
  c.addEventListener("click",function(){
    var i=+this.getAttribute("data-sc");
    dirty.scape=Date.now();paintScape(i);send("/api/bed?i="+i);
  });
});
function paintRadio(on){
  $("#radiotgl").classList.toggle("on",on);
  $("#radiotgl").setAttribute("aria-checked",on);
}
$("#radiotgl").addEventListener("click",function(){
  var on=!this.classList.contains("on");
  dirty.scape=Date.now();dirty.sta=Date.now();paintRadio(on);
  send("/api/bed?radio="+(on?1:0));
});

/* ----- boot-logo globe (the LCD splash, in miniature) ----- */
/* Orthographic wireframe sphere, 20° axial tilt, 8 half-meridians sweeping
   at ~5.5s/rev, 3 steady parallels — same recipe as drawSplashFrame(). */
(function(){
  var cv=$("#glogo"),ctx=cv.getContext("2d");
  var S=22,R=Math.min(2,window.devicePixelRatio||1);
  cv.width=S*R;cv.height=S*R;
  var TILT=20*Math.PI/180,cT=Math.cos(TILT),sT=Math.sin(TILT);
  var still=matchMedia("(prefers-reduced-motion: reduce)").matches;
  function stroke(pts){
    var pen=false;
    ctx.beginPath();
    for(var i=0;i<pts.length;i++){
      var p=pts[i];
      if(!p){pen=false;continue}
      if(pen)ctx.lineTo(p[0],p[1]);else ctx.moveTo(p[0],p[1]);
      pen=true;
    }
    ctx.stroke();
  }
  function pt(lat,lon,cx,cy,r){
    var x=Math.cos(lat)*Math.sin(lon),y=Math.sin(lat),z=Math.cos(lat)*Math.cos(lon);
    var y2=y*cT-z*sT,z2=y*sT+z*cT;
    return z2>0?[cx+r*x,cy-r*y2]:null;   // back hemisphere stays hidden
  }
  function draw(now){
    var cx=S*R/2,cy=S*R/2,r=cx-1.2*R,i;
    var ink=getComputedStyle(document.documentElement).getPropertyValue("--ink").trim()||"#0c0c10";
    ctx.clearRect(0,0,cv.width,cv.height);
    ctx.strokeStyle=ink;ctx.lineWidth=1.05*R;ctx.lineCap="round";
    ctx.globalAlpha=.9;
    ctx.beginPath();ctx.arc(cx,cy,r,0,6.2832);ctx.stroke();
    var rot=(now%5500)/5500*2*Math.PI;
    ctx.globalAlpha=.6;ctx.lineWidth=.85*R;
    for(var m=0;m<8;m++){
      var lon=rot+m*Math.PI/4,pts=[];
      for(i=0;i<=24;i++)pts.push(pt((i/24-.5)*Math.PI*.98,lon,cx,cy,r));
      stroke(pts);
    }
    for(var pl=-1;pl<=1;pl++){
      var lat=pl*Math.PI/4,pts2=[];
      for(i=0;i<=40;i++)pts2.push(pt(lat,i/40*2*Math.PI,cx,cy,r));
      stroke(pts2);
    }
  }
  if(still){draw(1200)}
  else (function tick(now){draw(now||0);requestAnimationFrame(tick)})();
})();

/* ----- live poster combo ----- */
function setCombo(c){
  if(!c||!c.bg)return;
  var r=document.documentElement.style;
  r.setProperty("--bgc","rgb("+c.bg.join(",")+")");
  r.setProperty("--ink","rgb("+c.ink.join(",")+")");
  r.setProperty("--inkrgb",c.ink.join(","));
  r.setProperty("--ink2","rgb("+c.ink2.join(",")+")");
  var m=document.querySelector('meta[name=theme-color]');
  if(m)m.setAttribute("content","rgb("+c.bg.join(",")+")");
}

/* ----- alarm ----- */
function two(n){return(n<10?"0":"")+n}
function paintAlarm(){
  $("#altime").textContent=two(al.h)+":"+two(al.m);
  $("#alcard").classList.toggle("armed",al.armed);
  var t=$("#alarm");
  t.classList.toggle("on",al.armed);
  t.setAttribute("aria-checked",al.armed);
  var w=$("#wakes");
  if(al.armed){
    var now=new Date();
    var mins=(al.h*60+al.m)-(now.getHours()*60+now.getMinutes());
    if(mins<=0)mins+=1440;
    var h=Math.floor(mins/60),m=mins%60;
    w.textContent="Wakes you in "+(h?h+"h ":"")+m+"m";
  }else w.textContent="Alarm is off";
}
var alT=null;
function alarmChanged(){
  dirty.alarm=Date.now();paintAlarm();
  clearTimeout(alT);
  alT=setTimeout(function(){
    send("/api/alarm?h="+al.h+"&m="+al.m+"&armed="+(al.armed?1:0));
  },450);
}
function stepAlarm(t,d){
  if(t==="h")al.h=(al.h+d+24)%24;else al.m=(al.m+d+60)%60;
  alarmChanged();
}
Array.prototype.forEach.call(document.querySelectorAll(".stepb"),function(b){
  var t=b.dataset.t,d=+b.dataset.d,hold=null,rep=null;
  b.addEventListener("pointerdown",function(e){
    e.preventDefault();stepAlarm(t,d);
    hold=setTimeout(function(){rep=setInterval(function(){stepAlarm(t,d)},90)},420);
  });
  ["pointerup","pointerleave","pointercancel"].forEach(function(ev){
    b.addEventListener(ev,function(){clearTimeout(hold);clearInterval(rep)});
  });
});
$("#alarm").addEventListener("click",function(){
  al.armed=!al.armed;alarmChanged();
});
setInterval(function(){if(al.armed)paintAlarm()},30000);

/* ----- sleep ----- */
var chips=Array.prototype.slice.call(document.querySelectorAll(".chip"));
function paintSleep(active,remain){
  chips.forEach(function(c){c.classList.remove("on")});
  var st=$("#sleepst");
  if(active){
    st.classList.add("show");
    $("#sleeptxt").textContent="🌙 Off in "+remain+" min";
    chips.forEach(function(c){if(+c.dataset.min===remain)c.classList.add("on")});
  }else st.classList.remove("show");
}
chips.forEach(function(c){
  c.addEventListener("click",function(){
    var m=+c.dataset.min;
    dirty.sleep=Date.now();
    paintSleep(true,m);c.classList.add("on");
    send("/api/sleep?min="+m);
  });
});
$("#slcancel").addEventListener("click",function(){
  dirty.sleep=Date.now();paintSleep(false,0);send("/api/sleep?min=0");
});

/* ----- footer ----- */
function paintFooter(s){
  var b=$("#batt");
  if(s.battery&&!s.battery.usb){
    b.classList.add("show");
    $("#bpct").textContent=s.battery.pct+"%";
    $("#bfill").style.width=Math.max(4,s.battery.pct)+"%";
    $("#bfill").style.background=s.battery.pct<20?"rgb(255,90,130)":"rgb(100,240,180)";
  }else b.classList.remove("show");
  var r=s.rssi||-100;
  var n=r>=-55?4:r>=-65?3:r>=-75?2:1;
  var bars=$("#bars").children;
  for(var i=0;i<4;i++)bars[i].classList.toggle("on",i<n);
}

/* ----- status apply ----- */
function apply(s){
  if(!s)return;
  if(!gotFirst){gotFirst=true;document.body.classList.remove("skel")}
  if(s.volumeMax){volMax=s.volumeMax;vol.max=volMax}
  setCombo(s.combo);
  if(!fresh("sta")&&s.station){
    var off=!!s.radioOff;
    var heroKey=off?1000+(s.mix||0):s.station.i;
    if(heroKey!==cur){
      cur=heroKey;markCurrent();
      var hero=off?{name:(s.mixName&&s.mixName!=="none")?s.mixName:"QUIET",city:"soundscape",country:""}:s.station;
      if(!off&&s.mixName&&s.mixName!=="none")
        hero={name:s.station.name,city:s.station.city,country:s.station.country+" · "+s.mixName.toLowerCase()};
      setHero(hero);
    }
    setLive(s.playing,s.loading);
  }else if(fresh("sta")){setLive(s.playing,s.loading)}
  if(!fresh("sta"))xfade($("#ttl"),s.title||"");
  if(!fresh("vol")&&+vol.value!==s.volume){vol.value=s.volume;paintVol(s.volume)}
  if(!fresh("scape")){
    if(typeof s.mix==="number")paintScape(s.mix);
    paintRadio(!s.radioOff);
  }
  if(!fresh("alarm")&&s.alarm){
    al.h=s.alarm.h;al.m=s.alarm.m;al.armed=!!s.alarm.armed;paintAlarm();
  }
  if(!fresh("sleep")&&s.sleep)paintSleep(s.sleep.active,s.sleep.remainMin);
  paintFooter(s);
}

/* ----- polling ----- */
function poll(){
  api("/api/status").then(function(s){
    fails=0;$("#rec").classList.remove("show");apply(s);
  }).catch(function(){
    fails++;
    if(fails>1)$("#rec").classList.add("show");
  });
}
setInterval(function(){if(!document.hidden)poll()},2500);
document.addEventListener("visibilitychange",function(){if(!document.hidden)poll()});

/* ----- boot ----- */
api("/api/stations").then(function(d){
  stations=d.stations||[];buildList();
}).catch(function(){
  setTimeout(function(){
    api("/api/stations").then(function(d){stations=d.stations||[];buildList()}).catch(function(){});
  },3000);
});
poll();
paintVol(0);paintAlarm();
})();
</script>
</body>
</html>
)html";
