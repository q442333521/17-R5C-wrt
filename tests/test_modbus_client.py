#!/usr/bin/env python3
"""
Modbus TCP Client Test Script
测试 Modbus TCP 服务器连接和数据读取
"""

import sys
import struct
import time

try:
    from pymodbus.client import ModbusTcpClient
except ImportError as exc:
    print("ERROR: pymodbus not installed or incompatible")
    print(f"Details: {exc}")
    print("Please install it:")
    print('  pip3 install "pymodbus>=2.5"')
    sys.exit(1)

def read_thickness_data(host='192.168.2.1', port=1502):
    """读取厚度数据"""
    
    print(f"Connecting to {host}:{port}...")
    client = ModbusTcpClient(host, port=port)
    
    if not client.connect():
        print("ERROR: Failed to connect to Modbus server")
        print("Is modbusd running?")
        return False
    
    print("Connected successfully")
    print("-" * 60)
    
    try:
        while True:
            # 读取保持寄存器 40001-40008 (Modbus 地址从 0 开始)
            try:
                result = client.read_holding_registers(0, count=8, device_id=1)
            except TypeError:
                try:
                    result = client.read_holding_registers(0, 8, unit=1)
                except TypeError:
                    # 兼容最旧版本的 pymodbus
                    result = client.read_holding_registers(0, 8, slave=1)
            
            if result.isError():
                print(f"ERROR: {result}")
                time.sleep(1)
                continue
            
            registers = result.registers
            
            # 解析 Float32 厚度值 (寄存器 0-1, Big-Endian)
            thickness_raw = (registers[0] << 16) | registers[1]
            thickness = struct.unpack('>f', struct.pack('>I', thickness_raw))[0]
            
            # 解析 Uint64 时间戳 (寄存器 2-5, Big-Endian, Unix ms)
            timestamp_raw = (registers[2] << 48) | (registers[3] << 32) | \
                           (registers[4] << 16) | registers[5]
            timestamp_s = timestamp_raw / 1000.0
            
            # 解析状态位 (寄存器 6)
            status = registers[6]
            
            # 解析序列号 (寄存器 7)
            sequence = registers[7]
            
            # 显示数据
            print(f"\r[{time.strftime('%H:%M:%S')}] "
                  f"Thickness: {thickness:7.3f} mm | "
                  f"Sequence: {sequence:6d} | "
                  f"Status: 0x{status:04X} | "
                  f"Timestamp: {timestamp_s:.3f}",
                  end='', flush=True)
            
            time.sleep(0.5)  # 每 0.5 秒读取一次
            
    except KeyboardInterrupt:
        print("\n\nStopped by user")
    finally:
        client.close()
        print("\nConnection closed")
    
    return True

def main():
    """主函数"""
    host = '192.168.2.1'
    port = 1502
    
    if len(sys.argv) > 1:
        host = sys.argv[1]
    if len(sys.argv) > 2:
        port = int(sys.argv[2])
    
    print("=" * 60)
    print("Modbus TCP Client Test")
    print("=" * 60)
    print(f"Target: {host}:{port}")
    print("Press Ctrl+C to stop")
    print("=" * 60)
    print()
    
    read_thickness_data(host, port)

if __name__ == '__main__':
    main()
