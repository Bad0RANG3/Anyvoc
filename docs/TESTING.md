# Anyvoc 测试与验收指南

本指南针对 **0.20.2 / Windows x64 / VST3 与独立版**。本轮修复 ARP 多通道按键与踏板所有权，完整合同见[琶音器约定](ARPEGGIATOR.md)。七组新增回归的首次修复后定向运行已 **7／7 通过**；随后完整四套测试 **220＝75／129／10／6 全部通过，204.40 秒**。pluginval、音频对比、有限范围原生 smoke 及本机发行包校验已通过；这些证据各自保留适用范围。[本版验证记录](VALIDATION-0.20.2.md)汇总实际结果；操作与复现见[用户指南](USER_GUIDE.md)和[构建指南](BUILDING.md)。

“测试通过”只适用于记录中的版本、硬件、设置和步骤。自动化测试、pluginval、界面截图、真人听音、DAW 工程重开是不同证据，不能互相替代，也不能证明与 Serum 2 声音等价。

## 1. 当前证据与结果

### 0.20.2：本机交付校验通过

| 检查 | 当前状态与通过要求 |
|---|---|
| 旧逻辑失败证据 | 前五组新增 `Arp ownership` 回归在冻结旧生产逻辑下 **5／5 失败**，[原始日志](processor-0.20.2-ownership-before.txt)保留；后两组属于新增时序与生命周期覆盖，不冒称已参与这轮红例。 |
| 修复后定向／完整自动测试 | [七组定向首次运行全部通过](processor-0.20.2-ownership-after.txt)，未改 fixture，逐样本严格相等断言保留。随后 [Release 构建](build-0.20.2-release.txt)和[完整四套测试](ctest-0.20.2-final.txt)通过：**220／220，204.40 秒**；Engine 75／10.02 秒，Processor 129／193.67 秒，Wavetable 10／0.08 秒，PresetLibrary 6／0.63 秒。[逐组日志](test-details-0.20.2-final.txt)、[JUnit](ctest-0.20.2-final.xml)独立保留。 |
| 参数／状态／预设 | 保持 **1256／schema14／28**；[参数与工厂预设源码定义核对](source-compatibility-0.20.2.json)和本轮完整兼容回归通过，原参数定义、顺序不变。 |
| pluginval／二进制 | [实际 0.20.2 模块 strictness 5：SUCCESS](pluginval-0.20.2.txt)，退出码 0，seed `0x63446cc`。[二进制记录](binaries-0.20.2.json)包含当前生产与四个测试程序的版本、大小与 SHA，六个当前 PE 均为 0.20.2；历史 renderer／pluginval 工具另列。 |
| 音频／预览 | [四份 0.20.2 新渲染与冻结 0.20.1 ZIP 成员逐字节一致](demo-compatibility-0.20.2.json)；真实留存 0.20.1 EXE 另行新渲染 Legacy，也逐字节一致。[独立宏分析 PASS](audio-analysis-0.20.2.json)，显式 current `0.20.2`／baseline `0.20.1`。本轮未记录重新生成 92 张预览；历史截图不改称本轮结果。 |
| 原生 smoke | [0.20.2 限定检查通过](native-smoke-0.20.2.json)：真实启动 PLAY／页脚、INIT、屏幕 C4 绿色电平与 EV latest voice，松键后 idle／电平熄灭。测试进程已退出，原设置恢复且 SHA 相同。未做外接 MIDI、实际多通道／重复键／踏板控制器输入、SAVE／重启／LOAD、真人听音、DAW、最小尺寸／DPI 巡检或回调峰值测量。 |
| 发行包 | Windows／完整源码包首轮校验通过：CRC、安全唯一路径、精确集合、逐文件内容、二进制版本及必需演示均通过。当前入口 `dist/Anyvoc-0.20.2-Windows/Anyvoc.exe`；包外 `dist/Anyvoc-0.20.2-PackageVerification.json` 为最终权威记录。文档或其他输入更新后须重新打包逐文件复验。 |
| 云端 | 本版本尚无云端记录；历史 [0.20.1 f37169e9 CI](GITHUB-CI-0.20.1.md)已通过 213 组，仅覆盖该提交。 |

### 0.20.1：已交付历史结果，不是本轮结果

| 检查 | 当前状态与通过要求 |
|---|---|
| 构建与全套测试 | **213／213 通过，199.66 秒**；[CTest](ctest-0.20.1-final.txt)、[逐组日志](test-details-0.20.1-final.txt)、[JUnit](ctest-0.20.1-final.xml)。Engine 75／5.44 秒，Processor 122／193.51 秒，Wavetable 10／0.08 秒，PresetLibrary 6／0.63 秒，四套零失败。 |
| 新增回归 | 三组 ARP CC121 测试均通过：释放踏板锁存并保留有限 release；保留实按键与后续踏板周期；向引擎传递弯音／调制轮复位。实测范围见下方用例，不代表所有 ARP 或 MIDI 情况均已验证。 |
| 参数／状态／预设 | **1256／schema14／28** 和原有映射保持；本轮完整兼容回归通过。 |
| pluginval | [strictness 5：SUCCESS](pluginval-0.20.1.txt)，退出码 0，seed `0x3e22679`，实际 0.20.1 模块；[二进制记录](binaries-0.20.1.json)。 |
| 音频 | [四份新渲染与冻结 0.20 ZIP 成员逐字节一致](demo-compatibility-0.20.1.json)；[独立宏分析 PASS](audio-analysis-0.20.1.json)，明确 current `0.20.1`、baseline `0.20.0`。未使用留存 0.20 测试程序重新渲染旧参照。 |
| 发行包 | Windows／完整源码包校验通过：CRC、安全唯一路径、精确文件集合、逐文件内容、当前文档及 0.20.1 二进制版本一致。独立版入口 `dist/Anyvoc-0.20.1-Windows/Anyvoc.exe`；权威记录为包外 `dist/Anyvoc-0.20.1-PackageVerification.json`，不将 ZIP 哈希写入包内。 |
| 原生启动／演奏 | [0.20.1 限定检查通过](native-smoke-0.20.1.json)：新程序 PLAY／版本显示、INIT、屏幕 C4 绿色输出与 EV latest voice，松键后 idle／电平熄灭；退出并恢复原设置哈希。未做当前版本 SAVE／重启／LOAD、原生 CC121、外接 MIDI、真人听音或 DAW。 |
| 云端 | [0.20.1 f37169e9 的 213 组通过](GITHUB-CI-0.20.1.md)：运行 `44273076006`，四套 JUnit 合计约 168.78 秒，构建与原始附件已核对；不覆盖 0.20.2。 |

[真实 0.20 独立版重启核对](NATIVE_RESTART-0.20.md)已通过：SAVE→退出→原素材离线→重启→INIT→LOAD→再 SAVE，1256 个参数实际值和内嵌 WAV 字节一致，96000 帧 PCM 转换精确匹配。它仅覆盖记录中的一个样本及两路调制；不改称为 0.20.1 检查，也不是播放音频捕获或真人听音。

### 0.20：已交付历史结果，不是本轮结果

| 检查 | 当前状态 | 证据与范围 |
|---|---|---|
| 生产构建 | 已通过 | Standalone／VST3 已构建；首次 private 成员访问编译失败及修复后的日志均保留 |
| 最终完整自动测试 | **210／210 通过，198.69 秒** | [CTest](ctest-0.20-final.txt)、[逐组日志](test-details-0.20-final.txt)、[JUnit](ctest-0.20-final.xml)：Engine 75／4.85 秒，Processor 119／193.14 秒，Wavetable 10／0.06 秒，PresetLibrary 6／0.63 秒 |
| 首轮失败与 COPY 定向修复 | 首轮 **209 PASS／1 FAIL**；定向 **1／1 通过** | [首轮日志](ctest-0.20-initial.txt)与[修复复测](processor-0.20-copy-fix-2.txt)独立保留；12 个通知边界与销毁／导航／自动化场景的严格断言保留，随后已执行上面的最终四套全量运行 |
| 参数／状态／预设 | 源码核对与该版回归通过 | [源码兼容记录](source-compatibility-0.20.json)：相对冻结 0.19 的参数和工厂片段相同，仍为 1256／schema14／28；运行时状态与路由另由自动测试检查 |
| pluginval | **strictness 5：SUCCESS** | 0.20.0 模块，seed `0x7910988`；若之后生产代码变化，需重新验证相应产物 |
| 音频 | 0.20 渲染及独立分析通过 | 0.20 Legacy／Motion／Chaos／Macro 四 WAV 与冻结 0.19 包对应成员逐字节相同；Legacy 另与真实留存 0.19 测试程序新渲染比较 |
| PLAY 原生操作 | 限定范围通过 | [原生记录](ui-play-native-0.20.json)：默认 PLAY、Filter A 旁通、跨页参数／Undo、BPM 包络显示、Lab 打开时拖放、最小窗口、C4 绿色输出、音色浏览入口 |
| COPY 原生操作 | 限定范围通过 | [记录](native-copy-0.20.json)：列表、详情、旋钮菜单三入口；单 Undo／Redo、独立副本、四条匹配行 Remove-all 与一次 Undo；最小尺寸 |
| 预览 | 生成 **92 张**，人工查看 **6 张** | [生成日志](preview-0.20-initial.txt)；PLAY 默认／小、Synth 默认、Matrix-detail／expanded／last-page 小尺寸无观察到的裁切；其余 86 张未声称逐张查看 |
| 包校验 | **本机 Windows／完整源码包通过** | 文件集合、逐文件内容、当前文档与 0.20.0 二进制一致；最终报告为包外 `dist/Anyvoc-0.20.0-PackageVerification.json`，不把 ZIP 哈希写回包内 |
| 云端 CI | **beecbc09 的 210 组通过** | [运行与原始证据](GITHUB-CI-0.20.md)，四套合计约 174.62 秒；该结果不覆盖后续 0.20.1 修复提交 |
| 真人听音／外接 MIDI／DAW | 该版未完成验收 | 绿色电平不是听音证据；pluginval 不替代宿主扫描、自动化或工程重开 |

上述 **0.20** 构建、测试、pluginval、音频报告和原生记录已归档到 `docs/`，索引见 [VALIDATION-0.20.md](VALIDATION-0.20.md)。[首次生产构建失败](build-0.20-production.txt)、[修复构建](build-0.20-production-fix.txt)、[PLAY 构建](build-0.20-play-production.txt)、[首轮测试明细](test-details-0.20-initial.txt)及[最终明细](test-details-0.20-final.txt)分别保留。历史 WAV 和留存旧版本程序在被 Git 忽略的 `build/validation-baseline/`，首次克隆不自带；0.20.1 当时没有可用的 0.20 ProcessorTests EXE，本轮已有真实 0.20.1 测试程序作参照。

COPY UI 的[第一次诊断](processor-0.20-copy-diagnostic-1.txt)在 editor 销毁的 begin 回调中，参数精确检查已通过，但监听器报告 starts=2、ends=1、active=0。测试 probe 晚于 GUI listener 注册；JUCE 逆序通知期间 editor 移除较早 listener，使 probe 再被通知一次。将 probe 提前注册后保留原计数断言；[第二次诊断](processor-0.20-copy-diagnostic-2.txt)发现切到 SYNTH 后用仅找可见组件的助手查隐藏矩阵，改为提前保存 SafePointer 并继续验证取消。[测试目标重建](build-0.20-copy-fix-2.txt)和[定向复测](processor-0.20-copy-fix-2.txt)通过后，最终四套再次全部通过。生产代码没有因这项 fixture 修复而变化。

完整路由发布的验收必须在操作**中途**运行真实音频回调。覆盖 copy、单行／按目标批量清空、move、Undo／Redo、宿主嵌套恢复、实际通知值及持续用户手势；要求发布未完成时使用之前的完整图，结束后才接收最终实际图。旋钮连续编辑的每个值仍应进入后续音频块。协议只覆盖 [ROUTE_PUBLICATION.md](ROUTE_PUBLICATION.md)列出的路由操作，不扩张到整个音色、素材或宿主分别发送的自动化事件。不能把方法返回后的单次 render 冒充中途并发验证。

[PLAY 原生检查](ui-play-native-0.20.json)确认界面与引擎活动，音色入口只检查浏览器打开，未加载另一音色。[COPY 原生检查](native-copy-0.20.json)确认三入口、独立修改、Undo／Redo、最小布局和四行批量清空；深度值设置包含 UI Automation，不冒充全部鼠标拖动。两场均使用记录中的相同 0.20 二进制，测试进程已退出，原设置恢复且哈希一致。未原生手工核对 COPY 全 19 字段、满表、最后页导航或宿主回调中断；这两场当时也未做真人听音、外接 MIDI、DAW、完整音色保存／重启／恢复或所有编辑器全部尺寸。后补的独立重启会话单列在上面的专门报告中；其余未测范围不因自动测试覆盖部分路径而改写为原生通过。

### 历史记录单独归档

| 版本 | 本机验收与失败／修复历史 | 云端或补充证据 |
|---|---|---|
| 0.19 | [完整验收](VALIDATION-0.19.md)：sanitizer 首轮失败、Phase 链生产修复、UI 文字补测、首次包输入不一致和最终限定原生范围 | [指定提交 CI](GITHUB_CI-0.19.md)；[原生 SAVE→INIT→LOAD](native-0.19.json)未覆盖进程重启／DAW |
| 0.18 | [完整验收](VALIDATION-0.18.md)：测试辅助代码编译失败及修正、曲线工具模型／集成测试、真实 `.anycurve` 文件操作；系统剪贴板等未测项保留 | [指定提交 CI](GITHUB_CI-0.18.md) |
| 0.17 | [完整验收](VALIDATION-0.17.md)：首轮失败、Chaos 标签／模式快照修复和各轮原始记录；短音后保持不冒充原生完整名义周期测试 | [指定提交 CI](GITHUB_CI-0.17.md) |
| 0.16 | [完整验收](VALIDATION-0.16.md)：首轮四个 Processor 失败、生产参数与 fixture 修复及定向复测 | [GitHub 整理全量复测](ctest-github-handoff-0.16.txt)及[明细](test-details-github-handoff-0.16.txt) |

历史报告不重写为本轮结果；冻结的 **0.20.1 及更早** ZIP、包内文档和校验值保持原样。本文后续表格是检查标准，除非第 1 节或对应版本报告给出证据，否则不能勾为已执行。

## 2. 准备与构建

需要 Windows x64、Visual Studio 2022 C++ Build Tools、Windows SDK、CMake ≥ 3.22、Git；源码打包、独立音频分析和发布包校验需要 Python 3，脚本仅使用标准库。JUCE 固定为 8.0.12，提交 `29396c22c93392d6738e021b83196283d6e4d850`。发布前同时核对版本、提交和未提交改动，不要用一个工作目录的新源码解释另一个旧二进制的结果。

下列命令在**仓库根目录的 PowerShell**运行，不依赖某位开发者的磁盘路径。先关闭用于上一次验证的 Anyvoc 进程，避免 Windows 锁住正在替换的二进制；不要关闭用户尚未保存的工作。手动测试使用单独的音色库和测试工程，先备份用户设置及音色。

最快的完整构建与测试入口：

```powershell
./scripts/build.ps1
```

脚本会在缺少 JUCE 时获取固定版本，配置 Visual Studio 2022 x64，构建 Release，再执行 CTest。脚本内部会对每一步非零退出码报错。需要逐步记录时使用下面的等价命令；该方式要求 JUCE 已存在：

```powershell
$ErrorActionPreference = 'Stop'
$repoRoot = (Get-Location).Path
$runDir = Join-Path $repoRoot ('build/acceptance/0.20.2-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
if (!(Select-String -LiteralPath CMakeLists.txt -Pattern '^project\(Anyvoc VERSION 0\.20\.2 ' -Quiet)) {
    throw 'Check out the intended 0.20.2 commit before validation.'
}
git rev-parse HEAD | Set-Content (Join-Path $runDir 'commit.txt')
git status --short | Set-Content (Join-Path $runDir 'worktree.txt')
cmake -S . -B build -G 'Visual Studio 17 2022' -A x64 -DANYVOC_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
cmake --build build --config Release --parallel 4 2>&1 |
    Tee-Object -FilePath (Join-Path $runDir 'build.txt')
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
ctest --test-dir build -C Release --output-on-failure --output-junit (Join-Path $runDir 'ctest.xml') 2>&1 |
    Tee-Object -FilePath (Join-Path $runDir 'ctest.txt')
$testExit = $LASTEXITCODE
Copy-Item -LiteralPath build/Testing/Temporary/LastTest.log -Destination (Join-Path $runDir 'test-details.txt')
if ($testExit -ne 0) { throw 'CTest failed; retain the failed log before retrying' }
```

**通过标准：**构建退出码 0；CTest 四个目标全部通过；0.20.2 的 75／129／10／6 组分别汇总零失败，共 220 组。本轮已达到该标准，实际日志见第 1 节；后续代码改动须验证对应新构建，不能沿用本轮结果。测试组数只能随有评审的测试变更调整，不能删除失败项保持绿色。保留警告，区分既有与新增警告。排查单套失败可运行 `ctest --test-dir build -C Release -R '^ProcessorTests$' --output-on-failure`，但须注明定向复测并保留初始失败日志。

## 3. 可重现预览与 WAV 演示

处理器测试程序支持以下入口。它是控制台程序，可使用 PowerShell 的调用运算符 `&`；每次检查退出码。输出放在本次验收目录，避免覆盖已发布的证据。

```powershell
$processorTests = Join-Path $repoRoot 'build/AnyvocProcessorTests_artefacts/Release/AnyvocProcessorTests.exe'
& $processorTests --preview (Join-Path $runDir 'images')
if ($LASTEXITCODE -ne 0) { throw 'Preview generation failed' }
& $processorTests --lfo-motion-demo (Join-Path $runDir 'Anyvoc-v0.20.2-Motion.wav')
if ($LASTEXITCODE -ne 0) { throw 'LFO motion render failed' }
& $processorTests --chaos-demo (Join-Path $runDir 'Anyvoc-v0.20.2-Chaos.wav')
if ($LASTEXITCODE -ne 0) { throw 'Chaos render failed' }
& $processorTests --macro-demo (Join-Path $runDir 'Anyvoc-v0.20.2-Macro.wav')
if ($LASTEXITCODE -ne 0) { throw 'Macro render failed' }
& $processorTests --demo (Join-Path $runDir 'Anyvoc-v0.20.2-Legacy.wav')
if ($LASTEXITCODE -ne 0) { throw 'Legacy render failed' }
```

其他入口同样接受一个 WAV 输出路径：

| 参数 | 长度 | 演示内容 |
|---|---:|---|
| `--demo` | 15 秒 | Pad、Sample、Granular 各 5 秒；旧音频兼容基线 |
| `--warp-demo` | 12 秒 | 原声、单槽、双槽、双槽调制 |
| `--voicing-demo` | 12 秒 | Poly、Mono、Legato、Legato + 180 ms 滑音 |
| `--crossmod-demo` | 15 秒 | 原声、FM、PM、AM、RM；B 不作为可听直达声源 |
| `--matrix-demo` | 15 秒 | LFO→Cutoff；AUX Macro 1 从 0% 至 100%，最后改变响应曲线 |
| `--sources-demo` | 16 秒 | Prism Weave Macro 5 和 Four Horizons Macro 8 的 0%/100% 对比 |
| `--envelopes-demo` | 20 秒 | Clockwork Bloom 的节拍/曲率，以及 Second Breath 的连奏重触发 |
| `--lfo-motion-demo` | 20 秒 | 0–4 秒直拍、4–8 秒 Swing 60%、8–12 秒 Swing + Reverse；12–16 秒 110 Hz、16–20 秒 220 Hz |
| `--chaos-demo` | 20 秒 | 0–10 秒 Lorenz、10–20 秒 Rossler；相同 C4、力度、Trigger、名义速度 2、声音与固定增益，仅更换算法 |
| `--macro-demo` | 20 秒 | 四段各 5 秒：固定 Macro1；增加 LFO1→Macro1；再增加 LFO2→Pan、受调 Macro1 作 AUX；最后加入 Macro1/8 互调并改用 Macro8 作 Pan AUX |

**预览通过标准：**当前版本应生成 92 张 PNG，以本次日志核对；至少回归检查 `Play.png`／`Play-small.png`，以及矩阵、详情与上下文菜单布局。正常 1120×760、最小 1008×684 下，PLAY 三张卡、完整页面入口、矩阵 COPY／EDIT／X、宏 EV、bank、琴键与输出不得裁切。保留其他页、Curve Tools、Chaos、Swing／Motion／Audio／Phase Routing 的回归检查；运行读数必须来自真实音频快照。记录具体查看的图片，不能把全部生成写成全部人工检查，截图也不替代拖动、焦点、文件对话框或 MIDI。

Motion／Chaos／Macro 沿用 `Anyvoc-v0.16-Lfo-Motion-Demo.wav`、`Anyvoc-v0.17-Chaos-Demo.wav`、`Anyvoc-v0.19-Macro-Demo.wav` 的包内名称，三个文件内容均须由 **0.20.2 程序重新渲染**。上面在独立 `$runDir` 生成本轮四份 WAV。本次四份实际新渲染已与冻结 0.20.1 ZIP 逐字节一致，Legacy 另与真实旧 EXE 新渲染一致，独立宏分析也通过，证据见第 1 节；输出不得覆盖 0.20.1 及更早证据或发行目录。

**音频通过标准：**命令退出 0，长度/声道/采样率/位深符合约定，无意外静音或 PCM 满幅样本。三个必需演示均为 960,000 帧、48 kHz、24-bit、双声道。Chaos 两段各 10 秒，第 9 秒松键；宏演示四段各 5 秒，每段第 4 秒松键；均用 20 ms 淡入、100 ms 淡出。宏四段使用同一 Prism Handle 基础、C4、力度 0.75、保存 Macro1=0.4 和 master=0.42，固定增益，不逐段归一化。第 0/1 段只增加一条 LFO1 输入路由；后两段更改了明确的路由组合，不能称为单变量对照。LFO1 为 Trigger Triangle 0.4 Hz、Unipolar、Amount 0.4；Pan 路由的 LFO2 为 1.2 Hz、Amount 0.65；末段 Macro8 base=0.1，Macro1→8 为 0.5、Macro8→1 为 0.2。

渲染测试检查编码前浮点 NaN/Inf；整数 PCM 不含 NaN/Inf，独立文件分析不能替代此项检查。无 PCM 满幅样本也不能排除上游限幅。听音记录写明设备、音量和听到的差异，不把 RMS 差异写成“音质更好”。

旧行为要求逐字节一致时，明确区分“真实旧程序新渲染”和“冻结发行包的原文件”。本轮可使用下述任一可追溯来源，但必须分别记录，不能混称；当前版本必须实际重新渲染。若声音变化是明确修复，记录差异原因与受影响预设，不能盲目更新基线消除失败。

本轮有真实留存的 `build/validation-baseline/AnyvocProcessorTests-v0.20.1.exe`，SHA-256 为 `be79d16b9359880b05a7c690a702741ba201dfe1b39c2a376b1acbb1ae0018de`，同名 `.json` 记录其来源。也可使用冻结 `dist/Anyvoc-0.20.1-Windows-x64.zip` 的四份 WAV；须先核对原外置报告，再只读提取并比较。不能把其他版本或当前程序改名充当基线。

先令 `$renderDir = $runDir`，执行[构建指南](BUILDING.md)第 3.2 节的 ZIP 核验、提取与四文件比较；该段会生成 `$referenceDir`，指向本次目录下的 `reference-0.20.1`。没有可追溯旧 ZIP／报告时记为未测，不能借用旧版通过记录。随后独立分析本轮文件：

```powershell
python scripts/analyze-chaos-demo.py `
    --version 0.20.2 `
    --demo (Join-Path $runDir 'Anyvoc-v0.20.2-Chaos.wav') `
    --legacy-before (Join-Path $referenceDir 'Anyvoc-Demo.wav') `
    --legacy-after (Join-Path $runDir 'Anyvoc-v0.20.2-Legacy.wav') `
    --output (Join-Path $runDir 'chaos-audio-analysis-0.20.2.json')
if ($LASTEXITCODE -ne 0) { throw 'Independent Chaos analysis failed' }
python scripts/analyze-macro-demo.py `
    --version 0.20.2 --baseline-version 0.20.1 `
    --demo (Join-Path $runDir 'Anyvoc-v0.20.2-Macro.wav') `
    --legacy-before (Join-Path $referenceDir 'Anyvoc-Demo.wav') `
    --legacy-after (Join-Path $runDir 'Anyvoc-v0.20.2-Legacy.wav') `
    --output (Join-Path $runDir 'audio-analysis-0.20.2.json')
if ($LASTEXITCODE -ne 0) { throw 'Independent macro analysis failed' }
```

上面 `--legacy-before` 是冻结包成员；若改用留存 0.20.1 EXE 的新渲染，按[构建指南](BUILDING.md)第 3.2 节核对程序哈希并另存报告，仍显式指定 `--version 0.20.2 --baseline-version 0.20.1`。本轮两种来源都已实际比较通过，见第 1 节；两种来源仍不能互相改写，其他提交须按实际新渲染重新验证。

**分析通过标准：**两脚本退出 0，两个 JSON 的 `status` 为 `PASS`；格式与完整 PCM 长度正确；各段 RMS 大于 0.001、各规定对照的解码样本不完全相同、无达到 PCM 满幅的样本；旧演示逐字节及 SHA-256 一致。宏脚本只复用通用 WAV 解码、RMS 和哈希运算，不导入合成器代码或生成期望音频；它能独立说明文件中的信号和差异，不能单靠段落不同证明逐样宏反馈算法正确。算法与接线由独立 Engine/Processor 参考测试另行核验。

## 4. VST3：pluginval

pluginval 是外部工具，不属于合成器构建依赖。把它放在 `tools/pluginval/pluginval.exe`，或将下方变量改为自己的工具位置，并记录工具版本。Windows 下使用 **`Start-Process -Wait -PassThru`** 启动，不能只通过 `& pluginval.exe` 返回后的 `$LASTEXITCODE` 判定完成；还要检查验证进程产出的完整日志。

```powershell
$validator = Join-Path $repoRoot 'tools/pluginval/pluginval.exe'
$bundle = Join-Path $repoRoot 'build/Anyvoc_artefacts/Release/VST3/Anyvoc.vst3'
$validationDir = Join-Path $runDir 'pluginval'
New-Item -ItemType Directory -Force -Path $validationDir | Out-Null
$validatorArgs = '--strictness-level 5 --validate "{0}"' -f $bundle
$validationProcess = Start-Process -FilePath $validator -ArgumentList $validatorArgs `
    -WindowStyle Hidden -Wait -PassThru `
    -RedirectStandardOutput (Join-Path $validationDir 'pluginval.txt') `
    -RedirectStandardError (Join-Path $validationDir 'pluginval-stderr.txt')
$validationProcess.ExitCode
Get-Content -LiteralPath (Join-Path $validationDir 'pluginval.txt')
Get-Content -LiteralPath (Join-Path $validationDir 'pluginval-stderr.txt')
```

**通过标准：**进程正常结束、退出码 0，完整验证日志明确显示预期 Anyvoc 版本、strictness 5、正确模块路径，最后为 `SUCCESS`，没有失败或超时；保存随机种子与日志。无日志、空日志、仅启动成功或只写出插件名称均为“未完成”，不是通过。失败时保留随机种子复现。不要通过排除失败测试或降低 strictness 消除问题。若另用 Steinberg SDK validator，应单独记工具版本和结果；未运行就不能声称通过。

## 5. 独立版人工验收

运行 `build/Anyvoc_artefacts/Release/Standalone/Anyvoc.exe`，或解压发布包后运行 `Anyvoc.exe`。以下是**执行清单**，不是对所有步骤已完成的声明。每一行记录 PASS / FAIL / 未测及证据。

| ID | 操作 | 预期 / 失败判定 |
|---|---|---|
| S01 启动与设备 | 新开测试实例，核对版本；选择实际输出设备和 48 kHz / 256 样本缓冲，记录驱动类型 | 窗口可用、设备成功打开；无崩溃、设备错误循环或永久静音 |
| S02 声音 | INIT 后点屏幕键盘 C4，松开；依次试听全部 28 个工厂预设，包含 Prism Handle / Breathing Prism；适度音量下实际听音 | 0.19 的 28 个预设及顺序保留；电平与扬声器/耳机均有输出，松键按包络结束，无卡音；仅看到电平不能勾“听音通过” |
| S03 MIDI | 选择物理/虚拟 MIDI 输入；单音、和弦、力度、弯音、CC1、延音踏板；松开全部音符和踏板 | 音高/力度/路由响应正确；弯音约 ±2 半音；无残留卡音。无 MIDI 设备则该项未测 |
| S04 调音与历史 | 改 Cutoff 一次、一次连续旋钮拖动、Undo、Redo；INIT 后 Undo | 数值与声音恢复；一个拖动是一个撤销步骤；INIT 可撤销。另测 Ctrl 快捷键并单独记录，不能由鼠标按钮通过推断 |
| S05 自绘曲线 | MOD→LFO1 EDIT→Custom，连续画一笔；选曲线预置；逐步 Undo/Redo | 整笔/整个预置分别一次撤销，33 点及类型正确恢复；切到 LFO10 不串改 LFO1 |
| S06 包络图 | 分别编辑 Amp / Filter / Env3 / Env4；拖 Attack、Decay/Sustain、曲率；开 BPM 与 Legato 重触发 | 图、数值和声音一致；Decay/Sustain 联动一次撤销；BPM 模式编辑分割并保留原秒值 |
| S07 调制路由 | 拖 LFO 至 Cutoff；改正负 Amount、旁通、AUX Macro、曲线；第 4 页编辑路由32；移动/清除后 Undo | 旁通保留配置，AUX=0 可关闭深度，移动保留整行；Undo 恢复整条配置，无越界、误覆盖 |
| S08 新 LFO 功能 | Swing Carousel 改 Swing/Direction；Phase Lantern 改 Phase、三种 Rate Mode；从 MOD SOURCES 拖到 Phase | 曲线读数/声音随设置变化；切换模式保留先前速度；Random 禁用 Phase/Direction 并保留值；Undo 能恢复 |
| S09 保存与重开 | SAVE 到新 `.anyvoc`；改音色；LOAD；关闭测试实例再启动并 LOAD | 参数、名称、波表、样本、自绘曲线、路由一致；每次保存使用测试文件，不覆盖唯一原件 |
| S10 导入与嵌入 | 导入已知 WAV 为波表和样本，编辑后另存；把测试输入 WAV 移到另一测试目录，再重新 LOAD | 保存的音色仍可恢复内嵌素材；错误/不支持文件给出错误且不损坏当前音色。当前样本限制：≤20 秒、8–192 kHz、单/双声道 |
| S11 窗口 | PLAY／SYNTH／FILTER／MOD／FX／Browser 与各 Lab；最小 1008×684、默认 1120×760，再放大；测试缩放/DPI | 所有按钮可达、标签可读，无遮挡、空白、控件串页；截图记录系统 DPI |
| S12 设备切换与重启 | 停音后切输出设备/采样率/缓冲，重新发音；退出后重开 | 无崩溃、卡音或旧设备残留占用；记录是否需要手动重选设备，不把自动恢复未实测行为写成保证 |

已知历史记录涉及原生 Ctrl 键递送以及间歇性的琶音器延音 RMS 问题；0.16 没有宣称专门修复。验收遇到类似问题应保留复现步骤，不以“以前出现过”忽略。人工检查结束后恢复原设置；如果做了字节备份，核对恢复后的哈希。

### 0.20.2 ARP 所有权专项

新七组采用手写生成 MIDI，经 **ARP 关闭的第二个处理器**渲染独立参考；不调用生产候选排序或调度 helper 生成预期。旧逻辑前五组已实际失败，后续修复结果与初始日志分别保留。自动注入覆盖行为不等于真实外接设备通过；这些是检查标准，状态仍以第 1 节为准。

| 项目 | 必须检查的行为 |
|---|---|
| 重复键／零力度／多余松键 | 同通道同音高两次有效 NoteOn 需两次匹配 NoteOff；仅占一个候选，后续步使用最新力度。零力度 NoteOn 等价 Off，多余或其他通道的 Off 不提前释放。 |
| 排序与输出身份 | 不同通道的同音高各占一个候选；按 pitch→channel 排序，八度扩展到 127 时保留力度和来源通道；生成 On/Off 均用原通道。 |
| 踏板与 CC121 | 16 路踏板互不影响；CC64 只保持候选、不延长每个生成 Gate。CC121 清本通道踏板／锁存，保留按键计数，并原样转发引擎轮控复位。 |
| 定向停止与同采样事件 | CC120／123 只清本通道键和锁存，保留踏板值；当前输出属于该通道才清其记录。其他通道时钟继续；同采样先按原顺序处理全部输入，再发 Gate-off／step-on。 |
| 普通 Gate／空池 | 普通松键移除候选但池仍非空时，当前脉冲走完原 Gate；最后候选消失则立即发送普通 NoteOff，允许有限 Release。再次入池从首步开始。 |
| 采样率／分块／生命周期 | 手写不同采样率的分数步进时刻并逐样比对；不同块大小应一致。全局 panic、prepare、ARP 开关、声音恢复／切换、transport stop 清所有运行时状态；Poly／Mono／Legato 切换保留 ARP 候选与时钟，下一步继续。 |

严格区分通道 CC120／123 与全局 PANIC；当前 CC123 沿用引擎“清本通道锁存并进入 Release”的合同，不另称其为遵循踏板的替代策略。候选数组固定容量不代表整个 `processArp` 零分配：既有 `MidiBuffer` 交换与扩容成本尚未专门验收。该修复也不证明所有历史间歇性卡音已经解决。

### 继承的 0.20.1 ARP 控制器复位回归

记录事件由真实硬件、虚拟 MIDI 端口还是自动测试注入；三者不能互相冒充。固定音色，关闭 Delay／Reverb 并给 Amp Release 一个有限值；先确认开启 ARP 的普通按键与踏板能正常工作。这里检查 CC121，而不是 PANIC 或 All Sound Off。

| ID | 操作 | 预期 / 失败判定 |
|---|---|---|
| A01 踏板锁存释放 | Note On C4 → CC64=127 → Note Off C4 → 在记录的时间发送 CC121=0 | 仅由踏板维持的音符进入正常 release 并结束，不无限琶音，也不硬切掉包络尾音；与同一位置 CC64=0 的参考比较。保存的音色参数不变。 |
| A02 仍按住的键 | C4 与 G4 先后按下，踩踏板、松 C4、保持 G4，再发 CC121；随后松 G4 | C4 的锁存解除，仍按住的 G4 继续演奏；之后松 G4 会释放。分别检查 Poly／Mono／Legato。 |
| A03 后续踏板周期 | 完成复位后，再次踩下踏板，按下／松开新音，最后放开踏板 | 新一轮踏板仍能保持和释放，不因复位永久关闭，也不因旧锁存状态留下卡音。 |
| A04 轮控转发 | 设置弯音及 CC1，保持琴键，发送 CC121；另用中心弯音、CC1=0、CC64=0 的显式事件序列作参考 | 引擎收到轮控复位，按住的音不被清掉；自动回归对固定序列逐样比较，实测另记录输出和事件。 |

自动回归还覆盖 44.1／48／96 kHz、127／257／512 样本块及块中非零事件位置。这三组属于 0.20.1 已有回归，已随本轮 220 组重新通过；历史结果与当前执行分别归档。该修复只针对已复现的 CC121 路径，不能据此宣布所有间歇性卡音已经解决。

### PLAY、COPY 与按目标清空回归

以下是逐项记录的操作标准；[PLAY 原生记录](ui-play-native-0.20.json)和 [COPY 原生记录](native-copy-0.20.json)仅证明 0.20 当时的限定范围，不证明当前 0.20.2 已执行表中各项。尤其全字段、满表、最后页与宿主中断不能从这两场原生操作推断通过。

| ID | 操作 | 预期／失败判定 |
|---|---|---|
| P01 入门与完整页 | 启动后查看 PLAY，逐个进入 SYNTH／FILTER／MOD／FX 再返回 | 默认 PLAY；切页不改参数或增加 Undo，全部深度编辑页仍可达；底部宏、琴键与 MASTER 保留 |
| P02 Tone 与历史 | 改 PLAY Cutoff，去 FILTER 对照，再 Undo；关闭 TONE 后尝试拖 Cutoff／Resonance，重新启用 | 使用同一 Filter A 参数和历史；旁通保留值且禁用无效编辑，不修改 Filter B 或声源路由 |
| P03 包络与来源 | PLAY NOTE SHAPE→EDIT，打开 BPM 后返回；开 MOD SOURCES 拖 LFO 到共振或宏 | 时间显示分割并禁止秒值编辑，Sustain 仍可调；有效投放关闭覆盖面板并留在 PLAY，外圈与 MOD 行一致；ADSR／Echo／Reverb 不是新增目标 |
| P04 布局与音色入口 | 默认／最小窗口按 C4，CHOOSE A SOUND 打开 Browser；另行加载测试预设 | 无裁切；单选不加载，双击／Enter／LOAD SOUND 才加载；将打开浏览器、加载、绿色电平、实际听音分别记录 |
| R01 三入口复制 | 主矩阵 COPY，详情 COPY，旋钮 Route 子菜单 Copy route and edit copy | 第一完全空行获得独立副本，全部 19 字段一致、原行不变、不移位不去重；主列表定位副本，详情／菜单打开副本详情；一次 Undo／Redo |
| R02 草稿与满表 | 为 Off／None／旁通／Amount0 行保存非默认隐藏字段，复制它；占满 32 行再复制 | 草稿可复制且不可当空槽覆盖；只有全部 19 默认的行才空；纯默认来源和满表无写入、无 Undo。存在活动手势时提示先完成 |
| R03 声音与独立性 | 复制已启用 LFO→Cutoff，改副本 Amount，再单独旁通副本 | 启用副本立即参与调制，可能加强原效果；修改不影响原行。复制不是保存到系统剪贴板，也不是尚未应用的草稿 |
| R04 取消和菜单迟到 | 打开菜单后改路由、换页或关闭 editor；宿主在复制 begin/value/end 改来源／候选空行或恢复状态 | 旧操作停止，不换槽重试、不覆盖宿主新值、无悬挂手势。中断可能已有部分写入，应提示检查当前路由，不能断言失败自动回滚 |
| R05 按目标清空 | 同一目标设两条活动源行（含旁通），另存 Source Off 草稿；菜单 Remove all，再 Undo | 活动源匹配行的全部 19 字段清默认；旁通也清，Off 草稿及其他目标保留；整次仅一步 Undo。无匹配时无历史 |
| R06 发布中途 | 用自动回归在第二／最后一行通知中处理音频，另测宿主改未匹配行 | 音频在组合发布未完成时保留旧完整图，结束后接收实际最终图；宿主变化中止后续写入，不重新扩展删除集合；原生音色听感不能代替该并发检查 |

### 宏目标专项人工用例

以下是本轮逐项记录的检查清单；历史通过不自动延续。Macro1–8 既可作源/AUX，也可作为矩阵目标；保存旋钮是 base，`EV` 是最新活动声部实际使用的有效值，UI 按刷新频率显示快照，不逐音频样本显示。复音下不同声部可以有不同值，一个 EV 标签不能代表所有声部。

| ID | 操作 | 预期 / 失败判定 |
|---|---|---|
| M01 目标与两组旋钮 | INIT 后打开 MOD／MOD SOURCES，把 LFO1 拖到 Macro1，再切 5–8 bank 拖到 Macro8；检查矩阵下拉框及外圈 | Macro1–8 目标仍排在原 28 项之后；八个宏均可接收、编辑正负深度。切 bank 只改视图，不改 base、路由或 Undo；隐藏 bank 的迟到拖动不新增路由 |
| M02 保存值与有效值 | Macro1 base=0.25，Velocity→Macro1 Amount=0.25；用满力度音符持音，再改力度；将 Macro1 路由到 Cutoff | 稳定满力度时 Macro1 EV 约 0.5、base 仍 0.25；较低力度的声部可不同。声部输入改变声音但不写回旋钮、宿主自动化或声音历史；松键进入 Release 时仍可显示活动声部 |
| M03 AUX 与截断 | 用受调 Macro1 作 LFO2→Pan 的 AUX；把入宏深度改为正/负，分别跨过 0 与 1；旁通入宏路由 | 主源和 AUX 都读取有效值；EV 始终在 0–1。旁通/Source Off 后该入路不继续反馈，所有有效入路关闭时回到平滑 base；不要把 Amount 仍在平滑中的短暂变化误判为立即失效 |
| M04 自调制与互调制 | Macro1 base=0.2，Macro1→Macro1 Amount=0.5；另测 Macro1→Macro8、Macro8→Macro1，含 AUX 和曲线 | 自调制稳定值应接近 0.4，运行有限且可通过旁通/Panic 停止；互调制按上一样本输入作用，不是矩阵行内递归。声音变化及稳定性另记，不能只凭 EV 图证明每样本算法 |
| M05 生命周期与清空 | 对比 Poly 新音、Mono 重触发、Legato 重叠/松键回退、延音踏板与尾音；Panic、换预设、重新开音频设备 | 新声部/Mono 重触发清输入历史，Legato 重叠/回退保持；Release/踏板仍按活动声部处理。Panic 或换预设立即显示当前 base/idle，不短暂冒出旧有效值；无活动声部时 `EV: idle base` |
| M06 路由历史与满表 | 在第 1/32 行设宏目标、AUX、曲线和旁通；拖行移动、清除、Undo/Redo；另将 32 行填满再尝试新增 | 整行 19 字段及隐藏的旧目标选择保持；一次动作一次撤销。满表不覆盖已有路由，旁通但占用的行不当空位；重复同源/同目标的旁通路由重新启用并保留其配置 |
| M07 实际快照与布局 | 持音比较 Prism Handle 与 Breathing Prism；两组宏来回切换，先后按两个不同力度音；在默认/最小窗口查看 EV、外圈和矩阵 | 八标签来自同一次实际引擎快照，显示最新活动声部而不是本地模拟。保存值保持，所有标签/按钮/琴键可读可达；截图与电平不等于实际听音通过 |
| M08 保存与宿主边界 | 用宏目标、互调制、AUX 和旧 Phase 目标保存 `.anyvoc` 并重开；在 DAW 再做自动化、复制实例与工程重开 | 恢复 base 与完整路由；运行时的反馈输入/声部 ID 不作为音色存档接续。旧参数映射不变，宿主相关写入不会被旧 UI 操作覆盖；按完整发布协议另测移动中的音频块，不从方法返回后的比较推断并发性 |

没有物理/虚拟 MIDI 输入时，精确力度、踏板和重叠音用例应记未测或交由明确记录的宿主 MIDI 工程完成。自动测试已覆盖全部八宏的独立常数声音参考、32 行容量、回调和可见控件路径；它不能代替上述原生、外部 MIDI、听音和 DAW 操作。

### 曲线工具专项人工用例

以下是保留的曲线工具操作标准；0.18 的有限原生记录见第 1 节历史部分，不能据此把 0.20.2 整行勾选为通过。先备份用户设置，在测试实例打开 LFO Lab→TOOLS，至少检查 LFO1 与 LFO10；使用独立临时 `.anycurve` / `.anyvoc` 文件。固定 33 点、选区与文件的完整定义见 [LFO_CURVE_TOOLS.md](LFO_CURVE_TOOLS.md)。

| ID | 操作 | 预期 / 失败判定 |
|---|---|---|
| T01 查看与独立状态 | 两个 LFO 分别改 Draw/Select、X/Y 网格、Snap 和选区；切换后再返回；暂存位移/缩放但不按 APPLY | 不改变声音参数或新增 Undo；每条 LFO 的视图设置互不串改。视图设置不属于音色存档，关闭编辑器后不保证保留 |
| T02 网格绘制 | 分别开关 X/Y 吸附，覆盖 1/2/4/8/16/32；在半格和半格两侧落笔，连续拖一笔，Undo/Redo | 各轴独立；半格取较大坐标。X 从连续鼠标位置选最近格，grid4 的点坐标 3.6→0、4→8；完整一笔一次 Undo，隐藏/切页后迟到拖动不继续写 |
| T03 点值精度 | Y16 取目标 0.125、Y32 取 0.0625；另用 SET 输入 0.12345678 | 原有声音参数步长为 **0.01**，分别实际约为 0.13、0.06、0.12；网格或精确输入不绕过宿主量化。SET 不受网格吸附影响，修改单点一次 Undo |
| T04 选区与反转 | 对非对称曲线选择 9–17 号点，分别 Flip X / Flip Y 后 Undo；再 All | 闭区间包含两端；X 反转顺序、Y 反号，不能用对称直线代替区分用例；Custom 选区外点完全不变，单次 Flip 一次 Undo |
| T05 位移与缩放 | 在 Linear/Step 中分别对选区设置 X 位移、宽度、Y 位移、高度，再 APPLY；含单点选区和越界读位 | X 单位为一个真实点，范围 −32…32；宽度 0.25…4、高度 0…4、Y −2…2。固定选区掩码，超出源范围保持端点、值限于 ±1，无循环回绕；每次 APPLY 一次 Undo |
| T06 身份与形状 | Basic Sine 先做身份 APPLY，再 Copy→Paste；Random 保存的曲线也做 Copy→Paste；已经 Custom 再重复同资产 | 身份 Flip/SET/APPLY 不改变点或插值时不改形状、不新增历史。Paste/Load 是明确资产替换，Basic/Random 即使 33 点相同也切 Custom，Undo 恢复原形状和隐藏点。已 Custom 且量化后同值不增加 Undo |
| T07 系统剪贴板 | 用真实 Copy/Paste 跨 LFO、跨实例；暂存并恢复原剪贴板内容；粘贴坏 XML | 转移完整 33 点与插值，不受当前选区限制，不转移速度/路由/其他 LFO；Copy 不改声音。无效内容有错误提示、声音和历史不变。平台模拟测试不能代替此项 |
| T08 原生曲线文件 | 用原生 SAVE/LOAD 保存到新 `.anycurve`，再加载；另试取消、损坏/超限文件和明确不可写的测试目标；导入任意 float 后重新导出 | 保存不改声音，加载整条曲线选 Custom；重新导出实际已量化的点值。文件最多 16 KiB，失败不部分写入、不损坏旧文件；取消无声音改动。只使用测试文件，不覆盖唯一原件 |
| T09 迟到回复与禁用 | 打开 LOAD 后改变音色/相关参数、切 LFO、隐藏 TOOLS 或关闭编辑器，再返回选择结果；切 Chaos 尝试操作 | 旧请求不覆盖新状态或访问已销毁界面。Chaos 禁止绘图、点编辑、替换；切回 Custom 保留点值。原生窗口模态阻止的路径记不适用，自动回调测试覆盖情况另记 |
| T10 声音、存档与布局 | 将自绘 LFO 路由到 Cutoff/Pan，执行一笔+选区变换+Undo/Redo；保存 `.anyvoc`，重开加载；默认/最小窗口检查 TOOLS | 参数与可听变化对应，音色保存恢复点、插值、形状和路由；工具设置不要求恢复。图、选区、全部按钮、错误提示及琴键可用；电平或截图不代替真人听音 |

手动步骤中若平台输入、对话框或剪贴板无法实际完成，记录“未测”及原因，不把可控测试服务的成功换算成原生通过。记录实际读回的点值；不要因 0.125→0.13 等已约定的宿主量化而悄然扩展参数范围或更新旧 schema。

### Chaos 专项人工用例

从 INIT 或指定工厂预设开始，记录 LFO 序号、算法、播放模式与速度。至少覆盖 LFO1 和 LFO10；自动测试负责全部十条源路径，但不能替代真实窗口的操作记录。

| ID | 操作 | 预期 / 失败判定 |
|---|---|---|
| C01 算法与声音 | 打开 LFO Lab，在同一持续音与同一路由上依次选 Lorenz、Rossler；试听 Motion / Butterfly Current 与 Texture / Spiral Drift | 统一 Shape 共 8 项；两算法能持续改变调制和声音，图显示实际 XYZ；不是播放固定循环波形或静态装饰图 |
| C02 Free 连续性 | Free 下保持音频处理，松键/再按键；在 DAW 中另测播放头跳转 | 演化不因 MIDI 或播放头跳转复位；松键后若音频仍处理，Free 轨迹可继续变化，不能误判为假动画 |
| C03 Trigger 与连奏 | Trigger 下释放至声部结束后重按同音；另测 Mono 重触发、Legato 重叠音与松开最新音后的回退 | 同一 LFO 新声部从确定初态开始；Mono 重触发会重置，Legato 重叠及回退保持旧轨迹。不同 LFO 的初态略不同，不要求彼此相同 |
| C04 Envelope 与切换 | Envelope、速度 2 下持续按键超过 0.5 秒，再继续保持；改算法或 Free/Trigger/Envelope 模式；全松键后再换算法 | 一个名义周期后 XYZ 与图轨迹保持；算法/模式切换重新初始化，下一次处理/发声显示新状态；不得把旧算法的闲置快照标成新算法数据 |
| C05 速度与同步 | 在同一 Chaos 路由上比较 Free Hz、Tempo、Audio Rate；改 BPM、分割、Follow Swing 与 Swing Amount，再切回 | 速度改变演化快慢；三个模式保留各自参数；Sync 优先于 Audio Rate，Follow Swing 仅 Tempo 下有效。速度不是固定重复周期，快设定也不保证实时负载足够 |
| C06 禁用与保存值 | 先在周期形状设 Phase/Direction 并画 Custom；切 Chaos，尝试拖 Phase、修改 Direction、绘图和五个曲线预置，再切回 Custom | Chaos 中上述编辑禁用、Phase 拒绝源拖放，既有 Phase 调制不影响 Chaos；保存的 Phase/Direction/33 点仍在，返回周期/Custom 后恢复可编辑 |
| C07 真实轨迹快照 | 持音截取两帧，记录 XYZ；Envelope 到达保持点后再等数秒并多次截图/缩放；确认停止音频处理时也检查 | 演化时读数和轨迹会更新；保持或音频处理确实停止后，不因重绘、缩放、等待而制造新运动。自动测试另将 UI 属性与引擎 XYZ 快照逐轴比较 |
| C08 撤销与重开 | 从已绘制 Custom 切 Lorenz、再切 Rossler，逐步 Undo/Redo；另存 `.anyvoc`，关实例重开并加载；DAW 再做 D05 | 每次算法选择一次撤销，隐藏的旧 Shape/Kind/Custom 点不丢失；加载恢复算法、速度、模式、路由及保存值。运行中的混沌积分位置不是音色存档，不要求加载后接续同一瞬间 |

出现轨迹冻结时，先确认是 Envelope 已完成、音频停止，还是持续处理的 Free/Trigger 意外冻结；将原因和实际 XYZ 一并写入记录。不能只凭两张图片“看起来不同”认定独立 ODE 数值验证通过。

## 6. DAW 验收矩阵

| 宿主 / 平台 | 当前状态 | 下一步 |
|---|---|---|
| FL Studio，Windows x64 | 未完成扫描/加载/工程重开；早期用户目录扫描未发现插件 | 安装完整 bundle 到宿主支持的 VST3 路径，重新扫描，执行 D01–D09 |
| REAPER，Windows x64 | 未验证 | 配置 VST3 扫描路径，扫描后执行 D01–D09 |
| Ableton Live，Windows x64 | 未验证 | 启用 VST3 插件扫描，执行 D01–D09 |
| Cubase / Studio One / Bitwig，Windows x64 | 未验证 | 各宿主分别记录准确版本并执行 D01–D09，不能相互代替 |
| Logic / macOS | 当前不支持 | 未提供 macOS/AU 产物；不适用 Windows VST3 测试 |
| Pro Tools / AAX | 当前不支持 | 未提供 AAX 产物 |

安装时复制**整个** `Anyvoc.vst3` 文件夹，保留 `Contents/x86_64-win`。FL Studio 的具体菜单因版本而异，可按 [用户指南的安装步骤](USER_GUIDE.md#在-fl-studio-里使用)。系统级目录可能需用户确认管理员权限。记录实际扫描路径，不把目录存在当成宿主已经识别。

| ID | 操作 | 通过标准 |
|---|---|---|
| D01 扫描 | 清楚记录安装路径、扫描结果、插件标识/版本 | 被识别为立体声乐器；无黑名单、超时或扫描崩溃 |
| D02 发声 | 新工程插入实例；钢琴卷帘写音符与和弦，播放/停止 | 能听到且有输出；无卡音，编辑器可反复开关 |
| D03 节拍 | Tempo 模式下改 BPM，暂停/启动/循环/跳转播放头 | Follow Swing 和 LFO 模式符合约定；Free 同步位置与 Trigger/Envelope 的逐音触发区别可确认；无异常重触发 |
| D04 自动化 | 录制/绘制 Cutoff、Macro base、Phase、Rate Mode、`lfoNChaos`、LFO Point，以及 `modNMacroEnabled/Target` 自动化；编辑时触发宿主写入，隐藏/显示 UI | 声音和显示跟随；0.19 全部 1256 参数映射保持；宏有效值不写回 base 自动化，宿主写入不会被过时 UI/文件回复覆盖 |
| D05 工程状态 | 使用导入素材、Custom LFO、包络和路由；保存工程，完全关闭宿主，重新打开 | 音色名称、参数、嵌入素材、自动化及声音恢复；原素材路径失效后仍可恢复已嵌入内容 |
| D06 多实例 | 至少两个不同音色，复制实例，独立修改/Undo | 实例不串音色或历史；一个实例关闭不影响另一个 |
| D07 渲染 | 同一 MIDI 工程实时与离线导出，记录尾音、随机参数设置和增益 | 无静音/漏音/截断异常；关闭随机性并设置确定触发时再做严格波形比较，有随机性不强求逐字节相同 |
| D08 配置 | 44.1/48/96 kHz 与 64/128/256/512 样本中设备支持的组合，开关旁通/换预设 | 无崩溃/非有限值/卡音；不足以实时运行的组合记实际掉音与负载，不能当通过 |
| D09 MIDI/界面 | 力度、弯音、CC1、延音、缩放、键盘焦点、关闭重开 UI | MIDI 功能与焦点可用，宿主快捷键和插件快捷键行为明确；记录冲突 |

每个宿主独立填写结果。缺工具、无许可证、无硬件或未执行记为“未测”，不要写“预计兼容”。

## 7. 性能与发布门槛

**CPU 必须测量，不能设一个未经测量的“低于 10% 就通过”。**记录 CPU 型号/核心数、电源模式、操作系统、音频设备/驱动、采样率、缓冲、宿主版本、实例数、复音、Unison、颗粒密度、Warp、调制/效果配置及 UI 是否打开。用固定 MIDI 和同一音色分别测单音、16 复音、三振荡器/高 Unison、颗粒、32 路调制、Audio Rate/相位反馈等代表性负载。

建议预热后每项持续演奏至少 60 秒、重复三次，报告平均值、峰值、掉音/underrun 次数和测量工具。宿主 DSP 百分比与任务管理器 CPU 百分比要分开记录。音频回调时间若有测量，报告最坏块；块期限为 `缓冲样本数 / 采样率`，例如 48 kHz、256 样本约 5.33 ms。平均离线渲染时间不能证明所有音频块都按时完成。

Chaos 的可复现离线基准入口如下。它不属于当前 75 组 EngineTests，退出 0 只说明基准完成且测试音频未意外静音，没有内置“实时性能通过”阈值。[0.17 基准记录](benchmark-chaos-0.17.txt)仅对应当时产物，不能作为本轮新测量。

```powershell
$engineTests = Join-Path $repoRoot 'build/AnyvocEngineTests_artefacts/Release/AnyvocEngineTests.exe'
& $engineTests --benchmark-chaos 2>&1 |
    Tee-Object -FilePath (Join-Path $runDir 'benchmark-chaos.txt')
if ($LASTEXITCODE -ne 0) { throw 'Chaos benchmark failed to complete' }
```

基准固定 Release、48 kHz、256 样本、16 个错开发声的持续声部、10 个 LFO、一个波表振荡器、Unison 1、滤波/效果关闭。八种场景包括 Sine/Lorenz/Rossler 的 10 Hz Trigger、Sine 1000 Hz Trigger、Lorenz 1000 Hz Free、两种 Chaos 1000 Hz Trigger，以及无任何路由的 Rossler 1000 Hz Trigger。最后一项仍计算 160 个声部/LFO 状态，用于暴露“没有路由也有成本”的情况。每项预热后计算 2 秒音频、三轮轮换顺序，输出每轮耗时和中位数 `wall/audio`。

宏目标另有独立离线入口，同样不计入 75 组，也不以退出码 0 宣称实时负载已通过：

```powershell
& $engineTests --benchmark-macros 2>&1 |
    Tee-Object -FilePath (Join-Path $runDir 'benchmark-macros.txt')
if ($LASTEXITCODE -ne 0) { throw 'Macro benchmark failed to complete' }
```

宏基准使用 48 kHz、256 样本、16 个不同力度且错开发声的持续声部、32 行路由、单波表振荡器、Unison 1、关闭效果。四项分别为旧宏源路由、带主/AUX 曲线的宏反馈及 Phase 反馈、997 Hz LFO→宏→Pitch/Pan、997 Hz LFO→宏→双滤波；仅最后一项开启双滤波。每次计算 2 秒音频，共三轮轮换顺序，计时包含每块 `setSettings()`，不含构造与预热。保留每轮数值、中位数及 CHECKSUM；它不覆盖 UI/宿主调度、设备回调期限，也不证明并发路由编辑原子一致。

**记录标准：**保留全部场景、CHECKSUM、硬件与当时后台负载；比较同机同配置数据，不删掉最慢场景。`wall/audio < 1` 只代表整段离线吞吐快于音频时长，不能证明最坏回调准时、UI 开启时无掉音、16 声部以外负载可用，或某个 DAW 的 DSP 百分比。实际实时测试仍按上述至少 60 秒、三次和掉音计数执行。

发布前应预先声明目标硬件和可支持的工程负载，在该条件下验证无掉音、无持续爆音和无超时；若只有平均离线数据，只能发布该数据与限制，不能声称最坏实时性能已经通过。复杂音色混叠和音质仍需单独听音/频谱评价。

| 门槛 | 必须保留的证据 | 阻止相应发布声明的情况 |
|---|---|---|
| 版本一致 | 源码提交、构建日志、两个二进制 FileVersion 与 SHA-256 | 版本不符、混用旧模块、未构建的功能混入文档 |
| 回归 | 四套测试零失败、失败后的修复与复测记录 | 未解释失败、删除断言、无依据放宽容差 |
| 参数兼容 | 0.20.2 完整保留 0.20.1 的 **1256** 参数定义和顺序，不追加参数；其中前 1192 对应 0.18；旧 33 点仍为 **0.01** 步长 | 以工具精度为由改宿主范围、重排/重用 ID 或改变旧映射 |
| 状态兼容 | schema14 必需全部参数；schema1–13 缺少宏目标尾参数时补 false/0；未来 schema15 和损坏/重复/缺失当前字段原子拒绝；`.anyvoc`/工程保留整条 19 字段路由、素材与声音 | 损坏状态部分覆盖声音、旧内嵌素材丢失，或把 `.anycurve` 混同完整音色 |
| 工厂与默认声音 | 0.19 的 28 个工厂预设及序号不变；本轮四个 demo 需与冻结 **0.20.1 Windows ZIP** 的原始成员逐字节比较；可另用真实旧 EXE 新渲染 Legacy，保留各自来源及结果 | 静音、重排旧预设、用当前 WAV 假作旧基线或无说明更新基线 |
| ARP 所有权 | 七组独立手写 MIDI 音频参考、旧逻辑失败及修复后全套证据；跨通道踏板、重复键、局部控制器和时序／生命周期均符合[合同](ARPEGGIATOR.md) | 通道串扰、提前松键、Gate 被踏板吞掉、清错通道、取消后幽灵音或只用两个相同错误实现互相对比 |
| 宏目标与有效值 | 保留全部八宏独立声音参考、主/AUX 一致快照、生命周期、reset pending、路由与回调回归；原生与性能结果分开 | 用 UI 本地计算代替实际快照、EV 写回 base/历史、旧声部泄漏 |
| 路由发布／复制 | 19 字段默认空槽、Off/None/旁通草稿、满表、单步 Undo、来源／目的冲突、隐藏／销毁 UI；真实音频在复制／移动／批量删除／嵌套恢复的通知中运行 | 覆盖草稿、COPY 去重、半次组合编辑被音频采用、取消后继续覆盖宿主，或把仅操作结束后的 render 当并发证明 |
| PLAY | 默认进入、跨页参数与历史、Filter A 旁通、BPM 时间只读、调制投放留页、最小窗口和琴键 | 新建另一套声音状态、隐藏控件迟到写入、非目标错误接线或阻断完整页入口 |
| 曲线工具与资产 | 七组独立模型测试、六组真实控件集成测试；原生专项结果另记；Custom 替换语义、量化、一次 Undo、严格 ≤16 KiB XML 和失败保护 | 身份操作意外改声音、导入未选 Custom、迟到写入、未选点/其他 LFO 被改、解析失败部分覆盖、只用平台模拟宣称原生通过 |
| 新版扩展 | 新参数只能追加；新 schema 明确旧版本缺省值及未来版本拒绝规则 | 用加 choice 项目的方式悄然改变旧宿主自动化映射；未经实现/测试即改 schema |
| 模块规范 | 最终模块 pluginval strictness5 SUCCESS | 只检验较早二进制，或日志空白/失败/超时 |
| 音频/UI | 演示检查、最小尺寸、真实独立版操作；DAW范围另记 | 用截图替代交互，用电平替代听音，用 pluginval 替代 DAW |
| 发布包 | ZIP CRC、无重复/越界路径、精确文件集合、逐文件字节匹配、许可证、版本/hash、必需演示 | 漏文件、旧二进制、新源码混入旧版、损坏压缩包 |

构建、演示和文档冻结后才打包：

```powershell
# 使用第 3 节已经渲染并审核的本轮文件；这里只更新 dist 根级暂存名。
New-Item -ItemType Directory -Force -Path (Join-Path $repoRoot 'dist') | Out-Null
Copy-Item -LiteralPath (Join-Path $runDir 'Anyvoc-v0.20.2-Legacy.wav') -Destination (Join-Path $repoRoot 'dist/Anyvoc-Demo.wav') -Force
Copy-Item -LiteralPath (Join-Path $runDir 'Anyvoc-v0.20.2-Motion.wav') -Destination (Join-Path $repoRoot 'dist/Anyvoc-v0.16-Lfo-Motion-Demo.wav') -Force
Copy-Item -LiteralPath (Join-Path $runDir 'Anyvoc-v0.20.2-Chaos.wav') -Destination (Join-Path $repoRoot 'dist/Anyvoc-v0.17-Chaos-Demo.wav') -Force
Copy-Item -LiteralPath (Join-Path $runDir 'Anyvoc-v0.20.2-Macro.wav') -Destination (Join-Path $repoRoot 'dist/Anyvoc-v0.19-Macro-Demo.wav') -Force
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package.ps1
if ($LASTEXITCODE -ne 0) { throw 'Windows packaging failed' }
python ./scripts/package-source.py
if ($LASTEXITCODE -ne 0) { throw 'Source packaging failed' }
python ./scripts/verify-release.py
if ($LASTEXITCODE -ne 0) { throw 'Release package verification failed' }
```

Windows 打包脚本会检查二进制版本。0.20.2 必须包含 `Anyvoc-v0.16-Lfo-Motion-Demo.wav`、`Anyvoc-v0.17-Chaos-Demo.wav`、`Anyvoc-v0.19-Macro-Demo.wav` 三份演示；旧名字仅是资产约定，内容来自当前 0.20.2 重渲染。其他历史演示只在已有时收录。先完成音频检查，不能用空文件满足存在性要求。同版本打包会覆盖 ZIP 并更新输出目录；本轮使用独立 0.20.2 路径，既有候选需先归档避免残留。**已交付的 0.20.1 及更早发行目录、ZIP 和历史报告保持冻结**。本轮两包首轮逐文件校验已通过，最终状态以本版外置报告为准；输入更新后须重新打包复验。完整解压 Windows ZIP 后运行包根目录的 `Anyvoc.exe`。

[发布包校验脚本](../scripts/verify-release.py) 从 CMake 读取版本，核对两个 ZIP 的 CRC、Windows 安全且唯一的路径、精确文件集合与每个文件的原始字节；同时核对解压目录、两个产物与构建目录的四个 FileVersion、二进制哈希及三个必需演示的完整 20 秒/48 kHz/24-bit/双声道 PCM。源码包必须包含 `.gitattributes`、`.gitmodules`、`.github/`、Chaos 模型、`LfoCurveDocument.h/.cpp` 及固定 JUCE 源码/许可。脚本不替代 CTest、pluginval、听音或性能测试；未知将来版本不会自动获得某个新功能已验收的结论。

**包校验通过标准：**脚本退出 0，包外 `dist/Anyvoc-0.20.2-PackageVerification.json` 的 CRC、安全唯一路径、精确集合、逐文件匹配全部通过，版本一致，三个必需 demo 均有格式及哈希记录。本轮两包首轮校验已达到该标准；实际结果、文件数、大小与哈希以最终外置报告为准，不把 ZIP 哈希写入包内文档。报告不放入 ZIP，避免包含自身哈希。可单独运行 `python scripts/verify-release.py --self-test-paths` 检查路径逻辑，但这不是发行包验收。公开脚本可复现，不要求另写临时校验工具。

文档修改后重新打包会改变 ZIP 哈希；重新核对对应文件及包报告。若没有改变声音/生产代码，不需要为了文档重跑所有 DSP，但不得继续引用旧包哈希作为新包的校验值。

## 8. 验收与缺陷记录模板

复制到单独的验收记录，每次执行保留独立文件；敏感目录/用户名可在公开副本中脱敏，原始记录另存。

```text
日期/时区：
测试人员：
Anyvoc 版本 / Git commit / 工作区是否干净：
Standalone / VST3 文件版本、SHA-256：
Windows / CPU / 内存 / 电源模式 / DPI：
宿主与准确版本（独立版填 Standalone）：
声卡、驱动、采样率、缓冲、MIDI 设备：
音色/素材/MIDI 工程及其 hash：

检查 ID / 套件名：
状态：PASS / FAIL / 未测 / 不适用
完整命令或逐步操作：
预期结果：
实际结果（数值、可听现象、UI行为）：
退出码 / 失败组名 / pluginval seed：
日志、截图、WAV、工程文件位置：
可复现次数 / 总尝试次数：
性能数据与测量工具：
问题链接、影响范围、是否阻止发布：
修复 commit：
复测版本、步骤、结果、证据（保留首次失败记录）：
恢复用户设置/测试文件清理情况：
```

报错时优先给最小工程、最少音符和最短操作序列；保留崩溃日志与原始文件。对偶发错误记录触发频率，不通过反复重跑直到一次成功来宣称已经修复。
