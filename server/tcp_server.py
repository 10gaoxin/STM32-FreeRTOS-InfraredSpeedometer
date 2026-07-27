#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
红外测速系统 - TCP服务器
接收STM32通过ESP8266发送的测速数据
"""

import socket
import json
import threading
import time
from datetime import datetime

class SpeedServer:
    def __init__(self, host='0.0.0.0', port=8080):
        self.host = host
        self.port = port
        self.server = None
        self.running = False
        self.speed_history = []
        self.max_history = 100  # 保存最近100条记录

    def start(self):
        """启动服务器"""
        self.server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)

        try:
            self.server.bind((self.host, self.port))
            self.server.listen(5)
            self.running = True

            print("=" * 60)
            print("红外测速系统 - TCP服务器")
            print("=" * 60)
            print(f"服务器启动成功！")
            print(f"监听地址: {self.host}:{self.port}")
            print(f"等待STM32连接...")
            print("=" * 60)
            print()

            while self.running:
                try:
                    client, addr = self.server.accept()
                    print(f"[{self.get_timestamp()}] 新连接: {addr[0]}:{addr[1]}")

                    # 为每个客户端创建新线程
                    client_thread = threading.Thread(
                        target=self.handle_client,
                        args=(client, addr)
                    )
                    client_thread.daemon = True
                    client_thread.start()

                except KeyboardInterrupt:
                    print("\n服务器正在关闭...")
                    break
                except Exception as e:
                    print(f"错误: {e}")

        except Exception as e:
            print(f"服务器启动失败: {e}")
        finally:
            if self.server:
                self.server.close()

    def handle_client(self, client, addr):
        """处理客户端连接"""
        try:
            while self.running:
                data = client.recv(1024)
                if not data:
                    break

                # 解析数据
                try:
                    data_str = data.decode('utf-8').strip()
                    speed_data = json.loads(data_str)

                    # 添加时间戳和来源
                    speed_data['timestamp'] = self.get_timestamp()
                    speed_data['source'] = f"{addr[0]}:{addr[1]}"

                    # 保存到历史记录
                    self.speed_history.append(speed_data)
                    if len(self.speed_history) > self.max_history:
                        self.speed_history.pop(0)

                    # 显示数据
                    self.display_speed_data(speed_data)

                    # 可选：保存到文件
                    self.save_to_file(speed_data)

                except json.JSONDecodeError:
                    print(f"[{self.get_timestamp()}] 无效的JSON数据: {data_str}")
                except Exception as e:
                    print(f"[{self.get_timestamp()}] 数据处理错误: {e}")

        except Exception as e:
            print(f"[{self.get_timestamp()}] 客户端错误: {e}")
        finally:
            print(f"[{self.get_timestamp()}] 断开连接: {addr[0]}:{addr[1]}")
            client.close()

    def display_speed_data(self, data):
        """显示测速数据"""
        print("-" * 60)
        print(f"时间: {data['timestamp']}")
        print(f"来源: {data['source']}")
        print(f"速度: {data['speed_ms']:.2f} m/s  |  {data['speed_kmh']:.2f} km/h")
        print(f"通过时间: {data['time_us']} μs")
        print("-" * 60)
        print()

    def save_to_file(self, data):
        """保存数据到文件"""
        try:
            with open('speed_data.log', 'a', encoding='utf-8') as f:
                f.write(json.dumps(data, ensure_ascii=False) + '\n')
        except Exception as e:
            print(f"保存文件失败: {e}")

    def get_timestamp(self):
        """获取时间戳"""
        return datetime.now().strftime('%Y-%m-%d %H:%M:%S')

    def stop(self):
        """停止服务器"""
        self.running = False
        if self.server:
            self.server.close()

def main():
    # 创建服务器实例
    server = SpeedServer(host='0.0.0.0', port=8080)

    try:
        # 启动服务器
        server.start()
    except KeyboardInterrupt:
        print("\n正在关闭服务器...")
        server.stop()
        print("服务器已关闭")

if __name__ == '__main__':
    main()
