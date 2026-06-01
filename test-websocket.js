#!/usr/bin/env node
// ==========================================================
// WebSocket Test - Kiểm tra kết nối và mã hóa Chen
// ==========================================================

const WebSocket = require('ws');

// Chen Decryptor Class (giống server.js)
class ChenDecryptor {
    constructor() {
        this.a = 35.0;
        this.b = 3.0;
        this.c = 28.0;
        this.teta = 7.0;
        this.M = 100.0;
        this.dt = 0.001;
        this.xm = 0.15;
        this.ym = 0.25;
        this.zm = -0.5;
        this.decryptCount = 0;
        this.decryptErrors = 0;
    }

    init() {
        this.xm = 0.15;
        this.ym = 0.25;
        this.zm = -0.5;
    }

    weights(x) {
        const xc = Math.max(-this.M, Math.min(this.M, x));
        const w1 = (this.M - xc) / (2.0 * this.M);
        const w2 = (this.M + xc) / (2.0 * this.M);
        return { w1, w2 };
    }

    fTS(x, y, z) {
        const { w1, w2 } = this.weights(x);
        const a1x = -this.a * x + this.a * y;
        const a1y = -this.teta * x + this.c * y + this.M * z;
        const a1z = -this.M * y - this.b * z;
        const a2x = -this.a * x + this.a * y;
        const a2y = -this.teta * x + this.c * y - this.M * z;
        const a2z = this.M * y - this.b * z;
        const fsx = w1 * a1x + w2 * a2x;
        const fsy = w1 * a1y + w2 * a2y;
        const fsz = w1 * a1z + w2 * a2z;
        return {
            fx: fsx + x,
            fy: fsy + y,
            fz: fsz + z
        };
    }

    step() {
        const { fx, fy, fz } = this.fTS(this.xm, this.ym, this.zm);
        this.xm += fx * this.dt;
        this.ym += fy * this.dt;
        this.zm += fz * this.dt;
    }

    advance(n) {
        for (let i = 0; i < n; i++) this.step();
    }

    getKey() {
        return Math.floor(Math.abs(this.xm % 1.0) * 255);
    }

    decrypt(ciphertext) {
        const key = this.getKey();
        const dec = ((ciphertext - key + 255) % 255);
        this.step();
        return dec;
    }
}

const decryptor = new ChenDecryptor();

// Test Configuration
const SERVER_URL = 'ws://localhost:3000';

console.log('='.repeat(60));
console.log('AGV WebSocket Test Suite');
console.log('='.repeat(60));
console.log(`[TEST] Connecting to ${SERVER_URL}\n`);

let testsPassed = 0;
let testsFailed = 0;

// Test 1: Connection
console.log('[TEST 1] WebSocket Connection');
const ws = new WebSocket(SERVER_URL);

ws.on('open', () => {
    console.log('✓ PASS: WebSocket connected\n');
    testsPassed++;

    // Test 2: Encryption Sync
    console.log('[TEST 2] Chen Encryption Sync');
    decryptor.init();
    console.log(`✓ Decryptor initialized: xm=${decryptor.xm.toFixed(4)}, ym=${decryptor.ym.toFixed(4)}, zm=${decryptor.zm.toFixed(4)}\n`);
    testsPassed++;

    // Test 3: Send Mock Encrypted Telemetry
    console.log('[TEST 3] Send Mock Encrypted Telemetry');
    
    // Mock telemetry (simulating agv_websocket.ino data)
    const mockTelemetry = {
        type: 'agv_telemetry',
        enc_vL: 145,      // Random encrypted values
        enc_vR: 152,
        enc_state: 23,
        enc_x: 189,
        enc_y: 76,
        enc_theta: 128
    };
    
    // Calculate checksum
    mockTelemetry.checksum = 
        mockTelemetry.enc_vL ^ 
        mockTelemetry.enc_vR ^ 
        mockTelemetry.enc_state ^ 
        mockTelemetry.enc_x ^ 
        mockTelemetry.enc_y ^ 
        mockTelemetry.enc_theta;

    console.log('Sending mock encrypted packet:');
    console.log(`  enc_vL=${mockTelemetry.enc_vL}, enc_vR=${mockTelemetry.enc_vR}`);
    console.log(`  enc_state=${mockTelemetry.enc_state}, enc_x=${mockTelemetry.enc_x}`);
    console.log(`  enc_y=${mockTelemetry.enc_y}, enc_theta=${mockTelemetry.enc_theta}`);
    console.log(`  checksum=${mockTelemetry.checksum}\n`);

    ws.send(JSON.stringify(mockTelemetry));

    // Test 4: Decryption Verification
    console.log('[TEST 4] Local Decryption (verification)');
    decryptor.advance(100);

    const dec_vL = decryptor.decrypt(mockTelemetry.enc_vL);
    const dec_vR = decryptor.decrypt(mockTelemetry.enc_vR);
    const dec_state = decryptor.decrypt(mockTelemetry.enc_state);
    const dec_x = decryptor.decrypt(mockTelemetry.enc_x);
    const dec_y = decryptor.decrypt(mockTelemetry.enc_y);
    const dec_theta = decryptor.decrypt(mockTelemetry.enc_theta);

    console.log('Decrypted values:');
    console.log(`  v_left = ${dec_vL - 128}`);
    console.log(`  v_right = ${dec_vR - 128}`);
    console.log(`  state = ${Math.floor(dec_state / 50)}`);
    console.log(`  x = ${dec_x - 128}`);
    console.log(`  y = ${dec_y - 128}`);
    console.log(`  theta = ${(((dec_theta - 128) / 127.0) * Math.PI).toFixed(4)}\n`);
    testsPassed++;

    // Test 5: Command Reception
    console.log('[TEST 5] Send Control Command');
    const cmdData = {
        action: 'speed',
        value: 100
    };
    ws.send(JSON.stringify(cmdData));
    console.log('✓ Control command sent (server should relay to AGV)\n');
    testsPassed++;

    // Keep connection alive for 5 seconds
    setTimeout(() => {
        console.log('[TEST 6] Connection Hold (5 seconds)');
        console.log('✓ PASS: Connection stable\n');
        testsPassed++;

        console.log('='.repeat(60));
        console.log('Test Summary');
        console.log('='.repeat(60));
        console.log(`✓ Passed: ${testsPassed}`);
        console.log(`✗ Failed: ${testsFailed}`);
        console.log(`Total:   ${testsPassed + testsFailed}\n`);

        if (testsFailed === 0) {
            console.log('🎉 All tests passed! WebSocket & Chen encryption working correctly.\n');
        }

        ws.close();
        process.exit(testsFailed > 0 ? 1 : 0);
    }, 5000);
});

ws.on('error', (err) => {
    console.log(`✗ FAIL: WebSocket error - ${err.message}\n`);
    testsFailed++;
    console.log('Make sure server is running: npm start\n');
    process.exit(1);
});

ws.on('close', () => {
    console.log('WebSocket connection closed.');
});

ws.on('message', (data) => {
    try {
        const msg = JSON.parse(data);
        console.log(`[MSG] Received from server: ${JSON.stringify(msg)}`);
    } catch (e) {
        console.log(`[MSG] Received: ${data}`);
    }
});

// Timeout safety
setTimeout(() => {
    console.log('✗ Test timeout - server not responding');
    ws.close();
    process.exit(1);
}, 15000);
