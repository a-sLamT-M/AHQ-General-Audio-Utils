# 开发说明

本项目遵循已读取的 AFFiNE 通用代码规范、质量保障及评审约定（文档标识分别为
`XwXFXzC1t6`、`NEl8EmrHq5`、`7y2PabJDJc`），以及本机只读文件
`J:/Projects/lubao/NuRules/通用开发指南.md` 中适用于本库的通用约定。Flutter 专属
约定不适用于此库。这些资料不随当前仓库复制。

## 构建与测试

需要 C++20 编译器、CMake 3.25 或更新版本，以及 Ninja。在 Windows 和 Linux
上使用相同命令；构建目录按操作系统和预设分开。

```sh
cmake --workflow --preset dev
cmake --workflow --preset release
```

两个工作流均依次配置、构建并运行 CTest；`dev` 使用 Debug，`release` 使用
Release。核心测试没有外部依赖。

本次本机验证使用工作区 `.tools` 中的 CMake 3.31.6 和 Ninja 1.11.1，未修改
系统安装。当前 Windows 系统中的 CMake 版本较旧，因此应先在项目根目录为
当前终端设置工具路径，再执行上述工作流。PowerShell 命令为：

```powershell
$audioTools = Join-Path (Get-Location) '.tools/windows'
$env:Path = "$audioTools/cmake/data/bin;$audioTools/ninja-1.11.1.4.data/scripts;$env:Path"
chcp.com 65001
```

中文 MSVC 的配置与构建终端须保持同一 UTF-8 代码页，使 CMake/Ninja 正确识别
`/showIncludes` 的头文件依赖。若旧构建出现大量“注意: 包含文件”输出且
`ninja -t deps` 显示对象文件有零项依赖，切换代码页后应重新配置
（`cmake --fresh --preset plugins-release`），再以
`cmake --build --preset plugins-release --clean-first` 清理重建；仅重新配置不会
清除全部旧对象文件。

WSL Ubuntu 中，在同一项目根目录使用：

```sh
export PATH="$PWD/.tools/linux/cmake/data/bin:$PWD/.tools/linux/ninja-1.11.1.4.data/scripts:$PATH"
```

`.tools` 仅供本机使用，已被 Git 忽略；其他机器需自行提供前述构建工具和
C++20 编译器。本次另用 clang-format 19.1.7 检查 C++ 格式。

## API 约束

效果参数的范围声明在各自的 `*Parameters` 类型中，由简单的 `ParameterRange`
值类型检查有限数值、闭区间边界及整数步长。核心的 `isValid()` 保留各效果的
类型检查；JUCE 参数布局复用这些范围，界面旋钮读取已注册参数的范围。
`PluginState` 在构造时缓存注册参数的范围，统一验证用户编辑和预置加载；音频
线程上的校验不分配内存。插件独有的参数（例如预置种子的 UI 范围、过采样选项）
仍由插件布局定义。非法输入直接拒绝，参数 ID、XML 版本及字段顺序不变。

包含 `<ahq/audio/gainProcessor.h>`，在 CMake 中链接 `ahq::audio_core`。配置或
非空处理之前须成功调用 `prepare(sampleRate)`。采样率须为有限值，范围为
1 至 384000 Hz。成功的 `prepare()` 会结束已有过渡，并将当前增益和干湿比
设为各自目标值。两者的初始目标值都是 1。

`setGain(linearGain)` 接受 0 至 16 的有限线性增益。目标发生变化时，从当前值
开始，经过 `ceil(sampleRate * 0.005)` 帧线性到达新目标；首帧执行第一步，
末帧精确设为目标。重复设置相同目标不会重启过渡。过渡中改变目标会从当时的
当前值重新开始完整的 5 毫秒过渡。`setMix(mix)` 接受 0 至 1 的有限值，
0 表示仅保留原采样，1 表示完整施加当前增益；目标改变时独立执行同样时长的
线性过渡。`reset()` 会使两者立即到达目标。

`process(left, right)` 原地处理 `left` 的所有帧，并为双声道同一帧使用同样的
增益和干湿比。省略 `right` 表示单声道；双声道缓冲区长度必须相同。两个缓冲区
均为空时，调用始终成功且不改变状态，包括尚未 `prepare()` 时。未准备时处理非空缓冲区会
失败。无效配置或双声道长度不一致时返回 `false`，处理器状态与缓冲区保持
原样。调用方必须检查这些返回值。

同一实例的所有调用必须在同一线程进行，不支持并发访问。缓冲区必须有效、
互不重叠；输入采样必须是有限值。处理器不削波，所以足够大的输入可能产生
非有限输出。`process()` 不分配内存、不执行 I/O，也不加锁。

## Quick Filter 与 Transient 核心

`<ahq/audio/quickFilter.h>` 提供 `QuickFilter` 与 `QuickFilterParameters`。
参数包含 `mode`（lowPass/highPass）、`frequencyHz`（20–20000）、`q`（0.1–10）、
`slope`（db12/db24/db36/db48）及 `mix`（0–1）。有效截止频率限制为
`min(frequencyHz, 0.45 * sampleRate)`。频率、Q 和干湿比独立平滑 5 毫秒，模式
和斜率切换使用同样时长的交叉淡化，快速重设从当时的混合状态继续。

滤波器为 TPT 状态变量结构；每声道持续运行四节低通和四节高通，分别提供四种
渐近斜率的候选输出。持续更新历史使切换后的滤波器不必从空状态开始。Q 只控制
每个级联的第一节，后续节使用 `1/sqrt(2)`，因此高阶曲线不是 Butterworth
对齐；默认 Q 下，截止处的衰减会随节数增加。干湿混合为线性混合，湿声的滤波
相位会影响混合结果。插件报告的延迟为零。

`<ahq/audio/transientShaper.h>` 提供 `TransientShaper` 与对应参数结构。
`attackDb` 和 `sustainDb` 范围均为 −12 至 12 dB，`sensitivityDb` 为 −80 至
−20 dBFS，`timeMs` 为 5–200 ms，`mix` 为 0–1。Sensitivity 是检测包络的噪声
阈值；Time 同时控制快峰值包络的释放时间和慢包络的观察时间。慢包络追随快
峰值包络，两者的正差选择 Attack，其余活动权重选择 Sustain。它是包络塑形，不能把任意
录音中的起音和延音完全分离。

Transient 使用左右绝对幅度的最大值更新同一个检测器，两个声道施加共同增益。
Attack 和 Sustain 均为零时严格保持原样；低于检测阈值时也不施加增益，超过
阈值后连续进入处理。五个参数均平滑 5 毫秒。两个处理器的 prepare、reset、
空块、无效调用、缓冲区及单线程约束与上述 Gain 一致，参数均须在 prepare
成功后设置。核心滤波器的调用方应在实时线程上处理浮点非正规数；插件层使用
`juce::ScopedNoDenormals`。

## Saturator 核心

`<ahq/audio/saturator.h>` 提供 `Saturator`、`SaturatorParameters`、
`SaturatorModel` 和 `OversamplingFactor`。七个模型分别为 tanh soft clip、hard
clip、非对称 tanh tube 风格、软限幅 tape 风格、三角波折叠、全波整流及 255
电平的量化。这些是数字曲线，不是电子管、磁带或放大器的电路模拟。

参数 `driveDb` 范围为 0–36 dB，`bias`、`tone` 为 −1 至 1，`mix` 为 0–1。
连续参数和模型权重平滑 5 毫秒。Bias 在非线性曲线前加入，并减去曲线对 Bias
本身的输出，使零输入保持零输出；非对称曲线和整流仍可能产生与信号相关的
直流分量。Tone 为零时严格跳过音色运算，负值混入单极低通，正值增加其互补
高频；低通截止为 `min(2000, 0.2 * sampleRate)` Hz。

`prepare(sampleRate, factor)` 接受与 Gain 相同的采样率范围，factor 可选
`x1`、`x2`、`x4`，默认 `x2`。成功调用会清空处理历史，因此核心的质量切换需要
调用方处理过渡。33 抽头半带 FIR 完成实际的插值和抽取，`getLatencySamples()`
分别报告 0、16、24 个基准采样率下的采样。干路补偿相同延迟；Mix 为零时，
输出逐采样等于延迟后的原输入。非零 Tone 仍有额外的频率相关相位，不能用
固定延迟消除。湿路没有最终削波，FIR 振铃可能使峰值超过曲线自身的限幅范围。

短 FIR 和有限过采样倍率不能消除全部混叠。核心测试用 48 kHz、10 kHz 输入、
24 dB Drive 的 hard clip 检查两个折叠频点：2x 和 4x 的 18 kHz 分量相对 1x
分别降低约 27 dB 和 29 dB，但 2x 的 6 kHz 分量反而增加；4x 的该分量再比
2x 降低约 23 dB。测试结果只描述这组信号，不能据此声称所有输入均有相同改善。

## Orbit 与 Grain Delay 核心

`<ahq/audio/orbit.h>` 提供耳机双耳旋转。湿声先将左右输入相加取平均，作为单一
点声源，再在水平面相邻 HRIR 间插值；因此反相输入可能在湿路抵消，干声仍保留
原立体声。参数包含 Hz、BPM 同步、每圈拍数、方向、深度、起始角度和干湿比。
深度在正前方与旋转响应间混合，不是物理距离。频率、深度、相位及 Mix 均有
5 ms 过渡，相位选择最短圆弧；BPM 控制转速，不锁定宿主节拍位置。

核心接受 8–384 kHz；prepare 中完成有限窗 sinc 重采样，实时线程只做固定长度
卷积。HRIR 保留声道间的声学时间差，并增加两路共同的因果延迟；48 kHz 时为
18 个采样，干声对齐同一延迟。内嵌数据来自 MIT KEMAR diffuse 水平面 37 个
双声道响应，负方位通过交换耳侧得到。数据许可、来源、SHA-256 与生成脚本见
`src/spatial/data/kemarAttribution.md` 和 `scripts/buildKemarTable.ps1`。当前没有个体化
HRTF、头部追踪或实际听音验收。

`<ahq/audio/grainDelay.h>` 是 Ghost Delay 的核心，使用 64 个固定颗粒槽和
prepare 中分配的四秒双声道环形历史。支持 20–500 ms 颗粒、10–2000 ms 延迟、
±24 半音、散布、1–80 Hz 密度、0–0.95 反馈、随机程度、种子和 Mix。种子决定
随机音高、声像与起点，任意缓冲分块下可重复。每个新颗粒锁存参数，Feedback
和 Mix 平滑 5 ms；Hann 窗重叠按权重归一化，随机声像只衰减另一侧。

上移音高时，实际延迟可能增加，以防读取头追上写入头。Feedback 支路单独使用
tanh，且循环增益不超过反馈参数；干路不受该曲线影响。此处的延迟属于创意
效果，宿主补偿延迟为零。变调使用线性插值，仍可能产生混叠；当前不声称高保真
变调。48 kHz 下历史内存约 1.5 MiB，384 kHz 下约 11.7 MiB。

## Loudness 与 True Peak 核心

`<ahq/audio/loudnessMeter.h>` 提供单声道/双声道 K 加权、400 ms 瞬时及 3 s 短时
响度，每 100 ms 产出一个完成的 400 ms 能量块。未填满窗口时读数无效，完整
静音 M/S 窗口为有效的负无穷。`IntegratedLoudness` 在非音频线程保存这些块，再执行
−70 LUFS 绝对门限和相对 −10 LU 门限；全部被门限排除时，综合响度无有效值。
音频回调没有可增长容器或历史扫描。
prepare 支持 32–384 kHz；更低采样率被拒绝，因为当前滤波器转换方式在 8 kHz
的独立校准误差超出了本项目采用的容差。48 kHz 参考系数与其他采样率在相同
物理频率下的响应偏差也单独测试，不能只用相同公式互相验证。

`<ahq/audio/truePeakMeter.h>` 使用 ITU-R BS.1770-5 附件 2 的四相、每相 12 抽头
FIR，分别保持左右 Sample Peak 与估计 True Peak。process 输入只读，finish
固定补 11 个零帧以计入末尾插值峰；finish 后需 reset 才能开始新音频。clearPeaks
仅清除保持值，不清除滤波器历史。核心接受 8–384 kHz，但固定 4x 在低于 48 kHz
时达不到 192 kHz 的计量指导，且有限插值仍有误差；不能把此读数当作任意波形
连续时间峰值的数学上界。算法依据为
[ITU-R BS.1770-5](https://www.itu.int/dms_pubrec/itu-r/rec/bs/R-REC-BS.1770-5-202311-I!!PDF-C.pdf)
和 [EBU Tech 3341](https://tech.ebu.ch/docs/tech/tech3341.pdf)（查阅：2026-10-04）。

## Space Lab、Chaos 与 Master Scope

`<ahq/audio/spaceLab.h>` 提供 0–3 倍 M/S 宽度、左右独立的 ±12 dB Tilt EQ、
左右反相、±30 ms Haas 和右侧 0–20 ms 正弦调制延迟。Tilt EQ 在 1 kHz 的
互补单极低通/高通上施加相反增益。正 Haas 延迟右侧，负 Haas 延迟左侧；
Split 模式将当前输入的 M 分量直接放在左侧，将延迟、右侧 EQ 和反相处理后的
M 分量放在右侧。Split 中宽度、左 EQ/反相和负 Haas 不参与计算，界面保留
参数值并说明这一行为。参数及路由过渡平滑 5 ms，插件延迟为零。它是立体声
相位与延迟塑形，不使用 Orbit 的双耳模型；折叠到单声道时可能抵消。

`<ahq/audio/chaos.h>` 按 10–250 ms 的片段长度和概率，从已经收到的输入中
锁定一段声音，执行 1–8 次重复、概率倒放、最多 ±24 半音随机变调、衰减式
随机声像和概率 tanh 失真。每个事件锁存处理参数，事件边缘与当前干声交叉
淡化；Mix 平滑 5 ms。种子可重现同一输入与参数时间线，缓冲分块不改变结果。
九秒双声道环形历史在 prepare 中分配，48 kHz 约 3.3 MiB；不在音频回调复制
整段片段，也没有反馈。变调使用线性插值，不能消除混叠。宿主延迟为零，开始
运行时尚不存在的历史按零处理。

`<ahq/audio/stereoMeter.h>` 每 400 ms 非重叠窗口计算两侧 RMS、同一窗口的
峰均比 Crest、相关度和 M/S 能量。Crest 的单位为 dB，与响度范围 LRA 的 LU
不同；静音的 Crest 和相关度没有有效物理解释。`<ahq/audio/loudnessRange.h>`
在非音频线程接收 10 Hz 的 3 s 短时响度，按 −70 LUFS 绝对门限和相对 −20 LU
能量门限筛选，再计算 95% 与 10% 分位之差。每十个有效观测更新一次，算法依据为
[EBU Tech 3342](https://tech.ebu.ch/docs/tech/tech3342.pdf)（查阅：2026-10-04）。
短素材、起止处静音与尚未填满的窗口会影响 LRA，不应把短时间读数解释成整曲范围。

Master Scope 逐采样保留音频，支持匹配的 mono/stereo 输入输出且延迟为零。
独立后台线程统计 Integrated/LRA，界面关闭后继续采集；音频回调只做有界计量
和固定队列写入。Reset 在块边界交换预分配的响度实例，由后台清理退役实例，
不在音频线程清空三秒历史。队列溢出或内存分配失败会明确使整段统计失效，
不能悄悄跳过缺失部分。频谱和实际 L/R 或 M/S 相位图只在界面可见时采集。

## True Limiter 核心

`<ahq/audio/trueLimiter.h>` 支持 32–384 kHz、mono/stereo 联动限制、−24 至
0 dB Ceiling、10–1000 ms Release、±24 dB Input Gain 和 0–1 Mix。
prepare 预分配最大 0–20 ms Lookahead，延迟为对应采样数加 24 帧；运行中
改变有效 Lookahead 只移动检测时间，不移动音频读取位置。4x/8x/16x 在预计算
的相位中选择，切换不分配内存、不清空响度窗口，宿主延迟保持不变。

最终保护约束 24 抽头 Hann 窗 sinc 的插值相位及已输出采样的后续贡献。为通过
更长的独立重建参考，4x 额外保留 1.75 dB，8x/16x 保留 1.6 dB 余量，因此输出
可能明显低于 Ceiling；这是保守限制器，不声称透明母带处理。降低 Ceiling
或启用新插值相位后，旧输出须经过 24 帧才完全离开滤波器支撑区；新采样本身
立即受限。有限滤波器检查不能证明所有连续时间重建方式都满足相同上界。

Mix 位于最终保护之前：零值关闭 Input Gain 与自动响度增益，但仍保留延迟和
峰值保护。这样混入干声不会直接绕过上限。Live 模式每 100 ms 读取已收到输入
的 3 s 短时响度，对目标 −36 至 −5 LUFS 计算补偿，并扣除手动输入增益；
自动补偿限制为 −96 至 +24 dB，平滑 250 ms。窗口未填满或输入低于 −60 LUFS
时回到单位自动增益。它不能预知未来，不能保证整曲综合响度，限制器持续工作
时也可能达不到目标。

非实时 `analyzeMatch` 测量整段 Integrated，估计固定增益，再实际重渲染、
移除前导延迟并计入延迟尾部，重新测量同长度输出。结果区分有效分析、目标
是否在 ±0.1 LU 内达到，以及峰值保护是否实际介入。未达到不等于数学上
不存在其他可行增益。该核心便利函数会保存整段 PCM，仅允许离线调用。

插件的 Analyze Audio File 使用独立后台线程，分两遍、每块 4096 帧读取
mono/stereo 文件，不保存整首 PCM。Apply Match 一次性恢复分析时的参数快照，
应用所求增益，并设置 Mix=1、Live=Off；整个操作可以一次撤销。预测只对应所选
素材、声道布局及采样率，播放链中先前的处理变化会使预测失效。界面显示 Input I、
实际重渲染 Output I、Gain、是否达到目标及峰值保护是否介入；它不改写原音频文件。
实时界面另显示输入/输出 S、增益衰减和自 prepare 起保持的输出 4x TP 估计。

## 可选插件构建

开启 `AHQ_BUILD_PLUGINS` 时才准备 JUCE，核心库仍不依赖 JUCE。仓库固定到
JUCE 9.0.3 的提交 `be29c81492b6151c8ea8d14c840e1311963b3a83`，下载归档通过
SHA-256 校验，不依赖浮动分支。该层还需要 C 编译器。Windows 插件构建应在
安装了 MSVC C++ 工具的 Developer PowerShell 中执行；MinGW 核心测试不能
代替 Windows 插件验证。

Ubuntu/Debian 上先提供满足版本要求的 CMake、C++20 编译器和 Ninja，再安装
JUCE 本次使用的 Linux 开发依赖：

```sh
sudo apt install pkg-config libasound2-dev libfontconfig1-dev libfreetype6-dev \
    libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
    libxrandr-dev libxrender-dev libxi-dev
cmake --workflow --preset plugins-dev
cmake --workflow --preset plugins-release
```

Ubuntu 22.04 的系统 CMake 可能低于本项目要求，不能只根据安装成功判断版本
满足要求。以上插件预设使用各自独立的构建目录。构建产物位于对应目录下的
`plugins/quick-filter/ahq_quick_filter_artefacts/<配置>/` 和
`plugins/transient/ahq_transient_artefacts/<配置>/`。需要单独构建格式时，
例如运行 `cmake --build --preset plugins-dev --target ahq_quick_filter_VST3`；
LV2 和 Standalone 分别使用同名前缀加 `_LV2`、`_Standalone`。LV2 仅在 Linux
目标中启用。构建过程不会自动安装或复制插件到用户插件目录。
其余已接入目标前缀为 `ahq_transient`、`ahq_saturator`、`ahq_orbit`、
`ahq_ghost_delay`、`ahq_space_lab`、`ahq_chaos`、`ahq_master_scope`、`ahq_true_limiter`。对应目录
分别是 `transient`、`saturator`、`orbit`、`ghost-delay`、`space-lab`、`chaos`、
`master-scope`、`true-limiter`，产物位于其 `<目标>_artefacts/<配置>/` 中。
同一次工作流会构建全部已接入插件。

离线准备 JUCE 后，可以先配置
`cmake --preset plugins-dev -DFETCHCONTENT_SOURCE_DIR_JUCE=/absolute/path/to/JUCE`，
后续照常使用该工作流。`.tools` 下的依赖是本机验证用缓存，不随 Git 分发。
JUCE 本身受其 AGPLv3/商业许可约束，本仓库没有替使用者选择分发许可。

## 插件状态与验证范围

Quick Filter 的参数 ID 固定为 `mode`、`frequency`、`q`、`slope`、`mix`。
Transient 的参数 ID 固定为 `attack`、`sustain`、`sensitivity`、`time`、`mix`，
用户预置扩展名为 `.ahqtransient`，XML 根标签为 `AHQTransientState`。
音频线程只读预缓存的原子参数并调用 DSP；预置解析、文件操作和撤销历史均在
非音频线程处理。用户预置使用 `.ahqfilter` XML，包含根标签、版本和五个参数。
载入先验证全部值；版本、类型、范围或格式不正确时保留原参数。主机状态恢复
会使旧撤销历史失效，避免撤销回到另一个工程的状态。
这些插件共用参数状态、编辑撤销和显示实现；单参数操作的撤销/重做只修改该
参数，也只向宿主发送该参数的编辑事件，其他参数的自动化保持原样。

Saturator 的参数 ID 为 `model`、`drive`、`bias`、`tone`、`oversampling`、`mix`。
插件固定报告 24 个采样的延迟，1x/2x 分别补足到这一值。倍率改变时，另一个
预分配处理器回放最近 512 帧输入，再经过 5 ms 交叉淡化切换；快速重设倍率
保留最新请求，当前过渡完成后才开始下一次。未切换时只运行一个处理器。
用户预置扩展名为 `.ahqsaturator`，根标签为 `AHQSaturatorState`。

Orbit 的参数 ID 为 `rate`、`sync`、`bpm`、`beats`、`direction`、`depth`、
`phase`、`mix`。支持 mono 输入到 stereo 输出及 stereo 到 stereo，拒绝 mono
输出。同步模式优先读取音频线程中宿主提供的有效 20–300 BPM，否则使用手动
BPM；界面显示最后处理块实际使用的来源和值。用户预置为 `.ahqorbit`，根标签
为 `AHQOrbitState`。宿主 bypass 时仍前进 DSP 历史，并返回对齐延迟的干声。

Ghost Delay 的稳定参数为 `grain`、`delay`、`pitch`、`scatter`、`density`、
`feedback`、`randomness`、`seed`、`mix`，用户 Seed 范围为 0–65535 的整数。
Space Lab 使用 `width`、`haas`、`leftTilt`、`rightTilt`、`invertLeft`、
`invertRight`、`modRate`、`modDepth`、`split`、`mix`。Chaos 使用 `slice`、
`probability`、`repeats`、`reverse`、`pitchRange`、`pan`、`drive`、`distortion`、
`seed`、`mix`，Repeats 与 Seed 是整数参数。这三个插件在宿主 bypass 时仍更新
内部历史，同时输出原始干声。

Master Scope 只有 `run`、`hold`、`scope` 三个计量设置，音频始终不变，因此没有
会改变音频的 Dry/Wet。Capture 重新开启会开始新统计；峰值 Hold 关闭时每 100 ms
更新窗口峰值，开启时保持自重置后的最大值。True Limiter 使用 `ceiling`、
`target`、`live`、`gain`、`lookahead`、`release`、`quality`、`mix`；插件固定
预留最大 20 ms 加 24 帧延迟，例如 48 kHz 为 984 帧。宿主 bypass 输出对齐后的
原干声并继续更新 DSP，Dry/Wet=0 则仍保留最终保护，两者语义不同。

频谱使用 2048 点 Hann 窗，并取 L/R 频谱功率平均。音频线程只写固定容量的
单生产者、单消费者队列；显示落后时丢弃采样，FFT 和绘制由消息线程完成。
编辑器隐藏或关闭后停止采集和显示定时器。效果插件的电平及频谱是近似显示，
应与 Master Scope 的响度、峰值估计和声场统计区分。

参数选择框通过 `juce::ParameterAttachment` 接收参数变化，界面定时器不再反复
写入其选中项。此前 30 Hz 刷新可能抢在 ComboBox 的异步 `onChange` 之前把
新选择覆盖为旧值，导致点击无效；2026-10-04 修复涉及 Quick Filter、Saturator、
Orbit、Master Scope 和 True Limiter 的八个选择框。用户编辑仍走原有撤销事务，
参数 ID 和插件 ID 不变。五项插件测试均加入“选择后先刷新、再执行异步通知”
的回归场景，并逐项检查参数写入、撤销、重做与宿主参数更新。

核心测试检查实际频率响应、干湿端点、包络处理方向、分块一致性、声道状态、
采样率切换和无效调用。插件测试检查主机状态、预置、撤销与自动化之间的边界、
不同声道数/采样率/块长度、频谱传输，以及离线处理写入浮点 WAV 后逐采样读回。
Linux 无显示服务器的环境可通过 Xvfb 运行需要图形初始化的插件测试。

2026-10-04 的验证环境与结果如下。Windows 使用 MinGW GCC 15.2，Linux 使用
WSL Ubuntu 22.04.5 的 GCC 11.4。Windows Debug/Release 的十三项核心测试全部
通过。Linux Debug/Release 均已生成全部九个插件的 VST3、LV2 和 Standalone，
两种配置均为 22/22 测试通过，包括十三项核心与九项插件测试。完整 Debug
测试日志中没有 JUCE 断言。

全部九个 Debug/Release VST3 均通过 pluginval 1.0.4 的 level 5 检查，Debug
日志中没有 JUCE 断言。检查使用固定随机种子
42，采样率 44100/48000/96000 Hz，块长 1/17/256/1024。检查包含界面创建、
处理时打开界面、状态恢复和自动化；没有另行运行 Steinberg 的独立 validator。
LV2 包通过 lilv 工具扫描，并通过 LV2 ABI 实际加载、处理双声道测试信号；发送
`patch:Set` 改变滤波、起音增强、非线性和过采样参数后，实际处理符合核心
参考。将 `mix` 改为 0 后，Quick Filter/Transient 逐采样等于输入，Saturator
逐采样等于延迟 24 帧的输入，Orbit 为 18 帧，分别与报告的宿主延迟一致。
Ghost Delay 的默认输出也与核心一致，Mix 为零时逐采样保持干声。Space Lab
在宽度归零后输出 M 分量，Chaos 开启切片与失真后的输出确实改变，两者 Mix
归零后均逐采样保持干声。Master Scope 开启或停止计量都不改变音频。True
Limiter 默认输出与独立核心一致，运行中切换检测倍率和 Lookahead 后延迟仍为
984 帧；低电平 Mix=0 测试逐采样等于相同延迟后的输入，高电平仍受保护。

Orbit 的默认旋转输出还与独立核心逐采样对照通过，插件测试验证了宿主 BPM
有效、缺失、无效及 Hz 模式的实际转速，以及前后方位不同的脉冲响应。
True Limiter 插件测试用实际 WAV 文件执行两遍分析，将分析参数应用后重新
通过插件处理同一文件，输出综合响度与预测相差不到 0.01 LU；还覆盖峰值限制
导致目标未达到、取消分析及关闭编辑器后后台分析继续的情况。界面测试将编辑器
挂到原生窗口、输入测试信号，再截取实际组件图像；九个 Release 编辑器均已
检查控件布局、电平更新和非空频谱，Debug 检查同时关注 JUCE 生命周期断言。

本机 WSL 的包管理器已有未完成配置的系统包，因此本次将所需 Ubuntu 开发库
解包至被忽略的 `.tools/linux/sysroot`，没有修复或重启无关服务。图形验证使用
已检查可连接的 WSLg 显示服务；本机临时 Xvfb 因缺少 `xkbcomp` 未能启动。
同日另用 Windows x64 MSVC 19.44.35225、Windows SDK 10.0.26100.0 构建
Release，九个插件均生成 VST3 和 Standalone，22/22 核心及插件测试通过。
九个 Windows VST3 也通过 pluginval 1.0.4 level 5，随机种子、采样率和块长
与上述 Linux 检查相同，包含界面测试。Windows 构建目录为
`build/Windows/plugins-release`，与 MinGW 核心构建分开；验证日志保存在该目录
的 `validation` 子目录。本机完整插件包及独立程序另汇集于
`build/Windows/AHQ-Audio-Utils-Windows-x64-Release`，并生成同名 ZIP。
二进制依赖 MSVC x64 运行库；复制 VST3 时须保留整个 `.vst3` 目录结构。
这些结果不代表已在实际 Windows/Linux DAW 或音频设备中验收，也不包含音乐
素材试听。Windows Debug 插件尚未在此次构建中验证。

同日完成参数校验去重、嵌套条件整理及源码/测试目录重构后，Windows MSVC 与
Linux GCC 的 Release 构建各通过 23/23 测试（十四项核心、九项插件），覆盖
参数边界、小数枚举/布尔值拒绝、预置拒绝时状态不变，以及选择框异步更新回归。
九个 Windows VST3 再次通过上述配置的 pluginval level 5；日志位于
`build/Windows/plugins-release/validation/refactor`。本轮未重跑 Linux pluginval、
LV2 ABI 检查或实际 DAW 验收，前述检查结果属于重构前的版本。

响度另以本机 FFmpeg 2023-03-02 版本的 ebur128 过滤器对照：44.1/48/96 kHz
立体声校准音、单声道校准音和含静音/低电平/不同频率的 32 秒节目，共五个
整段结果的最大差为 0.0244 LU。此对照不是官方认证。按 EBU Tech 3341 描述
重新生成的 True Peak 测试信号通过相应容差；官方下载测试集被站点访问保护
拒绝，因此没有声明完整官方素材测试通过，也没有把其音频放入仓库。

四倍 True Peak 另与同一 FFmpeg 版本对照。Fs/4 相位偏移正弦的差约 0.045 dB，
相邻双脉冲低约 0.160 dB，但一组 48 kHz 宽频随机噪声低约 0.869 dB。此结果
体现短插值滤波器的限制；Master Scope 需要显示估计语义，Limiter 输出还须
独立检查，不能仅凭输入表头读数声称符合任何波形的连续峰值上限。

Limiter 输出另以 FFmpeg 复测：上述六种输入分别运行 4x/8x/16x、Input Gain
+12 dB、Ceiling −1 dB，共十八组，参考峰值均不超过 −1 dBTP。48 kHz 噪声的
8x/16x 输出参考为约 −2.510 dBTP，表现出当前保护余量的保守性。核心测试还用
更长 sinc 重建检查正弦、方波、脉冲、多音、噪声和运行中参数切换；两种参考
滤波器的结果不能互相替代，也不是任意输入的数学保证。
