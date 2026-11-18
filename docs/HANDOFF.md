# 暂停开发交接

日期：**2026-11-18（Asia/Shanghai）**。用户要求收尾后暂时停止开发。当前停在 **0.20.4 候选版**；没有宣布完整交付，也没有达到完整 Serum 2 功能或声音等价。恢复开发需要用户重新提出要求，不自动继续性能优化或功能开发。

## 接手先看这几项

| 项目 | 交接状态 |
| --- | --- |
| 仓库 | [Bad0RANG3/Anyvoc](https://github.com/Bad0RANG3/Anyvoc)，以 `main` 最新提交为交接入口。最终提交身份用 `git log -1` 和远端 `main` 核对。 |
| 当前版本 | Windows x64，独立程序与 VST3；1,256 个宿主参数、状态 schema 14、28 个工厂音色。 |
| 本机独立版 | `dist/Anyvoc-0.20.4-Windows/Anyvoc.exe`，候选版。ZIP 应完整解压后运行。 |
| Windows ZIP | `dist/Anyvoc-0.20.4-Windows-x64.zip`。 |
| 完整源码 ZIP | `dist/Anyvoc-0.20.4-Source.zip`，包含固定 JUCE 源码。 |
| 最终包身份 | **以包外 `dist/Anyvoc-0.20.4-PackageVerification.json` 为准**，记录实际 ZIP 哈希、大小、文件集合及二进制版本；不把 ZIP 哈希写进包内文档。 |
| GitHub 下载 | 仓库提供源码、文档和验证证据，尚无二进制 Release 附件；GitHub 的 Download ZIP 不是可运行安装包，也不包含子模块源码。 |
| 声卡 | 用户确认其他程序正在独占声卡，尚未释放重验。没有关闭该程序。0.20.4 未做新的原生演奏或实际设备回调验收。 |

本机 `build/`、`dist/`、`tools/` 被 Git 忽略，**推送源码不会备份这些目录**。换电脑或重装前，如需保留现成程序，应另行保存上述两个最终 ZIP 和包外校验 JSON；不要只复制 EXE 或 VST3 内部模块。

## 已完成及证据

最近的生产代码提交为 **`6f0121b`**：ARP 开启时让引擎直接读取私有 MIDI 缓冲，保留其预分配存储；关闭时仍读取宿主缓冲，回调末尾仍清空宿主 MIDI。生产修复只涉及 `PluginProcessor.cpp/.h`，另将构建版本更新为 0.20.4。没有改变参数 ID、schema、工厂预设或合成算法。

| 验证 | 已确认结果 |
| --- | --- |
| 正式无插桩 Release | 构建通过，7 个 PE 文件版本均为 0.20.4；正常 `/MT` 构建没有诊断宏或强制包含。 |
| 自动测试 | **220/220（75/129/10/6）**，210.62 秒。 |
| VST3 pluginval | strictness 5，通过，seed `0xc83e3a`。不替代实际 DAW 验收。 |
| 演示回归 | 本轮重新渲染的 Legacy、Motion、Chaos、Macro 与冻结的 0.20.3 包逐字节相同，宏独立分析通过。 |
| 分配诊断 | 真正未修复与修复后的隔离构建各 9 案有效，12 个原始音频文件与输入 manifest 逐字节相同，3 案使用手写 MIDI 参考。五类有界 ARP 私有缓冲分配 **43→0**。 |
| 正向控制 | 键盘输入的宿主缓冲增长、长 SysEx 的 malloc 仍被捕获；不宣称全回调零分配、零锁或无掉音。 |
| 候选包 | Windows 与完整源码候选包完整性校验通过；以包含本交接文档的最终包外 JSON 为权威。 |
| CI | 本机测试与云端检查分开。0.20.3 的 `a956c5e3` CI 成功，不覆盖 0.20.4。最终推送的新 CI 结论须按其真实 head 单独核实，暂停交接不将运行中状态写成通过。 |

详细入口：[当前状态](STATUS.md)、[本版验收](VALIDATION-0.20.4.md)、[分配诊断方法](MIDI_ALLOCATION_PROFILING.md)。

已入库的冻结证据：

- [分配专项原件](midi-allocation-0.20.4/README.md)：79 文件，集合 SHA-256 `9d9ebcb740e79a7f218d541e9bb4321e56e24783723c6d050e38464f6f4c3174`；[外部清单](midi-allocation-archive-verification-0.20.4.json)。
- [正式构建验证原件](validation-0.20.4/README.md)：22 文件，集合 SHA-256 `1664bc1e44d3ebbc5c0d9caff5e7c69dffe465247e2f0fca78ad85d181fd1cc7`；[外部清单](validation-archive-verification-0.20.4.json)。

诊断 EXE 的项目版本号均为 **0.20.3**，它们比较的是随后纳入 0.20.4 的修复，不能与正式无插桩 0.20.4 EXE 互换。原件也保留了两次诊断构建失败和辅助核验器错误，不改写成首次全部成功。

## 未完成，恢复时先处理

1. **先恢复声卡输出，再验独立版。** 由用户释放已知独占程序后，按[测试指南](TESTING.md)检查输出设备、琴键、电平、发音、预设保存及重开。本轮没有新的听音结论。
2. **补正式实时测量。** 六种负载各三轮，用[实时测量方法](REALTIME_PROFILING.md)采集并独立分析；保留全部失败和超预算数据。0.20.2 的旧设备数据曾有超预算，0.20.4 的分配改善不能证明这些超预算已消失。
3. **核对最终推送的 CI。** 到 [Actions](https://github.com/Bad0RANG3/Anyvoc/actions)按完整 head SHA 找运行。当前 workflow 包含 220 组 CTest，以及独立的分析器 20、缓存比较器 12、运行器 7、MIDI 比较器 27 项检查；正常完整附件预期 7 成员。这些无设备检查不执行真实分配探针、听音或 DAW。
4. **再做 FL Studio 验收。** 完整 VST3 文件夹安装、扫描、MIDI/自动化、工程保存重开等仍待实际验证。先前系统插件目录写入需要管理员权限，用户已选择先使用独立版；不要据 pluginval 推断 FL Studio 已通过。
5. **再选择下一阶段。** 按[路线图](ROADMAP.md)恢复功能或性能工作。完整 Serum 2 等价、Serum 预设兼容、更多平台、MPE、多采样/SFZ、频谱声源和效果总线等均未完成。

下一项性能建议只停留在只读侦察：研究 `SynthEngine.cpp` 的 `updateVoiceControl` 是否可在同一采样共享振荡器 tuning、detune 和 unison 系数。**尚未实现、未证明为热点、没有加速结论**；恢复后应先保存真实旧二进制与输出，保持表达式类型和运算顺序，用同一工具链做旧/新原始音频比较。不要直接套用建议后宣称声音等价。

## 恢复开发的最短路径

安装 Visual Studio 2022 C++ Build Tools、Windows SDK、CMake ≥3.22、Git；脚本分析和打包另需 Python 3。固定 JUCE 为 **8.0.12 / `29396c22c93392d6738e021b83196283d6e4d850`**。

```powershell
git clone --recurse-submodules https://github.com/Bad0RANG3/Anyvoc.git
cd Anyvoc
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
```

已有检出先确认 `git status`、`git log -1` 与子模块身份，再创建工作分支；正常构建保持 `ANYVOC_BUILD_MIDI_ALLOCATION_PROBE=OFF`。完整命令和验收标准见 [BUILDING](BUILDING.md) 与 [TESTING](TESTING.md)。

不要分发隔离诊断树内的 EXE/插件，不要覆盖冻结的 0.20.2/0.20.3 包或历史证据。实际设备未恢复时可继续离线开发，但恢复开发本身须先得到用户的新指令。

## 本机保留位置

- `build/validation-midi-allocation-before/`、`before2/`、`before3/` 与 `after/`（均带 `validation-midi-allocation-` 前缀）：失败和有效的隔离源码、诊断二进制及构建记录。
- `build/validation-midi-allocation-before-capture/`、`build/validation-midi-allocation-after-capture/`：真实原始采集；已入库副本见专项档案。
- `build/validation-midi-allocation-tooling/`：正式验证、工具检查和打包日志；`packaging-first/` 保存首包及其报告，最终包以 `dist/` 为准。
- `docs/GITHUB_HANDOFF.md` 是 0.16 初始 GitHub 交接的历史记录，不能用来判断当前版本。

暂停后不保留主动开发或 CI 轮询任务。GitHub 自身可能继续执行最后一次推送自动触发的工作流；结果留待恢复时按实际提交核对。
