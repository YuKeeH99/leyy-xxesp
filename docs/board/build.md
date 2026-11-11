# 编译与烧录

## 1. 环境准备

- ESP-IDF **5.5.1**（5.4 及以上一般也可）
- Python 3.8+
- Windows / Linux / macOS 均可，Linux 编译更快

安装并激活 IDF：

```bash
# Windows（PowerShell）
C:\Espressif\frameworks\esp-idf-v5.5\export.ps1

# Linux / macOS / WSL
. $HOME/esp/esp-idf/export.sh
```

## 2. 选择板型

```bash
cd xiaoxin-esp32
idf.py set-target esp32s3
idf.py menuconfig
```

在 `Xiaozhi Assistant` → `Board Type` 中选择：

```text
美好憧憬科技ESP32-S3开发板(新思版)
```

对应配置项为 `CONFIG_BOARD_TYPE_MEIJING_XINSI`。

仓库已经附带验证过的 `sdkconfig`，默认板型就是 `meijing-xinsi`。如果 `set-target` 覆盖了配置，重新执行一次 `menuconfig` 即可。

## 3. 编译

```bash
idf.py build
```

首次编译会从 ESP-IDF 组件仓库下载依赖（约 470 MB），需要联网。依赖版本记录在 `dependencies.lock`，一般不需要手动改。

## 4. 编译产物

| 产物 | 路径 | 用途 |
| --- | --- | --- |
| 应用镜像 | `build/xiaozhi.bin` | **OTA 上传用这个** |
| 合并镜像 | `build/merged-binary.bin` | 首次整片烧录用（含 bootloader、分区表、模型） |
| 发布包 | `releases/v<版本>_meijing-xinsi.zip` | 内含 `merged-binary.bin`，适合整片烧录 |

> OTA 只能上传 `xiaozhi.bin`（纯应用镜像）。上传 `merged-binary.bin` 会导致升级失败或设备无法启动。

## 5. 一键发布

```bash
python scripts/release.py meijing-xinsi
```

成功后产物位于：

```text
releases/v<版本号>_meijing-xinsi.zip
```

## 6. 修改版本号

版本号在根目录 `CMakeLists.txt`：

```cmake
set(PROJECT_VER "2.0.4")
```

**每次发布 OTA 前必须递增**，否则设备比较版本后不会升级。

## 7. 整片烧录（USB 可用时）

```bash
idf.py -p COM26 flash
```

或直接写入合并镜像：

```bash
python -m esptool --chip esp32s3 -p COM26 -b 460800 write_flash 0x0 build/merged-binary.bin
```

> 部分板子的 USB 只供电、不传输数据，这种情况只能通过配网 + OTA 升级，无法本地烧录。

## 8. 串口日志

- 波特率：115200 8N1
- 启动日志中可以看到固件版本、板型、网络连接和 OTA 检查过程：

```text
Ota: Current version: 2.0.4
Ota: Current is the latest version
```
