# 来源与第三方组件

- Native32 核心及行为参考：AloysHF/Native32Emu。保留根目录 BSD-3-Clause LICENSE（Copyright (c) 2025, Aloys (AloysHF)）。PSP C++ 移植维护者为 pikawz。
- `src/third_party/minimp3.h`：lieff/minimp3，头文件保留原作者的公共领域/CC0 声明。项目地址：https://github.com/lieff/minimp3 。
- `src/platform/menu_font_data.inc`：Native32 Menu Bitmap，派生自 GNU Unifont 16.0.04 的字体子集，采用 SIL OFL 1.1；原字体许可及署名在 `licenses/UNIFONT-*`。生成工具为 `tools/generate_menu_font.py`。字体源地址：https://unifoundry.com/pub/unifont/unifont-16.0.04/font-builds/unifont-16.0.04.hex.gz 。
- 图标与原始标识来自项目提供的 `res/logo.png`，独立仓库保存为 `assets/logo-source.png`，保留上游项目致谢。
- `tests/fixtures/` 仅包含合成测试图案和正弦音，生成方式见该目录 README.md，不含实际游戏资源。

发布运行包时同时附带 LICENSE、THIRD_PARTY.md、licenses 和 minimp3 原始许可声明。
