# 🐳 Docker Setup Guide

## Chuẩn Bị

### Yêu Cầu
- **Docker Desktop** cài đặt (tải từ https://www.docker.com/products/docker-desktop)
- **Docker Desktop đang chạy** trước khi execute commands

### Kiểm Tra
```bash
docker --version
docker-compose --version
```

---

## 🚀 Quick Start

### **1. Build Image**
```bash
cd c:\Users\truon\.vscode\Webcode
docker-compose build
```
**Output mong đợi:**
```
Building agv-server
...
Successfully tagged webcode_agv-server:latest
```

### **2. Chạy Container**
```bash
docker-compose up
```
**Output mong đợi:**
```
Creating agv-websocket-server ... done
Attaching to agv-websocket-server
agv-websocket-server | [INFO] Dashboard server running on port 3000
```

### **3. Mở Dashboard**
```
http://localhost:3000
```

### **4. Dừng Container**
```bash
docker-compose down
```

---

## 📋 Commands

| Command | Mục đích |
|---------|---------|
| `docker-compose up` | Khởi động services |
| `docker-compose up -d` | Khởi động ở background |
| `docker-compose down` | Dừng + xóa containers |
| `docker-compose logs -f` | Xem logs real-time |
| `docker-compose build --no-cache` | Build lại từ đầu |
| `docker-compose ps` | Liệt kê running containers |
| `docker-compose restart` | Restart services |

---

## 🔍 Monitoring

### Xem Logs
```bash
docker-compose logs -f agv-server
```

### Kiểm Tra Health
```bash
docker-compose ps
```
Sẽ thấy status: `Up (healthy)` ✅

### Access Container
```bash
docker-compose exec agv-server sh
# Bây giờ bạn ở trong container
ls /app
```

---

## 🔧 Configuration

### Environment Variables (docker-compose.yml)
```yaml
environment:
  - NODE_ENV=production
  - PORT=3000
  - AGV_SERIAL_PORT=COM3
  - AGV_BAUD_RATE=115200
```

Để thay đổi:
1. Edit `docker-compose.yml`
2. `docker-compose down`
3. `docker-compose up`

---

## ⚠️ Troubleshooting

### **Port 3000 đang sử dụng**
```bash
# Windows: Kill process on port 3000
netstat -ano | findstr :3000
taskkill /PID <PID> /F
```

### **Container exits immediately**
```bash
docker-compose logs agv-server
```
Kiểm tra logs để xem lỗi

### **Build failure**
```bash
docker-compose build --no-cache
```

### **Permission denied**
- Windows: Chạy PowerShell as Administrator
- Linux/Mac: Thêm `sudo` trước commands

---

## 📊 System Resources

Container này sử dụng:
- **RAM**: ~80 MB (Node.js + dependencies)
- **CPU**: <5% (idle), ~10% (active)
- **Disk**: ~200 MB (image) + ~100 MB (node_modules)

---

## 🎯 Production Tips

### 1. Run in Background
```bash
docker-compose up -d
```

### 2. Auto Restart
```yaml
restart: unless-stopped  # Tự động restart nếu crashed
```

### 3. View Logs Later
```bash
docker-compose logs agv-server | tail -50
```

### 4. Update Code
```bash
# Edit code
# Rebuild
docker-compose build --no-cache
docker-compose down
docker-compose up
```

---

## 📁 Volume Mounts

```yaml
volumes:
  - ./public:/app/public      # Dashboard files
  - ./logs:/app/logs          # Log files (nếu tạo)
```

Tất cả thay đổi trong thư mục này đều reflect vào container.

---

## 🔐 Security Notes

- Container chạy với non-root user (nodejs:1001)
- Health check mỗi 30s
- Signal handling được cấu hình (dumb-init)
- Production-ready Dockerfile

---

**Bây giờ bạn có thể khởi động project với:**
```bash
docker-compose up
```

🎉
