# 主机端 UI 预览

本工程可以不连接 RLCD 屏幕，在开发电脑上生成固件 UI 的 PNG 预览。流程会用 Zig 的 C 编译器把生产绘图代码编译为主机程序，再由 Python 将绘图指令栅格化为 400 × 300 单色画面。它不是 QEMU/WSL 中运行的 ESP32 固件，也不需要启动整机模拟器。

## 预览整个 UI

在仓库根目录运行：

```powershell
python tests/test_ui.py --zig <zig.exe路径>
```

脚本会编译 UI 主机测试程序，使用测试输入运行页面和按键逻辑，并生成 PNG 预览。结果在 `docs/ui-preview/`，打开其中的 `index.html` 查看各页面。测试输入覆盖示例传感器值、联网/离线状态、弱信号、配网状态和多账户长内容等场景。

## 预览 Codeck 页面

使用内置契约样例生成正常和故障状态的预览：

```powershell
python tests/preview_codeck_snapshot.py --zig <zig.exe路径>
```

结果在 `docs/codeck-live-preview/`。该脚本使用生产解析器和页面绘图代码，但默认数据来自测试样例。

也可以先从开发电脑访问只读快照接口，再用真实响应绘制页面：

```powershell
python tests/probe_codeck.py --check-auth
python tests/preview_codeck_snapshot.py --zig <zig.exe路径> --json build/codeck-live.json
```

第一条命令将成功响应保存在被 Git 忽略的 `build/codeck-live.json`；需要有效的本地凭据 `codeck.key` 和网络连接。第二条命令读取该响应，预览输出保存在 `build/codeck-current-preview/`。快照可能包含账户数据，分享或提交前应先检查内容。

如果 Zig 已在默认位置 `build/portal_toolchain/ziglang/zig.exe`，可以省略 `--zig <zig.exe路径>`。

## 结果如何解读

- 预览调用固件中的 UI、解析和绘图代码，便于检查布局、文字、分页及状态表达。
- 图片由主机端绘制生成。网络请求若经过验证，也是开发电脑发起的 HTTP 请求；它不能证明 ESP32 实机已经联网。
- 模拟输入不会覆盖屏幕刷新效果、实际按键、电池/传感器读数、板上 Wi-Fi 行为或真实面板的显示差异。
- README 中的预览图是合成画面，不是设备照片，也不应被描述成实机联网截图。验证实机效果仍需烧录固件并在真实设备上检查。

## 实现位置

- `tests/test_ui.py`：编译 UI 主机测试程序并生成统一 UI 预览。
- `tests/preview_codeck_snapshot.py`：使用测试样例或 JSON 快照生成 Codeck 页面预览。
- `tests/render_rlcd.py`：把绘图指令转换为 PNG，并检查画布边界及文字/图形重叠。
- `tests/probe_codeck.py`：在开发电脑上只读获取 Codeck 快照；它不操作 RLCD 设备。
