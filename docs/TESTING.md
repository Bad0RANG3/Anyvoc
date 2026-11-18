# Anyvoc 测试与验收指南

本指南针对 **0.16.0 / Windows x64 / VST3 与独立版**，说明怎样复现验证、怎样判定结果，以及怎样留下可追溯记录。0.17 混沌 LFO 尚未交付，不属于本次验收。实际版本证据见 [0.16 验证记录](VALIDATION-0.16.md)；操作说明见 [用户指南](USER_GUIDE.md)，开发环境、子模块与仓库构建方式见 [构建指南](BUILDING.md)。

“测试通过”只适用于记录中的版本、硬件、设置和步骤。自动化测试、pluginval、界面截图、真人听音、DAW 工程重开是不同层面的证据，不能互相替代，也不能证明与 Serum 2 声音等价。

## 1. 当前证据与结果

| 检查 | 0.16 结果 | 可核查证据 / 范围 |
|---|---|---|
| Release 构建 | 通过 | Windows x64；独立版和 VST3 文件版本均为 0.16.0；现存转换/弃用警告仍有记录 |
| EngineTests | 54 组通过 | [Engine 明细](engine-0.16.txt)，含独立 DSP 参考、块划分、相位反馈和高速目标 |
| ProcessorTests | 92 组通过 | [最终定向复测](ctest-processor-0.16.txt)，[92 组明细](test-details-0.16.txt) |
| WavetableTests | 10 组通过 | [首轮完整 CTest](ctest-0.16.txt) 中该套通过 |
| PresetLibraryTests | 6 组通过 | 同一首轮日志中该套通过 |
| 合计 | **162 组通过** | 54 + 92 + 10 + 6；不是 162 个 CTest 可执行程序，而是四个程序内部的测试组 |
| VST3 规范检查 | pluginval strictness 5：SUCCESS | [最终插件日志](pluginval-0.16.txt)，模块版本 0.16.0 |
| 旧演示一致性 | 通过 | 新旧二进制重新生成的 legacy WAV 逐字节相同，见版本验证记录 |
| 新演示格式及信号 | 通过 | [独立音频分析](audio-analysis-0.16.json)：20 秒、48 kHz、24-bit、双声道，无满幅 PCM 样本 |
| 预览与独立版操作 | 有限定范围的通过记录 | 74 张预览生成，8 张新增图人工检查；独立版操作范围及执行先后见版本记录 |
| FL Studio / 其他 DAW | **未验证** | 用户选择先用独立版；不能从 pluginval 推断宿主兼容 |
| 现场最坏负载 / 完整听音 | **未完成全面验收** | 需要按本文硬件与操作步骤另记结果；表头电平变化不等于已经听到扬声器声音 |

首轮完整 CTest 的退出结果确实为失败：Processor 有 4 项失败，其余三套通过。原因包括新 Phase/Swing 使用了带 0.01 步长的浮点构造器，以及新 UI 测试误操作隐藏行控件。生产参数已改为明确的连续范围，测试改为真实可见详情页控件，精确数值及一个 ULP 的冲突断言均保留。最终仅重跑受影响的 Processor 套件，**92/92 通过，108.01 秒**（CTest 总时间 108.02 秒）。初始日志没有被改写成“全通过”，也没有再次声称执行了一次最终完整四套运行。以后验证新候选版本应重新运行完整四套。

本次 GitHub 整理另于 2026-11-18 对已验证的 0.16 二进制重跑了**完整四套测试，全部通过，112.05 秒**。独立记录见 [GitHub 整理全量复测](ctest-github-handoff-0.16.txt)及[完整测试明细](test-details-github-handoff-0.16.txt)。这项新记录不改写上面的历史首轮失败与定向复测记录，也不是新的 DAW 或听音验证。

## 2. 准备与构建

需要 Windows x64、Visual Studio 2022 C++ Build Tools、Windows SDK、CMake ≥ 3.22、Git；源码打包需要 Python 3。JUCE 固定为 8.0.12，提交 `29396c22c93392d6738e021b83196283d6e4d850`。发布前同时核对版本、提交和未提交改动，不要用一个工作目录的新源码解释另一个旧二进制的结果。

下列命令在**仓库根目录的 PowerShell**运行，不依赖某位开发者的磁盘路径。先关闭用于上一次验证的 Anyvoc 进程，避免 Windows 锁住正在替换的二进制；不要关闭用户尚未保存的工作。手动测试使用单独的音色库和测试工程，先备份用户设置及音色。

最快的完整构建与测试入口：

```powershell
./scripts/build.ps1
```

脚本会在缺少 JUCE 时获取固定版本，配置 Visual Studio 2022 x64，构建 Release，再执行 CTest。脚本内部会对每一步非零退出码报错。需要逐步记录时使用下面的等价命令；该方式要求 JUCE 已存在：

```powershell
$ErrorActionPreference = 'Stop'
$repoRoot = (Get-Location).Path
$runDir = Join-Path $repoRoot ('build/acceptance/' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
git rev-parse HEAD | Set-Content (Join-Path $runDir 'commit.txt')
git status --short | Set-Content (Join-Path $runDir 'worktree.txt')
cmake -S . -B build -G 'Visual Studio 17 2022' -A x64 -DANYVOC_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
cmake --build build --config Release --parallel 4 2>&1 |
    Tee-Object -FilePath (Join-Path $runDir 'build.txt')
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
ctest --test-dir build -C Release --output-on-failure 2>&1 |
    Tee-Object -FilePath (Join-Path $runDir 'ctest.txt')
$testExit = $LASTEXITCODE
Copy-Item -LiteralPath build/Testing/Temporary/LastTest.log -Destination (Join-Path $runDir 'test-details.txt')
if ($testExit -ne 0) { throw 'CTest failed; retain the failed log before retrying' }
```

**通过标准：**构建退出码 0；CTest 四个目标全部通过；各程序汇总零失败。测试组数只能在有评审的测试变更后增加或调整，不能通过删掉失败项保持“绿色”。保留警告，区别既有警告与新增警告。排查单套失败可运行 `ctest --test-dir build -C Release -R '^ProcessorTests$' --output-on-failure`，但需注明“定向复测”，不覆盖初始失败日志。

## 3. 可重现预览与 WAV 演示

处理器测试程序支持以下入口。它是控制台程序，可使用 PowerShell 的调用运算符 `&`；每次检查退出码。输出放在本次验收目录，避免覆盖已发布的证据。

```powershell
$processorTests = Join-Path $repoRoot 'build/AnyvocProcessorTests_artefacts/Release/AnyvocProcessorTests.exe'
& $processorTests --preview (Join-Path $runDir 'images')
if ($LASTEXITCODE -ne 0) { throw 'Preview generation failed' }
& $processorTests --lfo-motion-demo (Join-Path $runDir 'Lfo-Motion.wav')
if ($LASTEXITCODE -ne 0) { throw 'LFO motion render failed' }
& $processorTests --demo (Join-Path $runDir 'Legacy.wav')
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

**预览通过标准：**0.16 生成 74 张 PNG；正常和最小窗口无裁切、重叠、缺失控件；禁用状态和显示值符合参数。重点逐张看 LFO-Swing、LFO-Motion、LFO-Audio、LFO-Phase-Routing 及其 `-small` 版本。截图只验证静态显示，拖动、焦点和 MIDI 要继续做真实窗口检查。

**音频通过标准：**命令退出 0，长度/声道/采样率/位深符合约定，无意外静音或满幅削波；在相同固定增益下比较各段。0.16 新演示预期为 960,000 帧、48 kHz、24-bit、双声道。渲染测试负责检查编码前浮点 NaN/Inf；整数 PCM 分析无法替代这项检查。听音记录写明设备、音量和听到的差异，不把 RMS 差异直接写成“音质更好”。

旧行为要求逐字节一致时，用留存的上一版**测试二进制**和候选版分别执行 `--demo`，比较 `Get-FileHash -Algorithm SHA256` 结果；不拿陈旧的 WAV 充当刚生成的基线。若声音变化是明确修复，记录差异原因与受影响预设，不能盲目更新基线消除失败。

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
| S02 声音 | INIT 后点屏幕键盘 C4，松开；依次试听工厂预设；适度音量下实际听音 | 电平与扬声器/耳机均有输出，松键按包络结束，无卡音；仅看到电平不能勾“听音通过” |
| S03 MIDI | 选择物理/虚拟 MIDI 输入；单音、和弦、力度、弯音、CC1、延音踏板；松开全部音符和踏板 | 音高/力度/路由响应正确；弯音约 ±2 半音；无残留卡音。无 MIDI 设备则该项未测 |
| S04 调音与历史 | 改 Cutoff 一次、一次连续旋钮拖动、Undo、Redo；INIT 后 Undo | 数值与声音恢复；一个拖动是一个撤销步骤；INIT 可撤销。另测 Ctrl 快捷键并单独记录，不能由鼠标按钮通过推断 |
| S05 自绘曲线 | MOD→LFO1 EDIT→Custom，连续画一笔；选曲线预置；逐步 Undo/Redo | 整笔/整个预置分别一次撤销，33 点及类型正确恢复；切到 LFO10 不串改 LFO1 |
| S06 包络图 | 分别编辑 Amp / Filter / Env3 / Env4；拖 Attack、Decay/Sustain、曲率；开 BPM 与 Legato 重触发 | 图、数值和声音一致；Decay/Sustain 联动一次撤销；BPM 模式编辑分割并保留原秒值 |
| S07 调制路由 | 拖 LFO 至 Cutoff；改正负 Amount、旁通、AUX Macro、曲线；第 4 页编辑路由32；移动/清除后 Undo | 旁通保留配置，AUX=0 可关闭深度，移动保留整行；Undo 恢复整条配置，无越界、误覆盖 |
| S08 新 LFO 功能 | Swing Carousel 改 Swing/Direction；Phase Lantern 改 Phase、三种 Rate Mode；从 SOURCES 拖到 Phase | 曲线读数/声音随设置变化；切换模式保留先前速度；Random 禁用 Phase/Direction 并保留值；Undo 能恢复 |
| S09 保存与重开 | SAVE 到新 `.anyvoc`；改音色；LOAD；关闭测试实例再启动并 LOAD | 参数、名称、波表、样本、自绘曲线、路由一致；每次保存使用测试文件，不覆盖唯一原件 |
| S10 导入与嵌入 | 导入已知 WAV 为波表和样本，编辑后另存；把测试输入 WAV 移到另一测试目录，再重新 LOAD | 保存的音色仍可恢复内嵌素材；错误/不支持文件给出错误且不损坏当前音色。当前样本限制：≤20 秒、8–192 kHz、单/双声道 |
| S11 窗口 | Synth/MOD/FX/Browser 与各 Lab；最小 1008×684、默认 1120×760，再放大；测试缩放/DPI | 所有按钮可达、标签可读，无遮挡、空白、控件串页；截图记录系统 DPI |
| S12 设备切换与重启 | 停音后切输出设备/采样率/缓冲，重新发音；退出后重开 | 无崩溃、卡音或旧设备残留占用；记录是否需要手动重选设备，不把自动恢复未实测行为写成保证 |

已知历史记录涉及原生 Ctrl 键递送以及间歇性的琶音器延音 RMS 问题；0.16 没有宣称专门修复。验收遇到类似问题应保留复现步骤，不以“以前出现过”忽略。人工检查结束后恢复原设置；如果做了字节备份，核对恢复后的哈希。

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
| D04 自动化 | 录制/绘制 Cutoff、Macro、Phase 与 Rate Mode 相关参数自动化；隐藏/显示 UI | 曲线控制声音，显示跟随；宿主写入不会被过时 UI 操作覆盖 |
| D05 工程状态 | 使用导入素材、Custom LFO、包络和路由；保存工程，完全关闭宿主，重新打开 | 音色名称、参数、嵌入素材、自动化及声音恢复；原素材路径失效后仍可恢复已嵌入内容 |
| D06 多实例 | 至少两个不同音色，复制实例，独立修改/Undo | 实例不串音色或历史；一个实例关闭不影响另一个 |
| D07 渲染 | 同一 MIDI 工程实时与离线导出，记录尾音、随机参数设置和增益 | 无静音/漏音/截断异常；关闭随机性并设置确定触发时再做严格波形比较，有随机性不强求逐字节相同 |
| D08 配置 | 44.1/48/96 kHz 与 64/128/256/512 样本中设备支持的组合，开关旁通/换预设 | 无崩溃/非有限值/卡音；不足以实时运行的组合记实际掉音与负载，不能当通过 |
| D09 MIDI/界面 | 力度、弯音、CC1、延音、缩放、键盘焦点、关闭重开 UI | MIDI 功能与焦点可用，宿主快捷键和插件快捷键行为明确；记录冲突 |

每个宿主独立填写结果。缺工具、无许可证、无硬件或未执行记为“未测”，不要写“预计兼容”。

## 7. 性能与发布门槛

**CPU 必须测量，不能设一个未经测量的“低于 10% 就通过”。**记录 CPU 型号/核心数、电源模式、操作系统、音频设备/驱动、采样率、缓冲、宿主版本、实例数、复音、Unison、颗粒密度、Warp、调制/效果配置及 UI 是否打开。用固定 MIDI 和同一音色分别测单音、16 复音、三振荡器/高 Unison、颗粒、32 路调制、Audio Rate/相位反馈等代表性负载。

建议预热后每项持续演奏至少 60 秒、重复三次，报告平均值、峰值、掉音/underrun 次数和测量工具。宿主 DSP 百分比与任务管理器 CPU 百分比要分开记录。音频回调时间若有测量，报告最坏块；块期限为 `缓冲样本数 / 采样率`，例如 48 kHz、256 样本约 5.33 ms。平均离线渲染时间不能证明所有音频块都按时完成。

发布前应预先声明目标硬件和可支持的工程负载，在该条件下验证无掉音、无持续爆音和无超时；若只有平均离线数据，只能发布该数据与限制，不能声称最坏实时性能已经通过。复杂音色混叠和音质仍需单独听音/频谱评价。

| 门槛 | 必须保留的证据 | 阻止相应发布声明的情况 |
|---|---|---|
| 版本一致 | 源码提交、构建日志、两个二进制 FileVersion 与 SHA-256 | 版本不符、混用旧模块、未构建的功能混入文档 |
| 回归 | 四套测试零失败、失败后的修复与复测记录 | 未解释失败、删除断言、无依据放宽容差 |
| 参数兼容 | 0.16 共 1182 参数；原 1066 参数前缀保持；116 尾参顺序/ID/范围/默认/VersionHint12 | 重排/重用旧 ID、改变旧 choice 的归一化含义、无迁移修改默认 |
| 状态兼容 | schema12；schema1–11迁移；DAW和`.anyvoc`往返；损坏/缺失/重复/非有限值原子拒绝 | 损坏状态部分覆盖当前声音、旧音色或内嵌素材丢失 |
| 新版扩展 | 新参数只能追加；新 schema 明确旧版本缺省值及未来版本拒绝规则 | 用加 choice 项目的方式悄然改变旧宿主自动化映射；未经实现/测试即改 schema |
| 模块规范 | 最终模块 pluginval strictness5 SUCCESS | 只检验较早二进制，或日志空白/失败/超时 |
| 音频/UI | 演示检查、最小尺寸、真实独立版操作；DAW范围另记 | 用截图替代交互，用电平替代听音，用 pluginval 替代 DAW |
| 发布包 | ZIP CRC、无重复/越界路径、精确文件集合、逐文件字节匹配、许可证、版本/hash、必需演示 | 漏文件、旧二进制、新源码混入旧版、损坏压缩包 |

构建、演示和文档冻结后才打包：

```powershell
./scripts/package.ps1
python ./scripts/package-source.py
if ($LASTEXITCODE -ne 0) { throw 'Source packaging failed' }
```

Windows 打包脚本会检查二进制版本，并要求当前版本 LFO motion 演示已存在。它还会收录既有演示，不能用空文件满足要求。发布验收报告应在打包结束后生成、放在包外，避免报告包含自身哈希。0.16 的包报告为 `dist/Anyvoc-0.16.0-PackageVerification.json`。临时 `build/validation-baseline` 下的本机分析/校验工具可能不随源码包分发；缺少它们时要实现并记录同等校验，不能假设别人可以运行该路径。

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
