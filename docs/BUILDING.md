# 构建、仓库结构与贡献

适用版本：**Anyvoc 0.17.0，Windows x64**。版本来自根目录 `CMakeLists.txt`。本地 Release 构建、四套共 175 组测试、pluginval strictness 5、独立 PCM 分析、旧声音对比及独立版操作检查已完成，具体证据与边界见 [VALIDATION.md](VALIDATION.md)。最终发行包以 `dist/Anyvoc-0.17.0-PackageVerification.json` 报告为准。0.16 的历史发布包与验证记录保留原样，不用 0.17 覆盖。

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
if (!(Select-String -LiteralPath CMakeLists.txt -Pattern '^project\(Anyvoc VERSION 0\.17\.0 ' -Quiet)) {
    throw 'Check out the intended 0.17.0 commit before building.'
}
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
```

上面从 `main` 获取源码。若版本检查失败，先核对实际提交并检出所需的 0.17.0 源码，不要仅修改版本号。用 `git rev-parse HEAD` 记录实际提交 SHA，后续验收与打包始终使用同一份源码；历史本地验证也不能代替新提交的构建测试。

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

先完成当前源码的构建和测试。在仓库根目录运行下列渲染命令，等待每个命令退出并检查退出码。新渲染结果不自动等于历史发布证据。

### 3.1 生成当前版本的音频与预览

```powershell
New-Item -ItemType Directory -Force -Path dist,build/acceptance | Out-Null
$processorTests = Resolve-Path build/AnyvocProcessorTests_artefacts/Release/AnyvocProcessorTests.exe
& $processorTests --demo dist/Anyvoc-Demo.wav
if ($LASTEXITCODE -ne 0) { throw 'Demo render failed' }
& $processorTests --lfo-motion-demo dist/Anyvoc-v0.16-Lfo-Motion-Demo.wav
if ($LASTEXITCODE -ne 0) { throw 'LFO motion demo render failed' }
& $processorTests --chaos-demo dist/Anyvoc-v0.17-Chaos-Demo.wav
if ($LASTEXITCODE -ne 0) { throw 'Chaos demo render failed' }
& $processorTests --preview build/acceptance/images
if ($LASTEXITCODE -ne 0) { throw 'Preview render failed' }
```

`--lfo-motion-demo` 和 `--chaos-demo` 都输出 20 秒、48 kHz、24-bit 立体声 PCM。Motion 文件沿用 0.16 的名字，以上命令会用当前 0.17 程序重新渲染它；不能把文件名当作旧引擎来源证据。Chaos 对比为前 10 秒 Lorenz、后 10 秒 Rossler，使用相同的音符、速度和增益条件。`--demo` 是原有 15 秒声音示例。

预览先写到 `build/acceptance/images`。人工检查正常/最小尺寸和实际轨迹后，再更新 `docs/images`，避免把尚未检查的新截图当成已验收证据。

### 3.2 用真实 0.16 程序重新生成旧声音参考

这一步需要**真正留存、来源可追溯的 0.16 `AnyvocProcessorTests.exe`**。本机留存文件名为 `build/validation-baseline/AnyvocProcessorTests-v0.16.exe`；该目录被 Git 忽略，首次克隆不会拥有它。缺少参考程序时，仍可运行当前测试、渲染、打包和包完整性校验，但必须将这项旧版音频对比记为“未执行”，不能把当前 EXE 改名、复制当前 WAV 或仅引用旧哈希代替。

确认留存程序的来源、版本与历史哈希后，下面的独立流程重新渲染前后两份 `--demo`。路径可按实际留存位置修改，不要覆盖 0.16 发行目录中的原音频：

```powershell
New-Item -ItemType Directory -Force -Path build/acceptance | Out-Null
$legacyTests = 'build/validation-baseline/AnyvocProcessorTests-v0.16.exe'
if (!(Test-Path -LiteralPath $legacyTests)) { throw 'A retained, verified 0.16 test binary is required for this comparison.' }
Get-FileHash -LiteralPath $legacyTests -Algorithm SHA256 |
    Format-List | Out-File -Encoding utf8 build/acceptance/legacy-renderer-sha256.txt
& $legacyTests --demo build/acceptance/Anyvoc-v0.16-before-v0.17.wav
if ($LASTEXITCODE -ne 0) { throw 'Retained 0.16 baseline render failed' }
$processorTests = Resolve-Path build/AnyvocProcessorTests_artefacts/Release/AnyvocProcessorTests.exe
& $processorTests --demo build/acceptance/Anyvoc-v0.17-legacy-demo.wav
if ($LASTEXITCODE -ne 0) { throw 'Current legacy demo render failed' }
python scripts/analyze-chaos-demo.py --demo dist/Anyvoc-v0.17-Chaos-Demo.wav --legacy-before build/acceptance/Anyvoc-v0.16-before-v0.17.wav --legacy-after build/acceptance/Anyvoc-v0.17-legacy-demo.wav --output build/acceptance/audio-analysis-0.17.json
if ($LASTEXITCODE -ne 0) { throw 'Chaos audio or legacy comparison failed; retain the report' }
```

分析参数：

| 参数 | 内容 |
| --- | --- |
| `--demo` | 已完成的 20 秒 Chaos 对比 WAV；默认 `dist/Anyvoc-v0.17-Chaos-Demo.wav` |
| `--legacy-before` | 用留存 0.16 测试程序新渲染的旧示例；默认 `build/validation-baseline/Anyvoc-v0.16-before-v0.17.wav` |
| `--legacy-after` | 当前 0.17 程序渲染的同一旧示例；默认 `build/validation-baseline/Anyvoc-v0.17-legacy-demo.wav` |
| `--output` | JSON 报告位置；默认 `docs/audio-analysis-0.17.json`，新验收优先显式写入 `build/acceptance`，审核后才归档 |

脚本独立解码 PCM，记录电平、峰值、分段数值差异和前后旧 WAV 的字节/SHA-256 一致性。它不进行主观听音；整数 PCM 的非有限值检查也不能替代编码前浮点音频的有限值测试。当前程序对比失败时保留报告、诊断原因，不通过改参考文件消除差异。

### 3.3 打包并校验最终输入

完成音频、预览和文档审核后，依次执行 Windows 打包、完整源码打包，再执行仓库内的校验脚本；不要让校验与仍在写 ZIP 的打包并行运行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package.ps1
if ($LASTEXITCODE -ne 0) { throw 'Windows package failed' }
python scripts/package-source.py
if ($LASTEXITCODE -ne 0) { throw 'Source package failed' }
python scripts/verify-release.py
if ($LASTEXITCODE -ne 0) { throw 'Release verification failed' }
```

`package.ps1` 读取 CMake 的版本并要求 EXE、VST3 文件版本一致；0.17 必须同时提供 Motion 和 Chaos 两份试听。更早的示例 WAV 只在已有时复制，首次克隆不要求找回所有历史音频。源码 ZIP 包含固定的 JUCE、许可、仓库配置和 `ChaosLfoModel.h`，不含 Git 元数据或构建缓存。

0.17 输出为：

- `dist/Anyvoc-0.17.0-Windows/`
- `dist/Anyvoc-0.17.0-Windows-x64.zip`
- `dist/Anyvoc-0.17.0-Source.zip`
- 校验全部通过后写出的外部报告 `dist/Anyvoc-0.17.0-PackageVerification.json`

`verify-release.py` 核对 ZIP CRC、安全且不重复的路径、精确文件集合、每个文件与当前输入的一致性、构建与打包二进制的版本/哈希、必需模型头文件，以及两份试听的 PCM 格式和完整长度。报告留在两个 ZIP 外，避免自引用哈希。无需再编写私有校验工具；`python scripts/verify-release.py --self-test-paths` 可单独检查路径校验器，但不检查发行包。

包校验不重新构建或运行 DSP/DAW 测试，也不检查声音是否好听。校验失败应先查对应输入或残留文件；校验后若又改变源码、文档、截图或试听，必须重新打包并再次校验。放行条件见[测试与验收](TESTING.md)，不能仅凭 ZIP 校验通过发布尚未通过测试的改动。

**同版本打包会覆盖同名 ZIP，并更新同名输出目录。** 重新打包已有 0.17 产物时，先归档之前的包，并确保目标目录不存在过期文件。0.16 的目录、ZIP 与历史校验报告不覆盖、不改写；本次使用独立的 0.17 路径。历史哈希只对应当时的文件，不能套用到新生成的包。

Git 默认忽略 `build/`、`dist/`、`tools/`。需要分发二进制时使用 GitHub Release 附件并同时提供对应源码、许可和校验值，不要把大型 EXE/WAV/ZIP 提交到源代码历史。

## 4. 代码在哪里

| 路径 | 职责 |
| --- | --- |
| `Source/SynthEngine.*` | 音频合成、复音、振荡器、滤波、效果、调制与 MIDI 行为 |
| `Source/PluginProcessor.*` | JUCE 插件接口、宿主参数、音色/工程状态、迁移、全局撤销 |
| `Source/PluginEditor.*` | 界面、曲线编辑、拖放、参数交互和运行显示 |
| `Source/EnvelopeModel.h`、`Source/LfoModel.h` | 包络及周期 LFO 的共享数学定义 |
| `Source/ChaosLfoModel.h` | Lorenz / Rossler 状态、积分、演化速度及归一化读数 |
| `Source/ModulationRoute.h`、`ModulationView.h` | 调制路由定义和显示计算 |
| `Source/WavetableDocument.*` | 波表文档及编辑/导出 |
| `Source/PresetLibrary.*` | 工厂/用户音色浏览和收藏 |
| `Tests/` | Engine、Processor、Wavetable、PresetLibrary 四套测试 |
| `docs/` | 现状、路线、验收、使用说明与历史原始证据 |
| `scripts/` | 构建、打包、安装，以及 `analyze-chaos-demo.py` / `verify-release.py` 可复现校验工具 |
| `third_party/JUCE` | 固定版本的外部依赖（子模块） |

## 5. 修改与提交约定

1. 从明确的提交开始，创建功能分支。一次变更同时描述用户可见行为、状态兼容影响和验证结果。
2. 当前版本宿主参数共 **1,192 个、状态 schema 13**：在 0.16 的 1,182 项之后追加十个 Chaos 选择，原 Shape / Kind 选项不扩展。保留已有参数 ID、顺序、范围、选项顺序、默认值与版本提示；新参数只追加。新状态格式需要旧版本迁移和损坏状态原子拒绝测试。
3. 当前工厂预设 **26 个**，Butterfly Current / Spiral Drift 追加在原 24 个之后。保持旧预设序号稳定。已修复的错误不必恢复错误音频，但必须说明行为差异，附可复现的前后证据。
4. 音频回调中不要做文件 IO、界面操作、阻塞锁或临时堆分配。新增声音算法应有独立参考、边界测试及资源开销记录。
5. UI 改动需检查正常/最小尺寸、隐藏控件、撤销粒度、宿主自动化回调重入、编辑器关闭后回调安全。旋钮可见不等于参数已经到达音频路径。
6. 提交前运行相关测试；合并/发布前四套全量测试必须通过。新增能力另按验收表检查，不能只报总数。
7. 使用 `git diff --check` 和 `git status` 检查提交范围，不加入用户音频、个人预设、凭据、缓存或运行时设置。子模块升级必须单独说明及重验。
8. PR/提交说明写清楚“改了什么、为什么、怎么测、还没测什么”。未完成工作留在功能分支，主分支保持可构建。

## 6. GitHub 自动检查的边界

[Windows workflow](../.github/workflows/windows.yml) 在推送 `main`、PR 或手动触发时，检出固定子模块，构建 Release 并运行四套 CTest。失败时仍上传 `windows-test-evidence`，其中包括 CTest 详细日志和 JUnit 结果。Actions 引用固定 commit，工作流只有读取仓库的权限。

本工作流**不**自动安装 pluginval、不播放电脑扬声器、不启动 FL Studio，也不发布发行版；因此绿色状态只证明该云端 Windows 环境的构建及自动测试通过。当前提交是否成功要检查实际 Actions run，不能以 YAML 已存在代替结果。

## 7. 报告问题时提供什么

提供提交 SHA/版本、Windows 版本、独立版或 DAW 名称/版本、采样率/缓冲、从 Init 或指定工厂预设开始的最短步骤、预期和实际结果、对应日志。涉及工程时先使用可公开的最小复现工程；不要上传不属于你的录音、商业音源或密码。

功能需求可引用[路线图](ROADMAP.md)的阶段 ID；测试失败可引用[验收指南](TESTING.md)的用例。未测标为“未测”，有明确失败标为“失败”，不以计划代替证据。
