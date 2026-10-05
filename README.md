# ESP32-S3-RLCD-4.2

基于微雪 **ESP32-S3-RLCD-4.2** 的原生 ESP-IDF 项目，使用本机已经安装的 **ESP-IDF V6.1.0** 开发。

## 当前工程状态

工程已建立基础启动程序和板级参数。上电后通过 USB Serial/JTAG 输出 SDK 版本、芯片信息、Flash 和 PSRAM 容量，并每 5 秒输出一次可用内存信息。

当前尚未初始化屏幕、按键、I²C、音频、SD 卡、Wi-Fi 或 BLE，也尚未引入 LVGL / U8g2。板级引脚常量集中在 `main/board_config.h`，方便后续驱动使用。

## 设备情况

| 项目 | 配置 |
| --- | --- |
| 开发板 | Waveshare ESP32-S3-RLCD-4.2 |
| 模组 | ESP32-S3-WROOM-1-N16R8 |
| CPU | Xtensa LX7 双核，最高 240 MHz |
| 内存 | 512 KB 片上 SRAM、16 MB Flash、8 MB PSRAM |
| 无线 | 2.4 GHz Wi-Fi、Bluetooth 5 LE |
| 显示 | 4.2 英寸全反射 RLCD，原生 300 × 400，横屏 400 × 300 |
| 显示控制器 | ST7305，SPI 接口 |
| 音频 | ES8311 编解码器、ES7210 ADC、双麦克风、扬声器接口 |
| RTC | PCF85063，支持独立备用电源 |
| 环境传感器 | SHTC3 温湿度传感器 |
| 存储扩展 | Micro SD 卡槽，官方示例使用 SDMMC，支持 FAT32 |
| 操作 | KEY、BOOT 功能按键；PWR 电源按键 |
| 供电 | USB Type-C、18650 可充电锂电池座及充放电管理电路 |
| 扩展 | 2 × 8、2.54 mm 排母，预留 GPIO、UART、I²C 等接口 |

硬件信息来源：[官方产品文档](https://docs.waveshare.net/ESP32-S3-RLCD-4.2/)及[相关资料](https://docs.waveshare.net/ESP32-S3-RLCD-4.2/Resources-And-Documents/)。

### 显示特点

RLCD 利用环境光反射成像，没有背光；环境越明亮，显示越清晰。官方显示驱动使用黑白 1 bit 像素，一屏帧缓冲为 `300 × 400 / 8 = 15,000` 字节。像素需要按控制器格式排列，不能直接发送普通 RGB 图片缓冲。官方 LVGL 示例会将绘制结果转换为黑白像素。

官方提供 LVGL 8、LVGL 9 和 U8g2 示例。后续接入时需要匹配对应版本及驱动；当前工程尚未选定图形库。

参考：[官方显示驱动](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2/blob/main/02_Example/ESP-IDF/08_LVGL_V8_Test/components/port_bsp/display_bsp.cpp)及[ESP-IDF 示例说明](https://docs.waveshare.net/ESP32-S3-RLCD-4.2/ESP-IDF/)。

### 已核对的引脚

以下配置来自官方综合示例的 [`main/user_config.h`](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2/blob/main/02_Example/ESP-IDF/10_FactoryProgram/main/user_config.h)，尚未通过本工程上板验证。

| 外设 | 信号 | GPIO |
| --- | --- | --- |
| RLCD | MOSI | 12 |
| RLCD | SCK | 11 |
| RLCD | DC | 5 |
| RLCD | CS | 40 |
| RLCD | RST | 41 |
| RLCD | TE | 6 |
| I²C | SDA | 13 |
| I²C | SCL | 14 |

音频、SD 卡、按键和电池 ADC 的完整配置将在对应驱动接入时进一步核对，不应将上述已占用引脚直接分配给其他外设。

### 使用注意

- 安装 18650 电池或插拔 Type-C 时，避免按压屏幕。
- PWR 单击开机、长按关机；电池首次安装后可能需要接入 Type-C 激活。
- RTC 备用电源须使用可充电电池，官方推荐 ML1220 或兼容型号，不可使用 CR1220 等一次性电池。
- 烧录无法连接时，可按住 BOOT 重新上电进入下载模式。

参考：[官方 FAQ](https://docs.waveshare.net/ESP32-S3-RLCD-4.2/FAQ/)。

## 开发环境

| 项目 | 当前本机配置 |
| --- | --- |
| 框架 | ESP-IDF V6.1.0 |
| SDK 路径 | `D:\esp\v6.1\esp-idf` |
| Python 虚拟环境 | `C:\Espressif\tools\python\v6.1\venv` |
| 工具目录 | `C:\Espressif\tools` |
| 工程目录 | `D:\workspace\ESP32\RLCD` |
| 目标芯片 | `esp32s3` |
| 编辑器 | VS Code + Espressif ESP-IDF 扩展 |
| 配置的烧录端口 | `COM9`，实际端口以设备枚举结果为准 |
| 调试配置 | `board/esp32s3-builtin.cfg` |

本机 SDK 的 `tools/cmake/version.cmake` 声明版本为 `6.1.0`；`idf.py --version` 可能同时包含开发分支和提交信息。以实际安装版本输出为准。

微雪当前文档要求 ESP-IDF 5.5.0 及以上；官方示例的依赖不自动等同于兼容 6.1.0，接入外设时需要逐项验证。

## 工程结构

```text
RLCD/
├── CMakeLists.txt          # ESP-IDF 项目入口，项目名 rlcd
├── sdkconfig.defaults     # 可复现的板级默认配置
├── partitions.csv         # NVS、PHY 和 4 MB factory 应用分区
├── main/
│   ├── CMakeLists.txt      # 主组件依赖
│   ├── board_config.h     # 设备参数和已核对的引脚
│   └── main.c             # 启动诊断和周期日志
├── .vscode/settings.json  # 本机 SDK、端口和 clangd 配置
├── .clangd                # clangd 编译参数设置
├── .gitignore
└── README.md
```

`sdkconfig` 和 `build/` 由 ESP-IDF 生成，不纳入版本管理；`sdkconfig.defaults` 是新配置的起点。已有 `sdkconfig` 时，修改 defaults 不会自动覆盖其中已设置的选项，应通过 `menuconfig` 更新并用 `save-defconfig` 保存。

### 默认构建配置

- CPU：240 MHz。
- Flash：16 MB，QIO，80 MHz。
- PSRAM：Octal，80 MHz，上电初始化，加入 `malloc` 可用堆。
- 控制台：USB Serial/JTAG。
- 分区：NVS `0x9000` / `0x6000`，PHY `0xF000` / `0x1000`，factory `0x10000` / 4 MB。剩余 Flash 暂未分配；当前不包含 OTA 和文件系统分区。

## 编译、烧录与日志

在 VS Code 中运行 **ESP-IDF: Open ESP-IDF Terminal**，确认终端激活本机 6.1 环境，再执行：

```powershell
Set-Location D:\workspace\ESP32\RLCD
idf.py --version
idf.py build
```

首次构建会读取 `sdkconfig.defaults` 并选择 `esp32s3`。若需切换已有工程的目标，可执行 `idf.py set-target esp32s3`；该命令会重建配置，执行前应保存自定义设置。

连接设备后，按实际串口执行：

```powershell
idf.py -p COM9 flash monitor
```

用 `Ctrl+]` 退出串口监视器。也可以分别执行 `idf.py -p COM9 flash` 和 `idf.py -p COM9 monitor`。

配置和体积检查：

```powershell
idf.py menuconfig
idf.py save-defconfig
idf.py size
```

基础程序运行时应输出板名、SDK 版本、双核芯片信息、约 16 MB Flash 和 8 MB PSRAM 的实际检测值，以及每 5 秒一次的 `Alive` 日志。屏幕此阶段不会显示新的应用界面。

## 验证记录

- 初始化日期：2026-10-05。
- 编译验证：使用本机 ESP-IDF 6.1.0 配置和 Xtensa GCC 15.2.0，通过 `idf.py reconfigure build size`；已核对生成配置中的 12 项板级参数。
- 构建产物：`build/rlcd.bin`（160,576 字节）、`build/bootloader/bootloader.bin`（22,608 字节）和 `build/partition_table/partition-table.bin`（3,072 字节）。
- 硬件验证：尚未烧录；实际启动、USB 日志及内存检测需上板确认。

## 官方资源

- [产品文档](https://docs.waveshare.net/ESP32-S3-RLCD-4.2/)
- [原理图、数据手册及示例入口](https://docs.waveshare.net/ESP32-S3-RLCD-4.2/Resources-And-Documents/)
- [ESP-IDF 开发教程](https://docs.waveshare.net/ESP32-S3-RLCD-4.2/ESP-IDF/)
- [官方示例仓库](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2)
- [综合出厂示例](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2/tree/main/02_Example/ESP-IDF/10_FactoryProgram)
