# Anyvoc 0.20.2 GitHub 构建证据

核对日期：2026-11-18（Asia/Shanghai）。**提交 `2acc71db00b8e70fb2fb5456d3207d2a0cf0096e` 的 Windows 云端构建和全部 220 组自动测试通过。** 本页是 0.20.2 发布后的独立证据补充，只归档这一场已有运行，没有重新触发它。**不代表之后的真实音频回调探测或其他提交已经通过云端验收。**

## 运行身份与时间

| 项目 | 实际记录 |
| --- | --- |
| 源码 | [完整提交 2acc71db](https://github.com/Bad0RANG3/Anyvoc/commit/2acc71db00b8e70fb2fb5456d3207d2a0cf0096e) |
| Actions | [运行 44278368789](https://github.com/Bad0RANG3/Anyvoc/actions/runs/44278368789)，`main` 的 `push`，attempt 1，结论 `success` |
| Job | [windows / 134712234012](https://github.com/Bad0RANG3/Anyvoc/actions/runs/44278368789/job/134712234012)，九个步骤全部 `success` |
| 环境 | `windows-2022`，Visual Studio 2022，x64 Release；检出并验证固定 JUCE `29396c22c93392d6738e021b83196283d6e4d850` |
| Run 创建 | 2026-11-18 09:42:00 UTC，即北京时间 17:42:00 |
| Job 起止 | 09:42:03–09:53:32 UTC，即北京时间 17:42:03–17:53:32，共 11 分 29 秒 |
| 配置 | 09:42:24–09:44:00 UTC，`success` |
| Release 构建 | 09:44:00–09:50:28 UTC，`success` |
| 四套测试步骤 | 09:50:28–09:53:29 UTC，`success`；步骤时间包含 CTest 启动与收尾 |
| 附件 | `windows-test-evidence`，artifact ID **14417190401**，8,433 字节；创建于 09:53:29 UTC，核对时未过期 |

Run、Job 和 Artifact 的 head SHA 均与上述完整提交一致。下载 ZIP 的 SHA-256 为 `7d238cf7471b8f9cc283d436e88386ec225d42abc2b5c3b88e04f4bd2409a5ed`，与 GitHub Artifact API 的 digest 完全一致；ZIP CRC、安全路径、路径唯一性及精确的三个文件集合检查通过。

## 220 组结果从哪里来

独立解析附件里的 JUnit，并逐段清点完整 `LastTest.log`：

| 测试程序 | 完整日志 PASS | FAIL | JUnit 耗时 |
| --- | ---: | ---: | ---: |
| EngineTests | 75 | 0 | 8.58332 秒 |
| ProcessorTests | 129 | 0 | 170.266 秒 |
| WavetableTests | 10 | 0 | 0.0572539 秒 |
| PresetLibraryTests | 6 | 0 | 0.621825 秒 |
| 合计 | **220** | **0** | **179.5283989 秒（约 179.53 秒）** |

完整日志还包含四个 `Test Passed.`。JUnit 根节点记录四个测试程序，`failures=0`、`disabled=0`、`skipped=0`，根时间为整数 179 秒；四个程序均为 `status="run"`，没有 failure、error 或 skipped 节点。四个 CTest 程序和 220 个内部测试组是不同计数单位。

**Engine 和 Processor 的 JUnit `system-out` 明确说明超过 1,024 字节的内容被移除。** 其中仅能看到 22／18 条 PASS，不能当成完整计数；75／129 来自未截断的 `LastTest.log`。另两套输出完整。云端用时与本机最终全套 204.40 秒分别保留，不互相替代。

## 仓库中的证据

| 文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| [完整原始日志](test-details-github-ci-0.20.2.txt)，来自附件 `Testing/Temporary/LastTest.log` | 20,054 | `8e34ef341df95cb7f6e7457e2aca2ddc87e3a15986a58b9dd4af64ab279a85da` |
| [原始 JUnit](test-results-github-ci-0.20.2.xml)，来自附件 `test-results.xml` | 4,163 | `09b64d9031ac3e46d514c4dfe9c5bb6ee1ce74255e0c7422557c7307bf2f85cd` |

上述两份文件按原字节复制，不改写时间、编码或换行；`.gitattributes` 仅为这两个精确路径关闭文本转换和空白检查。另提供[精简独立核验 JSON](verification-github-ci-0.20.2.json)，记录公开运行身份、摘要、测试计数来源和边界，不复制完整 API 响应。

附件里的第三个文件 `Testing/Temporary/CTestCostData.txt` 为 113 字节，SHA-256 是 `0037d34cf1dcbab957de0a1fed97030ca13bad87ae72379897ec95b287cbf8c3`。它与原 ZIP、完整 API 元数据保留在本机 `build/github-ci-0.20.2/`，没有添加到仓库。

## 适用范围

这次运行证明指定提交可在 Windows 干净环境构建并通过四套自动测试，包括 0.20.2 新增的七组 ARP 通道／按键所有权回归。工作流不执行 pluginval、原生 UI、真人听音、系统剪贴板、外接 MIDI 或 DAW 工程操作；这些证据分别见[本机 0.20.2 验证记录](VALIDATION-0.20.2.md)。后续真实音频回调探测或其他修改必须按自己的提交重新验证，不能引用本页替代。

本次归档**没有重新生成或修改任何 0.20.2 发行 ZIP**。冻结发行包及其包内文档仍保持发布时的内容，外置包校验报告的原始结论不变；本页和新添原始证据没有写回旧包。GitHub 源码仓库也未因此增加 EXE、VST3、WAV、ZIP 或 Release 二进制附件。
