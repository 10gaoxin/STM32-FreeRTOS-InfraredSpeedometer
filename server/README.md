# 服务器端使用说明

## 概述

本项目提供了两种服务器实现：
1. **Python版本** - 简单易用，适合快速测试
2. **Node.js版本** - 高性能，适合生产环境

两种服务器都提供：
- TCP服务器（端口8080）- 接收STM32数据
- Web服务器（端口8000）- 提供可视化界面
- RESTful API - 供前端调用

## Python服务器

### 环境要求

- Python 3.6 或更高版本
- 无需额外依赖（使用标准库）

### 安装步骤

1. **检查Python版本**
```bash
python --version
# 或
python3 --version
```

2. **无需安装依赖**
   - Python服务器使用标准库，无需安装额外包

### 使用方法

#### 方法1：简单TCP服务器

适合只需要接收数据，不需要Web界面的场景。

```bash
cd server
python tcp_server.py
```

输出示例：
```
============================================================
红外测速系统 - TCP服务器
============================================================
服务器启动成功！
监听地址: 0.0.0.0:8080
等待STM32连接...
============================================================

[2026-01-28 10:30:45] 新连接: 192.168.1.100:12345
------------------------------------------------------------
时间: 2026-01-28 10:30:50
来源: 192.168.1.100:12345
速度: 1.23 m/s  |  4.43 km/h
通过时间: 81300 μs
------------------------------------------------------------
```

**功能：**
- 接收并显示测速数据
- 自动保存到 `speed_data.log` 文件
- 支持多个STM32同时连接

#### 方法2：Web服务器（推荐）

提供完整的Web可视化界面。

```bash
cd server
python web_server.py
```

输出示例：
```
============================================================
红外测速系统 - Web服务器
============================================================
TCP服务器: 0.0.0.0:8080 (接收STM32数据)
Web服务器: http://localhost:8000 (查看可视化界面)
============================================================
按 Ctrl+C 停止服务器

TCP服务器启动: 0.0.0.0:8080
STM32连接: 192.168.1.100:12345
收到数据: 4.43 km/h
```

**访问Web界面：**
1. 打开浏览器
2. 访问 `http://localhost:8000`
3. 查看实时数据和历史记录

**功能：**
- 实时显示当前速度
- 统计信息（最高/最低/平均速度）
- 历史记录表格
- 自动刷新（每2秒）

### API接口

#### 1. 获取当前速度
```
GET /api/current
```

响应示例：
```json
{
  "speed_ms": 1.23,
  "speed_kmh": 4.43,
  "time_us": 81300,
  "timestamp": "2026-01-28 10:30:50",
  "source": "192.168.1.100:12345"
}
```

#### 2. 获取历史记录
```
GET /api/history?count=20
```

参数：
- `count` - 返回记录数量（默认20）

响应示例：
```json
[
  {
    "speed_ms": 1.23,
    "speed_kmh": 4.43,
    "time_us": 81300,
    "timestamp": "2026-01-28 10:30:50",
    "source": "192.168.1.100:12345"
  },
  ...
]
```

#### 3. 获取统计信息
```
GET /api/statistics
```

响应示例：
```json
{
  "count": 50,
  "max": 12.5,
  "min": 2.3,
  "avg": 6.8
}
```

---

## Node.js服务器

### 环境要求

- Node.js 12.0 或更高版本
- npm（Node包管理器）

### 安装步骤

1. **安装Node.js**
   - 访问 https://nodejs.org/
   - 下载并安装LTS版本

2. **检查安装**
```bash
node --version
npm --version
```

3. **安装依赖**
```bash
cd server
npm install
```

这将安装以下依赖：
- `express` - Web框架
- `ws` - WebSocket支持（实时推送）
- `cors` - 跨域支持

### 使用方法

```bash
cd server
node server.js
```

输出示例：
```
============================================================
红外测速系统 - Node.js服务器
============================================================
TCP服务器: 0.0.0.0:8080 (接收STM32数据)
Web服务器: http://localhost:8000 (查看可视化界面)
WebSocket: ws://localhost:8000 (实时推送)
============================================================

STM32连接: 192.168.1.100:12345
收到数据: 4.43 km/h
```

**访问Web界面：**
- 浏览器访问 `http://localhost:8000`

**功能：**
- 所有Python版本的功能
- WebSocket实时推送（无需轮询）
- 更高的并发性能
- 更好的错误处理

---

## 配置说明

### 修改端口

#### Python服务器

编辑 `web_server.py`：

```python
# TCP端口
tcp_server = TCPServerThread(data_manager, host='0.0.0.0', port=8080)

# Web端口
http_server = HTTPServer(('0.0.0.0', 8000), APIHandler)
```

#### Node.js服务器

编辑 `server.js`：

```javascript
const TCP_PORT = 8080;  // TCP端口
const WEB_PORT = 8000;  // Web端口
```

### 修改数据保存

#### Python服务器

编辑 `tcp_server.py` 中的 `save_to_file` 方法：

```python
def save_to_file(self, data):
    """保存数据到文件"""
    try:
        with open('speed_data.log', 'a', encoding='utf-8') as f:
            f.write(json.dumps(data, ensure_ascii=False) + '\n')
    except Exception as e:
        print(f"保存文件失败: {e}")
```

可以修改文件名或保存格式。

---

## 部署到云服务器

### 1. 准备云服务器

推荐使用：
- 阿里云ECS
- 腾讯云CVM
- AWS EC2
- 或其他云服务商

**最低配置：**
- CPU: 1核
- 内存: 1GB
- 带宽: 1Mbps
- 系统: Ubuntu 20.04 或 CentOS 7

### 2. 安装环境

#### Ubuntu/Debian

```bash
# 更新系统
sudo apt update
sudo apt upgrade -y

# 安装Python3（通常已预装）
sudo apt install python3 python3-pip -y

# 或安装Node.js
curl -fsSL https://deb.nodesource.com/setup_lts.x | sudo -E bash -
sudo apt install nodejs -y
```

#### CentOS/RHEL

```bash
# 更新系统
sudo yum update -y

# 安装Python3
sudo yum install python3 python3-pip -y

# 或安装Node.js
curl -fsSL https://rpm.nodesource.com/setup_lts.x | sudo bash -
sudo yum install nodejs -y
```

### 3. 上传代码

```bash
# 使用scp上传
scp -r server/ user@your-server-ip:/home/user/

# 或使用git
git clone <your-repository>
```

### 4. 运行服务器

#### 使用screen（推荐）

```bash
# 安装screen
sudo apt install screen  # Ubuntu
sudo yum install screen  # CentOS

# 创建新会话
screen -S speedometer

# 运行服务器
cd server
python3 web_server.py

# 按 Ctrl+A 然后按 D 退出会话（服务器继续运行）

# 重新连接会话
screen -r speedometer
```

#### 使用systemd（生产环境推荐）

创建服务文件 `/etc/systemd/system/speedometer.service`：

```ini
[Unit]
Description=Infrared Speedometer Server
After=network.target

[Service]
Type=simple
User=your-username
WorkingDirectory=/home/your-username/server
ExecStart=/usr/bin/python3 /home/your-username/server/web_server.py
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

启动服务：

```bash
sudo systemctl daemon-reload
sudo systemctl start speedometer
sudo systemctl enable speedometer  # 开机自启

# 查看状态
sudo systemctl status speedometer

# 查看日志
sudo journalctl -u speedometer -f
```

### 5. 配置防火墙

```bash
# Ubuntu (ufw)
sudo ufw allow 8080/tcp
sudo ufw allow 8000/tcp

# CentOS (firewalld)
sudo firewall-cmd --permanent --add-port=8080/tcp
sudo firewall-cmd --permanent --add-port=8000/tcp
sudo firewall-cmd --reload
```

### 6. 配置域名（可选）

如果有域名，可以配置Nginx反向代理：

```bash
# 装Nginx
sudo apt install nginx  # Ubuntu
sudo yum install nginx  # CentOS

# 创建配置文件
sudo nano /etc/nginx/sites-available/speedometer
```

配置内容：

```nginx
server {
    listen 80;
    server_name your-domain.com;

    location / {
        proxy_pass http://localhost:8000;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection 'upgrade';
        proxy_set_header Host $host;
        proxy_cache_bypass $http_upgrade;
    }
}
```

启用配置：

```bash
sudo ln -s /etc/nginx/sites-available/speedometer /etc/nginx/sites-enabled/
sudo nginx -t
sudo systemctl restart nginx
```

---

## 故障排查

### 问题1：端口被占用

**错误信息：**
```
OSError: [Errno 98] Address already in use
```

**解决方法：**
```bash
# 查找占用端口的进程
sudo lsof -i :8080
sudo lsof -i :8000

# 杀死进程
sudo kill -9 <PID>

# 或修改服务器端口
```

### 问题2：无法访问Web界面

**检查项：**
1. 服务器是否正常运行
2. 防火墙是否开放端口
3. 云服务器安全组是否配置
4. 浏览器是否输入正确地址

### 问题3：STM32无法连接

**检查项：**
1. ESP8266配置的服务器IP是否正确
2. 服务器TCP端口是否正确（8080）
3. STM32和服务器是否在同一网络
4. 防火墙是否阻止连接

### 问题4：数据不更新

**检查项：**
1. STM32是否正常发送数据
2. 服务器是否收到数据（查看日志）
3. 浏览器是否禁用JavaScript
4. 清除浏览器缓存

---

## 性能优化

### 1. 数据库存储

对于长期运行，建议使用数据库存储数据：

**SQLite（简单）：**
```python
import sqlite3

conn = sqlite3.connect('speedometer.db')
cursor = conn.cursor()

cursor.execute('''
    CREATE TABLE IF NOT EXISTS speed_data (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        timestamp TEXT,
        speed_ms REAL,
        speed_kmh REAL,
        time_us INTEGER,
        source TEXT
    )
''')
```

**MySQL/PostgreSQL（生产环境）：**
- 更好的性能
- 支持更多并发
- 更强大的查询功能

### 2. 数据压缩

对于大量历史数据，可以定期压缩：

```python
import gzip
import shutil

# 压缩日志文件
with open('speed_data.log', 'rb') as f_in:
    with gzip.open('speed_data.log.gz', 'wb') as f_out:
        shutil.copyfileobj(f_in, f_out)
```

### 3. 缓存优化

使用Redis缓存热点数据：

```python
import redis

r = redis.Redis(host='localhost', port=6379, db=0)
r.set('current_speed', json.dumps(speed_data))
```

---

## 下一步

服务器配置完成后，可以：
1. 修改STM32中的服务器IP地址
2. 重新编译下载程序
3. 测试数据上传
4. 访问Web界面查看数据
5. 根据需求定制界面和功能

---

最后更新：2026-01-28
