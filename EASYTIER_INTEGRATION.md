# EasyTier ESP-IDF 集成手册

适用：ESP32-S3 / ESP-IDF 6.x，用 `myqfeng/easytier-esp` 把一个 ESP 节点接入 EasyTier 三层网络，并验证到目标主机的 TCP/HTTP 连通性。

本项目落地文件：

- `main/easytier_link.h`：纯 C 接口
- `main/easytier_link.cpp`：C++ 桥接、`et::Inst` 生命周期、事件回调
- `main/main.c`：Wi-Fi 状态驱动 connect/disconnect，10 秒状态打印
- `main/idf_component.yml`：组件依赖
- `main/CMakeLists.txt`：源文件、组件名、TOML 嵌入
- `easytier.toml.example`：配置模板；真实 `easytier.toml` 被 gitignore

---

## 1. 接依赖

`main/idf_component.yml`：

```yaml
dependencies:
  idf: ">=6.1.0"
  myqfeng/easytier-esp: "^0.1.0"
```

`main/CMakeLists.txt` 的 `PRIV_REQUIRES` 必须写组件实际构建名，不是仓库名：

```cmake
myqfeng__easytier-esp
```

同时把桥接源文件加入 `SRCS`：

```cmake
"easytier_link.cpp"
```

第一次构建会自动拉组件；国内网络先设代理：

```powershell
$env:HTTP_PROXY="http://127.0.0.1:7897"
$env:HTTPS_PROXY="http://127.0.0.1:7897"
idf.py build
```

---

## 2. TOML 配置与 secret

复制模板：

```powershell
Copy-Item easytier.toml.example easytier.toml
```

真实文件内容：

```toml
instance_name = "esp32-rlcd-42"
hostname = "esp32-rlcd-42"
ipv4 = "10.126.31.42/24"

[network_identity]
network_name = "home-rc"
network_secret = "真实 secret"

[[peer]]
uri = "tcp://ty.ezios.cn:11030"

[flags]
mtu = 1380
enable_encryption = true
```

规则：

- `easytier.toml` 只放本地，必须进 `.gitignore`；提交前 `git status` 不能出现它。
- `ipv4` 选目标 overlay 网段内的**空闲地址**。和目标 LAN 同网段可以用，但先确认没有物理设备占用；有冲突就换地址或换网段。
- `mtu` 先按 1380；目标网络支持 jumbo 也不要贸然调大，ESP 侧缓冲区按小 MTU 设计更稳。
- `network_secret` 不要写进文档、日志、issue、截图。
- `REPLACE_ME` 占位必须让 `easytier_start()` 直接失败，不能静默上线。

### 新克隆没有 easytier.toml 怎么办

`main/CMakeLists.txt` 做条件嵌入：有真实文件用真实文件，没有就用模板，符号名统一为 `easytier_toml`：

```cmake
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../easytier.toml")
    set(EASYTIER_TOML "${CMAKE_CURRENT_SOURCE_DIR}/../easytier.toml")
else()
    set(EASYTIER_TOML "${CMAKE_CURRENT_SOURCE_DIR}/../easytier.toml.example")
endif()
target_add_binary_data(${COMPONENT_LIB} "${EASYTIER_TOML}" TEXT RENAME_TO easytier_toml)
```

不要用 `EMBED_TXTFILES "../easytier.toml"` 直接依赖一个被 gitignore 的文件，否则别人克隆后构建直接报：

```text
ninja: error: '.../easytier.toml', needed by 'easytier.toml.S', missing
```

---

## 3. C/C++ 桥接约定

头文件只暴露 C 类型：

```c
typedef enum {
    EASYTIER_STATE_UNINITIALIZED = 0,
    EASYTIER_STATE_STOPPED,
    EASYTIER_STATE_WAITING_UNDERLAY,
    EASYTIER_STATE_CONNECTING,
    EASYTIER_STATE_HANDSHAKING,
    EASYTIER_STATE_SYNCING,
    EASYTIER_STATE_READY,
    EASYTIER_STATE_STOPPING,
    EASYTIER_STATE_ERROR,
} easytier_state_t;

typedef struct {
    easytier_state_t state;
    uint32_t virtual_ipv4;
    uint8_t prefix_len;
    uint64_t peer_id;
    uint32_t direct_peers;
    uint32_t reachable_routes;
    uint64_t dropped_packets;
    esp_err_t last_error;
} easytier_status_t;

esp_err_t easytier_start(void);
esp_err_t easytier_connect(void);
esp_err_t easytier_disconnect(void);
esp_err_t easytier_status(easytier_status_t *out);
```

实现侧：

- `static std::optional<et::Inst>` 管生命周期；不要全局裸指针。
- 事件回调只做一件事：把 `et::Status` 抄进 atomic 快照，立刻返回。不要在回调里连 Wi-Fi、发 HTTP、打大量日志。
- `easytier_status()` 优先调用 `get_status()`；失败时回退 atomic 快照。
- `easytier_start()` 先检查嵌入 TOML 是否含 `REPLACE_ME`，有就 `ESP_LOGE` + 返回 `ESP_ERR_INVALID_STATE`。
- `main.c` 保持 C；C++ 只出现在 `.cpp` 和组件内部。
- 不拷贝、不改组件源码；升级只改 `idf_component.yml` 版本。

组件内部虚拟网卡名是 `et`，是 raw L3，不带 ARP。判断路由是否命中它，用 `ip4_route()`，不要用 `netif_find("et")` 作为唯一依据——本项目实测出现过 `ip4_route()` 返回 `et`、`netif_find("et")` 返回 `NULL` 的情况。

---

## 4. Wi-Fi 生命周期

必须用 Wi-Fi 状态边沿驱动，不要在 `app_main` 里只 connect 一次：

```c
// CONNECTED 边沿
easytier_connect();
// DISCONNECTED 边沿
easytier_disconnect();
```

每次重连都要再次 `easytier_connect()`；组件不会替你记住“上次连过”。

启动顺序：

1. `nvs_flash_init`
2. Wi-Fi STA 启动并拿到 IP
3. `easytier_start()`（只一次）
4. Wi-Fi `CONNECTED` 后 `easytier_connect()`
5. 每 10 秒打印 `state / ip / peers / routes / dropped`

状态日志示例：

```text
mesh: state=READY ip=10.126.31.42 peers=1 routes=3 err=OK dropped=0
```

关键判断：

- `state=READY` 才说明 overlay 可用。
- `routes` 大于 0 只代表有路由条目，不代表目标主机一定能回包。
- `dropped` 持续增长，说明数据面没找到目标 peer 映射，先查目标 IP 是否在 EasyTier 路由表里，不要先怪 Wi-Fi。
- `ET.TASKS.IO: data rx queue full` 说明接收队列打满；先把 `CONFIG_EASYTIER_ESP_MAX_CONNECTED_PEERS` 降到 2、减少同时在线 peer，再谈吞吐。

---

## 5. Kconfig 默认项

`sdkconfig.defaults`：

```ini
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
CONFIG_EASYTIER_ESP_MAX_CONNECTED_PEERS=2
CONFIG_EASYTIER_ESP_LOG_INFO=y
```

- 8192 是 TOML 递归解析的实际需求，3584 会栈溢出。
- bring-up 阶段开 INFO；验收后改回 WARN，靠 `esp_log_level_set("easytier", ESP_LOG_INFO)` 单独开。
- `sdkconfig` 是本地生成文件，不进库；改默认项改 `sdkconfig.defaults`。

如果同时跑 TLS/HTTP 大缓冲，再加：

```ini
CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=8192
```

这不是 EasyTier 必需项，只在内部 RAM 紧张时开。

---

## 6. 构建、烧录、看串口

```powershell
idf.py build
idf.py -p COM3 flash
```

非 TTY 环境不要用 `idf.py monitor`，会直接报：

```text
Error: Monitor requires standard input to be attached to TTY
```

用独立脚本复位 + 读串口，例如：

```powershell
python serial_capture.py COM3 90 serial.log
```

脚本要处理 DTR/RTS 复位，波特率 115200。

体积基线（本项目实测）：

| 项 | 数值 |
|---|---:|
| 接入前 app bin | 2,091,728 B |
| 接入后 app bin | 2,169,280 B |
| Flash 增量 | 约 77 KB |
| EasyTier 组件归档 | 约 60.7 KB |
| factory 分区占用 | 约 51.7% |

用 `idf.py size` / `idf.py size-components` 复核，不要拍脑袋。

---

## 7. 连通性验收顺序

不要一上来就抓包。按下面顺序，哪一步断就停在哪一步。

### 7.1 先确认 ESP 自己的网卡和路由

在固件里打印：

```c
esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &info);
// 打印 sta ip / mask / gw

ip4_addr_t target;
IP4_ADDR(&target, 10, 126, 31, 41);
struct netif *route = ip4_route(&target);
// 打印 route->name
```

预期：`route 10.126.31.41 -> et`，不是 `st`（Wi-Fi STA）。

如果打印 `st`，说明目标地址没命中 `et` 的网段，先解决路由，不要继续测 HTTP。

### 7.2 ICMP

ESP 侧用 `esp_ping` 打目标：

```c
esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
cfg.count = 4;
cfg.timeout_ms = 2000;
IP_ADDR4(&cfg.target_addr, 10, 126, 31, 41);
```

预期 `4/4`。ping 通只证明 L3 双向可达，不证明 TCP 服务可用。

### 7.3 TCP

用 lwIP socket 直连目标端口，打印 `connect()` 返回值 + `errno`：

```c
int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
struct sockaddr_in dst = { .sin_family = AF_INET,
                           .sin_port = htons(8123),
                           .sin_addr.s_addr = htonl(0x0A7E1F29) };
int rc = connect(s, (struct sockaddr *)&dst, sizeof(dst));
ESP_LOGI(TAG, "tcp connect rc=%d errno=%d", rc, errno);
```

预期 `rc=0`。`errno=113` 是 `EHOSTUNREACH`，先回去查路由/peer；`ECONNREFUSED` 才是端口没开。

### 7.4 原始 HTTP

TCP 通了再做一次最小 GET，避免把 HTTP 客户端配置问题误判成网络问题：

```c
const char req[] = "GET / HTTP/1.0\r\nHost: 10.126.31.41\r\nConnection: close\r\n\r\n";
send(s, req, sizeof(req) - 1, 0);
recv(s, buf, sizeof(buf) - 1, 0);
```

预期首行 `HTTP/1.0 200 OK`。

### 7.5 HA API 与解析

HA 的 `/api/template` 当前返回 `Content-Type: text/plain`，body 是直接渲染出来的 JSON：

```json
{"t0":{"s":"unavailable","u":""}, ...}
```

不是旧格式：

```json
{"result":"{\"t0\":...}"}
```

解析器必须两种都吃：

```c
const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
bool valid = cJSON_IsObject(root) && cJSON_IsString(result);
if (valid) {
    // legacy: copy result->valuestring
} else if (cJSON_IsObject(root)) {
    // current HA: plain rendered object, use json directly as inner
    memcpy(inner, json, strlen(json) + 1);
    valid = true;
}
```

只认旧格式的后果：HTTP 200、body 几百字节、`complete=1`，但解析失败，界面一直 `READ_ERROR`。这不是网络问题。

### 7.6 从目标主机反向验证

PC 能 `curl` 通不代表 ESP 能通；PC 和目标主机可能在同一物理 LAN，或者 PC 的路由表和 ESP 不一样。必须从**目标主机自己**执行：

```bash
ping -c 3 10.126.31.42
curl -v --max-time 5 http://10.126.31.42:8080/
```

如果目标主机没有 shell，就在目标主机所在网段找一台能代表它路由的机器，不要用一台双网卡的 PC 代替。

---

## 8. 常见故障对照

| 现象 | 根因 | 处理 |
|---|---|---|
| `state=READY`，但 HA `READ_ERROR` | 先怀疑 HTTP/解析，不要先怀疑 mesh | 按 7.2→7.5 逐步缩小 |
| 首次请求 `ESP_ERR_HTTP_CONNECT`，约 60 秒后恢复 | HA 客户端在 mesh READY 之前发起了第一轮 | 把 HA 首轮延后到 READY，或接受一轮 60 秒冷却 |
| HTTP 200、body 有内容、仍 `READ_ERROR` | `/api/template` 返回 `text/plain`，解析器只认 `{"result":...}` | 解析器兼容两种格式 |
| `dropped` 持续增长 | EasyTier 数据面 `lookup_ip(dst)` 找不到目标 | 查目标 IP/网段是否在 EasyTier 路由表；不要先改 Wi-Fi |
| `ET.TASKS.IO: data rx queue full` | 接收队列满，peer 太多/吞吐过大 | 降 `MAX_CONNECTED_PEERS`，降 MTU，减少同时请求 |
| `ip4_route()` 返回 `et`，`netif_find("et")` 返回 NULL | 组件 netif 注册方式与 `netif_find` 行为不一致 | 用 `ip4_route()` 判断路由，不要用 `netif_find` 做唯一判据 |
| 新克隆构建报 `easytier.toml` missing | `EMBED_TXTFILES` 硬依赖 gitignored 文件 | 用条件嵌入 + `easytier.toml.example`，`RENAME_TO easytier_toml` |
| `state=ERROR`，日志有 `REPLACE_ME` | TOML 模板没填 | 复制真实配置；`easytier_start()` 应直接失败 |
| 接上 EasyTier 后内部 RAM LOW 掉到 50–70 KB | TLS/HTTP 握手 + EasyTier 队列/任务叠加 | 先量 `int_free/int_low/int_max`；TLS 大缓冲放 PSRAM；降 peer/队列；不要把 LOW 当常量 |

---

## 9. 验收清单

- [ ] `idf.py build` 0 warning、0 error
- [ ] `easytier.toml` 不在 `git status`，日志/截图无 secret
- [ ] 串口 `state=READY`，`ip` 正确，`peers >= 1`
- [ ] `ip4_route(target)` 命中 `et`
- [ ] ESP `ping target` 4/4
- [ ] ESP `TCP connect target:port` 成功
- [ ] 原始 HTTP 首行 200
- [ ] 业务 HTTP 客户端 `status=200` 且解析成功
- [ ] 目标主机反向 `ping/curl` ESP 成功（有 shell 时）
- [ ] 断开 Wi-Fi 再恢复，能再次 `READY`
- [ ] 静置 10 分钟 `dropped` 不持续增长、无 `queue full`
- [ ] `idf.py size` 记录基线/接入后差值

---

## 10. 不要做的事

- 不要把 `network_secret`、`rc.key`、`codeck.key` 提交或贴日志。
- 不要把 `easytier.toml` 从 `.gitignore` 拿掉。
- 不要在事件回调里做阻塞操作。
- 不要在头文件里暴露 `et::` 类型。
- 不要用 `idf.py monitor` 跑非 TTY 自动化。
- 不要用 PC 的 `curl` 成功就判定 ESP 链路成功。
- 不要看到 `READ_ERROR` 就先改 IP/路由；先确认 HTTP 状态码和 body 是否已经拿到。
- 不要拷贝组件源码来“修”问题；优先升级版本或在应用层做兼容。
