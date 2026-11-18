# 0.20.4 ARP MIDI 分配专项原件

本目录保存两份隔离诊断构建的真实采集、编译输入身份及失败记录。诊断构建仍使用 **0.20.3 版本号**，比较的是随后纳入 0.20.4 的 ARP 缓冲区修复；它们不是可分发的正式插件。这里不包含正式版 220 组测试、pluginval、四份演示音频或云端 CI 的验收，也没有声卡或实时截止时间测量。

## 结论与范围

[初次比较](reports/comparison.json)与[归档副本的独立重算](reports/comparison-independent.json)均通过。9 个案例的输入清单逐字节相同，12 份 48 kHz 双声道 little-endian float32 音频逐字节相同；每侧音频共 1,560,000 字节、195,000 帧、390,000 个浮点样本，其中包含 3 份手写 MIDI 参考输出。

| 案例 | 旧构建被记录的分配次数 | 修复后次数 | 独立 MIDI 参考 |
| --- | ---: | ---: | --- |
| fresh-cold | 7 | 0 | 无，只比较旧／新音频 |
| fresh-warmed | 8 | 0 | 有 |
| rotation | 5 | 0 | 有 |
| long-cc | 10 | 0 | 有 |
| lifecycle | 13 | 0 | 无，只比较旧／新音频 |
| keyboard-off | 2 | 2 | 无，只比较旧／新音频 |
| keyboard-on | 8 | 2 | 无，只比较旧／新音频 |
| sysex-off | 2 | 2 | 无，只比较旧／新音频 |
| sysex-on | 5 | 4 | 无，只比较旧／新音频 |

前五项受限 ARP 案例中的 `other-MidiBuffer` 分配合计 **43 → 0**。键盘注入仍产生 host 缓冲区分配；长 SysEx 仍产生 `MidiMessage::allocateSpace` 分配。这些正控保留，不能将结果表述为整个回调零分配。

计数只包围真实 `processBlock` 调用，输入构造、缓冲区准备、日志和文件输出均在计数区外。插桩记录 JUCE `HeapBlock` 的 malloc/calloc/realloc 与 `MidiMessage::allocateSpace` 的 malloc；realloc 返回同一地址也计数。不覆盖所有 CRT 调用、全局 `new`、释放操作或未插桩的其他分配路径。owner 仅按对象身份区分 `host`、`other-MidiBuffer`、`none`；在这些单线程案例中，另一个 MIDI 缓冲区对应处理器私有 ARP 缓冲区，不由内存地址范围猜测类型。

每份采集都要求真实分配正控生效、host MIDI 清空、音频有限且非静音，并检查固定容量 8,192 条记录不溢出。嵌套计数域与 owner 恢复、容量溢出另有合成控制。3 个手写参考案例精确匹配；其余 6 个案例只证明本次旧／新行为一致，不扩充为独立正确性证明。

## 输入与二进制身份

[源差异记录](reports/source-delta.json)核对每侧 4,405 个快照文件，仅 `Source/PluginProcessor.cpp` 和 `.h` 不同，CMake、测试 fixture、工具与 JUCE hooks 相同。修复保留私有 ARP 缓冲区的预留容量，不再把该容量通过 `swapWith` 交给宿主缓冲区。

| 构建 | 诊断 EXE SHA-256 |
| --- | --- |
| before3（有效旧构建） | `8a627e40e49e6b405763140382fd21401174f2c8b0999ad79c026375f425d95a` |
| after（有效新构建） | `e5ddc24f6b13cad59a5b9c5f8b8c2811fb52dd1e4289c02e5e2d14b0419180be` |

两份 EXE 均为 9,825,792 字节，仅保留身份记录，不复制 EXE、PDB 或完整 JUCE 树。实际 Release `/MT`、C++ 强制包含 recorder 及 `ANYVOC_MIDI_ALLOCATION_DIAGNOSTIC=1` 编译字段见 [before 身份](snapshots/before/postbuild-identity.json)与 [after 身份](snapshots/after/postbuild-identity.json)。

目录与原件的逐文件对应关系在 [originals.json](originals.json)：

- `capture/before/`、`capture/after/`：每侧完整 15 成员，含输入清单、分配记录、音频元数据与 12 份原始 float32 音频。
- `snapshots/before/`、`snapshots/after/`：有效构建的准备清单、诊断标记、构建后身份、configure/build/capture 原始输出，以及实际编译的 Processor `.cpp/.h`、CMake、recorder、CLI 源文件。
- `failures/`：两次先前准备／构建失败及对应 CMake、recorder 快照。
- `tooling/`：本次核验所用脚本、比较器的 27 项合成自测源码与原始日志、准备脚本及打包保护的小树检查输出。

所有 76 份复制原件均逐字节及 SHA-256 核对。`README.md`、`originals.json` 和独立重算报告是归档时生成的文件，不冒充采集原件。包外 [归档核验清单](../midi-allocation-archive-verification-0.20.4.json)列出目录内完整集合；它自身不计入集合，集合摘要按大小写敏感的相对路径排序后，对 `path<TAB>bytes<TAB>小写sha256<LF>` 的 UTF-8 字节求 SHA-256。该目录的 Git 属性禁用文本换行转换。

## 重算已归档的音频与分配比较

在仓库根目录运行，输出文件必须尚不存在：

```powershell
python scripts/compare-midi-allocation-probe.py `
  --before docs/midi-allocation-0.20.4/capture/before `
  --after docs/midi-allocation-0.20.4/capture/after `
  --output build/midi-allocation-recheck.json
```

预期退出码 0，报告 `passed: true`，并显示 `9 exact audio cases; bounded private calls 43 -> 0`。当前脚本与归档的 `tooling/scripts/compare-midi-allocation-probe.py` 在归档时逐字节相同。`tooling/verify-snapshot.py` 是原身份核验 helper 的冻结副本，使用原构建目录布局；它不是无需原构建树即可重跑的便携入口。

## 失败记录与工具检查边界

- [首次构建输出](failures/before/build.txt)：C++ recorder 的强制包含传播到 SheenBidi C 源，MSVC 报 STL1003。该失败构建没有用于有效采集。
- [第二次配置输出](failures/before2/configure.txt)：按语言区分的定义导致 `Defs.txt` 多次生成且内容不同。随后 before3 成功；失败目录未被覆盖。
- [Python 3.11 启动器失败](tooling/logs/comparator-self-test-python311.txt)原样保留；[直接调用 3.11](tooling/logs/comparator-self-test-python311-direct.txt)与[初次 3.14](tooling/logs/comparator-self-test-initial.txt)均为 27/27 合成比较器检查通过。这些检查不等于真实分配采集，也不计入正式 C++ 测试组数。
- [19 项准备脚本检查](tooling/prepare-tests/self-test.json)和[24 项打包保护检查](tooling/package-guard-tests/self-test.json)使用临时小树／内存对象。执行方式为 inline 检查，没有保存独立 helper；这里只保留当时原始输出和被测脚本，不补造重现脚本或声称正式发行包已因此验收。
- 两次辅助身份核验的失败没有保存原始 stdout：首次 inline 核验错误地在 `AdditionalOptions` 中查 `/FI`，实际有效字段是 `ForcedIncludeFiles`；随后 helper 初版的模块导入父路径错误。修正核验 helper 后，未修改 EXE 或输入即通过。此段是透明说明，不是原始日志。

本目录不包含个人设置、截图、发行二进制或后续正式版验收材料；冻结后的专项证据与后续发行验证分开保存。
