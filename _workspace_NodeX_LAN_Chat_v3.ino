#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

const char* ssid        = "NodeX_Tech";
const char* password_ap = "password";

ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

struct User { String name; String ip; };
User activeUsers[10];

// ─── Message History Storage ───
#define MAX_HISTORY 50
struct StoredMsg {
  String json;       // full serialized message JSON
  unsigned long ts;  // millis() when stored
};
StoredMsg msgHistory[MAX_HISTORY];
int msgHistoryCount = 0;

void storeMessage(const String& json) {
  if (msgHistoryCount < MAX_HISTORY) {
    msgHistory[msgHistoryCount].json = json;
    msgHistory[msgHistoryCount].ts   = millis();
    msgHistoryCount++;
  } else {
    // Shift out oldest
    for (int i = 0; i < MAX_HISTORY - 1; i++) msgHistory[i] = msgHistory[i+1];
    msgHistory[MAX_HISTORY-1].json = json;
    msgHistory[MAX_HISTORY-1].ts   = millis();
  }
}

// Send stored history (last 45 seconds) to a newly joined client
void sendHistoryTo(uint8_t num) {
  unsigned long now = millis();
  for (int i = 0; i < msgHistoryCount; i++) {
    if (now - msgHistory[i].ts <= 45000UL) {
      webSocket.sendTXT(num, msgHistory[i].json);
    }
  }
}

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
  flex-direction:row;border:1px solid #333;
}

/* ── SIDEBAR ── */
.sidebar{width:30%;background:#111;border-right:1px solid #222;display:flex;flex-direction:column}
.sidebar-header{
  padding:16px 20px;background:#1a0000;color:#ff0000;font-weight:bold;
  border-bottom:1px solid #333;display:flex;align-items:center;gap:8px;font-size:15px;
}
#userList{flex:1;overflow-y:auto}
.contact{
  padding:13px 15px;border-bottom:1px solid #1e1e1e;cursor:pointer;transition:0.25s;
  display:flex;align-items:center;justify-content:space-between;color:#ccc;
}
.contact:hover{background:#1e1e1e}
.contact.active{background:#3a0000;color:white;border-left:4px solid #ff0000}
.contact-name{display:flex;align-items:center;gap:8px;font-size:14px}
.online-dot{width:8px;height:8px;border-radius:50%;background:#00e676;flex-shrink:0;box-shadow:0 0 5px #00e676}
.badge{background:#00a8ff;color:white;border-radius:50%;padding:3px 7px;font-size:10px;font-weight:bold;display:none;min-width:20px;text-align:center}

/* ── CHAT AREA ── */
.chat-container{width:70%;display:flex;flex-direction:column;background:rgba(15,15,15,0.7)}
#chat-title{
  padding:14px 18px;background:#151515;border-bottom:1px solid #2a2a2a;
  color:#ff0000;font-weight:bold;font-size:17px;display:flex;align-items:center;gap:10px;
}
#typing-indicator{font-size:11px;color:#888;font-weight:normal;font-style:italic;min-height:16px;margin-left:auto}
#messages{flex:1;padding:18px;overflow-y:auto;display:flex;flex-direction:column;gap:8px}

/* ── MESSAGES ── */
.msg{
  padding:9px 14px;border-radius:14px;max-width:72%;font-size:13.5px;
  color:white;position:relative;word-break:break-word;line-height:1.5;
  animation:popIn 0.2s ease;
}
@keyframes popIn{from{transform:scale(0.92);opacity:0}to{transform:scale(1);opacity:1}}
@keyframes deleteAnim{from{transform:scale(1);opacity:1}to{transform:scale(0.7);opacity:0}}
.msg.deleting{animation:deleteAnim 0.3s ease forwards}
.sent{align-self:flex-end;background:#054d44;border-bottom-right-radius:3px}
.received{align-self:flex-start;background:#252525;border:1px solid #3a3a3a;border-bottom-left-radius:3px}
.msg-sender{font-size:10px;color:#ff6666;font-weight:bold;margin-bottom:2px}
.msg-meta{font-size:9px;color:#888;margin-top:4px;display:flex;align-items:center;justify-content:flex-end;gap:6px}
.msg-text{font-size:14px}
.deleted-msg{font-style:italic;color:#555;font-size:12px}

/* ── DELETE BUTTON ── */
.msg:hover .msg-actions{opacity:1}
.msg-actions{
  position:absolute;top:-16px;right:4px;display:flex;gap:4px;
  opacity:0;transition:opacity 0.2s;
}
.action-btn{
  background:#1a1a1a;border:1px solid #444;border-radius:8px;
  padding:2px 7px;font-size:11px;cursor:pointer;color:#ccc;white-space:nowrap;
  transition:all 0.15s;
}
.action-btn:hover{background:#3a0000;border-color:#ff0000;color:#ff4444}
.action-btn.del{color:#ff4444}
.action-btn.del:hover{background:#4a0000}

/* ── REACTIONS ── */
.reactions{display:flex;flex-wrap:wrap;gap:3px;margin-top:5px}
.reaction-badge{
  background:#1a1a1a;border:1px solid #333;border-radius:12px;
  padding:2px 7px;font-size:12px;cursor:pointer;transition:background 0.2s;
}
.reaction-badge:hover{background:#2a2a2a}

/* ── INPUT AREA ── */
.input-area{
  padding:10px 14px;background:#0d0d0d;display:flex;
  align-items:center;gap:8px;position:relative;border-top:1px solid #222;
}

/* EMOJI BTN */
#emojiBtn{
  width:36px;height:36px;flex-shrink:0;background:#1a1a1a;border:1px solid #333;
  border-radius:50%;font-size:19px;cursor:pointer;display:flex;align-items:center;
  justify-content:center;transition:all 0.2s;user-select:none;
}
#emojiBtn:hover{background:#2a2a2a;transform:scale(1.1)}

/* EMOJI PICKER */
#emojiPicker{
  display:none;position:absolute;bottom:62px;left:12px;width:310px;
  background:#1a1a1a;border:1px solid #333;border-radius:14px;padding:10px;
  z-index:999;box-shadow:0 6px 25px rgba(0,0,0,0.8);
}
.emoji-tabs{display:flex;gap:4px;margin-bottom:8px;flex-wrap:wrap}
.etab{padding:4px 8px;border-radius:8px;cursor:pointer;font-size:16px;background:#222;border:1px solid #333;transition:background 0.15s}
.etab:hover,.etab.active{background:#3a0000;border-color:#ff0000}
.emoji-search{width:100%;padding:6px 10px;border-radius:8px;border:1px solid #333;background:#222;color:white;outline:none;margin-bottom:8px;font-size:13px}
.emoji-search:focus{border-color:#ff0000}
.emoji-grid{display:flex;flex-wrap:wrap;gap:3px;max-height:160px;overflow-y:auto}
.emoji-item{font-size:22px;cursor:pointer;padding:4px;border-radius:6px;transition:all 0.15s;user-select:none}
.emoji-item:hover{background:#2a2a2a;transform:scale(1.2)}

#msgInput{
  flex:1;padding:10px 16px;border-radius:25px;border:1px solid #333;
  background:#1a1a1a;color:white;outline:none;font-size:14px;transition:border 0.2s;
}
#msgInput:focus{border-color:#bc0000;box-shadow:0 0 6px #bc000055}
#sendBtn{
  padding:10px 20px;background:linear-gradient(135deg,#bc0000,#ff4444);color:white;
  border:none;border-radius:25px;cursor:pointer;font-weight:bold;flex-shrink:0;
  transition:all 0.2s;font-size:13px;
}
#sendBtn:hover{transform:scale(1.05);box-shadow:0 0 12px #ff000066}

/* GHOST MODE */
#ghostBar{
  display:none;position:fixed;bottom:20px;right:20px;background:#0a0a0a;
  border:1px solid #ff0000;border-radius:12px;padding:8px 14px;color:#ff4444;
  font-size:12px;font-weight:bold;z-index:9999;box-shadow:0 0 15px #ff000066;
  letter-spacing:1px;animation:pulse 1.5s infinite;
}
@keyframes pulse{0%,100%{box-shadow:0 0 10px #ff000066}50%{box-shadow:0 0 25px #ff0000cc}}
#ninjaOverlay{display:none;position:fixed;top:0;left:0;width:100%;height:100%;pointer-events:none;z-index:9990}

/* HISTORY BANNER */
#histBanner{
  display:none;background:#1a0a00;border-bottom:1px solid #ff6600;
  padding:6px 16px;font-size:11px;color:#ff9944;text-align:center;
  letter-spacing:0.5px;
}

::-webkit-scrollbar{width:4px}
::-webkit-scrollbar-track{background:#0a0a0a}
::-webkit-scrollbar-thumb{background:#333;border-radius:10px}
</style>
</head>
<body>

<!-- LOGIN -->
<div id="login-screen">
  <div class="login-box">
    <div style="font-size:40px;margin-bottom:8px">⚔️</div>
    <h2 style="color:#ff0000;margin-bottom:5px;letter-spacing:2px">NODEX LAN CHAT</h2>
    <p style="color:#666;font-size:12px;margin-bottom:15px">Local Network Messenger</p>
    <input type="text" id="userName" placeholder="👤  Enter your name"
           onkeydown="if(event.key==='Enter') document.getElementById('passCode').focus()">
    <input type="password" id="passCode" placeholder="🔒  Enter password"
           onkeydown="if(event.key==='Enter') login()">
    <button class="login-btn" onclick="login()">⚡ AUTHENTICATE</button>
    <p style="color:#333;font-size:10px;margin-top:12px">NodeX_Tech © LAN Only</p>
  </div>
</div>

<!-- MAIN UI -->
<div id="main-ui">
  <div class="sidebar">
    <div class="sidebar-header">⚔️ NODEX CHATS</div>
    <div id="userList"></div>
  </div>
  <div class="chat-container">
    <div id="chat-title">
      🌐 Group Chat
      <span id="typing-indicator"></span>
    </div>
    <!-- History restored banner -->
    <div id="histBanner">📜 Message history restored (last 45 seconds)</div>
    <div id="messages"></div>
    <div class="input-area">

      <!-- EMOJI BUTTON -->
      <div id="emojiBtn" onclick="toggleEmojiPicker()" title="Emojis">😊</div>

      <!-- EMOJI PICKER -->
      <div id="emojiPicker">
        <div class="emoji-tabs" id="emojiTabs"></div>
        <input class="emoji-search" type="text" id="emojiSearch"
               placeholder="🔍 Search emoji..." oninput="filterEmojis(this.value)">
        <div class="emoji-grid" id="emojiGrid"></div>
      </div>

      <!-- TEXT INPUT -->
      <input type="text" id="msgInput"
             placeholder="Write message… (Enter to send)" autocomplete="off">

      <!-- SEND BUTTON -->
      <button id="sendBtn" onclick="sendMsg()">SEND ➤</button>
    </div>
  </div>
</div>

<div id="ghostBar">👻 GHOST MODE ACTIVE</div>
<canvas id="ninjaOverlay"></canvas>

<script>
// ════════════════════════════════════════════════════════
//  WEB AUDIO — Custom Futuristic Sounds
// ════════════════════════════════════════════════════════
var AudioCtx = window.AudioContext || window.webkitAudioContext;
var actx = null;
function getACtx(){
  if(!actx) actx = new AudioCtx();
  return actx;
}

function soundSent(){
  try{
    var a=getACtx(), o=a.createOscillator(), g=a.createGain();
    o.connect(g); g.connect(a.destination);
    o.type="sine";
    o.frequency.setValueAtTime(600,a.currentTime);
    o.frequency.exponentialRampToValueAtTime(1100,a.currentTime+0.12);
    g.gain.setValueAtTime(0.25,a.currentTime);
    g.gain.exponentialRampToValueAtTime(0.001,a.currentTime+0.18);
    o.start(a.currentTime); o.stop(a.currentTime+0.18);
  }catch(e){}
}

function soundReceived(){
  try{
    var a=getACtx();
    [0, 0.1].forEach(function(delay, i){
      var o=a.createOscillator(), g=a.createGain();
      o.connect(g); g.connect(a.destination);
      o.type="triangle";
      o.frequency.setValueAtTime(i===0?880:1100, a.currentTime+delay);
      g.gain.setValueAtTime(0.2, a.currentTime+delay);
      g.gain.exponentialRampToValueAtTime(0.001, a.currentTime+delay+0.15);
      o.start(a.currentTime+delay);
      o.stop(a.currentTime+delay+0.15);
    });
  }catch(e){}
}

function soundJoin(){
  try{
    var a=getACtx(), o=a.createOscillator(), g=a.createGain();
    o.connect(g); g.connect(a.destination);
    o.type="sine";
    o.frequency.setValueAtTime(300,a.currentTime);
    o.frequency.exponentialRampToValueAtTime(900,a.currentTime+0.25);
    g.gain.setValueAtTime(0.15,a.currentTime);
    g.gain.exponentialRampToValueAtTime(0.001,a.currentTime+0.3);
    o.start(a.currentTime); o.stop(a.currentTime+0.3);
  }catch(e){}
}

function soundDelete(){
  try{
    var a=getACtx(), o=a.createOscillator(), g=a.createGain();
    o.connect(g); g.connect(a.destination);
    o.type="sawtooth";
    o.frequency.setValueAtTime(400,a.currentTime);
    o.frequency.exponentialRampToValueAtTime(80,a.currentTime+0.2);
    g.gain.setValueAtTime(0.2,a.currentTime);
    g.gain.exponentialRampToValueAtTime(0.001,a.currentTime+0.22);
    o.start(a.currentTime); o.stop(a.currentTime+0.22);
  }catch(e){}
}

document.addEventListener("click", function(){ getACtx(); }, {once:true});

// ════════════════════════════════════════════════════════
//  EMOJI DATABASE
// ════════════════════════════════════════════════════════
var EMOJIS = {
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
  var all=document.createElement("span");
  all.className="etab active"; all.textContent="🌐"; all.title="All";
  all.dataset.tab="all"; all.onclick=function(){ setTab(this); };
  td.appendChild(all);
  TABS.forEach(function(t){
    var s=document.createElement("span");
    s.className="etab"; s.textContent=t.icon; s.title=t.label;
    s.dataset.tab=t.label; s.onclick=function(){ setTab(this); };
    td.appendChild(s);
  });
  buildEmojiGrid(emojiAllList);
})();

function setTab(el){
  document.querySelectorAll(".etab").forEach(function(e){ e.classList.remove("active"); });
  el.classList.add("active");
  document.getElementById("emojiSearch").value="";
  if(el.dataset.tab==="all"){ buildEmojiGrid(emojiAllList); return; }
  var tab=TABS.find(function(t){ return t.label===el.dataset.tab; });
  if(!tab){ buildEmojiGrid(emojiAllList); return; }
  buildEmojiGrid(emojiAllList.filter(function(o){
    return tab.keys.some(function(k){ return o.n.indexOf(k)!==-1; });
  }));
}

function buildEmojiGrid(list){
  var grid=document.getElementById("emojiGrid"); grid.innerHTML="";
  list.forEach(function(obj){
    var span=document.createElement("span");
    span.className="emoji-item"; span.textContent=obj.e; span.title=obj.n.split(" ")[0];
    span.onclick=function(){ insertEmoji(obj.e); };
    grid.appendChild(span);
  });
}
function filterEmojis(q){
  q=q.toLowerCase().trim();
  if(!q){ buildEmojiGrid(emojiAllList); return; }
  buildEmojiGrid(emojiAllList.filter(function(o){ return o.n.indexOf(q)!==-1; }));
}
function toggleEmojiPicker(){
  var p=document.getElementById("emojiPicker");
  p.style.display=(p.style.display==="block")?"none":"block";
  if(p.style.display==="block") document.getElementById("emojiSearch").focus();
}
function insertEmoji(e){
  var inp=document.getElementById("msgInput");
  var s=inp.selectionStart, end=inp.selectionEnd;
  inp.value=inp.value.substring(0,s)+e+inp.value.substring(end);
  inp.selectionStart=inp.selectionEnd=s+e.length; inp.focus();
}
document.addEventListener("click",function(ev){
  var p=document.getElementById("emojiPicker"), b=document.getElementById("emojiBtn");
  if(p.style.display==="block"&&!p.contains(ev.target)&&ev.target!==b) p.style.display="none";
});

// ════════════════════════════════════════════════════════
//  CHAT CORE
// ════════════════════════════════════════════════════════
var socket, myName="", currentChat="all", allMessages=[], unreadCounts={};
var deletedMsgs={}, reactStore={};
var ghostMode=false;
var historyLoaded=false;

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
  socket=new WebSocket('ws://'+window.location.hostname+':81/');
  socket.onopen=function(){
    socket.send(JSON.stringify({type:"login",user:myName})); focusInput();
  };
  socket.onmessage=function(ev){
    var data=JSON.parse(ev.data);

    if(data.type==="userList"){
      var newCount=data.users.length;
      if(newCount>prevUserCount) soundJoin();
      prevUserCount=newCount;
      updateSidebar(data.users);
    }
    // ── History replay packet sent by server on login ──
    else if(data.type==="history"){
      if(data.messages && data.messages.length>0){
        historyLoaded=true;
        data.messages.forEach(function(m){ applyHistoryMsg(m); });
        renderMessages();
        var banner=document.getElementById("histBanner");
        banner.style.display="block";
        setTimeout(function(){ banner.style.display="none"; }, 4000);
      }
    }
    else if(data.type==="chat"){
      allMessages.push(data); handleNewMsg(data);
    }
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

// Apply a history message silently (no sounds, no unread count bump)
function applyHistoryMsg(m){
  if(m.type==="delete"){ deletedMsgs[m.msgID]=true; return; }
  if(m.type==="reaction"){ applyReaction(m.msgID,m.emoji,m.from); return; }
  if(m.type==="chat"){ allMessages.push(m); }
}

function handleNewMsg(data){
  var sid=(data.to==="all")?"all":data.fromIP;
  var isForMe=(data.to==="all")||(data.to==="private"&&(data.fromIP===currentChat||data.targetIP===currentChat));
  if(currentChat!==sid&&data.from!==myName){
    unreadCounts[sid]=(unreadCounts[sid]||0)+1;
    updateSidebarListOnly();
  }
  if(data.from!==myName&&isForMe){ soundReceived(); }
  renderMessages();
}

function focusInput(){ var i=document.getElementById("msgInput"); if(i) i.focus(); }

document.addEventListener("DOMContentLoaded",function(){
  buildEmojiGrid(emojiAllList);
  document.getElementById("msgInput").addEventListener("keydown",function(e){
    if(e.key==="Enter"&&!e.shiftKey){ e.preventDefault(); sendMsg(); return; }
    if(socket&&socket.readyState===1)
      socket.send(JSON.stringify({type:"typing",from:myName,to:currentChat}));
  });
});

function updateSidebar(u){ window.cachedUsers=u; updateSidebarListOnly(); }

function updateSidebarListOnly(){
  var list=document.getElementById("userList");
  var gc=unreadCounts["all"]||0;
  var gB=gc>0?'<span class="badge" style="display:inline-block">'+gc+'</span>':'<span class="badge"></span>';
  list.innerHTML='<div class="contact '+(currentChat==='all'?'active':'')+'" onclick="setChat(\'all\',\'🌐 Group Chat\')">'
    +'<div class="contact-name"><span class="online-dot"></span>Group Chat</div>'+gB+'</div>';
  if(window.cachedUsers){
    window.cachedUsers.forEach(function(u){
      if(u.name!==myName){
        var c=unreadCounts[u.ip]||0;
        var bH=c>0?'<span class="badge" style="display:inline-block">'+c+'</span>':'<span class="badge"></span>';
        list.innerHTML+='<div class="contact '+(currentChat===u.ip?'active':'')+'" onclick="setChat(\''+u.ip+'\',\'👤 '+u.name+'\')">'
          +'<div class="contact-name"><span class="online-dot"></span>'+u.name+'</div>'+bH+'</div>';
      }
    });
  }
}

function setChat(id,name){
  currentChat=id; unreadCounts[id]=0;
  document.getElementById("chat-title").innerHTML=name+' <span id="typing-indicator"></span>';
  updateSidebarListOnly(); renderMessages(); focusInput();
}

// ── RENDER MESSAGES ──
function renderMessages(){
  var mDiv=document.getElementById("messages"); mDiv.innerHTML="";
  allMessages.forEach(function(m,idx){
    var isG=(currentChat==="all"&&m.to==="all");
    var isP=(currentChat!=="all"&&((m.fromIP===currentChat&&m.to==="private")||(m.from===myName&&m.targetIP===currentChat)));
    if(!isG&&!isP) return;

    var type=(m.from===myName)?"sent":"received";
    var mID="msg_"+idx;
    var isMine=(m.from===myName||m.from==="Anonymous");

    if(deletedMsgs[mID]){
      mDiv.innerHTML+='<div class="msg '+type+'" id="'+mID+'">'
        +'<span class="deleted-msg">🚫 Message deleted</span>'
        +'<span class="msg-meta">'+m.time+'</span></div>';
      return;
    }

    var rHtml="";
    if(reactStore[mID]){
      rHtml='<div class="reactions">';
      Object.keys(reactStore[mID]).forEach(function(em){
        rHtml+='<span class="reaction-badge" onclick="addReaction(\''+mID+'\',\''+em+'\','+idx+')">'+em+' '+reactStore[mID][em]+'</span>';
      });
      rHtml+="</div>";
    }

    var actHtml='<div class="msg-actions">';
    actHtml+='<span class="action-btn" onclick="showReactPicker(\''+mID+'\','+idx+')">👍</span>';
    if(isMine) actHtml+='<span class="action-btn del" onclick="deleteMsg(\''+mID+'\','+idx+')">🗑️</span>';
    actHtml+="</div>";

    mDiv.innerHTML+='<div class="msg '+type+'" id="'+mID+'">'
      +(type==="received"?'<div class="msg-sender">'+m.from+'</div>':"")
      +'<div class="msg-text">'+m.text+'</div>'
      +'<span class="msg-meta">'+m.time+'</span>'
      +rHtml+actHtml+'</div>';
  });
  mDiv.scrollTop=mDiv.scrollHeight;
}

// ── DELETE MESSAGE ──
function deleteMsg(mID, idx){
  if(!confirm("Delete this message?")) return;
  deletedMsgs[mID]=true;
  soundDelete();
  if(socket&&socket.readyState===1){
    socket.send(JSON.stringify({type:"delete",msgID:mID,from:myName}));
  }
  var el=document.getElementById(mID);
  if(el){ el.classList.add("deleting"); setTimeout(function(){ renderMessages(); },300); }
  else { renderMessages(); }
}

// ── REACTIONS ──
var quickReacts=["👍","😂","😮","😢","🔥","💯","😈"];
function showReactPicker(mID,idx){
  var old=document.getElementById("rp_"+mID); if(old){ old.remove(); return; }
  var msgEl=document.getElementById(mID); if(!msgEl) return;
  var div=document.createElement("div"); div.id="rp_"+mID;
  div.style.cssText="position:absolute;top:-50px;left:0;background:#111;border:1px solid #444;"
    +"border-radius:20px;padding:5px 8px;display:flex;gap:6px;z-index:100;box-shadow:0 3px 12px #0008;";
  quickReacts.forEach(function(em){
    var s=document.createElement("span");
    s.textContent=em; s.style.cssText="font-size:20px;cursor:pointer;transition:transform 0.15s;";
    s.onmouseover=function(){ s.style.transform="scale(1.3)"; };
    s.onmouseout=function(){ s.style.transform="scale(1)"; };
    s.onclick=function(){ addReaction(mID,em,idx); div.remove(); };
    div.appendChild(s);
  });
  msgEl.style.position="relative"; msgEl.appendChild(div);
  setTimeout(function(){ if(div.parentNode) div.remove(); },3500);
}

function addReaction(mID,emoji,idx){
  if(!reactStore[mID]) reactStore[mID]={};
  reactStore[mID][emoji]=(reactStore[mID][emoji]||0)+1;
  if(socket&&socket.readyState===1)
    socket.send(JSON.stringify({type:"reaction",msgID:mID,emoji:emoji,from:myName}));
  renderMessages();
}

function applyReaction(mID,emoji,from){
  if(!reactStore[mID]) reactStore[mID]={};
  reactStore[mID][emoji]=(reactStore[mID][emoji]||0)+1;
  renderMessages();
}

// ── SEND TEXT MESSAGE ──
function sendMsg(){
  var inp=document.getElementById("msgInput");
  var txt=inp.value.trim();
  if(txt!==""&&socket&&socket.readyState===WebSocket.OPEN){
    var timeNow=new Date().toLocaleTimeString([],{hour:'2-digit',minute:'2-digit'});
    var dn=ghostMode?"Anonymous":myName;
    socket.send(JSON.stringify({type:"chat",from:dn,text:txt,
      to:(currentChat==="all"?"all":"private"),targetIP:currentChat,time:timeNow}));
    inp.value=""; soundSent();
    document.getElementById("emojiPicker").style.display="none";
    focusInput();
  }
}

// ── GHOST MODE (Konami Code) ──
var konamiSeq=["ArrowUp","ArrowUp","ArrowDown","ArrowDown","ArrowLeft","ArrowRight","ArrowLeft","ArrowRight","b","a"];
var konamiIdx=0;
document.addEventListener("keydown",function(e){
  if(e.key===konamiSeq[konamiIdx]){ konamiIdx++;
    if(konamiIdx===konamiSeq.length){ konamiIdx=0; toggleGhostMode(); }
  } else { konamiIdx=0; }
});

function toggleGhostMode(){
  ghostMode=!ghostMode;
  document.getElementById("ghostBar").style.display=ghostMode?"block":"none";
  if(ghostMode){ startMatrixRain(); showToast("👻 Ghost Mode ON"); }
  else { stopMatrixRain(); showToast("Ghost Mode OFF"); }
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
      if(drops[i]*16>c.height&&Math.random()>0.975) drops[i]=0;
      drops[i]++;
    }
  },45);
}
function stopMatrixRain(){
  clearInterval(matrixAnim);
  var c=document.getElementById("ninjaOverlay");
  c.getContext("2d").clearRect(0,0,c.width,c.height);
  c.style.display="none";
}

function showToast(msg){
  var t=document.createElement("div");
  t.style.cssText="position:fixed;top:20px;left:50%;transform:translateX(-50%);"
    +"background:#1a1a1a;border:1px solid #ff0000;color:#ff4444;padding:10px 20px;"
    +"border-radius:10px;z-index:9999;font-size:13px;font-weight:bold;"
    +"box-shadow:0 0 15px #ff000066;";
  t.textContent=msg; document.body.appendChild(t);
  setTimeout(function(){ t.remove(); },3000);
}
</script>
</body>
</html>
)=====";

// ═══════════════════════════════════════════════════════
//  ESP8266 BACKEND
// ═══════════════════════════════════════════════════════

void broadcastUserList(){
    JsonDocument doc;
    doc["type"]="userList";
    JsonArray users=doc.createNestedArray("users");
    for(int i=0;i<10;i++){
        if(activeUsers[i].name!=""){
            JsonObject u=users.createNestedObject();
            u["name"]=activeUsers[i].name;
            u["ip"]=activeUsers[i].ip;
        }
    }
    String out; serializeJson(doc,out);
    webSocket.broadcastTXT(out);
}

// ── Send last-45-sec history to a newly connected client ──
void sendHistoryToClient(uint8_t num){
    unsigned long now=millis();
    // Build a single "history" wrapper packet with all qualifying messages
    String out="{\"type\":\"history\",\"messages\":[";
    bool first=true;
    for(int i=0;i<msgHistoryCount;i++){
        if(now - msgHistory[i].ts <= 45000UL){
            if(!first) out+=",";
            out+=msgHistory[i].json;
            first=false;
        }
    }
    out+="]}";
    webSocket.sendTXT(num, out);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length){
    if(type==WStype_TEXT){
        JsonDocument doc;
        if(deserializeJson(doc,payload)) return;
        String t=doc["type"];

        if(t=="login"){
            activeUsers[num].name=doc["user"].as<String>();
            activeUsers[num].ip=webSocket.remoteIP(num).toString();
            // ── Send history FIRST, then broadcast updated user list ──
            sendHistoryToClient(num);
            broadcastUserList();
        }
        else if(t=="chat"){
            // Store in history before broadcasting
            doc["fromIP"]=webSocket.remoteIP(num).toString();
            String out; serializeJson(doc,out);
            storeMessage(out);
            webSocket.broadcastTXT(out);
        }
        else if(t=="delete"||t=="reaction"){
            // Record delete/reaction so history replay is consistent
            doc["fromIP"]=webSocket.remoteIP(num).toString();
            String out; serializeJson(doc,out);
            storeMessage(out);
            webSocket.broadcastTXT(out);
        }
        else {
            // typing — broadcast only, do NOT store
            doc["fromIP"]=webSocket.remoteIP(num).toString();
            String out; serializeJson(doc,out);
            webSocket.broadcastTXT(out);
        }
    }
    else if(type==WStype_DISCONNECTED){
        activeUsers[num].name="";
        broadcastUserList();
    }
}

void setup(){
    Serial.begin(115200);
    WiFi.softAP(ssid,password_ap);
    server.on("/",[](){ server.send(200,"text/html; charset=utf-8",
        String(reinterpret_cast<const char*>(chatPage))); });
    server.begin();
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
}

void loop(){
    webSocket.loop();
    server.handleClient();
}