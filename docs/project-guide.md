# ESP32-S3-RLCD-4.2 项目说明与开发指南

基于微雪 **ESP32-S3-RLCD-4.2** 的原生 ESP-IDF 项目，使用本机已经安装的 **ESP-IDF V6.1.0** 开发。

## 当前工程状态

工程已接入 **SHTC3 温湿度读取和 RLCD 显示**。上电后通过 USB Serial/JTAG 输出 SDK、芯片和内存信息，然后每 **2 秒**采样一次，在 **400 × 300 横屏**上显示温度（°C）和相对湿度（%RH），保留一位小数。

屏幕使用内置黑白点阵字体；无需 LVGL / U8g2。传感器页显示 **本机 SHTC3、客厅空调、书房空调**，并可手动翻看主机监控实体。读取失败或 CRC 校验失败时，本机数值变为 `--.-`，之后继续采样。已接入 **Home Assistant 实体读取、Wi-Fi 热点网页配网、NVS 保存、自动重连和 KEY 长按重新配网**；音频、SD 卡、BLE 和 RTC 尚未接入。

### 统一 HEADER 与北京时间

所有页面（含配网视图）共用固定 HEADER，显示页名、北京时间（UTC+8）`HH:MM`、Wi-Fi 图标/信号、电池图标和估算电量，并在第二行显示时区与当前内容屏序号。没有固定 Footer。日期与电池详情放在网络/系统页；传感器页保留三张温湿度卡片，并新增一屏 Host Monitor 实体。时间按分钟重绘，数据仍按原周期刷新。获得 Wi-Fi IP 后通过 SNTP 自动校时，重连时重新请求校时；采用 [ESP-IDF 的系统时间接口](https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32s3/api-reference/system/system_time.html)。校时在网络任务中进行，不等待网络响应，也不阻塞按键和传感器采样。

首次校时成功前显示 `--:--`；网络/系统卡片中的日期也使用占位符。校时后断网仍使用系统时钟继续走时；断电重启后需要重新联网校时，当前未接入板载 PCF85063 RTC。NTP 服务器默认为 `ntp.aliyun.com`，服务器和时区可通过 `main/board_config.h` 中的 `BOARD_NTP_SERVER`、`BOARD_TIMEZONE` 修改；网络需允许 DNS 和 NTP（UDP 123）。

### Home Assistant 实体卡片

三张卡片从上到下为 `LOCAL SHTC3`（本机）、`LIVING ROOM`（客厅）和 `STUDY`（书房），左侧温度，右侧相对湿度。内置字体暂不支持中文和度数符号，因此屏幕使用 `C` 表示摄氏度。KEY 短按切换到合并的网络/系统页面，BOOT 单击浏览其网络或系统卡片。

| 卡片 | 温度实体 | 湿度实体 |
| --- | --- | --- |
| 客厅 | `sensor.xiaomi_c24_08dc_temperature` | `sensor.xiaomi_c24_08dc_relative_humidity` |
| 书房 | `sensor.xiaomi_h39h00_47aa_temperature` | `sensor.xiaomi_h39h00_47aa_relative_humidity` |

传感器页按 BOOT 翻屏：首屏显示上述温湿度卡片；第二屏以双列紧凑卡片集中显示 Host Monitor 的 CPU、内存、磁盘使用率、CPU 温度、运行时间和网络收发速率。主机数值旁显示 HA 返回的 `attributes.unit_of_measurement`；UPTIME 按返回的 RFC3339 时间换算为 UTC+8；RLCD 内置字体不含度数符号，因此 `°C` 显示为 `C`。

| 用途 | 实体 ID |
| --- | --- |
| CPU 使用率 | `sensor.miwifi_rd15_srv_cpu_usage` |
| 内存使用率 | `sensor.miwifi_rd15_srv_memory_usage` |
| 磁盘使用率 | `sensor.miwifi_rd15_srv_disk_usage` |
| CPU 温度 | `sensor.miwifi_rd15_srv_cpu_temperature` |
| 运行时间 | `sensor.miwifi_rd15_srv_uptime` |
| 网络接收速率 | `sensor.miwifi_rd15_srv_network_receive_rate` |
| 网络发送速率 | `sensor.miwifi_rd15_srv_network_transmit_rate` |

- HA 地址在 `main/board_config.h` 的 `BOARD_HA_URL`，默认 `http://192.168.31.41:8123`。ESP32 应连接到可访问该地址的家庭局域网。
- 将长期访问令牌单独放在项目根目录 `rc.key`，无需添加 `Bearer` 或引号；支持末尾换行及 UTF-8 BOM。构建时嵌入固件，所有请求添加 `Authorization: Bearer <token>`，协议依据 [HA REST API 文档](https://developers.home-assistant.io/docs/api/rest/)。令牌不输出到日志，`rc.key` 已被 Git 忽略；构建产物包含令牌，应作为私有文件保存。修改令牌后须重新构建并烧录；首次添加或删除文件时先运行 `idf.py reconfigure`。
- 没有 `rc.key` 也可编译，此时 HA 实体显示 `NO TOKEN`，本机传感器正常工作。
- 独立后台任务逐个 GET 四个温湿度实体和七个 Host Monitor 实体，地址为 `http://192.168.31.41:8123/api/states/<entity_id>`，完成一轮后等待 **15 秒**，单次 HTTP 操作超时 **4 秒**，响应限制为 4095 字节；请求使用 `Authorization: Bearer <token>`，读取 `state` 和 `attributes.unit_of_measurement`；不读取整个实体列表，不执行空调控制。轮询间隔、超时和过期时间均在 `main/board_config.h`。
- `unknown`、`unavailable` 或无效数值显示占位符；401/403 显示 `AUTH ERROR`，404 显示 `NOT FOUND`，网络或响应读取异常显示 `READ ERROR`。断网立即隐藏 HA 当前值，超过 **45 秒**未成功读取的数据显示 `STALE`。每个实体独立更新，单项失败不影响其余读数。连接恢复后自动读取。
- UI 只读取任务快照，所有屏幕绘制仍在主任务中；本机 SHTC3 保持每 2 秒采样，按键轮询保持 20 ms。

解析器回归测试编译实际 `main/ha_state.c` 与项目的 cJSON，覆盖正常值、独立湿度范围、非数值、离线状态、溢出、实体不匹配、截断和多余 JSON：

```powershell
python tests/test_ha_state.py --zig <zig.exe路径>
```

上板验收：确认三张卡片显示 → 断开 Wi-Fi 后本机继续刷新且 HA 值隐藏 → 重连后 HA 恢复 → 检查 KEY 切页及配网。令牌失效时更换 `rc.key` 后重新构建烧录。

### 页面切换

共有三个顶层页面：温湿度、Codeck 实时快照、网络/系统。温湿度继续每 2 秒采样。网络/系统页可翻三屏：连接、SSID、IP、当前 RSSI 和配网入口；芯片、Flash、PSRAM、显示参数、SDK、电池和日期；以及应用分区、内部 heap 和 PSRAM heap 的动态占用条。内存数据每 30 秒采样一次，显示当前空闲量、启动以来低水位和最大连续块。

- **KEY 短按并释放**：温湿度 → Codeck → 网络/系统 → 温湿度。
- **BOOT 单击并释放**：翻到当前页面下一屏；末屏回首屏。传感器页有两屏，网络/系统页有三屏，配网视图不翻屏。切换顶层页面时保留各页位置，重启从各页首屏开始；不写入 NVS。
- **取消自动翻屏**：等待或刷新数据不推进页面；没有滚动文字。
- **KEY 长按 3 秒**：仅在网络/系统页进入 Wi-Fi 配网；其他普通页面无动作。长按释放不会再切换页面。BOOT 长按无操作。
- 首次未配网时自动显示配网页，短按返回温湿度页，再短按循环浏览。浏览不会关闭配网热点；到网络/系统页长按可再次显示配网提示。
- 配网成功后自动退出配网页，恢复之前选择的普通页面。

页面状态与刷新在 `main/ui.c`，各页面绘制在 `main/ui_pages.c`，按键消抖与短按/长按识别在 `main/button.c`。主任务每 20 ms 轮询，消抖 40 ms；切页使用缓存数据重绘，不等待 2 秒采样周期。所有绘制和 SPI 刷新仍由主任务执行。

烧录后建议检查：KEY 循环三页 → BOOT 手动翻屏及位置保留 → 仅网络/系统页长按配网 → 松开不跳页 → 配网页短按浏览 → 配网成功恢复页面。BOOT 为 GPIO0、KEY 为 GPIO18，均为低电平有效，已核对本地出厂按键示例。当前连接 RSSI 每 2 秒由网络线程采样，HEADER 的四级信号阈值为 -55/-67/-75 dBm；断网带叉、连接中带时钟、信号未知带问号。

### 系统页电池信息

网络/系统页的电池卡片显示电压（V）、估算百分比和按电压推测的检测状态；所有 HEADER 同时显示估算电量。沿用本地出厂示例的 ADC1 通道 3（GPIO 4）、12 dB 衰减、12 位采样和 3 倍分压还原，使用 ESP-IDF 曲线拟合校准；每 2 秒读取 16 次取平均，与温湿度采样共用主任务周期。电池初始化或采样失败显示 `READING UNKNOWN` 和占位符，HEADER 显示 `--%`，不影响其他页面。

电量沿用出厂示例：3.0～4.12 V 线性换算为 0～100%，上下限截断，显示 `EST` 标明估算。它并非电量计，充电和负载会影响百分比。

**是否插入电池目前仅按电压推测，并非独立插入检测信号。** 默认电压达到 2.0 V 时显示检测到电池，低于阈值显示未检测到，后者隐藏百分比、保留采样电压。USB 供电时空电池座可能出现残余或充电电压，极低电压的电池也可能被判为未检测到；尚未确认原理图及实机空电池座读数，因此不能保证物理插入状态准确。阈值、分压比及电量端点集中在 `main/board_config.h` 的 `BOARD_BATTERY_*`。

上板验收：USB 供电且不装电池时记录电压和检测状态；安装电池后用万用表比较显示电压；确认系统页每 2 秒更新、KEY 切页正常。若空电池座读数与装电池读数重叠，单靠该 ADC 无法可靠判定插入，需要独立硬件检测信号。

### Codeck 实时快照

设备只调用 `GET https://codeck.wangj.de/api/device/v1/snapshot`，请求携带 `Accept: application/json` 和设备 Bearer 凭据，不调用管理接口、不触发刷新或写操作，也不跟随重定向。后台任务执行网络请求，主任务负责绘图，按键、温湿度和时间页面继续工作。

**凭据配置**：将设备访问凭据原文保存到根目录 `codeck.key`，不用添加 `Bearer`、引号或 JSON；支持 UTF-8 BOM 和末尾换行。此文件已加入 Git 忽略，与 HA 的 `rc.key` 独立。CMake 构建时将其嵌入固件，首次创建或删除文件后执行 `idf.py reconfigure`，更换凭据后重新构建和烧录。缺失或无效文件时页面显示 `NO DEVICE KEY`，不发送请求。日志只记录固定状态码，HTTP 客户端的头部调试日志被抑制。**构建产物含凭据，应私有保存；当前不是加密 NVS 或在线凭据配置方案。**

**TLS 与轮询**：等待 SNTP 成功校时之后才发起 HTTPS，使用 ESP-IDF `esp_crt_bundle_attach` 与完整 Mozilla 根证书 bundle，验证证书链、主机名和有效期，不固定叶证书、不关闭验证。成功后每 45 秒拉取一次；TLS、DNS、网络错误和连接超时依次退避约 10、20、40、60 秒，之后保持 60 秒，再加 0–10% 随机抖动。503、403、解析、容量及版本错误仍退避至最多约 5 分钟。401 最快 15 分钟重试一次，Wi-Fi 重连不会绕过认证冷却。各错误分别显示独立状态。

**自动恢复**：每次请求结束都销毁 HTTP/TLS 客户端，下次重建连接。Wi-Fi 或校时门控恢复后立即尝试读取，不再等待旧网络错误的退避计时。连续 3 次可恢复的传输失败时，请求网络工作线程重连已保存的 Wi-Fi；该恢复最多每 5 分钟一次，配网期间、测试新 Wi-Fi 配置期间不执行。明确的证书校验错误、401、403、503、数据解析错误不会触发 Wi-Fi 重连。Wi-Fi 恢复会短暂影响 HA 请求，已知数据仍保留。页面右上角显示 `CONNECTING` 或 `RETRY <秒数>S`，串口记录失败次数、下次重试间隔、排队恢复及成功恢复。真正的证书或公网不可达问题仍需修复网络或信任配置，重试不跳过 TLS 校验。

连接使用域名解析得到的 IPv4 地址，仍以原域名进行 SNI 和主机名校验，不固定服务器 IP。单次网络操作超时为 `BOARD_CODECK_TIMEOUT_MS`（20 秒）。页面分别显示 `DNS ERROR`、`CONNECTION TIMEOUT` 和 `TLS CONNECTION ERROR`。串口诊断仅记录请求阶段、耗时和数字错误码，不记录请求头或凭据。`Failed to open new connection in specified timeout` 表示尚未收到 HTTP 响应；旧日志中的 `status 7` 是内部网络错误状态，不是 HTTP 状态码。电脑端请求成功不能证明设备网络路径可达；如烧录后仍失败，可通过新的 `Snapshot transport` 日志区分失败阶段。

证书 bundle 需开启 `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY=y`，配置已写入 `sdkconfig.defaults` 并同步当前 `sdkconfig`。本地 SDK 默认关闭此项；服务器在 2026-10-06 返回的链包含 WE1 → GTS Root R4 → GlobalSign 的交叉签名证书，旧模式可能沿链寻找 bundle 中已无的 GlobalSign 旧根而失败。开启后可选择 bundle 中受信任的 GTS Root R4 验证链，不添加叶证书、不关闭验证。已有项目只改 defaults 不会覆盖当前 sdkconfig，重新配置后应确认 `build/config/sdkconfig.h` 中该项为 1。可用 `python tests/probe_codeck.py --ca-file D:/esp/v6.1/esp-idf/components/mbedtls/esp_crt_bundle/cacrt_all.pem` 验证接口在 SDK 根证书集合下可达；此主机检查不代替板上握手验收。

**页面与数据**：KEY 切换环境 → Codeck → 网络/系统；BOOT 手动翻屏。Codeck 首屏使用官方 OpenAI Blossom，显示 CLI、RUN、SCHED、SERVICE 四项图标状态，以及 **5H、7Day 剩余额度**和重置时间。SCHED 只表示调度器开关，不是运行计时器。随后每屏两个账户，整行卡片上下排列；每个账户的全部币种留在同一卡片，单币种大字、多币种分行，名称最多两行后省略。长金额使用窄数字字形完整显示，不裁切高位，不合计币种。没有自动轮播或固定 Footer，阅读位置保留。

- 严格解析 schema 1，使用最新字段 `service.codex_cli_available` 和 `balances.items[].name`；未知整数版本显示 `UNSUPPORTED API VERSION`，不猜字段。
- 金额保留原始十进制字符串，绘图时按十进制运算四舍五入到两位小数，不经浮点数覆盖原值。每个配置、每个币种独立显示，不相加、不换算。
- 额度不可用、空窗口、无余额观测、空金额均显示 `--`；成功响应中的其他可用分区继续显示。`stale=true` 显示 `OLD` 并保留观测时间。
- 请求失败保留 RAM 中最后成功快照，显示具体错误、`LAST KNOWN` / `CACHED` 和最后快照生成时间；任务计数标为 `LAST`。首次尚无成功数据或重启后没有缓存时显示占位符。
- 时间统一显示 UTC+8，正确处理带偏移的时间戳和跨日/月/年，并区分快照生成时间、额度观测时间、账户观测时间和窗口重置时间。配置名支持常用中文，最多两行后用省略号显示，模型保留原文；其他未覆盖字符显示方框。
- 初始响应容量为 4 KiB（另加一个终止字节），完整接收后才解析，不静默截断。模型最多保存 8 条配置、每条 4 个币种；金额字符串最多 63 字节，名称最多 127 UTF-8 字节，平台最多 31 字节，币种代码最多 7 字节。超出模型或响应容量时整次更新失败并保留上次快照，不丢弃部分配置。

主要文件：`main/codeck_client.c/.h`（凭据、HTTPS、后台轮询）、`main/codeck_state.c/.h`（契约解析、精确金额格式化、失败状态和退避）、`main/codeck_page.c/.h`（真实分页绘图）、`main/codeck_label.c/.h` 与 `assets/fonts/`（中文配置名点阵）；`main/ui.c`、`main/main.c`、`main/clock_service.c/.h`、`main/board_config.h`、`main/CMakeLists.txt` 和 `sdkconfig.defaults` 完成入口、线程安全校时门控、URL、凭据嵌入和证书配置。

官方图标库为 `main/brand_icons.c/.h` / `brand_icons_data.h`，包含 24、32、48 px 三种大小，共 1,464 字节；[图标图库](brand-icons/index.html) 提供原始资源与来源。配置名字体为 GNU Unifont 的 16 px 点阵子集，约 900 KiB，只驻留 Flash，不在设备上加载为同等大小的 RAM 图片，许可证随资源保留。

[统一 UI 生产预览](ui-preview/index.html) 使用实际 UI、按键、解析器与绘图代码，覆盖正常/离线、连接中、长名称、四币种长金额、8 个账户及额度边界。[Codeck 状态预览](codeck-live-preview/index.html) 保留认证、TLS、服务、版本、容量和缺失数据样例。旧版 [历史布局演示](codeck-preview/index.html) 不代表当前固件，其演示模块已从固件构建中移除。

主机端预览的生成命令、真实快照用法和实机验证范围见[预览流程说明](ui-preview-workflow.md)。

当前设备运行画面见仓库根目录的 [README](../README.md)。本指南保留完整功能、硬件和开发说明。


### Wi-Fi 配网

1. 首次启动且没有有效的已保存配置时，设备创建 **`RLCD-xxxxxx`** 热点。屏幕显示热点名称和 **8 位纯数字热点密码**；密码首次生成后保存，重启不会改变。旧版 12 位密码在升级后自动替换为 8 位数字密码，以屏幕显示为准。
2. 手机连接这个热点。若提示“无法上网”，选择保持连接；如未自动弹出配网页面，手动打开 **`http://192.168.4.1`**。
3. 中文页面列出最多 20 个扫描结果（同名网络合并），也可以手动输入隐藏网络的 SSID。设备支持 **2.4 GHz Wi-Fi**。
4. 输入家庭 Wi-Fi 密码，点击“连接并保存”。SSID 按 UTF-8 字节计数，最多 32 字节；密码支持 8–63 个 ASCII 字符或 64 位十六进制密钥，开放网络留空。支持开放网络、WPA2/WPA3 个人网络，不提供企业网络认证或路由器门户登录。
5. 获取 IP 地址后才保存新配置，并在页面和屏幕显示连接结果。热点在成功后 **10 秒**关闭，手机可切回家庭 Wi-Fi。配网失败或 30 秒超时后，热点继续保留，可修改并重试；已有配置不会因一次错误密码被清除。
6. 后续开机自动连接已保存的网络；断线或失去 IP 地址时自动重连，每次失败后等待 **5 秒**再尝试。普通断网不会清除配置，也不会自动开启热点。
7. 更换网络时，长按 **KEY（GPIO 18，低电平有效）3 秒**进入配网。按键引脚与电平已按本地出厂示例 `components/port_bsp/button_bsp.c` 核对。进入配网时暂停旧网络的重连；若放弃更换，可重启设备重新使用旧配置。

配网期间温湿度仍每 2 秒采样，配网页上部显示配网提示，下部显示温湿度；连接成功后恢复之前浏览的普通页面，网络页显示设备 IP。网络事件由独立任务处理，按键轮询和屏幕绘制只在主任务执行。

热点使用 WPA2，网页及 DNS 只对设备热点接口服务。提交接口校验页面会话令牌，SSID 使用文本方式显示；配置和热点密码不会输出到日志或提交到外部服务。家庭 Wi-Fi 配置保存于 NVS `wifi_setup` 命名空间，当前未启用 NVS 加密；NVS 初始化异常时记录错误并继续温湿度显示，不自动擦除已有数据。获取 IP 表示连上路由器，不代表已验证互联网可用。

网页版仅使用设备内嵌资源，不依赖 CDN 或互联网。自动弹窗采用 DNS 重定向和 HTTP 门户机制，受手机系统影响；手动访问上述地址是备用入口。

ESP-IDF 6.1 已移除内置 `json` 组件。本工程通过 `main/idf_component.yml` 引入 `espressif/cjson`，`dependencies.lock` 固定解析结果；首次构建需要下载依赖。`sdkconfig.defaults` 将 `CONFIG_CJSON_NESTING_LIMIT` 设为 8，限制网页接口 JSON 解析的栈占用；已有配置应在 `menuconfig` 的 `Component config → cJSON` 中同步此值。

可调整的参数集中在 `main/board_config.h`：KEY/BOOT 引脚、长按时间、连接超时、重连间隔和热点关闭延时。

### 配网 HTTP 日志排查

- `403 Forbidden - Connect to the device hotspot`：旧代码只识别 IPv4 socket 地址，而启用 IPv6 的 ESP-IDF HTTP 服务会以 `::ffff:192.168.4.1` 返回 IPv4 客户端的本地地址。本版本已使用完整 socket 地址缓冲区，并兼容 IPv4 与映射形式的 IPv6；家庭局域网接口仍不能访问配网页面。
- `URI '/mmtls/...' not found` 等非配网路径：手机后台请求可能经热点 DNS 指向设备，未知 HTTP 路径会重定向到配网入口。这条日志不是 Wi-Fi 连接失败事件。
- `parser error = 16` / `400 Bad Request`：HTTP 解析器遇到了无效的请求方法；可能来自手机后台的非 HTTP 流量，仅凭这条日志不能确定发送者或协议。手机保持连接设备热点后，用浏览器访问 **`http://192.168.4.1`**；该服务不接收 HTTPS。

### 温湿度采样与显示配置

板级参数集中在 `main/board_config.h`：

| 配置 | 当前值 |
| --- | --- |
| SHTC3 地址 | `0x70`（7 bit 地址） |
| I²C 控制器 / 时钟 | I²C 0 / 400 kHz |
| I²C SDA / SCL | GPIO 13 / 14 |
| RLCD 控制器 / 时钟 | SPI3 / 10 MHz，mode 0 |
| 显示方向 | 横屏 400 × 300 |
| 采样间隔 | `BOARD_SAMPLE_INTERVAL_MS = 2000` |
| 温度补偿 | `BOARD_TEMPERATURE_OFFSET_C = (-4.0f)`，与 demo 一致 |

SHTC3 采用唤醒 `0x3517` → 普通模式测量 `0x7866`（温度先返回、不使用时钟拉伸）→ 等待至少 20 ms → 读取 6 字节 → 休眠 `0xB098` 的流程，分别校验温度和湿度数据的 CRC-8（初始值 `0xFF`、多项式 `0x31`）。成功和失败的测量都会尝试让传感器返回休眠状态。

换算公式：`T = -45 + 175 × rawT / 65536 + offset`；`RH = 100 × rawRH / 65536`。按用户要求，本工程与本地出厂示例一致，温度固定减去 4°C，屏幕与串口均显示补偿后的温度；湿度不添加偏移。调整 `BOARD_TEMPERATURE_OFFSET_C` 后需重新编译并烧录。

屏幕初始化寄存器、复位时序和横屏像素排列参考用户指定的本地示例：

```text
D:\workspace\ESP32\ESP32-S3-RLCD-4.2-Demo\02_ESP-IDF\10_FactoryProgram
```

主要参考文件为 `main/main.cpp`、`main/user_config.h`、`components/port_bsp/display_bsp.cpp` 和 `components/port_bsp/i2c_equipment.*`。该示例的 `user_config.h` 写的是 300 × 400，但 `main.cpp` 实际传给显示驱动的是 **400 × 300**；本工程采用后者，并保留本地版本的屏幕初始化参数。TE 引脚当前未使用。

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

官方提供 LVGL 8、LVGL 9 和 U8g2 示例。本工程目前直接绘制黑白点阵文本，使用 15,000 字节的内部 DMA 帧缓冲和同步 SPI 传输，发送完成后才更新下一帧，避免传输过程中修改缓冲区。

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
│   ├── main.c             # 启动诊断、采样循环和温湿度界面
│   ├── shtc3.c / .h       # ESP-IDF I²C 温湿度驱动和 CRC 校验
│   ├── rlcd.c / .h        # RLCD 初始化、像素排列、字体和同步 SPI 刷新
│   ├── wifi_setup.c / .h  # 网络任务、HTTP 门户、NVS、KEY 与自动重连
│   ├── home_assistant.c/.h # HA 后台读取、令牌处理与温湿度快照
│   ├── ha_state.c / .h    # HA JSON 实体及数值校验
│   ├── portal_codec.c/.h  # 输入校验与有边界检查的 DNS 报文处理
│   ├── wifi_portal.html   # 内嵌中文配网页面
│   └── idf_component.yml  # ESP-IDF 6.1 的 cJSON 依赖
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

程序运行时应先输出板名、SDK 版本、芯片及内存信息。未配网时屏幕进入 `WIFI SETUP` 并显示热点名称和密码；已保存配置时显示温湿度和 Wi-Fi 连接状态。串口每 2 秒输出类似 `Temperature: 25.3 C, humidity: 48.6 %RH` 的实测值。读取失败时串口会记录具体 ESP-IDF 错误，屏幕显示占位符并继续重试。

### 配网验证

主机测试不需要连接开发板：

```powershell
# 使用可编译主机程序的 Clang；ESP 专用 Clang 不包含 x86 主机后端。
python tests/test_portal_codec.py --clang <主机clang路径>
# Windows 也可使用 Zig，不需要安装 Windows SDK。
python tests/test_portal_codec.py --zig <zig.exe路径>
# 安装了 Node.js 时可运行页面交互测试。
node tests/test_wifi_portal.js
```

主机测试直接编译实际 `portal_codec.c`，覆盖热点 IPv4/映射 IPv6 地址识别和家庭局域网地址拒绝，以及 DNS IPv4 响应、AAAA 空响应、截断数据、压缩指针、容量边界和 20,000 组确定性随机报文；输入测试覆盖 SSID 字节限制、开放网络和密码长度。页面测试执行实际内联脚本，覆盖网络选择、输入限制、失败重试、响应丢失和成功反馈。

上板验收依次检查：首次启动热点及屏幕提示 → 手机扫描和正确密码配网 → 热点关闭 → 断电重启自动连接 → 关闭路由器后自动重连 → KEY 长按重新配网 → 错误密码和隐藏网络 → 配网期间温湿度持续刷新。自动弹窗需分别在实际 Android/iOS 手机上验证。

## 验证记录

- 统一 UI 版本（2026-10-06）：生产 UI 集成测试与双按键消抖测试通过；25 张预览通过字形、边界和文字/图形重叠检查，包含两行中文、四币种长金额、8 账户、未知数据和额度边界。69 个 Codeck 状态用例及模拟传输回归、40 个 HA 解析用例通过；ESP-IDF 6.1 构建通过。尚未烧录，实机 BOOT、RSSI、电池和屏幕效果待验证。规格见 [本地文档](specs/unified-ui-and-manual-navigation.md) 和 [Issue #1](https://github.com/EziosWJ/ESP32-S3-RLCD-4.2/issues/1)。

- 初始化日期：2026-10-05。
- Home Assistant 版本（2026-10-06）：ESP-IDF 6.1.0 构建通过，应用固件为 1,026,576 字节，4 MB 分区剩余 76%；40 个主机解析用例通过。四个真实实体均返回 HTTP 200，原始响应通过生产解析器校验；实际 UI 绘制代码的正常值、部分失败及数值边界预览已检查。尚未烧录，ESP32 上的读取、断网恢复及实际屏幕效果待上板验证。
- 编译验证：使用本机 ESP-IDF 6.1.0 配置和 Xtensa GCC 15.2.0，通过 `idf.py reconfigure build size`；已核对生成配置中的 12 项板级参数。
- 温湿度显示版本：通过 ESP-IDF 6.1.0 `idf.py build`，构建日志无编译警告或错误；应用固件为 226,736 字节。
- Wi-Fi 配网版本：通过 ESP-IDF 6.1.0 构建和分区容量检查；8 位数字密码及双栈 HTTP 地址兼容版本应用固件为 917,136 字节，4 MB 应用分区剩余约 78%，无源码编译警告或错误。
- 配网主机测试：热点 IPv4/映射 IPv6 接受与其他地址拒绝的回归用例、16 组凭据边界、DNS 协议/截断/容量用例和 20,000 组随机报文通过；实际页面脚本的输入校验、失败重试、响应丢失和成功反馈测试通过。浏览器可视化及 Android/iOS 自动弹窗尚待验证。
- 示例核对：屏幕初始化的 27 条命令、参数字节和延时与指定本地出厂示例一致；已检查界面正常值、极值和错误状态的文字范围与字体覆盖。
- 构建产物：`build/rlcd.bin`、`build/bootloader/bootloader.bin` 和 `build/partition_table/partition-table.bin`；固件体积随功能变化，以最新构建为准。
- 硬件验证：用户回传日志已确认温湿度采样和 HTTP 请求到达设备；双栈 HTTP 地址识别修复需重新烧录后确认手机配网流程，内存及自动弹窗仍需上板验证。

## 官方资源

- [产品文档](https://docs.waveshare.net/ESP32-S3-RLCD-4.2/)
- [原理图、数据手册及示例入口](https://docs.waveshare.net/ESP32-S3-RLCD-4.2/Resources-And-Documents/)
- [ESP-IDF 开发教程](https://docs.waveshare.net/ESP32-S3-RLCD-4.2/ESP-IDF/)
- [官方示例仓库](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2)
- [综合出厂示例](https://github.com/waveshareteam/ESP32-S3-RLCD-4.2/tree/main/02_Example/ESP-IDF/10_FactoryProgram)
