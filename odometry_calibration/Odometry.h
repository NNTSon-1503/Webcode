#ifndef ODOMETRY_H
#define ODOMETRY_H

#include <math.h>

/*
 * Thư viện Odometry có hỗ trợ áp dụng hệ số hiệu chuẩn
 * Tạo ra cho xe Differential Drive sau khi đã chạy Python calibrate.py
 */

class Odometry {
private:
    float x;
    float y;
    float theta;
    
    // Thống số cơ bản
    float wheel_radius;
    float wheel_base;
    float tick_to_meter;
    
    // HỆ SỐ HIỆU CHỈNH (Mặc định là 1.0 nếu chưa hiệu chuẩn)
    float E_d; 
    float E_b;

public:
    Odometry(float radius, float base, int encoder_res, float ed = 1.0, float eb = 1.0) {
        x = 0.0;
        y = 0.0;
        theta = 0.0;
        wheel_radius = radius;
        wheel_base = base;
        tick_to_meter = (2 * M_PI * wheel_radius) / encoder_res;
        E_d = ed;
        E_b = eb;
    }

    // Hàm gọi trong ngắt để cập nhật tọa độ
    void update(long delta_tick_L, long delta_tick_R) {
        // 1. Áp dụng hệ số hiệu chỉnh Ed cho bánh phải
        float dL = delta_tick_L * tick_to_meter;
        float dR = delta_tick_R * tick_to_meter * E_d;

        float d_center = (dL + dR) / 2.0;
        
        // 2. Áp dụng hệ số hiệu chỉnh Eb cho chiều dài trục
        float d_theta = (dR - dL) / (wheel_base * E_b);

        theta += d_theta;
        x += d_center * cos(theta);
        y += d_center * sin(theta);
    }
    
    // Lấy tọa độ hiện tại
    float getX() { return x; }
    float getY() { return y; }
    float getTheta() { return theta; }
    
    // Reset hệ tọa độ
    void reset(float new_x = 0, float new_y = 0, float new_theta = 0) {
        x = new_x;
        y = new_y;
        theta = new_theta;
    }
};

#endif
