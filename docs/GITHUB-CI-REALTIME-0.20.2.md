# 0.20.2 探针工具提交的 GitHub CI 失败记录

核对日期：2026-11-18。**提交 `63a8192c126ae7dc751110df3d93512dcb5e6550` 的这一次 CI 结论为 `failure`。** Release 构建、四套 CTest 内部 220 组和分析器 20 项自检通过；运行器 7 项测试中 6 项通过、1 项失败，因此不能把整场运行称为通过。

这里只跟踪并下载已经存在的运行，没有触发重跑。它对应新增探针及证据的提交，**不覆盖之后的曲率缓存优化或 0.20.3 修改，也没有在云端执行本机的实际设备长测**。

## 运行身份

| 项目 | 本次记录 |
| --- | --- |
| 源码 | [完整提交 63a8192](https://github.com/Bad0RANG3/Anyvoc/commit/63a8192c126ae7dc751110df3d93512dcb5e6550) |
| Actions | [运行 44286216206](https://github.com/Bad0RANG3/Anyvoc/actions/runs/44286216206)，`main` / `push`，attempt 1，`failure` |
| Job | [windows / 134739448543](https://github.com/Bad0RANG3/Anyvoc/actions/runs/44286216206/job/134739448543)，`failure` |
| 环境 | `windows-2022`，Visual Studio 2022 x64 Release；固定 JUCE `29396c22c93392d6738e021b83196283d6e4d850` |
| Run 创建 | 2026-11-18 10:31:28 UTC（北京时间 18:31:28） |
| Job 起止 | 10:31:31–10:44:32 UTC（北京时间 18:31:31–18:44:32），共 13 分 01 秒 |
| 配置 | 10:31:49–10:33:25 UTC，`success`；启用测试与 `ANYVOC_BUILD_REALTIME_PROBE=ON` |
| Release 构建 | 10:33:25–10:41:24 UTC，`success` |
| 四套 CTest | 10:41:24–10:44:26 UTC，步骤 `success` |
| 工具检查 | 10:44:26–10:44:29 UTC，步骤 `failure` |
| 附件上传 | 10:44:29–10:44:30 UTC，`success` |

Run、Job 与 Artifact API 的 head SHA 均为同一完整提交。10 个 Job 步骤里只有工具检查失败，其余 9 个成功。

## 失败内容与成功子集

运行器失败用例为 `test_competing_runner_cannot_enter_after_atomic_reservation`。云端日志在 `Tests/RealtimeRunnerTests.py:238` 报告：

```text
self.assertTrue(contender_attempted, "Runner must reserve before starting any capture")
AssertionError: False is not true : Runner must reserve before starting any capture
```

原始输出为 `Ran 7 tests in 0.144s`、`FAILED (failures=1)`。另外 6 项运行器测试为 `ok`。该用例使用临时假 EXE 和模拟子进程检查套件运行器，没有启动音频设备或执行 DSP。此处保留失败事实；后续诊断、修复或其他提交的结果不改写本次结论。此前本机 7/7 通过也不能替代这次云端失败。

同一步骤中，探针 `--help` 已成功输出，分析器 JSON 记录 **20 PASS / 0 FAIL**，20 条明细全部为 PASS。运行器失败后，工作流按设计抛出 `Profiler runner tests failed`，步骤退出码为 1。

四个 CTest 程序的完整原始日志独立计数如下：

| 测试程序 | LastTest PASS | FAIL | JUnit 耗时 |
| --- | ---: | ---: | ---: |
| EngineTests | 75 | 0 | 8.38822 秒 |
| ProcessorTests | 129 | 0 | 172.62 秒 |
| WavetableTests | 10 | 0 | 0.0564108 秒 |
| PresetLibraryTests | 6 | 0 | 0.623273 秒 |
| 合计 | **220** | **0** | **181.6879038 秒** |

JUnit 根节点为 4 个程序，failures / disabled / skipped 均为 0，根时间为整数 181 秒；四个 testcase 都为 `status="run"`，没有 failure、error 或 skipped 节点。完整 LastTest 包含四个 `Test Passed.`。Engine / Processor 的 JUnit 输出被截断，只可见 22 / 18 条 PASS，故 75 / 129 来自完整 LastTest，而非截断片段。

四个程序、220 个内部测试组、20 个分析器自检与 7 个运行器用例是不同计数单位；不合并成“全部通过”的总数。

## 下载与原字节核对

附件 `windows-test-evidence` 的 ID 为 **14420570954**，10,508 字节，创建于 2026-11-18 10:44:29 UTC，核对时未过期。下载 ZIP 的 SHA-256 为：

`139693cef1564060596bbd0b404242a3dbe419f8ea3a8aa6f34bb4f7c8c298f0`

它与 GitHub Artifact API digest 一致；ZIP CRC、安全路径、大小写无重复路径、非符号链接及**实际完整五成员集合**均已核对，没有沿用旧版三成员假设。

| 原附件成员／获取方式 | 仓库原件 | 字节 | SHA-256 |
| --- | --- | ---: | --- |
| `Testing/Temporary/LastTest.log` | [完整日志](realtime-ci-0.20.2-LastTest.log) | 20,054 | `ce821de0f63b379d4e11c32e47fb2a7ac37671ef9522864ebfa9abea10a1f6bd` |
| `test-results.xml` | [JUnit](realtime-ci-0.20.2-test-results.xml) | 4,162 | `84cfe9597731b2bc8ae2d65c5670f6a159c2d39c3b71e641c2d44b087b5fb803` |
| `Testing/Temporary/CTestCostData.txt` | [CTest 耗时原件](realtime-ci-0.20.2-CTestCostData.txt) | 112 | `6c3470d39f788f9ad3763e417c41433a672a488bcece36c1b552edd4f3aa445a` |
| `realtime-probe-analyzer-self-test.json` | [分析器自检](realtime-ci-0.20.2-analyzer-self-test.json) | 4,241 | `686b7300c6533879ad2fb55d3d216fb5b7dc4dcbdb10143132d2112ea742eccb` |
| `realtime-runner-tests.txt` | [运行器失败原件](realtime-ci-0.20.2-runner-tests.txt) | 1,849 | `bfa002790dc23c17cf5b7b9886881525ae7e7d365da515edd2e96a363848ec34` |
| `gh run view 44286216206 --log-failed` | [失败步骤完整输出](realtime-ci-0.20.2-failed-step.txt) | 9,815 | `051c05d19505e5ee89c316197e21d7ab72b808807a6ddcb44e612153f52c5844` |

最后一项是单独获取的失败步骤输出，不是 ZIP 的第六个成员。以上原件按字节复制，保留云端换行、编码和 ANSI 序列；`.gitattributes` 对这六个精确路径关闭文本转换。另附[独立核验 JSON](realtime-ci-0.20.2-verification.json)，其中 `evidence_consistency_verified=true` 指附件与记录核对一致，`overall_ci_success=false` 保留整场失败结论。原 ZIP、完整 API 元数据和观察记录留在本机 `build/github-ci-realtime-0.20.2/`。

## 证据边界

这次工作流编译了可选实际设备探针，但只执行 `--help` 和无设备工具测试。它没有测量云端或本机音频回调时限，没有执行 pluginval、原生界面、听音、外接 MIDI、DAW 或零分配检查。本机真实设备的 18 次记录仍独立见[设备报告](REALTIME-0.20.2.md)，原有 205 文件归档没有被本页修改。

本次没有修改或重新生成冻结的 0.20.2 Windows / Source 发行 ZIP，没有上传二进制 Release 附件，也没有触发失败运行的重跑。后续曲率缓存修改及工具测试修复应由对应提交与新的验证记录证明。
