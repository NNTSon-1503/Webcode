/**
 * TEST: Kiểm tra mã hóa Chaotic Masking & giải mã T-S SMC
 * Mô phỏng quá trình: Arduino → Server
 * 
 * Mục đích: Xác nhận sync error → 0 ✓
 */

// ========================================================
// T-S LORENZ MASTER (Mô phỏng Arduino)
// ========================================================
class ChenMaster {
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
    }

    init() {
        this.xm = 0.15;
        this.ym = 0.25;
        this.zm = -0.5;
    }

    weights(x) {
        const xc = Math.max(-this.M, Math.min(x, this.M));
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

        const fsx = w1 * a1x + w2 * a2x + x;
        const fsy = w1 * a1y + w2 * a2y + y;
        const fsz = w1 * a1z + w2 * a2z + z;

        return { fsx, fsy, fsz };
    }

    step() {
        const { fsx, fsy, fsz } = this.fTS(this.xm, this.ym, this.zm);
        this.xm += fsx * this.dt;
        this.ym += fsy * this.dt;
        this.zm += fsz * this.dt;
    }

    advance(n) {
        for (let i = 0; i < n; i++) this.step();
    }

    getState() {
        return { x: this.xm, y: this.ym, z: this.zm };
    }
}

// ========================================================
// T-S LORENZ SLAVE RECEIVER (Mô phỏng Server)
// ========================================================
class ChenReceiver {
    constructor() {
        this.a = 35.0;
        this.b = 3.0;
        this.c = 28.0;
        this.M = 100.0;
        this.dt = 0.001;
        this.xs = 0.15;  // Match master initial state
        this.ys = 0.25;
        this.zs = -0.5;
        this.iex = 0.0;
        this.iey = 0.0;
        this.iez = 0.0;
        this.lambda = 400.0;
        this.k_gain_x = 0.5;
        this.k_gain_y = 0.75;
        this.k_gain_z = 0.5;
    }

    init() {
        this.xs = 0.15;  // Match master initial state
        this.ys = 0.25;
        this.zs = -0.5;
        this.iex = 0.0;
        this.iey = 0.0;
        this.iez = 0.0;
    }

    fTS_Slave(xs, ys, zs) {
        const omega_s1 = (this.M - xs) / (2.0 * this.M);
        const omega_s2 = (this.M + xs) / (2.0 * this.M);

        const a1x = -this.a * xs + this.a * ys;
        const a1y = -7.0 * xs + this.c * ys + this.M * zs;
        const a1z = -this.M * ys - this.b * zs;

        const a2x = -this.a * xs + this.a * ys;
        const a2y = -7.0 * xs + this.c * ys - this.M * zs;
        const a2z = this.M * ys - this.b * zs;

        const fsx = omega_s1 * a1x + omega_s2 * a2x + xs;
        const fsy = omega_s1 * a1y + omega_s2 * a2y + ys;
        const fsz = omega_s1 * a1z + omega_s2 * a2z + zs;

        return { fsx, fsy, fsz };
    }

    sat(err, epsilon = 0.05) {
        return Math.max(-1, Math.min(1, err / epsilon));
    }

    syncWithMaster(receivedMaster) {
        for (let i = 0; i < 100; i++) {
            const eX = receivedMaster.x - this.xs;
            const eY = receivedMaster.y - this.ys;
            const eZ = receivedMaster.z - this.zs;

            const sX = eX + this.lambda * this.iex;
            const sY = eY + this.lambda * this.iey;
            const sZ = eZ + this.lambda * this.iez;

            const omega_m1 = (this.M - receivedMaster.x) / (2.0 * this.M);
            const omega_m2 = (this.M + receivedMaster.x) / (2.0 * this.M);

            const f_master_x =
                omega_m1 * (-this.a * receivedMaster.x + this.a * receivedMaster.y) +
                omega_m2 * (-this.a * receivedMaster.x + this.a * receivedMaster.y) +
                receivedMaster.x;
            const f_master_y =
                omega_m1 * (-7.0 * receivedMaster.x + this.c * receivedMaster.y + this.M * receivedMaster.z) +
                omega_m2 * (-7.0 * receivedMaster.x + this.c * receivedMaster.y - this.M * receivedMaster.z) +
                receivedMaster.y;
            const f_master_z =
                omega_m1 * (-this.M * receivedMaster.y - this.b * receivedMaster.z) +
                omega_m2 * (this.M * receivedMaster.y - this.b * receivedMaster.z) +
                receivedMaster.z;

            const { fsx, fsy, fsz } = this.fTS_Slave(this.xs, this.ys, this.zs);

            const u_eq_x = f_master_x - fsx + this.lambda * eX;
            const u_eq_y = f_master_y - fsy + this.lambda * eY;
            const u_eq_z = f_master_z - fsz + this.lambda * eZ;

            const u_sw_x = this.k_gain_x * this.sat(sX);
            const u_sw_y = this.k_gain_y * this.sat(sY);
            const u_sw_z = this.k_gain_z * this.sat(sZ);

            const uX = u_eq_x + u_sw_x;
            const uY = u_eq_y + u_sw_y;
            const uZ = u_eq_z + u_sw_z;

            this.xs += (fsx + uX) * this.dt;
            this.ys += (fsy + uY) * this.dt;
            this.zs += (fsz + uZ) * this.dt;

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

// ========================================================
// TEST
// ========================================================
console.log("═══════════════════════════════════════════════════════════");
console.log("  TEST: Chaotic Masking Encoding & Decoding (100 cycles)");
console.log("═══════════════════════════════════════════════════════════\n");

const master = new ChenMaster();
const receiver = new ChenReceiver();

master.init();
receiver.init();  // Init once, do NOT reset between cycles

let totalSyncError = 0;
let maxSyncError = 0;
let decodeErrors = [];

for (let cycle = 0; cycle < 100; cycle++) {
    // ===== ARDUINO SIDE (MASTER) =====
    master.advance(100);
    const masterState = master.getState();
    const chaosSum = masterState.x + masterState.y + masterState.z;

    // Simulated physical data
    const actualVelL = 50 + cycle;
    const actualVelR = 55 + cycle;
    const state = 0;
    const xPos = cycle * 0.5;
    const yPos = cycle * 0.3;
    const thetaPos = cycle * 0.01;

    // Mask data
    const maskedVL = actualVelL + chaosSum;
    const maskedVR = actualVelR + chaosSum;
    const maskedState = state + chaosSum;
    const maskedX = xPos + chaosSum;
    const maskedY = yPos + chaosSum;
    const maskedTheta = thetaPos + chaosSum;

    // ===== SERVER SIDE (RECEIVER) =====
    // Do NOT reset receiver - it continues from previous state
    const syncResult = receiver.syncWithMaster(masterState);
    const syncError = Math.sqrt(
        syncResult.errX * syncResult.errX +
        syncResult.errY * syncResult.errY +
        syncResult.errZ * syncResult.errZ
    );

    const localChaosSum = receiver.xs + receiver.ys + receiver.zs;

    // Decode
    const decodedVL = maskedVL - localChaosSum;
    const decodedVR = maskedVR - localChaosSum;
    const decodedState = maskedState - localChaosSum;
    const decodedX = maskedX - localChaosSum;
    const decodedY = maskedY - localChaosSum;
    const decodedTheta = maskedTheta - localChaosSum;

    // Error calculation
    const errVL = Math.abs(decodedVL - actualVelL);
    const errVR = Math.abs(decodedVR - actualVelR);
    const errX = Math.abs(decodedX - xPos);
    const errY = Math.abs(decodedY - yPos);

    decodeErrors.push({
        errVL,
        errVR,
        errX,
        errY
    });

    totalSyncError += syncError;
    if (syncError > maxSyncError) maxSyncError = syncError;

    if (cycle % 20 === 0 || cycle === 99) {
        console.log(
            `Cycle ${cycle.toString().padStart(2, '0')}: ` +
            `syncErr=${syncError.toFixed(6).padStart(10, ' ')} | ` +
            `chaosSum=${chaosSum.toFixed(3).padStart(8, ' ')} vs ${localChaosSum.toFixed(3).padStart(8, ' ')} | ` +
            `errVL=${errVL.toFixed(6).padStart(10, ' ')} errVR=${errVR.toFixed(6).padStart(10, ' ')}`
        );
    }
}

// Calculate average decode errors
const avgErrVL = decodeErrors.reduce((sum, e) => sum + e.errVL, 0) / decodeErrors.length;
const avgErrVR = decodeErrors.reduce((sum, e) => sum + e.errVR, 0) / decodeErrors.length;
const avgErrX = decodeErrors.reduce((sum, e) => sum + e.errX, 0) / decodeErrors.length;
const avgErrY = decodeErrors.reduce((sum, e) => sum + e.errY, 0) / decodeErrors.length;

console.log("\n═══════════════════════════════════════════════════════════");
console.log("RESULTS:");
console.log("───────────────────────────────────────────────────────────");
console.log(`Average Sync Error:     ${(totalSyncError / 100).toFixed(8)}`);
console.log(`Maximum Sync Error:     ${maxSyncError.toFixed(8)}`);
console.log(`Status (syncErr):       ${maxSyncError < 1.0 ? "✅ PASS" : "⚠️  HIGH"}`);
console.log("\nDecoding Errors:");
console.log(`  Average vL error:     ${avgErrVL.toFixed(8)}`);
console.log(`  Average vR error:     ${avgErrVR.toFixed(8)}`);
console.log(`  Average x error:      ${avgErrX.toFixed(8)}`);
console.log(`  Average y error:      ${avgErrY.toFixed(8)}`);
console.log(`  Status (decoding):    ${avgErrVL < 1.0 && avgErrVR < 1.0 ? "✅ PASS" : "❌ FAIL"}`);
console.log("═══════════════════════════════════════════════════════════\n");
