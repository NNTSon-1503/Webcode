// ==========================================================
// AGV PI - T-S SMC MASTER CHAOS ENCRYPTION
// Mã hóa hỗn loạn bằng hệ Takagi-Sugeno tuyến tính hóa
// Khớp 100% với mô phỏng MATLAB (main_sync_ts_smc_dob.m)
// ==========================================================

// ==========================================================
// 1. KHAI BÁO THƯ VIỆN VÀ ĐỊNH NGHĨA CHÂN
// ==========================================================
#include <SPI.h>
#include <MFRC522.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <math.h>

// --- Định nghĩa chân RC522 (Giao tiếp SPI) ---
#define RST_PIN   35
#define SS_PIN    32
#define SCK_PIN   33
#define MISO_PIN  22
#define MOSI_PIN  23
MFRC522 mfrc522(SS_PIN, RST_PIN);

// --- Mạch cầu H (BTS7960) ---
const int L_RPWM = 13; const int L_REN = 12; const int L_LEN = 14; const int L_LPWM = 27;
const int R_RPWM = 15; const int R_REN = 16; const int R_LEN = 4;  const int R_LPWM = 2;

// --- Encoder ---
const int L_ENC = 26;
const int R_ENC = 25;
// --- Cảm biến TCRT5000 ---
const int S1 = 5; const int S2 = 17; const int S3 = 18; const int S4 = 21; const int S5 = 19;

// ==========================================================
// 2. ESP-NOW: CẤU TRÚC DỮ LIỆU
// ==========================================================
// THAY ĐỊA CHỈ MAC CỦA ESP32 NHẬN VÀO ĐÂY
uint8_t broadcastAddress[] = {0xA0, 0xB7, 0x65, 0x20, 0xBF, 0xB8};

typedef struct struct_message {
    int v_left;
    int v_right;
    int state;
    float x;
    float y;
    float theta;
} struct_message;

// Cấu trúc gói tin đã mã hóa T-S SMC
typedef struct encrypted_message {
    uint8_t enc_v_left;
    uint8_t enc_v_right;
    uint8_t enc_state;
    uint8_t enc_x;
    uint8_t enc_y;
    uint8_t enc_theta;
    uint8_t checksum;   // XOR toàn bộ để kiểm tra toàn vẹn
} encrypted_message;

typedef struct command_message {
    int action; // 1: Start, 2: Stop, 3: E-Stop
} command_message;

struct_message   speedData;
encrypted_message encryptedData;
esp_now_peer_info_t peerInfo;

// ==========================================================
// 3. BIẾN TOÀN CỤC & HỆ SỐ ĐIỀU KHIỂN
// ==========================================================
int agv_state = 0;
unsigned long stop_start_time = 0;
const int PAUSE_TIME = 5000;

float Kp_line = 0.8f; float Kd_line = 3.0f;
float error_line = 0; float previous_error_line = 0;
float filtered_error_line = 0; float previous_filtered_error_line = 0;

int base_speed_config = 18;
int base_speed        = 18;

float Kp_vel = 5.0f; float Ki_vel = 0.8f;
float integral_vel_L = 0, integral_vel_R = 0;

volatile long left_pulses  = 0;
volatile long right_pulses = 0;
long actual_vel_L = 0, actual_vel_R = 0;
long target_vel_L = 0, target_vel_R = 0;

// --- Odometry ---
const float WHEEL_RADIUS   = 3.25f;
const float WHEEL_BASE     = 26.0f;
const float PULSES_PER_REV = 990.0f;
float posX = 0.0f, posY = 0.0f, theta = 0.0f;

unsigned long previousMillis = 0;
const int interval = 20;

// ==========================================================
// 4. CÁC HÀM NGẮT & ĐIỀU KHIỂN CƠ BẢN
// ==========================================================
void IRAM_ATTR leftEncoderISR()  { left_pulses++; }
void IRAM_ATTR rightEncoderISR() { right_pulses++; }

void OnDataRecv(const esp_now_recv_info *recv_info, const uint8_t *incomingData, int len) {
    if (len != sizeof(command_message)) return;
    command_message cmd;
    memcpy(&cmd, incomingData, sizeof(cmd));
    if (cmd.action == 1) {
        agv_state = 0;
        Serial.println("{\"type\":\"action\",\"msg\":\"Received Start Command via ESP-NOW\"}");
    } else if (cmd.action == 2) {
        agv_state = 1; stop_start_time = millis();
        Serial.println("{\"type\":\"action\",\"msg\":\"Received Stop Command via ESP-NOW\"}");
    } else if (cmd.action == 3) {
        agv_state = 2;
        Serial.println("{\"type\":\"action\",\"msg\":\"Received E-Stop Command via ESP-NOW\"}");
    }
}

void calculateLineError() {
    int v1 = !digitalRead(S1), v2 = !digitalRead(S2), v3 = !digitalRead(S3);
    int v4 = !digitalRead(S4), v5 = !digitalRead(S5);
    int sum = v1 + v2 + v3 + v4 + v5;
    float raw = 0;
    if (sum != 0) {
        raw = (v1*-20 + v2*-10 + v3*0 + v4*10 + v5*20) / (float)sum;
        previous_error_line = raw;
    } else {
        raw = (previous_error_line > 0) ? 20.0f : ((previous_error_line < 0) ? -20.0f : 0.0f);
    }
    filtered_error_line = filtered_error_line * 0.6f + raw * 0.4f;
    error_line = filtered_error_line;
}

void setMotorsPWM(int pwm_L, int pwm_R) {
    pwm_L = constrain(pwm_L, -255, 255);
    pwm_R = constrain(pwm_R, -255, 255);
    if (pwm_L >= 0) { analogWrite(L_RPWM, pwm_L); analogWrite(L_LPWM, 0); }
    else             { analogWrite(L_RPWM, 0);     analogWrite(L_LPWM, -pwm_L); }
    if (pwm_R >= 0) { analogWrite(R_RPWM, pwm_R); analogWrite(R_LPWM, 0); }
    else             { analogWrite(R_RPWM, 0);     analogWrite(R_LPWM, -pwm_R); }
}

// ==========================================================
// T-S SMC MASTER - Tuyến tính hóa mờ Takagi-Sugeno
// ----------------------------------------------------------
// Hệ động học (khớp MATLAB):
//   ẋ = w1(x)*A1*x + w2(x)*A2*x + x
//   A1 = [-a  a  0 ; -ζ  c   M ; 0  -M  -b]
//   A2 = [-a  a  0 ; -ζ  c  -M ; 0   M  -b]
//   w1 = (M-x)/(2M),  w2 = (M+x)/(2M)
// Tham số: a=35, b=3, c=28, ζ=7, M=100
// Điều kiện biên: xm0 = [0.15, 0.25, -0.5]
// Bước Euler: dt = 0.001 (1ms)
// ==========================================================
class TSSMCMaster {
private:
    // --- Tham số T-S Lorenz ---
    const float _a    = 35.0f;
    const float _b    =  3.0f;
    const float _c    = 28.0f;
    const float _teta =  7.0f;
    const float _M    = 100.0f;
    const float _dt   =  0.001f;  // 1ms – khớp MATLAB

    // --- Trạng thái Master ---
    float xm, ym, zm;

    // Tính trọng số mờ T-S
    void weights(float x, float &w1, float &w2) {
        float xc = constrain(x, -_M, _M);
        w1 = (_M - xc) / (2.0f * _M);
        w2 = (_M + xc) / (2.0f * _M);
    }

    // Tính đạo hàm theo mô hình T-S
    void fTS(float x, float y, float z, float &fx, float &fy, float &fz) {
        float w1, w2;
        weights(x, w1, w2);
        // A1*[x,y,z]
        float a1x = -_a*x + _a*y;
        float a1y = -_teta*x + _c*y + _M*z;
        float a1z = -_M*y - _b*z;
        // A2*[x,y,z]
        float a2x = -_a*x + _a*y;
        float a2y = -_teta*x + _c*y - _M*z;
        float a2z =  _M*y - _b*z;
        // f = w1*A1*x + w2*A2*x + x  (item "+x" như trong MATLAB)
        fx = w1*a1x + w2*a2x + x;
        fy = w1*a1y + w2*a2y + y;
        fz = w1*a1z + w2*a2z + z;
    }

public:
    TSSMCMaster() : xm(0.15f), ym(0.25f), zm(-0.5f) {}

    // Khởi tạo về điều kiện biên ban đầu (giống MATLAB xm0)
    void init() { xm = 0.15f; ym = 0.25f; zm = -0.5f; }

    // 1 bước Euler tích phân
    void step() {
        float fx, fy, fz;
        fTS(xm, ym, zm, fx, fy, fz);
        xm += fx * _dt;
        ym += fy * _dt;
        zm += fz * _dt;
    }

    // Trộn chaos N bước (100 bước ≈ 100ms giữa các lần phát)
    void advance(int n) {
        for (int i = 0; i < n; i++) step();
    }

    // Lấy khóa mã hóa từ trạng thái xm
    uint8_t getKey() {
        return (uint8_t)((int)(fmod(fabs(xm), 1.0f) * 255.0f));
    }

    // Mã hóa 1 trường + advance 1 bước cho lần tiếp theo
    uint8_t encrypt(int signal) {
        uint8_t key = getKey();
        uint8_t enc = (uint8_t)((signal + (int)key) % 255);
        step();
        return enc;
    }

    void getState(float &x, float &y, float &z) { x = xm; y = ym; z = zm; }
};

// Instance toàn cục
TSSMCMaster master;

// ==========================================================
// 5. HÀM SETUP
// ==========================================================
void setup() {
    Serial.begin(115200);

    // Cấu hình WiFi + quét kênh cho ESP-NOW
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    Serial.println("Dang quet mang ICEA_T3 de dong bo Channel ESP-NOW...");
    int ch = 1, n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i) == "ICEA_T3") {
            ch = WiFi.channel(i);
            Serial.printf("Tim thay 'ICEA_T3' tai Channel: %d\n", ch);
            break;
        }
    }
    WiFi.scanDelete();
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    if (esp_now_init() != ESP_OK) Serial.println("Loi khoi tao ESP-NOW");

    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = ch;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) Serial.println("Loi add peer");

    esp_now_register_recv_cb(OnDataRecv);

    // Cài đặt chân cảm biến, encoder, motor
    pinMode(S1, INPUT); pinMode(S2, INPUT); pinMode(S3, INPUT);
    pinMode(S4, INPUT); pinMode(S5, INPUT);
    pinMode(L_ENC, INPUT_PULLUP); pinMode(R_ENC, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(L_ENC), leftEncoderISR, RISING);
    attachInterrupt(digitalPinToInterrupt(R_ENC), rightEncoderISR, RISING);

    pinMode(L_RPWM, OUTPUT); pinMode(L_LPWM, OUTPUT);
    pinMode(L_REN,  OUTPUT); pinMode(L_LEN,  OUTPUT);
    pinMode(R_RPWM, OUTPUT); pinMode(R_LPWM, OUTPUT);
    pinMode(R_REN,  OUTPUT); pinMode(R_LEN,  OUTPUT);
    digitalWrite(L_REN, HIGH); digitalWrite(L_LEN, HIGH);
    digitalWrite(R_REN, HIGH); digitalWrite(R_LEN, HIGH);

    SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN);
    mfrc522.PCD_Init();

    // Khởi tạo T-S SMC Master về điều kiện biên ban đầu
    master.init();

    Serial.println("{\"type\":\"log\",\"msg\":\"AGV + ESP-NOW + T-S SMC Master Ready!\"}");
}

// ==========================================================
// 6. VÒNG LẶP CHÍNH
// ==========================================================
void loop() {
    // --- KHỐI 1: ĐỌC THẺ RFID ---
    if (agv_state == 0 && mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
        String uid = "";
        for (byte i = 0; i < mfrc522.uid.size; i++) {
            uid += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
            uid += String(mfrc522.uid.uidByte[i], HEX);
        }
        uid.toUpperCase();
        if (uid == "98B1F3E3") {
            agv_state = 1; stop_start_time = millis();
            posX = 0; posY = 0; theta = 0;
        } else if (uid == "985BC7E3") {
            agv_state = 2;
            posX = 0; posY = 0; theta = 0;
        }
        mfrc522.PICC_HaltA();
    }

    // --- KHỐI 2: ĐIỀU KHIỂN & GỬI DỮ LIỆU (20ms) ---
    unsigned long now = millis();
    if (now - previousMillis >= interval) {
        previousMillis = now;

        noInterrupts();
        actual_vel_L = left_pulses; actual_vel_R = right_pulses;
        left_pulses  = 0;          right_pulses  = 0;
        interrupts();

        // Odometry
        float dL = (actual_vel_L / PULSES_PER_REV) * (2.0f * PI * WHEEL_RADIUS);
        float dR = (actual_vel_R / PULSES_PER_REV) * (2.0f * PI * WHEEL_RADIUS);
        if (target_vel_L < 0) dL = -dL;
        if (target_vel_R < 0) dR = -dR;
        float dc    = (dL + dR) / 2.0f;
        float dth   = (dR - dL) / WHEEL_BASE;
        posX  += dc * cosf(theta + dth / 2.0f);
        posY  += dc * sinf(theta + dth / 2.0f);
        theta += dth;

        // Quản lý trạng thái
        if      (agv_state == 1) {
            if (millis() - stop_start_time >= PAUSE_TIME) agv_state = 0;
            else base_speed = 0;
        }
        else if (agv_state == 2) base_speed = 0;
        else                     base_speed = base_speed_config;

        if (base_speed == 0) { integral_vel_L = 0; integral_vel_R = 0; }

        // Dò line PD
        calculateLineError();
        float deriv    = error_line - previous_filtered_error_line;
        float turn     = Kp_line * error_line + Kd_line * deriv;
        previous_filtered_error_line = error_line;

        if (base_speed == 0) turn = 0;
        turn = constrain(turn, -(float)base_speed * 0.7f, (float)base_speed * 0.7f);

        int adap = base_speed;
        if (base_speed > 0) {
            float sr = min(0.4f, fabsf(error_line) / 30.0f);
            adap = max(12, (int)(base_speed * (1.0f - sr)));
        }

        target_vel_L = (base_speed == 0) ? 0 : max((long)(adap * 0.3), adap + (long)turn);
        target_vel_R = (base_speed == 0) ? 0 : max((long)(adap * 0.3), adap - (long)turn);

        // PI tốc độ
        float eL = target_vel_L - actual_vel_L;
        integral_vel_L = constrain(integral_vel_L + eL, -500.0f, 500.0f);
        int pwmL = (int)(Kp_vel * eL + Ki_vel * integral_vel_L);

        float eR = target_vel_R - actual_vel_R;
        integral_vel_R = constrain(integral_vel_R + eR, -500.0f, 500.0f);
        int pwmR = (int)(Kp_vel * eR + Ki_vel * integral_vel_R);

        setMotorsPWM(pwmL, pwmR);

        // --- GỬI QUA ESP-NOW (T-S SMC Chaos Encryption, 100ms) ---
        static unsigned long lastSend = 0;
        if (now - lastSend >= 100) {
            lastSend = now;

            speedData.v_left  = (int)actual_vel_L;
            speedData.v_right = (int)actual_vel_R;
            speedData.state   = agv_state;
            speedData.x       = posX;
            speedData.y       = posY;
            speedData.theta   = theta;

            // ===================================================
            // TRỘN CHAOS: advance 100 bước T-S Lorenz (= 100ms)
            // Đồng bộ với bộ thu: cả 2 phía advance đúng 100 bước
            // trước mỗi lần mã hóa / giải mã gói tin.
            // ===================================================
            master.advance(100);

            // Chuẩn hóa về [0, 254] trước khi mã hóa
            int nVL = constrain((int)(speedData.v_left  + 128), 0, 254);
            int nVR = constrain((int)(speedData.v_right + 128), 0, 254);
            int nSt = constrain(speedData.state * 50,            0, 254);
            int nX  = constrain((int)speedData.x + 128,          0, 254);
            int nY  = constrain((int)speedData.y + 128,          0, 254);
            int nTh = constrain((int)((speedData.theta / PI) * 127.0f + 128.0f), 0, 254);

            // ===================================================
            // MÃ HÓA T-S SMC: mỗi encrypt() dùng key(xm) hiện tại
            // rồi advance thêm 1 bước → 6 trường = 6 bước liên tiếp
            // Bộ thu phải decrypt theo đúng thứ tự và số bước này.
            // ===================================================
            encryptedData.enc_v_left  = master.encrypt(nVL);
            encryptedData.enc_v_right = master.encrypt(nVR);
            encryptedData.enc_state   = master.encrypt(nSt);
            encryptedData.enc_x       = master.encrypt(nX);
            encryptedData.enc_y       = master.encrypt(nY);
            encryptedData.enc_theta   = master.encrypt(nTh);

            // Checksum XOR
            encryptedData.checksum =
                encryptedData.enc_v_left  ^ encryptedData.enc_v_right ^
                encryptedData.enc_state   ^ encryptedData.enc_x ^
                encryptedData.enc_y       ^ encryptedData.enc_theta;

            esp_now_send(broadcastAddress, (uint8_t*)&encryptedData, sizeof(encryptedData));

            // Debug Serial (in thưa 1s)
            static unsigned long lastDbg = 0;
            if (now - lastDbg >= 1000) {
                lastDbg = now;
                float xm, ym, zm;
                master.getState(xm, ym, zm);
                Serial.printf("{\"type\":\"ts_master\",\"xm\":%.3f,\"ym\":%.3f,\"vL\":%d,\"vR\":%d,\"state\":%d}\n",
                              xm, ym, speedData.v_left, speedData.v_right, speedData.state);
            }
        }
    }
}