# 构建与启动脚本说明

本项目现在将脚本分为两类：**本地开发环境**与 **FriendlyWrt 专用**。以下说明各脚本的定位、使用方式以及典型流程，帮助你快速区分 `build` 与 `start` 相关脚本的差异。

## 1. 本地开发脚本（Host）

| 脚本 | 作用 | 适用场景 | 常用命令 |
| --- | --- | --- | --- |
| `scripts/build.sh` | 使用本机工具链在 `build/` 目录内编译所有守护进程 | PC/ARM64 上的常规开发、调试 | `./scripts/build.sh`<br>`./scripts/build.sh clean` |
| `scripts/start.sh` | 读取 `config/config.json`，依次启动 `build/` 目录下的守护进程，可选模拟模式 | 本地联调、功能验证 | `./scripts/start.sh`<br>`./scripts/start.sh --simulate` |
| `scripts/stop.sh` | 根据 `start.sh` 记录的 PID 结束所有进程并清理临时文件 | 停止本地调试环境 | `./scripts/stop.sh` |

> **差异提示**：`build.sh` 负责生成可执行文件，不会启动服务；`start.sh` 假设可执行文件已经存在，只负责运行并提供日志位置。

## 2. FriendlyWrt 专用脚本

FriendlyWrt 相关文件统一迁移至 `scripts/wrt/` 目录，便于区分交叉部署流程。

| 脚本 | 作用 | 适用场景 | 常用命令 |
| --- | --- | --- | --- |
| `scripts/wrt/build_and_deploy.sh` | 一体化流程：检查依赖 → 重新生成 `build-wrt/` → 可选打包 IPK → 推送到目标设备 | 从源码重新生成并部署到 FriendlyWrt | `./scripts/wrt/build_and_deploy.sh build`<br>`./scripts/wrt/build_and_deploy.sh package`<br>`./scripts/wrt/build_and_deploy.sh deploy` |
| `scripts/wrt/deploy_wrt.sh` | 生成/上传 IPK 包并远程控制服务（含 `deploy`、`install`、`sync`、`restart` 等命令） | 将完整网关以 IPK 形式安装到 FriendlyWrt，或快速同步二进制 | `./scripts/wrt/deploy_wrt.sh deploy` |
| `scripts/wrt/start_local_wrt.sh` | 在本地使用 `build-wrt/` 的 ARM64 产物启动所有守护进程，并运行 Modbus 自动化测试 | 不连接设备时验证 FriendlyWrt 构建物 | `./scripts/wrt/start_local_wrt.sh` |
| `scripts/wrt/build_open62541.sh` | 以 Root 权限从源码构建 open62541（OPC UA 库），支持 minimal/standard/full 三种配置 | 需要升级或重新安装 open62541 依赖时 | `sudo BUILD_CONFIG=standard ./scripts/wrt/build_open62541.sh` |

> **差异提示**：Wrt 脚本默认针对 FriendlyWrt 设备。`start_local_wrt.sh` 只执行生成的二进制，不会重新编译；`build_and_deploy.sh` 负责重新生成二进制并可选部署。

## 3. 典型使用流程

### 3.1 本地开发与调试
1. `./scripts/build.sh`
2. `./scripts/start.sh`（必要时加 `--simulate`）
3. 修改代码后重复步骤 1、2
4. `./scripts/stop.sh` 停止服务

### 3.2 FriendlyWrt 全流程
1. `sudo ./scripts/wrt/build_open62541.sh`（首次部署或升级 open62541 时）
2. `./scripts/wrt/build_and_deploy.sh build`（仅编译）或 `deploy`（编译并部署）
3. （可选）`./scripts/wrt/deploy_wrt.sh status` 查看设备状态

### 3.3 离线验证 FriendlyWrt 产物
1. 在 FriendlyWrt 流程中生成 `build-wrt/`
2. `./scripts/wrt/start_local_wrt.sh`
3. 脚本会自动运行 `tests/test_modbus_client.py` 进行连通性测试

## 4. 额外说明

- `scripts/wrt/` 目录用于集中存放 FriendlyWrt 构建与执行相关脚本，原先的重复脚本已清理。
- 如需在 CI 或其他自动化场景中调用，建议优先使用表格中的命令形式，保持路径一致。
- 如果部署目标与默认的 IP 与账号不同，可通过环境变量覆盖（详见各脚本顶部说明）。
