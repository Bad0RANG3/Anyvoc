# Anyvoc 验证记录入口

当前源码为 **0.20.4 候选版**。[本版验证](VALIDATION-0.20.4.md)记录 ARP 私有 MIDI 缓冲修复：真实隔离两侧 9 案有效、3 手写 oracle 精确，五类有界私有调用 43→0，12 个音频文件及 fixture manifest 逐字节一致。诊断 CMake 均为 0.20.3；正式无插桩 0.20.4 Release、220 组（210.62 秒）、pluginval strictness 5（seed `0xc83e3a`）、四演示对冻结 0.20.3 的字节回归和宏分析已通过。Windows／完整源码候选包完整性校验 PASS；候选入口为 `dist/Anyvoc-0.20.4-Windows/Anyvoc.exe`，最终结果以包外 `dist/Anyvoc-0.20.4-PackageVerification.json` 为准。最新推送 CI 须按实际 head 另核，暂停时未确认。方法见 [MIDI 分配诊断](MIDI_ALLOCATION_PROFILING.md)。

用户确认其他程序独占声卡，尚未释放重测；本轮没有原生演奏或实际实时通过结论。范围内分配结果不等于全回调零分配、零锁或无掉音。

- [0.20.3 冻结候选记录](VALIDATION-0.20.3.md)：精确曲率缓存、完整本机 220 组与 27 例 raw 回归、候选包校验及原生设备受阻。
- [0.20.3 指定 a956 云端验证](GITHUB-CI-0.20.3.md)：SUCCESS，220 组及独立工具 20／12／7 项通过，不覆盖本次 ARP 修改。
- [0.20.2 已交付版本](VALIDATION-0.20.2.md)：ARP 通道及重复按键所有权；冻结本机目录和 ZIP 保留。
- [0.20.2 发布后真实设备回调测量](REALTIME-0.20.2.md)：六种固定负载各 60 秒、三轮，18 次数据有效；正式块中有 206 次超预算。不改称 0.20.3 的测量，也不宣称实时无掉音。
- [0.20.2 指定提交 `2acc71db` 云端验证](GITHUB-CI-0.20.2.md)：220 PASS、0 FAIL，约 179.53 秒；仅适用于该 head。
- [`63a8192` 探针工具提交的云端失败](GITHUB-CI-REALTIME-0.20.2.md)：220 组 CTest 和 20 项分析器通过，运行器 6/7，整场 failure。其原始失败证据不因后续本地 fixture 修复而改写。


- [0.20.1 本机验收](VALIDATION-0.20.1.md)及[指定提交云端验证](GITHUB-CI-0.20.1.md)：CC121 踏板重置修复，213 组自动回归。

- [0.20 本机验收](VALIDATION-0.20.md)：PLAY、COPY 与完整路由发布。
- [0.20 指定提交云端验证](GITHUB-CI-0.20.md)：完整日志 210 PASS、0 FAIL。
- [0.20 发布后原生退出／重启／采样恢复](NATIVE_RESTART-0.20.md)：真实进程与独立存储数据核验。
- [0.19 验收](VALIDATION-0.19.md)及[云端记录](GITHUB_CI-0.19.md)。
- [0.18 验收](VALIDATION-0.18.md)及[云端记录](GITHUB_CI-0.18.md)。

历史记录只适用于各自的版本、提交、二进制和步骤。复现命令及通过标准见[测试指南](TESTING.md)，当前能力与后续缺口见[项目状态](STATUS.md)和[路线图](ROADMAP.md)。
