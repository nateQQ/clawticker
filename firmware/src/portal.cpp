#include "portal.h"
#include "hw.h"
#include "settings.h"
#include "ui.h"
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

static const char PAGE[] PROGMEM = R"(<!doctype html><html><head><meta charset=utf-8>
<meta name=viewport content="width=device-width,initial-scale=1"><title>ClawTicker setup</title>
<style>body{font:16px system-ui;max-width:420px;margin:24px auto;padding:0 16px}
label{display:block;margin-top:14px;font-weight:600}input,select{width:100%;padding:10px;margin-top:4px;box-sizing:border-box;font-size:16px}
input[type=checkbox]{width:auto;margin-right:8px}.row{font-weight:400;display:flex;align-items:center;margin-top:8px}
button{margin-top:20px;padding:12px;width:100%;font-size:16px}small{color:#666}
canvas{background:#000;border:1px solid #888;width:64px;height:64px;image-rendering:pixelated;display:block;margin-top:8px}</style></head><body>
<h2>ClawTicker setup</h2><form method=post action=/save>
<label>Wi-Fi network</label><input name=ssid list=nets value="%SSID%" required autocomplete=off><datalist id=nets>%NETS%</datalist>
<label>Wi-Fi password</label><input name=pass type=password>
<label>Computer address</label><input name=host value="%HOST%" placeholder="192.168.1.20" required>
<small>The IP address printed by the installer on the computer running the daemon.</small>
<label>Port</label><input name=port value="%PORT%" inputmode=numeric>
<label>Logo (optional)</label>
<input type=file id=file accept="image/*"><small>Shown top left. Any photo or image, cropped to a square.</small>
<label class=row><input type=checkbox id=round checked>Round</label>
<label class=row><input type=checkbox name=rmlogo value=1>Use the default logo</label>
<canvas id=prev width=32 height=32></canvas>
<input type=hidden name=logo id=logo>
<button>Save and restart</button></form>
<script>
const SZ=32,file=document.getElementById('file'),round=document.getElementById('round'),
out=document.getElementById('logo'),prev=document.getElementById('prev').getContext('2d');
let img=null;
function render(){
  if(!img)return;
  const c=document.createElement('canvas');c.width=c.height=SZ;const x=c.getContext('2d');
  const s=Math.min(img.width,img.height),sx=(img.width-s)/2,sy=(img.height-s)/2;
  if(round.checked){x.beginPath();x.arc(SZ/2,SZ/2,SZ/2,0,7);x.clip();}
  x.drawImage(img,sx,sy,s,s,0,0,SZ,SZ);
  prev.clearRect(0,0,SZ,SZ);prev.drawImage(c,0,0);
  const d=x.getImageData(0,0,SZ,SZ).data;let h='';
  for(let i=0;i<d.length;i+=4)h+=[d[i+2],d[i+1],d[i],d[i+3]].map(v=>('0'+v.toString(16)).slice(-2)).join('');
  out.value=h;
}
file.onchange=()=>{const f=file.files[0];if(!f)return;const im=new Image();
  im.onload=()=>{img=im;render();};im.src=URL.createObjectURL(f);};
round.onchange=render;
</script></body></html>)";

static WebServer server(80);
static DNSServer dns;

static String page() {
    String nets, html = FPSTR(PAGE);
    int n = WiFi.scanComplete();
    for (int i = 0; i < n; i++) nets += "<option value=\"" + WiFi.SSID(i) + "\">";
    html.replace("%NETS%", nets);
    html.replace("%SSID%", cfg.ssid);
    html.replace("%HOST%", cfg.host);
    html.replace("%PORT%", String(cfg.port));
    return html;
}

static void save() {
    cfg.ssid = server.arg("ssid");
    if (server.arg("pass").length() || server.arg("ssid") != cfg.ssid) cfg.pass = server.arg("pass");   // blank = keep
    cfg.host = server.arg("host");
    cfg.host.trim();
    int port = server.arg("port").toInt();
    cfg.port = port > 0 && port < 65536 ? port : 8787;
    settings_save();
    String hex = server.arg("logo");
    if (server.arg("rmlogo") == "1") settings_clear_logo();
    else if (hex.length() == LOGO_BYTES * 2) {
        static uint8_t logo[LOGO_BYTES];
        for (int i = 0; i < LOGO_BYTES; i++) logo[i] = strtoul(hex.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
        settings_save_logo(logo);
    }
    server.send(200, "text/html", "<meta name=viewport content='width=device-width'><h3>Saved. Restarting...</h3>");
    delay(800);
    ESP.restart();
}

void portal_run() {
    WiFi.mode(WIFI_AP_STA);
    WiFi.scanNetworks(true);
    WiFi.softAP("ClawTicker-Setup");
    dns.start(53, "*", WiFi.softAPIP());
    server.on("/save", HTTP_POST, save);
    server.onNotFound([]() { server.send(200, "text/html", page()); });
    server.begin();
    ui_draw_portal();
    for (;;) {
        dns.processNextRequest();
        server.handleClient();
        delay(5);
    }
}
