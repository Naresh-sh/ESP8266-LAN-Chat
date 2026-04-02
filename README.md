
⚔️ NodeX LAN Chat (ESP8266)
'
'
'
odeX LAN Chat is a real-time local network messenger powered by ESP8266.
It creates its own WiFi hotspot and allows multiple users to chat without internet using a modern WhatsApp-style interface.

Built using WebSockets + JSON, the system provides fast, interactive, and lightweight communication over LAN.
'
✨ Features
💬 Real-time Messaging
'
'
'
Group chat + private chat
Typing indicator
Unread message counter
Message timestamps
'
'
'
😀 Emoji & Reactions
Built-in emoji picker
Emoji search
Quick reactions (👍 😂 😮 😢 🔥 💯 😈)
Reaction counters synced across users
'
'
'
🧠 Smart System
Automatic user detection
Message history restore (last 45 seconds)
Delete message sync across all users
Lightweight JSON message handling
🔊 Sound Effects
'
'
'
Custom futuristic sounds for:
'
'
Message sent
Message received
User joined
Message deleted
🎨 Modern UI
Dark futuristic theme
Responsive layout
Sidebar user list
Smooth animations
Emoji popup panel
'
'
'
🧰 Hardware Requirements
Component	Quantity
ESP8266 (NodeMCU / ESP-12E)	1
USB Cable	1
Laptop / Mobile (browser)	1+    
'
'
'
💻 Software Requirements
Arduino IDE
ESP8266 Board Package
Libraries:
ESP8266WiFi
ESP8266WebServer
WebSocketsServer
ArduinoJson
'
'
'
🚀 Getting Started
1.Tools → Board → Boards Manager → install ESP8266
2. Install Required Libraries
'
Arduino IDE → Library Manager → install:
ArduinoJson
WebSocketsServer
'
3. Upload Code to ESP8266
Board selection:
NodeMCU 1.0 (ESP-12E Module)   
Open browser:
http://192.168.4.1
Login credentials:
Chat Password: 112211
Enter any username
'
'
'
📡 How It Works
ESP8266 acts as WiFi Access Point
Users connect via mobile or laptop
Web interface loads from ESP8266
WebSocket server enables real-time messaging
Messages sync instantly between connected clients
'
'
🧠 Technical Overview
Backend (ESP8266)
HTTP server on port 80
WebSocket server on port 81
JSON message routing
User session tracking
Message history buffer
Reaction and delete synchronization
Frontend (Web App)
HTML + CSS + JavaScript
WebSocket client
Emoji database
Audio API sound effects
Canvas animation overlay
'
'
'
📈 Limitations
Maximum 10 users simultaneously
No internet messaging (LAN only)
No file transfer (text + emoji only)
Range depends on WiFi signal strength
🔮 Future Improvements
'
'
Possible upgrades:
'
'
'
File sharing
Image transfer
Voice messages
Message encryption
Admin controls
Chat rooms
OTA firmware update
Message persistence (SPIFFS)
'
'
🔐 Security Note
'
This project is designed for local network communication only.
It does not include advanced encryption by default.
'
Do not use in sensitive environments without implementing encryption.
'
'
📜 License
'
This project is licensed under the MIT License.
'
You are free to:
'
use
modify
distribute
include in other projects
'
with proper credit.
'
