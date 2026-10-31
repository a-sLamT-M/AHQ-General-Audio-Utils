# AHQ General Audio

[English](README.md) | 中文

**注意：目前这是一个 VIBE CODING 测试。**

目前有二十二款音频插件，使用 C++20 和 JUCE 开发。Windows 支持 VST3、独立程序；Linux 另支持 LV2。

| 插件 | 用途 |
| --- | --- |
| Quick Filter | 高通、低通滤波，可调频率、Q 和 12/24/36/48 dB/oct 斜率 |
| Transient | 调整起音和延音，增强冲击感或柔化瞬态 |
| Saturator | 七种饱和与失真模型，可调 Drive、Bias、Tone，支持 1x/2x/4x 过采样 |
| Orbit | 耳机双耳空间旋转，可调速度、方向、深度，支持 BPM 同步 |
| Ghost Delay | 颗粒延迟、变调、随机散布与反馈 |
| Space Lab | 立体声宽度、Haas 延迟、左右独立 EQ、反相与调制声场 |
| Chaos | 随机切片、重复、倒放、变调、声像和失真 |
| Master Scope | 可缩放的频谱、相位、波形和响度历史图，可调显示增益、范围、样式与颜色；保留 LUFS、LRA、RMS、True Peak 等读数 |
| True Limiter | 峰值限制、实时短时响度控制、整段音频响度分析与匹配 |
| Curve EQ | 24 个可拖动频段，立体声、左、右、中、侧声道处理，动态钟形/搁架频段及输出频谱 |
| Volume Envelope | 八点可编辑音量包络，默认跟随宿主 BPM 与拍位置，也可由超过阈值的侧链声音触发 |
| Noise Gate | 立体声联动噪音门，可调迟滞、起音、保持、释放和衰减量 |
| Tremolo | 跟随 BPM 或自由运行的音量调制，提供四种波形 |
| Opto Leveler | 光学压缩行为模型，双阶段释放随先前增益衰减变化 |
| FET Compressor | 快速 FET 压缩行为模型，提供 4:1、8:1、12:1、20:1 和近似 All 模式 |
| Cabinet | 四路并行箱体卷积，各路独立启用、音量、声像、反相和延迟；提供总音量、路由预置及包含全部 IR 的预置保存/加载 |
| Hybrid Reverb | 卷积早期反射与八路反馈延迟网络尾声 |
| Gated Reverb | 有限长度的非线性/反向混响，可调长度、亮度和预延迟 |
| Elevation | 使用具有明确许可的 SADIE II 响应，在耳机中模拟下方至上方的声音 |
| Retro Tools | 合奏、磁带式慢速/快速音高晃动、随机喇叭故障破音、失真、音色和位深处理 |
| Modular | 四个模拟波形/波表/PCM/噪声发生器、双包络、双 LFO、八路调制及 32 个原创预置 |
| PCM Station | 双振荡器 PCM 音色、八声部叠加与键区划分，内置 64 个原创程序和 16 个组合 |

插件支持宿主自动化、状态保存和撤销/重做。宿主提供原生参数菜单时，可以在参数控件上右键调用。
多数效果器提供 Dry/Wet；Cabinet 直接混合四路卷积输出，Master Scope 保持音频不变。

所有插件底部均有共用的用户预置栏。输入名称后按 `Save preset`，预置会保存到系统“文档”
目录下的 `AHQ/Presets/<插件名称>/`，格式为 `.ahqpreset`。同名保存生成新文件并保留旧版；
`User presets` 只显示当前插件的预置，重新打开列表即可刷新。`Load preset` 可导入原有
AHQ 预置文件，验证成功后收录到对应目录；`Folder` 打开这个目录。加载可撤销和重做。
Cabinet 和 Hybrid Reverb 的预置包含 IR 音频，Master Scope 的预置包含图表增益和显示设置。
各插件共用原生 UI 库，并使用固定的产品配色及功能标识；原有参数 ID 和工程状态保持兼容。

两款合成器采用模块合成器和 PCM 工作站的同类合成方式及编辑思路，不包含商业产品的 ROM、
原厂音色或预置格式兼容功能，内置声音由原创代码生成。光学/FET 压缩器属于行为模型，
未声称通过硬件电路校准；Gated Reverb 使用自行生成的脉冲响应。Elevation 使用通用假人头响应，
实际高度感因听者而异。

性能方面，文件解码、卷积构建和内存分配位于音频处理之外，合成器最多运行 64 个声部，
界面隐藏时停止图形分析。具体限制、延迟和处理耗时见[实现与验证记录（英文）](doc/development.md#2026-10-expansion)。

## 构建

准备 C++20 编译器、CMake 3.25+ 和 Ninja。Windows 使用 MSVC Developer PowerShell；Linux 依赖见[开发说明（英文）](doc/development.md#requirements-and-setup)。

```sh
cmake --workflow --preset plugins-release
```

该命令构建并测试全部插件。产物位于 `build/<系统>/plugins-release/plugins/<插件>/<目标>_artefacts/Release/`。
仅开发 DSP 核心时，使用 `cmake --workflow --preset dev`。

## 安装

Windows：将整个 `.vst3` 文件夹复制到 `C:\Program Files\Common Files\VST3`，然后在宿主中重新扫描。
Linux：将插件包复制到宿主配置的 VST3 或 LV2 搜索目录。独立程序可直接运行。

参数说明、实现细节和测试记录见[开发说明（英文）](doc/development.md)。
