// The captive setup page — poster identity end-to-end, same design as
// globo.local. Served by the device WebServer while the offline hub is up.
#pragma once
static const char SETUP_HTML[] PROGMEM = R"html(<!DOCTYPE html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>GLOBO</title><style>
:root{color-scheme:light}
*{margin:0;padding:0;box-sizing:border-box}
body{background:#2350ff;color:#0c0c10;font-family:-apple-system,BlinkMacSystemFont,system-ui,sans-serif;min-height:100vh;padding:22px 16px;-webkit-font-smoothing:antialiased}
body:before{content:"";position:fixed;inset:0;pointer-events:none;opacity:.08;background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='120' height='120'%3E%3Cfilter id='n'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='.9' numOctaves='2'/%3E%3C/filter%3E%3Crect width='120' height='120' filter='url(%23n)'/%3E%3C/svg%3E")}
.wrap{max-width:400px;margin:0 auto;border:2px solid #0c0c10;border-radius:16px;background:rgba(12,12,16,.04);padding:24px 18px}
h1{font-size:56px;font-weight:900;letter-spacing:-.045em;line-height:1;text-transform:uppercase;transform:scaleY(1.22);transform-origin:center top;text-align:center;margin-bottom:34px}
.lbl{font-size:11px;font-weight:700;letter-spacing:.18em;text-transform:uppercase;color:rgba(12,12,16,.5);margin:14px 0 6px}
.net{display:flex;justify-content:space-between;align-items:center;border:2px solid rgba(12,12,16,.3);border-radius:12px;padding:12px 14px;margin:8px 0;font-weight:700;font-size:15px;cursor:pointer}
.net small{font-weight:600;font-size:12px;color:rgba(12,12,16,.5)}
.net.known{border-color:#0c0c10}
input{display:block;width:100%;background:transparent;border:2px solid rgba(12,12,16,.3);border-radius:12px;color:#0c0c10;padding:13px 14px;font-size:16px;outline:none;margin:6px 0}
input:focus{border-color:#0c0c10}
input::placeholder{color:rgba(12,12,16,.4)}
button{width:100%;background:#0c0c10;color:#f6f4ee;border:0;border-radius:14px;padding:15px;font-size:15px;font-weight:700;margin-top:12px;cursor:pointer}
button:active{transform:scale(.97)}
.hint{font-size:12.5px;line-height:1.5;color:rgba(12,12,16,.55);margin-top:14px}
#done{display:none}
</style></head><body><div class="wrap">
<h1>GLOBO</h1>
<div id="form">
<div class="lbl">Networks</div><div id="list"></div>
<div class="lbl">Or type it</div>
<input id="s" placeholder="network name" autocapitalize="none" autocorrect="off">
<input id="p" type="password" placeholder="password">
<button onclick="go()">Connect</button>
<div class="hint">Phone hotspot? Keep the hotspot settings screen open until Globo joins. iPhone: turn on Maximize Compatibility &mdash; Globo speaks 2.4&thinsp;GHz only.</div>
</div>
<div id="done">
<div class="lbl">Connecting</div>
<div class="net known" id="dn"></div>
<div class="hint">Globo is joining now &mdash; its screen shows the search. If this is your phone's hotspot: close this page and keep the hotspot screen open; Globo walks in by itself.</div>
</div>
</div><script>
function $(i){return document.getElementById(i)}
function row(h){var d=document.createElement("div");d.className="net"+(h.known?" known":"");
 var sp=document.createElement("span");sp.textContent=h.ssid;
 var sm=document.createElement("small");sm.textContent=h.rssi>-60?"strong":h.rssi>-72?"ok":"weak";
 d.appendChild(sp);d.appendChild(sm);
 d.onclick=function(){$("s").value=h.ssid;$("p").focus()};return d}
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

