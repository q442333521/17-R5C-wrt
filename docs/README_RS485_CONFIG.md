# RS485 测厚仪配置指南

## 当前状态

✅ **串口设备**: /dev/ttyUSB0 (CH341 芯片)
✅ **驱动**: 已加载
✅ **权限**: 正确
⚠️  **通信**: 无数据响应

## 配置文件

位置: `/opt/gw/conf/config.json`

当前配置:
```json
"rs485": {
  "device": "/dev/ttyUSB0",
  "baudrate": 19200,
  "poll_rate_ms": 20,
  "timeout_ms": 200,
  "retry_count": 3,
  "simulate": false
}
```

## 需要的信息

为了正确配置测厚仪通信，您需要提供以下信息：

### 1. 测厚仪型号
- [ ] 品牌: _____________
- [ ] 型号: _____________
- [ ] 手册/文档链接: _____________

### 2. 通信参数
- [ ] 波特率: _____ (当前: 19200)
- [ ] 数据位: _____ (通常: 8)
- [ ] 停止位: _____ (通常: 1)
- [ ] 校验位: _____ (通常: None)

### 3. 通信协议
- [ ] 协议类型: ASCII / Binary / Modbus RTU / 其他
- [ ] 查询命令格式: _____________
- [ ] 响应格式: _____________
- [ ] 数据长度: _____ 字节

### 4. 示例通信数据
```
发送: [请填写十六进制]
接收: [请填写十六进制]
```

## 常见波特率

如果不确定波特率，可以尝试:
- 9600 (最常见)
- 19200 (当前设置)
- 38400
- 57600
- 115200

修改波特率:
```bash
# 编辑配置文件
vi /opt/gw/conf/config.json
# 修改 "baudrate" 值

# 重启服务
killall rs485d
/opt/gw/bin/rs485d /opt/gw/conf/config.json > /tmp/rs485d.log 2>&1 &
```

## 测试不同波特率

```bash
# 创建测试脚本
cat > /tmp/test_baudrates.sh << 'TESTEOF'
#!/bin/sh
for baud in 9600 19200 38400 57600 115200; do
    echo "测试波特率: $baud"
    stty -F /dev/ttyUSB0 $baud
    timeout 2 cat /dev/ttyUSB0 > /tmp/test_${baud}.log 2>&1 &
    sleep 2
    size=$(wc -c < /tmp/test_${baud}.log)
    if [ $size -gt 0 ]; then
        echo "  ✓ 接收到 $size 字节"
        hexdump -C /tmp/test_${baud}.log | head -5
    else
        echo "  ✗ 无数据"
    fi
    echo ""
done
TESTEOF
chmod +x /tmp/test_baudrates.sh
/tmp/test_baudrates.sh
```

## 源代码位置

如果需要修改通信协议，相关代码在:
- 开发机: `/root/r5c/src/rs485d/main.cpp`
- 关键函数: `query_thickness()` (约第 318 行)

当前实现是示例代码，需要根据实际测厚仪协议修改。

## 下一步

1. **获取测厚仪手册**
   - 找到通信协议章节
   - 记录查询命令和响应格式

2. **修改源代码**
   - 在开发机修改 `query_thickness()` 函数
   - 重新编译: `cd /root/r5c && ./scripts/wrt/build_alpine.sh`
   - 上传到设备

3. **测试通信**
   - 使用串口调试工具验证通信
   - 查看日志: `tail -f /tmp/rs485d.log`

## 当前降级模式

⚠️ 由于无法从测厚仪读取数据，系统已自动降级为模拟数据模式。
- Modbus 和 Web 界面仍然正常工作
- 显示的是模拟厚度值 (1.0-2.0 mm)
- 一旦测厚仪通信正常，将自动切换到真实数据

## 联系支持

如需帮助，请提供:
1. 测厚仪型号
2. 通信手册截图
3. `/tmp/rs485d.log` 日志文件
