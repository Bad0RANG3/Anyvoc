# Anyvoc 0.20 GitHub 构建证据

核对日期：2026-11-18（Asia/Shanghai）。**提交 `beecbc09b2524e26175e3534c3ac0b60f68d6cff` 的 Windows 云端构建和全部 210 组自动测试通过。** 本页是 0.20 发布后的独立证据补充，只归档这一场已有运行，没有重新触发它。**不代表此提交之后的可靠性修复或其他提交已经通过云端验收。**

## 运行身份与时间

| 项目 | 实际记录 |
| --- | --- |
| 源码 | [完整提交 beecbc09](https://github.com/Bad0RANG3/Anyvoc/commit/beecbc09b2524e26175e3534c3ac0b60f68d6cff) |
| Actions | [运行 44269099634](https://github.com/Bad0RANG3/Anyvoc/actions/runs/44269099634)，`main` 的 `push`，attempt 1，结论 `success` |
| Job | [windows / 134680903413](https://github.com/Bad0RANG3/Anyvoc/actions/runs/44269099634/job/134680903413)，九个步骤全部 `success` |
| 环境 | `windows-2022`，Visual Studio 2022，x64 Release；检出并验证固定 JUCE `29396c22c93392d6738e021b83196283d6e4d850` |
| Run 创建 | 2026-11-18 08:40:59 UTC，即北京时间 16:40:59 |
| Job 起止 | 08:41:02–08:52:10 UTC，即北京时间 16:41:02–16:52:10，共 11 分 08 秒 |
| 配置 | 08:41:19–08:42:46 UTC，`success` |
| Release 构建 | 08:42:46–08:49:11 UTC，`success` |
| 四套测试步骤 | 08:49:11–08:52:07 UTC，`success`；步骤时间包含 CTest 启动与收尾 |
| 附件 | `windows-test-evidence`，artifact ID **14412142319**，8,152 字节；创建于 08:52:08 UTC，核对时未过期 |

Run、Job 和 Artifact 的 head SHA 均与上述完整提交一致。下载 ZIP 的 SHA-256 为 `032e39f608737e44e6aa690b438f28553bee3892a390c0beb599068f58a863f6`，与 GitHub Artifact API 的 digest 完全一致；ZIP CRC、安全路径和路径唯一性检查通过。

## 210 组结果从哪里来

独立解析附件里的 JUnit，并逐段清点完整 `LastTest.log`：

| 测试程序 | 完整日志 PASS | FAIL | JUnit 耗时 |
| --- | ---: | ---: | ---: |
| EngineTests | 75 | 0 | 8.24985 秒 |
| ProcessorTests | 119 | 0 | 165.704 秒 |
| WavetableTests | 10 | 0 | 0.056744 秒 |
| PresetLibraryTests | 6 | 0 | 0.61357 秒 |
| 合计 | **210** | **0** | **174.624164 秒（约 174.62 秒）** |

完整日志还包含四个 `Test Passed.`。JUnit 根节点记录四个测试程序，`failures=0`、`disabled=0`、`skipped=0`，根时间为整数 174 秒；四个程序均为 `status="run"`，没有 failure、error 或 skipped 节点。四个 CTest 程序和 210 个内部测试组是不同计数单位。

**Engine 和 Processor 的 JUnit `system-out` 明确说明超过 1,024 字节的内容被移除。** 其中仅能看到 22／18 条 PASS，不能当成完整计数；75／119 来自未截断的 `LastTest.log`。另两套输出完整。云端用时与本机最终全套 198.69 秒分别保留，不互相替代。

## 仓库中的证据

| 文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| [完整原始日志](test-details-github-ci-0.20.txt)，来自附件 `Testing/Temporary/LastTest.log` | 19,222 | `e13c47524b0b4c9c6309d387c0b880ca2c77c3e0ed4d04ce69ab2d1fb7280f91` |
| [原始 JUnit](test-results-github-ci-0.20.xml)，来自附件 `test-results.xml` | 4,161 | `52c6c8302ea800c2775d33a2dc0be7c55639e741c44ce98b56f1fb67e3bdacac` |

上述两份文件按原字节复制，不改写时间、编码或换行；`.gitattributes` 仅为这两个精确路径关闭文本转换和空白检查。另提供[精简独立核验 JSON](verification-github-ci-0.20.json)，记录公开运行身份、摘要、测试计数来源和边界，不复制完整 API 响应。

附件里的 `Testing/Temporary/CTestCostData.txt` 为 111 字节，SHA-256 是 `f08813d93dbaccfd783e4a48f210a9efbb0f85450d4dbab80f8b4604db7307f6`。它与原 ZIP、完整 API 元数据保留在本机 `build/github-ci-0.20/`，没有添加到仓库。

## 适用范围

这次运行证明指定提交可在 Windows 干净环境构建并通过四套自动测试。工作流不执行 pluginval、原生 UI、真人听音、系统剪贴板、外接 MIDI 或 DAW 工程操作；这些证据分别见[本机 0.20 验证记录](VALIDATION-0.20.md)。后续修复必须按自己的提交重新验证，不能引用本页替代。

本次归档**没有重新生成或修改任何 0.20 发行 ZIP**。冻结发行包及其包内文档仍保持发布时的内容，外置包校验报告的原始结论不变；本页和新添原始证据没有写回旧包。GitHub 源码仓库也未因此增加 EXE、VST3、WAV、ZIP 或 Release 二进制附件。
