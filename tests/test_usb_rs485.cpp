/**
 * @file test_usb_rs485.cpp
 * @brief USB 转 RS485 设备测试程序
 * 
 * 功能：
 * 1. 检测 USB 转 RS485 设备（CH340 芯片 1a86:7523）
 * 2. 测试串口通信
 * 3. 发送 Modbus RTU 查询指令
 * 4. 接收并解析响应数据
 * 
 * 编译:
 *   g++ -o test_usb_rs485 test_usb_rs485.cpp -lmodbus -std=c++17
 * 
 * 使用:
 *   sudo ./test_usb_rs485 /dev/ttyUSB0
 * 
 * @author Gateway Project
 * @date 2025-10-13
 */

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <modbus/modbus.h>

using namespace std;

// 颜色输出宏
#define COLOR_RED     "\033[0;31m"
#define COLOR_GREEN   "\033[0;32m"
#define COLOR_YELLOW  "\033[1;33m"
#define COLOR_BLUE    "\033[0;34m"
#define COLOR_RESET   "\033[0m"

#define LOG_INFO(msg)  cout << COLOR_GREEN  << "[INFO]  " << COLOR_RESET << msg << endl
#define LOG_WARN(msg)  cout << COLOR_YELLOW << "[WARN]  " << COLOR_RESET << msg << endl
#define LOG_ERROR(msg) cout << COLOR_RED    << "[ERROR] " << COLOR_RESET << msg << endl

/**
 * @brief 检查串口设备是否存在
 */
bool check_device_exists(const string& device) {
    int fd = open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return false;
    }
    close(fd);
    return true;
}

/**
 * @brief 获取串口设备信息
 */
void print_device_info(const string& device) {
    cout << COLOR_BLUE << "========================================" << COLOR_RESET << endl;
    cout << COLOR_BLUE << "USB 转 RS485 设备信息" << COLOR_RESET << endl;
    cout << COLOR_BLUE << "========================================" << COLOR_RESET << endl;
    
    // 执行 udevadm 命令获取设备信息
    string cmd = "udevadm info -a -n " + device + " 2>/dev/null | grep -E 'idVendor|idProduct|manufacturer|product|serial' | head -10";
    system(cmd.c_str());
    
    cout << endl;
}

/**
 * @brief 测试串口基本通信
 */
bool test_serial_basic(const string& device) {
    LOG_INFO("测试串口基本通信...");
    
    // 打开串口
    int fd = open(device.c_str(), O_RDWR | O_NOCTTY);
    if (fd < 0) {
        LOG_ERROR("无法打开串口: " + string(strerror(errno)));
        return false;
    }
    
    // 配置串口参数（19200 8N1）
    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    
    if (tcgetattr(fd, &tty) != 0) {
        LOG_ERROR("无法获取串口属性");
        close(fd);
        return false;
    }
    
    // 设置波特率 19200
    cfsetospeed(&tty, B19200);
    cfsetispeed(&tty, B19200);
    
    // 8N1
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag |= CREAD | CLOCAL;
    
    // 原始模式
    tty.c_lflag &= ~ICANON;
    tty.c_lflag &= ~ECHO;
    tty.c_lflag &= ~ECHOE;
    tty.c_lflag &= ~ECHONL;
    tty.c_lflag &= ~ISIG;
    
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    
    tty.c_oflag &= ~OPOST;
    tty.c_oflag &= ~ONLCR;
    
    // 超时设置
    tty.c_cc[VTIME] = 10;
    tty.c_cc[VMIN] = 0;
    
    // 应用配置
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        LOG_ERROR("无法设置串口属性");
        close(fd);
        return false;
    }
    
    // 清空缓冲区
    tcflush(fd, TCIOFLUSH);
    
    LOG_INFO("串口配置成功: 19200 8N1");
    
    // 发送测试数据
    const char* test_msg = "TEST\r\n";
    ssize_t n = write(fd, test_msg, strlen(test_msg));
    if (n < 0) {
        LOG_ERROR("写入失败");
        close(fd);
        return false;
    }
    
    LOG_INFO("发送测试数据: TEST");
    
    // 尝试读取响应
    char buf[128];
    usleep(100000);
    n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        LOG_INFO("收到响应: " + string(buf));
    } else {
        LOG_WARN("未收到响应（正常，设备可能不响应文本消息）");
    }
    
    close(fd);
    return true;
}

/**
 * @brief 使用 libmodbus 测试 Modbus RTU 通信
 */
bool test_modbus_rtu(const string& device) {
    LOG_INFO("测试 Modbus RTU 通信...");
    
    // 创建 Modbus RTU 上下文
    modbus_t* ctx = modbus_new_rtu(device.c_str(), 19200, 'N', 8, 1);
    if (ctx == nullptr) {
        LOG_ERROR("无法创建 Modbus 上下文");
        return false;
    }
    
    // 设置从站 ID（根据实际测厚仪配置修改）
    modbus_set_slave(ctx, 1);
    
    // 设置超时时间
    modbus_set_response_timeout(ctx, 1, 0);
    
    // 设置串口模式为 RS485
    modbus_rtu_set_serial_mode(ctx, MODBUS_RTU_RS485);
    
    // 连接设备
    if (modbus_connect(ctx) == -1) {
        LOG_ERROR("无法连接到设备: " + string(modbus_strerror(errno)));
        modbus_free(ctx);
        return false;
    }
    
    LOG_INFO("Modbus RTU 连接成功");
    
    // 测试读取保持寄存器
    uint16_t tab_reg[10];
    int rc = modbus_read_registers(ctx, 0, 2, tab_reg);
    
    if (rc == -1) {
        LOG_WARN("读取寄存器失败: " + string(modbus_strerror(errno)));
        LOG_WARN("可能原因:");
        LOG_WARN("  1. 设备未连接或未上电");
        LOG_WARN("  2. 从站 ID 不正确（当前设置为 1）");
        LOG_WARN("  3. 波特率不匹配（当前 19200）");
        LOG_WARN("  4. 寄存器地址不存在");
    } else {
        LOG_INFO("读取寄存器成功:");
        cout << "  寄存器 0: 0x" << hex << setw(4) << setfill('0') << tab_reg[0] 
             << " (" << dec << tab_reg[0] << ")" << endl;
        cout << "  寄存器 1: 0x" << hex << setw(4) << setfill('0') << tab_reg[1] 
             << " (" << dec << tab_reg[1] << ")" << endl;
    }
    
    // 清理
    modbus_close(ctx);
    modbus_free(ctx);
    
    return rc != -1;
}

/**
 * @brief 扫描所有可用的串口设备
 */
void scan_serial_devices() {
    LOG_INFO("扫描串口设备...");
    
    vector<string> devices = {
        "/dev/ttyUSB0", "/dev/ttyUSB1", "/dev/ttyUSB2",
        "/dev/ttyS0", "/dev/ttyS1",
        "/dev/ttyAMA0"
    };
    
    bool found = false;
    for (const auto& dev : devices) {
        if (check_device_exists(dev)) {
            cout << "  ✓ " << dev << " (存在)" << endl;
            found = true;
        }
    }
    
    if (!found) {
        LOG_WARN("未找到任何串口设备");
        LOG_INFO("请检查:");
        LOG_INFO("  1. USB 设备是否已连接");
        LOG_INFO("  2. 驱动是否已加载 (lsmod | grep ch341)");
        LOG_INFO("  3. 设备权限 (ls -l /dev/ttyUSB*)");
    }
    
    cout << endl;
}

/**
 * @brief 显示使用帮助
 */
void print_usage(const char* prog_name) {
    cout << "用法: " << prog_name << " [选项] <设备路径>" << endl;
    cout << endl;
    cout << "选项:" << endl;
    cout << "  -s, --scan     扫描所有串口设备" << endl;
    cout << "  -h, --help     显示帮助信息" << endl;
    cout << endl;
    cout << "示例:" << endl;
    cout << "  " << prog_name << " /dev/ttyUSB0" << endl;
    cout << "  " << prog_name << " --scan" << endl;
    cout << endl;
}

/**
 * @brief 主函数
 */
int main(int argc, char* argv[]) {
    cout << COLOR_BLUE << "========================================" << COLOR_RESET << endl;
    cout << COLOR_BLUE << "USB 转 RS485 设备测试程序" << COLOR_RESET << endl;
    cout << COLOR_BLUE << "适用于: CH340 (1a86:7523)" << COLOR_RESET << endl;
    cout << COLOR_BLUE << "========================================" << COLOR_RESET << endl;
    cout << endl;
    
    // 解析命令行参数
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    string arg1 = argv[1];
    
    // 扫描模式
    if (arg1 == "-s" || arg1 == "--scan") {
        scan_serial_devices();
        return 0;
    }
    
    // 帮助信息
    if (arg1 == "-h" || arg1 == "--help") {
        print_usage(argv[0]);
        return 0;
    }
    
    // 测试指定设备
    string device = argv[1];
    
    // 检查设备是否存在
    if (!check_device_exists(device)) {
        LOG_ERROR("设备不存在: " + device);
        LOG_INFO("运行 '" + string(argv[0]) + " --scan' 查看可用设备");
        return 1;
    }
    
    // 显示设备信息
    print_device_info(device);
    
    // 测试串口基本通信
    bool serial_ok = test_serial_basic(device);
    cout << endl;
    
    // 测试 Modbus RTU 通信
    bool modbus_ok = test_modbus_rtu(device);
    cout << endl;
    
    // 总结
    cout << COLOR_BLUE << "========================================" << COLOR_RESET << endl;
    cout << COLOR_BLUE << "测试结果" << COLOR_RESET << endl;
    cout << COLOR_BLUE << "========================================" << COLOR_RESET << endl;
    cout << "串口通信:    " << (serial_ok ? COLOR_GREEN "通过 ✓" : COLOR_RED "失败 ✗") << COLOR_RESET << endl;
    cout << "Modbus RTU:  " << (modbus_ok ? COLOR_GREEN "通过 ✓" : COLOR_RED "失败 ✗") << COLOR_RESET << endl;
    cout << endl;
    
    if (!modbus_ok) {
        LOG_INFO("Modbus 测试失败是正常的，如果:");
        LOG_INFO("  1. 测厚仪设备未连接到 RS485 总线");
        LOG_INFO("  2. 设备参数（从站ID、波特率）不匹配");
        LOG_INFO("  3. 需要根据实际设备调整测试参数");
    }
    
    return 0;
}
