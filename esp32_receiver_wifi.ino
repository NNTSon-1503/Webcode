// ==========================================================
// ESP32 RECEIVER - T-S SMC SLAVE + DISTURBANCE OBSERVER
// ----------------------------------------------------------
// Nhận gói tin mã hóa từ AGV qua ESP-NOW
// Giải mã bằng khóa hỗn loạn T-S Lorenz (giống agv_pi)
// Chạy đồng bộ hệ Master-Slave T-S SMC + DOB (khớp MATLAB):
//   main_sync_ts_smc_dob.m / chaotic_sync_ts_smc_dob_ode.m
// Gửi kết quả lên Web Dashboard qua Socket.IO
// ==========================================================

#include <esp_now.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <WebSocketsClient.h>
#include <SocketIOclient.h>
#include <math.h>

// ==========================================================
// 1. CẤU HÌNH WIFI & WEBSOCKETS
// ==========================================================
const char* ssid       = "ICEA_T3";
const char* password   = "02438683518";
const char* serverIP   = "192.168.3.183";
const int   SERVER_PORT = 3000;

SocketIOclient socketIO;

// ==========================================================
// 2. CẤU TRÚC DỮ LIỆU ESP-NOW
// ==========================================================
typedef struct struct_message {
    int v_left, v_right, state;
    float x, y, theta;
} struct_message;

typedef struct encrypted_message {
    uint8_t enc_v_left, enc_v_right, enc_state;
    uint8_t enc_x, enc_y, enc_theta;
    uint8_t checksum;
} encrypted_message;

typedef struct command_message {
    int action; // 1:Start 2:Stop 3:E-Stop
} command_message;

struct_message    incomingData;
encrypted_message encryptedData;
command_message   cmdData;
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ==========================================================
// 3. T-S SMC MASTER (GIẢI MÃ) - Khớp hoàn toàn với agv_pi
// ----------------------------------------------------------
// Chạy cùng điều kiện biên & tham số với Master trên xe.
// Tạo ra đúng chuỗi khóa chaos để giải mã gói tin.
// ==========================================================
class TSSMCMaster {
private:
    const float _a    = 35.0f, _b = 3.0f, _c = 28.0f;
    const float _teta =  7.0f, _M = 100.0f, _dt = 0.001f;

    float xm, ym, zm;

    void weights(float x, float &w1, float &w2) {
        float xc = constrain(x, -_M, _M);
        w1 = (_M - xc) / (2.0f * _M);
        w2 = (_M + xc) / (2.0f * _M);
    }

    void fTS(float x, float y, float z, float &fx, float &fy, float &fz) {
        float w1, w2;
        weights(x, w1, w2);
        float a1x = -_a*x + _a*y,  a1y = -_teta*x + _c*y + _M*z,  a1z = -_M*y - _b*z;
        float a2x = -_a*x + _a*y,  a2y = -_teta*x + _c*y - _M*z,  a2z =  _M*y - _b*z;
        fx = w1*a1x + w2*a2x + x;
        fy = w1*a1y + w2*a2y + y;
        fz = w1*a1z + w2*a2z + z;
    }

public:
    TSSMCMaster() : xm(0.15f), ym(0.25f), zm(-0.5f) {}
    void init() { xm = 0.15f; ym = 0.25f; zm = -0.5f; }

    void step() {
        float fx, fy, fz;
        fTS(xm, ym, zm, fx, fy, fz);
        xm += fx * _dt; ym += fy * _dt; zm += fz * _dt;
    }

    void advance(int n) { for (int i = 0; i < n; i++) step(); }

    uint8_t getKey() {
        return (uint8_t)((int)(fmod(fabs(xm), 1.0f) * 255.0f));
    }

    // Giải mã 1 trường (đối xứng với encrypt() ở agv_pi) + advance 1 bước
    int decrypt(uint8_t enc) {
        uint8_t key = getKey();
        int dec = ((int)enc - (int)key + 255) % 255;
        step();
        return dec;
    }

    void getState(float &x, float &y, float &z) { x = xm; y = ym; z = zm; }
};

// ==========================================================
// 4. T-S SMC SLAVE + DOB - Đồng bộ hóa & Ước lượng nhiễu
// ----------------------------------------------------------
// Chạy cả hệ Master và Slave theo phương trình MATLAB:
//   chaotic_sync_ts_smc_dob_ode()
//
// THAM SỐ (khớp MATLAB):
//   λ = 400          (hệ số mặt trượt)
//   k_gain = diag([0.001, 0.0015, 0.001])  (0.00001*[100,150,100])
//   L_dob  = [50, 50, 50]                  (gain DOB)
//   d0     = [0.1, 0.1, 0.1], ω = 2π      (nhiễu kiểm thử)
//
// ĐIỀU KIỆN BAN ĐẦU (khớp MATLAB):
//   xm0 = [0.15, 0.25, -0.5]
//   xs0 = [1.0,  2.0,  -4.0]
// ==========================================================
class TSSMCSlaveDOB {
private:
    // Tham số T-S Lorenz (giống TSSMCMaster)
    const float _a    = 35.0f, _b = 3.0f, _c = 28.0f;
    const float _teta =  7.0f, _M = 100.0f, _dt = 0.001f;

    // Tham số SMC (khớp MATLAB)
    const float lambda = 400.0f;          // Hệ số mặt trượt
    const float k_x    = 0.001f;          // 0.00001 * 100
    const float k_y    = 0.0015f;         // 0.00001 * 150
    const float k_z    = 0.001f;          // 0.00001 * 100

    // Tham số DOB
    const float L_dob  = 50.0f;

    // Tham số nhiễu kiểm thử (khớp MATLAB: d0=0.1, omega=2π)
    const float d0    = 0.1f;
    const float omega = 2.0f * PI;        // 2π rad/s

    // === Trạng thái Master (song song, mở vòng) ===
    float xm, ym, zm;

    // === Trạng thái Slave ===
    float xs, ys, zs;

    // === Tích phân sai số (cho mặt trượt s = e + λ*ie) ===
    float iex, iey, iez;

    // === Ước lượng nhiễu DOB ===
    float d_hat_x, d_hat_y, d_hat_z;

    // === Thời gian nội bộ (cho nhiễu d = d0*sin(ω*t)) ===
    float sync_time;

    // === Thống kê ===
    float ex_last, ey_last, ez_last;       // Sai số đồng bộ tức thời

    // ---- Hàm T-S (dùng chung) ----
    void weights(float x, float &w1, float &w2) {
        float xc = constrain(x, -_M, _M);
        w1 = (_M - xc) / (2.0f * _M);
        w2 = (_M + xc) / (2.0f * _M);
    }

    void fTS(float x, float y, float z, float &fx, float &fy, float &fz) {
        float w1, w2;
        weights(x, w1, w2);
        float a1x = -_a*x + _a*y,  a1y = -_teta*x + _c*y + _M*z,  a1z = -_M*y - _b*z;
        float a2x = -_a*x + _a*y,  a2y = -_teta*x + _c*y - _M*z,  a2z =  _M*y - _b*z;
        fx = w1*a1x + w2*a2x + x;
        fy = w1*a1y + w2*a2y + y;
        fz = w1*a1z + w2*a2z + z;
    }

    // Hàm bão hòa thay thế sign() để giảm chattering (saturation function)
    float sat(float s, float eps = 0.01f) {
        if (s >  eps) return  1.0f;
        if (s < -eps) return -1.0f;
        return s / eps;
    }

public:
    TSSMCSlaveDOB() :
        xm(0.15f), ym(0.25f), zm(-0.5f),
        xs(1.0f),  ys(2.0f),  zs(-4.0f),
        iex(0), iey(0), iez(0),
        d_hat_x(0), d_hat_y(0), d_hat_z(0),
        sync_time(0),
        ex_last(0), ey_last(0), ez_last(0) {}

    void init() {
        xm = 0.15f; ym = 0.25f; zm = -0.5f;
        xs = 1.0f;  ys = 2.0f;  zs = -4.0f;
        iex = iey = iez = 0;
        d_hat_x = d_hat_y = d_hat_z = 0;
        sync_time = 0;
    }

    // ============================================================
    // Chạy N bước Euler tích phân (dt = 0.001s = 1ms)
    // Khớp hoàn toàn với chaotic_sync_ts_smc_dob_ode() trong MATLAB:
    //
    //   [dy, u, d, d_hat] = chaotic_sync_ts_smc_dob_ode(t, y, ...)
    //
    //   e     = xm - xs
    //   ie   += e * dt
    //   s     = e + lambda * ie
    //   u_eq  = (f_m - f_s) + lambda * e
    //   u_sw  = k_gain * sat(s)
    //   u     = u_eq + u_sw - d_hat
    //   d     = d0 * sin(omega * t)          (nhiễu kiểm thử)
    //   residual = d - d_hat
    //   d_hat_dot = L_dob * residual         (DOB update)
    //   xs_dot = f_slave + u + d             (Slave dynamics)
    //   xm_dot = f_master                    (Master dynamics)
    // ============================================================
    void advance(int steps) {
        for (int i = 0; i < steps; i++) {
            // Đạo hàm Master và Slave
            float fmx, fmy, fmz, fsx, fsy, fsz;
            fTS(xm, ym, zm, fmx, fmy, fmz);
            fTS(xs, ys, zs, fsx, fsy, fsz);

            // Sai số đồng bộ
            float ex = xm - xs;
            float ey = ym - ys;
            float ez = zm - zs;

            // Cập nhật tích phân sai số
            iex += ex * _dt;
            iey += ey * _dt;
            iez += ez * _dt;

            // Mặt trượt: s = e + lambda * ie
            float sx = ex + lambda * iex;
            float sy = ey + lambda * iey;
            float sz = ez + lambda * iez;

            // Tín hiệu điều khiển (khớp MATLAB)
            float u_eq_x = (fmx - fsx) + lambda * ex;
            float u_eq_y = (fmy - fsy) + lambda * ey;
            float u_eq_z = (fmz - fsz) + lambda * ez;

            float u_sw_x = k_x * sat(sx);
            float u_sw_y = k_y * sat(sy);   // k_gain[1] = 0.00001*150
            float u_sw_z = k_z * sat(sz);

            float ux = u_eq_x + u_sw_x - d_hat_x;
            float uy = u_eq_y + u_sw_y - d_hat_y;
            float uz = u_eq_z + u_sw_z - d_hat_z;

            // Nhiễu kiểm thử (khớp MATLAB: d = d0.*sin(omega_d*t))
            float d_x = d0 * sinf(omega * sync_time);
            float d_y = d0 * sinf(omega * sync_time);
            float d_z = d0 * sinf(omega * sync_time);

            // DOB: residual = d - d_hat → d_hat hội tụ về d
            float res_x = d_x - d_hat_x;
            float res_y = d_y - d_hat_y;
            float res_z = d_z - d_hat_z;

            d_hat_x += L_dob * res_x * _dt;
            d_hat_y += L_dob * res_y * _dt;
            d_hat_z += L_dob * res_z * _dt;

            // Cập nhật Slave (f_slave + u + d, khớp MATLAB f_slave_controlled)
            xs += (fsx + ux + d_x) * _dt;
            ys += (fsy + uy + d_y) * _dt;
            zs += (fsz + uz + d_z) * _dt;

            // Cập nhật Master mở vòng
            xm += fmx * _dt;
            ym += fmy * _dt;
            zm += fmz * _dt;

            // Tăng thời gian nội bộ
            sync_time += _dt;
        }

        // Lưu sai số tức thời sau advance
        ex_last = xm - xs;
        ey_last = ym - ys;
        ez_last = zm - zs;
    }

    // Lấy sai số đồng bộ (nên → 0 khi hội tụ)
    void getSyncError(float &ex, float &ey, float &ez) {
        ex = ex_last; ey = ey_last; ez = ez_last;
    }

    // Lấy ước lượng nhiễu DOB (hội tụ về d0*sin(ω*t))
    void getDOB(float &dx, float &dy, float &dz) {
        dx = d_hat_x; dy = d_hat_y; dz = d_hat_z;
    }

    float getSyncTime() { return sync_time; }
};

// ==========================================================
// 5. BIẾN TOÀN CỤC
// ==========================================================
TSSMCMaster    cryptoMaster;   // Dùng để giải mã (crypto)
TSSMCSlaveDOB  slaveDOB;       // Dùng để chạy SMC + DOB (sync demo)

const int STATUS_LED = 2;
bool      sendPending = false;
String    pendingJson = "";

unsigned long decode_count  = 0;
unsigned long decode_errors = 0;

// ==========================================================
// 6. HÀM SOCKET.IO CALLBACK
// ==========================================================
void socketIOEvent(socketIOmessageType_t type, uint8_t *payload, size_t length) {
    switch (type) {
        case sIOtype_CONNECT:
            Serial.printf("[Socket.IO] Ket noi thanh cong\n");
            socketIO.send(sIOtype_CONNECT, "/");
            break;
        case sIOtype_DISCONNECT:
            Serial.printf("[Socket.IO] Mat ket noi!\n");
            break;
        case sIOtype_EVENT: {
            StaticJsonDocument<512> doc;
            if (!deserializeJson(doc, payload, length)) {
                const char* ev = doc[0];
                if (strcmp(ev, "command") == 0) {
                    const char* action = doc[1]["action"];
                    int code = 0;
                    if      (strcmp(action, "start") == 0) code = 1;
                    else if (strcmp(action, "stop")  == 0) code = 2;
                    else if (strcmp(action, "estop") == 0) code = 3;
                    if (code > 0) {
                        cmdData.action = code;
                        esp_now_send(broadcastAddress, (uint8_t*)&cmdData, sizeof(cmdData));
                        Serial.printf("[ESP-NOW] Gui lenh %s xuong AGV\n", action);
                        digitalWrite(STATUS_LED, HIGH); delay(50); digitalWrite(STATUS_LED, LOW);
                    }
                }
            }
            break;
        }
        default: break;
    }
}

// ==========================================================
// 7. ESP-NOW CALLBACK - GIẢI MÃ T-S SMC + CẬP NHẬT DOB
// ==========================================================
void onReceive(const esp_now_recv_info *recv_info, const uint8_t *buf, int len) {
    if (len != sizeof(encrypted_message)) return;

    memcpy(&encryptedData, buf, sizeof(encrypted_message));

    // --- Kiểm tra Checksum ---
    uint8_t chk = encryptedData.enc_v_left ^ encryptedData.enc_v_right ^
                  encryptedData.enc_state  ^ encryptedData.enc_x ^
                  encryptedData.enc_y      ^ encryptedData.enc_theta;
    if (chk != encryptedData.checksum) {
        decode_errors++;
        Serial.printf("[ERR] Checksum sai! Loi: %lu/%lu\n", decode_errors, decode_count + decode_errors);
        return;
    }

    // =========================================================
    // GIẢI MÃ T-S SMC:
    //   1. advance 100 bước (khớp với master.advance(100) ở agv_pi)
    //   2. decrypt 6 trường (mỗi decrypt advance thêm 1 bước)
    //   → Tổng: 106 bước mỗi gói tin, đồng bộ hoàn hảo 2 phía
    // =========================================================
    cryptoMaster.advance(100);

    int dec_vL  = cryptoMaster.decrypt(encryptedData.enc_v_left);
    int dec_vR  = cryptoMaster.decrypt(encryptedData.enc_v_right);
    int dec_st  = cryptoMaster.decrypt(encryptedData.enc_state);
    int dec_x   = cryptoMaster.decrypt(encryptedData.enc_x);
    int dec_y   = cryptoMaster.decrypt(encryptedData.enc_y);
    int dec_th  = cryptoMaster.decrypt(encryptedData.enc_theta);

    // --- Khôi phục giá trị gốc (nghịch đảo chuẩn hóa) ---
    incomingData.v_left  = dec_vL - 128;
    incomingData.v_right = dec_vR - 128;
    incomingData.state   = dec_st / 50;
    incomingData.x       = (float)(dec_x - 128);
    incomingData.y       = (float)(dec_y - 128);
    incomingData.theta   = ((float)(dec_th - 128) / 127.0f) * PI;

    decode_count++;

    // =========================================================
    // CẬP NHẬT T-S SMC + DOB (106 bước = khớp thời gian gói tin)
    // Chạy đồng bộ với crypto master để giữ sync_time nhất quán
    // =========================================================
    slaveDOB.advance(106);

    // Lấy kết quả đồng bộ và DOB
    float ex, ey, ez;
    slaveDOB.getSyncError(ex, ey, ez);
    float dob_x, dob_y, dob_z;
    slaveDOB.getDOB(dob_x, dob_y, dob_z);

    // =========================================================
    // Đóng gói JSON cho Socket.IO
    // Gửi cả telemetry thực từ xe VÀ thống kê SMC+DOB
    // =========================================================
    StaticJsonDocument<512> doc;
    JsonArray arr = doc.to<JsonArray>();
    arr.add("agv_data");

    JsonObject d = arr.createNestedObject();
    d["type"]    = "speed_ts_dob";

    // Telemetry thực (đã giải mã)
    d["v_left"]  = incomingData.v_left;
    d["v_right"] = incomingData.v_right;
    d["state"]   = incomingData.state;
    d["x"]       = incomingData.x;
    d["y"]       = incomingData.y;
    d["theta"]   = incomingData.theta;

    // Thống kê giải mã
    d["decode_count"]  = (unsigned int)decode_count;
    d["decode_errors"] = (unsigned int)decode_errors;

    // Sai số đồng bộ T-S SMC (nên hội tụ về 0)
    d["e_x"] = ex;
    d["e_y"] = ey;
    d["e_z"] = ez;

    // Ước lượng nhiễu DOB (hội tụ về d0*sin(ω*t) ≈ 0.1*sin(2π*t))
    d["dob_dx"] = dob_x;
    d["dob_dy"] = dob_y;
    d["dob_dz"] = dob_z;

    // Thời gian đồng bộ nội bộ
    d["sync_time"] = slaveDOB.getSyncTime();

    serializeJson(doc, pendingJson);
    sendPending = true;
}

// ==========================================================
// 8. KẾT NỐI WIFI
// ==========================================================
void connectToWiFi() {
    Serial.printf("\nKet noi WiFi: %s\n", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    int att = 0;
    while (WiFi.status() != WL_CONNECTED && att < 20) {
        delay(500); Serial.print("."); att++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n✓ WiFi ket noi! IP: %s | Channel: %d\n",
                      WiFi.localIP().toString().c_str(), WiFi.channel());
    } else {
        Serial.println("\n✗ Khong the ket noi WiFi");
    }
}

// ==========================================================
// 9. SETUP
// ==========================================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== ESP32 RECEIVER - T-S SMC + DOB Version ===");

    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(STATUS_LED, LOW);

    // Kết nối WiFi
    connectToWiFi();
    Serial.printf("MAC: %s\n", WiFi.macAddress().c_str());

    // Khởi tạo ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("Loi: Khong the khoi tao ESP-NOW"); return;
    }
    esp_now_register_recv_cb(onReceive);

    // Đăng ký peer broadcast để gửi lệnh xuống AGV
    esp_now_peer_info_t pi;
    memset(&pi, 0, sizeof(pi));
    memcpy(pi.peer_addr, broadcastAddress, 6);
    pi.channel = 0; pi.encrypt = false;
    if (esp_now_add_peer(&pi) != ESP_OK)
        Serial.println("Loi add peer broadcast");

    Serial.println("✓ ESP-NOW san sang");

    // Khởi tạo T-S SMC Master (crypto) - cùng điều kiện với agv_pi
    cryptoMaster.init();
    Serial.println("✓ T-S SMC Master (crypto) khoi tao: xm=[0.15, 0.25, -0.5]");

    // Khởi tạo T-S SMC Slave + DOB - khớp với MATLAB xs0
    slaveDOB.init();
    Serial.println("✓ T-S SMC Slave + DOB khoi tao: xs=[1.0, 2.0, -4.0]");
    Serial.println("  lambda=400, k=[0.001,0.0015,0.001], L_dob=50, d0=0.1, omega=2pi");

    // Kết nối Socket.IO
    socketIO.begin(serverIP, SERVER_PORT, "/socket.io/?EIO=4");
    socketIO.onEvent(socketIOEvent);

    Serial.println("✓ He thong san sang nhan du lieu tu AGV!");
}

// ==========================================================
// 10. LOOP
// ==========================================================
void loop() {
    socketIO.loop();

    // Gửi dữ liệu ra ngoài ISR context (an toàn)
    if (sendPending && WiFi.status() == WL_CONNECTED) {
        socketIO.sendEVENT(pendingJson);
        sendPending = false;
        digitalWrite(STATUS_LED, HIGH); delay(10); digitalWrite(STATUS_LED, LOW);

        // Debug in thưa
        static unsigned long lastDbg = 0;
        if (millis() - lastDbg >= 2000) {
            lastDbg = millis();
            float ex, ey, ez, dx, dy, dz;
            slaveDOB.getSyncError(ex, ey, ez);
            slaveDOB.getDOB(dx, dy, dz);
            Serial.printf("[SMC+DOB] t=%.2fs | e=[%.4f,%.4f,%.4f] | dob=[%.4f,%.4f,%.4f] | Decode:%lu Err:%lu\n",
                          slaveDOB.getSyncTime(), ex, ey, ez, dx, dy, dz,
                          decode_count, decode_errors);
        }
    }

    // Kiểm tra và kết nối lại WiFi định kỳ
    static unsigned long lastWiFiCheck = 0;
    if (millis() - lastWiFiCheck >= 10000) {
        lastWiFiCheck = millis();
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WARN] WiFi mat ket noi, dang ket noi lai...");
            connectToWiFi();
        }
    }
}
