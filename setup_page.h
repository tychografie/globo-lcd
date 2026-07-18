// The captive setup page — the same design grammar as globo.local
// (web_page.h): wordmark + wireframe globe, outlined cards, poster field,
// grain. Served by the device WebServer while the offline hub is up.
#pragma once
static const char SETUP_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="theme-color" content="#2350ff">
<title>GLOBO</title><style>
:root{color-scheme:light;--bgc:#2350ff;--ink:#0c0c10;--inkrgb:12,12,16}
*{margin:0;padding:0;box-sizing:border-box;-webkit-tap-highlight-color:transparent}
body{background:var(--bgc);color:var(--ink);min-height:100vh;
  font-family:-apple-system,BlinkMacSystemFont,"SF Pro Display",system-ui,sans-serif;
  -webkit-font-smoothing:antialiased;
  padding:calc(env(safe-area-inset-top) + 20px) 20px calc(env(safe-area-inset-bottom) + 28px)}
.grain{position:fixed;inset:-50%;width:200%;height:200%;z-index:50;pointer-events:none;opacity:.08;
  background-image:url("data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='140' height='140'><filter id='n'><feTurbulence type='fractalNoise' baseFrequency='0.85' numOctaves='2' stitchTiles='stitch'/></filter><rect width='140' height='140' filter='url(%23n)'/></svg>");
  background-size:140px 140px}
main{position:relative;z-index:1;max-width:620px;margin:0 auto;display:flex;flex-direction:column;gap:14px}
.card{background:rgba(var(--inkrgb),.04);border:2px solid var(--ink);border-radius:16px;padding:22px}
.lbl{font-size:11px;font-weight:700;letter-spacing:.18em;text-transform:uppercase;color:rgba(var(--inkrgb),.42)}
.wordmark{display:flex;align-items:center;gap:10px;font-size:12px;font-weight:900;letter-spacing:.42em;color:var(--ink)}
.wordmark canvas{width:22px;height:22px;flex:none}
.big{font-size:clamp(38px,11vw,64px);font-weight:900;letter-spacing:-.045em;line-height:1;
  text-transform:uppercase;transform:scaleY(1.2);transform-origin:left top;margin:18px 0 10px}
.sub{font-size:13px;font-weight:500;color:rgba(var(--inkrgb),.6);line-height:1.45}
.netlist{margin-top:10px;display:flex;flex-direction:column}
.net{display:flex;align-items:center;gap:12px;width:100%;padding:10px;border-radius:14px;
  text-align:left;font:inherit;color:inherit;background:none;border:none;cursor:pointer;
  transition:background .18s ease}
.net:active{background:rgba(var(--inkrgb),.08)}
.net .nm{font-size:15px;font-weight:700;letter-spacing:-.01em}
.net .sg{font-size:12px;font-weight:500;letter-spacing:.06em;text-transform:uppercase;color:rgba(var(--inkrgb),.45);margin-top:2px}
.net .dot{margin-left:auto;flex:none;width:8px;height:8px;border-radius:50%;background:rgb(100,240,180);
  box-shadow:0 0 8px rgba(100,240,180,.7);opacity:0}
.net.known .dot{opacity:1}
input{display:block;width:100%;background:transparent;border:2px solid rgba(var(--inkrgb),.3);
  border-radius:12px;color:var(--ink);padding:12px 13px;font-size:16px;outline:none;margin-top:8px;
  transition:border-color .18s}
input::placeholder{color:rgba(var(--inkrgb),.4)}
input:focus{border-color:var(--ink)}
button.go{width:100%;background:var(--ink);color:var(--bgc);border:0;border-radius:14px;
  padding:14px;font:inherit;font-size:15px;font-weight:700;margin-top:12px;cursor:pointer;
  transition:transform .16s cubic-bezier(.2,.9,.3,1.4)}
button.go:active{transform:scale(.97)}
.hint{font-size:12.5px;line-height:1.5;color:rgba(var(--inkrgb),.55);margin-top:12px}
#done{display:none}
</style></head><body>
<div class="grain"></div>
<main>
<section class="card" style="padding-bottom:18px">
  <div class="wordmark"><canvas id="glogo"></canvas><span>GL<b>O</b>BO</span></div>
  <div class="big">WIFI</div>
  <div class="sub">Pick the network Globo should live on.</div>
</section>
<div id="form" style="display:contents">
<section class="card">
  <span class="lbl">Networks</span>
  <div class="netlist" id="list"></div>
</section>
<section class="card">
  <span class="lbl">Join</span>
  <input id="s" placeholder="network name" autocapitalize="none" autocorrect="off">
  <input id="p" type="password" placeholder="password">
  <button class="go" onclick="go()">Connect</button>
  <div class="hint">Phone hotspot? Keep the hotspot settings screen open until Globo joins. iPhone: turn on Maximize Compatibility &mdash; Globo speaks 2.4&thinsp;GHz only.</div>
</section>
</div>
<section class="card" id="done">
  <span class="lbl">Connecting</span>
  <div class="big" id="dn" style="font-size:clamp(28px,8vw,44px)"></div>
  <div class="sub">Globo is joining now &mdash; its screen shows the search. If this is your phone's hotspot: close this page and keep the hotspot screen open; Globo walks in by itself.</div>
</section>
</main>
<script>
function $(i){return document.getElementById(i)}
(function(){
  var cv=$("glogo"),ctx=cv.getContext("2d");
  var S=22,R=Math.min(2,window.devicePixelRatio||1);
  cv.width=S*R;cv.height=S*R;
  var TILT=20*Math.PI/180,cT=Math.cos(TILT),sT=Math.sin(TILT);
  function stroke(pts){var pen=false;ctx.beginPath();
    for(var i=0;i<pts.length;i++){var p=pts[i];
      if(!p){pen=false;continue}
      if(pen)ctx.lineTo(p[0],p[1]);else ctx.moveTo(p[0],p[1]);pen=true}
    ctx.stroke()}
  function pt(lat,lon,cx,cy,r){
    var x=Math.cos(lat)*Math.sin(lon),y=Math.sin(lat),z=Math.cos(lat)*Math.cos(lon);
    var y2=y*cT-z*sT,z2=y*sT+z*cT;
    return z2>0?[cx+r*x,cy-r*y2]:null}
  function draw(now){
    var cx=S*R/2,cy=S*R/2,r=cx-1.2*R,i;
    ctx.clearRect(0,0,cv.width,cv.height);
    ctx.strokeStyle="#0c0c10";ctx.lineWidth=1.05*R;ctx.lineCap="round";
    ctx.globalAlpha=.9;ctx.beginPath();ctx.arc(cx,cy,r,0,6.2832);ctx.stroke();
    var rot=(now%5500)/5500*2*Math.PI;
    ctx.globalAlpha=.6;ctx.lineWidth=.85*R;
    for(var m=0;m<8;m++){var lon=rot+m*Math.PI/4,pts=[];
      for(i=0;i<=24;i++)pts.push(pt((i/24-.5)*Math.PI*.98,lon,cx,cy,r));stroke(pts)}
    for(var pl=-1;pl<=1;pl++){var lat=pl*Math.PI/4,pts2=[];
      for(i=0;i<=40;i++)pts2.push(pt(lat,i/40*2*Math.PI,cx,cy,r));stroke(pts2)}
  }
  if(matchMedia("(prefers-reduced-motion: reduce)").matches){draw(1200)}
  else (function tick(now){draw(now||0);requestAnimationFrame(tick)})();
})();
function row(h){
  var b=document.createElement("button");b.className="net"+(h.known?" known":"");
  var w=document.createElement("span");
  var n=document.createElement("div");n.className="nm";n.textContent=h.ssid;
  var c=document.createElement("div");c.className="sg";
  c.textContent=h.rssi>-60?"strong":h.rssi>-72?"ok":"weak";
  w.appendChild(n);w.appendChild(c);
  var d=document.createElement("span");d.className="dot";
  b.appendChild(w);b.appendChild(d);
  b.onclick=function(){$("s").value=h.ssid;$("p").focus()};
  return b}
function load(){fetch("/api/scan").then(function(r){return r.json()}).then(function(d){
  var L=$("list");L.innerHTML="";
  d.hits.sort(function(a,b){return b.rssi-a.rssi}).forEach(function(h){L.appendChild(row(h))});
}).catch(function(){})}
function go(){var s=$("s").value.trim();if(!s)return;
  fetch("/api/wifisave?s="+encodeURIComponent(s)+"&p="+encodeURIComponent($("p").value))
  .then(function(){$("form").style.display="none";$("dn").textContent=s;
   $("done").style.display="block"}).catch(function(){})}
load();setInterval(load,7000);
</script></body></html>)html";
