# AHQ General Audio

当前仓库提供一个 C++20 静态库 `ahq_audio_core`，其中只有
`ahq::audio::GainProcessor`。生产代码没有第三方依赖，可在 Windows 和 Linux
上构建。当前范围不含 GUI、插件格式或 JUCE 代码。

`GainProcessor` 对单声道或立体声浮点采样原地施加线性增益。初始增益和干湿比
都是 1，调用 `prepare()` 后直接处理时，采样值保持不变。增益和干湿比变化分别
经过 5 毫秒线性过渡。处理器不对输出削波。构建命令和 API 约束见
[开发说明](doc/development.md)。
