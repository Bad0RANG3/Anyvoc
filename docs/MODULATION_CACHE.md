# 调制曲率缓存：合同与复现

0.20.3 优化的是重复输入的曲率计算。它保留原有调制公式、路由结果和声音参数，不增加宿主参数、状态字段或工厂音色。实现位于 [SynthEngine.cpp](../Source/SynthEngine.cpp)，变换公式仍在 [ModulationRoute.h](../Source/ModulationRoute.h)。

## 缓存保存什么

每个引擎固定保存 **32 个物理路由槽**，每槽的主源与 AUX 各有一份变换描述及缓存。活动行和累加顺序由独立的索引数组表示；准备路由时只排序索引，不搬运或重建缓存对象。删除前面的活动行不会把后续物理槽的缓存挤到另一个槽。

每个有曲率的源依次查询两层：

1. **共享最近值**：先检查这个路由源最近一次的原始输入。Free LFO 等多个声部同时读取相同值时，仍可共享一次计算。
2. **16 个声部槽的最近值**：共享值未命中，再检查当前声部槽。这能保留不同力度、保持阶段或阶梯源的各自结果，避免被其他声部的输入反复挤掉。

未命中才调用原有 `transformModulationSource()`，随后记录原始输入与变换结果。零曲率直接走原变换，不使用这两层曲率缓存。主源、AUX 分开缓存；**不缓存两者乘积、Amount 或一整条路由的贡献**。

命中要求相同描述下的 `raw == previousInput`，并在值为零时额外比较 `signbit`，不将 `+0.0` 和 `-0.0` 混同。没有 epsilon、舍入、查表或近似 `pow`。NaN 不会命中；非有限输入仍按原变换处理，缓存不改变其结果。

描述由 source、polarity、curve 组成。任一有效描述改变，就重建变换并清除该源两层缓存的 valid 标志；未变时保留。旁通／Off 路由再次激活时同样检查描述。`reset()` 清除 valid，但保留变换描述，不能要求调用者再次 `setSettings()` 才恢复正确声音。声部抢占或复用本身不要求删除缓存：这里保存的是描述和输入决定的纯函数值，不是上一个音符的时间状态。

以下内容不因缓存改变：原有 `pow`／极性转换、累加顺序、Amount 平滑、主源 × AUX × 平滑 Amount 的运算、宏与相位的一采样反馈、依赖闭包和快速控制更新。完整路由换行仍沿用既有平滑量匹配与规范累加规则，合同见 [ROUTE_PUBLICATION.md](ROUTE_PUBLICATION.md) 和 [MACRO_TARGETS.md](MACRO_TARGETS.md)。

按当前 MSVC x64 布局，每引擎较旧共享缓存增加 **12 KiB**；固定数组的编译期断言限制额外空间**不超过 16 KiB**，也考虑索引数组和 ABI padding。缓存查询、失效与索引准备使用固定容量；这不等于整条音频回调已证明零分配或无锁，已有 MIDI、素材、JUCE 管理器等路径不在这个结论内。

## 已完成的精确声音比较

本轮先用未优化生产源码与冻结的新 exporter 构建真实旧 EXE，导出基线，再修改生产实现。[最终 0.20.3 比较报告](modulation-cache-0.20.3/reports/cache-audio-comparison-final-0.20.3.json)为 **PASS**：两侧各 **27 个音频文件、406,296 帧、812,592 个 float 样点、3,250,368 个音频字节**，全部 manifest 字段和全部音频原始字节相同。报告及两侧原件随[证据目录](modulation-cache-0.20.3/README.md)提供，可独立复算；渲染器 EXE 未提交到 Git。

旧生产基线为提交 `63a8192c126ae7dc751110df3d93512dcb5e6550`；用于两侧的 [EngineTests.cpp](../Tests/EngineTests.cpp) SHA-256 为 `9fe4e478ca1981ec846079e19c5bfbde1f380859260f29f577511c07b9f1a083`。旧 EXE 是在该旧生产源码上加入相同 exporter 后构建，不是把当前程序改名。

三个 profile 为全部主／AUX 源、常数／保持源、宏／相位反馈，分别组合 44.1／48／96 kHz 与名义块长 127／256／511。每例 0.24 秒，总计 6.48 秒；固定绝对采样位置会将块进一步切开。manifest 记录全部 MIDI、设置变更和实际正长／零长块序列。文件格式为无头、little-endian IEEE754 float32、左右声道交错，不经过 PCM24 量化或归一化。

覆盖包括 16 声部不同力度、共享与逐声部 LFO、主／AUX 曲率和极性编辑、旁通后编辑再启用、Off／删除／恢复、纯换行与重复描述的在途平滑量、Amount 归零尾部、宏和相位反馈、抢音、控制器停止、Mono／Legato、reset 和 prepare。后两者的首个空块及正长块刻意不先调用 `setSettings()`。

这不是全部参数组合的穷举或私有缓存命中率测试；没有声称动态输入已经直接覆盖 `+0/-0` 私有缓存键。符号零条件另由源码审查确认。短 fixture 未包含 Chaos、颗粒、长时间漂移、编辑器／宿主并发或设备调度。声音逐字节一致也不证明主观音质、实时期限、零分配或无锁。

## 用两个隔离 checkout 复现

以下在 **已提交的 0.20.3 源码仓库根目录**用 PowerShell 执行，要求 Git、固定 JUCE 子模块和 Windows 构建工具。它创建两个独立 clone 和两个全新构建目录，不切换当前工作树、不替换当前生产程序，也不写发行包。独立 clone 避开 linked worktree 对子模块支持的限制。开发中的未提交生产改动不能用 `HEAD` 代表；先保留准确源码身份，再复现。

```powershell
$ErrorActionPreference = 'Stop'
$repoRoot = (Get-Location).Path
$candidateCommit = (git rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Not a Git checkout' }
if (git status --porcelain -- Source Tests/EngineTests.cpp CMakeLists.txt) {
    throw 'Use a committed candidate; HEAD must identify the tested source.'
}
$oldCommit = '63a8192c126ae7dc751110df3d93512dcb5e6550'
$repro = Join-Path $repoRoot ('build/cache-repro-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $repro | Out-Null
@{ old_commit=$oldCommit; candidate_commit=$candidateCommit } |
    ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $repro 'commits.json')
$oldTree = Join-Path $repro 'old-source'
$newTree = Join-Path $repro 'new-source'
git clone --no-hardlinks --no-checkout $repoRoot $oldTree
if ($LASTEXITCODE -ne 0) { throw 'Cannot clone old checkout' }
git -C $oldTree checkout --detach $oldCommit
if ($LASTEXITCODE -ne 0) { throw 'Cannot select old commit' }
git clone --no-hardlinks --no-checkout $repoRoot $newTree
if ($LASTEXITCODE -ne 0) { throw 'Cannot clone candidate checkout' }
git -C $newTree checkout --detach $candidateCommit
if ($LASTEXITCODE -ne 0) { throw 'Cannot select candidate commit' }
if (!(Select-String -LiteralPath (Join-Path $newTree 'CMakeLists.txt') `
    -Pattern '^project\(Anyvoc VERSION 0\.20\.3 ' -Quiet)) {
    throw 'Candidate commit is not 0.20.3.'
}
foreach ($tree in @($oldTree, $newTree)) {
    git -C $tree submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) { throw 'Pinned JUCE checkout failed' }
    $jucePin = git -C (Join-Path $tree 'third_party/JUCE') rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $jucePin -ne '29396c22c93392d6738e021b83196283d6e4d850') {
        throw 'Unexpected JUCE dependency commit.'
    }
}
# Only the test exporter is overlaid on old Source; both sides use identical tests.
$fixture = Join-Path $newTree 'Tests/EngineTests.cpp'
if ((Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash.ToLowerInvariant() -ne `
    '9fe4e478ca1981ec846079e19c5bfbde1f380859260f29f577511c07b9f1a083') {
    throw 'Use the frozen 0.20.3 fixture.'
}
Copy-Item -LiteralPath $fixture -Destination (Join-Path $oldTree 'Tests/EngineTests.cpp')
if ((Get-FileHash -LiteralPath $fixture).Hash -ne `
    (Get-FileHash -LiteralPath (Join-Path $oldTree 'Tests/EngineTests.cpp')).Hash) {
    throw 'Fixture copies differ.'
}
git -C $oldTree diff --exit-code $oldCommit -- Source
if ($LASTEXITCODE -ne 0) { throw 'Old production source was modified.' }
git -C $newTree diff --exit-code $candidateCommit -- Source
if ($LASTEXITCODE -ne 0) { throw 'Candidate production source was modified.' }
$runs = @(
    @{ Name='old'; Tree=$oldTree; Build=(Join-Path $repro 'old-build') },
    @{ Name='new'; Tree=$newTree; Build=(Join-Path $repro 'new-build') }
)
foreach ($run in $runs) {
    cmake -S $run.Tree -B $run.Build -G 'Visual Studio 17 2022' -A x64 `
        -DANYVOC_BUILD_TESTS=ON -DANYVOC_BUILD_REALTIME_PROBE=OFF
    if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
    cmake --build $run.Build --config Release --target AnyvocEngineTests --parallel 4 2>&1 |
        Tee-Object -FilePath (Join-Path $repro ($run.Name + '-build.txt'))
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    $run.Exe = Join-Path $run.Build 'AnyvocEngineTests_artefacts/Release/AnyvocEngineTests.exe'
    Get-FileHash -Algorithm SHA256 -LiteralPath $run.Exe, `
        (Join-Path $run.Tree 'Source/SynthEngine.cpp'), `
        (Join-Path $run.Tree 'Tests/EngineTests.cpp') |
        ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $repro ($run.Name + '-identity.json'))
    & $run.Exe --render-cache-regression (Join-Path $repro ($run.Name + '-output')) 2>&1 |
        Tee-Object -FilePath (Join-Path $repro ($run.Name + '-render.txt'))
    if ($LASTEXITCODE -ne 0) { throw 'Export failed; retain this directory' }
}
python (Join-Path $newTree 'scripts/compare-cache-regression.py') `
    --reference (Join-Path $repro 'old-output') --candidate (Join-Path $repro 'new-output') `
    --output (Join-Path $repro 'comparison.json')
if ($LASTEXITCODE -ne 0) { throw 'Exact regression failed; retain both exports and report' }
```

exporter 拒绝已有输出目录，比较器拒绝覆盖已有报告。比较器验证 27 个组合、完整／安全／唯一文件集合、大小、有限及非静音样点、manifest 事件与块序列、两侧全部字段以及每个音频字节；报告包含全部文件 SHA 和首个差异。即使声音差异极小也不会接受容差。更换 fixture 后必须同时重建旧／新两侧，不能拿新 fixture 对旧 fixture 的输出比较。

比较器自身的 `--self-test --output NEW_JSON` 使用合成临时文件验证正反例，不运行合成器，不能加到正常 75 组引擎测试或冒充真实 27 例。源码 ZIP 中没有 Git 历史；这套双 checkout 流程需要仓库及上述旧提交。

## 性能入口与解释

沿用上面生成的 `$runs`，在没有其他构建或压力测试并行运行时顺序测量：

```powershell
foreach ($run in $runs) {
    foreach ($mode in @('macros', 'matrix')) {
        & $run.Exe ('--benchmark-' + $mode) 2>&1 |
            Tee-Object -FilePath (Join-Path $repro ($run.Name + '-benchmark-' + $mode + '.txt'))
        if ($LASTEXITCODE -ne 0) { throw 'Benchmark did not complete' }
    }
}
```

两项均为 48 kHz、256 样本、16 个错开发声的持续音、三轮轮换顺序；宏基准每场景 2 秒音频，矩阵基准每场景 10 秒。计时包含**每块 `setSettings()` 与 render**，不含构造和预热，不能只用 `pow` 微基准替代实际准备／排序成本。

`--benchmark-matrix` 对比无路由、8 条线性路由、32 条曲率／AUX 路由、32 条逐声部曲率路由；`--benchmark-macros` 对比旧宏源、曲率／AUX／宏与相位反馈、997 Hz 宏链至 Pitch／Pan、997 Hz 宏链至双滤波。保留全部轮次、中位数、CHECKSUM、EXE 哈希、硬件及后台负载，不删除变慢场景。

离线吞吐与真实设备块期限是不同证据；退出 0 也没有内置性能达标阈值。设备复测按[真实回调指南](REALTIME_PROFILING.md)执行，并独立分析原始 CSV。旧[0.20.2 回调记录](REALTIME-0.20.2.md)只对应旧生产源码，不能用来声称 0.20.3 已经完成设备验收。本页的精确音频比较也不替代完整 CTest、pluginval、独立版操作或发布包校验。
