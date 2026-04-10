// ==========================================================
// 1. KHAI BÁO THƯ VIỆN VÀ ĐỊNH NGHĨA CHÂN
// ==========================================================
#include <SPI.h>
#include <MFRC522.h>

// --- Định nghĩa chân RC522 (Giao tiếp SPI) ---
#define RST_PIN   35    
#define SS_PIN    32    
#define SCK_PIN   33
#define MISO_PIN  22
#define MOSI_PIN  23
MFRC522 mfrc522(SS_PIN, RST_PIN); 


// --- Mạch cầu H (BTS7960) ---
const int L_RPWM = 13; const int L_REN = 12; const int L_LEN = 14; const int L_LPWM = 27; // Trái
const int R_RPWM = 15; const int R_REN = 16; const int R_LEN = 4;  const int R_LPWM = 2;  // Phải

// --- Encoder ---
const int L_ENC = 26; 
const int R_ENC = 25; 
// --- Cảm biến TCRT5000 ---
const int S1 = 5;
const int S2 = 17;
const int S3 = 18;
const int S4 = 21;
const int S5 = 19;

// ==========================================================
// 2. BIẾN TOÀN CỤC & HỆ SỐ ĐIỀU KHIỂN
// ==========================================================
// --- Máy trạng thái (RFID State Machine) ---
int agv_state = 0; // 0: Chạy, 1: Dừng 5s, 2: Dừng hẳn
unsigned long stop_start_time = 0;
const int PAUSE_TIME = 5000; 

unsigned long turn_start_time = 0;
const int TURN_TIME = 600; // Thời gian ép rẽ phải (600 mili-giây).

// --- Vòng ngoài (Dò Line PD) ---
float Kp_line = 0.8;  // Đã scale lại cho phù hợp tốc độ
float Ki_line = 0;    // Không dùng khâu I cho dò line
float Kd_line = 3.0;  // Khâu D làm phanh giảm xóc (chống zigzag)
float error_line = 0;
float previous_error_line = 0;

int base_speed_config = 18; // Tốc độ nền mặc định
int base_speed = 18;        // Biến tốc độ thực tế (thay đổi theo thẻ)

// --- Vòng trong (Tốc độ PI) ---
float Kp_vel = 5.0; 
float Ki_vel = 0.8;
float integral_vel_L = 0, integral_vel_R = 0;

volatile long left_pulses = 0;
volatile long right_pulses = 0;
long actual_vel_L = 0, actual_vel_R = 0;
long target_vel_L = 0, target_vel_R = 0;

// --- Chu kỳ trích mẫu ---
unsigned long previousMillis = 0;
const int interval = 20; // CHÚ Ý: Đã sửa thành 20ms (Số nguyên)

// ==========================================================
// 3. CÁC HÀM NGẮT & HÀM ĐIỀU KHIỂN
// ==========================================================
void IRAM_ATTR leftEncoderISR() { left_pulses++; } 
void IRAM_ATTR rightEncoderISR() { right_pulses++; }

void calculateLineError() {
  int val1 = !digitalRead(S1);
  int val2 = !digitalRead(S2);
  int val3 = !digitalRead(S3);
  int val4 = !digitalRead(S4);
  int val5 = !digitalRead(S5);

  int sum = val1 + val2 + val3 + val4 + val5;
  
  if (sum != 0) {
    error_line = (val1 * -20 + val2 * -10 + val3 * 0 + val4 * 10 + val5 * 20) / sum;
  } else {
    // Tự tìm lại line nếu bị văng
    if (previous_error_line > 0) error_line = 30; 
    else if (previous_error_line < 0) error_line = -30; 
  }
}

void setMotorsPWM(int pwm_L, int pwm_R) {
  pwm_L = constrain(pwm_L, 0, 255);
  pwm_R = constrain(pwm_R, 0, 255);

  if (pwm_L >= 0) { analogWrite(L_RPWM, pwm_L); analogWrite(L_LPWM, 0); } 
  else { analogWrite(L_RPWM, 0); analogWrite(L_LPWM, -pwm_L); }

  if (pwm_R >= 0) { analogWrite(R_RPWM, pwm_R); analogWrite(R_LPWM, 0); } 
  else { analogWrite(R_RPWM, 0); analogWrite(R_LPWM, -pwm_R); }
}


// 4. HÀM SETUP

void setup() {
  Serial.begin(115200);

  pinMode(S1, INPUT); pinMode(S2, INPUT); pinMode(S3, INPUT);
  pinMode(S4, INPUT); pinMode(S5, INPUT);

  pinMode(L_ENC, INPUT_PULLUP); pinMode(R_ENC, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(L_ENC), leftEncoderISR, RISING);
  attachInterrupt(digitalPinToInterrupt(R_ENC), rightEncoderISR, RISING);

  pinMode(L_RPWM, OUTPUT); pinMode(L_LPWM, OUTPUT);
  pinMode(L_REN, OUTPUT); pinMode(L_LEN, OUTPUT);
  pinMode(R_RPWM, OUTPUT); pinMode(R_LPWM, OUTPUT);
  pinMode(R_REN, OUTPUT); pinMode(R_LEN, OUTPUT);

  digitalWrite(L_REN, HIGH); digitalWrite(L_LEN, HIGH);
  digitalWrite(R_REN, HIGH); digitalWrite(R_LEN, HIGH);

  SPI.begin(SCK_PIN, MISO_PIN, MOSI_PIN, SS_PIN); 
  mfrc522.PCD_Init();
  mfrc522.PCD_DumpVersionToSerial();
  Serial.println("He thong AGV da san sang!");
}


// 5. VÒNG LẶP CHÍNH

void loop() {

  // KHỐI 1: XỬ LÝ ĐỌC THẺ RFID
 
  if (agv_state == 0 && mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    String card_UID = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      card_UID += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
      card_UID += String(mfrc522.uid.uidByte[i], HEX);
    }
    card_UID.toUpperCase(); 

    // In ra Serial để kiểm tra mã thẻ thực tế
    Serial.print("Da quet the: ["); Serial.print(card_UID); Serial.println("]");

    if (card_UID == "98B1F3E3") {
      agv_state = 1; 
      stop_start_time = millis(); 
      Serial.println("-> LENH: Dung 5 giay");
    } 
   
    else if (card_UID == "985BC7E3") {
      agv_state = 2; 
      Serial.println("-> LENH: Dung han");
    }
    mfrc522.PICC_HaltA(); 
  }


  // KHỐI 2: ĐIỀU KHIỂN ĐỘNG CƠ (Chu kỳ 20ms)

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    // --- BƯỚC 1: Đo tốc độ Encoder ---
    noInterrupts(); 
    actual_vel_L = left_pulses; actual_vel_R = right_pulses;
    left_pulses = 0; right_pulses = 0;
    interrupts();

    // --- BƯỚC 2: Cập nhật trạng thái xe ---
    if (agv_state == 1) { 
      if (millis() - stop_start_time >= PAUSE_TIME) {
        agv_state = 0; Serial.println("Het 5s. Chay tiep!");
      } else {
        base_speed = 0; 
      }
    } 
    else if (agv_state == 2) { base_speed = 0; } 
    else { base_speed = base_speed_config; }

    if (base_speed == 0) {
       integral_vel_L = 0; integral_vel_R = 0; // Xóa rác khâu I
    }

    // --- BƯỚC 3: Dò Line (PD) ---
    calculateLineError();
    float derivative_line = error_line - previous_error_line;
    float turn_speed = (Kp_line * error_line) + (Kd_line * derivative_line);
    previous_error_line = error_line;

    // Khóa vô lăng khi xe đỗ
    if (base_speed == 0) turn_speed = 0;

    // Thuật toán ôm cua mượt (Tránh dừng bánh)
    float max_turn = base_speed * 0.7; 
    turn_speed = constrain(turn_speed, -max_turn, max_turn);
    long min_speed = base_speed * 0.3;

    // Setpoint vòng ngoài
    target_vel_L = max(min_speed, base_speed + (long)turn_speed);
    target_vel_R = max(min_speed, base_speed - (long)turn_speed);

    // Ép tốc độ về 0 hoàn toàn nếu agv_state yêu cầu dừng
    if (base_speed == 0) {
      target_vel_L = 0; target_vel_R = 0;
    }

    // --- BƯỚC 4: Tốc độ (PI) ---
    float error_vel_L = target_vel_L - actual_vel_L;
    integral_vel_L += error_vel_L;     
    integral_vel_L = constrain(integral_vel_L, -500, 500);
    int pwm_out_L = (Kp_vel * error_vel_L) + (Ki_vel * integral_vel_L);

    float error_vel_R = target_vel_R - actual_vel_R;
    integral_vel_R += error_vel_R;
    integral_vel_R = constrain(integral_vel_R, -500, 500);
    int pwm_out_R = (Kp_vel * error_vel_R) + (Ki_vel * integral_vel_R);

    // --- BƯỚC 5: Xuất tín hiệu PWM ---
    setMotorsPWM(pwm_out_L, pwm_out_R);

    // --- In thông số test (Mở Serial Monitor để xem) ---
     //Bỏ comment đoạn này nếu muốn xem đồ thị tốc độ
    //Serial.print("Set_L:"); Serial.print(target_vel_L); Serial.print(",");
    //Serial.print("Act_L:"); Serial.print(actual_vel_L); Serial.print(",");
    //Serial.print("Set_R:"); Serial.print(target_vel_R); Serial.print(",");
    //Serial.print("Act_R:"); Serial.println(actual_vel_R);
    
  }
}