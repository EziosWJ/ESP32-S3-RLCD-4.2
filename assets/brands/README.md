# 官方图形资源

原始文件保留官方形状；品牌图形属于各自权利人。下载记录、来源 URL 和 SHA-256 见 [sources.json](sources.json)。

- `openai.svg`：[OpenAI 官方品牌资源包](https://cdn.openai.com/brand/OpenAI-Logos-2025.zip)中的 `OpenAI-black-monoblossom.svg`，原始文件保持不变；Codex CLI 额度卡片使用这个 Blossom。
- `codex-terminal.svg`：保留的旧终端 UI 资源，当前固件不使用；已有文件改动保留。
- `deepseek-original.svg`：[DeepSeek 官方仓库横标](https://github.com/deepseek-ai/DeepSeek-LLM/blob/main/images/logo.svg)。生成器复制根节点下的鲸鱼路径到 `deepseek-symbol.svg`，不重绘路径。
- `openrouter.svg`：[OpenRouter 官方品牌资源](https://openrouter.ai/brand)，使用 Glyph / Ink 版本。

`tools/build_brand_icons.py` 将透明边缘去除，再等比例缩放、居中并保留至少 2 px 空白，按 alpha 阈值 128 转为黑白轮廓。不加抖动，不拉伸、不描摹或手绘图形。彩色原始文件保留供其他显示设备使用。

生成物：`main/brand_icons_data.h` 为固件 1-bit 图库；`docs/brand-icons/` 为三种尺寸预览。`brand_icons.c` 使用现有 RLCD 水平线原语绘制，电脑页面预览调用同一份代码。
