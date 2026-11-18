# Anyvoc 0.20.1 验证记录 — 2026-11-18

本版只修复琶音器的 MIDI CC121 延音踏板重置，另更新版本显示、打包规则与验收工具。仍为 **1,256 个宿主参数、schema 14、28 个工厂音色**。完整自动测试 **213 组全部通过**，pluginval、四份音频回归、限定原生启动检查及 Windows／完整源码包校验通过。上一版的[本机报告](VALIDATION-0.20.md)、[云端 CI](GITHUB-CI-0.20.md)与[发布后原生重启检查](NATIVE_RESTART-0.20.md)独立归档。

## 问题与修复

最小复现：开启 ARP → C4 note-on → CC64=127 → C4 note-off → CC121=0，之后不再发送踏板松开。旧逻辑把 CC121 传给声音引擎，但没有清除琶音器自己的踏板保持状态，因此会继续产生已经松开的 C4。

修复让 CC121 将琶音器的踏板状态复位，只清除 `keyDown=false` 的保持音符；仍按住的键继续参与琶音，琶音步位置保留，原始 CC121 继续传递。已发出的声部按正常振幅 Release 结束，而不是被硬切静音。依据为 [MIDI Association RP-015: Response to Reset All Controllers](https://midi.org/response-to-reset-all-controllers) 对踏板控制器重置的定义。

现有琶音器仍按音高保存全局 `held`／`keyDown`／sustain，并在通道 1 输出；不同输入通道和相同音高的重叠按键没有独立所有权。本次没有改变此模型。历史间歇性持音观察与这个确定的 CC121 缺陷尚未证明是同一个原因，不能一并标记为解决。

## 回归证据

新增三组 Processor 回归：

1. 44.1／48／96 kHz、127／257／512 块长，在非块边界发送重置；释放后安静、正常尾音仍存在，并与显式 CC64 松开产生的独立参考逐样本一致。
2. Poly／Mono／Legato 中保留真正按住的键，之后松键正常结束，再进行一次踏板踩下／松开仍有效；不同分块方式得到完全相同的音频。
3. 弯音和调制轮仍由原 CC121 在引擎内复位，音频与显式 CC64 松开、CC1=0、弯音回中参考完全相同。

[修复前这三组](processor-0.20.1-cc121-before.txt)为 **1 PASS／2 FAIL**，第一个失败在 44.1 kHz／127 块长记录到应释放后的 RMS 0.023040。[修复后定向](processor-0.20.1-cc121-after.txt) **3 PASS／0 FAIL**，原 [ARP 生命周期](processor-0.20.1-arp-lifecycle.txt)与[分块／跨通道 panic](processor-0.20.1-arp-partition.txt)两组也通过。这五组定向结果不重复计入完整套件总数。定向诊断发生在升版前的工作树；发行的 0.20 包没有被修改，最终完整运行使用重新构建的 0.20.1。

## 当前验证状态

Windows x64，Visual Studio 2022／MSVC 19.44，Windows SDK 10.0.26100，CMake 4.4.4，固定 JUCE 8.0.12。[配置](configure-0.20.1.txt)与[完整 Release 构建](build-0.20.1-release.txt)成功，仍有已有类型转换警告，不宣称零警告。

本轮第一次[完整 CTest](ctest-0.20.1-final.txt)即通过，退出码 0、耗时 **199.66 秒**。[完整逐组日志](test-details-0.20.1-final.txt)确认 **213 PASS／0 FAIL**；[JUnit](ctest-0.20.1-final.xml)记录四个测试程序。没有为了取得绿色状态反复重跑；前面的诊断失败独立保留。

| 套件 | 通过组数 | 耗时 |
| --- | ---: | ---: |
| EngineTests | 75 | 5.44 秒 |
| ProcessorTests | 122 | 193.51 秒 |
| WavetableTests | 10 | 0.08 秒 |
| PresetLibraryTests | 6 | 0.63 秒 |

[pluginval strictness 5](pluginval-0.20.1.txt) **SUCCESS**，seed `0x3e22679`，实际等待进程完成，退出码 0、[stderr](pluginval-0.20.1-stderr.txt)为空。工具为 pluginval 1.0.4；这不能代替 FL Studio 加载或工程恢复。

## 音频与产物

使用新构建的 0.20.1 ProcessorTests 程序重新生成 [Legacy](render-0.20.1-legacy.txt)、[Motion](render-0.20.1-motion.txt)、[Chaos](render-0.20.1-chaos.txt)、[Macro](render-0.20.1-macro.txt)。[独立比较](demo-compatibility-0.20.1.json)确认四份 WAV 与冻结 0.20.0 Windows ZIP 的对应成员逐字节一致；基线来自旧 ZIP，没有用候选文件代替，也没有声称保留了不存在的 0.20 渲染 EXE。

[Macro 独立分析](audio-analysis-0.20.1.json)使用显式 `--version 0.20.1 --baseline-version 0.20.0`，PASS；其 Legacy before 是从真实冻结 ZIP 提取的文件。固定示例没有发送本次修复涉及的 CC121 序列，该行为变化由上述新回归验证；四份演示相同不意味着缺陷仍存在，也不证明所有音色或主观音质相同。

| 产物 | 字节数 | SHA-256 |
| --- | ---: | --- |
| Standalone `Anyvoc.exe` | 9,682,944 | `dfd63b975675ab7ef712d5d57495e73c283d71119a915569b25b12e61b69862a` |
| VST3 模块 `Contents/x86_64-win/Anyvoc.vst3` | 10,222,592 | `975c1c5e87246e2c60d616241335a027dacc7b9cb291c3892ef1b7e5f3bbcd5b` |

[六个 PE 的记录](binaries-0.20.1.json)包含 Standalone、VST3 与四个测试程序；FileVersion 和 ProductVersion 均为 **0.20.1**。生产文件在验证后没有再重建。

## 原生范围

[0.20.1 原生检查](native-smoke-0.20.1.json)使用上述 Standalone：启动后 PLAY 及页脚 0.20.1 可见；INIT 加载 Pure Canvas；点击屏幕 C4 时看到绿色输出和 EV `latest voice`，松键后 EV 回到 `idle base`、输出表熄灭。测试进程已退出，原用户设置恢复且前后 SHA-256 一致。

该会话没有发送外部 MIDI／CC121，也没有重复 SAVE／重启／LOAD、全尺寸界面或原生 COPY。0.20 的采样重启证据仅适用于报告中的旧二进制。真人听音、外接 MIDI、FL Studio／其他 DAW 与实时回调峰值均未验收；绿色电平不等于实际听音。

## 交付规则

独立 `dist/Anyvoc-0.20.1-Windows/`、Windows ZIP 与完整源码 ZIP 已生成并通过校验：CRC、安全唯一路径、精确文件集合、逐文件内容、当前文档和 0.20.1 二进制版本一致，三份必需演示均为完整 20 秒／48 kHz／24-bit／双声道 PCM。最终大小、文件数与 SHA-256 以包外 `dist/Anyvoc-0.20.1-PackageVerification.json` 为准，不将 ZIP 哈希写回包内。

首轮完整包校验通过后，收口文档并重新打包、逐文件复验；首轮候选及其报告保存在 `build/validation-0.20.1/packaging-first/`。生产二进制没有变化。冻结的 0.20 及 0.19 Windows／源码 ZIP 重新核对 SHA-256 与原交付值相同，不把新文档写回旧包。

GitHub 只推送源码、文档和验证记录，尚未上传二进制或 Release 附件。指定 0.20 提交的 CI 成功不代表本次修复提交，新的云端结果须按实际提交单独检查。
