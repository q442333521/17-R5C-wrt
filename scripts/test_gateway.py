#!/usr/bin/env python3
"""
Gateway Bridge 测试脚本
测试Modbus TCP连接和数据读取
"""

import sys
import time
import struct

try:
    from pymodbus.client import ModbusTcpClient
except ImportError:
    print("错误: 未安装 pymodbus")
    print("请运行: pip3 install pymodbus")
    sys.exit(1)

def test_modbus_tcp(host='127.0.0.1', port=502):
    print("═══════════════════════════════════════════")
    print("  Gateway Bridge Modbus TCP 测试")
    print("═══════════════════════════════════════════")
    print(f"\n连接到: {host}:{port}")

    client = ModbusTcpClient(host, port=port)

    if not client.connect():
        print("✗ 连接失败！")
        print("请确保 gateway-bridge 正在运行")
        return False

    print("✓ 连接成功\n")

    # 测试读取厚度值 (寄存器100-101, Float)
    print("读取厚度值 (寄存器100-101)...")
    try:
        result = client.read_holding_registers(100, 2, slave=1)

        if result.isError():
            print(f"✗ 读取失败: {result}")
            return False

        # 转换为Float (Big Endian)
        value = struct.unpack('>f', struct.pack('>HH', *result.registers))[0]
        print(f"✓ 厚度值: {value:.3f} mm\n")

    except Exception as e:
        print(f"✗ 异常: {e}")
        return False

    # 连续读取测试
    print("连续读取测试 (10次)...")
    for i in range(10):
        result = client.read_holding_registers(100, 2, slave=1)
        if not result.isError():
            value = struct.unpack('>f', struct.pack('>HH', *result.registers))[0]
            print(f"  [{i+1}] 厚度: {value:.3f} mm")
        else:
            print(f"  [{i+1}] 读取失败")
        time.sleep(0.5)

    client.close()
    print("\n✓ 测试完成")
    return True

if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser(description='Test Gateway Bridge Modbus TCP')
    parser.add_argument('--host', default='127.0.0.1', help='Gateway IP address')
    parser.add_argument('--port', type=int, default=502, help='Modbus TCP port')

    args = parser.parse_args()

    success = test_modbus_tcp(args.host, args.port)
    sys.exit(0 if success else 1)
