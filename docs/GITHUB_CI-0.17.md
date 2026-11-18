# Anyvoc 0.17 GitHub 构建证据

核对日期：2026-11-18（Asia/Shanghai）。已推送的 **0.17.0 源码提交 `f20c1aab4155d80486f5dd12b20fd0da0d6efcf3`** 在 GitHub 的 Windows 干净环境构建成功，四套自动测试全部通过。

| 项目 | 实际记录 |
| --- | --- |
| 源码 | [提交 f20c1aa](https://github.com/Bad0RANG3/Anyvoc/commit/f20c1aab4155d80486f5dd12b20fd0da0d6efcf3) |
| Actions | [运行 44253113677](https://github.com/Bad0RANG3/Anyvoc/actions/runs/44253113677)，结论 `success` |
| 环境与配置 | `windows-2022`，Visual Studio 2022，x64 Release |
| 依赖 | 干净检出，JUCE 固定 `29396c22c93392d6738e021b83196283d6e4d850` 并核验 |
| job 起止 | 2026-11-18 06:40:50–06:51:04 UTC，即北京时间 14:40:50–14:51:04，共 10 分 14 秒 |
| 四套测试 | Engine 61、Processor 98、Wavetable 10、Preset Library 6，共 **175 个 PASS，零失败** |
| 测试耗时 | 四套分别 8.04861、109.009、0.0558261、0.616376 秒，合计约 117.73 秒；JUnit 根节点按整数记录为 117 秒 |
| 原始附件 | `windows-test-evidence`，已下载并核对 JUnit 与完整日志 |

仓库保留[完整云端测试日志](test-details-github-ci-0.17.txt)和[JUnit 结果](test-results-github-ci-0.17.xml)。JUnit 有 4 个测试程序，`failures=0`、`skipped=0`；完整日志包含 175 个内部测试组的 PASS。二者的计数单位不同。

完整日志 SHA-256：`bbd70650884abc473faa73e894a90dc8b937309a5ab4d83c032a08c768c794fd`。日志保留下载字节，不重新格式化时间或换行。

此证据证明上述提交可从干净环境构建并通过自动测试。它不包含 pluginval、原生设备听音、FL Studio 或其他 DAW 的实际操作；这些项目以[本机 0.17 验证记录](VALIDATION-0.17.md)和[验收指南](TESTING.md)的各自状态为准。尚未验收的 0.18 工作不包含在本次结论中。

本次只补充发布之后取得的云端证据，不重新生成或覆盖本机 0.17 发行 ZIP。原包保持发布时的文档快照与原哈希；本页及云端日志是仓库后补记录。若以后需要包含这些新文档的发行包，应另行归档旧包、重新打包和校验，不能继续沿用旧 ZIP 哈希。
