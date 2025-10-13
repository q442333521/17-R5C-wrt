# IPK 打包问题修复说明

## 问题现象

在 FriendlyWrt 上安装 IPK 包时出现 "Malformed package file" 错误：

```bash
root@FriendlyWrt:~# opkg install /tmp/gw-gateway_1.0.0_aarch64_cortex-a53.ipk
Collected errors:
 * pkg_init_from_file: Malformed package file /tmp/gw-gateway_1.0.0_aarch64_cortex-a53.ipk.
```

## 问题原因

脚本 `scripts/wrt/build_and_deploy.sh` 的 `package_ipk()` 函数中，使用了错误的 `ar` 命令参数：

```bash
# ❌ 错误的命令
ar rcs "$IPK_NAME" debian-binary control.tar.gz data.tar.gz
```

**问题解释：**

1. **`ar rcs` 中的 `s` 参数**：会创建符号表索引（symbol table index）
2. **OpenWrt/opkg 要求**：IPK 包必须是纯粹的 ar 归档文件，不能包含符号表
3. **导致结果**：opkg 无法识别包含符号表的 ar 文件，报 "Malformed package file" 错误

## 修复方案

### 1. 主要修复点

将 `ar rcs` 改为 `ar rc`（移除 `s` 参数）：

```bash
# ✅ 正确的命令
ar rc "$IPK_NAME" debian-binary control.tar.gz data.tar.gz
```

### 2. 其他优化

同时进行了以下改进：

1. **使用独立工作目录**：避免污染 package 目录
2. **CONTROL 目录打包**：使用 `.` 而不是列举文件名，确保所有控制文件都被包含
3. **添加详细日志**：每个步骤都有信息输出，便于调试

## 完整修复后的代码

```bash
# 打包 IPK
print_info "打包 IPK..."
cd "$PACKAGE_DIR"

# 创建临时工作目录
local WORK_DIR="$PACKAGE_DIR/work"
mkdir -p "$WORK_DIR"
cd "$WORK_DIR"

# 创建数据压缩包（注意：必须使用 gzip 而不是其他压缩格式）
print_info "  创建 data.tar.gz..."
tar --numeric-owner --owner=0 --group=0 -czf data.tar.gz -C "$PKG_ROOT" opt etc

# 创建控制文件压缩包
print_info "  创建 control.tar.gz..."
tar --numeric-owner --owner=0 --group=0 -czf control.tar.gz -C "$PKG_ROOT/CONTROL" .

# 创建 debian-binary（必须包含换行符）
print_info "  创建 debian-binary..."
echo "2.0" > debian-binary

# 创建最终的 IPK 包（注意：使用 'ar rc' 而不是 'ar rcs'，s 参数会导致 opkg 报错）
print_info "  打包 IPK..."
local IPK_NAME="gw-gateway_${IPK_VERSION}_aarch64_cortex-a53.ipk"
rm -f "../$IPK_NAME"
ar rc "../$IPK_NAME" debian-binary control.tar.gz data.tar.gz

# 清理临时文件
cd "$PACKAGE_DIR"
rm -rf "$WORK_DIR"
```

## IPK 包格式要求

### OpenWrt IPK 包的标准格式

IPK（Itsy Package）是基于 ar 归档格式的轻量级包管理格式，必须满足以下要求：

1. **使用 ar 命令创建**
   - 必须使用 `ar rc`（create, replace）
   - 不能使用 `ar rcs`（会添加符号表）

2. **包含三个文件，顺序固定**
   ```
   debian-binary      # 包含 "2.0\n"
   control.tar.gz     # 包含元数据（control, postinst, prerm 等）
   data.tar.gz        # 包含实际文件
   ```

3. **debian-binary 格式**
   - 内容必须是：`2.0` + 换行符
   - 表示包格式版本

4. **control.tar.gz 内容**
   - `control` 文件：包的元数据（必需）
   - `postinst`：安装后脚本（可选）
   - `prerm`：卸载前脚本（可选）
   - `postrm`：卸载后脚本（可选）

5. **data.tar.gz 内容**
   - 包含所有要安装的文件
   - 目录结构必须与目标系统一致

## 验证 IPK 包格式

### 方法 1: 使用 ar 命令检查

```bash
# 查看 ar 归档内容
ar t gw-gateway_1.0.0_aarch64_cortex-a53.ipk

# 预期输出（正确的顺序）：
debian-binary
control.tar.gz
data.tar.gz
```

### 方法 2: 解包检查

```bash
# 创建临时目录
mkdir -p /tmp/ipk-check
cd /tmp/ipk-check

# 解包 IPK
ar x /path/to/gw-gateway_1.0.0_aarch64_cortex-a53.ipk

# 检查文件
ls -la
# 应该看到：debian-binary, control.tar.gz, data.tar.gz

# 检查 debian-binary 内容
cat debian-binary
# 应该输出：2.0

# 检查 control.tar.gz 内容
tar -tzf control.tar.gz
# 应该看到：./control, ./postinst, ./prerm

# 检查 data.tar.gz 内容
tar -tzf data.tar.gz
# 应该看到：opt/, etc/ 等目录
```

### 方法 3: 使用 file 命令

```bash
# 检查文件类型
file gw-gateway_1.0.0_aarch64_cortex-a53.ipk

# 正确的输出应该是：
# gw-gateway_1.0.0_aarch64_cortex-a53.ipk: current ar archive
```

### 方法 4: 在 FriendlyWrt 上测试

```bash
# 上传到设备
scp gw-gateway_1.0.0_aarch64_cortex-a53.ipk root@192.168.2.1:/tmp/

# SSH 到设备
ssh root@192.168.2.1

# 测试安装（dry-run）
opkg install --noaction /tmp/gw-gateway_1.0.0_aarch64_cortex-a53.ipk

# 如果没有报错，就是成功的
```

## 使用修复后的脚本

### 1. 重新编译和打包

```bash
cd ~/gateway-project

# 完整的编译、打包、部署流程
./scripts/wrt/build_and_deploy.sh deploy

# 或者分步执行
./scripts/wrt/build_and_deploy.sh build      # 仅编译
./scripts/wrt/build_and_deploy.sh package    # 编译+打包
./scripts/wrt/build_and_deploy.sh install    # 仅安装已有的 IPK
```

### 2. 预期输出

```
=========================================
制作 IPK 安装包
=========================================
[INFO] 创建包目录结构...
[INFO] 复制可执行文件...
[INFO] 复制配置文件...
[INFO] 创建 OpenWrt init 脚本...
[INFO] 创建 CONTROL 文件...
[INFO] 打包 IPK...
[INFO]   创建 data.tar.gz...
[INFO]   创建 control.tar.gz...
[INFO]   创建 debian-binary...
[INFO]   打包 IPK...
[INFO] IPK 包创建成功: gw-gateway_1.0.0_aarch64_cortex-a53.ipk (547K)
[INFO] IPK 路径: /root/gateway-project/package/gw-gateway_1.0.0_aarch64_cortex-a53.ipk
```

### 3. 安装到 FriendlyWrt

```bash
# 在 FriendlyWrt 上执行
opkg install /tmp/gw-gateway_1.0.0_aarch64_cortex-a53.ipk

# 预期输出
Installing gw-gateway (1.0.0) to root...
Configuring gw-gateway.
Gateway 安装完成
访问 Web 界面: http://192.168.2.1:8080
```

## 常见问题

### Q1: 为什么不能使用 `ar rcs`？

**A:** `s` 参数会在 ar 归档中创建符号表索引，这是为 C/C++ 链接器设计的功能。OpenWrt 的 opkg 包管理器期望的是纯粹的 ar 归档格式，不能包含符号表。

### Q2: 如果还是报 "Malformed package file" 怎么办？

**A:** 检查以下几点：
1. 确认使用的是 `ar rc` 而不是 `ar rcs`
2. 检查文件顺序是否正确：debian-binary, control.tar.gz, data.tar.gz
3. 验证 debian-binary 内容是 "2.0" + 换行符
4. 确保 tar 包使用 gzip 压缩（-z 参数）
5. 检查 control 文件格式是否正确

### Q3: 能否使用其他压缩格式？

**A:** 不行。OpenWrt 要求使用 gzip 压缩（.tar.gz），不能使用 bzip2（.tar.bz2）或 xz（.tar.xz）。

### Q4: control 文件有什么要求？

**A:** 
- 必须包含 Package, Version, Architecture 字段
- 字段名和值之间用冒号和空格分隔
- 描述可以多行，续行必须以空格开头
- 不能有多余的空行

## 参考资源

- [OpenWrt Package Manager](https://openwrt.org/docs/guide-user/additional-software/opkg)
- [Debian Binary Package Format](https://www.debian.org/doc/debian-policy/ch-controlfields.html)
- [ar Command Manual](https://man7.org/linux/man-pages/man1/ar.1.html)

## 总结

**核心要点：**
- ✅ 使用 `ar rc` 而不是 `ar rcs`
- ✅ 保持正确的文件顺序
- ✅ 使用 gzip 压缩
- ✅ 确保 control 文件格式正确

修复后的脚本已经可以正常工作，生成的 IPK 包可以在 FriendlyWrt 上成功安装！
