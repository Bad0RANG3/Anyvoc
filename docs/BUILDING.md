# 构建、仓库结构与贡献

适用快照：Anyvoc 0.16.0，Windows x64。版本来自根目录 `CMakeLists.txt`。本次 GitHub 初始化提交不加入未完成的 0.17 混沌 LFO；已有发布包保持原样，新增文档与仓库配置不代表重新发布了安装包。

## 1. 准备开发环境

- Git，且可以访问 GitHub。
- Visual Studio 2022 或 Build Tools 2022，安装“使用 C++ 的桌面开发”、MSVC v143 和 Windows SDK。
- CMake 3.22 或更新版本，加入 PATH。已验证本机组合是 CMake 4.4.4、MSVC 19.44、Windows SDK 10.0.26100.0；其他组合需自行构建测试。
- PowerShell；只有生成对应源码 ZIP 时才需要 Python 3（仅用标准库）。

不需要安装 Serum。JUCE 作为 Git 子模块固定到 8.0.12 的 `29396c22c93392d6738e021b83196283d6e4d850`，不要自行切换到最新分支后声称结果等同于本版。

```powershell
git clone --recurse-submodules https://github.com/Bad0RANG3/Anyvoc.git
cd Anyvoc
git submodule status
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
```

如果已经克隆但忘记子模块，运行 `git submodule update --init --recursive`。完整对应源码 ZIP 已包含 JUCE 文件，不需要 Git 元数据；GitHub 自动生成的普通源码 ZIP **不包含子模块内容**，新手优先用上述克隆命令。

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

在仓库根目录运行，下列命令会生成当前源码的渲染结果；不会证明历史发布包与当前文件相同：

```powershell
New-Item -ItemType Directory -Force -Path dist,build/acceptance | Out-Null
$processorTests = Resolve-Path build/AnyvocProcessorTests_artefacts/Release/AnyvocProcessorTests.exe
& $processorTests --demo dist/Anyvoc-Demo.wav
if ($LASTEXITCODE -ne 0) { throw 'Demo render failed' }
& $processorTests --lfo-motion-demo dist/Anyvoc-v0.16-Lfo-Motion-Demo.wav
if ($LASTEXITCODE -ne 0) { throw 'LFO demo render failed' }
& $processorTests --preview build/acceptance/images
if ($LASTEXITCODE -ne 0) { throw 'Preview render failed' }
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package.ps1
if ($LASTEXITCODE -ne 0) { throw 'Windows package failed' }
python scripts/package-source.py
if ($LASTEXITCODE -ne 0) { throw 'Source package failed' }
```

`package.ps1` 读取 CMake 的版本并要求 EXE、VST3 文件版本一致，必须有当前 0.16 的 LFO motion 试听。历史示例 WAV 只在已有时复制；首次克隆不要求找回所有旧版音频。预览先写到 `build/acceptance/images`，人工看过后才更新 `docs/images`，避免把未检查的新截图当成旧版证据。

输出是 `dist/Anyvoc-0.16.0-Windows/`、`dist/Anyvoc-0.16.0-Windows-x64.zip` 和 `dist/Anyvoc-0.16.0-Source.zip`。源码 ZIP 包含 JUCE 及许可，不含 Git、构建缓存。打包前后应核对文件清单、版本和 SHA-256；详细放行条件见[测试与验收](TESTING.md)。

**同版本打包会覆盖同名 ZIP，并更新同名输出目录。** 正式发布前先归档已有包或升版本，再重新检查清单，防止旧输出目录残留文件。此仓库保留的 0.16 历史验证报告针对当时的包，不自动适用于你重新打包的结果。当前源码包脚本会包含新增的 GitHub 文档和配置，因此重新生成的 ZIP 哈希应当不同；不要修改历史哈希来伪装成原包。

Git 默认忽略 `build/`、`dist/`、`tools/`。需要分发二进制时使用 GitHub Release 附件并同时提供对应源码、许可和校验值，不要把大型 EXE/WAV/ZIP 提交到源代码历史。

## 4. 代码在哪里

| 路径 | 职责 |
| --- | --- |
| `Source/SynthEngine.*` | 音频合成、复音、振荡器、滤波、效果、调制与 MIDI 行为 |
| `Source/PluginProcessor.*` | JUCE 插件接口、宿主参数、音色/工程状态、迁移、全局撤销 |
| `Source/PluginEditor.*` | 界面、曲线编辑、拖放、参数交互和运行显示 |
| `Source/EnvelopeModel.h`、`LfoModel.h` | 包络及 LFO 的共享数学定义 |
| `Source/ModulationRoute.h`、`ModulationView.h` | 调制路由定义和显示计算 |
| `Source/WavetableDocument.*` | 波表文档及编辑/导出 |
| `Source/PresetLibrary.*` | 工厂/用户音色浏览和收藏 |
| `Tests/` | Engine、Processor、Wavetable、PresetLibrary 四套测试 |
| `docs/` | 现状、路线、验收、使用说明与历史原始证据 |
| `scripts/` | 构建、打包、安装工具 |
| `third_party/JUCE` | 固定版本的外部依赖（子模块） |

## 5. 修改与提交约定

1. 从明确的提交开始，创建功能分支。一次变更同时描述用户可见行为、状态兼容影响和验证结果。
2. 当前宿主参数共 1,182 个、状态 schema 12。保留已有参数 ID、顺序、范围、选项顺序、默认值与版本提示；新参数只追加。新状态格式需要旧版本迁移和损坏状态原子拒绝测试。
3. 保持旧预设序号稳定，追加新预设。已修复的错误不必恢复错误音频，但必须说明行为差异，附可复现的前后证据。
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
