# 构建、仓库结构与贡献

适用版本：**Anyvoc 0.19.0，Windows x64**。版本来自根目录 `CMakeLists.txt`。[完整构建](build-0.19-final.txt)后，[四套测试](ctest-0.19-final.txt)共 **201 组（75／110／10／6）全部通过，192.89 秒**，各套为 5.43／186.75／0.06／0.64 秒。随后仅将宏范围正文／提示两处文字替换为 ASCII `0-100%`，没有改变 DSP；[重新构建](build-0.19-text-fix.txt)后，[Processor 110 组补测](ctest-0.19-text-fix.txt)全部通过，测试程序用时 184.38 秒、CTest 总计 184.39 秒。最终 0.19.0 VST3 再次通过 [pluginval strictness 5](pluginval-0.19.txt)，seed `0x619c021`，SUCCESS。不能把补测的 110 组另加到 201 个不同测试组中。

[原生记录](native-0.19.json)涵盖 Prism Handle 的 LFO 1 → Macro 1（base 40%、EV 46.2%）、外圈 25%→40%→Undo 25%、Macro 1 → Macro 8（base 0%、EV 15.7%）、1008×684 布局，以及真实 SAVE→INIT→LOAD。保存的 `Native19-Macros.anyvoc` 为 59,175 字节、schema 14、1,256 参数；LOAD 后第 4／5 行宏接线恢复，C4 有绿色输出表，Macro 8 EV 再次为 15.7%。退出测试进程后原设置恢复且 SHA-256 一致。这些原生操作在两处文字修复前完成，未在最终文本构建重复；也不包含程序重启、外部资产恢复、OS 剪贴板、外接 MIDI、真人听音或 DAW。

最终文字修复版已 [重新渲染四份音频](render-0.19-text-fix.txt)，[独立音频分析](audio-analysis-0.19.json)重算为 PASS；四份 WAV 的 SHA-256 均与文字修复前相同。旧演示与留存 0.18 程序新渲染逐字节相同，Motion／Chaos 与 0.18 文件哈希一致。[90 张预览已重生成](preview-0.19-text-fix.txt)：新增六图中的 Macro-Live／Macro-Bank2-Live 各正常与最小尺寸四图在修复前检查，Macro-Routing 两图在修复后重新查看，`0-100%` 正常且无裁切；没有逐张人工复查其余 84 张。Windows **246 个文件**与完整源码 **4,638 个文件**的发行包已通过 CRC、安全唯一路径、精确集合、逐文件字节、交付时冻结文档、PE 0.19.0 版本和三份必需演示校验，**本机当前宏目标范围已交付**。最终包记录以外置 `dist/Anyvoc-0.19.0-PackageVerification.json` 为准；[0.19 验证记录](VALIDATION-0.19.md)说明全部范围。[0.19 云端 bf3efc6](GITHUB_CI-0.19.md)已完成干净构建与四套 201 组零失败，约 204.59 秒；这是交付后的仓库证据补充，原 0.19 ZIP 和包内文档保持该提交时的快照，未重打包，也未创建 GitHub Release 附件。

历史 [0.18 本机验收](VALIDATION-0.18.md)包含四套 188 组、161.94 秒及 pluginval、音频和限定原生证据；[0.18 云端记录](GITHUB_CI-0.18.md)对应 `45578ca`，四套 188 组通过。这些记录与 0.18 原包保持原样，不自动适用于 0.19。新功能的参数与音频语义见 [宏目标约定](MACRO_TARGETS.md)。

## 1. 准备开发环境

- Git，且可以访问 GitHub。
- Visual Studio 2022 或 Build Tools 2022，安装“使用 C++ 的桌面开发”、MSVC v143 和 Windows SDK。
- CMake 3.22 或更新版本，加入 PATH。已验证本机组合是 CMake 4.4.4、MSVC 19.44、Windows SDK 10.0.26100.0；其他组合需自行构建测试。
- PowerShell。Python 3 用于源码打包、WAV 分析和发行包校验；这些脚本仅使用 Python 标准库，校验 PE 文件版本时还会调用 Windows PowerShell。

不需要安装 Serum。JUCE 作为 Git 子模块固定到 8.0.12 的 `29396c22c93392d6738e021b83196283d6e4d850`，不要自行切换到最新分支后声称结果等同于本版。

```powershell
git clone --branch main --recurse-submodules https://github.com/Bad0RANG3/Anyvoc.git
cd Anyvoc
git submodule status
# 确认源码版本，避免误用本地旧提交或未来版本。
if (!(Select-String -LiteralPath CMakeLists.txt -Pattern '^project\(Anyvoc VERSION 0\.19\.0 ' -Quiet)) {
    throw 'Check out the intended 0.19.0 commit before building.'
}
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
```

上面从 `main` 获取源码；分支内容可能随后续提交变化。请使用所需的 0.19.0 提交，并保留版本检查；检查失败时核对实际提交，不要仅修改版本号。用 `git rev-parse HEAD` 记录实际提交 SHA，后续验收与打包始终使用同一份源码；历史本地验证也不能代替新提交的构建测试。

如果已经克隆但忘记子模块，运行 `git submodule update --init --recursive`。切换提交后也应重新同步子模块。完整对应源码 ZIP 已包含 JUCE 文件，不需要 Git 元数据；GitHub 自动生成的普通源码 ZIP **不包含子模块内容**，新手优先用上述克隆命令。

脚本执行以下三个步骤，任何步骤失败都会停止：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
```

若内存不足，可将 `--parallel 4` 改成 `--parallel 2`，无需改源码或测试。找不到 VS/SDK 时应补装工具链；不能通过关闭测试来认定构建完成。

## 2. 运行产物

| 内容 | 仓库内相对路径 |
| --- | --- |
| 独立程序 | `build/Anyvoc_artefacts/Release/Standalone/Anyvoc.exe` |
| 完整 VST3 插件目录 | `build/Anyvoc_artefacts/Release/VST3/Anyvoc.vst3` |
| VST3 二进制模块 | `build/Anyvoc_artefacts/Release/VST3/Anyvoc.vst3/Contents/x86_64-win/Anyvoc.vst3` |
| Processor 测试和离线渲染工具 | `build/AnyvocProcessorTests_artefacts/Release/AnyvocProcessorTests.exe` |
| 测试详细日志 | `build/Testing/Temporary/LastTest.log` |

独立版可直接双击，不需要管理员权限。第一次启动使用 Options / Audio settings 选择本机输出设备，再按底部琴键。需要 VST3 时复制**整个目录**；安装和 FL Studio 扫描说明见[使用手册](USER_GUIDE.md)。目前用户选择先用独立版，不能把此选择记录成 FL Studio 测试通过。

## 3. 生成试听、预览与发布包

下面是复现 0.19 音频、预览及打包的流程。重新构建时仍需先完成该份源码的完整测试，每个命令退出后核对退出码；失败时保留报告，不能以已有发布结果替代当前输入的验证。

### 3.1 生成当前版本的音频与预览

```powershell
$ErrorActionPreference = 'Stop'
$renderDir = Join-Path (Get-Location) ('build/acceptance/0.19-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force -Path $renderDir | Out-Null
$processorTests = Resolve-Path build/AnyvocProcessorTests_artefacts/Release/AnyvocProcessorTests.exe
Get-FileHash -LiteralPath $processorTests -Algorithm SHA256 |
    Format-List | Out-File -Encoding utf8 (Join-Path $renderDir 'current-renderer-sha256.txt')
& $processorTests --demo (Join-Path $renderDir 'Anyvoc-v0.19-legacy-demo.wav')
if ($LASTEXITCODE -ne 0) { throw 'Legacy demo render failed' }
& $processorTests --lfo-motion-demo (Join-Path $renderDir 'Anyvoc-v0.19-Lfo-Motion-Demo.wav')
if ($LASTEXITCODE -ne 0) { throw 'LFO motion demo render failed' }
& $processorTests --chaos-demo (Join-Path $renderDir 'Anyvoc-v0.19-Chaos-Demo.wav')
if ($LASTEXITCODE -ne 0) { throw 'Chaos demo render failed' }
& $processorTests --macro-demo (Join-Path $renderDir 'Anyvoc-v0.19-Macro-Demo.wav')
if ($LASTEXITCODE -ne 0) { throw 'Macro demo render failed' }
& $processorTests --preview (Join-Path $renderDir 'images')
if ($LASTEXITCODE -ne 0) { throw 'Preview render failed' }
```

本次输入均来自当前 **0.19** 测试程序，并用版本明确的验收文件名保存。`--demo` 为原有 15 秒示例；Motion、Chaos、Macro 各为 **20 秒、48 kHz、24-bit、双声道 PCM**。Chaos 前 10 秒为 Lorenz、后 10 秒为 Rossler；两段使用相同音符、速度和增益条件。

新增 `--macro-demo` 基于 Prism Handle，四段各 5 秒，使用相同 C4 音符、基础声源与固定增益：

| 时间 | 接线 |
| --- | --- |
| 0–5 秒 | Macro 1 基础值 0.4，通过三条出路控制截止、波表位置和 Warp |
| 5–10 秒 | 添加 LFO 1 → Macro 1，Trigger Triangle、0.4 Hz、Unipolar、Amount 0.4 |
| 10–15 秒 | 继续添加 LFO 2 → Pan，1.2 Hz、Amount 0.65，AUX 为有效 Macro 1 |
| 15–20 秒 | 追加 Macro 1 → Macro 8（base 0.1、Amount 0.5）及 Macro 8 → Macro 1（0.2），Pan AUX 改为 Macro 8 |

只有前两段是单条入路差异；后两段为明确列出的组合接线变化。不要将分段差异描述为每次只改一个参数，也不要据此推断主观音质或 Serum 等价。

预览应生成 **90 张**，新增 `Macro-Live`、`Macro-Bank2-Live`、`Macro-Routing` 各正常／最小尺寸，共六张。先查看本次 `$renderDir/images`，确认两个宏组、base／EV、latest voice／idle base、路由、琴键及最小窗口，再归档到 `docs/images`。数量与布局须由实际输出确认，不能仅因命令已写好就标为通过。

### 3.2 用真实 0.18 程序重新生成参考并分析宏演示

这一步需要**真正留存、来源可追溯的 0.18 `AnyvocProcessorTests.exe`**。本机路径为 `build/validation-baseline/AnyvocProcessorTests-v0.18.exe`；该目录被 Git 忽略，首次克隆不会拥有它。缺少参考程序时仍可测试及渲染当前版，但必须将旧版对比标为未执行，不能把当前程序改名、复制当前 WAV 或仅引用旧哈希代替。

本机已为本轮保留的参考命名是 `build/validation-baseline/Anyvoc-v0.18-before-v0.19.wav`；重新执行时以下命令在本次 `$renderDir` 写入同样明确的 before 文件名，并与 **0.19** 的 after 比较，不覆盖历史基线。记录两份渲染程序的来源与 SHA-256 后运行：

```powershell
$legacyTests = 'build/validation-baseline/AnyvocProcessorTests-v0.18.exe'
if (!(Test-Path -LiteralPath $legacyTests)) { throw 'A retained, verified 0.18 test binary is required for this comparison.' }
Get-FileHash -LiteralPath $legacyTests -Algorithm SHA256 |
    Format-List | Out-File -Encoding utf8 (Join-Path $renderDir 'legacy-renderer-sha256.txt')
& $legacyTests --demo (Join-Path $renderDir 'Anyvoc-v0.18-before-v0.19.wav')
if ($LASTEXITCODE -ne 0) { throw 'Retained 0.18 baseline render failed' }
python scripts/analyze-macro-demo.py `
    --demo (Join-Path $renderDir 'Anyvoc-v0.19-Macro-Demo.wav') `
    --legacy-before (Join-Path $renderDir 'Anyvoc-v0.18-before-v0.19.wav') `
    --legacy-after (Join-Path $renderDir 'Anyvoc-v0.19-legacy-demo.wav') `
    --output (Join-Path $renderDir 'audio-analysis-0.19.json')
if ($LASTEXITCODE -ne 0) { throw 'Macro audio or legacy comparison failed; retain the report' }
```

`analyze-macro-demo.py` 只使用 Python 标准库，复用 `analyze-chaos-demo.py` 的 PCM 数值解码辅助函数，不调用合成器生成或校正音频。四个路径参数均为必填，报告版本固定为 `0.19.0`，**没有 `--version` 参数**：

| 参数 | 本轮输入／输出 |
| --- | --- |
| `--demo` | 当前 0.19 程序完成的 20 秒 Macro WAV |
| `--legacy-before` | 留存 0.18 测试程序新渲染的旧 15 秒示例 |
| `--legacy-after` | 当前 0.19 程序渲染的同一旧示例 |
| `--output` | 独立的 `audio-analysis-0.19.json`；不是 0.18 报告 |

报告核对完整 PCM 格式、长度、各段电平、相邻段的数值差异和旧示例前后字节／SHA-256 一致性。若旧声音不一致或输入不完整，保留失败报告并调查，不更改参考来消除差异。整数 PCM 不含 NaN/Inf，不能因此证明编码前浮点缓冲有限；没有满幅样本也不能证明上游从未限幅。听音和最坏实时负载仍需分别检查。

如需同时记录当前版 Chaos 的独立分析，仍显式传入它的版本和本轮路径，避免使用脚本的 0.17 默认值：

```powershell
python scripts/analyze-chaos-demo.py `
    --version 0.19.0 `
    --demo (Join-Path $renderDir 'Anyvoc-v0.19-Chaos-Demo.wav') `
    --legacy-before (Join-Path $renderDir 'Anyvoc-v0.18-before-v0.19.wav') `
    --legacy-after (Join-Path $renderDir 'Anyvoc-v0.19-legacy-demo.wav') `
    --output (Join-Path $renderDir 'chaos-audio-analysis-0.19.json')
if ($LASTEXITCODE -ne 0) { throw 'Chaos audio or legacy comparison failed; retain the report' }
```

0.18 原验收使用的 `Anyvoc-v0.17-before-v0.18.wav` 和 `audio-analysis-0.18.json` 继续保留为历史记录，不能拿来代替本轮的 before／after。需要确认 Motion／Chaos 保持旧声音时，也应比较本轮重渲染结果与有来源记录的 0.18 文件，单凭旧文件存在不能声称本轮重渲染相同。

### 3.3 打包并校验最终输入

完整测试、音频、预览、文档和所需原生验收完成后，才将本次审阅过的音频放入 `dist` 根级暂存位置。打包脚本沿用 Motion 0.16、Chaos 0.17 的资产名称；下列映射明确把 **0.19 新渲染**复制到这些包内名字，不改变渲染来源，也不写入任何 0.18 或更早的发行目录／ZIP：

```powershell
New-Item -ItemType Directory -Force -Path dist | Out-Null
$releaseAudio = [ordered]@{
    'Anyvoc-v0.19-legacy-demo.wav' = 'Anyvoc-Demo.wav'
    'Anyvoc-v0.19-Lfo-Motion-Demo.wav' = 'Anyvoc-v0.16-Lfo-Motion-Demo.wav'
    'Anyvoc-v0.19-Chaos-Demo.wav' = 'Anyvoc-v0.17-Chaos-Demo.wav'
    'Anyvoc-v0.19-Macro-Demo.wav' = 'Anyvoc-v0.19-Macro-Demo.wav'
}
foreach ($entry in $releaseAudio.GetEnumerator()) {
    Copy-Item -LiteralPath (Join-Path $renderDir $entry.Key) -Destination (Join-Path 'dist' $entry.Value) -Force
}
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package.ps1
if ($LASTEXITCODE -ne 0) { throw 'Windows package failed' }
python scripts/package-source.py
if ($LASTEXITCODE -ne 0) { throw 'Source package failed' }
python scripts/verify-release.py
if ($LASTEXITCODE -ne 0) { throw 'Release verification failed' }
```

按顺序等待 Windows 包、源码包、校验各自完成，不能让校验与仍在写 ZIP 的打包并行运行。`package.ps1` 读取 CMake 版本，并要求 EXE／VST3 文件版本一致；0.19 必须提供 Motion、Chaos、Macro **三份**完整 20 秒试听。更早示例只在已有时复制，首次克隆不要求找回全部历史 WAV。

0.19 以下输出已生成并通过校验；本地独立版入口是 `dist/Anyvoc-0.19.0-Windows/Anyvoc.exe`：

- `dist/Anyvoc-0.19.0-Windows/`
- `dist/Anyvoc-0.19.0-Windows-x64.zip`
- `dist/Anyvoc-0.19.0-Source.zip`
- 外部权威校验报告 `dist/Anyvoc-0.19.0-PackageVerification.json`

源码 ZIP 包含固定 JUCE、许可、配置、`ChaosLfoModel.h`、`LfoCurveDocument.h/.cpp`、宏路由／引擎／测试及公开分析脚本，不含 Git 元数据或构建缓存。`verify-release.py` 核对 ZIP CRC、安全且不重复的路径、精确文件集合、每文件与最终输入的一致性、构建与包内二进制的版本／哈希、必需模型文件和三份试听的 PCM 格式／完整长度。报告放在 ZIP 外避免自引用；`python scripts/verify-release.py --self-test-paths` 只检查路径校验器，不等于校验发行包。

包校验不重新构建或运行 DSP／DAW 测试，也不检查声音是否好听。校验后若改变源码、文档、截图或试听，须重新打包和校验；不能仅凭 ZIP 完整就发布测试未通过的修改。

**同版本打包会覆盖同名 ZIP，并更新同名目录。** 重打已有 0.19 产物前先归档，确认没有过期残留文件；0.18 及更早发行目录、ZIP 和历史报告不覆盖、不改写。本轮只使用独立 0.19 路径。历史哈希不能套用到新包。

Git 默认忽略 `build/`、`dist/`、`tools/`。分发二进制时应提供对应源码、许可和校验值，不把大型 EXE／WAV／ZIP 提交到源代码历史。

## 4. 代码在哪里

| 路径 | 职责 |
| --- | --- |
| `Source/SynthEngine.*` | 音频合成、复音、振荡器、滤波、效果、调制与 MIDI 行为 |
| `Source/PluginProcessor.*` | JUCE 插件接口、宿主参数、音色/工程状态、迁移、全局撤销 |
| `Source/PluginEditor.*` | 界面、曲线编辑、拖放、参数交互和运行显示 |
| `Source/EnvelopeModel.h`、`Source/LfoModel.h` | 包络及周期 LFO 的共享数学定义 |
| `Source/ChaosLfoModel.h` | Lorenz / Rossler 状态、积分、演化速度及归一化读数 |
| `Source/LfoCurveDocument.*` | 离线 LFO 网格/选区变换、严格 `.anycurve` 编解码与文件替换 |
| `Source/ModulationRoute.h`、`ModulationView.h` | 调制路由定义和显示计算 |
| `Source/WavetableDocument.*` | 波表文档及编辑/导出 |
| `Source/PresetLibrary.*` | 工厂/用户音色浏览和收藏 |
| `Tests/` | Engine、Processor、Wavetable、PresetLibrary 四套测试 |
| `docs/` | 现状、路线、验收、使用说明与历史原始证据 |
| `scripts/` | 构建、打包、安装，以及 `analyze-macro-demo.py` / `analyze-chaos-demo.py` / `verify-release.py` 可复现校验工具 |
| `third_party/JUCE` | 固定版本的外部依赖（子模块） |

## 5. 修改与提交约定

1. 从明确的提交开始，创建功能分支。一次变更同时描述用户可见行为、状态兼容影响和验证结果。
2. 当前版本宿主参数共 **1,256 个、状态 schema 14**。完整保留 0.18 前 1,192 个参数定义、ID、顺序、范围、选项、默认值和版本提示，仅尾追加 32 × 2 个宏目标选择器，新增 VersionHint 14。旧 schema 1–13 缺字段补默认；当前 schema 缺字段、重复或非法数据必须原子拒绝。旧目标 0–27 不重排，Macro 1–8 为 28–35，LFO Phase 边界仍是 18–27。曲线工具仍为 33 点、0.01 步长，不因本版改变资产精度。
3. 工厂预设 **28 个**，Prism Handle／Breathing Prism 追加在原 26 个之后，旧顺序不变。已修复错误不必恢复错误音频，但必须解释差异并留下可复现前后证据。
4. 音频回调中不要做文件 IO、界面操作、阻塞锁或临时堆分配。新增声音算法应有独立参考、边界测试及资源开销记录。
5. UI 改动需检查正常/最小尺寸、隐藏控件、撤销粒度、宿主自动化回调重入、编辑器关闭后回调安全。曲线 Paste/Load 是整条资产替换，须选 Custom；已 Custom 且参数量化后同值才是无历史的重复导入。Flip/SET/APPLY 身份操作不改形状。测试平台服务只模拟系统剪贴板和原生选择器，真实按钮、事务、codec、磁盘 IO 仍执行；它不代表原生平台交互已验证。详见[专项合同](LFO_CURVE_TOOLS.md)。
6. 提交前运行相关测试；合并/发布前四套全量测试必须通过。新增能力另按验收表检查，不能只报总数。
7. 使用 `git diff --check` 和 `git status` 检查提交范围，不加入用户音频、个人预设、凭据、缓存或运行时设置。子模块升级必须单独说明及重验。
8. PR/提交说明写清楚“改了什么、为什么、怎么测、还没测什么”。未完成工作留在功能分支，主分支保持可构建。

宏实时显示只读引擎的八值一致快照及活动声部标志，基础旋钮和 APVTS 不能被 EV 反写。新声部清入量，Legato 保持，主来源／AUX 使用同一采样的有效值，反馈延迟一个采样；完整规则见 [MACRO_TARGETS.md](MACRO_TARGETS.md)。矩阵完整 19 字段需保留、撤销及迁移。完整纯行置换后，平滑量跟随匹配配置；Processor 的多字段移动仍逐参数发布，音频回调若恰好观察到中间状态，不能承诺任意并发移动逐位不变。原子发布是单独的后续增强，不将完成移动后再渲染的测试冒充并发原子性证明。

## 6. GitHub 自动检查的边界

[Windows workflow](../.github/workflows/windows.yml) 在推送 `main`、PR 或手动触发时，检出固定子模块，构建 Release 并运行四套 CTest。失败时仍上传 `windows-test-evidence`，其中包括 CTest 详细日志和 JUnit 结果。Actions 引用固定 commit，工作流只有读取仓库的权限。

本工作流**不**自动安装 pluginval、不播放电脑扬声器、不启动 FL Studio，也不发布发行版；因此绿色状态只证明该云端 Windows 环境的构建及自动测试通过。当前提交是否成功要检查实际 Actions run，不能以 YAML 已存在代替结果。

0.18 已有对应证据：[运行 44257671955](GITHUB_CI-0.18.md)检出 `45578ca2f770b21c99ec9aec59a3d9a0b0fc38e1`，构建成功，四套 188 组全部通过、零失败，测试合计约 130.98 秒。原始日志和 JUnit 保留下载字节。此为发布之后补充的仓库记录，未重新打包或覆盖既有 0.18 ZIP；原包仍保留发布时的文档与外置校验报告哈希。

0.19 的 [运行 44262489842](GITHUB_CI-0.19.md)对应 `bf3efc6ecde3044409ec6693c6b98f7eb65371b3`，干净构建与四套 201 组均通过，测试合计约 204.59 秒。JUnit 的 Engine／Processor 输出被截断，完整内部计数来自 LastTest.log；原始字节与哈希均已归档。此项是交付后的仓库补充，冻结的 0.19 ZIP 及包内文档保持该提交时的快照，没有重打包。本地发行包已校验不表示已有 GitHub Release 附件；目前没有创建这些附件。

## 7. 报告问题时提供什么

提供提交 SHA/版本、Windows 版本、独立版或 DAW 名称/版本、采样率/缓冲、从 Init 或指定工厂预设开始的最短步骤、预期和实际结果、对应日志。涉及工程时先使用可公开的最小复现工程；不要上传不属于你的录音、商业音源或密码。

功能需求可引用[路线图](ROADMAP.md)的阶段 ID；测试失败可引用[验收指南](TESTING.md)的用例。未测标为“未测”，有明确失败标为“失败”，不以计划代替证据。
