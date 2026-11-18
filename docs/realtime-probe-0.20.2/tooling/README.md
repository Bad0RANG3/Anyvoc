# 测试工具保护检查

此目录的检查不属于正式 18 次 / 60 秒性能采集，不能并入 `table-rows.md` 的任何合计。

`guard-checks/` 是首次命名空间保护检查。两个实际子进程的输出和获胜进程的 1 秒原始采集均保留。随后 Python harness 读取包含中文设备名的 UTF-8 JSON 时使用 Windows 默认 GBK 解码，产生 `UnicodeDecodeError`；`harness-first-failure.json` 记录该 harness 失败。这不是探针或生产音频代码失败。

`guard-checks-after/` 用显式 UTF-8 解码后重新运行，保留完整原件。五项检查均完成：同名并发只有一方成功、获胜报告有效、已完成结果拒绝覆盖且原字节不变、遗留锁拒绝写入、无效 CLI 在生成采集原件前退出。获胜进程又执行了 1 秒静音设备采集，`simultaneous.analysis.json` 是其独立复算结果。因此前后共两次 1 秒短测，均排除在正式统计之外。

`check_probe_guards-before.py` 与 `check_probe_guards.py` 是两次实际使用的 helper 原件；`guard-checks-after-console.txt` 保留修正后输出。这些保护结果只适用于已执行的输出命名空间和 CLI 场景，不证明实时音频性能。

`RealtimeRunnerTests.py`、`runner-tests.txt` 和 `runner-tests.json` 记录套件运行器的 7 项无设备测试：完整套件原件、原目录保护、并发独占、启动失败、超时保留、运行中 EXE 被替换、两轮间 EXE 被替换。它们首次运行 7/7 通过（0.152 秒），使用临时假 EXE 和模拟子进程，真实调用生产 runner 的独占文件创建与清单/哈希逻辑；没有设备、DSP 或构建。摘要生成最初未识别 Windows CRLF 的 `Ran` 行，随后从同一原始日志正确解析，未重跑测试或改写该日志。

这 7 项不计入现有四套 CTest 的组数，也不计入分析器 20 项自检，更不计入正式 18 次回调性能记录。
