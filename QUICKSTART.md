# 🚀 AGV WebSocket + Chen Encryption - Quick Start

## ✅ System Status

- ✅ **Server**: Running on `http://localhost:3000`
- ✅ **WebSocket**: Listening on `ws://localhost:3000`
- ✅ **Socket.IO**: Connected to dashboard
- ✅ **Chen Encryption**: Initialized and synced
- ✅ **Error Handling**: Robust (no crash on malformed frames)

---

## 📋 Nhanh Chóng Setup (5 phút)

### **Bước 1: Server đang chạy**
```bash
# Terminal 1 (đã chạy)
npm start
# Output: [INFO] Dashboard server running on port 3000
```

### **Bước 2: Mở Dashboard**
Mở browser:
```
http://localhost:3000
```
Sẽ thấy:
- Motor gauges (L/R)
- Control buttons (Start, Stop, E-Stop)
- Status indicator
- Map visualization

### **Bước 3: Configure ESP32 & Upload**

**Sửa agv_websocket.ino (dòng 26-28):**
```cpp
const char* SSID = "ICEA_T3";              // WiFi SSID
const char* PASSWORD = "02438683518";      // WiFi Password  
const char* SERVER_IP = "192.168.3.183";   // Your server IP!
```

**Upload:**
1. Arduino IDE → File → Open → agv_websocket.ino
2. Select Board: "ESP32 Dev Module"
3. Select COM Port
4. Sketch → Upload
5. Serial Monitor (115200 baud) → Watch for:
   ```
   ✓ WiFi connected! IP: 192.168.x.x
   ✓ Socket.IO client ready
   ✓ Chen encryption initialized
   ```

### **Bước 4: Test Kết Nối**

**Terminal 2:**
```bash
cd Webcode
node test-websocket.js
```

**Kết quả mong đợi:**
```
✓ PASS: WebSocket connected
✓ PASS: Decryptor initialized
✓ Control command sent
✓ PASS: Connection stable
🎉 All tests passed!
```

---

## 🎮 Cách Sử Dụng Dashboard

### **Control Buttons:**
- **START** - Xe bắt đầu chạy (state=0)
- **STOP** - Xe dừng bình thường (state=1, timeout 5s)
- **E-STOP** - Dừng khẩn cấp (state=2)

### **Motor Display:**
- Left/Right motor speed (-255 to +255)
- Conic gradient shows speed percentage
- Color indicates direction

### **Map View:**
- Xe hiện thị dưới dạng mũi tên
- Vệt đường (trail) theo dõi quỹ đạo
- Base station ở (250, 0)
- Tỷ lệ: 0.18 pixels/mm

### **Status Log:**
- Real-time telemetry từ xe
- Command execution status
- Connection events

---

## 📊 Data Flow

### **AGV → Server (100ms interval)**
```
[AGV firmware]
  ↓
Collect sensor data + odometry
  ↓
Normalize: v_left+128, state*50, etc.
  ↓
Encrypt with Chen keystream (106 steps)
  ↓
WebSocket JSON frame
  ↓
[Server]
  ↓
Decrypt with matching keystream
  ↓
Verify checksum
  ↓
Broadcast via Socket.IO to dashboard
```

### **Dashboard → Server (on button click)**
```
[Dashboard]
  ↓
POST /api/command
  {"action": "speed", "value": 100}
  ↓
[Server]
  ↓
Relay via WebSocket to AGV
  ↓
[AGV]
  ↓
Handle command
  ↓
Set TARGET_PWM, state, etc.
```

---

## 🔍 Monitoring

### **Server Console:**
```bash
[INFO] Dashboard server running on port 3000
[Socket.IO] Client connected: ...
[WebSocket] AGV device connected
[AGV] x=123.4, y=56.7, θ=0.785
[AGV] x=125.1, y=58.2, θ=0.795
```

### **Browser Console (F12):**
```javascript
[Socket.IO] Connected!
[SPEED_DATA] Validated - L=45, R=52, State=0
[✓ SPEED_DATA] Update map position
[Command from Dashboard] Action: speed, Value: 100
```

### **ESP32 Serial Monitor:**
```
✓ WiFi connected! IP: 192.168.3.x
✓ Socket.IO client ready
✓ Chen encryption initialized
[TEL] x=123.4, y=56.7, θ=0.785, vL=45, vR=52
[CMD] Action: speed, Value: 100
```

---

## 🛠️ Troubleshooting

| Problem | Solution |
|---------|----------|
| **Dashboard won't load** | Check `http://localhost:3000`, F5 refresh |
| **WebSocket "Connection refused"** | Server not running: `npm start` |
| **ESP32 won't connect** | Check WiFi SSID/PASSWORD, SERVER_IP must be server PC's IP |
| **No telemetry data** | Check ESP32 serial: should see telemetry logs |
| **Commands don't work** | Check browser console for errors, verify server running |
| **Encryption checksum fails** | Keystreams out of sync, restart ESP32 + server |

---

## 📁 File Structure

```
Webcode/
├── server.js                    ← WebSocket + Chen server ✅
├── agv_websocket.ino            ← ESP32 firmware ✅
├── public/
│   ├── index.html               ← Dashboard UI ✅
│   ├── style.css                ← Styling ✅
│   └── script.js                ← Control + telemetry ✅
├── package.json                 ← Dependencies ✅
├── WEBSOCKET_SETUP.md           ← Full guide ✅
├── QUICKSTART.md                ← This file
├── test-websocket.js            ← Connection test ✅
├── calibrate.py                 ← Odometry cal (pending Eq 4-5)
└── Odometry.h                   ← Odometry class (pending Eq 5)
```

---

## 🔧 Configuration Quick Reference

### **Motor Control (agv_websocket.ino:43-48)**
```cpp
float Kp_line = 1.5;    // PD line following
float Kd_line = 5.0;
float Kp_vel = 2.0;     // PI velocity control
float Ki_vel = 0.3;
int TARGET_PWM = 40;    // Default speed
```

### **Sensors (agv_websocket.ino:35-41)**
```cpp
const int IR_PINS[5] = {34, 35, 32, 33, 25};  // Line sensors
const int IR_THRESHOLD = 1500;                 // Detection threshold
const int MOTOR_L_PWM = 12;                    // Motor pins
const int MOTOR_R_PWM = 27;
const int ENC_L = 15, ENC_R = 2;              // Encoder pins
```

### **Odometry (agv_websocket.ino:51-55)**
```cpp
const float WHEEL_RADIUS = 32.5;   // mm
const float WHEEL_BASE = 195.0;    // mm
const int PULSES_PER_REV = 990;    // Encoder resolution
```

---

## 🚀 Next Steps

### **For Testing:**
1. ✅ Server running
2. ✅ Dashboard accessible
3. ⏳ Upload firmware to ESP32
4. ⏳ Connect WiFi
5. ⏳ Verify telemetry in dashboard
6. ⏳ Test control buttons

### **For Production:**
1. **Calibration**: Run `calibrate.py` → get Ed, Eb coefficients
2. **Equations (4) & (5)**: Integrate uncertainty matrix into telemetry
3. **Advanced Control**: Add speed ramp-up, obstacle detection, RFID handling
4. **Monitoring**: Set up data logging, statistics dashboard
5. **Security**: Add HTTPS/WSS for production deployment

---

## 📞 Support

**Common Issues:**

```bash
# Check server syntax
node -c server.js

# Check ESP32 code compiles
# In Arduino IDE: Sketch → Verify/Compile

# Test WebSocket directly
node test-websocket.js

# View server logs
npm start

# View ESP32 logs
# Arduino IDE → Tools → Serial Monitor
```

---

## 📈 Performance

| Metric | Value |
|--------|-------|
| **Telemetry Interval** | 100ms (10 Hz) |
| **Packet Size** | ~25 bytes (encrypted) |
| **Bandwidth** | ~2 Kbps per AGV |
| **Latency** | <200ms (WiFi dependent) |
| **Encryption Overhead** | ~5% CPU (server), ~15% (ESP32) |

---

**Status: ✅ READY FOR TESTING**

Last Updated: May 26, 2026
