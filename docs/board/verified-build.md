# 构建环境与发货说明

## 1. 构建环境

| 项目 | 值 |
| --- | --- |
| 目标芯片 | ESP32-S3 |
| ESP-IDF | v5.5.1 |
| 板型 | `meijing-xinsi` |
| 固件版本 | 2.0.4 |
| 构建主机 | Windows |
| 应用镜像 | `build/mj-xinsi-release/xiaozhi.bin`，约 4.7 MB |
| 版本标记 | `project_description.json` 和镜像内均为 `2.0.4` |

## 2. 源码与配置核对

- 按构建时的 `compile_commands.json` 检查，66 个参与编译的源文件全部存在。
- `main/main.cc`、`main/boards/common/axp2101.cc`、`main/iot/things/battery.cc` 已包含。
- `main/radar/radar.h`、`main/https_test.h` 已包含。
- `main/assets/common/huanxing.p3` 已包含。
- `partitions/v1/16m.csv`、`CMakeLists.txt`、`main/boards/meijing-xinsi/config.json` 已包含。
- `sdkconfig` 中板型为 `CONFIG_BOARD_TYPE_MEIJING_XINSI=y`，Flash 16MB。

## 3. 发货内容

买家资料包中提供：

- `merged-binary.bin`：整片烧录镜像，写入地址 `0x0`。
- `xiaozhi.bin`：OTA 应用镜像，应用分区地址 `0x100000`。
- `SHA256SUMS.txt` / `MD5SUMS.txt`：固件校验值。

## 4. 复现构建

```bash
# 1. 安装并激活 ESP-IDF 5.5.1
. $HOME/esp/esp-idf/export.sh

# 2. 进入仓库
cd leyy-xxesp

# 3. 编译
idf.py set-target esp32s3
idf.py build
```

如果 `set-target` 覆盖了配置，执行 `idf.py menuconfig` 重新选择：

```text
Xiaozhi Assistant → Board Type → 美好憧憬科技ESP32-S3开发板(新思版)
```

## 5. 卖家已确认

- 本页与仓库当前版本一致，固件版本为 `2.0.4`。
- 源码、板级配置、固件、文档和校验值已随买家资料包核对。
- 买家按本页或在线路书操作即可；遇到环境问题，把报错和操作步骤交给 AI 编程工具按本文命令排查。
