# Anyvoc 0.20.2 验证记录 — 2026-11-18

本版修复琶音器的多通道、重复按键与踏板所有权，完整合同见 [ARPEGGIATOR.md](ARPEGGIATOR.md)。完整 **220 组测试、pluginval、四份音频回归、限定原生启动及 Windows／完整源码包校验通过**。仍为 **1,256 个宿主参数、schema 14、28 个工厂音色**。历史 [0.20.1 本机验收](VALIDATION-0.20.1.md)、[指定提交云端验证](GITHUB-CI-0.20.1.md)和 [0.20 采样重启检查](NATIVE_RESTART-0.20.md)各自保留，不作为本版已通过的证据。

## 改动与兼容

旧琶音器只有 128 个全局音高槽和一个踏板状态，生成音符固定在 MIDI 通道 1。不同通道的同音高会相互覆盖，松键和踏板影响彼此，生成音符也可能使用错误通道的弯音／调制轮。CC120／CC123 会清空整个池并额外向通道 1 发送停止命令。

现在 16×128 个组合分别保存饱和按键计数、最新力度与锁存，16 个踏板独立；固定候选数组按音高再通道排序。重复 note-on 仍占一个候选，匹配的 note-off 全部到齐后才释放；零力度 note-on 视为 note-off，未匹配的松键被忽略。生成音符使用来源通道。CC64 仅控制来源锁存，CC121 只重置该通道踏板并继续转发；CC120／CC123 只停止该通道，保留其踏板控制器值和其他通道的步进。

普通松键移除一个候选、但池中仍有其他候选时，当前脉冲按原 Gate 走完；空池时立即发送 note-off 并按正常 Release 结束。相同采样位置的输入先按原顺序处理，再更新 Gate 和步进。Poly／Mono／Legato 切换保留候选与时钟，全局 PANIC、ARP 开关、prepare 和原有声音／宿主停止重置仍清空池。步进计数改用无符号 64 位，避免原有长期有符号递增溢出。

单通道、没有重叠同音高的正常演奏继续沿用原步长、Gate 和八度公式。多通道、重复按键及定向停止行为是有意修复的变化，不保留旧错误音频。[源码定义比较](source-compatibility-0.20.2.json)确认相对真实 0.20.1 提交，参数布局与 28 个工厂预设的定义在统一换行后逐字节相同；这不替代运行时兼容测试。

固定容量只适用于新增按键和候选资料，不宣称整条音频回调零分配；已有 MIDI 缓冲交换及高负载回调峰值仍需另行测量。MPE、Clip、宿主 seek／loop 小节锁相没有在本版实现。

## 先复现再修复

前五组回归使用未修改的 `f37169e9e7f4ccc3c4de7655bde84fdfa30af801` 生产逻辑构建，结果 **0 PASS／5 FAIL**，分别命中重复按键、同音高通道所有权、独立踏板／CC121、来源通道轮控以及定向 panic。[原始失败输出](processor-0.20.2-ownership-before.txt)已归档，修复前测试源快照也保留在本地 `build/validation-arp-ownership/`。该构建只用于诊断，未覆盖冻结的 0.20.1 发行包。

参考处理器关闭 ARP，使用手工列出的绝对采样位置、note-on／note-off／控制器事件；不调用生产调度或候选池代码。两者使用相同固定声源和初始平滑／随机状态，逐样本严格相等；另检查参考确实发声、尾部确实安静、音色参数未改变以及最终无活动声部。分块一致性是附加检查，不能替代音符选择参考。

补齐分块及生命周期测试后，[0.20.2 定向首次运行](processor-0.20.2-ownership-after.txt) **7 PASS／0 FAIL**；[目标重建日志](build-0.20.2-targeted.txt)保留原始警告。七组覆盖 Poly／Mono／Legato，来源通道 1／2／16，重复与零力度按键、独立踏板、CC121、弯音／调制轮、排序及八度上限、CC120／123、同采样位置的两种顺序、44.1／48／96 kHz 与 127／257／512 块长，以及五类全局清理和声部模式切换。严格相等断言没有因运行结果而放宽。

## 构建与完整自动测试

Windows x64，Visual Studio 2022／MSVC 19.44、Windows SDK 10.0.26100、CMake 4.4.4，固定 JUCE 8.0.12。[CMake 配置](configure-0.20.2.txt)、[定向目标构建](build-0.20.2-targeted.txt)及[完整 Release 构建](build-0.20.2-release.txt)通过，保留已有类型转换等警告，不宣称零警告。

本轮第一次[完整 CTest](ctest-0.20.2-final.txt)即通过，退出码 0，**220 PASS／0 FAIL，204.40 秒**。[完整逐组日志](test-details-0.20.2-final.txt)与 [JUnit](ctest-0.20.2-final.xml)保留原始结果。七组定向运行不重复计入总数，原三组 CC121 回归也在全套中再次通过。

| 套件 | 通过组数 | 耗时 |
| --- | ---: | ---: |
| EngineTests | 75 | 10.02 秒 |
| ProcessorTests | 129 | 193.67 秒 |
| WavetableTests | 10 | 0.08 秒 |
| PresetLibraryTests | 6 | 0.63 秒 |

## VST3、音频与二进制

[pluginval strictness 5](pluginval-0.20.2.txt) **SUCCESS**，随机种子 `0x63446cc`；实际等待进程完成，退出码 0，[stderr](pluginval-0.20.2-stderr.txt)为空。工具为 pluginval 1.0.4。它验证的是本版 VST3 模块，不代替 DAW 扫描或工程恢复。

使用新 0.20.2 测试程序重新生成 [Legacy](render-0.20.2-legacy.txt)、[Motion](render-0.20.2-motion.txt)、[Chaos](render-0.20.2-chaos.txt)、[Macro](render-0.20.2-macro.txt)。[逐字节比较](demo-compatibility-0.20.2.json)确认四份当前 WAV 与冻结 0.20.1 Windows ZIP 对应成员一致；该旧 ZIP 的 CRC 及原始 SHA-256 也核对通过。

真实留存的 0.20.1 ProcessorTests EXE（SHA-256 `be79d16b9359880b05a7c690a702741ba201dfe1b39c2a376b1acbb1ae0018de`）另[新渲染 Legacy 基线](render-0.20.1-before-0.20.2.txt)，也与旧 ZIP 一致。[宏音频分析](audio-analysis-0.20.2.json)显式使用 `--version 0.20.2 --baseline-version 0.20.1`，before 取自这个真实旧程序的新渲染，结果 PASS。

固定演示未包含本次修复的多通道和重复同音高序列；那些有意改变的音频由新的手写 MIDI 回归检查。固定演示一致不能推断所有声音、主观音质或硬件实时性能相同。

| 产物 | 字节数 | SHA-256 |
| --- | ---: | --- |
| Standalone `Anyvoc.exe` | 9,682,432 | `f0d3252e80e92561d8d8ef50dbcd2dde2edde12167b63e97c039d498a41ff323` |
| VST3 `Contents/x86_64-win/Anyvoc.vst3` | 10,221,568 | `3015c1c06691e71b581718016fa74271fb4118f44ba8f7d73e6cdd946d3228fe` |

[二进制记录](binaries-0.20.2.json)包含生产文件、四个测试 PE、验证器与旧基线；六个新 PE 的 FileVersion／ProductVersion 均为 0.20.2。生产文件在上述验证后未重建。

## 限定原生检查

[实际 0.20.2 独立版记录](native-smoke-0.20.2.json)确认：启动进入 PLAY，页脚 0.20.2；INIT 加载 Pure Canvas；屏幕 C4 高亮、EV 显示 `latest voice`、输出表为绿色；松键后 EV 为 `idle base`、输出表熄灭。进程 PID 123292 已退出，原用户设置恢复且前后 SHA-256 相同。该操作使用上表中的实际 Standalone。

本次原生会话没有发送外接多通道 MIDI、重复按键或踏板／CC121／120／123，也没有重复保存重启／加载、全尺寸布局或系统剪贴板。外接 MIDI、真人听音、FL Studio／其他 DAW、实时回调峰值及全部素材重启组合仍未验收。原生屏幕琴键输出不能证明外部设备的多通道操作已经通过。

## 交付边界

独立 `dist/Anyvoc-0.20.2-Windows/`、Windows ZIP 与完整源码 ZIP 已生成并通过校验：CRC、安全唯一路径、精确文件集合、逐文件内容、当前文档、0.20.2 二进制版本／哈希和三份完整必需演示一致。最终文件数、大小及 ZIP SHA-256 以包外 `dist/Anyvoc-0.20.2-PackageVerification.json` 为准，不写回包内文档。

首轮完整包校验通过后收口文档并重新打包、逐文件复验，首轮候选及其报告保存在 `build/validation-0.20.2/packaging-first/`。README 同时加入原创 SVG 横幅、状态徽章、明确标为 0.20 快照的界面预览及导航；使用 GitHub Markdown API 和本地浏览器渲染检查布局，不把静态预览当作原生运行证据。

0.20.1 及更早发行目录与 ZIP 冻结。GitHub 当前只保存源码、文档和文本证据，尚无二进制 Release 附件；指定 0.20.1 的 CI 成功不代表新提交，新版云端结果须按实际提交分别检查。
