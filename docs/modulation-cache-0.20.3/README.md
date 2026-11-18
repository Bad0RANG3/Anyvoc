# 0.20.3 调制缓存原始证据

本目录保存固定曲率缓存优化的输入、原始输出与失败记录。合同及隔离复现流程见 [MODULATION_CACHE.md](../MODULATION_CACHE.md)。文件按显式清单复制，保留原始编码、换行和字节；未清理日志中的失败、警告或旧版本标签。

`originals.json` 逐一列出原件在仓库中的相对路径、归档路径、大小与 SHA-256。绝对磁盘路径可能仍出现在原始日志／报告中，它们标识当时采集位置；本目录没有重写这些原件。目录整体的精确集合与逐字节核验记录放在目录外的 `../modulation-cache-archive-verification-0.20.3.json`，避免包含自身。

## 输入与声音比较

- `audio/old/`：未优化生产源码，提交 `63a8192c126ae7dc751110df3d93512dcb5e6550`，加入同一冻结 exporter 后实际构建的旧 EXE 输出。
- `audio/final/`：最终 0.20.3 引擎 EXE 输出，不使用中间候选音频。
- 每侧 27 个 `.f32le` 和一个 manifest；音频为无头、little-endian float32、左右交错。每侧 406,296 帧／812,592 样点／3,250,368 音频字节。manifest 保留全部 MIDI、编辑和实际块序列。
- `reports/cache-audio-comparison-final-0.20.3.json` 为原始 **PASS** 报告，两侧全部 manifest 字段与原始音频字节一致；27 例不是新增的 27 个 CTest 组，也不是全部参数组合的穷举。
- `identity/` 绑定旧基线程序、旧 exporter、最终 benchmark EXE 与源文件身份。EXE 本身不放入归档；保存 EXE 的哈希不代表仅凭仓库即可恢复相同二进制。

缓存的 `+0/-0` 判断保留在源码；这些音频结果不能冒充直接命中了所有私有缓存键。该短 fixture 不覆盖长时间、所有素材、UI／宿主并发或硬件调度。

## 构建、基准和功能检查

`build/` 包含旧 exporter 构建、0.20.3 配置／首次完整构建／最终增量构建，以及两侧最终使用的导出日志。`tests/` 保存两次完整四套 CTest 的摘要、JUnit、逐组日志和退出结果：初轮 206.65 秒，最终 218.75 秒，均为 220 组。JUnit 输出可能截断，内部组数须依据完整逐组日志，不能只数 JUnit 中可见的 PASS 行。

`benchmarks/` 保留两份真实旧基准和两份最终 0.20.3 基准。旧宏基准使用 `identity/baseline-identity.json` 中的 `b5bdd7d8…f6f43f` EXE；旧矩阵基准使用 `identity/old-reference-identity.json` 中加入冻结 exporter 后构建的 `3f38bb7d…006a4bc` EXE，两者生产源码相同。最终两项共用 `identity/benchmark-final-binary.json` 的 `d03387e5…b69352c` EXE。宏基准每场景 2 秒音频，矩阵基准每场景 10 秒，均为三轮且包含每块 `setSettings()`。保留全部轮次、CHECKSUM 和变慢场景，不把离线速度换算为硬件回调通过。

`intermediate/` 另存中间构建和两份 `after` 基准。它们没有单独绑定当时 EXE 哈希，不作为最终性能结果；不能混用其数字与最终二进制身份。

`pluginval/` 保留初轮和最终 stdout、stderr、进程结果，均为 strictness 5 验证；最终 seed 为 `0x481d1f1`。`render/` 保存最终程序的四份演示渲染命令、日志和结果身份，不重复存储 WAV。

## 未通过与受限的原生检查

`native/` 完整保留第一次页脚显示 0.20.2 的错误记录，以及改为构建版本绑定后的最终记录。最终确认 PLAY、0.20.3 页脚和 INIT；音频设备无法打开，状态为 **PARTIAL_AUDIO_NOT_ACCEPTED**，没有成功琴键发声的结论。进程结束与设置恢复的哈希已记录；这里没有用户设置内容。

`device/` 保留设备枚举和三套失败短试的 TXT／JSON：新 0.20.3、旧 0.20.2、旧程序默认配置均未成功打开输出设备，没有形成有效逐块 CSV。**本轮没有新的有效声卡性能测量**；旧程序也失败不能证明驱动故障的具体原因，不能将失败记为零超预算或零掉音。历史有效测量单列在 [REALTIME-0.20.2.md](../REALTIME-0.20.2.md)，不挪作本版数据。

另存同期用户说明 `device/device-user-context-0.20.3.json`：用户确认当时有程序独占声卡。尚未在释放独占后重验，因此不能把这条说明写成已隔离根因或新的有效实时结果；本版实际演奏仍待验。已有 invalid 短试和 native partial 原件保持不变。

## 工具及测试 fixture 修复

`tooling/source-current/` 保存当前 17 个生产 `.h/.cpp`、CMake 和 RealtimeProbe 共 19 个构建输入；`tooling/source-old/` 是真实旧 SynthEngine.cpp。其余工具包括冻结 EngineTests、比较器、运行器、运行器测试和本轮输入核验脚本。这些是采集时快照，不是第二套活动生产源码。

`tooling/controls/` 保留比较器 12 个合成正反例和独占输出保护结果；它们不执行 DSP，也不加入 220 组。独占输出测试来自比较器最后诊断文字修正前，输出保护实现未变化；报告原件没有改写成新版本自测。

同次最终工具检查另保留 `analyzer-self-test-current.json` 的 20 项、`comparator-self-test-root.json` 的 12 项和 `runner-final-root.txt` 的 7 项通过结果；三项不是云端运行，也不增加四套 CTest 的组数。对应分析器／比较器／运行器源文件随 `tooling/` 保留。

运行器旧测试在普通 Python 3.11 路径下通过 7 组，在 junction 别名路径下准确复现竞争探针未触发的 1 项失败；规范化 fixture 路径后，Python 3.11 与直接启动的 Python 3.14 各 7 组通过。`runner-py314-after.txt` 仍保留首次 launcher 找不到 runtime 的失败，不能将它称为 Python 3.14 测试通过。原始云端失败另见 [GITHUB-CI-REALTIME-0.20.2.md](../GITHUB-CI-REALTIME-0.20.2.md)。这些模拟检查不申请音频设备。

最终输入核验脚本 `tooling/verify-release-inputs.py` 与 `reports/` 中 binaries／source-compatibility／demo-compatibility 三份 0.20.3 报告均来自 `release-inputs-final-3`，三项 **PASS**。八个 PE 的实际版本与身份、17 个生产文件、最终四 WAV 与冻结 0.20.2 ZIP 的字节及旧报告绑定均已核对；独立宏 PCM 分析另存同目录。它们核验的是发行输入，不是新 0.20.3 发行 ZIP 校验。

`intermediate/first-verifier/` 和 `intermediate/release-inputs-final-2/` 保留前两轮脚本与全部报告：初轮误要求 JUCE 未填写的固定 PRODUCTVERSION 也等于字符串版本，并误拒 Windows ZIP 成员的反斜杠；第二轮仍误拒 ZIP spelling。第三轮修正核验逻辑后通过，不把这些脚本误拒解释为发布输入损坏或曾被修改。每轮报告的 verifier SHA 与相应冻结脚本逐一绑定，不能拿最终脚本解释旧报告。

本目录不包含 EXE、PDB、WAV、截图、用户设置、junction 目标或整棵构建缓存。归档完整性只证明文件与原件一致；不替代听音、外接 MIDI、DAW、包校验或后续提交的 CI。
