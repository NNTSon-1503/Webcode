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
    if (data.type === 'telemetry') {
        updateTelemetry(data);
    } else if (data.type === 'rfid') {
        updateRFID(data.uid);
        addLog(`RFID Scanned: ${data.uid}`, '#06b6d4');
    } else if (data.type === 'action') {
        addLog(`Action Command: ${data.msg}`, '#f59e0b');
    } else if (data.type === 'log') {
        addLog(data.msg, '#a3be8c');
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

function updateMotor(actEl, tgtEl, ringEl, actVal, tgtVal) {
    actEl.innerText = actVal;
    tgtEl.innerText = tgtVal;

    // Convert to percentage for conic gradient (Max speed is ~255)
    let percent = Math.min(Math.max((Math.abs(actVal) / 255) * 100, 0), 100);
    
    // Choose color based on speed and direction
    let color = 'var(--acc-blue)';
    if(actVal < 0) color = 'var(--acc-red)'; // Going in reverse (?) or just visual diff

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
