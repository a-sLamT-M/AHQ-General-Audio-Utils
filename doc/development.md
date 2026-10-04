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
Release。测试没有外部依赖。

本次本机验证使用工作区 `.tools` 中的 CMake 3.31.6 和 Ninja 1.11.1，未修改
系统安装。当前 Windows 系统中的 CMake 版本较旧，因此应先在项目根目录为
当前终端设置工具路径，再执行上述工作流。PowerShell 命令为：

```powershell
$audioTools = Join-Path (Get-Location) '.tools/windows'
$env:Path = "$audioTools/cmake/data/bin;$audioTools/ninja-1.11.1.4.data/scripts;$env:Path"
```

WSL Ubuntu 中，在同一项目根目录使用：

```sh
export PATH="$PWD/.tools/linux/cmake/data/bin:$PWD/.tools/linux/ninja-1.11.1.4.data/scripts:$PATH"
```

`.tools` 仅供本机使用，已被 Git 忽略；其他机器需自行提供前述构建工具和
C++20 编译器。本次另用 clang-format 19.1.7 检查 C++ 格式。

## API 约束

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
