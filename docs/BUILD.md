# 构建与验证

## PSP 构建

在 Linux 或具备 PSP 开发工具链的环境中安装 PSPDEV/PSPSDK、PSP 版 zlib 及 `pspkubridge` 库，保证 `psp-config`、PSP 编译器和打包工具在 PATH 中。

从独立仓库根目录执行：

```sh
make -j4
```

输出 `EBOOT.PBP`。Makefile 启用 PRX 与加密打包，标题固定为 Native32PSP，图标来自 `assets/ICON0.png`。代码或工具链发生较大变更时使用全新目录构建，避免旧目标文件混用。

构建产物不提交到 Git；将运行包上传至 GitHub Release。版本定义位于 `src/platform/version.h`。

## 主机回归

需要支持 C++11 的 g++、zlib 开发文件和 POSIX shell：

```sh
sh tests/run-av.sh
sh tests/run-mp3.sh
sh tests/run-host.sh
# 可选：运行地址/未定义行为检查
SANITIZE=1 sh tests/run-av.sh
```

测试数据是合成媒体。产物写入 `tests/out`，不进入版本库。主机测试使用 PSP 接口替身，验证逻辑和像素结果；音频设备、GE 和实际帧率仍需真机验证。

## 可选资产生成

Windows PowerShell：`./tools/generate_logo.ps1`，默认读取 `assets/logo-source.png`。

字体：`python tools/generate_menu_font.py /path/to/unifont-16.0.04.hex.gz`。

生成文件已随源码提供，不需要连接原开发者的机器或提供 SSH 凭据。
