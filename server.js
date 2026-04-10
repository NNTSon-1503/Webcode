require('dotenv').config();
const express = require('express');
const http = require('http');
const { Server } = require('socket.io');
const { SerialPort } = require('serialport');
const { ReadlineParser } = require('@serialport/parser-readline');
const path = require('path');

const app = express();
const server = http.createServer(app);
const io = new Server(server);

// Serve static files from public directory
app.use(express.static(path.join(__dirname, 'public')));

const COMPORT = process.env.AGV_SERIAL_PORT || '/dev/ttyUSB0';
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
        console.error(`[ERROR] Serial Port Error:`, err.message);
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
    console.error(`[CRITICAL ERROR] Could not initialize SerialPort:`, error.message);
}

io.on('connection', (socket) => {
    console.log('[INFO] Dashboard client connected');
    socket.on('disconnect', () => {
        console.log('[INFO] Dashboard client disconnected');
    });
});

const WEB_PORT = process.env.PORT || 3000;
server.listen(WEB_PORT, () => {
    console.log(`[INFO] Dashboard server running at http://localhost:${WEB_PORT}`);
});
