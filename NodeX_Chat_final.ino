#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

// ── AP Config (always on) ──
const char* AP_SSID = "NodeX_Tech";
const char* AP_PASS = "password";

ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// ── Users ──
struct User { String name; String ip; };
User activeUsers[10];

// ── Admin ──
String adminIP = "";
bool   adminSet = false;

// ── Router / STA state ──
bool   staConnecting   = false;
bool   staConnected    = false;
String staSSID         = "";
unsigned long staConnectStart = 0;
#define STA_TIMEOUT 15000UL

// ── Message History ──
#define MAX_HISTORY 50
struct StoredMsg { String json; unsigned long ts; };
StoredMsg msgHistory[MAX_HISTORY];
int msgHistoryCount = 0;

void storeMessage(const String& json){
  if(msgHistoryCount < MAX_HISTORY){
    msgHistory[msgHistoryCount].json = json;
    msgHistory[msgHistoryCount].ts   = millis();
    msgHistoryCount++;
  } else {
    for(int i=0;i<MAX_HISTORY-1;i++) msgHistory[i]=msgHistory[i+1];
    msgHistory[MAX_HISTORY-1].json = json;
    msgHistory[MAX_HISTORY-1].ts   = millis();
  }
}

void sendHistoryToClient(uint8_t num){
  unsigned long now = millis();
  String out = "{\"type\":\"history\",\"messages\":[";
  bool first = true;
  for(int i=0;i<msgHistoryCount;i++){
    if(now - msgHistory[i].ts <= 45000UL){
      if(!first) out+=",";
      out += msgHistory[i].json;
      first = false;
    }
  }
  out += "]}";
  webSocket.sendTXT(num, out);
}

// ── Broadcast helpers ──
void broadcastUserList(){
  JsonDocument doc;
  doc["type"] = "userList";
  JsonArray users = doc.createNestedArray("users");
  for(int i=0;i<10;i++){
    if(activeUsers[i].name!=""){
      JsonObject u = users.createNestedObject();
      u["name"] = activeUsers[i].name;
      u["ip"]   = activeUsers[i].ip;
    }
  }
  String out; serializeJson(doc,out);
  webSocket.broadcastTXT(out);
}

void broadcastRouterStatus(){
  JsonDocument doc;
  doc["type"]      = "routerStatus";
  doc["connected"] = staConnected;
  doc["ssid"]      = staConnected ? staSSID : "";
  doc["ip"]        = staConnected ? WiFi.localIP().toString() : "";
  String out; serializeJson(doc,out);
  webSocket.broadcastTXT(out);
}

void sendAdminFlag(uint8_t num, bool isAdmin){
  String msg = "{\"type\":\"adminFlag\",\"isAdmin\":";
  msg += isAdmin ? "true" : "false";
  msg += "}";
  webSocket.sendTXT(num, msg);
}

// ── HTTP: scan networks (admin only) ──
void handleScan(){
  String callerIP = server.client().remoteIP().toString();
  if(callerIP != adminIP){ server.send(403,"application/json","{\"error\":\"Not admin\"}"); return; }
  int n = WiFi.scanNetworks(false, false);
  String json = "[";
  for(int i=0;i<n;i++){
    if(i>0) json+=",";
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + WiFi.RSSI(i)
          + ",\"enc\":" + (WiFi.encryptionType(i)==ENC_TYPE_NONE?"false":"true") + "}";
  }
  json += "]";
  server.send(200,"application/json",json);
}

// ── HTTP: connect to router (admin only) ──
void handleConnect(){
  String callerIP = server.client().remoteIP().toString();
  if(callerIP != adminIP){ server.send(403,"application/json","{\"error\":\"Not admin\"}"); return; }
  if(!server.hasArg("plain")){ server.send(400,"application/json","{\"error\":\"No body\"}"); return; }
  JsonDocument doc;
  if(deserializeJson(doc, server.arg("plain"))){ server.send(400,"application/json","{\"error\":\"Bad JSON\"}"); return; }
  String ssid = doc["ssid"].as<String>();
  String pass = doc["pass"].as<String>();
  server.send(200,"application/json","{\"status\":\"connecting\"}");
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  staSSID         = ssid;
  staConnecting   = true;
  staConnected    = false;
  staConnectStart = millis();
  String notif = "{\"type\":\"routerStatus\",\"connected\":false,\"ssid\":\""+ssid+"\",\"connecting\":true,\"ip\":\"\"}";
  webSocket.broadcastTXT(notif);
}

// ── HTTP: disconnect from router (admin only) ──
void handleDisconnect(){
  String callerIP = server.client().remoteIP().toString();
  if(callerIP != adminIP){ server.send(403,"application/json","{\"error\":\"Not admin\"}"); return; }
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  staConnected  = false;
  staConnecting = false;
  staSSID       = "";
  broadcastRouterStatus();
  server.send(200,"application/json","{\"status\":\"disconnected\"}");
}

// ── WebSocket event ──
void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length){
  if(type == WStype_CONNECTED){
    String ip = webSocket.remoteIP(num).toString();
    if(!adminSet){ adminIP = ip; adminSet = true; }
  }
  else if(type == WStype_TEXT){
    JsonDocument doc;
    if(deserializeJson(doc,payload)) return;
    String t = doc["type"];
    if(t=="login"){
      String ip = webSocket.remoteIP(num).toString();
      activeUsers[num].name = doc["user"].as<String>();
      activeUsers[num].ip   = ip;
      sendHistoryToClient(num);
      broadcastUserList();
      sendAdminFlag(num, (ip == adminIP));
      broadcastRouterStatus();
    }
    else if(t=="chat"){
      doc["fromIP"] = webSocket.remoteIP(num).toString();
      String out; serializeJson(doc,out);
      storeMessage(out);
      webSocket.broadcastTXT(out);
    }
    else if(t=="delete"||t=="reaction"){
      doc["fromIP"] = webSocket.remoteIP(num).toString();
      String out; serializeJson(doc,out);
      storeMessage(out);
      webSocket.broadcastTXT(out);
    }
    else {
      doc["fromIP"] = webSocket.remoteIP(num).toString();
      String out; serializeJson(doc,out);
      webSocket.broadcastTXT(out);
    }
  }
  else if(type == WStype_DISCONNECTED){
    activeUsers[num].name = "";
    broadcastUserList();
  }
}

// ════════════════════════════════════════════════════════
//  HTML PAGE
// ════════════════════════════════════════════════════════
const char chatPage[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Nodex LAN Chat</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{
  background:#000 url('https://w0.peakpx.com/wallpaper/1023/1000/desktop-wallpaper-itachi-uchiha-dark-itachi-uchiha-naruto-shippuden-anime.jpg') no-repeat center center fixed;
  background-size:cover;font-family:'Segoe UI',sans-serif;height:100vh;
  display:flex;align-items:center;justify-content:center;
}

/* ── LOGIN ── */
#login-screen{
  position:fixed;width:100%;height:100%;background:rgba(0,0,0,0.95);
  z-index:1000;display:flex;align-items:center;justify-content:center;
}
.login-box{
  background:#1a0000;padding:35px;border-radius:15px;border:2px solid #ff0000;
  text-align:center;width:330px;box-shadow:0 0 30px #f00,0 0 60px #f004;
}
.login-box input{
  width:100%;margin-top:12px;padding:10px 14px;border-radius:8px;
  border:1px solid #555;background:#2a0000;color:white;outline:none;font-size:14px;
}
.login-box input:focus{border-color:#ff0000;box-shadow:0 0 6px #ff000066}
.login-btn{
  margin-top:15px;width:100%;padding:11px;border-radius:8px;
  background:linear-gradient(135deg,#bc0000,#ff4444);color:white;border:none;
  font-weight:bold;font-size:15px;cursor:pointer;letter-spacing:1px;transition:all 0.2s;
}
.login-btn:hover{transform:scale(1.03);box-shadow:0 0 15px #ff0000aa}

/* ── MAIN UI ── */
#main-ui{
  display:none;width:98%;max-width:1200px;height:92vh;
  background:rgba(10,10,10,0.9);border-radius:12px;overflow:hidden;
  flex-direction:column;border:1px solid #333;
}
.main-body{display:flex;flex:1;overflow:hidden;min-height:0}

/* ── TOP BAR ── */
#topBar{
  display:flex;align-items:center;justify-content:space-between;
  background:#0d0d0d;border-bottom:1px solid #2a2a2a;
  padding:7px 14px;flex-shrink:0;
}
#topBarLeft{color:#ff0000;font-weight:bold;font-size:13px;letter-spacing:1px}
#topBarRight{display:flex;align-items:center;gap:8px}

/* ── ADMIN SCAN BUTTON ── */
#adminScanBtn{
  display:none;background:#1a0000;border:1px solid #ff0000;color:#ff4444;
  border-radius:20px;padding:4px 11px;font-size:10px;cursor:pointer;
  font-weight:bold;white-space:nowrap;transition:all 0.2s;letter-spacing:0.5px;
}
#adminScanBtn:hover{background:#3a0000;box-shadow:0 0 10px #ff000055}

/* ── ROUTER STATUS PILL ── */
#routerPill{
  display:flex;align-items:center;gap:5px;background:#111;
  border:1px solid #333;border-radius:20px;padding:4px 11px;
  font-size:10px;color:#666;transition:all 0.3s;white-space:nowrap;
}
#routerPill.connected{border-color:#00e676;color:#00e676}
#routerPill.connecting{border-color:#ffaa00;color:#ffaa00;animation:ppulse 1s infinite}
#routerDot{width:6px;height:6px;border-radius:50%;background:#444;flex-shrink:0;transition:background 0.3s}
#routerPill.connected  #routerDot{background:#00e676;box-shadow:0 0 5px #00e676}
#routerPill.connecting #routerDot{background:#ffaa00;box-shadow:0 0 5px #ffaa00}
@keyframes ppulse{0%,100%{opacity:1}50%{opacity:0.45}}

/* ── SCAN MODAL ── */
#scanModal{
  display:none;position:fixed;inset:0;background:rgba(0,0,0,0.88);
  z-index:2000;align-items:center;justify-content:center;
}
#scanModal.open{display:flex}
.scan-box{
  background:#111;border:1px solid #ff0000;border-radius:14px;
  width:340px;max-height:78vh;display:flex;flex-direction:column;
  box-shadow:0 0 30px #ff000066;overflow:hidden;
}
.scan-header{
  padding:13px 16px;background:#1a0000;color:#ff0000;font-weight:bold;
  font-size:13px;display:flex;align-items:center;justify-content:space-between;
  flex-shrink:0;
}
.scan-close{cursor:pointer;font-size:17px;color:#666;transition:color 0.2s}
.scan-close:hover{color:#ff4444}
#scanList{flex:1;overflow-y:auto;padding:8px}
.scan-item{
  padding:10px 12px;border-radius:8px;cursor:pointer;transition:0.2s;
  display:flex;align-items:center;justify-content:space-between;
  border:1px solid transparent;margin-bottom:4px;color:#ccc;
}
.scan-item:hover{background:#1e1e1e;border-color:#333}
.scan-ssid{font-size:13px;font-weight:bold;display:flex;align-items:center;gap:5px}
.scan-rssi{font-size:10px;color:#555;margin-top:2px}
.scan-loading{padding:30px;text-align:center;color:#555;font-size:13px}
/* disconnect button inside scan box */
#disconnectBtn{
  display:none;margin:8px;padding:9px;background:#1a0000;border:1px solid #ff4444;
  color:#ff4444;border-radius:8px;cursor:pointer;font-size:12px;font-weight:bold;
  text-align:center;transition:all 0.2s;flex-shrink:0;
}
#disconnectBtn:hover{background:#3a0000}

/* ── PASSWORD MODAL ── */
#passModal{
  display:none;position:fixed;inset:0;background:rgba(0,0,0,0.88);
  z-index:2100;align-items:center;justify-content:center;
}
#passModal.open{display:flex}
.pass-box{
  background:#111;border:1px solid #ff0000;border-radius:14px;
  width:300px;padding:24px;box-shadow:0 0 25px #ff000055;text-align:center;
}
.pass-box h3{color:#ff0000;margin-bottom:14px;font-size:13px;line-height:1.4}
.pass-box input{
  width:100%;padding:9px 12px;border-radius:8px;border:1px solid #444;
  background:#1a1a1a;color:white;outline:none;font-size:14px;margin-bottom:12px;
}
.pass-box input:focus{border-color:#ff0000}
.pbtn-row{display:flex;gap:8px}
.pbtn{flex:1;padding:9px;border-radius:8px;border:none;cursor:pointer;font-weight:bold;font-size:13px;transition:all 0.2s}
.pbtn.ok{background:linear-gradient(135deg,#bc0000,#ff4444);color:white}
.pbtn.ok:hover{transform:scale(1.03)}
.pbtn.cancel{background:#222;color:#777;border:1px solid #333}
.pbtn.cancel:hover{background:#2a2a2a}

/* ── SIDEBAR ── */
.sidebar{width:30%;background:#111;border-right:1px solid #222;display:flex;flex-direction:column;flex-shrink:0}
.sidebar-header{
  padding:14px 18px;background:#1a0000;color:#ff0000;font-weight:bold;
  border-bottom:1px solid #333;font-size:14px;display:flex;align-items:center;gap:8px;
}
#userList{flex:1;overflow-y:auto}
.contact{
  padding:12px 14px;border-bottom:1px solid #1e1e1e;cursor:pointer;transition:0.2s;
  display:flex;align-items:center;justify-content:space-between;color:#ccc;
}
.contact:hover{background:#1a1a1a}
.contact.active{background:#3a0000;color:white;border-left:3px solid #ff0000}
.contact-name{display:flex;align-items:center;gap:8px;font-size:13px}
.online-dot{width:7px;height:7px;border-radius:50%;background:#00e676;flex-shrink:0;box-shadow:0 0 4px #00e676}
.badge{background:#00a8ff;color:white;border-radius:50%;padding:2px 6px;font-size:10px;font-weight:bold;display:none;min-width:18px;text-align:center}

/* ── CHAT AREA ── */
.chat-container{flex:1;display:flex;flex-direction:column;background:rgba(15,15,15,0.7);min-width:0}
#chat-title{
  padding:13px 16px;background:#151515;border-bottom:1px solid #2a2a2a;
  color:#ff0000;font-weight:bold;font-size:16px;display:flex;align-items:center;gap:10px;
  flex-shrink:0;
}
#typing-indicator{font-size:11px;color:#777;font-weight:normal;font-style:italic;margin-left:auto}
#messages{flex:1;padding:16px;overflow-y:auto;display:flex;flex-direction:column;gap:8px;min-height:0}

/* ── MESSAGES ── */
.msg{
  padding:9px 13px;border-radius:14px;max-width:72%;font-size:13.5px;
  color:white;position:relative;word-break:break-word;line-height:1.5;
  animation:popIn 0.2s ease;
}
@keyframes popIn{from{transform:scale(0.92);opacity:0}to{transform:scale(1);opacity:1}}
@keyframes delAnim{from{transform:scale(1);opacity:1}to{transform:scale(0.7);opacity:0}}
.msg.deleting{animation:delAnim 0.3s ease forwards}
.sent{align-self:flex-end;background:#054d44;border-bottom-right-radius:3px}
.received{align-self:flex-start;background:#252525;border:1px solid #333;border-bottom-left-radius:3px}
.msg-sender{font-size:10px;color:#ff6666;font-weight:bold;margin-bottom:2px}
.msg-meta{font-size:9px;color:#666;margin-top:4px;display:flex;align-items:center;justify-content:flex-end;gap:6px}
.msg-text{font-size:14px}
.deleted-msg{font-style:italic;color:#444;font-size:12px}
.msg:hover .msg-actions{opacity:1}
.msg-actions{position:absolute;top:-18px;right:4px;display:flex;gap:4px;opacity:0;transition:opacity 0.2s}
.action-btn{
  background:#1a1a1a;border:1px solid #444;border-radius:8px;
  padding:2px 7px;font-size:11px;cursor:pointer;color:#bbb;white-space:nowrap;transition:all 0.15s;
}
.action-btn:hover{background:#3a0000;border-color:#ff0000;color:#ff4444}
.action-btn.del{color:#ff4444}
.action-btn.del:hover{background:#4a0000}
.reactions{display:flex;flex-wrap:wrap;gap:3px;margin-top:5px}
.reaction-badge{
  background:#1a1a1a;border:1px solid #2a2a2a;border-radius:12px;
  padding:2px 7px;font-size:12px;cursor:pointer;transition:background 0.2s;
}
.reaction-badge:hover{background:#2a2a2a}

/* ── INPUT AREA ── */
.input-area{
  padding:10px 13px;background:#0d0d0d;display:flex;
  align-items:center;gap:8px;position:relative;border-top:1px solid #222;flex-shrink:0;
}
#emojiBtn{
  width:35px;height:35px;flex-shrink:0;background:#1a1a1a;border:1px solid #333;
  border-radius:50%;font-size:18px;cursor:pointer;display:flex;align-items:center;
  justify-content:center;transition:all 0.2s;user-select:none;
}
#emojiBtn:hover{background:#2a2a2a;transform:scale(1.1)}
#emojiPicker{
  display:none;position:absolute;bottom:60px;left:12px;width:305px;
  background:#1a1a1a;border:1px solid #333;border-radius:14px;padding:10px;
  z-index:999;box-shadow:0 6px 25px rgba(0,0,0,0.85);
}
.emoji-tabs{display:flex;gap:4px;margin-bottom:8px;flex-wrap:wrap}
.etab{padding:3px 7px;border-radius:8px;cursor:pointer;font-size:15px;background:#222;border:1px solid #333;transition:background 0.15s}
.etab:hover,.etab.active{background:#3a0000;border-color:#ff0000}
.emoji-search{width:100%;padding:5px 10px;border-radius:8px;border:1px solid #333;background:#222;color:white;outline:none;margin-bottom:8px;font-size:13px}
.emoji-search:focus{border-color:#ff0000}
.emoji-grid{display:flex;flex-wrap:wrap;gap:3px;max-height:155px;overflow-y:auto}
.emoji-item{font-size:21px;cursor:pointer;padding:4px;border-radius:6px;transition:all 0.15s;user-select:none}
.emoji-item:hover{background:#2a2a2a;transform:scale(1.2)}
#msgInput{
  flex:1;padding:10px 15px;border-radius:25px;border:1px solid #333;
  background:#1a1a1a;color:white;outline:none;font-size:14px;transition:border 0.2s;
}
#msgInput:focus{border-color:#bc0000;box-shadow:0 0 6px #bc000044}
#sendBtn{
  padding:10px 18px;background:linear-gradient(135deg,#bc0000,#ff4444);color:white;
  border:none;border-radius:25px;cursor:pointer;font-weight:bold;flex-shrink:0;
  transition:all 0.2s;font-size:13px;
}
#sendBtn:hover{transform:scale(1.05);box-shadow:0 0 12px #ff000055}

/* ── GHOST / MATRIX ── */
#ghostBar{
  display:none;position:fixed;bottom:18px;right:18px;background:#0a0a0a;
  border:1px solid #ff0000;border-radius:12px;padding:7px 13px;color:#ff4444;
  font-size:11px;font-weight:bold;z-index:9999;box-shadow:0 0 15px #ff000055;
  letter-spacing:1px;animation:gpulse 1.5s infinite;
}
@keyframes gpulse{0%,100%{box-shadow:0 0 10px #ff000055}50%{box-shadow:0 0 22px #ff0000bb}}
#ninjaOverlay{display:none;position:fixed;inset:0;pointer-events:none;z-index:9990}

/* ── HISTORY BANNER ── */
#histBanner{
  display:none;background:#1a0a00;border-bottom:1px solid #ff6600;
  padding:5px 16px;font-size:11px;color:#ff9944;text-align:center;
  letter-spacing:0.5px;flex-shrink:0;
}
::-webkit-scrollbar{width:4px}
::-webkit-scrollbar-track{background:#0a0a0a}
::-webkit-scrollbar-thumb{background:#333;border-radius:10px}
</style>
</head>
<body>

<!-- ══ LOGIN ══ -->
<div id="login-screen">
  <div class="login-box">
    <div style="font-size:38px;margin-bottom:8px">⚔️</div>
    <h2 style="color:#ff0000;margin-bottom:5px;letter-spacing:2px">NODEX LAN CHAT</h2>
    <p style="color:#555;font-size:12px;margin-bottom:14px">Local Network Messenger</p>
    <input type="text" id="userName" placeholder="👤  Enter your name"
           onkeydown="if(event.key==='Enter') document.getElementById('passCode').focus()">
    <input type="password" id="passCode" placeholder="🔒  Enter password"
           onkeydown="if(event.key==='Enter') login()">
    <button class="login-btn" onclick="login()">⚡ AUTHENTICATE</button>
    <p style="color:#2a2a2a;font-size:10px;margin-top:12px">NodeX_Tech © LAN Only</p>
  </div>
</div>

<!-- ══ MAIN UI ══ -->
<div id="main-ui">

  <!-- TOP BAR -->
  <div id="topBar">
    <div id="topBarLeft">⚔️ NODEX LAN CHAT</div>
    <div id="topBarRight">
      <button id="adminScanBtn" onclick="openScanModal()">📡 SCAN & CONNECT ROUTER</button>
      <div id="routerPill">
        <div id="routerDot"></div>
        <span id="routerLabel">No Router</span>
      </div>
    </div>
  </div>

  <!-- HISTORY BANNER -->
  <div id="histBanner">📜 Message history restored (last 45 seconds)</div>

  <!-- BODY -->
  <div class="main-body">
    <!-- SIDEBAR -->
    <div class="sidebar">
      <div class="sidebar-header">⚔️ NODEX CHATS</div>
      <div id="userList"></div>
    </div>
    <!-- CHAT -->
    <div class="chat-container">
      <div id="chat-title">
        🌐 Group Chat
        <span id="typing-indicator"></span>
      </div>
      <div id="messages"></div>
      <div class="input-area">
        <div id="emojiBtn" onclick="toggleEmojiPicker()" title="Emojis">😊</div>
        <div id="emojiPicker">
          <div class="emoji-tabs" id="emojiTabs"></div>
          <input class="emoji-search" type="text" id="emojiSearch"
                 placeholder="🔍 Search emoji..." oninput="filterEmojis(this.value)">
          <div class="emoji-grid" id="emojiGrid"></div>
        </div>
        <input type="text" id="msgInput"
               placeholder="Write message… (Enter to send)" autocomplete="off">
        <button id="sendBtn" onclick="sendMsg()">SEND ➤</button>
      </div>
    </div>
  </div>
</div>

<!-- ══ SCAN MODAL ══ -->
<div id="scanModal">
  <div class="scan-box">
    <div class="scan-header">
      <span>📡 Nearby Networks</span>
      <span class="scan-close" onclick="closeScanModal()">✕</span>
    </div>
    <div id="scanList"><div class="scan-loading">⏳ Scanning…</div></div>
    <div id="disconnectBtn" onclick="doDisconnect()">⛔ Disconnect from Router</div>
  </div>
</div>

<!-- ══ PASSWORD MODAL ══ -->
<div id="passModal">
  <div class="pass-box">
    <h3 id="passModalTitle">🔒 Enter WiFi Password</h3>
    <input type="password" id="routerPassInput" placeholder="WiFi password"
           onkeydown="if(event.key==='Enter') doConnect()">
    <div class="pbtn-row">
      <button class="pbtn cancel" onclick="closePassModal()">Cancel</button>
      <button class="pbtn ok"     onclick="doConnect()">Connect ➤</button>
    </div>
  </div>
</div>

<div id="ghostBar">👻 GHOST MODE ACTIVE</div>
<canvas id="ninjaOverlay"></canvas>

<script>
// ════════════════════════════════════════════════════════
//  AUDIO
// ════════════════════════════════════════════════════════
var AudioCtx=window.AudioContext||window.webkitAudioContext, actx=null;
function getACtx(){ if(!actx) actx=new AudioCtx(); return actx; }
function soundSent(){
  try{ var a=getACtx(),o=a.createOscillator(),g=a.createGain(); o.connect(g);g.connect(a.destination);
    o.type="sine"; o.frequency.setValueAtTime(600,a.currentTime);
    o.frequency.exponentialRampToValueAtTime(1100,a.currentTime+0.12);
    g.gain.setValueAtTime(0.25,a.currentTime); g.gain.exponentialRampToValueAtTime(0.001,a.currentTime+0.18);
    o.start(a.currentTime); o.stop(a.currentTime+0.18); }catch(e){}
}
function soundReceived(){
  try{ var a=getACtx();
    [0,0.1].forEach(function(d,i){ var o=a.createOscillator(),g=a.createGain(); o.connect(g);g.connect(a.destination);
      o.type="triangle"; o.frequency.setValueAtTime(i===0?880:1100,a.currentTime+d);
      g.gain.setValueAtTime(0.2,a.currentTime+d); g.gain.exponentialRampToValueAtTime(0.001,a.currentTime+d+0.15);
      o.start(a.currentTime+d); o.stop(a.currentTime+d+0.15); }); }catch(e){}
}
function soundJoin(){
  try{ var a=getACtx(),o=a.createOscillator(),g=a.createGain(); o.connect(g);g.connect(a.destination);
    o.type="sine"; o.frequency.setValueAtTime(300,a.currentTime);
    o.frequency.exponentialRampToValueAtTime(900,a.currentTime+0.25);
    g.gain.setValueAtTime(0.15,a.currentTime); g.gain.exponentialRampToValueAtTime(0.001,a.currentTime+0.3);
    o.start(a.currentTime); o.stop(a.currentTime+0.3); }catch(e){}
}
function soundDelete(){
  try{ var a=getACtx(),o=a.createOscillator(),g=a.createGain(); o.connect(g);g.connect(a.destination);
    o.type="sawtooth"; o.frequency.setValueAtTime(400,a.currentTime);
    o.frequency.exponentialRampToValueAtTime(80,a.currentTime+0.2);
    g.gain.setValueAtTime(0.2,a.currentTime); g.gain.exponentialRampToValueAtTime(0.001,a.currentTime+0.22);
    o.start(a.currentTime); o.stop(a.currentTime+0.22); }catch(e){}
}
document.addEventListener("click",function(){ getACtx(); },{once:true});

// ════════════════════════════════════════════════════════
//  EMOJI
// ════════════════════════════════════════════════════════
var EMOJIS={
  "😀":"smiling grinning","😁":"grin happy","😂":"joy laugh cry","🤣":"rofl laugh",
  "😃":"smile happy","😄":"smile grin","😅":"sweat smile","😆":"laugh satisfied",
  "😇":"innocent angel","😉":"wink","😊":"blush smile","😋":"yum tongue",
  "😍":"heart eyes love","🥰":"smiling hearts","😘":"kiss love","🤩":"star struck wow",
  "😏":"smirk","😒":"unamused","😞":"disappointed sad","😢":"cry sad tear",
  "😭":"sob cry","😤":"triumph huff","😠":"angry mad","😡":"rage angry",
  "🤬":"cursing mad","🤯":"mind blown wow","😱":"scream shocked","😨":"fearful scared",
  "😰":"anxious nervous","😓":"downcast sweat","🤔":"thinking hmm","🤭":"hand mouth oops",
  "🤫":"shush quiet","🤐":"zipper mouth silent","😶":"no mouth","😐":"neutral",
  "🙄":"eye roll","😬":"grimace awkward","🤥":"lying nose","😴":"sleep zzz",
  "🤒":"sick ill","🤕":"hurt injury","🤑":"money rich","🤠":"cowboy",
  "🥳":"party celebrate","😎":"cool sunglasses","🤓":"nerd glasses","🥺":"pleading sad cute",
  "😲":"astonished shocked","🥵":"hot fire","🥶":"cold freeze","😵":"dizzy faint",
  "🤪":"zany crazy","🧐":"monocle",
  "👋":"wave hi hello","👌":"ok perfect","✌️":"peace victory","🤞":"fingers crossed",
  "👍":"thumbs up good","👎":"thumbs down bad","👊":"fist punch","✊":"raised fist",
  "👏":"clap applause","🙌":"raised hands praise","🙏":"pray thanks","💪":"muscle strong",
  "🖕":"middle finger","🤙":"call me shaka","👀":"eyes look","👁":"eye",
  "🧠":"brain smart","💀":"skull dead","👻":"ghost boo","🤖":"robot ai",
  "👾":"alien monster game","😈":"devil evil smiling","💩":"poop","🎃":"pumpkin halloween",
  "❤️":"heart love red","🧡":"orange heart","💛":"yellow heart","💚":"green heart",
  "💙":"blue heart","💜":"purple heart","🖤":"black heart","🤍":"white heart",
  "💔":"broken heart","💕":"two hearts","💯":"100 perfect","🔥":"fire hot lit",
  "💥":"boom explosion","✨":"sparkles magic","⚡":"lightning bolt","🌈":"rainbow",
  "⭐":"star","🌟":"glowing star","🌙":"moon night","☀️":"sun bright",
  "❄️":"snowflake cold","🌊":"wave water ocean","🎉":"party popper","🎊":"confetti celebrate",
  "🏆":"trophy winner","🎯":"target bullseye","🎮":"game controller",
  "🎵":"music note","🎶":"music notes song","🎸":"guitar music",
  "🚀":"rocket space launch","🛸":"ufo alien","🌍":"earth world globe",
  "🔮":"crystal ball magic","💻":"laptop computer","📱":"phone mobile",
  "⌨️":"keyboard type","🔑":"key","🔒":"locked secure","🛡️":"shield protect",
  "⚔️":"swords fight","💣":"bomb","🍕":"pizza food","🍔":"burger food",
  "🍟":"fries food","🌮":"taco food","🍜":"ramen noodles","🍣":"sushi food",
  "🎂":"cake birthday","☕":"coffee drink","🧋":"boba bubble tea","🍺":"beer drink",
  "🥤":"soda drink","⚠️":"warning danger","‼️":"double exclamation",
  "❓":"question","❗":"exclamation","✅":"check done","❌":"cross wrong no",
  "🔞":"18 adult","💤":"zzz sleep","🌀":"cyclone spin","🎭":"masks theater",
  "🧨":"firecracker","🥷":"ninja secret","😺":"cat smile","😸":"cat grin",
  "😹":"cat joy","🙀":"cat shock","😻":"cat heart eyes","🐉":"dragon",
  "🦊":"fox","🐺":"wolf","🦇":"bat dark"
};
var TABS=[
  {icon:"😊",label:"Faces",  keys:["smil","grin","joy","wink","blush","cry","angry","cool","nerd","love","kiss","yum","sad","scared","think"]},
  {icon:"👋",label:"Hands",  keys:["wave","ok","peace","fist","clap","pray","muscle","thumbs","finger","shaka"]},
  {icon:"❤️",label:"Hearts", keys:["heart","love","broken","two hearts","100"]},
  {icon:"🔥",label:"Symbols",keys:["fire","boom","sparkle","lightning","star","rainbow","party","trophy","100","warning","check","cross"]},
  {icon:"🎮",label:"Fun",    keys:["game","rocket","ufo","earth","magic","guitar","music","pizza","burger","cake","coffee","beer","ninja","dragon","wolf","fox","bat"]},
  {icon:"💻",label:"Tech",   keys:["laptop","phone","keyboard","key","lock","shield","sword","bomb","robot","brain"]}
];
var emojiAllList=[];
Object.keys(EMOJIS).forEach(function(e){ emojiAllList.push({e:e,n:EMOJIS[e]}); });
(function buildTabs(){
  var td=document.getElementById("emojiTabs");
  var all=document.createElement("span"); all.className="etab active"; all.textContent="🌐";
  all.dataset.tab="all"; all.onclick=function(){ setTab(this); }; td.appendChild(all);
  TABS.forEach(function(t){
    var s=document.createElement("span"); s.className="etab"; s.textContent=t.icon; s.title=t.label;
    s.dataset.tab=t.label; s.onclick=function(){ setTab(this); }; td.appendChild(s);
  });
  buildEmojiGrid(emojiAllList);
})();
function setTab(el){
  document.querySelectorAll(".etab").forEach(function(e){ e.classList.remove("active"); }); el.classList.add("active");
  document.getElementById("emojiSearch").value="";
  if(el.dataset.tab==="all"){ buildEmojiGrid(emojiAllList); return; }
  var tab=TABS.find(function(t){ return t.label===el.dataset.tab; });
  buildEmojiGrid(tab ? emojiAllList.filter(function(o){ return tab.keys.some(function(k){ return o.n.indexOf(k)!==-1; }); }) : emojiAllList);
}
function buildEmojiGrid(list){
  var grid=document.getElementById("emojiGrid"); grid.innerHTML="";
  list.forEach(function(obj){
    var s=document.createElement("span"); s.className="emoji-item"; s.textContent=obj.e; s.title=obj.n.split(" ")[0];
    s.onclick=function(){ insertEmoji(obj.e); }; grid.appendChild(s);
  });
}
function filterEmojis(q){ q=q.toLowerCase().trim();
  buildEmojiGrid(q ? emojiAllList.filter(function(o){ return o.n.indexOf(q)!==-1; }) : emojiAllList); }
function toggleEmojiPicker(){
  var p=document.getElementById("emojiPicker"); p.style.display=(p.style.display==="block")?"none":"block";
  if(p.style.display==="block") document.getElementById("emojiSearch").focus();
}
function insertEmoji(e){
  var inp=document.getElementById("msgInput"), s=inp.selectionStart, end=inp.selectionEnd;
  inp.value=inp.value.substring(0,s)+e+inp.value.substring(end);
  inp.selectionStart=inp.selectionEnd=s+e.length; inp.focus();
}
document.addEventListener("click",function(ev){
  var p=document.getElementById("emojiPicker"),b=document.getElementById("emojiBtn");
  if(p.style.display==="block"&&!p.contains(ev.target)&&ev.target!==b) p.style.display="none";
});

// ════════════════════════════════════════════════════════
//  ROUTER STATUS UI
// ════════════════════════════════════════════════════════
var routerConnected = false;
function updateRouterPill(data){
  var pill=document.getElementById("routerPill"), label=document.getElementById("routerLabel");
  var discBtn=document.getElementById("disconnectBtn");
  pill.classList.remove("connected","connecting");
  if(data.connecting){
    pill.classList.add("connecting");
    label.textContent="Connecting to "+data.ssid+"…";
    if(discBtn) discBtn.style.display="none";
    routerConnected=false;
  } else if(data.connected){
    pill.classList.add("connected");
    label.textContent="✓ "+data.ssid;
    if(discBtn) discBtn.style.display="block";
    routerConnected=true;
    showToast("✅ Connected to "+data.ssid+" · "+data.ip);
  } else {
    label.textContent="No Router";
    if(discBtn) discBtn.style.display="none";
    routerConnected=false;
    if(data.error==="timeout") showToast("❌ Connection timeout. Wrong password?");
  }
}

// ════════════════════════════════════════════════════════
//  SCAN MODAL
// ════════════════════════════════════════════════════════
var pendingSsid="", pendingEnc=false;

function openScanModal(){
  var modal=document.getElementById("scanModal"); modal.classList.add("open");
  var sl=document.getElementById("scanList"); sl.innerHTML='<div class="scan-loading">🔍 Scanning nearby networks…</div>';
  // Show disconnect button if already connected
  document.getElementById("disconnectBtn").style.display=routerConnected?"block":"none";
  fetch("/scan").then(function(r){ return r.json(); }).then(function(nets){
    if(!nets||nets.length===0){ sl.innerHTML='<div class="scan-loading">No networks found.</div>'; return; }
    nets.sort(function(a,b){ return b.rssi-a.rssi; });
    sl.innerHTML="";
    nets.forEach(function(n){
      var sig=n.rssi>-55?"████":n.rssi>-70?"███░":n.rssi>-80?"██░░":"█░░░";
      var div=document.createElement("div"); div.className="scan-item";
      div.innerHTML='<div>'
        +'<div class="scan-ssid">'+escHtml(n.ssid)+(n.enc?' 🔒':'')+'</div>'
        +'<div class="scan-rssi">'+sig+' '+n.rssi+' dBm</div>'
        +'</div>'
        +'<div style="font-size:18px">'+(n.enc?'🔐':'🔓')+'</div>';
      div.onclick=function(){ selectNetwork(n.ssid,n.enc); };
      sl.appendChild(div);
    });
  }).catch(function(){ sl.innerHTML='<div class="scan-loading">❌ Scan failed. Are you the admin?</div>'; });
}
function closeScanModal(){ document.getElementById("scanModal").classList.remove("open"); }
function selectNetwork(ssid,enc){
  closeScanModal(); pendingSsid=ssid; pendingEnc=enc;
  if(!enc){ connectToRouter(ssid,""); return; }
  document.getElementById("passModalTitle").textContent="🔒 Password for: "+ssid;
  document.getElementById("routerPassInput").value="";
  document.getElementById("passModal").classList.add("open");
  setTimeout(function(){ document.getElementById("routerPassInput").focus(); },80);
}
function closePassModal(){ document.getElementById("passModal").classList.remove("open"); pendingSsid=""; }
function doConnect(){
  var pass=document.getElementById("routerPassInput").value; closePassModal();
  connectToRouter(pendingSsid,pass);
}
function connectToRouter(ssid,pass){
  fetch("/connect",{method:"POST",headers:{"Content-Type":"application/json"},
    body:JSON.stringify({ssid:ssid,pass:pass})}).catch(function(){});
  showToast("⏳ Connecting to "+ssid+"…");
}
function doDisconnect(){
  closeScanModal();
  fetch("/disconnect",{method:"POST"}).catch(function(){});
  showToast("🔌 Disconnecting from router…");
}
function escHtml(s){ return s.replace(/&/g,"&").replace(/</g,"<").replace(/>/g,">").replace(/"/g,"""); }

// ════════════════════════════════════════════════════════
//  CHAT CORE
// ════════════════════════════════════════════════════════
var socket, myName="", currentChat="all", allMessages=[], unreadCounts={};
var deletedMsgs={}, reactStore={}, ghostMode=false, isAdmin=false;

function login(){
  var u=document.getElementById("userName").value.trim();
  var p=document.getElementById("passCode").value;
  if(p==="112211"&&u!==""){
    myName=u; getACtx();
    document.getElementById("login-screen").style.display="none";
    document.getElementById("main-ui").style.display="flex";
    initSocket();
  } else { alert("Galat password ya empty name!"); }
}

var prevUserCount=0;
function initSocket(){
  socket=new WebSocket("ws://"+window.location.hostname+":81/");
  socket.onopen=function(){ socket.send(JSON.stringify({type:"login",user:myName})); focusInput(); };
  socket.onmessage=function(ev){
    var data=JSON.parse(ev.data);
    if(data.type==="userList"){
      if(data.users.length>prevUserCount) soundJoin();
      prevUserCount=data.users.length; updateSidebar(data.users);
    }
    else if(data.type==="adminFlag"){
      isAdmin=data.isAdmin;
      if(isAdmin){ document.getElementById("adminScanBtn").style.display="inline-block"; showToast("👑 You are the Admin"); }
    }
    else if(data.type==="routerStatus"){ updateRouterPill(data); }
    else if(data.type==="history"){
      if(data.messages&&data.messages.length>0){
        data.messages.forEach(function(m){ applyHistoryMsg(m); });
        renderMessages();
        var b=document.getElementById("histBanner"); b.style.display="block";
        setTimeout(function(){ b.style.display="none"; },4000);
      }
    }
    else if(data.type==="chat"){ allMessages.push(data); handleNewMsg(data); }
    else if(data.type==="typing"){
      if(data.from!==myName){
        document.getElementById("typing-indicator").textContent=data.from+" is typing...";
        clearTimeout(window._typClear);
        window._typClear=setTimeout(function(){ document.getElementById("typing-indicator").textContent=""; },2000);
      }
    }
    else if(data.type==="reaction"){ applyReaction(data.msgID,data.emoji,data.from); }
    else if(data.type==="delete"){ deletedMsgs[data.msgID]=true; renderMessages(); }
  };
}

function applyHistoryMsg(m){
  if(m.type==="delete"){ deletedMsgs[m.msgID]=true; return; }
  if(m.type==="reaction"){ applyReaction(m.msgID,m.emoji,m.from); return; }
  if(m.type==="chat") allMessages.push(m);
}
function handleNewMsg(data){
  var sid=(data.to==="all")?"all":data.fromIP;
  var isForMe=(data.to==="all")||(data.to==="private"&&(data.fromIP===currentChat||data.targetIP===currentChat));
  if(currentChat!==sid&&data.from!==myName){ unreadCounts[sid]=(unreadCounts[sid]||0)+1; updateSidebarListOnly(); }
  if(data.from!==myName&&isForMe) soundReceived();
  renderMessages();
}
function focusInput(){ var i=document.getElementById("msgInput"); if(i) i.focus(); }

document.addEventListener("DOMContentLoaded",function(){
  buildEmojiGrid(emojiAllList);
  document.getElementById("msgInput").addEventListener("keydown",function(e){
    if(e.key==="Enter"&&!e.shiftKey){ e.preventDefault(); sendMsg(); return; }
    if(socket&&socket.readyState===1) socket.send(JSON.stringify({type:"typing",from:myName,to:currentChat}));
  });
});

function updateSidebar(u){ window.cachedUsers=u; updateSidebarListOnly(); }
function updateSidebarListOnly(){
  var list=document.getElementById("userList");
  var gc=unreadCounts["all"]||0;
  var gB=gc>0?'<span class="badge" style="display:inline-block">'+gc+'</span>':'<span class="badge"></span>';
  list.innerHTML='<div class="contact '+(currentChat==="all"?"active":"")+'" onclick="setChat(\'all\',\'🌐 Group Chat\')">'
    +'<div class="contact-name"><span class="online-dot"></span>Group Chat</div>'+gB+'</div>';
  if(window.cachedUsers) window.cachedUsers.forEach(function(u){
    if(u.name!==myName){
      var c=unreadCounts[u.ip]||0;
      var bH=c>0?'<span class="badge" style="display:inline-block">'+c+'</span>':'<span class="badge"></span>';
      list.innerHTML+='<div class="contact '+(currentChat===u.ip?"active":"")+'" onclick="setChat(\''+u.ip+'\',\'👤 '+u.name+'\')">'
        +'<div class="contact-name"><span class="online-dot"></span>'+u.name+'</div>'+bH+'</div>';
    }
  });
}
function setChat(id,name){
  currentChat=id; unreadCounts[id]=0;
  document.getElementById("chat-title").innerHTML=name+' <span id="typing-indicator"></span>';
  updateSidebarListOnly(); renderMessages(); focusInput();
}

function renderMessages(){
  var mDiv=document.getElementById("messages"); mDiv.innerHTML="";
  allMessages.forEach(function(m,idx){
    var isG=(currentChat==="all"&&m.to==="all");
    var isP=(currentChat!=="all"&&((m.fromIP===currentChat&&m.to==="private")||(m.from===myName&&m.targetIP===currentChat)));
    if(!isG&&!isP) return;
    var type=(m.from===myName)?"sent":"received", mID="msg_"+idx;
    var isMine=(m.from===myName||m.from==="Anonymous");
    if(deletedMsgs[mID]){
      mDiv.innerHTML+='<div class="msg '+type+'" id="'+mID+'"><span class="deleted-msg">🚫 Message deleted</span><span class="msg-meta">'+m.time+'</span></div>';
      return;
    }
    var rHtml="";
    if(reactStore[mID]){ rHtml='<div class="reactions">'; Object.keys(reactStore[mID]).forEach(function(em){ rHtml+='<span class="reaction-badge" onclick="addReaction(\''+mID+'\',\''+em+'\','+idx+')">'+em+' '+reactStore[mID][em]+'</span>'; }); rHtml+="</div>"; }
    var actHtml='<div class="msg-actions"><span class="action-btn" onclick="showReactPicker(\''+mID+'\','+idx+')">👍</span>';
    if(isMine) actHtml+='<span class="action-btn del" onclick="deleteMsg(\''+mID+'\','+idx+')">🗑️</span>';
    actHtml+="</div>";
    mDiv.innerHTML+='<div class="msg '+type+'" id="'+mID+'">'+(type==="received"?'<div class="msg-sender">'+m.from+'</div>':"")
      +'<div class="msg-text">'+m.text+'</div><span class="msg-meta">'+m.time+'</span>'+rHtml+actHtml+'</div>';
  });
  mDiv.scrollTop=mDiv.scrollHeight;
}

function deleteMsg(mID,idx){
  if(!confirm("Delete this message?")) return;
  deletedMsgs[mID]=true; soundDelete();
  if(socket&&socket.readyState===1) socket.send(JSON.stringify({type:"delete",msgID:mID,from:myName}));
  var el=document.getElementById(mID);
  if(el){ el.classList.add("deleting"); setTimeout(function(){ renderMessages(); },300); } else renderMessages();
}

var quickReacts=["👍","❤️","😂","😮","😢","🔥","💯","😈"];
function showReactPicker(mID,idx){
  var old=document.getElementById("rp_"+mID); if(old){ old.remove(); return; }
  var msgEl=document.getElementById(mID); if(!msgEl) return;
  var div=document.createElement("div"); div.id="rp_"+mID;
  div.style.cssText="position:absolute;top:-50px;left:0;background:#111;border:1px solid #444;border-radius:20px;padding:5px 8px;display:flex;gap:6px;z-index:100;box-shadow:0 3px 12px #0008;";
  quickReacts.forEach(function(em){
    var s=document.createElement("span"); s.textContent=em;
    s.style.cssText="font-size:20px;cursor:pointer;transition:transform 0.15s;";
    s.onmouseover=function(){ s.style.transform="scale(1.3)"; };
    s.onmouseout=function(){  s.style.transform="scale(1)"; };
    s.onclick=function(){ addReaction(mID,em,idx); div.remove(); }; div.appendChild(s);
  });
  msgEl.style.position="relative"; msgEl.appendChild(div);
  setTimeout(function(){ if(div.parentNode) div.remove(); },3500);
}
function addReaction(mID,emoji,idx){
  if(!reactStore[mID]) reactStore[mID]={};
  reactStore[mID][emoji]=(reactStore[mID][emoji]||0)+1;
  if(socket&&socket.readyState===1) socket.send(JSON.stringify({type:"reaction",msgID:mID,emoji:emoji,from:myName}));
  renderMessages();
}
function applyReaction(mID,emoji,from){
  if(!reactStore[mID]) reactStore[mID]={};
  reactStore[mID][emoji]=(reactStore[mID][emoji]||0)+1; renderMessages();
}

function sendMsg(){
  var inp=document.getElementById("msgInput"), txt=inp.value.trim();
  if(txt!==""&&socket&&socket.readyState===WebSocket.OPEN){
    var timeNow=new Date().toLocaleTimeString([],{hour:"2-digit",minute:"2-digit"});
    var dn=ghostMode?"Anonymous":myName;
    socket.send(JSON.stringify({type:"chat",from:dn,text:txt,
      to:(currentChat==="all"?"all":"private"),targetIP:currentChat,time:timeNow}));
    inp.value=""; soundSent(); document.getElementById("emojiPicker").style.display="none"; focusInput();
  }
}

// ── GHOST MODE (Konami) ──
var ks=["ArrowUp","ArrowUp","ArrowDown","ArrowDown","ArrowLeft","ArrowRight","ArrowLeft","ArrowRight","b","a"],ki=0;
document.addEventListener("keydown",function(e){
  if(e.key===ks[ki]){ ki++; if(ki===ks.length){ ki=0; toggleGhostMode(); } } else ki=0;
});
function toggleGhostMode(){
  ghostMode=!ghostMode; document.getElementById("ghostBar").style.display=ghostMode?"block":"none";
  ghostMode ? startMatrixRain() : stopMatrixRain();
  showToast(ghostMode?"👻 Ghost Mode ON":"Ghost Mode OFF");
}
var matrixAnim=null;
function startMatrixRain(){
  var c=document.getElementById("ninjaOverlay"); c.style.display="block";
  c.width=window.innerWidth; c.height=window.innerHeight;
  var ctx=c.getContext("2d"), cols=Math.floor(c.width/16), drops=[];
  for(var i=0;i<cols;i++) drops[i]=1;
  matrixAnim=setInterval(function(){
    ctx.fillStyle="rgba(0,0,0,0.05)"; ctx.fillRect(0,0,c.width,c.height);
    ctx.fillStyle="#ff0000"; ctx.font="14px monospace";
    for(var i=0;i<drops.length;i++){
      ctx.fillText(String.fromCharCode(0x30A0+Math.random()*96),i*16,drops[i]*16);
      if(drops[i]*16>c.height&&Math.random()>0.975) drops[i]=0; drops[i]++;
    }
  },45);
}
function stopMatrixRain(){
  clearInterval(matrixAnim); var c=document.getElementById("ninjaOverlay");
  c.getContext("2d").clearRect(0,0,c.width,c.height); c.style.display="none";
}
function showToast(msg){
  var t=document.createElement("div");
  t.style.cssText="position:fixed;top:18px;left:50%;transform:translateX(-50%);background:#1a1a1a;border:1px solid #ff0000;color:#ff4444;padding:9px 18px;border-radius:10px;z-index:9999;font-size:13px;font-weight:bold;box-shadow:0 0 15px #ff000055;";
  t.textContent=msg; document.body.appendChild(t); setTimeout(function(){ t.remove(); },3000);
}
</script>
</body>
</html>
)=====";

// ════════════════════════════════════════════════════════
//  SETUP & LOOP
// ════════════════════════════════════════════════════════
void setup(){
  Serial.begin(115200);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());

  server.on("/",[](){
    server.send(200,"text/html; charset=utf-8",
      String(reinterpret_cast<const char*>(chatPage)));
  });
  server.on("/scan",       HTTP_GET,  handleScan);
  server.on("/connect",    HTTP_POST, handleConnect);
  server.on("/disconnect", HTTP_POST, handleDisconnect);

  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

void loop(){
  webSocket.loop();
  server.handleClient();

  // ── STA connection watcher ──
  if(staConnecting){
    if(WiFi.status()==WL_CONNECTED){
      staConnecting=false; staConnected=true;
      Serial.print("Router IP: "); Serial.println(WiFi.localIP());
      broadcastRouterStatus();
    } else if(millis()-staConnectStart > STA_TIMEOUT){
      staConnecting=false; staConnected=false;
      WiFi.disconnect(); WiFi.mode(WIFI_AP);
      Serial.println("STA timeout");
      String fail="{\"type\":\"routerStatus\",\"connected\":false,\"ssid\":\"\",\"connecting\":false,\"ip\":\"\",\"error\":\"timeout\"}";
      webSocket.broadcastTXT(fail);
    }
  }
}