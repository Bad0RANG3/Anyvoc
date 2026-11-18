# Anyvoc 0.20.4 候选验证

记录日期：2026-11-18。此版针对 ARP MIDI 缓冲在回调之间交换存储、丢失私有预留容量的问题进行最小修复。**真实 before／after 隔离比较已通过：五个有界 fixture 的私有缓冲分配 43→0，12 个原始音频文件逐字节相同，键盘与 SysEx 正控保留。正式无插桩 0.20.4 Release、220 组、pluginval 和四演示回归也已通过；候选包完整性校验 PASS；最新推送 CI 暂停时未确认；原生／实时验收仍待完成。** 本页不把开发工具自检、诊断二进制或旧版结果当作当前整版验收。

## 改动与版本身份

旧 `processArp` 在开启 ARP 时生成私有 `arpMidi`，随后与宿主 MIDI 缓冲 `swapWith`；私有预留存储会被换给宿主，下次回调可能再次增长。当前最小改动使该函数返回供引擎读取的缓冲引用：ARP 开启时返回私有 `arpMidi`，关闭时返回原宿主缓冲；引擎直接渲染此引用，回调末尾仍清空宿主输出 MIDI。生产改动仅涉及 `PluginProcessor.cpp/.h` 的返回值与调用接线，参数、schema、工厂音色及 SynthEngine 算法不变。声音、事件时序及生命周期的证据限于下述实际测试范围。

必须区分以下身份：

| 对象 | 当前版本／作用 |
| --- | --- |
| `before3` 诊断 EXE | 使用真实未修复 Source，隔离 CMake 版本为 **0.20.3**；用于建立分配 baseline，不是正式 0.20.4。 |
| `after` 诊断副本 | 使用真实修复 Source，保留与 before3 **完全相同的 0.20.3 诊断 CMake**，其余工具与 fixture 一致；构建／采集／身份核对通过。 |
| 正式生产候选 | CMake 版本为 **0.20.4**，正常无插桩 Release 构建通过；EXE／VST3／渲染器身份及正式测试已另行核对。 |

`before`／`after` 标签只是记录，不改变程序的接受条件，也不能代替源文件身份。冻结的 0.20.3 档案、候选包、dist 文件及 [a956 指定 CI](GITHUB-CI-0.20.3.md)不改写；该 CI 不覆盖本次 ARP 修复和新诊断工具。

## 已取得的真实 baseline

采用固定 JUCE `29396c22c93392d6738e021b83196283d6e4d850`，隔离副本中三个 JUCE 文件插入四个真实 CRT 调用点，Release／`/MT` 构建成功。完整方法和边界见 [MIDI 分配诊断说明](MIDI_ALLOCATION_PROFILING.md)。这里不是 `operator new` 拦截，也不统计 free、锁或实时期限。

`before3` EXE 为 **9,825,792 字节**，SHA-256：

`8a627e40e49e6b405763140382fd21401174f2c8b0999ad79c026375f425d95a`

构建后独立核对 **4,405 份输入文件**、marker、两个目标的 `MultiThreaded` runtime、诊断宏、实际 `ForcedIncludeFiles` 和完整 capture，结果 PASS。prepare manifest SHA-256 为 `dc7d16c6ec5b30fd33bc1777c1161cc40f97f688746b6bdacde301e2abcf5ab4`；marker 为 `584bfcb8df841fbc3c8cecdeed7cd507c512830d51a1814bc750248020c8b9b1`。这项核对发生于最终构建后，不冒称 CMake 在每次增量编译前自动复查所有输入。

九个 fixture 全部 valid，三个手写 MIDI oracle 的实际音频逐字节相同。每侧正常输出 9 个实际音频加 3 个手写参考，共 **12 个小端交错双声道 float32 文件**。只有 `fresh-warmed`、`rotation`、`long-cc` 有独立手写 oracle；其余六个仍须靠旧／新精确音频比较，不扩大为九个独立事件参考。

前五个有界 ARP fixture 在 baseline 中均观察到 `other-MidiBuffer` 的 `heapRealloc`：

| Fixture | 私有缓冲调用次数 | 累计请求字节 |
| --- | ---: | ---: |
| fresh-cold | 7 | 112 |
| fresh-warmed | 8 | 128 |
| rotation | 5 | 80 |
| long-cc | 10 | 3,552 |
| lifecycle | 13 | 224 |
| 合计 | **43** | **4,096** |

字节列是调用时请求尺寸的累计，不是净新增或峰值内存。`other-MidiBuffer` 通过 `addEvent` 对象身份分类，在这些单线程 fixture 中对应 ARP 私有缓冲；不是依据地址范围猜测，也不作全进程通用推断。

正向控制保留了另外几类实际分配：

| Fixture | baseline 观察 |
| --- | --- |
| keyboard-off | 宿主 `heapRealloc` **2 次／32 字节**。 |
| keyboard-on | 宿主 `heapRealloc` **2 次／32 字节**，另有私有缓冲 **6 次／96 字节**。 |
| sysex-off | `MidiMessage::allocateSpace` malloc **2 次／2,052 字节**。 |
| sysex-on | `MidiMessage::allocateSpace` malloc **4 次／4,104 字节**，另有私有缓冲 **1 次／16 字节**。 |

这些控制证明观察范围内仍能看到真实分配；after 必须继续保留键盘宿主增长与 SysEx malloc 的正控，不能为了得到“0”而删除它们。验收条件仅为前五个有界 fixture 的私有 HeapBlock 调用：baseline 总数大于 0，after 逐 fixture 等于 0；所有 fixture／音频同时满足比较器的严格条件。

## 修复后实际比较

after 的 9 个 fixture 全部 valid，按上表顺序的总捕获调用次数为 **0／0／0／0／0／2／2／2／4**。前五项私有调用由 **43 降为 0**，累计请求字节由 **4,096 降为 0**。keyboard-off／on 仍各有宿主增长 **2 次／32 字节**；sysex-off 仍有 malloc **2 次／2,052 字节**，sysex-on 仍有 **4 次／4,104 字节**。这不是全回调“零分配”。

独立比较器核对两侧 fixture manifest 和 **12 个原始音频文件全部逐字节相同**，每侧 **1,560,000 音频字节／195,000 双声道帧**，其中 3 个文件是手写参考；9 个实际输出共 146,000 帧。三个手写参考在各自版本也精确相同。调用统计逐事件重算，音频长度、SHA、有限性、非静音、RMS／peak 和精确文件集合均满足条件。

after 诊断 EXE SHA-256 为 `e5ddc24f6b13cad59a5b9c5f8b8c2811fb52dd1e4289c02e5e2d14b0419180be`。构建后 4,405 文件、marker、`/MT`、宏、实际强制包含及 capture 身份核对 PASS。独立 source delta 比较确认两侧精确集合相同，仅 `Source/PluginProcessor.cpp` 和 `.h` 字节不同；诊断 CMake、工具、JUCE hook 与 fixture 完全相同。该 EXE 仍是 0.20.3 标记的隔离诊断程序，不能用它代替正式 0.20.4 的独立检查。

## 正式无插桩 0.20.4

[正式验收原件](validation-0.20.4/README.md)独立归档，正常 Release 构建成功。26 份 Source／Tests 文件与固定 after 快照逐字节一致，CMake 仅版本行从 0.20.3 改为 0.20.4；三个生产 JUCE 文件与未修改原件一致。实际 Release `/MT` 未启用诊断宏或强制包含，诊断 option 为 OFF。七个 PE FileVersion 均为 0.20.4，不分发诊断 EXE。

完整 CTest **220／220（75／129／10／6），0 FAIL，总用时 210.62 秒**。JUnit 各套时间为 Engine **4.9454**、Processor **204.953**、Wavetable **0.079499**、PresetLibrary **0.636504** 秒。pluginval strictness **5 SUCCESS**，seed **`0xc83e3a`**。

本轮正式 Processor 渲染器重新生成 Legacy、Motion、Chaos、Macro 四份 WAV，均与冻结 0.20.3 Windows ZIP 及其对应目录文件逐字节相同；旧 ZIP SHA-256 为 `077c4e958ecb2ae33d150e3e8aac914c2a4005986d8361773666e02467fbdd99`。宏独立分析 PASS，显式标签为 `version=0.20.4`、`baseline_version=0.20.3`。只证明这些固定输入的回归，不等于听音或实际声卡演奏。

| 正式产物 | SHA-256 |
| --- | --- |
| Standalone | `3d55bd6f7a1679242139bdbee6d993d22f1d43647c4f93f25b3baf91d5ae895a` |
| VST3 模块 | `cc32e87dfc6b297cc1cb9d8134dcd739d77479c7f30e92a312f58bdeca60f522` |
| Processor 测试／渲染器 | `4dafbd7f6ce5836724d3e3bb79665b73592eca166c40712c345768bf7be5525c` |

## 工具检查与失败记录

比较器的 **27 项合成自检**在 Python 3.14 与直接路径启动的 Python **3.11.17** 均通过；分别为 2.165 秒、2.379 秒。它们检查解析、篡改、NaN、截断、静音、溢出、统计、正控和独占输出，**不计入 220 组生产回归**，也不代替真实采集。`py -3.11` 启动器曾报告没有匹配 runtime；使用实际解释器路径后成功，这是环境启动失败，不是比较器测试失败。准备脚本 19 项 mock／内存锚点检查另计。

本地保留以下失败，不能把最终成功写成首次通过：

1. 第一份 `before` 隔离编译把 C++ Recorder 强制包含进 JUCE `juce_graphics_Sheenbidi.c`，报 STL1003。第二份 `before2` 改用语言条件宏，但 JUCE 从 target compile definitions 生成同一路径 `Defs.txt` 时内容随语言不同，配置失败。最终使用普通 target 宏／强制包含，Recorder 的 C++ 实现由 `#ifdef __cplusplus` 包围，C 编译时头文件为空；第三份 `before3` 才成功。这两项属于诊断配置失败。
2. 辅助身份核验器首次错误地假设 `/FI` 出现在 MSBuild `AdditionalOptions`，实际为 `ForcedIncludeFiles`；另一次本地 helper 导入父路径拼错。修正辅助核验器后，没有改变任何快照输入或 EXE，最终身份核验 PASS。这两次没有保留原始 stdout，此处是事实说明，不冒称已有失败日志。这不是通过修改被核对对象来消除差异。

以下完整构建树和 EXE 仍仅保留在本地；选定原件已另行归档，不将整个大目录纳入 Git：

| 本地路径 | 内容 |
| --- | --- |
| `build/validation-midi-allocation-before/`、`before2/`（同一 `validation-midi-allocation-` 前缀） | 前两次失败隔离树与日志。 |
| `build/validation-midi-allocation-before3/` | `manifest.json`、`configure.txt`、`build.txt`、`capture.txt`、`postbuild-identity.json`，以及真实旧源码／EXE。 |
| `build/validation-midi-allocation-before-capture/` | 3 个报告 JSON 与 12 个原始音频文件，完整精确集合。 |
| `build/validation-midi-allocation-after/`、`build/validation-midi-allocation-after-capture/` | 修复后隔离源码／EXE、构建后身份报告和完整 capture。 |
| `build/validation-midi-allocation-tooling/` | 工具自检、启动器环境失败、辅助核验脚本及开发日志。 |

`build/validation-midi-allocation-tooling/comparison.json` 为独立比较结果，`source-delta.json` 为两侧 4,405 文件比较。失败或成功目录均不覆盖。后续补充证据应绑定其实际输入、EXE 和 capture 身份，而不是仅引用基础 Git SHA 或 before／after 名称。

[诊断专项档案](midi-allocation-0.20.4/README.md)共 79 文件、10,134,351 字节，其中 76 份复制原件逐字节核对；集合 SHA-256 为 `9d9ebcb740e79a7f218d541e9bb4321e56e24783723c6d050e38464f6f4c3174`，见[外置清单](midi-allocation-archive-verification-0.20.4.json)。[正式验收档案](validation-0.20.4/README.md)共 22 文件、82,579 字节，20 份复制原件一致；集合 SHA-256 为 `1664bc1e44d3ebbc5c0d9caff5e7c69dffe465247e2f0fca78ad85d181fd1cc7`，见[外置清单](validation-archive-verification-0.20.4.json)。两份档案均不包含发行二进制或新的设备采集。

## 当前验收状态

| 项目 | 状态 |
| --- | --- |
| 未修复的真实 baseline | **完成**：Release 构建、9 valid、3 手写 oracle exact、4,405 输入及 EXE／capture 身份核对通过。 |
| 修复后的隔离 after | **通过**：9 valid、3 手写 oracle exact、构建后完整输入／EXE／capture 身份核对通过。 |
| before／after 独立比较 | **通过**：相同 fixture、12 音频逐字节相同、前五项私有增长 43→0、4,096→0 请求字节，键盘／SysEx 正控保留。 |
| 正式 0.20.4 Release | **通过**：无插桩 Release、版本／输入／正式 EXE 身份已核对。 |
| 正式完整 220 组、pluginval、四份演示 | **通过**：220／220，210.62 秒；strictness 5 seed `0xc83e3a`；四份本轮重渲染与冻结 0.20.3 字节一致，宏分析通过。 |
| 原生演奏、实际设备回调 | **待验／设备受阻**：用户确认其他程序独占声卡，尚未释放重测；本轮不重试、不关闭该程序。 |
| 候选包 | **完整性校验 PASS**：CRC、安全唯一路径、精确集合、逐文件内容、版本、必需演示和当前文档全部通过。最终文件集合与哈希以包外报告为准。 |
| 新提交 CI | 本地验收已过；**最新推送 CI 须按实际 head 另核，暂停时未确认**。未借用历史 CI，未宣称整版交付。 |

本机候选入口为 **`dist/Anyvoc-0.20.4-Windows/Anyvoc.exe`**，收到 Windows ZIP 后须完整解压。首轮 ZIP 与报告已留存 `build/validation-midi-allocation-tooling/packaging-first/`；候选包完整性校验 PASS，最终权威记录为包外 `dist/Anyvoc-0.20.4-PackageVerification.json`。本页不写候选 ZIP 哈希，避免包内自引用或把首包哈希套用至最终包。

上述有界私有 MIDI 缓冲条件通过，不能推导全处理器零分配、零锁、实际回调准时或无掉音；听音、外接 MIDI、FL Studio／其他 DAW 仍需各自证据。本轮没有运行新的原生或设备检查，不借用 0.20.3 的窗口观察作为新版验收。0.20.3 的[候选报告](VALIDATION-0.20.3.md)及既有设备失败记录保持独立。
