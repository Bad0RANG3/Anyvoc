# Anyvoc 0.20.3 指定提交的 GitHub CI 验证

核对日期：2026-11-18。**提交 `a956c5e3fc1dfb52d269650c3e75ff370289af82` 的运行 44292647323 成功完成。** Release 构建、四套 CTest 内部 **220 组（75／129／10／6）**、分析器 **20 项**、缓存比较器 **12 项**及运行器 **7 项**均通过。后三类为独立的无设备工具检查，不并入 220 组。

这里只观察同一次自动运行，没有触发重跑。结论限于该完整 head 的云端构建与自动检查，**不表示本机原生演奏、真实声卡期限或 0.20.3 整体交付已通过**，也不替后续文档提交或候选包背书。

## 运行身份与时间

| 项目 | 本次证据 |
| --- | --- |
| 源码 | [完整提交 a956c5e3](https://github.com/Bad0RANG3/Anyvoc/commit/a956c5e3fc1dfb52d269650c3e75ff370289af82) |
| Actions | [运行 44292647323](https://github.com/Bad0RANG3/Anyvoc/actions/runs/44292647323)，`main` / `push`，attempt 1，`success` |
| Job | [windows / 134761494604](https://github.com/Bad0RANG3/Anyvoc/actions/runs/44292647323/job/134761494604)，`windows-2022`，`success` |
| 依赖与构建 | 固定 JUCE `29396c22c93392d6738e021b83196283d6e4d850`；Visual Studio 2022 x64 Release，开启测试和可选探针 |
| Run 创建 | 2026-11-18 11:13:04 UTC（北京时间 19:13:04） |
| Job 起止 | 11:13:07–11:29:35 UTC（北京时间 19:13:07–19:29:35），16 分 28 秒 |
| 配置 | 11:13:23–11:15:07 UTC，成功 |
| Release 构建 | 11:15:07–11:25:35 UTC，成功 |
| 四套 CTest | 11:25:35–11:29:25 UTC，成功 |
| 无设备工具检查 | 11:29:25–11:29:31 UTC，成功 |
| 附件上传 | 11:29:31–11:29:32 UTC，成功 |

Run、Job 和 Artifact API 的 head 身份一致；10 个 Job 步骤全部成功。观察进程每约 55 秒读取同一运行，11:29:45 UTC 确认终态并正常退出。没有切换到后续文档提交触发的其他运行。

## 完整测试与工具结果

| 程序 | 完整 LastTest PASS | FAIL | JUnit 秒数 |
| --- | ---: | ---: | ---: |
| EngineTests | 75 | 0 | 10.9261 |
| ProcessorTests | 129 | 0 | 217.733 |
| WavetableTests | 10 | 0 | 0.0703826 |
| PresetLibraryTests | 6 | 0 | 0.638116 |
| 合计 | **220** | **0** | **229.3675986** |

组数逐一来自完整 `LastTest.log` 的 PASS／FAIL 行，四段各有 `Test Passed.`。JUnit 为 4 个程序，failure／error／disabled／skipped 均为 0，根时间字段为整数 229 秒；上表为四个 testcase 的实际时间之和，约 229.37 秒。Engine／Processor 的 JUnit 输出被截断，只可见 22／18 条 PASS，因此不能用这些截断片段计数 75／129。云端时间与本机最终 218.75 秒分别记录，不混称同一次测量。

- 分析器自检：**20 PASS／0 FAIL**，20 条具名明细全部 PASS；检查合成 trace、warmup、分位、预算及无效输入。
- 缓存比较器自检：**12 PASS**，12 条独立具名检查；含单字节改变、NaN、截断、静音、文件集合和 manifest 等负例。使用合成文件，没有在云端生成或比较本机的 27 例旧／新 DSP 输出。
- 运行器：**7／7，0.175 秒**，逐用例均为 `ok`，终行为 `OK`；包括 `test_competing_runner_cannot_enter_after_atomic_reservation`。

[旧 `63a8192` 运行](GITHUB-CI-REALTIME-0.20.2.md)中的该运行器用例曾失败。当前提交包含路径规范化及固定别名覆盖，这次相同用例通过；旧失败与本地 junction 复现记录保留，不改写成旧 CI 成功。运行器使用临时假 EXE／模拟子进程；分析器与比较器使用合成数据。工作流同时检查探针 `--help`，不申请音频设备。

## 六成员附件与原始字节

`windows-test-evidence` 附件 ID **14422967885**，创建于 2026-11-18 11:29:32 UTC，下载时未过期，ZIP **10,718 字节**。下载 SHA-256：

`e24ef5dce4c53f072e33ae9b8c27a59edce0550bf52189dfdc45b76881dc0148`

与 GitHub API digest 完全一致。已独立检查 ZIP CRC、非绝对／无越界路径、无符号链接、大小写无重复路径，以及以下**精确六成员集合**，没有沿用历史三／五成员假设。全部原件按字节复制，不重排 JSON、不转换换行或清理日志。

| 原附件成员 | 仓库原件 | 字节 | SHA-256 |
| --- | --- | ---: | --- |
| `Testing/Temporary/CTestCostData.txt` | [github-ci-0.20.3-CTestCostData.txt](github-ci-0.20.3-CTestCostData.txt) | 113 | `ff6a4c099db87ecf1056733c779dfeeb98299144cee012b338ed6fe4d1fc5d8f` |
| `Testing/Temporary/LastTest.log` | [github-ci-0.20.3-LastTest.log](github-ci-0.20.3-LastTest.log) | 20,054 | `4314f37a5c1c6f6130fc5669257e5f3cb2e0f994b2bea8cc46388981ae3e86ec` |
| `test-results.xml` | [github-ci-0.20.3-test-results.xml](github-ci-0.20.3-test-results.xml) | 4,163 | `cb972b63c205034426ea52395af4a6c05748010f2b1ec4d596abcc649ac4b20a` |
| `realtime-probe-analyzer-self-test.json` | [github-ci-0.20.3-analyzer-self-test.json](github-ci-0.20.3-analyzer-self-test.json) | 4,241 | `3c98a33f26a390428f73ef518087d9020f2f966e848c93f3f2a4863bb28571be` |
| `cache-comparator-self-test.json` | [github-ci-0.20.3-comparator-self-test.json](github-ci-0.20.3-comparator-self-test.json) | 489 | `e545127e55fd737f0fd90eb5f296e803eb1bb6b04c5ad9de5d10fc1492fbcb8f` |
| `realtime-runner-tests.txt` | [github-ci-0.20.3-runner-tests.txt](github-ci-0.20.3-runner-tests.txt) | 1,180 | `114f23567b3365fc131d3966558bf7312d930f0a63ffc6e2e4ac1eb0e3ec3a0a` |

[独立核验 JSON](github-ci-0.20.3-verification.json)保存 run/job 身份、步骤、API digest、成员哈希、完整日志计数与工具结果。`.gitattributes` 对六个原件使用精确路径 `-text -whitespace`，保留字节；原 ZIP、完整 API 元数据、观察记录及核验脚本留在本机 `build/github-ci-0.20.3/`。

## 适用范围

本次没有执行 pluginval、原生界面演奏、真人听音、外接 MIDI、DAW、实际音频回调或零分配检查，也没有运行最终离线 benchmark、27 例两侧渲染或发行包校验。它不能解除本机“用户确认其他程序独占声卡、尚待释放后重测”的验收限制。

[0.20.3 本机候选报告](VALIDATION-0.20.3.md)与[缓存专项 161 文件归档](modulation-cache-0.20.3/README.md)属于独立证据，本次未改写它们。0.20.2 的历史 CI、失败 CI、设备归档及冻结发行包也未改变。本报告仅后补指定 `a956c5e3` 的云端事实；后续 `a8b72e85` 等提交和候选 ZIP 的内容／校验应使用各自记录，不因为生产源码相同就冒称已由本 run 完整检查。
