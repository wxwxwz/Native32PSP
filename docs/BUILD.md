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
sh tests/run-game-loop.sh
# 可选：运行地址/未定义行为检查
SANITIZE=1 sh tests/run-av.sh
SANITIZE=1 sh tests/run-host.sh
SANITIZE=1 sh tests/run-game-loop.sh
```

测试数据是合成媒体。产物写入 `tests/out`，不进入版本库。`run-av.sh` 包含画面呈现、滤波、设置、截图，以及 MPEG 播放节奏、PES 短读/损坏输入和 DCT VLC 回归；VLC 也可单独执行 `sh tests/run-mpeg-vlc.sh`。`run-host.sh` 检查核心合成路径；`run-mp3.sh` 检查音频后端。主机测试使用 PSP 接口替身，验证逻辑、GU 参数约束和模拟像素结果；音频设备、真实 GE 的采样与缓存行为、实际帧率仍需真机验证。

`run-av.sh` 已包含游戏循环回归，也可单独执行 `run-game-loop.sh`。它独立比较旧时间轴及名称快照算法，覆盖真实脚本删除、改名、克隆和场景切换，并测量 128 个长名称精灵预热后的分配次数。普通构建统计分配；sanitizer 构建检查内存行为。游戏合成、画面准备和固定跳帧的本轮改动及验证状态见 [游戏循环与画面准备优化](GAME-SMOOTHNESS.md)，其中同时列出主机改善与回退的场景；PSP 流畅度仍需新设备日志确认。

`run-av.sh` 还包含 MPEG 起始码扫描与独立逐字节参考对照、运动补偿与块输出参考、预算突发模型，以及真实 MPEG 素材下 AUTO/关闭/固定跳帧的呈现和音频总量回归。预算模型不代表硬件帧率；本轮实现和验证范围见 [MPEG 低帧率优化](MPEG-FPS.md)。

滚动刷新回归也已纳入 `run-av.sh`：`run-image-decode.sh` 对照旧图像解码算法及透明边界，`run-renderer-scroll.sh` 对照全量排序的逐像素结果和缓存顺序；两者支持 `SANITIZE=1` 单独执行。解码基准可先运行 `sh tests/run-image-decode-bench.sh --build-only`，再交替调用 `tests/out/image-decode-bench --before` 和 `--after`。测量范围及日志语义见 [同场景滚动与刷新峰值优化](SCROLL-REFRESH.md)。

后续性能诊断包用于细分 `20260920.0036` 设备记录中仍存在的核心 tick 尖峰，详见 [剩余低帧率与慢 tick 诊断](FRAME-SPIKES.md)。新增统计本身不是提速改动；普通与 sanitizer 回归用于检查计数、统计边界和原有行为，主机替身不能提供 PSP 阶段耗时或证明低帧率已经解决。涉及诊断成员的头文件或类布局变更时应使用全新目录全量构建，避免混用旧对象文件。

YUV 残留零色度的颜色修复见 [YUV 阴影与边角绿点修复](YUV-COLOR-FIX.md)。旧实现差分只能验证行为一致，颜色兼容问题还需检查独立预期的黑色／透明网点、相邻色度恢复和合法绿色；真实游戏素材不作为公开测试资源分发。最终构建与验证状态以该文档为准。

`run-av.sh` 已纳入 `run-core-profile.sh`，也可单独运行 `sh tests/run-core-profile.sh --clock` 检查模拟时钟下的 VM 递归去重、声音启动统计、重置和时钟回绕；`--host` 检查普通主机计数及零设备耗时。MPEG 回归另外核对独立图片头、每次 advance 的计数重置和截断尾部；应用回归检查慢 tick 各阶段关联及连续视频的剩余统计窗口。

## 画面路径与验证

`run-av.sh` 包含 `run-mpeg-native.sh` 与 `run-string-extract.sh`：前者检查原尺寸视频与游戏画布分离、不同尺寸连续播放、跳过/EOF/截图/黑边及有界回退；后者用独立合成脚本复现旧解释器只显示两条候选，验证修复后四条及字符串截取边界。二者支持 `SANITIZE=1`。MPEG 原尺寸与旧游戏尺寸的 RGB/画面准备基准可用 `tests/out/mpeg-native --bench-native`、`--bench-game` 交替执行；记录及限制见 [原尺寸视频与四条候选修复](NATIVE-VIDEO.md)。

`run-av.sh` 同时运行 `run-vm-numeric.sh`、`run-vm-semantics.sh` 和 `run-mpeg-rgb.sh`，检查 VM 数值兼容性、与 0125 完整解释器的状态/宿主事件对照，以及 MPEG 缩放的逐像素一致性。可用各脚本的 `--build-only` 编译基准后串行运行其 `tests/out/` 程序；对照范围及测量方法见 [脚本与 MPEG 热点优化](CPU-HOTSPOTS.md)。

实际发生缩放且源宽高均不超过 512 时，程序将原生画布转换为对齐纹理，经 GU 绘制。显示设置中的“清晰”为默认最近邻滤波，“平滑”为双线性滤波。原尺寸不使用滤波；超大源画布需要缩放时，先由 CPU 生成屏幕尺寸内的结果。后两类路径均使用独立对齐的行跨度执行 GE 复制，可见矩形宽度无需是 16 的倍数。

线性滤波只复制采样可能触及的一行/一列边缘，避免黑边；写回可采样行，不再每帧填满和写回纹理的未使用部分。单张纹理像素缓冲最大 1 MiB，另加对齐余量；这是呈现缓冲大小，不包含核心资源或显存。截图按需重绘当前不可见页，读取没有 FPS、截图通知等叠加层的结果。

`native32psp.log` 的 `present:` 行记录 `prepare_us`、`transfer_us`（平均/最大微秒）、`mode`（`GU-texture` / `GE-copy`）、`filter` 和 `surface_kb`（保留容量）。真机比较时应使用相同游戏、场景、缩放、滤波和跳帧设置，并结合已有音频及帧调度日志。本次画面改动已通过 PSPSDK 全量构建、AV/MP3/渲染回归及 AV/渲染 ASan/UBSan 检查；尚无真机帧率测量。实现与验证范围见 [游戏画面呈现与滤波](GAME-RENDERING.md)。

前一轮 MPEG 优化包含 DCT VLC 前缀表、保留视频帧不重复上传，以及按当前 PES 返回的流式读取。该轮 PSPSDK 全量构建、普通 AV 回归、核心摘要及完整 AV ASan/UBSan 检查通过，包含损坏 PES 与缺失音轨用例。合成素材输出哈希一致，主机解码耗时下降不代表 PSP 帧率提升；该轮完成时尚无新设备记录。实现见 [MPEG 解码与读取突发优化](MPEG-STUTTER.md)，后续取得的 2224 构建设备基线见 [游戏优化记录](GAME-SMOOTHNESS.md)，本轮新改动仍待真机验证。

## 可选资产生成

Windows PowerShell：`./tools/generate_logo.ps1`，默认读取 `assets/logo-source.png`。

字体：`python tools/generate_menu_font.py /path/to/unifont-16.0.04.hex.gz`。

生成文件已随源码提供，不需要连接原开发者的机器或提供 SSH 凭据。
