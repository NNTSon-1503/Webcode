// ==========================================================
// AGV LINE FOLLOWER - WebSocket + T-S Lorenz Encryption
// Vi điều khiển: ESP32
// Động cơ: 2x JGB37-520 + Mạch HW039 (cầu H L298N compatible)
// ==========================================================

#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <SPI.h>
#include <MFRC522.h>

// ==========================================================
// WIFI & WEBSOCKET CONFIG
// ==========================================================
const char* SSID      = "ICEA_T3";
const char* PASSWORD  = "02438683518";
const char* SERVER_IP = "192.168.3.108";
const int   SERVER_PORT = 3000;

WebSocketsClient webSocket;

// ==========================================================
// CHÂN KẾT NỐI - HW039 (L298N compatible)
// HW039 Left:  ENA=L_REN, IN1=L_RPWM, IN2=L_LPWM, ENB=L_LEN
// HW039 Right: ENA=R_REN, IN1=R_RPWM, IN2=R_LPWM, ENB=R_LEN
// ==========================================================
const int L_RPWM = 13;  // PWM tiến trái
const int L_REN  = 12;  // Enable A trái
const int L_LEN  = 14;  // Enable B trái  
const int L_LPWM = 27;  // PWM lùi trái

const int R_RPWM = 15;  // PWM tiến phải
const int R_REN  = 16;  // Enable A phải
const int R_LEN  = 4;   // Enable B phải
const int R_LPWM = 2;   // PWM lùi phải (GPIO 2, tránh input-only 34)

// --- Encoder JGB37-520 ---
const int L_ENC = 26;   // Encoder trái
const int R_ENC = 25;   // Encoder phải

// --- Cảm biến TCRT5000 (5 cảm biến bám line) ---
// Vị trí: S1(trái nhất) .. S5(phải nhất)
const int S1 = 5, S2 = 17, S3 = 18, S4 = 21, S5 = 19;

// --- RC522 (RFID) ---
#define RST_PIN  35
#define SS_PIN   32
MFRC522 mfrc522(SS_PIN, RST_PIN);

// ==========================================================
// BIẾN TRẠNG THÁI CHUNG
// ==========================================================
float x_pos = 0, y_pos = 0, theta_pos = 0;

volatile long left_pulses  = 0;
volatile long right_pulses = 0;

int   state            = 0;    // 0=chạy, 1=dừng tạm, 2=estop
unsigned long stop_start_time = 0;
const unsigned long PAUSE_TIME = 5000;

// ==========================================================
// THAM SỐ ĐIỀU KHIỂN
// ==========================================================

// --- Tốc độ cơ bản (đơn vị PWM 0-255) ---
int base_speed_config = 120;   // Tốc độ mặc định (nên tune lại thực tế)
int base_speed        = 120;

// -------------------------------------------------------
// BỘ 1: PID BÁM LINE (Line Following PID)
// Input : error_line = sai lệch vị trí đường kẻ (-20 .. +20)
// Output: turn_signal = tín hiệu rẽ (điều chỉnh chênh lệch 2 bánh)
// -------------------------------------------------------
// Công thức: turn = Kp*e + Ki*∫e*dt + Kd*(de/dt)
// Gợi ý tune:
//   Bắt đầu Kp=1.5, Ki=0, Kd=0 → tăng Kp đến khi dao động
//   Giảm Kp 30%, thêm Kd để dập dao động
//   Thêm nhỏ Ki nếu bị lệch quỹ đạo thường xuyên
float Kp_line = 1.5f;
float Ki_line = 0.02f;  // Nhỏ thôi, chỉ bù sai số dài hạn
float Kd_line = 8.0f;

float error_line          = 0;
float prev_error_line     = 0;
float integral_line       = 0;

// Bộ lọc EMA cho đạo hàm (giảm nhiễu đầu vào sensor)
// alpha nhỏ → lọc nhiều nhưng chậm | alpha lớn → nhanh nhưng nhiễu
const float DERIV_FILTER_ALPHA = 0.3f;
float filtered_deriv = 0;

// Anti-windup: giới hạn tích phân line
const float INTEGRAL_LINE_MAX = 150.0f;

// -------------------------------------------------------
// BỘ 2: PI ĐIỀU KHIỂN TỐC ĐỘ BÁNH (Wheel Speed PI)
// Input : target_pwm_L/R (PWM mục tiêu từ Line PID)
//         actual_vel_L/R (xung encoder trong 20ms)
// 
// VẤN ĐỀ THIẾT KẾ: target là PWM (0-255), actual là xung encoder.
// Giải pháp: Dùng encoder feedback để ổn định tốc độ tương đối,
// chuẩn hóa encoder về dải 0-255 dựa trên xung tối đa đo được.
//
// JGB37-520 thông số tham khảo:
//   - Encoder: 12 xung/vòng × tỉ số hộp số
//   - Ở PWM=255, đo actual_vel thực tế rồi set MAX_PULSE
// -------------------------------------------------------
const float MAX_PULSE_PER_CYCLE = 25.0f; // Số xung tối đa trong 20ms ở PWM=255 (PHẢI ĐO THỰC TẾ)

float Kp_vel = 1.2f;   // Proportional wheel speed
float Ki_vel = 0.4f;   // Integral wheel speed

float integral_vel_L = 0, integral_vel_R = 0;
const float INTEGRAL_VEL_MAX = 80.0f;

// Target PWM cho từng bánh (sau tính từ Line PID)
float target_pwm_L = 0, target_pwm_R = 0;

// Vận tốc thực tế (xung/chu kỳ)
long actual_vel_L = 0, actual_vel_R = 0;

// Output PWM cuối cùng
int v_left = 0, v_right = 0;

// ==========================================================
// CHAOTIC ENCRYPTION (T-S LORENZ / Chen)
// ==========================================================
class ChenMaster {
private:
    const float _a    = 35.0f;
    const float _b    = 3.0f;
    const float _c    = 28.0f;
    const float _teta = 7.0f;
    const float _M    = 100.0f;
    const float _dt   = 0.001f;
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
        fx = w1*a1x + w2*a2x;
        fy = w1*a1y + w2*a2y;
        fz = w1*a1z + w2*a2z;
    }

public:
    ChenMaster() : xm(0.15f), ym(0.25f), zm(-0.5f) {}
    void init() { xm = 0.15f; ym = 0.25f; zm = -0.5f; }
    void step() {
        float fx, fy, fz;
        fTS(xm, ym, zm, fx, fy, fz);
        xm += fx * _dt; ym += fy * _dt; zm += fz * _dt;
    }
    void advance(int n) { for (int i = 0; i < n; i++) step(); }
    void getState(float &x, float &y, float &z) { x = xm; y = ym; z = zm; }
};

ChenMaster encryptor;

// ==========================================================
// INTERRUPT HANDLERS - ENCODER
// ==========================================================
void IRAM_ATTR isr_enc_L() { left_pulses++; }
void IRAM_ATTR isr_enc_R() { right_pulses++; }

// ==========================================================
// WEBSOCKET
// ==========================================================
void handleCommand(JsonObject cmd) {
    const char* action = cmd["action"];
    int value = cmd["value"] | 0;
    Serial.printf("[CMD] Action: %s, Value: %d\n", action, value);

    if (strcmp(action, "speed") == 0) {
        base_speed_config = constrain(value, 0, 255);
        base_speed = base_speed_config;
    } else if (strcmp(action, "stop") == 0) {
        state = 1;
        stop_start_time = millis();
    } else if (strcmp(action, "estop") == 0) {
        state = 2;
    } else if (strcmp(action, "resume") == 0) {
        state = 0;
        base_speed = base_speed_config;
    }
    // Cho phép tune PID qua WebSocket từ dashboard
    else if (strcmp(action, "set_kp_line") == 0) { Kp_line = (float)value / 100.0f; }
    else if (strcmp(action, "set_ki_line") == 0) { Ki_line = (float)value / 1000.0f; }
    else if (strcmp(action, "set_kd_line") == 0) { Kd_line = (float)value / 100.0f; }
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    switch(type) {
        case WStype_DISCONNECTED: Serial.println("[WS] Disconnected!"); break;
        case WStype_CONNECTED:    Serial.printf("[WS] Connected: %s\n", payload); break;
        case WStype_TEXT: {
            StaticJsonDocument<256> doc;
            if (!deserializeJson(doc, payload, length))
                handleCommand(doc.as<JsonObject>());
            break;
        }
        default: break;
    }
}

void setupWebSocket() {
    webSocket.begin(SERVER_IP, SERVER_PORT, "/");
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(5000);
}

// ==========================================================
// QUẢN LÝ TRẠNG THÁI
// ==========================================================
void updateState() {
    if (state == 1) {
        if (millis() - stop_start_time >= PAUSE_TIME) {
            state = 0;
            base_speed = base_speed_config;
            Serial.println("[STATE] Auto-resume after pause");
        } else {
            base_speed = 0;
        }
    } else if (state == 2) {
        base_speed = 0;
    } else {
        base_speed = base_speed_config;
    }
}

// ==========================================================
// ĐỌC CẢM BIẾN & TÍNH SAI SỐ LINE
// ==========================================================
// Trả về: error_line trong dải [-20, +20]
// Âm = lệch trái, Dương = lệch phải
// Trọng số: S1=-20, S2=-10, S3=0, S4=+10, S5=+20
// TCRT5000: line đen → output HIGH (1) khi dùng với module có comparator
// Điều chỉnh logic đảo nếu cảm biến của bạn hoạt động ngược
void calculateLineError() {
    // Đọc cảm biến - TCRT5000: cần đảo tùy phân cực module
    int v1 = digitalRead(S1);  // Thay !digitalRead nếu cảm biến đảo
    int v2 = digitalRead(S2);
    int v3 = digitalRead(S3);
    int v4 = digitalRead(S4);
    int v5 = digitalRead(S5);
    int sum = v1 + v2 + v3 + v4 + v5;

    if (sum != 0) {
        // Tính trọng tâm (centroid) của vị trí line
        error_line = (float)(v1*(-20) + v2*(-10) + v3*(0) + v4*(10) + v5*(20)) / (float)sum;
    } else {
        // Mất line: giữ sai số tối đa ở hướng mất cuối cùng
        error_line = (prev_error_line > 0) ? 20.0f : (prev_error_line < 0) ? -20.0f : 0.0f;
    }
}

// ==========================================================
// ĐIỀU KHIỂN MOTOR TRỰC TIẾP (PWM)
// HW039: ENA/ENB luôn HIGH, điều khiển qua IN1/IN2
// ==========================================================
void setMotorSpeed(int pwm_L, int pwm_R) {
    // --- Bánh TRÁI ---
    if (pwm_L >= 0) {
        analogWrite(L_RPWM, pwm_L);
        analogWrite(L_LPWM, 0);
    } else {
        analogWrite(L_RPWM, 0);
        analogWrite(L_LPWM, -pwm_L);
    }

    // --- Bánh PHẢI ---
    if (pwm_R >= 0) {
        analogWrite(R_RPWM, pwm_R);
        analogWrite(R_LPWM, 0);
    } else {
        analogWrite(R_RPWM, 0);
        analogWrite(R_LPWM, -pwm_R);
    }
}

// ==========================================================
// BỘ ĐIỀU KHIỂN CHÍNH - GỌI MỖI 20ms
// ==========================================================
void updateMotor() {
    // ---- BƯỚC 1: ĐỌC CẢM BIẾN & TÍNH SAI SỐ LINE ----
    calculateLineError();

    // ---- BƯỚC 2: PID BÁM LINE ----
    // Tích phân với anti-windup (chỉ tích phân khi chạy)
    integral_line += error_line;
    integral_line  = constrain(integral_line, -INTEGRAL_LINE_MAX, INTEGRAL_LINE_MAX);

    // Đạo hàm qua bộ lọc EMA để giảm nhiễu
    float raw_deriv   = error_line - prev_error_line;
    filtered_deriv    = DERIV_FILTER_ALPHA * raw_deriv + (1.0f - DERIV_FILTER_ALPHA) * filtered_deriv;
    prev_error_line   = error_line;

    // Tín hiệu rẽ (Turn Signal)
    float turn = Kp_line * error_line
               + Ki_line * integral_line
               + Kd_line * filtered_deriv;

    // Giới hạn turn tối đa = 80% tốc độ cơ bản
    float turn_limit = (float)base_speed * 0.8f;
    turn = constrain(turn, -turn_limit, turn_limit);

    // ---- BƯỚC 3: TÍNH TỐC ĐỘ MỤC TIÊU TỪNG BÁNH ----
    // Giảm tốc khi lỗi lớn (adaptive speed reduction)
    float slowdown_ratio = fabsf(error_line) / 20.0f;          // 0.0 (thẳng) → 1.0 (lệch max)
    float adaptive_speed = (float)base_speed * (1.0f - 0.35f * slowdown_ratio);
    adaptive_speed = max(adaptive_speed, (float)base_speed * 0.4f); // Tối thiểu 40% tốc độ

    target_pwm_L = adaptive_speed + turn;
    target_pwm_R = adaptive_speed - turn;

    // Clamp target PWM về dải [-255, 255]
    target_pwm_L = constrain(target_pwm_L, -255.0f, 255.0f);
    target_pwm_R = constrain(target_pwm_R, -255.0f, 255.0f);

    // ---- BƯỚC 4: DỪNG KHI base_speed = 0 ----
    if (base_speed == 0) {
        integral_line   = 0;
        integral_vel_L  = 0;
        integral_vel_R  = 0;
        filtered_deriv  = 0;
        prev_error_line = 0;
        setMotorSpeed(0, 0);
        v_left = 0; v_right = 0;
        return;
    }

    // ---- BƯỚC 5: PI ỔN ĐỊNH TỐC ĐỘ BÁNH (Encoder Feedback) ----
    // Chuyển encoder xung → PWM tương đương để so sánh cùng đơn vị
    // actual_vel = xung đếm trong 20ms → normalize về 0-255
    float norm_vel_L = (float)actual_vel_L / MAX_PULSE_PER_CYCLE * 255.0f;
    float norm_vel_R = (float)actual_vel_R / MAX_PULSE_PER_CYCLE * 255.0f;

    // Giữ dấu chiều quay (nếu target âm thì bánh đang lùi)
    if (target_pwm_L < 0) norm_vel_L = -norm_vel_L;
    if (target_pwm_R < 0) norm_vel_R = -norm_vel_R;

    // Sai số tốc độ
    float eL = target_pwm_L - norm_vel_L;
    float eR = target_pwm_R - norm_vel_R;

    // Tích phân với anti-windup
    integral_vel_L = constrain(integral_vel_L + eL, -INTEGRAL_VEL_MAX, INTEGRAL_VEL_MAX);
    integral_vel_R = constrain(integral_vel_R + eR, -INTEGRAL_VEL_MAX, INTEGRAL_VEL_MAX);

    // PWM đầu ra
    float pwmL_f = target_pwm_L + Kp_vel * eL + Ki_vel * integral_vel_L;
    float pwmR_f = target_pwm_R + Kp_vel * eR + Ki_vel * integral_vel_R;

    int pwmL = constrain((int)pwmL_f, -255, 255);
    int pwmR = constrain((int)pwmR_f, -255, 255);

    setMotorSpeed(pwmL, pwmR);
    v_left  = pwmL;
    v_right = pwmR;
}

// ==========================================================
// GỬI TELEMETRY (MÃ HÓA CHEN)
// ==========================================================
void sendTelemetry() {
    encryptor.advance(100);
    float cx, cy, cz;
    encryptor.getState(cx, cy, cz);
    float cs = cx + cy + cz;

    StaticJsonDocument<256> doc;
    doc["type"]     = "agv_telemetry";
    doc["chaos_x"]  = cx;
    doc["chaos_y"]  = cy;
    doc["chaos_z"]  = cz;
    doc["tx_vL"]    = actual_vel_L + cs;
    doc["tx_vR"]    = actual_vel_R + cs;
    doc["tx_state"] = (float)state  + cs;
    doc["tx_x"]     = x_pos         + cs;
    doc["tx_y"]     = y_pos         + cs;
    doc["tx_theta"] = theta_pos      + cs;

    // Debug thêm PID params (không mã hóa)
    doc["err"]   = error_line;
    doc["turn"]  = Kp_line * error_line + Ki_line * integral_line + Kd_line * filtered_deriv;

    String json_str;
    serializeJson(doc, json_str);
    webSocket.sendTXT(json_str);
}

// ==========================================================
// SETUP
// ==========================================================
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== AGV JGB37-520 + HW039 + PID Line Follower ===");

    // Output pins (HW039 driver)
    int outPins[] = {L_RPWM, L_LPWM, L_REN, L_LEN, R_RPWM, R_LPWM, R_REN, R_LEN};
    for (int pin : outPins) { pinMode(pin, OUTPUT); digitalWrite(pin, LOW); }

    // Enable cầu H (HW039 cần ENA/ENB HIGH để cho phép PWM điều khiển)
    digitalWrite(L_REN, HIGH); digitalWrite(L_LEN, HIGH);
    digitalWrite(R_REN, HIGH); digitalWrite(R_LEN, HIGH);

    // Input: cảm biến line
    pinMode(S1, INPUT); pinMode(S2, INPUT); pinMode(S3, INPUT);
    pinMode(S4, INPUT); pinMode(S5, INPUT);

    // Input: encoder JGB37-520
    pinMode(L_ENC, INPUT_PULLUP);
    pinMode(R_ENC, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(L_ENC), isr_enc_L, RISING);
    attachInterrupt(digitalPinToInterrupt(R_ENC), isr_enc_R, RISING);

    // RFID RC522 (SCK=33, MISO=22, MOSI=23, SS=32)
    SPI.begin(33, 22, 23, 32);
    mfrc522.PCD_Init();
    Serial.println("✓ RFID RC522 OK");

    // WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(SSID, PASSWORD);
    Serial.print("Connecting WiFi");
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.printf("\n✓ WiFi OK! IP: %s\n", WiFi.localIP().toString().c_str());

    setupWebSocket();
    encryptor.init();
    Serial.println("✓ System Ready!");
    Serial.printf("  PID Line: Kp=%.2f Ki=%.3f Kd=%.2f\n", Kp_line, Ki_line, Kd_line);
    Serial.printf("  PI  Vel:  Kp=%.2f Ki=%.2f\n", Kp_vel, Ki_vel);
    Serial.printf("  Base Speed: %d | Max Pulse: %.1f\n", base_speed_config, MAX_PULSE_PER_CYCLE);
}

// ==========================================================
// MAIN LOOP
// ==========================================================
void loop() {
    webSocket.loop();
    unsigned long now = millis();

    // ---- KHỐI 1: ĐIỀU KHIỂN ĐỘNG CƠ (Mỗi 20ms = 50Hz) ----
    static unsigned long prevMotor = 0;
    if (now - prevMotor >= 20) {
        prevMotor = now;

        // Đọc encoder atomic rồi reset ngay
        noInterrupts();
        actual_vel_L = left_pulses;
        actual_vel_R = right_pulses;
        left_pulses  = 0;
        right_pulses = 0;
        interrupts();

        updateState();
        updateMotor();
    }

    // ---- KHỐI 2: ĐỌC RFID CẬP NHẬT TỌA ĐỘ ----
    if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
        String uid = "";
        for (byte i = 0; i < mfrc522.uid.size; i++) {
            uid += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
            uid += String(mfrc522.uid.uidByte[i], HEX);
        }
        uid.toUpperCase();
        Serial.println("[RFID] UID: " + uid);

        // TODO: Thay UID thực tế của thẻ bạn dùng
        if      (uid == "A1B2C3D4") { x_pos = 100; y_pos = 100; theta_pos = 0;  }
        else if (uid == "E5F6G7H8") { x_pos = 200; y_pos = 100; theta_pos = 90; }

        mfrc522.PICC_HaltA();
    }

    // ---- KHỐI 3: GỬI TELEMETRY (Mỗi 100ms) ----
    static unsigned long lastSend = 0;
    if (now - lastSend >= 100) {
        lastSend = now;
        sendTelemetry();
    }

    // ---- KHỐI 4: KIỂM TRA WIFI (Mỗi 10s) ----
    static unsigned long lastWifi = 0;
    if (now - lastWifi >= 10000) {
        lastWifi = now;
        if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
    }
}
