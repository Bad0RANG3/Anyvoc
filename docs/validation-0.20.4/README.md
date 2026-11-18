# 0.20.4 正式构建的本机验证原件

本目录保存 0.20.4 **未插桩的正式构建**所对应的配置、编译、CTest、pluginval 和离线演示验证原件。这里不包含音频设备测量、原生应用人工操作、外部 MIDI、DAW、云端 CI 或发行 ZIP 的验收；不包含 WAV/EXE。发行包结果不在本目录内，也不由这些记录推定。

20 份原件均直接按字节复制，保留原编码、换行和本机路径。[原件映射](original-file-map.json)列出来源、大小和 SHA-256；[包外归档校验](../validation-archive-verification-0.20.4.json)核对全部 22 个归档文件，并将源文件、正式二进制与演示渲染器关联。README 和映射是本次归档新增说明，不冒充原始命令输出。

## 已核对的本机结果

| 项目 | 结果与原件 |
| --- | --- |
| 正式配置、构建 | [configure](configure-production-0.20.4.txt)、[build](build-production-0.20.4.txt)；[编译用 CMake 原件](compiled-inputs/CMakeLists.txt)为 0.20.4。 |
| 完整 CTest | [控制台](ctest-0.20.4.txt)、[JUnit](ctest-0.20.4.xml)、[完整 LastTest](LastTest.log)：Engine 75、Processor 129、Wavetable 10、PresetLibrary 6，共 **220 PASS**，四套 0 失败；墙钟 **210.62 s**。 |
| JUnit 时间 | 精确分项为 4.9454 / 204.953 / 0.079499 / 0.636504 s；suite 的 `time="210"` 是原件整数值，不将它冒充精确墙钟。JUnit 内部输出被 CTest 截断，220 组计数来自完整 LastTest。 |
| pluginval | [stdout](pluginval-0.20.4.txt)、[空 stderr](pluginval-0.20.4-stderr.txt)、[结果](pluginval-result-0.20.4.json)、[执行 helper](run-pluginval-0.20.4.ps1)：strictness 5，seed `0xc83e3a`，exit 0，末尾 `SUCCESS`。 |
| 正式输入与二进制 | [source-production](source-production-0.20.4.json)、[binaries](binaries-0.20.4.json)：七个正式 PE 均 0.20.4；原 JUCE 无诊断 hook，正常 Release 使用 `/MT`，没有诊断宏或强制 include。 |
| 四份演示 | [兼容性记录](demo-compatibility-0.20.4.json)及 [Macro PCM 分析](audio-analysis-0.20.4.json)通过。Legacy/Motion/Chaos/Macro 与冻结 0.20.3 Windows ZIP 对应成员逐字节相同；4 份 stdout 在 [render-0.20.4](render-0.20.4)。 |
| 旧证据完整性 | [old-frozen-integrity](old-frozen-integrity.json)：161 文件 cache 档案、205 文件设备 profiler 档案以及 0.20.2/0.20.3 四份冻结 ZIP 保持原哈希。归档时另作只读复核，没有重写这些输入。 |

`CTestCostData.txt` 是 CTest 自身的历史成本数据原件，不用它计算本轮耗时或测试组数。`LastTestsFailed.log` 的旧内容及旧时间戳不属于这次成功执行，未放入本目录。

## 输入关联与范围

正式 Source/Tests 当前字节与修复后的隔离 `after` 快照一致；正式 CMake 相对该快照只有 `0.20.3 → 0.20.4` 项目版本替换。隔离诊断使用有 hook 的 JUCE、版本号 0.20.3；本目录对应原始无 hook JUCE 的 0.20.4 正式构建，两种二进制不能互换。包外校验记录了该区分及七个当前 PE 的实际大小、SHA-256、FileVersion。

四份演示均由 `AnyvocProcessorTests.exe`（SHA-256 `4dafbd7f6ce5836724d3e3bb79665b73592eca166c40712c345768bf7be5525c`）重渲染。归档时核对当前文件 SHA、WAV 格式、冻结 ZIP 成员字节以及 JSON 中的 renderer 关联；Macro 数值分析保留原报告，没有将文件哈希核对写成一次新的 DSP 或听音测试。

本目录只汇集最终生产验证原件，不声称是所有尝试均首次通过的完整历史。诊断准备和辅助命令的失败原件、19/27/24 项工具自检、真实分配计数与原始 float 音频另见 [MIDI allocation 专项档案](../midi-allocation-0.20.4/README.md)，不在此重复，也不并入 220 组 DSP 计数。这里不证明整个回调零分配、零锁或实时性能，也不证明 Serum 等价。
