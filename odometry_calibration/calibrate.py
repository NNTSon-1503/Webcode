import numpy as np
from scipy.optimize import minimize
import math
import os

# ==========================================
# THÔNG SỐ XE CỦA BẠN (Cần cấu hình lại cho đúng)
# ==========================================
WHEEL_RADIUS = 0.033  # Bán kính bánh (m)
WHEEL_BASE = 0.160    # Khoảng cách 2 bánh (m)
ENCODER_RES = 360     # Số xung / vòng
TICK_TO_M = (2 * math.pi * WHEEL_RADIUS) / ENCODER_RES

def simulate_journey(fop_data, bop_data, Ed, Eb):
    x, y, theta = 0.0, 0.0, 0.0
    
    def run_path(data, cur_x, cur_y, cur_theta):
        for tick_L, tick_R in data:
            dL = tick_L * TICK_TO_M
            dR = tick_R * TICK_TO_M * Ed 
            
            d_center = (dL + dR) / 2.0
            d_theta = (dR - dL) / (WHEEL_BASE * Eb)
            
            cur_theta += d_theta
            cur_x += d_center * math.cos(cur_theta)
            cur_y += d_center * math.sin(cur_theta)
        return cur_x, cur_y, cur_theta

    # Lượt đi
    x, y, theta = run_path(fop_data, x, y, theta)
    
    # Quay đầu 180 độ
    theta += math.pi 
    
    # Lượt về
    x, y, theta = run_path(bop_data, x, y, theta)
    
    return x, y, theta

def loss_function(params, fop_data, bop_data):
    Ed, Eb = params
    final_x, final_y, final_theta = simulate_journey(fop_data, bop_data, Ed, Eb)
    # Hàm mục tiêu để đưa final_x, final_y về 0
    error = (final_x**2) + (final_y**2) + 0.1 * ((final_theta - math.pi)**2)
    return error

if __name__ == "__main__":
    current_dir = os.path.dirname(os.path.abspath(__file__))
    try:
        fop_data = np.loadtxt(os.path.join(current_dir, 'fop.csv'), delimiter=',') 
        bop_data = np.loadtxt(os.path.join(current_dir, 'bop.csv'), delimiter=',')
        print("Đã tải dữ liệu thực tế thành công.")
    except Exception as e:
        print("Không tìm thấy/Lỗi đọc file csv, sử dụng dữ liệu giả lập mẫu.")
        fop_data = np.array([[10, 10.2]] * 50 + [[8, 12]] * 20)
        bop_data = np.array([[10.2, 10]] * 50 + [[12, 8]] * 20)
    
    initial_guess = [1.0, 1.0]
    
    print("Đang chạy tối ưu hóa (Nelder-Mead)...")
    result = minimize(loss_function, initial_guess, args=(fop_data, bop_data), method='Nelder-Mead')
    
    best_Ed, best_Eb = result.x
    print("\n--- KẾT QUẢ ĐÃ TÌM THẤY ---")
    print(f"Hệ số sai lệch bánh xe (Ed) = {best_Ed:.6f}")
    print(f"Hệ số sai lệch trục xe (Eb) = {best_Eb:.6f}")
