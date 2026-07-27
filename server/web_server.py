#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
红外测速系统 - Web服务器
提供Web界面实时显示测速数据
"""

import socket
import json
import threading
import time
from datetime import datetime
from http.server import HTTPServer, SimpleHTTPRequestHandler
import os

class SpeedDataManager:
    """测速数据管理器"""
    def __init__(self):
        self.speed_history = []
        self.max_history = 100
        self.current_speed = None
        self.lock = threading.Lock()
        self.log_file = 'speed_data.log'

        # 从文件加载历史数据
        self.load_from_file()

    def load_from_file(self):
        """从日志文件加载历史数据"""
        try:
            if os.path.exists(self.log_file):
                with open(self.log_file, 'r', encoding='utf-8') as f:
                    for line in f:
                        line = line.strip()
                        if line:
                            try:
                                data = json.loads(line)
                                self.speed_history.append(data)
                            except:
                                pass
                # 只保留最近的max_history条记录
                if len(self.speed_history) > self.max_history:
                    self.speed_history = self.speed_history[-self.max_history:]

                # 设置最新的数据为当前速度
                if self.speed_history:
                    self.current_speed = self.speed_history[-1]
                    print(f"从文件加载了 {len(self.speed_history)} 条历史记录")
        except Exception as e:
            print(f"加载历史数据失败: {e}")

    def add_data(self, data):
        """添加测速数据"""
        with self.lock:
            self.current_speed = data
            self.speed_history.append(data)
            if len(self.speed_history) > self.max_history:
                self.speed_history.pop(0)

            # 保存到文件
            self.save_to_file(data)

    def save_to_file(self, data):
        """保存数据到文件"""
        try:
            with open(self.log_file, 'a', encoding='utf-8') as f:
                f.write(json.dumps(data, ensure_ascii=False) + '\n')
        except Exception as e:
            print(f"保存文件失败: {e}")

    def get_current(self):
        """获取当前速度"""
        with self.lock:
            return self.current_speed

    def get_history(self, count=20):
        """获取历史数据"""
        with self.lock:
            return self.speed_history[-count:]

    def get_statistics(self):
        """获取统计信息"""
        with self.lock:
            if not self.speed_history:
                return None

            speeds = [d['speed_kmh'] for d in self.speed_history]
            return {
                'count': len(speeds),
                'max': max(speeds),
                'min': min(speeds),
                'avg': sum(speeds) / len(speeds)
            }

class TCPServerThread(threading.Thread):
    """TCP服务器线程"""
    def __init__(self, data_manager, host='0.0.0.0', port=8080):
        super().__init__()
        self.data_manager = data_manager
        self.host = host
        self.port = port
        self.running = False
        self.daemon = True

    def run(self):
        """运行TCP服务器"""
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((self.host, self.port))
        server.listen(5)
        self.running = True

        print(f"TCP服务器启动: {self.host}:{self.port}")

        while self.running:
            try:
                client, addr = server.accept()
                print(f"STM32连接: {addr[0]}:{addr[1]}")

                client_thread = threading.Thread(
                    target=self.handle_client,
                    args=(client, addr)
                )
                client_thread.daemon = True
                client_thread.start()

            except Exception as e:
                print(f"TCP错误: {e}")

    def handle_client(self, client, addr):
        """处理客户端"""
        try:
            while self.running:
                data = client.recv(1024)
                if not data:
                    break

                try:
                    data_str = data.decode('utf-8').strip()
                    speed_data = json.loads(data_str)

                    # 添加元数据
                    speed_data['timestamp'] = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
                    speed_data['source'] = f"{addr[0]}:{addr[1]}"

                    # 保存数据
                    self.data_manager.add_data(speed_data)

                    print(f"收到数据: {speed_data['speed_kmh']:.2f} km/h")

                except Exception as e:
                    print(f"数据解析错误: {e}")

        except Exception as e:
            print(f"客户端错误: {e}")
        finally:
            print(f"STM32断开: {addr[0]}:{addr[1]}")
            client.close()

class APIHandler(SimpleHTTPRequestHandler):
    """HTTP API处理器"""
    data_manager = None

    def do_GET(self):
        """处理GET请求"""
        if self.path == '/api/current':
            self.send_json_response(self.data_manager.get_current())

        elif self.path.startswith('/api/history'):
            count = 20
            if '?' in self.path:
                params = self.path.split('?')[1]
                for param in params.split('&'):
                    if param.startswith('count='):
                        count = int(param.split('=')[1])
            self.send_json_response(self.data_manager.get_history(count))

        elif self.path == '/api/statistics':
            self.send_json_response(self.data_manager.get_statistics())

        else:
            # 提供静态文件
            super().do_GET()

    def do_POST(self):
        """处理POST请求（OneNET消息推送 / 手动测试）"""
        if self.path == '/api/onenet_push':
            content_length = int(self.headers.get('Content-Length', 0))
            body = self.rfile.read(content_length)
            try:
                raw = json.loads(body.decode('utf-8'))
                speed_data = self._parse_onenet(raw)
                if speed_data:
                    self.data_manager.add_data(speed_data)
                    print(f"[OneNET推送] {speed_data['speed_kmh']:.2f} km/h")
                    self.send_response(200)
                    self.end_headers()
                    self.wfile.write(b'OK')
                else:
                    self.send_response(400)
                    self.end_headers()
                    self.wfile.write(b'Cannot parse data')
            except Exception as e:
                print(f"[OneNET推送] 解析失败: {e}, body={body[:200]}")
                self.send_response(500)
                self.end_headers()
        else:
            self.send_response(404)
            self.end_headers()

    def _parse_onenet(self, raw):
        """
        解析OneNET推送数据，支持两种格式：
        格式1（MQTT dp上传原始payload）：
          {"id":1,"dp":{"speed_kmh":[{"v":1.5}],"speed_ms":[{"v":0.42}],"time_us":[{"v":242000}]}}
        格式2（OneNET平台HTTP推送）：
          {"msg":{"type":1,"dev_id":...,"ds_id":"speed_kmh","at":...,"value":1.5}}
        """
        timestamp = datetime.now().strftime('%Y-%m-%d %H:%M:%S')

        # 格式1：MQTT dp原始payload（含dp字段）
        if 'dp' in raw:
            dp = raw['dp']
            try:
                speed_kmh = dp['speed_kmh'][0]['v']
                speed_ms  = dp.get('speed_ms',  [{'v': speed_kmh / 3.6}])[0]['v']
                time_us   = dp.get('time_us',   [{'v': 0}])[0]['v']
                return {
                    'speed_kmh': float(speed_kmh),
                    'speed_ms':  float(speed_ms),
                    'time_us':   int(time_us),
                    'timestamp': timestamp,
                    'source':    'OneNET-MQTT'
                }
            except (KeyError, IndexError, TypeError):
                pass

        # 格式2：OneNET平台HTTP推送（单数据流）
        if 'msg' in raw:
            msg = raw['msg']
            ds_id = msg.get('ds_id', '')
            value = msg.get('value')
            if ds_id == 'speed_kmh' and value is not None:
                return {
                    'speed_kmh': float(value),
                    'speed_ms':  float(value) / 3.6,
                    'time_us':   0,
                    'timestamp': timestamp,
                    'source':    'OneNET-Push'
                }

        return None

    def send_json_response(self, data):
        """发送JSON响应"""
        self.send_response(200)
        self.send_header('Content-type', 'application/json')
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        self.wfile.write(json.dumps(data).encode())

    def log_message(self, format, *args):
        """禁用日志输出"""
        pass

def main():
    # 创建数据管理器
    data_manager = SpeedDataManager()

    # 设置API处理器的数据管理器
    APIHandler.data_manager = data_manager

    # 启动TCP服务器（接收STM32数据）
    tcp_server = TCPServerThread(data_manager, host='0.0.0.0', port=8080)
    tcp_server.start()

    # 启动HTTP服务器（提供Web界面）
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    http_server = HTTPServer(('0.0.0.0', 8000), APIHandler)

    # 获取本机IP用于提示
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(('8.8.8.8', 80))
        local_ip = s.getsockname()[0]
        s.close()
    except Exception:
        local_ip = '你的电脑IP'

    print("=" * 60)
    print("红外测速系统 - Web服务器")
    print("=" * 60)
    print(f"TCP服务器: 0.0.0.0:8080   (旧版TCP直连模式)")
    print(f"Web服务器: http://localhost:8000")
    print(f"OneNET推送: POST http://{local_ip}:8000/api/onenet_push")
    print("=" * 60)
    print("【OneNET配置步骤】")
    print(f"  1. 登录 https://open.iotda.huaweicloud.com 或 OneNET控制台")
    print(f"  2. 找到「消息推送」或「数据流转」功能")
    print(f"  3. 设置HTTP推送URL: http://{local_ip}:8000/api/onenet_push")
    print("=" * 60)
    print("按 Ctrl+C 停止服务器")
    print()

    try:
        http_server.serve_forever()
    except KeyboardInterrupt:
        print("\n正在关闭服务器...")
        http_server.shutdown()
        print("服务器已关闭")

if __name__ == '__main__':
    main()
