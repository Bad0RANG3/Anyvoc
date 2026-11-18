# Anyvoc 0.20.3 验证记录（验收进行中）

记录日期：2026-11-18。此版针对密集调制矩阵的曲率计算增加精确缓存，目标是在保留声音结果的同时减少重复计算。**最终 Release 重建、220 组完整自动测试、27 例原始浮点音频比较及 pluginval 已通过。实际输出设备无法打开，原生演奏与正式实时复测受阻。首轮 Windows／完整源码候选包校验通过，文档收口后复验；指定提交 CI 运行中，不能称为本版整体通过。**

改动合同见 [MODULATION_CACHE.md](MODULATION_CACHE.md)。仍为 1,256 个宿主参数、schema 14、28 个工厂音色；本次没有添加声音参数、修改预设或扩展音色能力。历史 [0.20.2 验证](VALIDATION-0.20.2.md)、[实际设备测量](REALTIME-0.20.2.md)与下述云端失败均按各自版本保留。

## 精确缓存与兼容边界

原缓存为每个主源／AUX 描述符保存最后一个输入和输出。当 16 个声部依次提供不同但长期不变的力度、保持值等输入时，这一个位置会被反复替换。现在保留原共享缓存优先路径，再为各声部增加一个精确输入／输出位置；共同变化的 Free LFO 等源仍可先命中共享缓存。32 个缓存固定属于物理路由槽，单独排序槽索引，避免每块排序移动描述符及其缓存。

没有查表近似、量化、epsilon 命中或曲率公式替换。输入使用精确相等并区分正负零；源、极性或曲率改变时使两层缓存失效。重置只清有效标记，不丢失已经准备好的描述符。零曲率直通、AUX Off 的乘法单位值、路由数量与累加顺序、平滑尾和宏／相位的一采样反馈保持原逻辑。缓存是纯函数结果，不能用来推断整条音频回调无分配或无锁。

## 修改前参考与最终音频一致性

先保留 `63a8192c126ae7dc751110df3d93512dcb5e6550` 的旧生产引擎，再用同一冻结导出器生成旧参考，之后才修改缓存。旧参考 EXE 的 SHA-256 为 `3f38bb7d6c2ee3ef57b3364e1510fab18597a074f1a7471a69d552c73006a4bc`；旧引擎源码 SHA-256 为 `1f68ed79b6b30531032a19656362f7f9a50fa8cb7c4e1f3960f9727203437917`。[原基线身份](modulation-cache-0.20.3/identity/baseline-identity.json)、[旧 exporter 身份](modulation-cache-0.20.3/identity/old-reference-identity.json)及最终输入记录已单独归档，不以当前优化实现重新生成“旧版”参考。

[最终 0.20.3 导出结果与旧参考](modulation-cache-0.20.3/reports/cache-audio-comparison-final-0.20.3.json) **27／27 逐字节一致**，manifest 全字段及原始字节也一致。三种配置分别覆盖全源、常数／保持值和宏／相位反馈；交叉使用 44.1／48／96 kHz 与 127／256／511 块长。每例 0.24 秒，合计 6.48 秒；每一侧为 **406,296 个立体声帧、812,592 个 float32 样本、3,250,368 字节音频**。文件是 little-endian、左右交错的原始 float32，不经过整数 WAV 量化。

序列包含主源与 AUX、曲率／极性编辑、旁通后编辑并恢复、早期槽位增删、相同描述符但不同在途平滑量的换行、16 声部、力度、偷声部、控制器、Mono／Legato 以及 reset／prepare 后未再次设置参数就渲染的边界。比较器另检查完整文件集合、有限且非静音、声明的 RMS／peak 与分块序列。其 12 项合成自检覆盖缺文件、单字节音频改变、NaN、截断、静音、manifest 改动等负例；这些属于比较工具测试，不计入 DSP 测试组数。

这证明上述冻结序列在本机两个实际渲染器中的输出完全一致，不是所有输入、平台或硬件实时表现的证明。缓存正负零键的保护另由源码审查确认，不能把音频一致本身当作该内部键的独立测试。

旧／新音频原件已分别归档至 `docs/modulation-cache-0.20.3/audio/old/` 与 `audio/final/`，最终比较报告在上述 `reports/` 链接。归档说明和完整清单见[专项证据目录](modulation-cache-0.20.3/README.md)。从仓库原件复算时使用新的包外输出，不覆盖原报告：

```powershell
python scripts/compare-cache-regression.py --reference docs/modulation-cache-0.20.3/audio/old --candidate docs/modulation-cache-0.20.3/audio/final --output build/cache-audio-recheck-0.20.3.json
```

输出必须是尚不存在的文件，且位于两个输入目录之外。上述归档可用于独立比较；重建两个渲染器则需按缓存合同使用准确历史源码，普通源码 ZIP 没有 Git 历史。

## 旧云端失败与运行器测试修复

[指定提交的失败归档](GITHUB-CI-REALTIME-0.20.2.md)记录了 `63a8192` 的 Run `44286216206`：构建、220 组 CTest 和 20 项分析器自检成功，运行器测试为 6／7，整场结论仍为 **failure**。它早于本次缓存改动，也不能作为 0.20.3 的云端结果。

本地用 Windows junction 使临时目录的字面路径与 `Path.resolve()` 结果不同，复现了同一个 `contender_attempted` 断言失败：旧测试为 6／7。生产运行器先解析输出路径，而测试拦截器按未经解析的路径拼写匹配，因此没有在预定位置插入竞争者。这是测试观察点未命中的复现，不是放宽并发保护断言；未单独取得云端临时目录的实际 junction 映射，不能将本地复现冒称为云端文件系统取证。

修复后拦截器比较解析后的路径，并在用例中固定加入 `component/../results` 别名，使每次运行都实际覆盖非规范路径。仍要求在独占创建成功、首字节写入之前启动竞争者，并检查它被拒绝、没有启动捕获、没有覆盖已有内容。junction 环境下 Python 3.11.17 为 **7／7，0.133 秒**，Python 3.14.8 为 **7／7，0.125 秒**。原始失败和成功输出按原字节保留在[专项证据目录](modulation-cache-0.20.3/README.md)，文件名为 `runner-py311-before-junction.txt`、`runner-py311-after.txt`、`runner-py314-after-direct.txt`。生产套件运行器未因这项修复改变。

这些测试使用假 EXE／模拟子进程，不打开音频设备。当前修复的本地通过不会改写旧云端运行的失败结论，0.20.3 的 [Run 44292647323](https://github.com/Bad0RANG3/Anyvoc/actions/runs/44292647323) 对应完整 head `a956c5e3fc1dfb52d269650c3e75ff370289af82`，记录时运行中，结果待核实；该记录不覆盖后续提交。

## 最终离线性能与受阻的设备验收

[最终 EngineTests EXE 身份](modulation-cache-0.20.3/identity/benchmark-final-binary.json)的 SHA-256 为 `d03387e5073cc24b5e30e9aa15249d3dc552c86e0f576da8e72ea18b8b69352c`，旧宏基准使用此前留存的 EngineTests EXE，SHA-256 为 `b5bdd7d8de02d822bdf67e2ef6315cbe28de5e6849961eed0226d1e5adf6f43f`；旧矩阵基准使用加入冻结 exporter 的真实旧 EXE `3f38bb7d6c2ee3ef57b3364e1510fab18597a074f1a7471a69d552c73006a4bc`。二者生产引擎源码都为前述旧 `1f68ed79…3437917`，不混称同一程序。新侧两项共同使用最终 EXE。每项沿用固定负载，三轮轮换顺序；下表列 `wall/audio` 中位数，越小表示该段离线计算用时越少。计时包含每块设置与渲染，原始轮次和 CHECKSUM 全部保留。

| 离线场景 | 修改前 | 最终 0.20.3 |
| --- | ---: | ---: |
| 宏：32 条旧宏来源路由 | 0.204532 | 0.197493 |
| 宏：32 条曲率／AUX／反馈 | 0.588157 | 0.522825 |
| 宏：997 Hz Pitch／Pan | 0.289211 | 0.287018 |
| 宏：997 Hz 双滤波 | 0.280951 | **0.282295（略慢）** |
| 矩阵：0 路由 | 0.080949 | 0.080255 |
| 矩阵：8 条旧线性路由 | 0.101188 | **0.104862（略慢）** |
| 矩阵：32 条曲率／AUX | 0.228527 | 0.195427 |
| 矩阵：32 条逐声部曲率／AUX | 0.510801 | 0.479171 |

两项 CHECKSUM 分别保持 `0.039300`、`0.204156`。这不是“全部路径加速”，也不是回调最坏时间证明；小幅差异含测量波动。中间候选输出仍保留，但未绑定中间 EXE 哈希，不纳入这张最终表。原始记录为[旧宏](modulation-cache-0.20.3/benchmarks/benchmark-macros-before.txt)、[旧矩阵](modulation-cache-0.20.3/benchmarks/benchmark-matrix-before.txt)、[最终宏](modulation-cache-0.20.3/benchmarks/benchmark-macros-final-0.20.3.txt)与[最终矩阵](modulation-cache-0.20.3/benchmarks/benchmark-matrix-final-0.20.3.txt)，中间候选另存。

实际 0.20.3 窗口启动、PLAY 和 INIT Pure Canvas 已观察；首次原生检查发现页脚仍为硬编码旧版本，随后改为构建版本常量、重建并重新运行全套测试。最终页脚显示 0.20.3。初始音频设置为空、Output 为 none；按 C4 及临时延长 Release 仍没有电平／EV 活动，选择 NICEHCK 输出报 `Couldn't open the output device!`。新 0.20.3 与真实旧 0.20.2 探针在 48 kHz／256 下都 exit 1；关闭应用后用旧探针的默认设备配置也同样失败。三份 JSON 都为 invalid，没有形成有效性能样本。

因此本次只确认界面启动与显示，**原生琴键演奏未通过，六负载各三轮的正式设备测量没有完成**。用户随后确认同期有其他程序正在独占声卡；这补充了失败时的环境条件，但尚未释放设备并重测，不能称具体根因已隔离验证，也不能归因于新缓存。未关闭用户的占用程序，原 invalid 报告保留不改写。实际程序已关闭，原用户设置恢复后的 SHA-256 为 `5000f1074d6b885621afabb357a32e2ba2050f8661ad38c3fb92bc520674ab7e`。[最终原生原件](modulation-cache-0.20.3/native/native-final-0.20.3.json)及[新探针](modulation-cache-0.20.3/device/device-open-short-0.20.3.json)、[旧探针相同配置](modulation-cache-0.20.3/device/device-open-short-0.20.2.json)、[旧探针默认配置](modulation-cache-0.20.3/device/device-open-old-defaults.json)失败报告保留；旧 0.20.2 的成功会话不替代当前结果。

最终发行输入独立核验也通过：[二进制](modulation-cache-0.20.3/reports/binaries-0.20.3.json)、[源码兼容](modulation-cache-0.20.3/reports/source-compatibility-0.20.3.json)和[四演示](modulation-cache-0.20.3/reports/demo-compatibility-0.20.3.json)分别记录。七个 Anyvoc PE 均为 0.20.3，pluginval 为 1.0.4；17 个生产源码文件中 15 个原字节不变，另外两个是引擎缓存改动及 Editor 唯一页脚版本替换。参数定义、状态 schema 和预设没有改变。辅助核验器前两次分别因不适用于 JUCE 的固定产品版本假设，以及 Python 3.14 路径分隔符标准化而误拒绝；保留失败及修正，第三次没有改变发行输入便通过。它们是辅助核验器失败，不冒称首次输入检查通过。

## 验收分项与待收口项目

| 项目 | 当前状态与边界 |
| --- | --- |
| 最终构建／完整自动测试 | **220／220 PASS，exit 0，218.75 秒**。Engine 75／4.9647 秒，Processor 129／212.997 秒，Wavetable 10／0.0901167 秒，Preset 6／0.687141 秒。[最终 CTest](modulation-cache-0.20.3/tests/ctest-final-0.20.3.txt)、[JUnit](modulation-cache-0.20.3/tests/ctest-final-0.20.3.xml)和[逐组日志](modulation-cache-0.20.3/tests/test-details-final-0.20.3.txt)已归档，[首次完整运行](modulation-cache-0.20.3/tests/ctest-0.20.3.txt)另存。 |
| VST3 pluginval | 最终 strictness 5 **PASS**，seed `0x481d1f1`，exit 0；[最终日志](modulation-cache-0.20.3/pluginval/pluginval-final-0.20.3.txt)、stderr 和结果 JSON 保留。页脚修复前 seed `0x7826acf` 的记录另存，不混作最终模块结果。 |
| 四份固定演示 | 最终 0.20.3 四份重渲染与冻结 0.20.2 ZIP 完整字节一致；旧 ZIP 的 CRC、安全唯一路径及原包哈希绑定均通过，[宏独立分析](modulation-cache-0.20.3/reports/audio-analysis-0.20.3.json) PASS。27 例缓存导出不替代此项。 |
| 最终离线 benchmark | 已完成，身份与所有八个场景如上；旧线性路由及宏双滤波略慢，不宣称所有路径加速。 |
| 实际设备回调 | **受阻待验**：新旧探针均无法打开输出设备，没有新的有效六负载三轮记录。旧 0.20.2 数据不替代，不称过载已消除或实时稳定。 |
| 限定原生操作 | **部分完成**：最终窗口／版本／PLAY／INIT 可见，设备失败，琴键输出未通过；没有听音、外接 MIDI、DAW 或保存重启验收。 |
| Windows／完整源码候选包 | **首轮校验 PASS，`verify-release.py` exit 0**：CRC、安全唯一路径、精确集合、逐文件内容、版本、三个必需演示及当前文档全部通过。候选入口 `dist/Anyvoc-0.20.3-Windows/Anyvoc.exe`；权威报告为包外 `dist/Anyvoc-0.20.3-PackageVerification.json`。首轮 ZIP／报告已存 `build/validation-cache-optimization/packaging-first/`，本次文档收口后重打包复验。冻结旧包不变，仍不声明整体验收完成。 |
| 指定提交 CI | 上述 Run 44292647323 记录时运行中，结果待核实；仅适用于 `a956c5e3fc1dfb52d269650c3e75ff370289af82`。 |

实际设备报告须分别列出测量是否有效、观察到的处理耗时／超预算、管理器估计以及驱动原生计数的支持情况。原生计数为 `-1` 时仍是未知，不得转换为“没有掉音”；离线平均加速也不能替代这一判断。听音、外接 MIDI、FL Studio／其他 DAW、零分配及跨平台验收不从上述自动测试推导。
