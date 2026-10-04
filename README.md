# AHQ General Audio

仓库包含不依赖第三方库的 C++20 DSP 静态库 `ahq_audio_core`，以及可选的 JUCE
插件层。一个仓库分别生成每个插件，Linux 构建目标为 VST3、LV2 和 Standalone。
核心与插件封装分开，单独测试 DSP 时不需要下载 JUCE。

| 插件 | 当前处理能力 |
| --- | --- |
| Quick Filter | HP/LP、Frequency、Q、12/24/36/48 dB/oct、Dry/Wet |
| Transient | Attack、Sustain、Sensitivity、Time、Dry/Wet |
| Saturator | 七种数字非线性曲线、Drive、Bias、Tone、1x/2x/4x、Dry/Wet |
| Orbit | KEMAR 耳机双耳旋转、Hz/BPM、方向、深度、相位、Dry/Wet |
| Ghost Delay | 颗粒延迟、变调、散布、密度、反馈、随机种子、Dry/Wet |
| Space Lab | M/S 宽度、Haas、左右独立 Tilt EQ/反相、调制延迟、干湿左右分置 |
| Chaos | 概率切片、重复、倒放、随机变调/声像/失真、随机种子、Dry/Wet |
| Master Scope | 频谱、M/S/I LUFS、LRA、RMS、Crest、4x TP 估计、相关度、相位图 |
| True Limiter | 峰值保护、实时短时 LUFS、整段文件分析与匹配、Lookahead、4x/8x/16x |

效果插件具有宿主自动化、预置、状态保存、撤销/重做、电平及频谱显示。
Master Scope 保持音频逐采样直通，计量在关闭界面后仍继续。Quick Filter 的
Q 只控制第一节，高阶曲线不是 Butterworth 对齐；Saturator 的曲线不代表电路
模拟。Orbit 湿声采用单一声源，固定人头响应不保证每位听者都有相同前后感知。
True Limiter 的 Mix 位于最终峰值保护前；当前有限滤波器算法保留额外余量，
输出可能低于 Ceiling。实时短时响度控制不能代替整段音频分析。

只构建核心使用 `cmake --workflow --preset dev`；包含插件使用
`cmake --workflow --preset plugins-dev`。首次配置插件时下载固定提交和哈希的
JUCE 9.0.3，纯核心构建不会下载 JUCE。依赖准备、Release 构建、API 约束和
验证范围见[开发说明](doc/development.md)。构建不会自动安装插件；不同插件的
编译、宿主检查、数值参考和实际设备验收应按文档中列明的范围区分。
