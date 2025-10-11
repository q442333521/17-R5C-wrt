#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OPC UA 模拟服务器 (用于测试)

功能:
- 模拟 OPC UA 服务器
- 监听端口 4840
- 接收网关写入的数据
- 显示接收到的厚度值、时间戳等

依赖:
- opcua: pip3 install opcua

作者: Gateway Project
日期: 2025-10-11
"""

import time
from datetime import datetime

try:
    from opcua import Server, ua
except ImportError:
    print("错误: 需要安装 opcua")
    print("运行: pip3 install opcua")
    exit(1)


class OPCUAMockServer:
    """OPC UA 模拟服务器"""
    
    def __init__(self):
        """初始化服务器"""
        self.server = Server()
        self.server.set_endpoint("opc.tcp://0.0.0.0:4840/freeopcua/server/")
        self.server.set_server_name("Gateway Mock OPC UA Server")
        
        # 设置安全策略
        self.server.set_security_policy([ua.SecurityPolicyType.NoSecurity])
        
        # 变量引用
        self.var_thickness = None
        self.var_timestamp = None
        self.var_status = None
        self.var_sequence = None
        
        # 上一次的值
        self.last_sequence = 0
    
    def setup_namespace(self):
        """设置命名空间和变量"""
        # 注册命名空间
        uri = "http://gateway.local"
        idx = self.server.register_namespace(uri)
        
        # 创建对象
        objects = self.server.get_objects_node()
        gateway_obj = objects.add_object(idx, "Gateway")
        
        # 创建变量节点 (可写)
        self.var_thickness = gateway_obj.add_variable(
            idx, "Thickness", 0.0, ua.VariantType.Float
        )
        self.var_thickness.set_writable()
        
        self.var_timestamp = gateway_obj.add_variable(
            idx, "Timestamp", 0, ua.VariantType.Int64
        )
        self.var_timestamp.set_writable()
        
        self.var_status = gateway_obj.add_variable(
            idx, "Status", 0, ua.VariantType.UInt16
        )
        self.var_status.set_writable()
        
        self.var_sequence = gateway_obj.add_variable(
            idx, "Sequence", 0, ua.VariantType.UInt32
        )
        self.var_sequence.set_writable()
        
        print("✅ 命名空间和变量节点已创建:")
        print(f"   ns=2;s=Gateway.Thickness (Float)")
        print(f"   ns=2;s=Gateway.Timestamp (Int64)")
        print(f"   ns=2;s=Gateway.Status    (UInt16)")
        print(f"   ns=2;s=Gateway.Sequence  (UInt32)")
        print()
    
    def start(self):
        """启动服务器"""
        try:
            # 设置命名空间
            self.setup_namespace()
            
            # 启动服务器
            self.server.start()
            
            print("✅ OPC UA 模拟服务器已启动")
            print(f"   监听地址: opc.tcp://0.0.0.0:4840/freeopcua/server/")
            print(f"   安全模式: None")
            print()
            print("=" * 60)
            print("等待网关连接...")
            print("=" * 60)
            print()
            
            return True
        except Exception as e:
            print(f"❌ 服务器启动失败: {e}")
            return False
    
    def stop(self):
        """停止服务器"""
        self.server.stop()
        print("OPC UA 模拟服务器已停止")
    
    def monitor_data(self):
        """监控数据变化"""
        while True:
            try:
                # 读取变量值
                thickness = self.var_thickness.get_value()
                timestamp_ms = self.var_timestamp.get_value()
                status = self.var_status.get_value()
                sequence = self.var_sequence.get_value()
                
                # 如果序列号变化,说明有新数据
                if sequence != self.last_sequence and sequence > 0:
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
                    
                    self.last_sequence = sequence
                
            except Exception as e:
                print(f"❌ 数据读取错误: {e}")
            
            time.sleep(0.1)  # 100ms 轮询周期


def main():
    """主函数"""
    print("=" * 60)
    print("OPC UA 模拟服务器")
    print("=" * 60)
    print()
    
    # 创建服务器
    server = OPCUAMockServer()
    
    # 启动服务器
    if not server.start():
        return 1
    
    try:
        print("按 Ctrl+C 停止服务器")
        print()
        
        # 监控数据
        server.monitor_data()
        
    except KeyboardInterrupt:
        print("\n\n收到中断信号")
        server.stop()
        return 0


if __name__ == "__main__":
    exit(main())
