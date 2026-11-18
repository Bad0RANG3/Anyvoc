# Anyvoc 0.20.1 GitHub 构建证据

核对日期：2026-11-18（Asia/Shanghai）。**提交 `f37169e9e7f4ccc3c4de7655bde84fdfa30af801` 的 Windows 云端构建和全部 213 组自动测试通过。** 本页是 0.20.1 发布后的独立证据补充，只归档这一场已有运行，没有重新触发它。**不代表之后的 ARP 通道／按键所有权修改或其他提交已经通过云端验收。**

## 运行身份与时间

| 项目 | 实际记录 |
| --- | --- |
| 源码 | [完整提交 f37169e9](https://github.com/Bad0RANG3/Anyvoc/commit/f37169e9e7f4ccc3c4de7655bde84fdfa30af801) |
| Actions | [运行 44273076006](https://github.com/Bad0RANG3/Anyvoc/actions/runs/44273076006)，`main` 的 `push`，attempt 1，结论 `success` |
| Job | [windows / 134694269900](https://github.com/Bad0RANG3/Anyvoc/actions/runs/44273076006/job/134694269900)，九个步骤全部 `success` |
| 环境 | `windows-2022`，Visual Studio 2022，x64 Release；检出并验证固定 JUCE `29396c22c93392d6738e021b83196283d6e4d850` |
| Run 创建 | 2026-11-18 09:08:10 UTC，即北京时间 17:08:10 |
| Job 起止 | 09:08:13–09:19:04 UTC，即北京时间 17:08:13–17:19:04，共 10 分 51 秒 |
| 配置 | 09:08:30–09:09:50 UTC，`success` |
| Release 构建 | 09:09:50–09:16:10 UTC，`success` |
| 四套测试步骤 | 09:16:10–09:19:00 UTC，`success`；步骤时间包含 CTest 启动与收尾 |
| 附件 | `windows-test-evidence`，artifact ID **14414615598**，8,244 字节；创建于 09:19:00 UTC，核对时未过期 |

Run、Job 和 Artifact 的 head SHA 均与上述完整提交一致。下载 ZIP 的 SHA-256 为 `6a3f11f77e19ec58c0e74b5ec2a7bbbc5bba904b8b051be87a176ae3218276cc`，与 GitHub Artifact API 的 digest 完全一致；ZIP CRC、安全路径和路径唯一性检查通过。

## 213 组结果从哪里来

独立解析附件里的 JUnit，并逐段清点完整 `LastTest.log`：

| 测试程序 | 完整日志 PASS | FAIL | JUnit 耗时 |
| --- | ---: | ---: | ---: |
| EngineTests | 75 | 0 | 8.50671 秒 |
| ProcessorTests | 122 | 0 | 159.608 秒 |
| WavetableTests | 10 | 0 | 0.0557644 秒 |
| PresetLibraryTests | 6 | 0 | 0.612639 秒 |
| 合计 | **213** | **0** | **168.7831134 秒（约 168.78 秒）** |

完整日志还包含四个 `Test Passed.`。JUnit 根节点记录四个测试程序，`failures=0`、`disabled=0`、`skipped=0`，根时间为整数 168 秒；四个程序均为 `status="run"`，没有 failure、error 或 skipped 节点。四个 CTest 程序和 213 个内部测试组是不同计数单位。

**Engine 和 Processor 的 JUnit `system-out` 明确说明超过 1,024 字节的内容被移除。** 其中仅能看到 22／18 条 PASS，不能当成完整计数；75／122 来自未截断的 `LastTest.log`。另两套输出完整。云端用时与本机最终全套 199.66 秒分别保留，不互相替代。

## 仓库中的证据

| 文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| [完整原始日志](test-details-github-ci-0.20.1.txt)，来自附件 `Testing/Temporary/LastTest.log` | 19,464 | `ebb921c23106976883310a861e93839716afdc78cea52b82ced02f90fffe31d1` |
| [原始 JUnit](test-results-github-ci-0.20.1.xml)，来自附件 `test-results.xml` | 4,163 | `3c36da9b4d797a70c727495040d8b12b2af95b0bac25810fcd0b066fecb80d8c` |

上述两份文件按原字节复制，不改写时间、编码或换行；`.gitattributes` 仅为这两个精确路径关闭文本转换和空白检查。另提供[精简独立核验 JSON](verification-github-ci-0.20.1.json)，记录公开运行身份、摘要、测试计数来源和边界，不复制完整 API 响应。

附件里的 `Testing/Temporary/CTestCostData.txt` 为 113 字节，SHA-256 是 `2367a26332e444a474705c73c0e409c35b0a1fbad89a804960349eba0622d795`。它与原 ZIP、完整 API 元数据保留在本机 `build/github-ci-0.20.1/`，没有添加到仓库。

## 适用范围

这次运行证明指定提交可在 Windows 干净环境构建并通过四套自动测试，包括 0.20.1 新增的三组 ARP CC121 回归。工作流不执行 pluginval、原生 UI、真人听音、系统剪贴板、外接 MIDI 或 DAW 工程操作；这些证据分别见[本机 0.20.1 验证记录](VALIDATION-0.20.1.md)。后续 ARP 多通道、同音高所有权或其他修复必须按自己的提交重新验证，不能引用本页替代。

本次归档**没有重新生成或修改任何 0.20.1 发行 ZIP**。冻结发行包及其包内文档仍保持发布时的内容，外置包校验报告的原始结论不变；本页和新添原始证据没有写回旧包。GitHub 源码仓库也未因此增加 EXE、VST3、WAV、ZIP 或 Release 二进制附件。
