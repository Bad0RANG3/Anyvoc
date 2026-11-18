# 0.20 原生退出、离线素材与音色恢复核对

2026-11-18，在 Windows 独立版 **0.20.0** 上完成了一次包含导入样本和调制的真实 `SAVE → 退出进程 → 原素材改名 → 重启 → INIT → LOAD → SAVE`。保存前后，1256 个参数的实际数值和其他声音状态字段一致；唯一内嵌 SAMPLE 的身份与完整 WAV 字节一致。原始 PCM16 与内嵌 PCM24 的 96000 帧也全部符合独立计算的 JUCE 转换规则。

本页补充 [0.20 验收报告](VALIDATION-0.20.md)。这次使用的是已发行的原版 `dist/Anyvoc-0.20.0-Windows/Anyvoc.exe`，SHA-256 为 `c366f6a6e829fa30d424252dae802024257a69d19f2388a0d7db0d947a25dcb9`。**本流程未使用 0.20.1，不能作为 0.20.1 的原生验证记录。**

## 原生操作与重启边界

操作记录、进程时间、设置恢复散列及本地原始证据的散列收录在 [精简原生记录](native-restart-0.20.json)。大型 UI 树、设置内容、原始音频和两个 `.anyvoc` 文件均仅在本地保留。

1. 第一进程 PID **209108**，启动于 `2026-11-18 16:43:20 +08:00`。导入自有生成的 `Restart-Source.wav`，为 2 秒、48 kHz、PCM16、单声道，共 96000 帧。SOURCE LAB 设置 START **17%**、ROOT **C4 / MIDI 60**、LOOP **On**。
2. 保存前 MOD 前两行分别为 `LFO 1 → Filter A Cutoff`，深度 **+25%** 和 **−10%**。通过原生保存对话框保存 `Native20-Embedded.anyvoc`。
3. 关闭第一进程。`16:49:17 +08:00` 的边界检查确认旧进程已经结束；源文件改名为 `Restart-Source.offline.wav`，**原 `Restart-Source.wav` 路径不存在**。改名后文件 SHA-256 仍为 `2afb4a71bb54fb14aee36db536d2aec81ddee8cb4de497e2241392fa94d7dc92`。
4. 新进程 PID **224724**，启动于 `16:49:31 +08:00`。独立版会自动恢复上次会话，因此先明确执行 **INIT**，再到 SYNTH 确认样本区显示 `Drop a WAV sample` 和空波形，到 MOD 确认可见路由为 Off。这一步避免把自动恢复误当成手动 LOAD 的结果。
5. 原源文件路径仍离线时，通过原生对话框手动 **LOAD `Native20-Embedded.anyvoc`**。SYNTH 的 Restart-Source 波形与 START 17% 恢复；SOURCE LAB 确认 ROOT C4 / MIDI 60、LOOP On；MOD 两条路由及深度恢复。
6. 点击屏幕键盘 **C4**，当时视觉观察记录了 C4 高亮、EV `latest voice` 和绿色输出表。随后原生 SAVE 为 `Native20-Restored.anyvoc`。
7. 经 UI 关闭第二进程后，恢复测试前设置。备份与恢复后的 SHA-256 均为 `5000f1074d6b885621afabb357a32e2ba2050f8661ad38c3fb92bc520674ab7e`；清理记录确认测试进程已退出。本页不公开设置文件内容。

以上绿色输出表是界面输出观察，**不是人工听音结论，也不是对捕获的实际播放音频做 PCM 比较**。

## 独立文件比较

[verify-preset-roundtrip.py](../scripts/verify-preset-roundtrip.py) 只使用 Python 标准库，不导入插件生产代码。它独立解析 `.anyvoc` XML、JUCE 的长度前缀及低位优先编码、RIFF/WAVE 分块和 PCM。完整最终 JSON 原字节归档为 [preset-roundtrip-0.20.json](preset-roundtrip-0.20.json)。

| 核对项 | 结果 |
| --- | --- |
| XML schema | 两文件均为 14 |
| 参数 | 各 1256 个唯一 PARAM；ID 集合、实际浮点值及其他属性一致，不使用数值容差 |
| 其他根状态 | 除允许随保存文件改变的 `presetName`、`presetId` 外一致 |
| 素材 | 各 1 个 `SAMPLE osc=0`，名称 Restart-Source；本次没有 WAVETABLE 资产 |
| 内嵌 WAV | 48 kHz、PCM24、单声道、96000 帧；完整 288104 字节逐字节一致 |
| 原始 PCM 对照 | 96000 帧；精确转换不匹配数 0；最大及均方根归一化误差均为 0 |

两份 `.anyvoc` 本身的文件散列不同，因为保存名称和身份改变；这不会被误报成声音状态不同。内嵌 WAV 的共同 SHA-256 为 `e32b91837e840af1b6885c7c59d0bcf8c1b677b732693616809884024f43f550`。

量化规则来自固定 JUCE 8.0.12 的读取和写入实现：PCM16 整数除以 32768，写出前按 `2147483647 × sample` 转为最近偶数舍入的有符号 PCM32，再算术右移 8 位写 PCM24，−1 单独饱和到 INT32_MIN。脚本不仅检查理论上最多一个 PCM24 LSB 的误差界限，还要求**每个 PCM24 整数精确等于该规则的结果**。本素材实际误差为零，未因数据结果放宽容差。

## 复跑命令与负例

以下命令在仓库根目录执行，前提是保留了本机 `build/validation-restart-0.20` 中的输入。**这些 `.anyvoc`、WAV 和负例副本不上传 Git；仅克隆仓库不能重跑这组特定输入。** 脚本可用于另行生成的同格式样本和保存文件，但不代表复现了这里的原生操作。

```powershell
python scripts/verify-preset-roundtrip.py `
  --before build/validation-restart-0.20/Native20-Embedded.anyvoc `
  --after build/validation-restart-0.20/Native20-Restored.anyvoc `
  --source-wav build/validation-restart-0.20/Restart-Source.offline.wav `
  --osc 0 `
  --output build/validation-restart-0.20/preset-roundtrip-rerun.json
```

预期退出码 **0 / PASS**。`--source-wav` 可省略；提供时仅支持本轮的 48 kHz PCM16 单声道到内嵌 PCM24 单声道，不验证其他采样率的重采样。

两种故意改坏的副本均只写在 `build`，原始输入保持不变。结果归档为 [负例核对记录](preset-roundtrip-negative-controls-0.20.json)。

- **合法范围内参数变动**：从 Restored 副本把 `mod1Amount` 从 `0.25` 改为 `0.26`。1256 个 ID 不变、素材完全不变；检查器准确报告只有这一参数不同，退出码 **1 / FAIL**。
- **素材单字节损坏**：从另一个 Restored 副本，只将 SAMPLE0 的有效 PCM 数据第 100 帧最低字节异或 1（本文件 WAV 偏移 404）。重新按 JUCE 编码写入后，XML、WAV 结构、素材元数据与全部参数仍合法且未变；检查器发现 WAV 字节不同，以及恰好 1 帧不符合原始 PCM 转换，退出码 **1 / FAIL**。

```powershell
python scripts/verify-preset-roundtrip.py `
  --before build/validation-restart-0.20/Native20-Embedded.anyvoc `
  --after build/validation-restart-0.20/negative-controls/Changed-Parameter-Legal.anyvoc `
  --source-wav build/validation-restart-0.20/Restart-Source.offline.wav `
  --osc 0 `
  --output build/validation-restart-0.20/negative-parameter-rerun.json

python scripts/verify-preset-roundtrip.py `
  --before build/validation-restart-0.20/Native20-Embedded.anyvoc `
  --after build/validation-restart-0.20/negative-controls/Changed-Asset.anyvoc `
  --source-wav build/validation-restart-0.20/Restart-Source.offline.wav `
  --osc 0 `
  --output build/validation-restart-0.20/negative-asset-rerun.json
```

原始 PASS/FAIL JSON 与标准输出均保留在本地；归档 JSON 记录其散列和退出码。最初额外执行过 `mod1Amount 0.25 → 1.25` 的参数负例，其输出也保留，正式负例采用上面的合法范围内变动。此处故意触发的 FAIL 表示检查器成功发现差异，不是实际恢复失败。

## 证据边界

这次流程覆盖原版 0.20.0 Windows 独立版、一个自有单声道样本、两个调制路由及完整保存状态。它不覆盖 WAVETABLE 导入恢复、立体声或重采样素材、所有音色组合、DAW、外接 MIDI、人工听音、实时性能，也不证明运行时声部、撤销历史或 GUI 导航会被保存。比较器核对两份文件中的 1256 个唯一 ID 和实际值，并未把它们与生产参数注册表及所有参数范围逐项验证。原生退出和素材离线由独立进程记录及操作观察证明，不由文件比较脚本自行证明。
