# GitHub 初始交接记录

日期：2026-11-18（Asia/Shanghai）。目标仓库：[Bad0RANG3/Anyvoc](https://github.com/Bad0RANG3/Anyvoc)，主分支 `main`。

这次交接保存 **0.16.0 已验证的合成器实现**，补齐开发文档和 GitHub 构建检查。未增加新合成功能，没有重新打包或覆盖已发布的 0.16 EXE/VST3/ZIP。

## 本次提交范围

- 生产源码、四套测试、CMake、AGPL 许可和第三方说明。
- README 文档入口；STATUS、ROADMAP、TESTING、BUILDING；原有长篇使用说明移到 USER_GUIDE，保留三步开始和历史验证记录、74 张预览。
- JUCE 子模块固定在 `29396c22c93392d6738e021b83196283d6e4d850`。
- Windows GitHub Actions：干净检出、配置、Release 构建、四套 CTest 和测试日志附件。
- 构建脚本改为初始化固定子模块并检查 commit；源码打包脚本纳入仓库配置；Windows 打包不再强制要求首次克隆没有的历史 0.2 WAV，仍要求 0.16 LFO motion 演示。

Git 排除构建目录、发布产物、外部测试工具、本机设置与凭据。当前仓库不提供二进制 Release 附件，普通 “Download ZIP” 也不包含 JUCE 子模块；请按 BUILDING 克隆及构建。

## 基线确认与新证据

整理前，生产 `Source/` 全部 14 个文件、`Tests/` 全部 4 个文件及 `CMakeLists.txt` 均与原 `Anyvoc-0.16.0-Source.zip` 内容逐字节比对通过。Git 统一文本换行不改变代码含义。

初次整理时，0.17 的生产实现尚未开始；ProcessorTests 中少量未完成的 schema13/数量草稿先保存在本机被 Git 忽略的开发目录，再恢复为已验证的 schema12 测试。它们不纳入 0.16 基线，没有任何混沌 LFO 可用性声明。

本机使用既有已验证二进制重跑了四套 CTest：Engine、Processor、Wavetable、PresetLibrary 全部通过，**112.05 秒，共 162 组**。见[完整复测摘要](ctest-github-handoff-0.16.txt)及[详细日志](test-details-github-handoff-0.16.txt)。这次复测不等于从干净环境重新构建；云端构建结果须另看[当前 Actions](https://github.com/Bad0RANG3/Anyvoc/actions)。

推送完成后，GitHub Actions 对提交 `43cf082217a7799da0312b83ad58c6070bd36de6` 的干净检出、固定 JUCE、Release 构建和四套测试均成功：[对应运行](https://github.com/Bad0RANG3/Anyvoc/actions/runs/44241309810)。JUnit 记录 4 套、0 失败，四套累计约 124 秒；下载的[完整云端测试日志](test-details-github-ci-0.16.txt)含 162 个 PASS。整个 job 用时 12 分 31 秒。初始提交 `804f547` 的运行也成功；该补充证据仍针对 0.16，不能用于证明后续功能分支。

本机保留二进制的 SHA-256 未变：

| 文件 | SHA-256 |
| --- | --- |
| 独立版 `Anyvoc.exe` | `2c8b987bdfedd833bd4b01ca6c5fec4f6d1f2285af4c5179714016dae9611046` |
| VST3 内部模块 | `6cbfeb6340f40f59e6beb11eb9a0b45d3b251c705755e8c3e86619ec81f9cbff` |

文档相对链接、代码围栏、Git 空白检查及常见凭据模式检查已执行。这里的检查不能替代源码安全审计。GitHub 初始推送后应核对远端 `main` 与本地提交 SHA 一致，并查看对应工作流结果；发生失败时保留失败记录，再做有依据的修复。

补充修正：Windows PowerShell 5.1 在本机代码页下会误读无 BOM 脚本中的中文文件名，打包脚本已保存为带 BOM 的 UTF-8。随后在隔离目录使用真实 0.16 二进制和 LFO 演示执行打包，刻意不放历史 0.2 WAV；打包成功，ZIP CRC、无重复路径、全部输出字节与中文快速开始文件均通过检查，见[打包流程检查](package-smoke-handoff.json)。这只是脚本验收，没有替换原发行包。

## 接手后怎么继续

先读 [STATUS](STATUS.md)，按 [BUILDING](BUILDING.md)构建，在 [TESTING](TESTING.md)记录当前环境的结果，再按 [ROADMAP](ROADMAP.md)选择一个阶段。0.16 初次交接时的下一步是完成 Chaos LFO；后续 0.17 的实际实现与验收单独记录在 [0.17 验证报告](VALIDATION-0.17.md)，不改写本页的历史基线。

DAW 扫描、真实音频设备、完整听音、macOS/AU/AAX 和完整 Serum 2 等价均仍未完成，不能由本次推送或 CI 成功推导出来。
