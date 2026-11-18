# Anyvoc 0.18 GitHub 构建证据

核对日期：2026-11-18（Asia/Shanghai）。已推送的 **0.18.0 源码提交 `45578ca2f770b21c99ec9aec59a3d9a0b0fc38e1`** 在 GitHub 的 Windows 干净环境构建成功，四套自动测试全部通过。以下证据来自既有运行，未为归档重新触发 CI。

| 项目 | 实际记录 |
| --- | --- |
| 源码 | [提交 45578ca](https://github.com/Bad0RANG3/Anyvoc/commit/45578ca2f770b21c99ec9aec59a3d9a0b0fc38e1) |
| Actions | [运行 44257671955](https://github.com/Bad0RANG3/Anyvoc/actions/runs/44257671955)，结论 `success` |
| job | [windows / 134643158258](https://github.com/Bad0RANG3/Anyvoc/actions/runs/44257671955/job/134643158258) |
| 环境与配置 | `windows-2022`，Visual Studio 2022，x64 Release |
| 依赖 | 干净检出，JUCE 固定 `29396c22c93392d6738e021b83196283d6e4d850` 并核验 |
| job 起止 | 2026-11-18 07:17:11–07:27:49 UTC，即北京时间 15:17:11–15:27:49，共 10 分 38 秒 |
| 构建步骤 | 07:19:09–07:25:32 UTC，`success` |
| 测试步骤 | 07:25:32–07:27:44 UTC，`success`；步骤时间包含 CTest 启动与收尾 |
| 四套测试 | Engine 68、Processor 104、Wavetable 10、Preset Library 6，共 **188 个 PASS，零失败** |
| 原始附件 | `windows-test-evidence`，artifact ID `14407272318`，下载时大小 7,539 字节，未过期 |

独立解析下载的 JUnit，并逐段清点完整日志中的内部 PASS：

| 测试程序 | 内部 PASS | JUnit 耗时 |
| --- | ---: | ---: |
| EngineTests | 68 | 8.28849 秒 |
| ProcessorTests | 104 | 122.014 秒 |
| WavetableTests | 10 | 0.0571748 秒 |
| PresetLibraryTests | 6 | 0.616009 秒 |
| 合计 | **188** | **130.9756738 秒（约 130.98 秒）** |

JUnit 根节点有 4 个测试程序，`failures=0`、`disabled=0`、`skipped=0`，时间按整数记录为 130 秒。完整日志有四个 `Test Passed.` 和 188 个内部 PASS，零个内部 FAIL。JUnit 程序数与日志内部测试组数的单位不同；本机 161.94 秒的记录仍独立保留，没有用云端耗时替换。

仓库保留以下下载原件，不改写时间、编码或换行；`.gitattributes` 对这两个精确路径禁用文本换行转换。复制后的文件与下载原件逐字节一致。

| 文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| [完整云端测试日志](test-details-github-ci-0.18.txt)（附件 `Testing/Temporary/LastTest.log`） | 17,230 | `a7ebe79e4ee18a365f547cf9037aec388a5f10ce5ead7e681f26a1b7a7cdbdbd` |
| [JUnit 结果](test-results-github-ci-0.18.xml)（附件 `test-results.xml`） | 4,163 | `0584a190091f57af72145596fc565e1953ceaf67526b968ecea2f2bea8419d6b` |

此证据证明上述提交可从干净环境构建并通过自动测试。工作流不运行 pluginval、不检查操作系统剪贴板、不执行真人听音、外接 MIDI、完整音色原生保存重开、FL Studio 或其他 DAW 的实际操作。这些项目以[本机 0.18 验证记录](VALIDATION-0.18.md)和[验收指南](TESTING.md)的各自状态为准，不能由绿色 CI 扩展验收范围。其他提交和后续未完成的功能不包含在本次结论中。

本次只补充发布之后取得的云端证据，**不重新生成或覆盖本机 0.18 发行 ZIP**。原 Windows 包 208 个文件、源码包 4,600 个文件，保持发布时的文档快照与外置校验报告中的原哈希；本页、原始云端日志和相关文档修订是仓库后补记录，并未写回原 ZIP。若以后需要包含这些新文档的发行包，应另行归档旧包、重新打包和校验，不能继续沿用旧 ZIP 哈希。0.17 的独立历史证据见 [GITHUB_CI-0.17.md](GITHUB_CI-0.17.md)。
