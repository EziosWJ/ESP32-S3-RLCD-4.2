# Codeck 配置名点阵字体

`codeck_labels.bin` 使用 GNU Unifont 18.0.01 的原始 16 px 字形，选取 ASCII 和 U+3000–U+9FFF，按固定 32 字节/字保存。原始 8 px 宽 ASCII 字形仅补齐右侧空白，不改变笔画。支持常用中文、日文及相关标点；其他 Unicode 字符显示方框。过长名称在屏幕尾部显示省略号，快照中仍保留完整名称。

来源：[GNU Unifont](https://unifoundry.com/unifont/index.html)，[原始 HEX](https://unifoundry.com/pub/unifont/unifont-18.0.01/font-builds/unifont-18.0.01.hex.gz)。字形版权归 Unifont 贡献者所有（包括 Roman Czyborra、Paul Hardy、Qianqian Fang、Andrew Miller、Johnnie Weaver 及项目列出的其他贡献者），按随附 `OFL-1.1.txt` 分发。字体不改变项目代码的许可证。

构建固件直接嵌入此二进制资源，不访问字体网站，也不需要系统字体。
