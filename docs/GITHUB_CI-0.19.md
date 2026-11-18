# Anyvoc 0.19 GitHub 构建证据

核对日期：2026-11-18（Asia/Shanghai）。已推送的 **0.19.0 源码提交 `bf3efc6ecde3044409ec6693c6b98f7eb65371b3`** 在 GitHub 的 Windows 干净环境构建成功，四套自动测试共 **201 组通过，零失败**。本页归档该次既有运行，未重新触发或重跑它；不修改已经交付的本机发行 ZIP。

| 项目 | 实际记录 |
| --- | --- |
| 源码 | [提交 bf3efc6](https://github.com/Bad0RANG3/Anyvoc/commit/bf3efc6ecde3044409ec6693c6b98f7eb65371b3) |
| Actions | [运行 44262489842](https://github.com/Bad0RANG3/Anyvoc/actions/runs/44262489842)，`main` 的 `push`，attempt 1，结论 `success` |
| job | [windows / 134658942758](https://github.com/Bad0RANG3/Anyvoc/actions/runs/44262489842/job/134658942758)，九个步骤全部 `success` |
| 环境与配置 | `windows-2022`，Visual Studio 2022，x64 Release |
| 依赖 | 干净检出，JUCE 固定 `29396c22c93392d6738e021b83196283d6e4d850` 并核验 |
| run 创建 | 2026-11-18 07:54:06 UTC，即北京时间 15:54:06 |
| job 起止 | 07:54:09–08:08:08 UTC，即北京时间 15:54:09–16:08:08，共 **13 分 59 秒** |
| 配置步骤 | 07:54:25–07:56:09 UTC，`success` |
| 构建步骤 | 07:56:09–08:04:39 UTC，`success` |
| 测试步骤 | 08:04:39–08:08:05 UTC，`success`；步骤时间含 CTest 启动与收尾 |
| 原始附件 | `windows-test-evidence`，artifact ID **`14409303373`**，下载时 **7,839 字节**，未过期 |

下载附件前已分别核对 run、job 和 artifact 中的 head SHA，均为上面的完整提交。ZIP CRC 通过，原 ZIP SHA-256 为 `9f9ad0659b8779661e4feaa2426f69f6f8b0643eed7f9befb680b831f8c35106`，与 GitHub artifact API 发布的 digest 完全一致。

独立解析 JUnit，并逐段清点完整 `LastTest.log` 的内部 PASS：

| 测试程序 | 完整日志内部 PASS | JUnit 耗时 |
| --- | ---: | ---: |
| EngineTests | 75 | 10.8073 秒 |
| ProcessorTests | 110 | 193.076 秒 |
| WavetableTests | 10 | 0.0684229 秒 |
| PresetLibraryTests | 6 | 0.635682 秒 |
| 合计 | **201** | **204.5874049 秒（约 204.59 秒）** |

JUnit 根节点有 4 个测试程序，`failures=0`、`disabled=0`、`skipped=0`，根时间按整数记录为 204 秒。每个程序均为 `status="run"`，没有 failure/error/skipped 节点。**Engine 和 Processor 的 JUnit `system-out` 有明确的 1024 字节截断提示，不能用其中的 PASS 行数统计完整测试组。** 201 的计数来自附件中未截断的 `LastTest.log`，四段分别为 75/110/10/6；完整日志同时有四个 `Test Passed.`、零个内部 FAIL。

上述云端耗时与本机完整 192.89 秒及文字修正后的 Processor 定向补测 184.39 秒各自保留，未互相替换。四个 CTest 程序和 201 个程序内部测试组是不同单位，不能相加或混称。

仓库保留以下下载原件，不改写时间、编码或换行；`.gitattributes` 对两个精确路径禁用文本换行转换。复制后的文件与下载原件逐字节一致。

| 文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| [完整云端测试日志](test-details-github-ci-0.19.txt)（附件 `Testing/Temporary/LastTest.log`） | 18,318 | `9da44c7f32fd0786b5dc2efb1f56add2c9ab6565337e77dfdee97e06f209e015` |
| [JUnit 结果](test-results-github-ci-0.19.xml)（附件 `test-results.xml`） | 4,163 | `1e181ef8fc8834dd02db21d1a2b221129e4e3fd758480c3adcfb67d87e9bfd81` |

附件还含 113 字节的 `Testing/Temporary/CTestCostData.txt`，SHA-256 为 `09da4fe04e5931d64b902bb5c4500d39c6c948499674fa981d133b39d623a0d1`；它与原 ZIP、API 元数据及独立解析报告保留在本机 `build/github-ci-0.19/`，不属于仓库中的两份原始测试记录。

运行有一条非失败的平台提示：固定的 checkout/upload-artifact Actions 声明 Node.js 20，平台强制使用 Node.js 24；本次各步骤实际成功，归档没有修改工作流。该提示不作为功能或测试失败，也不隐去。

此证据只证明指定提交可从干净环境构建并通过四套自动测试。工作流不执行 pluginval、真人听音、OS 剪贴板、外接 MIDI、原生音色文件对话框或 FL Studio 等 DAW 操作；这些仍以[本机 0.19 验证记录](VALIDATION-0.19.md)和[验收指南](TESTING.md)的各自状态为准。其他提交和未完成的后续功能不包含在本次结论中。

本次是交付后的仓库证据补充，**不重新生成或覆盖 0.19 发行 ZIP**。原 Windows 包 **246 文件**、源码包 **4,638 文件**继续保存 `bf3efc6` 提交时的文档内容与外置校验报告中的原哈希；本页、两份云端原始记录及相关文字更新没有写回原包。校验器的“当前文档逐字节匹配”是打包时冻结输入的结论，不能拿后补文档去声称旧包已经包含它们。今后若确需包含新文档，须先归档旧包，另行打包和核验，不能继续引用旧包哈希。本机没有新增 GitHub Release 二进制附件。[0.18 云端证据](GITHUB_CI-0.18.md)与更早历史保持独立。
