# MIDI 缓冲分配诊断方法

记录日期：2026-11-18。此工具用于开发中的 ARP MIDI 存储调查，**尚未完成真实 before／after 采集和修复验收**。它运行真实 Release 处理器与 CRT 分配调用，但只观测指定 JUCE 调用点；不是全回调零分配证明，也不是实际声卡性能测试。

比较器的 **27 项工具自检已通过**，使用合成报告与音频文件，不计入既有 220 组生产回归。准备脚本的 19 项 mock／内存锚点检查另计。真实诊断构建经历两次失败：首次 `before` 因 C++ 记录器被强制包含进 `juce_graphics_Sheenbidi.c`，出现 `STL1003: expected C++ compiler`；第二次 `before2` 尝试语言条件宏，但 JUCE 根据 `target COMPILE_DEFINITIONS` 生成 `Defs.txt`，`COMPILE_LANGUAGE` 条件使同一路径生成不同内容，配置失败。两份失败快照和日志均保留，不属于正式生产源码回归失败。

最终配置已取消语言条件生成表达式，宏和强制包含使用普通 target flags；记录器整个 C++ 实现由 `#ifdef __cplusplus` 包围，使 C 编译时该头文件为空。下一份 `before3` 全新副本仍须完成真实构建与采集，不能把前两次修正写成已通过。

## 观测范围

依赖固定为 JUCE **8.0.12**，提交 `29396c22c93392d6738e021b83196283d6e4d850`。使用 Visual Studio 2022 x64 **Release／`/MT`**，诊断宏为 `ANYVOC_MIDI_ALLOCATION_DIAGNOSTIC=1`。

[准备脚本](../scripts/prepare-midi-allocation-probe.py)仅修改新隔离副本中的三个 JUCE 文件：

| 隔离文件 | 插入的观测 |
| --- | --- |
| `modules/juce_core/memory/juce_HeapBlock.h` | `mallocWrapper`、`callocWrapper`、`reallocWrapper` 调用真实 CRT 后记录 site、请求字节和地址状态。即使 realloc 地址未变，调用仍计数。 |
| `modules/juce_audio_basics/midi/juce_MidiMessage.cpp` | `MidiMessage::allocateSpace` 内的 `std::malloc`；用于长 SysEx 正向控制。 |
| `modules/juce_audio_basics/midi/juce_MidiBuffer.cpp` | `addEvent(const void*, int, int)` 的作用域所有者，用当前 `MidiBuffer` 对象身份归类分配。 |

因此共有 **4 个分配 site**，而非三个。第三个文件只标记所有者，不是另一个分配 site。`host` 表示当前传给 `processBlock` 的精确缓冲对象；`other-MidiBuffer` 表示另一个 `addEvent` 所有者，不能一般性地推断为某个生产私有字段；本组单线程 fixture 中才对应待调查的 ARP 缓冲。`none` 表示不在该作用域中。

[记录器](../Tests/MidiAllocationRecorder.h)以 C++17 `inline thread_local` 保存当前捕获与所有者，同一最终 EXE 的 C++ 翻译单元共享该线程上的实例。每次捕获最多记录 8,192 事件，溢出单独计数并使 fixture 无效；记录路径不写日志、不扩容。Anyvoc 与诊断程序统一使用普通宏及强制包含标志，不使用语言条件生成表达式；`#ifdef __cplusplus` 使 C 编译时记录器头为空，四个分配 site 仍只位于 C++ JUCE 代码。juceaide 独立构建且不启用诊断宏，补丁在宏关闭时不依赖记录器。

以下内容不在覆盖范围：全局 `operator new`、其他直接 CRT 调用、`MidiMessage` 赋值中的独立分配、所有 `free`、锁、其他线程或整个进程。请求字节累计不是存活内存、峰值占用或泄漏量。程序离线调用 `processBlock`，不打开声卡，不统计耗时、期限、延迟、xrun 或掉音；不得用诊断 EXE 的计时替代正式实时探针。

## 生成并构建未修复的 before

先冻结真实未修复的生产 `Source`，记录实际提交、未提交差异和输入哈希，再创建副本。不能先修复源码，再仅用 `--label before` 冒充旧版。下面在仓库根目录的 PowerShell 中执行；示例名称必须未使用，失败后换新名字，不能清理重用旧目录。

```powershell
$beforeSnapshot = Join-Path (Get-Location) "build/midi-allocation-before-01"
$beforeCapture = Join-Path (Get-Location) "build/midi-allocation-before-capture-01"
python scripts/prepare-midi-allocation-probe.py --root . --output "$beforeSnapshot"

cmake -S "$beforeSnapshot/source" -B "$beforeSnapshot/build" -A x64 -DANYVOC_BUILD_MIDI_ALLOCATION_PROBE=ON -DANYVOC_BUILD_TESTS=OFF -DANYVOC_BUILD_REALTIME_PROBE=OFF
cmake --build "$beforeSnapshot/build" --config Release --target AnyvocMidiAllocationProbe --parallel 4

$beforeExe = Join-Path $beforeSnapshot "build/AnyvocMidiAllocationProbe_artefacts/Release/AnyvocMidiAllocationProbe.exe"
& "$beforeExe" --output "$beforeCapture" --label before
```

每一步检查退出码；前一步失败时不要继续采集。构建、标准输出和标准错误日志放在 capture 目录外，否则会违反比较器的精确文件集合要求。CLI 当前要求参数顺序为 `--output ABS_NEW_DIR --label before|after`。

准备脚本生成 `source/` 和 `manifest.json`，记录输入集合、字节数、原始／复制／最终哈希、基础提交、dirty 状态、脚本身份及三份补丁前后哈希。它要求固定且 tracked-clean 的 JUCE，拒绝已有输出和源目录内的输出，不改原 JUCE、不删除旧文件；复制前后核对身份、集合和全部源文件。排除的链接／reparse point 另有记录，不能忽略后自行假定副本完整。

原树没有诊断 marker，直接将 `ANYVOC_BUILD_MIDI_ALLOCATION_PROBE=ON` 打开会拒绝配置；此拒绝已实际观察。隔离树有 marker，关掉该选项同样会拒绝。正式默认构建不启用诊断宏。诊断配置排除 Standalone／VST3 的默认构建，但不阻止人为显式构建它们，因此这里只指定 `AnyvocMidiAllocationProbe`，**不分发或打包任何 instrumented EXE／插件**。

## 真实 after 与独立比较

先完整保留 before 的源码副本、manifest、构建日志、EXE 和捕获原件。完成实际生产修复后，冻结新源码并生成**另一个全新副本**；两侧诊断工具、fixture 和 hook 必须相同。仅把 label 改成 `after` 不构成修复，也不会改变程序的接受条件或主动删除分配事件。

```powershell
$afterSnapshot = Join-Path (Get-Location) "build/midi-allocation-after-01"
$afterCapture = Join-Path (Get-Location) "build/midi-allocation-after-capture-01"
python scripts/prepare-midi-allocation-probe.py --root . --output "$afterSnapshot"

cmake -S "$afterSnapshot/source" -B "$afterSnapshot/build" -A x64 -DANYVOC_BUILD_MIDI_ALLOCATION_PROBE=ON -DANYVOC_BUILD_TESTS=OFF -DANYVOC_BUILD_REALTIME_PROBE=OFF
cmake --build "$afterSnapshot/build" --config Release --target AnyvocMidiAllocationProbe --parallel 4

$afterExe = Join-Path $afterSnapshot "build/AnyvocMidiAllocationProbe_artefacts/Release/AnyvocMidiAllocationProbe.exe"
& "$afterExe" --output "$afterCapture" --label after

python scripts/compare-midi-allocation-probe.py --before "$beforeCapture" --after "$afterCapture" --output build/midi-allocation-comparison-01.json
```

[比较器](../scripts/compare-midi-allocation-probe.py)使用 Python 标准库，输出 JSON 必须是新文件，其父目录应已存在。成功或失败结果均以独占创建保留；重试另换文件名，不覆盖首次失败。

## 九个固定 fixture 与声音参考

采样率固定 48 kHz，双声道；普通分块循环为 127／256／511，长块为 8,192，生命周期另含 0 样本块。采集只包围真实 `processBlock` 调用；输入 MIDI、缓冲、记录器空间与输出文件处理均在作用域外。cold 与 warmed 分开，预热和清理事件写入 manifest，不把预热计数混进采集。

| Fixture | 主要覆盖 | 声音检查 |
| --- | --- | --- |
| `fresh-cold` | 未预热，每块新宿主 MIDI 缓冲 | 旧／新精确比较 |
| `fresh-warmed` | 预热后，每块新宿主缓冲 | 手写 MIDI 参考及旧／新比较 |
| `rotation` | 三个宿主缓冲轮转，后半程在捕获外重建 | 手写 MIDI 参考及旧／新比较 |
| `long-cc` | 8,192 样本块与跨 16 通道 CC 密集输入 | 手写 MIDI 参考及旧／新比较 |
| `lifecycle` | ARP 关开、prepare、panic、0 样本块及踏板／控制器 | 旧／新精确比较 |
| `keyboard-off`／`keyboard-on` | ARP 关／开时注入屏幕键盘事件 | 旧／新比较；保留宿主缓冲增长正控 |
| `sysex-off`／`sysex-on` | ARP 关／开时输入长 SysEx，宿主缓冲先预留 | 旧／新比较；保留 `MidiMessage` malloc 正控 |

只有 `fresh-warmed`、`rotation`、`long-cc` 三项采用独立手写参考：关闭参考处理器 ARP，显式列出 3,000 样本步长、1,500 样本 gate 的 MIDI 事件，不调用生产 ARP 生成参考事件。其他六项仅是旧／新精确声音回归，不能称独立 MIDI oracle。

每侧正常产物为 **9 个实际 `.f32le` 加 3 个手写参考 `.f32le`，共 12 个原始音频文件**，另有 `fixture-manifest.json`、`allocation-report.json`、`audio-meta.json`。float32 按小端 LR 交错写出。比较器检查精确文件集合、长度、SHA、有限／非静音数据、独立重算 RMS／peak、三个手写参考逐字节相同、两侧全部音频及 fixture manifest 逐字节相同；统计由逐事件记录重新求和。

## 接受条件与不能推出的结论

真实 allocator controls 必须观察到全部四个 site，包括 malloc、calloc、同尺寸／增长 realloc 和长 SysEx malloc；空捕获不能凭空产生事件。记录器的嵌套恢复与溢出行为另有小范围自检。所有 fixture 要求宿主输出 MIDI 清空、没有记录溢出、声音检查满足约定。

拟验证的修复条件仅针对前五个有界 ARP fixture：before 的 `other-MidiBuffer` HeapBlock 调用**总数大于 0**，after 的这些调用**逐 fixture 等于 0**。不能把 `MidiMessage` malloc 混算为私有缓冲增长，也不要求每个旧 fixture 都单独大于 0。

键盘两项仍必须观察到 `host` HeapBlock 分配，SysEx 两项仍必须观察到 `MidiMessage::allocateSpace` malloc；这些正向控制在两侧都应保留。删除它们来得到漂亮的“0”会使比较失败。即使有界私有缓冲条件通过，也只能说明这组输入下指定调用点的变化，不能宣称全处理器零分配、零锁或无掉音。

比较器自检可单独复现：

```powershell
python Tests/MidiAllocationComparatorTests.py
```

其 27 项合成测试覆盖篡改、NaN、截断、静音、溢出、集合、统计、正控和输出独占等；不运行真实 C++，不算入生产 220 组，也不代替 before／after 采集。

## 构建后身份核对与证据保留

CMake 的 marker／hook／Recorder 哈希检查发生于 **configure**，不是每次增量构建自动执行的完整冻结保护。最终构建后须再次独立核对 prepare manifest 的**全部 `files[].final_sha256`** 与隔离 `source/` 实际文件，核对 `diagnostic_marker.sha256`，并记录最终诊断 EXE 的 SHA-256、大小、Release／`/MT` 编译设置及完整构建退出结果。将 EXE 身份与该次 capture 绑定，不能只记录 label 或基础 Git SHA；dirty 源码同样需要完整字节身份。

保留两个 prepare manifest、输入核对报告、工具／CMake 身份、EXE 身份、原始 stdout／stderr、全部 JSON 与 12 个音频文件、独立比较结果。所有失败快照及日志保留原字节；重试使用新目录，不覆盖旧证据、不删除失败后冒称首次通过。capture 目录里只放程序规定的 15 个成员，额外身份／日志存其外部。

当前真实结果仍待补：未修复 before 完整构建与采集、修复后全新 after、两侧构建后输入／EXE 绑定、独立音频及分配比较、生产回归。此文的方法与工具检查不改变 [0.20.3 候选验收](VALIDATION-0.20.3.md)中的声卡受阻结论，也不属于 [a956 指定 CI](GITHUB-CI-0.20.3.md)已覆盖的源码范围。
