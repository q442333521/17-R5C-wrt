#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
S7 PLC 模拟服务器 (用于测试)

功能:
- 模拟西门子 S7 PLC
- 监听端口 102
- 接收网关写入的数据
- 显示接收到的厚度值、时间戳等

依赖:
- python-snap7: pip3 install python-snap7

作者: Gateway Project
日期: 2025-10-11
"""

import time
import struct
import socket
import threading
from datetime import datetime

try:
    import snap7
    from snap7.server import Server
except ImportError:
    print("错误: 需要安装 python-snap7")
    print("运行: pip3 install python-snap7")
    exit(1)


class S7MockServer:
    """S7 PLC 模拟服务器"""
    
    def __init__(self, db_number=10, db_size=16):
        """
        初始化服务器
        
        Args:
            db_number: DB块编号
            db_size: DB块大小(字节)
        """
        self.server = Server()
        self.db_number = db_number
        self.db_size = db_size
        self.running = False
        
        # 创建DB块
        self.db_data = bytearray(db_size)
        
    def start(self, ip="0.0.0.0", port=102):
        """启动服务器"""
        # 注册DB块
        self.server.register_area(snap7.types.srvAreaDB, self.db_number, self.db_data)
        
        # 启动服务器
        error = self.server.start(tcpport=port)
        if error == 0:
            print(f"✅ S7 模拟服务器已启动")
            print(f"   监听地址: {ip}:{port}")
            print(f"   DB块编号: DB{self.db_number}")
            print(f"   DB块大小: {self.db_size} 字节")
            print()
            self.running = True
            
            # 启动监控线程
            monitor_thread = threading.Thread(target=self._monitor_data, daemon=True)
            monitor_thread.start()
            
            return True
        else:
            print(f"❌ 服务器启动失败: {error}")
            return False
    
    def stop(self):
        """停止服务器"""
        self.running = False
        self.server.stop()
        print("S7 模拟服务器已停止")
    
    def _monitor_data(self):
        """监控数据变化"""
        last_sequence = 0
        
        print("=" * 60)
        print("等待网关连接...")
        print("=" * 60)
        print()
        
        while self.running:
            try:
                # 读取DB块数据
                data = bytes(self.db_data)
                
                # 解析数据 (Big-Endian)
                # Byte 0-3: Float32 - 厚度值
                thickness = struct.unpack('>f', data[0:4])[0]
                
                # Byte 4-7: DWord - 时间戳低32位
                timestamp_low = struct.unpack('>I', data[4:8])[0]
                
                # Byte 8-11: DWord - 时间戳高32位
                timestamp_high = struct.unpack('>I', data[8:12])[0]
                
                # 合并为64位时间戳
                timestamp_ms = (timestamp_high << 32) | timestamp_low
                
                # Byte 12-13: Word - 状态位
                status = struct.unpack('>H', data[12:14])[0]
                
                # Byte 14-15: Word - 序列号
                sequence = struct.unpack('>H', data[14:16])[0]
                
                # 如果序列号变化,说明有新数据
                if sequence != last_sequence and sequence > 0:
                    # 转换时间戳为可读格式
                    dt = datetime.fromtimestamp(timestamp_ms / 1000.0)
                    time_str = dt.strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
                    
                    # 解析状态位
                    data_valid = (status & 0x0001) != 0
                    rs485_ok = (status & 0x0002) != 0
                    crc_ok = (status & 0x0004) != 0
                    sensor_ok = (status & 0x0008) != 0
                    error_code = (status >> 8) & 0xFF
                    
                    # 显示数据
                    print(f"[{time_str}] 📊 收到数据:")
                    print(f"  厚度值: {thickness:.3f} mm")
                    print(f"  序列号: {sequence}")
                    print(f"  状态位: 0x{status:04X}")
                    print(f"    - 数据有效: {'✅' if data_valid else '❌'}")
                    print(f"    - RS485正常: {'✅' if rs485_ok else '❌'}")
                    print(f"    - CRC校验: {'✅' if crc_ok else '❌'}")
                    print(f"    - 传感器正常: {'✅' if sensor_ok else '❌'}")
                    if error_code > 0:
                        print(f"    - 错误代码: {error_code}")
                    print()
                    
                    last_sequence = sequence
                
            except Exception as e:
                print(f"❌ 数据解析错误: {e}")
            
            time.sleep(0.1)  # 100ms 轮询周期


def main():
    """主函数"""
    print("=" * 60)
    print("S7 PLC 模拟服务器")
    print("=" * 60)
    print()
    
    # 创建服务器
    server = S7MockServer(db_number=10, db_size=16)
    
    # 启动服务器
    if not server.start():
        return 1
    
    try:
        print("按 Ctrl+C 停止服务器")
        print()
        
        # 保持运行
        while True:
            time.sleep(1)
            
    except KeyboardInterrupt:
        print("\n\n收到中断信号")
        server.stop()
        return 0


if __name__ == "__main__":
    exit(main())
