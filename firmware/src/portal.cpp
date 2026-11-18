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
button{margin-top:20px;padding:12px;width:100%;font-size:16px}small{color:#666}</style></head><body>
<h2>ClawTicker setup</h2><form method=post action=/save>
<label>Wi-Fi network</label><input name=ssid list=nets required autocomplete=off><datalist id=nets>%NETS%</datalist>
<label>Wi-Fi password</label><input name=pass type=password>
<label>Computer address</label><input name=host placeholder="192.168.1.20" required>
<small>The IP address printed by the installer on the computer running the daemon.</small>
<label>Port</label><input name=port value=8787 inputmode=numeric>
<button>Save and restart</button></form></body></html>)";

static WebServer server(80);
static DNSServer dns;

static String page() {
    String nets, html = FPSTR(PAGE);
    int n = WiFi.scanComplete();
    for (int i = 0; i < n; i++) nets += "<option value=\"" + WiFi.SSID(i) + "\">";
    html.replace("%NETS%", nets);
    return html;
}

static void save() {
    cfg.ssid = server.arg("ssid");
    cfg.pass = server.arg("pass");
    cfg.host = server.arg("host");
    cfg.host.trim();
    int port = server.arg("port").toInt();
    cfg.port = port > 0 && port < 65536 ? port : 8787;
    settings_save();
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
