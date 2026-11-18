# 真实设备回调测量

此开发工具在真实音频设备的回调中运行 `AnyvocAudioProcessor::processBlock()`，分别记录处理器耗时和探针回调体耗时。它补充离线吞吐基准，不是 DAW、真人听音或驱动掉音验收。

工具是 0.20.2 发行冻结后的开发补充。普通构建默认关闭，冻结的 0.20.2 Windows／源码 ZIP 不包含它；源码中的音源、参数和状态格式没有因此变化。

## 构建与短测

在仓库根目录的 PowerShell 中执行：

```powershell
cmake -S . -B build -G 'Visual Studio 17 2022' -A x64 -DANYVOC_BUILD_REALTIME_PROBE=ON
cmake --build build --config Release --target AnyvocRealtimeProbe --parallel 4
$probe = Join-Path (Get-Location) 'build/AnyvocRealtimeProbe_artefacts/Release/AnyvocRealtimeProbe.exe'
& $probe --help
& $probe --list
```

短测先明确使用设备默认值，报告中的 `actual` 才是本次实际配置：

```powershell
$short = Join-Path (Get-Location) 'build/realtime-short-01'
& $probe --case baseline --seconds 3 --warmup 1 --sample-rate 0 --buffer-size 0 --output $short
if ($LASTEXITCODE -ne 0) { throw 'Capture invalid; preserve the report and investigate' }
python scripts/analyze_realtime_probe.py --report "$short.json" --csv "$short.csv" --output "$short.analysis.json"
```

先查设备支持的采样率和缓冲，再做短测；不要用不支持的请求值代替实际值。正式比较应使用相同硬件、电源方案、设备和后台负载。该工具不打开编辑器，不申请音频输入，不启用外接 MIDI，不写独立版的用户设置，也不自动切换 ASIO 或独占模式。

探针在私有立体声缓冲中运行真实 DSP，并清零所有硬件输出通道。这样可以检查渲染信号的非静音、峰值和有限值，同时避免将压力测试音播放出来。不能通过把振荡器或 Master 音量调成零来代替静音输出，那会跳过待测路径。

## 结果怎样解释

- **处理器耗时**：只覆盖 `processBlock()`；包含真实参数读取、MIDI 与合成器处理。
- **计时种类**：使用高分辨率软件墙钟，包含测量区间内的线程抢占／等待。它不是 CPU 核心使用率，也不能把每个峰值都归因于 DSP 算法。
- **探针回调体耗时**：另含清硬件输出、生成固定 MIDI、扫描渲染样点和部分记录开销。它不覆盖 JUCE 管理器外层、驱动栈和声卡，不能称为完整系统回调耗时。
- **块期限**：使用该回调的实际样本数除以实际采样率。记录超过期限的块数，并保留最慢块；平均值不能代替峰值。
- **实际配置**：指 JUCE 客户端的处理采样率和回调块长。WASAPI 共享模式可能进行系统重采样；它们不是独立测得的端点混音格式、驱动原生周期或 DAC 时钟。
- **回调间隔**：软件入口时间戳的间隔。调度抖动或长间隔不能直接换算成已确认的硬件掉音。
- **原生 xrun**：固定 JUCE 8.0.12 的仅输出 WASAPI 设备返回 `-1`，表示不支持。此时不能报告“驱动掉音 0 次”。
- **manager 估计**：`AudioDeviceManager::getXRunCount()` 还包含其耗时测量器的超期限估计，须与原生驱动计数分开；其范围包含预热和回调注册期间，不能伪装成只覆盖正式样本的硬件计数。

实现依据是仓库固定的 JUCE：`juce_WASAPI_windows.cpp` 的设备 `getXRunCount()` 与空回调 context，以及 `juce_AudioDeviceManager.cpp` 的计时、计数和关闭时重置逻辑。移除回调后，先读取计数，再关闭设备，最后离线整理数据。

**测量有效**与**观察到的性能**分开判断。设备和配置固定、无容量溢出／提前终止、记录完整且实际渲染非静音，才形成有效数据；有效数据仍可包含大量超时，不能把它筛掉。最终输出的有限值检查也不能证明内部计算从未出现 NaN，因为引擎已有末端归零保护。

## 正式记录范围

正式场景应预热后持续至少 60 秒，重复三轮并轮换场景顺序，保留原始逐块记录、完整参数快照、请求／实际设备配置和工具身份。短测只验证工具和设备能工作，不作为正式性能结论。

将短测报告的实际配置明确传给批处理。下面以 `48,000 Hz / 480` 为命令示例，必须替换成你的设备确实支持的值：

```powershell
python scripts/run_realtime_probe.py --exe $probe --output build/realtime-suite-01 --sample-rate 48000 --buffer-size 480 --seconds 60 --warmup 3 --rounds 3
```

批处理轮换六个场景，每次独立打开共享输出设备；无并行压测。`suite-manifest.json` 记录命令、工具／源码 SHA-256、每次退出码和报告身份，失败立即停止，不删除失败结果。每份 `.json`／`.csv` 都要另外交给分析脚本，不能只看批处理完成或 C++ 摘要。

批处理在每次执行前后核对 EXE SHA-256；超时、启动失败和二进制变化都应留下无效条目。源码快照只说明采集时的文件内容，不能独立证明某个 EXE 由它构建；还须保留实际构建日志与产物哈希。

在批处理完成后，逐份复算：

```powershell
$suite = Join-Path (Get-Location) 'build/realtime-suite-01'
Get-ChildItem -LiteralPath $suite -Filter 'round-*.json' |
    Where-Object { $_.Name -notlike '*.analysis.json' } |
    ForEach-Object {
        $base = Join-Path $_.DirectoryName $_.BaseName
        python scripts/analyze_realtime_probe.py --report $_.FullName --csv "$base.csv" --output "$base.analysis.json"
        if ($LASTEXITCODE -ne 0) { throw 'Independent analysis failed; retain original files' }
    }
```

结果名称禁止复用。探针占用自己的结果命名空间，批处理以独占创建的 manifest 占用输出目录，避免误启动两个相同任务覆盖证据。异常退出后的残留文件应保留，换新名称调查；不要覆盖初轮失败来只留下成功。

单独保留基线、高 Unison、曲率／AUX／宏反馈矩阵、Trigger Chaos 和已加载真实素材的颗粒场景。持续按住 16 音的稳态压力不覆盖新音符启动、抢音、音色切换、界面打开、路由编辑或其他 DAW 的调度。

复音、颗粒、LFO 与 Unison 的数量不是可相乘的统一指标：颗粒使用自己的池，Unison 不再把该池乘七。源音量必须非零，三颗粒振荡器也必须各自已加载素材，否则可能只测到空路径。

当前工具不声称整条回调零分配或无锁。MIDI、素材发布及 JUCE 管理器自身的同步需要专门审计；此测量也不代替音质、抗混叠和听感判断。

工具本身的检查单独运行，不申请音频设备：

```powershell
python scripts/analyze_realtime_probe.py --self-test --output build/realtime-analyzer-self-test.json
python Tests/RealtimeRunnerTests.py
```

本轮分别通过 20 项算术／负例检查和 7 项运行器保护检查，不加入原有四套 CTest 的 220 组。CI 显式开启探针编译，再执行 `--help` 和这两项检查；云端不执行设备压力测量。
