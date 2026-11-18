# 0.20.2 实际设备回调原始证据

正式采集为 Windows Audio / WASAPI 共享输出、48 kHz、256 samples、0 输入 / 2 输出，6 个场景各 3 轮。每轮请求预热 3 秒，再测量 60 秒；预热向整块取整为 563 块，正式段为 11,250 块。全部正式段共 202,500 块 / 1,080 秒。

`suite-verification.json` 检查记录完整性、原件字节、二进制与源码身份，以及每个场景三轮的全部 1,256 参数和 workload 一致性。`table-rows.md` 汇总独立重算后的耗时。`measurementValid: true` 只表示这些测量记录满足一致性检查，不表示实时性能通过。

正式数据中 processBlock 和带仪表的 callback body 分别有 206 次超过每块 5.333333 ms 预算。WASAPI 输出的原生 xrun 计数为 -1（不支持），不能写成 0 或据此证明无掉音。Manager 的估计值包含预热和外层管理器工作，不能当作驱动计数。输出在测试中被清零，没有真人听音或播放录音；也没有证明零分配或无锁。

目录用途：

- `runs/`：18 次正式采集的 JSON、逐块 CSV 和 stdout/stderr 原字节。
- `analysis/`：冻结的独立分析器逐次重算结果，以及该分析器的 stdout/stderr。
- `suite-manifest.json`：正式采集器原清单，含运行顺序、命令、开始结束时间和输入哈希。
- `support/`：原始机器环境、configure/build/help/devices、自检和运行器输出；`postrun-integrity.json` 记录冻结的发行 ZIP、生产 EXE/VST3 与用户设置的 7 项哈希均未变，生产 Source diff 为零。
- `short/`：正式采集前的默认短测及 6 场景短套件原件与已有分析。短套件 Rössler 的 4 次超预算保留；这些结果不计入正式统计。
- `tooling/`：输出命名空间、CLI 和套件运行器保护检查。它们验证测试工具，不属于正式性能样本。
- `tools/`：本次实际使用的分析器、运行器和一次性归档脚本副本。

从仓库根目录可用已归档原件独立重算任一正式记录，例如：

```powershell
python scripts/analyze_realtime_probe.py `
  --report docs/realtime-probe-0.20.2/runs/round-1-granular.json `
  --csv docs/realtime-probe-0.20.2/runs/round-1-granular.csv `
  --output build/reanalysis/round-1-granular.json
```

分析器明确接收当前 JSON/CSV 路径，所以原始 JSON 中保留的历史绝对路径不妨碍这项重算。输出应写到新的 build 文件，不覆盖原件；重新运行产生的分析生成时间和输入路径可以不同，统计值应一致。无需设备或原测试 EXE。

`tools/analyze-suite.py` 是本机归档时实际执行的一次性检查脚本，默认不会运行，必须显式传 `--capture-complete`。它额外要求原采集目录、原 EXE、原 Git/JUCE 状态及源码快照仍在，并拒绝覆盖已有 work/archive 目录。它用于保留本次核对步骤，不表示从普通 clone 可以还原当时桌面负载或重新得到相同耗时。若从归档位置运行，必须显式传 `--repo D:/Anyvoc` 和新的 `--work` / `--archive` 路径。

原件的逐文件 SHA-256 和长度列在 `suite-verification.json`；最终全目录集合哈希另写在包外/目录外核对记录，避免文件包含自己的哈希。Git 对本目录使用 `-text`，不转换已记录原件的换行。
