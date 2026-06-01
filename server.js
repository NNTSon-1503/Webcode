require('dotenv').config();
const express = require('express');
const http = require('http');
const WebSocket = require('ws');
const { Server } = require('socket.io');
const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');
const path = require('path');

const app = express();
const server = http.createServer(app);
const io = new Server(server);

// ==========================================================
// CHEN CHAOS DECRYPTION CLASS - T-S SMC (KHÔNG DÙNG DOB)
// ==========================================================
class ChenReceiver {
    constructor() {
        // Tham số hệ thống giống hệt MATLAB
        this.a = 35.0; this.b = 3.0; this.c = 28.0; this.M = 100.0;
        this.dt = 0.001; // h = 0.001

        // Trạng thái Slave cục bộ (khởi tạo gần với master để tránh lỗi lớn lúc kết nối đầu tiên)
        this.xs = 0.15; this.ys = 0.25; this.zs = -0.5;  // Match master initial state
        
        // Trạng thái bộ tích phân sai số (ie)
        this.iex = 0.0; this.iey = 0.0; this.iez = 0.0;

        // Cấu hình tham số mặt trượt từ mô phỏng
        this.lambda = 400.0;

        // Tăng gain chuyển mạch để đạt đồng bộ nhanh (yêu cầu convergence trong 100ms)
        this.k_gain_x = 0.5;  // Tăng từ 0.1
        this.k_gain_y = 0.75; // Tăng từ 0.15
        this.k_gain_z = 0.5;  // Tăng từ 0.1
    }

    init() {
        // Reset về trạng thái khởi tạo ban đầu (khớp với Master trên ESP32)
        this.xs = 0.15; this.ys = 0.25; this.zs = -0.5;
        this.iex = 0.0; this.iey = 0.0; this.iez = 0.0;
        console.log('[ChenReceiver] State reset to initial values');
    }

    // Hàm bão hòa mượt (sat) thay thế hàm sign để chống chattering (răng cưa) trên Dashboard
    sat(err, epsilon = 0.05) {
        return Math.max(-1, Math.min(1, err / epsilon));
    }

    // Hàm động học T-S Fuzzy của Slave (giống hệt Master trên ESP32)
    // Trả về đạo hàm (f) của trạng thái slave tại điểm (x, y, z)
    fTS_Slave(x, y, z) {
        const omega_1 = (this.M - x) / (2.0 * this.M);
        const omega_2 = (this.M + x) / (2.0 * this.M);

        // Quy tắc A1
        const a1x = -this.a * x + this.a * y;
        const a1y = -7.0 * x + this.c * y + this.M * z;  // teta = 7
        const a1z = -this.M * y - this.b * z;

        // Quy tắc A2
        const a2x = -this.a * x + this.a * y;
        const a2y = -7.0 * x + this.c * y - this.M * z;
        const a2z =  this.M * y - this.b * z;

        // Kết hợp T-S Fuzzy (cộng thêm trạng thái hiện tại như mô phỏng MATLAB)
        return {
            fsx: omega_1 * a1x + omega_2 * a2x + x,
            fsy: omega_1 * a1y + omega_2 * a2y + y,
            fsz: omega_1 * a1z + omega_2 * a2z + z
        };
    }

    // Đồng bộ hóa tích phân số 100 bước để khóa pha trạng thái
    syncWithMaster(receivedMaster) {
        for (let i = 0; i < 100; i++) {
            // 1. Tính sai số trực tiếp (e = xm - xs)
            const eX = receivedMaster.x - this.xs;
            const eY = receivedMaster.y - this.ys;
            const eZ = receivedMaster.z - this.zs;

            // 2. Tính bề mặt trượt: s = e + Lambda * ie
            const sX = eX + this.lambda * this.iex;
            const sY = eY + this.lambda * this.iey;
            const sZ = eZ + this.lambda * this.iez;

            // 3. Khôi phục hàm f_master tự động từ dữ liệu mạng nhận được
            const omega_m1 = (this.M - receivedMaster.x) / (2.0 * this.M);
            const omega_m2 = (this.M + receivedMaster.x) / (2.0 * this.M);
            
            const f_master_x = (omega_m1 * (-this.a * receivedMaster.x + this.a * receivedMaster.y) + omega_m2 * (-this.a * receivedMaster.x + this.a * receivedMaster.y)) + receivedMaster.x;
            const f_master_y = (omega_m1 * (-7.0 * receivedMaster.x + this.c * receivedMaster.y + this.M * receivedMaster.z) + omega_m2 * (-7.0 * receivedMaster.x + this.c * receivedMaster.y - this.M * receivedMaster.z)) + receivedMaster.y;
            const f_master_z = (omega_m1 * (-this.M * receivedMaster.y - this.b * receivedMaster.z) + omega_m2 * (this.M * receivedMaster.y - this.b * receivedMaster.z)) + receivedMaster.z;

            // 4. Lấy f_slave hiện tại của bộ thu
            const { fsx, fsy, fsz } = this.fTS_Slave(this.xs, this.ys, this.zs);

            // 5. Luật điều khiển tương đương: u_eq = f_master - f_slave + Lambda * e
            const u_eq_x = f_master_x - fsx + this.lambda * eX;
            const u_eq_y = f_master_y - fsy + this.lambda * eY;
            const u_eq_z = f_master_z - fsz + this.lambda * eZ;

            // 6. Luật điều khiển chuyển mạch (SMC) sử dụng hàm sat() chống nhiễu số
            const u_sw_x = this.k_gain_x * this.sat(sX);
            const u_sw_y = this.k_gain_y * this.sat(sY);
            const u_sw_z = this.k_gain_z * this.sat(sZ);

            // 7. Tổng hợp luật điều khiển (BỎ THÀNH PHẦN -d_hat)
            const uX = u_eq_x + u_sw_x;
            const uY = u_eq_y + u_sw_y;
            const uZ = u_eq_z + u_sw_z;

            // 8. Tính đạo hàm và tích phân cập nhật trạng thái Slave
            const dxs = fsx + uX;
            const dys = fsy + uY;
            const dzs = fsz + uZ;

            this.xs += dxs * this.dt;
            this.ys += dys * this.dt;
            this.zs += dzs * this.dt;

            // 9. Cập nhật thành phần tích phân sai số (die = e)
            this.iex += eX * this.dt;
            this.iey += eY * this.dt;
            this.iez += eZ * this.dt;
        }

        return {
            errX: receivedMaster.x - this.xs,
            errY: receivedMaster.y - this.ys,
            errZ: receivedMaster.z - this.zs
        };
    }
}

// Middleware: Parse JSON requests
app.use(express.json());
app.use(express.urlencoded({ extended: true }));

// Serve static files from public directory
app.use(express.static(path.join(__dirname, 'public')));

// ==========================================================
// WEBSOCKET SERVER (để nhận từ agv_websocket.ino)
// ==========================================================
const wss = new WebSocket.Server({ server });

const receiver = new ChenReceiver();

wss.on('connection', (ws) => {
    console.log('[WebSocket] AGV device connected');
    receiver.init(); // Reset slave state on connect
    
    ws.on('message', (data) => {
        try {
            const payload = JSON.parse(data);
            
            if (payload.type === 'agv_telemetry') {
                // Extract Master chaos state (gửi từ AGV)
                const receivedMaster = {
                    x: parseFloat(payload.chaos_x),
                    y: parseFloat(payload.chaos_y),
                    z: parseFloat(payload.chaos_z)
                };

                // Extract masked physical data
                const masked = {
                    vL: parseFloat(payload.tx_vL),
                    vR: parseFloat(payload.tx_vR),
                    state: parseFloat(payload.tx_state),
                    x: payload.tx_x !== undefined ? parseFloat(payload.tx_x) : 0,
                    y: payload.tx_y !== undefined ? parseFloat(payload.tx_y) : 0,
                    theta: payload.tx_theta !== undefined ? parseFloat(payload.tx_theta) : 0
                };

                // T-S SMC Synchronization với bề mặt trượt (100 bước)
                const syncResult = receiver.syncWithMaster(receivedMaster);
                const syncError = Math.sqrt(syncResult.errX*syncResult.errX + syncResult.errY*syncResult.errY + syncResult.errZ*syncResult.errZ);
                
                // Lấy chaos state đã đồng bộ từ receiver
                const localChaosSum = receiver.xs + receiver.ys + receiver.zs;

                // DECODE: Giải mã bằng cách trừ đi tín hiệu hỗn loạn đã đồng bộ
                const dec_vL = masked.vL - localChaosSum;
                const dec_vR = masked.vR - localChaosSum;
                const dec_state = masked.state - localChaosSum;
                const dec_x = masked.x - (payload.tx_x !== undefined ? localChaosSum : 0);
                const dec_y = masked.y - (payload.tx_y !== undefined ? localChaosSum : 0);
                const dec_theta = masked.theta - (payload.tx_theta !== undefined ? localChaosSum : 0);

                // Clamp decoded values về dải hợp lệ để tránh validation reject khi chaos chưa đồng bộ hoàn toàn
                const raw_state = Math.round(dec_state);
                const safe_state = Math.max(0, Math.min(2, raw_state)); // Clamp [0,2]
                const safe_vL = Math.max(-255, Math.min(255, Math.round(dec_vL))); // Clamp [-255,255]
                const safe_vR = Math.max(-255, Math.min(255, Math.round(dec_vR))); // Clamp [-255,255]

                const decoded_data = {
                    type: 'speed',
                    v_left: safe_vL,
                    v_right: safe_vR,
                    state: safe_state,
                    x: dec_x,
                    y: dec_y,
                    theta: dec_theta,
                    timestamp: Date.now(),
                    source: 'agv_websocket',
                    sync_error: syncError,
                    slave_state: { xs: receiver.xs, ys: receiver.ys, zs: receiver.zs }
                };

                console.log(`[AGV] syncErr=${syncError.toFixed(4)}, vL=${decoded_data.v_left}, vR=${decoded_data.v_right}, state=${decoded_data.state}(raw=${raw_state}), x=${decoded_data.x.toFixed(1)}, y=${decoded_data.y.toFixed(1)} (T-S SMC)`);

                // Broadcast to dashboard
                io.emit('agv_data', decoded_data);
            }
        } catch (err) {
            console.warn('[WebSocket Parse Error]', err.message);
        }
    });

    ws.on('error', (err) => {
        console.warn('[WebSocket Connection Error]', err.message);
        // Don't crash on WebSocket errors, just log and continue
    });

    ws.on('close', () => {
        console.log('[WebSocket] AGV device disconnected');
    });
});

// Handle WebSocket server errors
wss.on('error', (err) => {
    console.warn('[WebSocket Server Error]', err.message);
});

const COMPORT = process.env.AGV_SERIAL_PORT || 'COM3';  // Windows default COM3
const BAUDRATE = parseInt(process.env.AGV_BAUD_RATE) || 115200;

console.log(`[INFO] Attempting to connect to AGV on ${COMPORT} at ${BAUDRATE} baud...`);

let port;
let parser;

try {
    port = new SerialPort({ path: COMPORT, baudRate: BAUDRATE });
    parser = port.pipe(new ReadlineParser({ delimiter: '\r\n' }));

    port.on('open', () => {
        console.log(`[SUCCESS] Connected to serial port ${COMPORT}`);
    });

    port.on('error', (err) => {
        console.error(`[WARN] Serial Port Error (optional - WebSocket is primary):`, err.message);
    });

    parser.on('data', (data) => {
        try {
            // Trim any garbage and parse JSON
            const dataStr = data.trim();
            if(!dataStr || dataStr.length === 0) return;
            
            // Only process lines that look like JSON
            if(dataStr.startsWith('{') && dataStr.endsWith('}')) {
                const parsedData = JSON.parse(dataStr);
                io.emit('agv_data', parsedData);
            } else {
                // If it's not JSON, still emit as Raw logs
                io.emit('agv_data', { type: 'log', msg: dataStr });
            }
        } catch (err) {
            // Ignore parsing errors for general debug prints
            io.emit('agv_data', { type: 'log', msg: `[Parse Error]: ${data.trim()}` });
        }
    });

} catch (error) {
    console.log(`[WARN] Serial Port disabled (${error.message}). Using WebSocket only.`);
}

// ==========================================================
// DATA VALIDATION & TRANSFORMATION
// ==========================================================
function validateSpeedData(data) {
    const errors = [];
    
    // Validate v_left
    if (data.v_left === undefined || data.v_left === null) {
        errors.push('v_left is missing');
    } else if (!Number.isInteger(data.v_left)) {
        errors.push('v_left must be an integer');
    } else if (data.v_left < -255 || data.v_left > 255) {
        errors.push(`v_left out of range (-255 to 255): ${data.v_left}`);
    }
    
    // Validate v_right
    if (data.v_right === undefined || data.v_right === null) {
        errors.push('v_right is missing');
    } else if (!Number.isInteger(data.v_right)) {
        errors.push('v_right must be an integer');
    } else if (data.v_right < -255 || data.v_right > 255) {
        errors.push(`v_right out of range (-255 to 255): ${data.v_right}`);
    }
    
    // Validate state
    if (data.state === undefined || data.state === null) {
        errors.push('state is missing');
    } else if (!Number.isInteger(data.state)) {
        errors.push('state must be an integer');
    } else if (data.state < 0 || data.state > 2) {
        errors.push(`state out of range (0-2): ${data.state}`);
    }
    
    return { valid: errors.length === 0, errors };
}

// ==========================================================
// API ENDPOINT: Nhận lệnh điều khiển từ Dashboard
// ==========================================================
app.post('/api/command', (req, res) => {
    try {
        const { action, value } = req.body;

        // Tạo lệnh mã hóa
        const cmd_enc = {
            action: action,
            value: value || 0,
            timestamp: Date.now()
        };

        // Gửi lệnh qua WebSocket tới AGV (tất cả clients)
        wss.clients.forEach((client) => {
            if (client.readyState === WebSocket.OPEN) {
                client.send(JSON.stringify(cmd_enc));
            }
        });

        // Gửi lệnh qua Socket.IO tới Dashboard
        io.emit('command', cmd_enc);

        console.log(`[COMMAND] Action: ${action}, Value: ${value}`);
        res.json({ success: true, message: 'Command sent' });
    } catch (err) {
        console.error('[ERROR] Command API:', err.message);
        res.status(500).json({ success: false, error: err.message });
    }
});

// ==========================================================
// API ENDPOINT: Nhận dữ liệu từ ESP32 WiFi Receiver (HTTP)
// ==========================================================
app.post('/api/speed', (req, res) => {
    try {
        const data = req.body;
        
        // VALIDATE
        const validation = validateSpeedData(data);
        if (!validation.valid) {
            console.error('[VALIDATION FAILED]', validation.errors);
            return res.status(400).json({ 
                success: false, 
                error: 'Invalid data structure',
                details: validation.errors 
            });
        }
        
        // SANITIZE & PREPARE
        const cleanData = {
            type: 'speed',
            v_left: parseInt(data.v_left),
            v_right: parseInt(data.v_right),
            state: parseInt(data.state),
            timestamp: data.timestamp || Date.now(),
            source: 'esp32_http',
            receivedAt: new Date().toISOString()
        };
        
        console.log(`[✓ DATA VALID] v_left=${cleanData.v_left}, v_right=${cleanData.v_right}, state=${cleanData.state}`);
        
        // EMIT TO ALL CONNECTED CLIENTS
        io.emit('agv_data', cleanData);
        
        // RESPOND SUCCESS
        res.json({ 
            success: true, 
            message: 'Data received and broadcasted',
            clientsReceived: io.engine.clientsCount
        });
        
    } catch (err) {
        console.error('[ERROR] API Error:', err.message);
        res.status(500).json({ 
            success: false, 
            error: 'Internal server error',
            details: err.message 
        });
    }
});

// ==========================================================
// SOCKET.IO: WebSocket Connections (Dashboard)
// ==========================================================
io.on('connection', (socket) => {
    console.log(`[Socket.IO] Client connected: ${socket.id}`);
    
    socket.on('disconnect', () => {
        console.log(`[Socket.IO] Client disconnected: ${socket.id}`);
    });

    // Nhận dữ liệu từ ESP32 qua WebSockets và gửi cho Dashboard
    socket.on('agv_data', (data) => {
        if (!data.timestamp) data.timestamp = Date.now();
        if (!data.source) data.source = 'esp32_websocket';
        
        socket.broadcast.emit('agv_data', data);
    });

    // Nhận lệnh điều khiển từ Dashboard và gửi cho AGV/ESP32
    socket.on('command', (cmdData) => {
        console.log(`[Command from Dashboard] Action: ${cmdData.action}, Value: ${cmdData.value}`);
        
        // Gửi lệnh qua WebSocket tới tất cả AGV clients
        wss.clients.forEach((client) => {
            if (client.readyState === WebSocket.OPEN) {
                client.send(JSON.stringify(cmdData));
            }
        });

        // Broadcast lệnh tới các dashboard clients khác
        socket.broadcast.emit('command', cmdData);
    });
});

const WEB_PORT = process.env.PORT || 3000;
server.listen(WEB_PORT, '0.0.0.0', () => {
    console.log(`[INFO] ========================================`);
    console.log(`[INFO] Dashboard server running on port ${WEB_PORT}`);
    console.log(`[INFO] ========================================`);
    console.log(`[INFO] Dashboard: http://localhost:${WEB_PORT}`);
    console.log(`[INFO] API endpoints:`);
    console.log(`[INFO]   POST /api/speed   - Receive telemetry (HTTP)`);
    console.log(`[INFO]   POST /api/command - Send command`);
    console.log(`[INFO] WebSocket: ws://localhost:${WEB_PORT} (for AGV)`);
    console.log(`[INFO] Socket.IO: http://localhost:${WEB_PORT} (for Dashboard)`);
    console.log(`[INFO] ========================================`);
});

// Global error handler
process.on('uncaughtException', (err) => {
    console.error('[FATAL] Uncaught Exception:', err.message);
    if (err.message.includes('WebSocket frame')) {
        console.warn('[INFO] WebSocket frame error - client disconnected, continuing...');
    } else {
        console.error('[STACK]', err.stack);
    }
});

process.on('unhandledRejection', (reason, promise) => {
    console.error('[FATAL] Unhandled Rejection:', reason);
});
