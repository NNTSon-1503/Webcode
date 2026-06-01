const socket = io();

// DOM Elements
const connDot = document.getElementById('connection-dot');
const connText = document.getElementById('connection-text');

const stateDisplayCard = document.getElementById('agv-state-display');
const stateText = stateDisplayCard.querySelector('h2');
const stateDesc = stateDisplayCard.querySelector('p');

const logList = document.getElementById('log-list');
const terminalContainer = document.getElementById('terminal-container');

const actLeft = document.getElementById('act-left');
const tgtLeft = document.getElementById('tgt-left');
const leftRing = document.querySelector('.left-ring');

const actRight = document.getElementById('act-right');
const tgtRight = document.getElementById('tgt-right');
const rightRing = document.querySelector('.right-ring');

const rfidUid = document.getElementById('last-rfid');
const rfidIcon = document.querySelector('.rfid-icon');

const btnStop = document.getElementById('btn-stop');
const btnEstop = document.getElementById('btn-estop');
const btnStart = document.getElementById('btn-start');
const agvMarker = document.getElementById('agv-marker');

// Socket Connection handling
socket.on('connect', () => {
    connDot.className = 'status-dot connected';
    connText.innerText = 'Connected';
    addLog('[System] Connected to Server', '#10b981');
});

socket.on('disconnect', () => {
    connDot.className = 'status-dot disconnected';
    connText.innerText = 'Disconnected';
    addLog('[System] Disconnected from Server', '#ef4444');
    updateState(-1); // Unknown
});

// Incoming Data Handling
socket.on('agv_data', (data) => {
    if (!data || !data.type) {
        console.error('[ERROR] Invalid data received:', data);
        addLog('[ERROR] Received invalid data structure', '#ef4444');
        return;
    }

    try {
        if (data.type === 'telemetry') {
            updateTelemetry(data);
        } else if (data.type === 'speed') {
            // Handle ESP32 speed data (WiFi Receiver)
            updateSpeedData(data);
        } else if (data.type === 'heartbeat') {
            // Silently ignore heartbeat (no log spam)
            console.log('[HEARTBEAT] Data update signal received');
        } else if (data.type === 'rfid') {
            updateRFID(data.uid);
            addLog(`RFID Scanned: ${data.uid}`, '#06b6d4');
            // Xóa vệt đường đi (trail) nếu reset về trạm gốc
            if (data.uid === MAP_CONFIG.stations[0].id || data.uid === "98B1F3E3") {
                MAP_CONFIG.trail = [];
                addLog(`[MAP] Resetting Trail`, '#f59e0b');
                drawMap(); // Vẽ lại bản đồ ngay lập tức
            }
        } else if (data.type === 'action') {
            addLog(`Action Command: ${data.msg}`, '#f59e0b');
        } else if (data.type === 'log') {
            addLog(data.msg, '#a3be8c');
        } else {
            console.warn('[WARN] Unknown data type:', data.type);
        }
    } catch (error) {
        console.error('[ERROR] Error processing agv_data:', error);
        addLog(`[ERROR] Data processing failed: ${error.message}`, '#ef4444');
    }
});

function updateTelemetry(data) {
    // Update State
    updateState(data.state);

    // Update Motors
    // Max PWM is 255. Calculate percentage for the ring.
    updateMotor(actLeft, tgtLeft, leftRing, data.actual_L, data.target_L);
    updateMotor(actRight, tgtRight, rightRing, data.actual_R, data.target_R);
}

// Handle ESP32 Speed Data (WiFi Receiver) - WITH VALIDATION
function updateSpeedData(data) {
    console.log('[SPEED_DATA] Received:', data);

    // VALIDATION 1: Check required fields
    const hasValidData = (data.v_left !== undefined && data.v_right !== undefined && data.state !== undefined);
    if (!hasValidData) {
        console.error('[SPEED_DATA] Missing required fields:', { v_left: data.v_left, v_right: data.v_right, state: data.state });
        addLog('[ERROR] Speed data incomplete', '#ef4444');
        return;
    }

    // VALIDATION 2: Check data types (should be numbers)
    if (typeof data.v_left !== 'number' || typeof data.v_right !== 'number' || typeof data.state !== 'number') {
        console.error('[SPEED_DATA] Invalid data types:', typeof data.v_left, typeof data.v_right, typeof data.state);
        addLog('[ERROR] Invalid data types received', '#ef4444');
        return;
    }

    // VALIDATION 3: Check value ranges
    if (data.v_left < -255 || data.v_left > 255 || data.v_right < -255 || data.v_right > 255) {
        console.error('[SPEED_DATA] Value out of range:', { v_left: data.v_left, v_right: data.v_right });
        addLog('[ERROR] Speed value out of range(-255 to 255)', '#ef4444');
        return;
    }

    if (data.state < -1 || data.state > 3) {
        console.warn('[SPEED_DATA] State out of range, clamping:', data.state);
        data.state = Math.max(0, Math.min(2, data.state)); // Clamp thay vì reject
    }

    console.log(`[✓ SPEED_DATA] L=${data.v_left}, R=${data.v_right}, State=${data.state}${data.sync_error ? ' syncErr=' + data.sync_error.toFixed(3) : ''}`);

    // Update State
    updateState(data.state);

    // Update Motors using speed data
    updateMotor(actLeft, tgtLeft, leftRing, data.v_left, data.v_left);
    updateMotor(actRight, tgtRight, rightRing, data.v_right, data.v_right);

    // Update Map Odometry
    if (data.x !== undefined && data.y !== undefined && data.theta !== undefined) {
        const { scale, offsetX, offsetY } = MAP_CONFIG;

        // Hệ Oxy bản đồ: Trạm gốc nằm ở (250, 0)
        // Dữ liệu từ xe (data.x, data.y) ĐÃ LÀ toạ độ toàn cầu (Oxy) khớp với bản đồ (do Firmware đã cập nhật)
        // Không cần cộng thêm offset nữa.
        const mapX = data.x;
        const mapY = data.y;

        const screenX = offsetX + mapX * scale;
        const screenY = offsetY - mapY * scale;

        // CSS rotate() tính theo chiều kim đồng hồ, Theta toán học CCW nên cần dùng -theta
        agvMarker.style.transform = `translate(calc(-50% + ${screenX}px), calc(-50% + ${screenY}px)) rotate(${-data.theta}rad)`;

        // Lưu Vệt kéo dài
        MAP_CONFIG.trail.push({ x: mapX, y: mapY });
        if (MAP_CONFIG.trail.length > 200) {
            MAP_CONFIG.trail.shift(); // Giữ lại 200 điểm gần nhất
        }

        drawMap();
    }

    // Log update
    addLog(`[ESP32] L=${data.v_left}, R=${data.v_right}, X=${data.x}, Y=${data.y}`, '#10b981');

    // Optional: show data source and timestamp
    if (data.source === 'esp32_wifi') {
        console.log(`[SOURCE] WiFi | Received at: ${data.receivedAt}`);
    }
}

function updateMotor(actEl, tgtEl, ringEl, actVal, tgtVal) {
    actEl.innerText = actVal;
    tgtEl.innerText = tgtVal;

    // Convert to percentage for conic gradient (Max speed is ~255)
    let percent = Math.min(Math.max((Math.abs(actVal) / 255) * 100, 0), 100);

    // Choose color based on speed and direction
    let color = 'var(--acc-blue)';
    if (actVal < 0) color = 'var(--acc-red)'; // Going in reverse (?) or just visual diff

    ringEl.style.background = `conic-gradient(${color} ${percent}%, transparent 0%)`;
}

function updateState(state) {
    stateDisplayCard.className = 'state-display'; // Reset
    if (state === 0) {
        stateDisplayCard.classList.add('running');
        stateText.innerText = 'RUNNING';
        stateText.innerHTML = '<i class="fa-solid fa-play"></i> RUNNING';
        stateDesc.innerText = 'Line following mode active';
    } else if (state === 1) {
        stateDisplayCard.classList.add('paused');
        stateText.innerHTML = '<i class="fa-solid fa-pause"></i> PAUSED';
        stateDesc.innerText = 'Stopped temporarily (5 seconds)';
    } else if (state === 2) {
        stateDisplayCard.classList.add('stopped');
        stateText.innerHTML = '<i class="fa-solid fa-stop"></i> STOPPED';
        stateDesc.innerText = 'Halted completely by RFID';
    } else {
        stateText.innerText = 'WAITING';
        stateDesc.innerText = 'Awaiting telemetry format...';
    }
}

function updateRFID(uid) {
    rfidUid.innerText = uid;
    // Animate icon pop
    rfidIcon.style.transform = 'scale(1.2)';
    rfidIcon.style.boxShadow = '0 0 30px rgba(6, 182, 212, 0.8)';
    setTimeout(() => {
        rfidIcon.style.transform = 'scale(1)';
        rfidIcon.style.boxShadow = '0 0 15px rgba(6, 182, 212, 0.3)';
    }, 300);
}

function addLog(msg, color = '#a3be8c') {
    const li = document.createElement('li');

    const timeSpan = document.createElement('span');
    timeSpan.className = 'timestamp';
    const now = new Date();
    timeSpan.innerText = `[${now.getHours().toString().padStart(2, '0')}:${now.getMinutes().toString().padStart(2, '0')}:${now.getSeconds().toString().padStart(2, '0')}]`;

    const msgSpan = document.createElement('span');
    msgSpan.style.color = color;
    msgSpan.innerText = msg;

    li.appendChild(timeSpan);
    li.appendChild(msgSpan);

    logList.appendChild(li);

    // Keep only last 50 logs
    if (logList.children.length > 50) {
        logList.removeChild(logList.firstChild);
    }

    // Auto scroll to bottom
    terminalContainer.scrollTop = terminalContainer.scrollHeight;
}

// Control Buttons Event Listeners
btnStop.addEventListener('click', () => {
    sendCommand('stop', 0);
});

btnEstop.addEventListener('click', () => {
    sendCommand('estop', 0);
});

btnStart.addEventListener('click', () => {
    sendCommand('resume', 0);
});

// Hàm gửi lệnh điều khiển tới Server
async function sendCommand(action, value) {
    try {
        const response = await fetch('/api/command', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({
                action: action,
                value: value
            })
        });

        const result = await response.json();
        if (result.success) {
            console.log(`[Command Sent] ${action}=${value}`);
            addLog(`[Command] ${action.toUpperCase()} sent to AGV`, '#10b981');
        } else {
            addLog(`[ERROR] Command failed: ${result.error}`, '#ef4444');
        }
    } catch (err) {
        console.error('[ERROR] Command send failed:', err);
        addLog(`[ERROR] Could not send command: ${err.message}`, '#ef4444');
    }
}

// Nhận lệnh từ server (qua Socket.IO)
socket.on('command', (cmdData) => {
    console.log('[Dashboard] Command received from server:', cmdData);
    addLog(`[Cmd Broadcast] ${cmdData.action}`, '#f59e0b');
});

// ==========================================
// MAP & CANVAS LOGIC
// ==========================================
const mapCanvas = document.getElementById('map-canvas');
const ctx = mapCanvas ? mapCanvas.getContext('2d') : null;
const mapContainer = document.getElementById('map-container');

// Cấu trúc dữ liệu thẻ RFID
class RFIDTag {
    constructor(id, posX, posY) {
        this.id = id;
        this.posX = posX;
        this.posY = posY;
    }
}

// Trạm gốc nằm trên đường line (tại điểm bắt đầu của đoạn thẳng dưới)
// Khoảng cách từ lề trái là 250mm, lề dưới là 0mm
const baseStationX = 250;
const baseStationY = 0;
const rfidBaseStation = new RFIDTag("98B1F3E3", baseStationX, baseStationY);

// In kết quả ra màn hình theo yêu cầu
console.log(`[RFID] Thiết lập trạm gốc hệ Oxy bản đồ: ID=${rfidBaseStation.id}, X=${rfidBaseStation.posX}, Y=${rfidBaseStation.posY}`);
setTimeout(() => {
    addLog(`[System] Map Oxy System Initialized. Base Station: X=${rfidBaseStation.posX}, Y=${rfidBaseStation.posY}`, '#0ea5e9');
}, 1000);

const MAP_CONFIG = {
    scale: 0.18,       // Tỉ lệ thu nhỏ để vừa với màn hình (5500mm ~ 990px)
    offsetX: 0,
    offsetY: 0,
    trackLength: 5000, // Chiều dài 2 đoạn thẳng = 5000mm (500cm)
    trackRadius: 250,  // Bán kính 2 vòng cung = 250mm (25cm)
    stations: [
        { id: rfidBaseStation.id, name: "Trạm Gốc (Start)", x: rfidBaseStation.posX, y: rfidBaseStation.posY }
    ],
    trail: []
};

function resizeCanvas() {
    if (!mapCanvas || !mapContainer) return;
    mapCanvas.width = mapContainer.clientWidth;
    mapCanvas.height = mapContainer.clientHeight;

    // Đặt trục Oxy cố định ở góc trái dưới (padding 50px)
    const padding = 50;
    MAP_CONFIG.offsetX = padding;           // X=0 ở x=50px từ trái
    MAP_CONFIG.offsetY = mapCanvas.height - padding;  // Y=0 ở y=50px từ dưới

    // Tính toán scale tự động để vừa khung hình (Responsive)
    const totalMapWidthMm = MAP_CONFIG.trackLength + 2 * MAP_CONFIG.trackRadius; // 5000 + 500 = 5500mm
    const availableWidth = mapCanvas.width - (padding * 2);
    if (availableWidth > 0) {
        MAP_CONFIG.scale = availableWidth / totalMapWidthMm;
    }

    // Đảm bảo scale không quá lớn làm bản đồ vượt quá chiều cao
    const totalMapHeightMm = 2 * MAP_CONFIG.trackRadius + 200; // Chiều cao thực tế + một khoảng dư
    const availableHeight = mapCanvas.height - (padding * 2);
    if (availableHeight > 0) {
        const heightScale = availableHeight / totalMapHeightMm;
        if (MAP_CONFIG.scale > heightScale) {
            MAP_CONFIG.scale = heightScale; 
        }
    }

    drawMap();
}

if (mapCanvas) {
    window.addEventListener('resize', resizeCanvas);
    setTimeout(resizeCanvas, 100);
}

function drawMap() {
    if (!ctx) return;
    ctx.clearRect(0, 0, mapCanvas.width, mapCanvas.height);

    // 1. Vẽ Lưới (Grid)
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.05)';
    ctx.lineWidth = 1;
    for (let i = 0; i < mapCanvas.width; i += 20) {
        ctx.beginPath(); ctx.moveTo(i, 0); ctx.lineTo(i, mapCanvas.height); ctx.stroke();
    }
    for (let i = 0; i < mapCanvas.height; i += 20) {
        ctx.beginPath(); ctx.moveTo(0, i); ctx.lineTo(mapCanvas.width, i); ctx.stroke();
    }

    const { scale, offsetX, offsetY, trackLength, trackRadius } = MAP_CONFIG;
    const rPx = trackRadius * scale;
    const lPx = trackLength * scale;

    // Vẽ Trục Oxy cố định ở góc trái dưới
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.6)';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(offsetX, offsetY);
    ctx.lineTo(offsetX + 200, offsetY);
    ctx.moveTo(offsetX, offsetY);
    ctx.lineTo(offsetX, offsetY - 200);
    ctx.stroke();
    ctx.fillStyle = 'white';
    ctx.font = '12px Arial';
    ctx.fillText('X', offsetX + 205, offsetY + 4);
    ctx.fillText('Y', offsetX - 12, offsetY - 205);

    // 2. Vẽ Đường Vạch Kẻ Dán Sàn (Oval/Stadium)
    // Tâm vòng tròn trái trong hệ tọa độ mới (Oxy ở góc dưới trái)
    const cxLeft = offsetX + rPx;
    const cyLeft = offsetY - rPx;
    // Tâm vòng tròn phải
    const cxRight = cxLeft + lPx;
    const cyRight = cyLeft;

    ctx.beginPath();
    // Mép trên của vòng cung trái (X=R, Y=2R)
    ctx.moveTo(cxLeft, cyLeft - rPx);
    // Đoạn thẳng mép trên đến phải (X=L+R, Y=2R)
    ctx.lineTo(cxRight, cyLeft - rPx);
    // Nửa vòng tròn phải tâm (cxRight, cyRight)
    ctx.arc(cxRight, cyRight, rPx, -Math.PI / 2, Math.PI / 2);
    // Đoạn thẳng mép dưới về trái (X=R, Y=0)
    ctx.lineTo(cxLeft, cyRight + rPx);
    // Nửa vòng tròn trái tâm (cxLeft, cyLeft)
    ctx.arc(cxLeft, cyLeft, rPx, Math.PI / 2, -Math.PI / 2);

    ctx.strokeStyle = 'rgba(255, 255, 255, 0.3)';
    ctx.lineWidth = 4;
    ctx.setLineDash([15, 10]); // Nét đứt mô phỏng băng dính vạch kẻ
    ctx.stroke();
    ctx.setLineDash([]);

    // 3. Vẽ Trạm RFID
    MAP_CONFIG.stations.forEach(st => {
        const sx = offsetX + st.x * scale;
        const sy = offsetY - st.y * scale;

        ctx.beginPath();
        ctx.arc(sx, sy, 8, 0, Math.PI * 2);
        ctx.fillStyle = 'rgba(6, 182, 212, 0.2)';
        ctx.fill();
        ctx.strokeStyle = 'rgba(6, 182, 212, 0.8)';
        ctx.lineWidth = 2;
        ctx.stroke();

        ctx.fillStyle = '#94a3b8';
        ctx.font = '10px Inter';
        ctx.fillText(st.name, sx - 20, sy + 25);
    });

    // 4. Vẽ Vệt kéo dài (Trail)
    if (MAP_CONFIG.trail.length > 1) {
        ctx.beginPath();
        const first = MAP_CONFIG.trail[0];
        ctx.moveTo(offsetX + first.x * scale, offsetY - first.y * scale);
        for (let i = 1; i < MAP_CONFIG.trail.length; i++) {
            const p = MAP_CONFIG.trail[i];
            ctx.lineTo(offsetX + p.x * scale, offsetY - p.y * scale);
        }
        ctx.strokeStyle = 'rgba(16, 185, 129, 0.6)'; // Vệt màu xanh lá
        ctx.lineWidth = 2;
        ctx.stroke();
    }
}
