# Anyvoc 测试与验收指南

本指南针对 **0.19.0 / Windows x64 / VST3 与独立版**，说明怎样复现验证、怎样判定结果，以及怎样留下可追溯记录。0.19 已完成完整四套自动测试；随后两处 UI 文字修正的构建、Processor 补测与最终 pluginval 也已通过。最终音频、预览、有限原生操作和本机发布包已完成下述核对；[指定提交的云端 CI](GITHUB_CI-0.19.md)也已确认四套 201 组通过，作为交付后的仓库补充记录。0.18 及更早版本的真实记录独立保留，不自动适用于本版。操作说明见 [用户指南](USER_GUIDE.md)，构建方式见 [构建指南](BUILDING.md)。

“测试通过”只适用于记录中的版本、硬件、设置和步骤。自动化测试、pluginval、界面截图、真人听音、DAW 工程重开是不同层面的证据，不能互相替代，也不能证明与 Serum 2 声音等价。

## 1. 当前证据与结果

### 0.19：本机验收与指定提交的云端构建通过

| 检查 | 当前状态 | 必须核对的证据 / 范围 |
|---|---|---|
| Release 构建 | 两处 UI 文字修正后已重新构建 | 原完整 CTest、文字修正后的 Processor 补测和最终 VST3 验证分开记录；包内版本与二进制已匹配构建产物 |
| EngineTests | **75 组通过，5.43 秒** | 包含七组新增宏目标测试、旧 sanitizer 上限修正及真实 Phase 链退出修复的独立参考回归 |
| ProcessorTests | 完整运行 **110 组通过，186.75 秒**；文字修正后 **110 组再次通过，184.38 秒** | 后一次是定向补测，CTest 总计 **184.39 秒**；包含六组新增兼容、路由、回调、声音、快照与真实 UI 接线测试 |
| WavetableTests / PresetLibraryTests | **10 / 6 组通过，0.06 / 0.64 秒** | 同一次最终完整运行，未借用 0.18 通过记录 |
| 自动测试合计 | **201 组全部通过：75 + 110 + 10 + 6** | 文字修正前完整四套零失败，总耗时 **192.89 秒**；后续仅影响 UI 文字，Processor 另有补测，不能称为再次完整四套运行 |
| 参数、状态与预设 | 本轮自动回归通过 | 1256 参数：完整保留旧 1192，仅尾追加 64；schema14 兼容 1–13；旧 26 个预设保留，追加两个，共 28 个 |
| pluginval | **strictness 5：SUCCESS** | 文字修正后的最终模块版本 **0.19.0**，seed `0x619c021`；[完整日志](pluginval-0.19.txt) |
| 独立音频分析 | 最终重渲染后 **PASS** | [宏 PCM/legacy 报告](audio-analysis-0.19.json)已重算，legacy 与留存 0.18 EXE 字节一致；最终 Legacy/Motion/Chaos/Macro 四个 WAV 的 SHA-256 均与文字修正前一致 |
| 预览 | 最终构建已重生成 **90 张**；新增六图完成限定视觉检查 | 两张 Macro-Routing 图在文字修正后重新查看，`0-100%` 正常且无裁切；其余四张新增宏图在修正前已查看。未声称全部 90 张均逐张人工检查 |
| 原生操作 / 听音 / DAW | 限定原生操作通过；听音/DAW 未测 | 两组宏拖放、EV/base 分离、外圈 Undo、最小窗口、完整音色 SAVE→INIT→LOAD；使用文字修正前构建，未冒称最终二进制再次原生复测 |
| 发布包 | **本机独立校验通过** | Windows **246** 文件、源码 **4638** 文件；CRC、安全唯一路径、精确集合、逐文件字节、文档、四个 PE 版本及三个必需演示均通过；外置报告 `dist/Anyvoc-0.19.0-PackageVerification.json` |
| 云端 CI | **干净构建及 201 组通过，零失败，约 204.59 秒** | [bf3efc6 / 运行 44262489842](GITHUB_CI-0.19.md)；JUnit 四程序状态与完整日志的 75/110/10/6 内部计数分别核对，原始附件字节保留 |

首轮实际为 **200 组通过、1 组失败**，CTest 仅 3/4 套通过，总耗时 **189.59 秒**；失败是旧 sanitizer fixture 仍把合法目标上限写为 27，现改为 35，未降低断言强度。[首轮日志](ctest-0.19-initial.txt)、[明细](test-details-0.19-initial.txt)与 [JUnit](ctest-0.19-initial.xml)独立保留，不覆盖为成功日志。

首轮之外还发现宏→LFO Phase 链删边退出时的真实旧缓存边界，生产逻辑已修复并新增独立参考用例；最终完整运行确认通过，修复不只限于旧 fixture。宏有效值读取的 reset epoch 与 pending 状态守护及回归也随最终代码复测，避免 Panic/音色切换消费标志窗口显示旧声部。

上述修复后的 [完整 CTest](ctest-0.19-final.txt)、[JUnit](ctest-0.19-final.xml)和[明细](test-details-0.19-final.txt)确认 201 组通过、192.89 秒。随后预览发现两处范围文本乱码，仅将它们替换为 ASCII；重新构建后，[Processor 定向补测](ctest-0.19-text-fix.txt)、[JUnit](ctest-0.19-text-fix.xml)和[明细](test-details-0.19-text-fix.txt)确认 110/110 通过、总耗时 184.39 秒。Engine/DSP 未因这两处文字改变，未把补测写成又执行了全部四套。

文字修正前的 [pluginval 记录](pluginval-0.19-before-text-fix.txt)继续保留，[最终模块日志](pluginval-0.19.txt)已重新确认版本、strictness、种子和 `SUCCESS`。[最终渲染日志](render-0.19-text-fix.txt)与[最终预览日志](preview-0.19-text-fix.txt)记录四个 WAV 和 90 张 PNG 的重新生成；宏 PCM 分析已重新通过，四个 WAV 的 SHA-256 与文字修正前一致。

首轮新增图检查发现 Macro-Routing 范围说明中的 `0–100%` 在 MSVC 构建中乱码，生产代码仅将两处文本替换为 ASCII `0-100%`。最终正常/最小尺寸的 Routing 两图均重新查看，文字正常且无裁切；另外四张新增宏图此前已核对，不能把这一范围扩大为全部 90 图均人工检查。

本机两包已完成独立校验：Windows 246 文件、源码 4638 文件，ZIP CRC、安全唯一路径、精确文件集合、逐文件字节及文档匹配全部通过；包内独立版/VST3 与构建目录对应文件共四个 PE 版本均为 0.19.0，二进制哈希一致，三个必需演示均为完整 20 秒/48 kHz/24-bit/双声道。首个候选 ZIP 压缩期间本指南继续更新，精确匹配检查正确拒绝了不一致的 `docs/TESTING.md`，见[首次包校验失败](package-0.19-initial.txt)；冻结输入后重新打包已解决，没有修改代码或放宽校验。交付时的 ZIP 大小与哈希以包外 `dist/Anyvoc-0.19.0-PackageVerification.json` 为准，不引用候选包数值。本次云端证据是交付后的仓库补充，冻结的 0.19 ZIP 与包内文档保持 `bf3efc6` 时的快照，不重打包；本页新增的云端状态和原始日志没有写回原包。

[原生记录](native-0.19.json)明确使用文字修正前的构建，已确认两组宏拖放、EV 与保存 base 分离、外圈深度由 25% 改至 40% 后一次 Undo 回到 25%，以及最小 1008×684 窗口。真实 SAVE→INIT→LOAD 恢复五条路由，其中第 4 行 LFO1→Macro1 +25%、第 5 行 Macro1→Macro8 +25%；点击 C4 出现绿色电平，Macro8 base 仍为 0%、EV 为 15.7%。保存文件经独立 XML 检查为 schema14、1256 参数；未把电平读数当真人听音证据。

测试实例退出后已恢复原设置，字节一致，SHA-256 为 `5000f1074d6b885621afabb357a32e2ba2050f8661ad38c3fb92bc520674ab7e`。本轮没有在文字修正后的最终二进制重复原生操作，也未验证进程重启后加载外部素材、OS 剪贴板、外部 MIDI、真人听音或 DAW。上述有限操作不等于第 5、6 节清单全部通过。

新增测试的判定边界：宏是逐声部调制目标，所有主源和 AUX 先读取同一当前有效值；整张矩阵计算出的输入和留到下一样本使用。有效值为 `clamp(平滑后的保存值 + 上一样本输入和, 0, 1)`，不写回保存旋钮或声音历史。全八宏接线以独立常数和实际声音作参考；整行移动测试要求完整操作完成后的音频与参照精确一致，包括深度平滑尚未完成的情况。

**并发边界：**当前 `readSettings()` 仍逐参数读取，没有跨整条路由的原子发布。上述移动测试在完整 `moveModulationRoute()` 返回后才 render，不能据此声称移动过程中的每个并发音频块都位级不变。暂态路由读取及最坏实时负载仍需单独测量，本文不把它们列为已解决。

### 0.18 历史：自动测试、插件、音频与包校验通过，原生完成限定检查

| 检查 | 当前状态 | 可核查证据 / 待完成范围 |
|---|---|---|
| Release 构建 | 已完成 | 本轮构建产物完成下列完整 CTest；发布前仍须核对最终二进制与源码 |
| EngineTests | **68 组通过，4.55 秒** | [首次完整 CTest](ctest-0.18-initial.txt)、[测试明细](test-details-0.18-initial.txt)；新增七组离线曲线数学及严格文件测试 |
| ProcessorTests | **104 组通过，156.67 秒** | 同一运行；新增六组真实曲线控件、历史、参数隔离、回调与资产 IO 测试 |
| WavetableTests / PresetLibraryTests | **10 / 6 组通过，0.08 / 0.63 秒** | 同一完整运行 |
| 自动测试合计 | **188 组全部通过：68 + 104 + 10 + 6** | 四套零失败，总耗时 **161.94 秒** |
| 参数、状态与预设 | 本轮不新增定义 | 保持 1192 参数、schema 13、26 个工厂预设；旧点值步长 0.01，详见第 7 节 |
| 最终模块 pluginval | **strictness 5：SUCCESS** | [本轮完整日志](pluginval-0.18.txt)，seed `0x4fc4b12`；0.17 的日志未用于替代 |
| 新渲染音频与旧声音对照 | **独立分析 PASS** | [音频报告](audio-analysis-0.18.json)：本轮 Chaos 为 20 秒/48 kHz/24-bit/双声道；真正留存 0.17 EXE 与 0.18 新渲染的 legacy 逐字节及 SHA-256 一致 |
| 预览 | 84 张已生成，新工具四张逐张检查无裁切 | Curve Tools / Curve Selection，正常与最小尺寸；不把静态图当原生交互证明 |
| 原生交互 | 完成下列限定操作 | 真实 SAVE/LOAD 对话框、选区 Flip/Undo、Custom 切换、取消和最小窗口；不包含 OS 剪贴板或真人听音 |
| 发布包 | **独立校验通过** | Windows **208** 文件、完整源码 **4600** 文件；CRC、安全唯一路径、精确集合、逐文件匹配、0.18.0 版本及构建二进制一致性全部通过；外置报告 `dist/Anyvoc-0.18.0-PackageVerification.json` |
| 0.18 云端 CI | **干净构建及 188 组通过，零失败，约 130.98 秒** | [45578ca / 运行 44257671955](GITHUB_CI-0.18.md)；JUnit 4 个程序及内部 68 / 104 / 10 / 6 组分别核对，原始日志保留下载字节 |
| 其他人工与宿主范围 | 未测 / 未完成全面验收 | OS 剪贴板、真人听音、外部 MIDI、整音色原生保存重开、DAW 未测；最坏实时性能仍无完整验收 |

本轮七组模型测试使用独立数值例子覆盖半格吸附、选区反转、移动缩放、Linear/Step、端点保持、身份精确、非法输入原子拒绝、float 位级往返、严格 XML 与真实文件失败保护。六组 Processor 测试操作真实可见控件并检查实际参数变化、全部参数隔离、一个动作一次 Undo、声音参考和 46 个相关参数的回调边界；Flip X/Y 使用非对称输入，避免两种操作偶然得到相同结果。

平台测试只替换 OS 剪贴板和文件选择器的服务边界，仍执行生产按钮、快照守护、正常宿主手势、声音历史、严格 codec 和真实临时目录 IO。覆盖同步/异步/重复回复、隐藏/换页/状态替换/销毁后的迟到回复；**它不打开原生对话框、不操作用户系统剪贴板，不能替代原生交互验收**。上述是已通过自动测试的范围，保留首次日志。

独立音频报告中的两份 legacy SHA-256 均为 `3140a2896cd0954762e6406b4b344c22a185fb3f493dc3a96e7b0e9ce8747920`，文件逐字节相同。当前 Chaos WAV 的 SHA-256 为 `4885d2a3de312d88bc890840560eff45139b5c4d43de694c9d9fdeb232d18488`，没有 PCM 满幅样本；这是数值分析，不是主观听音结果。

本轮原生操作从 Neon Skyline 打开 LFO Lab→TOOLS，实际完成：

- 用真实 SAVE 对话框保存 Sine 曲线，文件含 33 点、1,121 字节。
- Select 9–25 后 Flip Y，并用一次 Undo 恢复。
- 用真实 LOAD 对话框载入曲线，形状切为 Custom；点击 C4 观察到绿色输出电平，再 Undo 回 Sine。电平可见不等于已经听音。
- 在最小窗口检查工具与琴键完整；另开 LOAD 后用 Escape 取消，声音状态未改。
- 关闭测试实例并恢复原设置，恢复后 SHA-256 为 `5000F1074D6B885621AFABB357A32E2BA2050F8661AD38C3FB92BC520674AB7E`。

这份原生记录不覆盖系统剪贴板、真人听音、外部 MIDI、整音色的原生保存/重开和 DAW。完整步骤与证据见 [0.18 版本验证记录](VALIDATION-0.18.md)；[0.18 云端证据](GITHUB_CI-0.18.md)仅证明相应提交的干净构建和自动测试。它是发布之后的补充记录，不覆盖既有 0.18 发行包，也不扩展上述原生验收范围。

### 0.17 历史：最终自动复测与发布包校验通过

以下结果只描述当时的 0.17 产物；完整历史见 [VALIDATION-0.17.md](VALIDATION-0.17.md)。

| 检查 | 0.17 历史状态 | 可核查证据 / 范围 |
|---|---|---|
| Release 构建 | 修复后增量构建完成 | 当时构建完成下列 CTest 与 VST3 检查，包内二进制版本与哈希均匹配构建产物 |
| EngineTests | **61 组通过** | 4.53 秒；[最终完整 CTest](ctest-0.17.txt)、[最终测试明细](test-details-0.17.txt) |
| ProcessorTests | **98 组通过** | 同一最终完整运行，129.80 秒；首轮 2 项失败及修复过程保留在下文 |
| WavetableTests / PresetLibraryTests | **10 / 6 组通过** | 同一最终完整运行，分别 0.06 / 0.63 秒 |
| 自动测试合计 | **175 组全部通过：61 + 98 + 10 + 6** | 四套零失败，总耗时 **135.03 秒**；不是仅定向重跑 Processor |
| 参数、状态与预设 | 自动回归通过 | 1192 参数、schema13、26 个工厂预设；具体兼容门槛见第 7 节 |
| 预览 | 最终二进制重新生成 80 张，6 张 Chaos 图再次逐张检查无裁切 | 正常与最小尺寸的 Lorenz / Rossler / Evolved；原生操作范围另列于下文 |
| Chaos 音频与旧演示 | **最终 CLI 渲染与独立分析 PASS** | [音频分析](audio-analysis-0.17.json)：20 秒、48 kHz、24-bit、双声道；旧演示逐字节及 SHA-256 一致，本次快照修复未改变演示哈希 |
| VST3 规范检查 | **pluginval strictness 5：SUCCESS** | [最终插件日志](pluginval-0.17.txt)，版本 0.17.0，随机种子 `0x5f5ab2b` |
| 原生界面 | 已完成限定范围的鼠标操作复核 | 算法、真实 XYZ、Undo 清图、禁用 Phase、速度撤销、模式与最小尺寸；具体边界见下文 |
| 发布包 | **独立校验通过** | Windows 190 文件、源码 4580 文件；CRC、安全唯一路径、精确集合、逐字节和当时文档匹配；四个 PE FileVersion 均为 0.17.0，包内二进制 SHA-256 与构建产物一致 |
| Chaos 离线基准 | 已测量；不代表实时验收通过 | [基准原始记录](benchmark-chaos-0.17.txt)；复现方式与局限见第 7 节 |
| DAW / 真人听音 / 最坏实时性能 | 未完成全面验收 | 逐项执行本文用例，保留未测状态 |

首轮完整 CTest 总耗时 124.72 秒，Engine 另有 1 项复位比较失败、Processor 有 2 项失败，见 [首轮 CTest](ctest-0.17-initial.txt)和[首轮明细](test-details-0.17-initial.txt)。音频复位用例把效果器首次运行的 dry/wet 平滑与已经稳定后的重置状态混在一起；现改为双方先静音预热 4096 样本再重置，并要求音频及同位置 Chaos XYZ 精确一致。另一项是 Phase 的拖放兴趣判断没有检查 Chaos 禁用状态；界面接收判定已修，测试仍要求拒绝拖放且声音状态逐字节不变。首次失败记录保留，没有覆盖为通过日志。

原生检查之前的 [完整 CTest](ctest-0.17-before-native-fix.txt)和[测试明细](test-details-0.17-before-native-fix.txt)曾确认 175 组零失败、总耗时 133.32 秒；当时的 [pluginval 日志](pluginval-0.17-before-native-fix.txt)也已单独留存。这些通过记录不替代后续代码修复后的验证。

随后原生操作发现一个此前没有覆盖的窗口：Rossler 演奏后 Undo 回 Lorenz，界面标签已经改变，下一音频块尚未处理，图却可能读取旧 Rossler 的 XYZ 并留下错误连线。修复将算法、播放模式和 XYZ 作为同一代快照发布，读取时按标签过滤；界面使用本次刷新捕获的同一组标签，避免宿主并发自动化在第二次参数读取时换掉标签。无匹配数据返回全零哨兵，清空历史且不绘制假原点；实际非零的 Envelope 保持值仍正常显示和去重。

已在原有测试组内补充 LFO1/LFO10 的算法选择、宿主算法/模式自动化、Undo 回归：不等待 timer、也不提前处理音频，立即要求 getter 与真实同步 UI 刷新拒绝旧快照；新音频发布后要求新标签得到精确 XYZ、旧标签得到零，并区分空闲零哨兵与非零保持值。标签一致性补充之前的 [复测记录](ctest-0.17-before-label-fix.txt)及[明细](test-details-0.17-before-label-fix.txt)也保留。最终冻结代码已重新完成四套 CTest，175 组通过、总耗时 135.03 秒，并重新完成插件验证、CLI 渲染和预览核对。

最终独立版原生鼠标检查完成了以下操作，范围不等于第 5 节所有人工用例均已通过：

- 在 Browser 双击加载 Butterfly Current，点击屏幕 C4；两种 Chaos 算法均观察到实际 XYZ 与绿色电平表。此项确认可视信号活动，没有记录真人扬声器/耳机听音。
- Rossler 演奏后 Undo 回 Lorenz，XYZ 清零且没有旧轨迹连线；Chaos 下拖 Phase 后仍为 0。Speed 从 1.80 改到 7.29，再 Undo 恢复 1.80。
- Free 模式持续演化。Envelope 短音后的 XYZ 稳定在约 `(-0.484, -0.455, 0.149)`；这次原生操作没有持续持音到完整名义周期，不能用短音后的稳定值证明 one-shot 周期终点正确，完整周期由自动测试另行验证。
- 切回 Sine 后相关控件恢复可用，再 Undo 返回；最小窗口无裁切。

原用户设置已恢复，恢复文件 SHA-256 为 `5000F1074D6B885621AFABB357A32E2BA2050F8661AD38C3FB92BC520674AB7E`。本轮未做真人听音、原生保存/加载或 DAW 验收；搜索框文字输入因自动化工具递送失败未完成验证，不将其记录为插件搜索功能通过或失败。

正式 Windows 与源码包已实际生成并通过 `scripts/verify-release.py` 全部检查，外置报告为 `dist/Anyvoc-0.17.0-PackageVerification.json`。两个必需演示均通过完整 20 秒、48 kHz、24-bit、双声道校验。文档冻结后的重新打包仍须再次运行同一校验；ZIP 大小与哈希以最终外置报告为准，不把较早包的哈希写入本指南。

### 0.16 历史记录

下面的通过结果仅适用于当时的 0.16 产物，不自动适用于当前 0.19 版本。

| 检查 | 0.16 结果 | 可核查证据 / 范围 |
|---|---|---|
| Release 构建 | 通过 | Windows x64；独立版和 VST3 文件版本均为 0.16.0；现存转换/弃用警告仍有记录 |
| EngineTests | 54 组通过 | [Engine 明细](engine-0.16.txt)，含独立 DSP 参考、块划分、相位反馈和高速目标 |
| ProcessorTests | 92 组通过 | [最终定向复测](ctest-processor-0.16.txt)，[92 组明细](test-details-0.16.txt) |
| WavetableTests | 10 组通过 | [首轮完整 CTest](ctest-0.16.txt) 中该套通过 |
| PresetLibraryTests | 6 组通过 | 同一首轮日志中该套通过 |
| 合计 | **162 组通过** | 54 + 92 + 10 + 6；不是 162 个 CTest 可执行程序，而是四个程序内部的测试组 |
| VST3 规范检查 | pluginval strictness 5：SUCCESS | [最终插件日志](pluginval-0.16.txt)，模块版本 0.16.0 |
| 旧演示一致性 | 通过 | 新旧二进制重新生成的 legacy WAV 逐字节相同，见版本验证记录 |
| 新演示格式及信号 | 通过 | [独立音频分析](audio-analysis-0.16.json)：20 秒、48 kHz、24-bit、双声道，无满幅 PCM 样本 |
| 预览与独立版操作 | 有限定范围的通过记录 | 74 张预览生成，8 张新增图人工检查；独立版操作范围及执行先后见版本记录 |
| FL Studio / 其他 DAW | **未验证** | 用户选择先用独立版；不能从 pluginval 推断宿主兼容 |
| 现场最坏负载 / 完整听音 | **未完成全面验收** | 需要按本文硬件与操作步骤另记结果；表头电平变化不等于已经听到扬声器声音 |

首轮完整 CTest 的退出结果确实为失败：Processor 有 4 项失败，其余三套通过。原因包括新 Phase/Swing 使用了带 0.01 步长的浮点构造器，以及新 UI 测试误操作隐藏行控件。生产参数已改为明确的连续范围，测试改为真实可见详情页控件，精确数值及一个 ULP 的冲突断言均保留。最终仅重跑受影响的 Processor 套件，**92/92 通过，108.01 秒**（CTest 总时间 108.02 秒）。初始日志没有被改写成“全通过”，也没有再次声称执行了一次最终完整四套运行。以后验证新候选版本应重新运行完整四套。

本次 GitHub 整理另于 2026-11-18 对已验证的 0.16 二进制重跑了**完整四套测试，全部通过，112.05 秒**。独立记录见 [GitHub 整理全量复测](ctest-github-handoff-0.16.txt)及[完整测试明细](test-details-github-handoff-0.16.txt)。这项新记录不改写上面的历史首轮失败与定向复测记录，也不是新的 DAW 或听音验证。

## 2. 准备与构建

需要 Windows x64、Visual Studio 2022 C++ Build Tools、Windows SDK、CMake ≥ 3.22、Git；源码打包、独立音频分析和发布包校验需要 Python 3，脚本仅使用标准库。JUCE 固定为 8.0.12，提交 `29396c22c93392d6738e021b83196283d6e4d850`。发布前同时核对版本、提交和未提交改动，不要用一个工作目录的新源码解释另一个旧二进制的结果。

下列命令在**仓库根目录的 PowerShell**运行，不依赖某位开发者的磁盘路径。先关闭用于上一次验证的 Anyvoc 进程，避免 Windows 锁住正在替换的二进制；不要关闭用户尚未保存的工作。手动测试使用单独的音色库和测试工程，先备份用户设置及音色。

最快的完整构建与测试入口：

```powershell
./scripts/build.ps1
```

脚本会在缺少 JUCE 时获取固定版本，配置 Visual Studio 2022 x64，构建 Release，再执行 CTest。脚本内部会对每一步非零退出码报错。需要逐步记录时使用下面的等价命令；该方式要求 JUCE 已存在：

```powershell
$ErrorActionPreference = 'Stop'
$repoRoot = (Get-Location).Path
$runDir = Join-Path $repoRoot ('build/acceptance/' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
git rev-parse HEAD | Set-Content (Join-Path $runDir 'commit.txt')
git status --short | Set-Content (Join-Path $runDir 'worktree.txt')
cmake -S . -B build -G 'Visual Studio 17 2022' -A x64 -DANYVOC_BUILD_TESTS=ON
if ($LASTEXITCODE -ne 0) { throw 'Configure failed' }
cmake --build build --config Release --parallel 4 2>&1 |
    Tee-Object -FilePath (Join-Path $runDir 'build.txt')
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
ctest --test-dir build -C Release --output-on-failure 2>&1 |
    Tee-Object -FilePath (Join-Path $runDir 'ctest.txt')
$testExit = $LASTEXITCODE
Copy-Item -LiteralPath build/Testing/Temporary/LastTest.log -Destination (Join-Path $runDir 'test-details.txt')
if ($testExit -ne 0) { throw 'CTest failed; retain the failed log before retrying' }
```

**通过标准：**构建退出码 0；CTest 四个目标全部通过；0.19 的 75 / 110 / 10 / 6 组分别汇总零失败，共 201 组。测试组数只能在有评审的测试变更后增加或调整，不能通过删掉失败项保持“绿色”。保留警告，区别既有警告与新增警告。排查单套失败可运行 `ctest --test-dir build -C Release -R '^ProcessorTests$' --output-on-failure`，但需注明“定向复测”，不覆盖初始失败日志。

## 3. 可重现预览与 WAV 演示

处理器测试程序支持以下入口。它是控制台程序，可使用 PowerShell 的调用运算符 `&`；每次检查退出码。输出放在本次验收目录，避免覆盖已发布的证据。

```powershell
$processorTests = Join-Path $repoRoot 'build/AnyvocProcessorTests_artefacts/Release/AnyvocProcessorTests.exe'
& $processorTests --preview (Join-Path $runDir 'images')
if ($LASTEXITCODE -ne 0) { throw 'Preview generation failed' }
& $processorTests --lfo-motion-demo (Join-Path $runDir 'Lfo-Motion.wav')
if ($LASTEXITCODE -ne 0) { throw 'LFO motion render failed' }
& $processorTests --chaos-demo (Join-Path $runDir 'Chaos.wav')
if ($LASTEXITCODE -ne 0) { throw 'Chaos render failed' }
& $processorTests --macro-demo (Join-Path $runDir 'Macro.wav')
if ($LASTEXITCODE -ne 0) { throw 'Macro render failed' }
& $processorTests --demo (Join-Path $runDir 'Legacy.wav')
if ($LASTEXITCODE -ne 0) { throw 'Legacy render failed' }
```

其他入口同样接受一个 WAV 输出路径：

| 参数 | 长度 | 演示内容 |
|---|---:|---|
| `--demo` | 15 秒 | Pad、Sample、Granular 各 5 秒；旧音频兼容基线 |
| `--warp-demo` | 12 秒 | 原声、单槽、双槽、双槽调制 |
| `--voicing-demo` | 12 秒 | Poly、Mono、Legato、Legato + 180 ms 滑音 |
| `--crossmod-demo` | 15 秒 | 原声、FM、PM、AM、RM；B 不作为可听直达声源 |
| `--matrix-demo` | 15 秒 | LFO→Cutoff；AUX Macro 1 从 0% 至 100%，最后改变响应曲线 |
| `--sources-demo` | 16 秒 | Prism Weave Macro 5 和 Four Horizons Macro 8 的 0%/100% 对比 |
| `--envelopes-demo` | 20 秒 | Clockwork Bloom 的节拍/曲率，以及 Second Breath 的连奏重触发 |
| `--lfo-motion-demo` | 20 秒 | 0–4 秒直拍、4–8 秒 Swing 60%、8–12 秒 Swing + Reverse；12–16 秒 110 Hz、16–20 秒 220 Hz |
| `--chaos-demo` | 20 秒 | 0–10 秒 Lorenz、10–20 秒 Rossler；相同 C4、力度、Trigger、名义速度 2、声音与固定增益，仅更换算法 |
| `--macro-demo` | 20 秒 | 四段各 5 秒：固定 Macro1；增加 LFO1→Macro1；再增加 LFO2→Pan、受调 Macro1 作 AUX；最后加入 Macro1/8 互调并改用 Macro8 作 Pan AUX |

**预览通过标准：**0.19 应生成 **90 张 PNG**，保留 0.18 的 84 张并新增 `Macro-Live.png`、`Macro-Bank2-Live.png`、`Macro-Routing.png` 及各自 `-small` 版本。正常 1120×760 与最小 1008×684 下，八个宏旋钮、EV 标签、bank 按钮、矩阵下拉框、底部琴键不得裁切/重叠。六张新图必须来自真实音频处理后的有效值快照，不能用本地动画或假读数代替。继续检查 Curve Tools/Selection、Chaos 六张图、Swing/Motion/Audio/Phase Routing；截图不替代拖动、焦点、原生对话框或 MIDI 操作。

Motion / Chaos 继续沿用 `Anyvoc-v0.16-Lfo-Motion-Demo.wav`、`Anyvoc-v0.17-Chaos-Demo.wav` 的打包名称，新宏演示为 `Anyvoc-v0.19-Macro-Demo.wav`；三个文件均须由 **0.19 程序重新渲染**。独立 `$runDir` 保存本轮结果，不能写入历史发行目录或覆盖旧报告。

**音频通过标准：**命令退出 0，长度/声道/采样率/位深符合约定，无意外静音或 PCM 满幅样本。三个必需演示均为 960,000 帧、48 kHz、24-bit、双声道。Chaos 两段各 10 秒，第 9 秒松键；宏演示四段各 5 秒，每段第 4 秒松键；均用 20 ms 淡入、100 ms 淡出。宏四段使用同一 Prism Handle 基础、C4、力度 0.75、保存 Macro1=0.4 和 master=0.42，固定增益，不逐段归一化。第 0/1 段只增加一条 LFO1 输入路由；后两段更改了明确的路由组合，不能称为单变量对照。LFO1 为 Trigger Triangle 0.4 Hz、Unipolar、Amount 0.4；Pan 路由的 LFO2 为 1.2 Hz、Amount 0.65；末段 Macro8 base=0.1，Macro1→8 为 0.5、Macro8→1 为 0.2。

渲染测试检查编码前浮点 NaN/Inf；整数 PCM 不含 NaN/Inf，独立文件分析不能替代此项检查。无 PCM 满幅样本也不能排除上游限幅。听音记录写明设备、音量和听到的差异，不把 RMS 差异写成“音质更好”。

旧行为要求逐字节一致时，用留存的上一版**测试二进制**和候选版分别执行 `--demo`，比较 `Get-FileHash -Algorithm SHA256` 结果；不拿陈旧的 WAV 充当刚生成的基线。若声音变化是明确修复，记录差异原因与受影响预设，不能盲目更新基线消除失败。

仓库中的 [独立 Chaos 音频分析脚本](../scripts/analyze-chaos-demo.py)和[宏演示分析脚本](../scripts/analyze-macro-demo.py)接受显式输入路径。先将已验证的 **0.18** 测试程序留存到下方路径，或修改 `$baselineProcessorTests` 指向自己的留存程序，并记录提交和哈希；它不是源码仓库的组成部分。缺少旧二进制时回归记“未测”，不能复制候选 WAV 假作旧基线。Chaos 脚本本轮显式指定版本 0.19.0；宏脚本是 0.19 专用，不借旧 JSON 或默认路径完成验收。

```powershell
$baselineProcessorTests = Join-Path $repoRoot 'build/validation-baseline/AnyvocProcessorTests-v0.18.exe'
if (!(Test-Path -LiteralPath $baselineProcessorTests)) { throw 'Supply the verified 0.18 test executable first' }
Get-FileHash -LiteralPath $baselineProcessorTests -Algorithm SHA256 |
    Format-List | Out-File -Encoding utf8 (Join-Path $runDir 'legacy-renderer-sha256.txt')
& $baselineProcessorTests --demo (Join-Path $runDir 'Legacy-0.18.wav')
if ($LASTEXITCODE -ne 0) { throw 'Baseline render failed' }
python scripts/analyze-chaos-demo.py `
    --version 0.19.0 `
    --demo (Join-Path $runDir 'Chaos.wav') `
    --legacy-before (Join-Path $runDir 'Legacy-0.18.wav') `
    --legacy-after (Join-Path $runDir 'Legacy.wav') `
    --output (Join-Path $runDir 'audio-analysis-0.19.json')
if ($LASTEXITCODE -ne 0) { throw 'Independent Chaos analysis failed' }
python scripts/analyze-macro-demo.py `
    --demo (Join-Path $runDir 'Macro.wav') `
    --legacy-before (Join-Path $runDir 'Legacy-0.18.wav') `
    --legacy-after (Join-Path $runDir 'Legacy.wav') `
    --output (Join-Path $runDir 'macro-audio-analysis-0.19.json')
if ($LASTEXITCODE -ne 0) { throw 'Independent macro analysis failed' }
```

**分析通过标准：**两脚本退出 0，两个 JSON 的 `status` 为 `PASS`；格式与完整 PCM 长度正确；各段 RMS 大于 0.001、各规定对照的解码样本不完全相同、无达到 PCM 满幅的样本；旧演示逐字节及 SHA-256 一致。宏脚本只复用通用 WAV 解码、RMS 和哈希运算，不导入合成器代码或生成期望音频；它能独立说明文件中的信号和差异，不能单靠段落不同证明逐样宏反馈算法正确。算法与接线由独立 Engine/Processor 参考测试另行核验。

## 4. VST3：pluginval

pluginval 是外部工具，不属于合成器构建依赖。把它放在 `tools/pluginval/pluginval.exe`，或将下方变量改为自己的工具位置，并记录工具版本。Windows 下使用 **`Start-Process -Wait -PassThru`** 启动，不能只通过 `& pluginval.exe` 返回后的 `$LASTEXITCODE` 判定完成；还要检查验证进程产出的完整日志。

```powershell
$validator = Join-Path $repoRoot 'tools/pluginval/pluginval.exe'
$bundle = Join-Path $repoRoot 'build/Anyvoc_artefacts/Release/VST3/Anyvoc.vst3'
$validationDir = Join-Path $runDir 'pluginval'
New-Item -ItemType Directory -Force -Path $validationDir | Out-Null
$validatorArgs = '--strictness-level 5 --validate "{0}"' -f $bundle
$validationProcess = Start-Process -FilePath $validator -ArgumentList $validatorArgs `
    -WindowStyle Hidden -Wait -PassThru `
    -RedirectStandardOutput (Join-Path $validationDir 'pluginval.txt') `
    -RedirectStandardError (Join-Path $validationDir 'pluginval-stderr.txt')
$validationProcess.ExitCode
Get-Content -LiteralPath (Join-Path $validationDir 'pluginval.txt')
Get-Content -LiteralPath (Join-Path $validationDir 'pluginval-stderr.txt')
```

**通过标准：**进程正常结束、退出码 0，完整验证日志明确显示预期 Anyvoc 版本、strictness 5、正确模块路径，最后为 `SUCCESS`，没有失败或超时；保存随机种子与日志。无日志、空日志、仅启动成功或只写出插件名称均为“未完成”，不是通过。失败时保留随机种子复现。不要通过排除失败测试或降低 strictness 消除问题。若另用 Steinberg SDK validator，应单独记工具版本和结果；未运行就不能声称通过。

## 5. 独立版人工验收

运行 `build/Anyvoc_artefacts/Release/Standalone/Anyvoc.exe`，或解压发布包后运行 `Anyvoc.exe`。以下是**执行清单**，不是对所有步骤已完成的声明。每一行记录 PASS / FAIL / 未测及证据。

| ID | 操作 | 预期 / 失败判定 |
|---|---|---|
| S01 启动与设备 | 新开测试实例，核对版本；选择实际输出设备和 48 kHz / 256 样本缓冲，记录驱动类型 | 窗口可用、设备成功打开；无崩溃、设备错误循环或永久静音 |
| S02 声音 | INIT 后点屏幕键盘 C4，松开；依次试听全部 28 个工厂预设，包含 Prism Handle / Breathing Prism；适度音量下实际听音 | 旧 26 个预设顺序保留，新两项追加；电平与扬声器/耳机均有输出，松键按包络结束，无卡音；仅看到电平不能勾“听音通过” |
| S03 MIDI | 选择物理/虚拟 MIDI 输入；单音、和弦、力度、弯音、CC1、延音踏板；松开全部音符和踏板 | 音高/力度/路由响应正确；弯音约 ±2 半音；无残留卡音。无 MIDI 设备则该项未测 |
| S04 调音与历史 | 改 Cutoff 一次、一次连续旋钮拖动、Undo、Redo；INIT 后 Undo | 数值与声音恢复；一个拖动是一个撤销步骤；INIT 可撤销。另测 Ctrl 快捷键并单独记录，不能由鼠标按钮通过推断 |
| S05 自绘曲线 | MOD→LFO1 EDIT→Custom，连续画一笔；选曲线预置；逐步 Undo/Redo | 整笔/整个预置分别一次撤销，33 点及类型正确恢复；切到 LFO10 不串改 LFO1 |
| S06 包络图 | 分别编辑 Amp / Filter / Env3 / Env4；拖 Attack、Decay/Sustain、曲率；开 BPM 与 Legato 重触发 | 图、数值和声音一致；Decay/Sustain 联动一次撤销；BPM 模式编辑分割并保留原秒值 |
| S07 调制路由 | 拖 LFO 至 Cutoff；改正负 Amount、旁通、AUX Macro、曲线；第 4 页编辑路由32；移动/清除后 Undo | 旁通保留配置，AUX=0 可关闭深度，移动保留整行；Undo 恢复整条配置，无越界、误覆盖 |
| S08 新 LFO 功能 | Swing Carousel 改 Swing/Direction；Phase Lantern 改 Phase、三种 Rate Mode；从 SOURCES 拖到 Phase | 曲线读数/声音随设置变化；切换模式保留先前速度；Random 禁用 Phase/Direction 并保留值；Undo 能恢复 |
| S09 保存与重开 | SAVE 到新 `.anyvoc`；改音色；LOAD；关闭测试实例再启动并 LOAD | 参数、名称、波表、样本、自绘曲线、路由一致；每次保存使用测试文件，不覆盖唯一原件 |
| S10 导入与嵌入 | 导入已知 WAV 为波表和样本，编辑后另存；把测试输入 WAV 移到另一测试目录，再重新 LOAD | 保存的音色仍可恢复内嵌素材；错误/不支持文件给出错误且不损坏当前音色。当前样本限制：≤20 秒、8–192 kHz、单/双声道 |
| S11 窗口 | Synth/MOD/FX/Browser 与各 Lab；最小 1008×684、默认 1120×760，再放大；测试缩放/DPI | 所有按钮可达、标签可读，无遮挡、空白、控件串页；截图记录系统 DPI |
| S12 设备切换与重启 | 停音后切输出设备/采样率/缓冲，重新发音；退出后重开 | 无崩溃、卡音或旧设备残留占用；记录是否需要手动重选设备，不把自动恢复未实测行为写成保证 |

已知历史记录涉及原生 Ctrl 键递送以及间歇性的琶音器延音 RMS 问题；0.16 没有宣称专门修复。验收遇到类似问题应保留复现步骤，不以“以前出现过”忽略。人工检查结束后恢复原设置；如果做了字节备份，核对恢复后的哈希。

### 0.19 宏目标专项人工用例

以下仍是待执行清单。Macro1–8 既可作源/AUX，也可作为矩阵目标；保存旋钮是 base，`EV` 是最新活动声部实际使用的有效值，UI 按刷新频率显示快照，不逐音频样本显示。复音下不同声部可以有不同值，一个 EV 标签不能代表所有声部。

| ID | 操作 | 预期 / 失败判定 |
|---|---|---|
| M01 目标与两组旋钮 | INIT 后打开 MOD/SOURCES，把 LFO1 拖到 Macro1，再切 5–8 bank 拖到 Macro8；检查矩阵下拉框及外圈 | 新目的地 Macro1–8 追加在原 28 项之后；八个宏均可接收、编辑正负深度。切 bank 只改视图，不改 base、路由或 Undo；隐藏 bank 的迟到拖动不新增路由 |
| M02 保存值与有效值 | Macro1 base=0.25，Velocity→Macro1 Amount=0.25；用满力度音符持音，再改力度；将 Macro1 路由到 Cutoff | 稳定满力度时 Macro1 EV 约 0.5、base 仍 0.25；较低力度的声部可不同。声部输入改变声音但不写回旋钮、宿主自动化或声音历史；松键进入 Release 时仍可显示活动声部 |
| M03 AUX 与截断 | 用受调 Macro1 作 LFO2→Pan 的 AUX；把入宏深度改为正/负，分别跨过 0 与 1；旁通入宏路由 | 主源和 AUX 都读取有效值；EV 始终在 0–1。旁通/Source Off 后该入路不继续反馈，所有有效入路关闭时回到平滑 base；不要把 Amount 仍在平滑中的短暂变化误判为立即失效 |
| M04 自调制与互调制 | Macro1 base=0.2，Macro1→Macro1 Amount=0.5；另测 Macro1→Macro8、Macro8→Macro1，含 AUX 和曲线 | 自调制稳定值应接近 0.4，运行有限且可通过旁通/Panic 停止；互调制按上一样本输入作用，不是矩阵行内递归。声音变化及稳定性另记，不能只凭 EV 图证明每样本算法 |
| M05 生命周期与清空 | 对比 Poly 新音、Mono 重触发、Legato 重叠/松键回退、延音踏板与尾音；Panic、换预设、重新开音频设备 | 新声部/Mono 重触发清输入历史，Legato 重叠/回退保持；Release/踏板仍按活动声部处理。Panic 或换预设立即显示当前 base/idle，不短暂冒出旧有效值；无活动声部时 `EV: idle base` |
| M06 路由历史与满表 | 在第 1/32 行设宏目标、AUX、曲线和旁通；拖行移动、清除、Undo/Redo；另将 32 行填满再尝试新增 | 整行 19 字段及隐藏的旧目标选择保持；一次动作一次撤销。满表不覆盖已有路由，旁通但占用的行不当空位；重复同源/同目标的旁通路由重新启用并保留其配置 |
| M07 实际快照与布局 | 持音比较 Prism Handle 与 Breathing Prism；两组宏来回切换，先后按两个不同力度音；在默认/最小窗口查看 EV、外圈和矩阵 | 八标签来自同一次实际引擎快照，显示最新活动声部而不是本地模拟。保存值保持，所有标签/按钮/琴键可读可达；截图与电平不等于实际听音通过 |
| M08 保存与宿主边界 | 用宏目标、互调制、AUX 和旧 Phase 目标保存 `.anyvoc` 并重开；在 DAW 再做自动化、复制实例与工程重开 | 恢复 base 与完整路由；运行时的反馈输入/声部 ID 不作为音色存档接续。旧参数映射不变，宿主相关写入不会被旧 UI 操作覆盖；并发移动过程中逐块音频一致性另测，不从完整移动后的比较推断 |

没有物理/虚拟 MIDI 输入时，精确力度、踏板和重叠音用例应记未测或交由明确记录的宿主 MIDI 工程完成。自动测试已覆盖全部八宏的独立常数声音参考、32 行容量、回调和可见控件路径；它不能代替上述原生、外部 MIDI、听音和 DAW 操作。

### 0.18 曲线工具专项人工用例

以下是保留的曲线工具操作标准；0.18 的有限原生记录见第 1 节历史部分，不能据此把 0.19 整行勾选为通过。先备份用户设置，在测试实例打开 LFO Lab→TOOLS，至少检查 LFO1 与 LFO10；使用独立临时 `.anycurve` / `.anyvoc` 文件。固定 33 点、选区与文件的完整定义见 [LFO_CURVE_TOOLS.md](LFO_CURVE_TOOLS.md)。

| ID | 操作 | 预期 / 失败判定 |
|---|---|---|
| T01 查看与独立状态 | 两个 LFO 分别改 Draw/Select、X/Y 网格、Snap 和选区；切换后再返回；暂存位移/缩放但不按 APPLY | 不改变声音参数或新增 Undo；每条 LFO 的视图设置互不串改。视图设置不属于音色存档，关闭编辑器后不保证保留 |
| T02 网格绘制 | 分别开关 X/Y 吸附，覆盖 1/2/4/8/16/32；在半格和半格两侧落笔，连续拖一笔，Undo/Redo | 各轴独立；半格取较大坐标。X 从连续鼠标位置选最近格，grid4 的点坐标 3.6→0、4→8；完整一笔一次 Undo，隐藏/切页后迟到拖动不继续写 |
| T03 点值精度 | Y16 取目标 0.125、Y32 取 0.0625；另用 SET 输入 0.12345678 | 原有声音参数步长为 **0.01**，分别实际约为 0.13、0.06、0.12；网格或精确输入不绕过宿主量化。SET 不受网格吸附影响，修改单点一次 Undo |
| T04 选区与反转 | 对非对称曲线选择 9–17 号点，分别 Flip X / Flip Y 后 Undo；再 All | 闭区间包含两端；X 反转顺序、Y 反号，不能用对称直线代替区分用例；Custom 选区外点完全不变，单次 Flip 一次 Undo |
| T05 位移与缩放 | 在 Linear/Step 中分别对选区设置 X 位移、宽度、Y 位移、高度，再 APPLY；含单点选区和越界读位 | X 单位为一个真实点，范围 −32…32；宽度 0.25…4、高度 0…4、Y −2…2。固定选区掩码，超出源范围保持端点、值限于 ±1，无循环回绕；每次 APPLY 一次 Undo |
| T06 身份与形状 | Basic Sine 先做身份 APPLY，再 Copy→Paste；Random 保存的曲线也做 Copy→Paste；已经 Custom 再重复同资产 | 身份 Flip/SET/APPLY 不改变点或插值时不改形状、不新增历史。Paste/Load 是明确资产替换，Basic/Random 即使 33 点相同也切 Custom，Undo 恢复原形状和隐藏点。已 Custom 且量化后同值不增加 Undo |
| T07 系统剪贴板 | 用真实 Copy/Paste 跨 LFO、跨实例；暂存并恢复原剪贴板内容；粘贴坏 XML | 转移完整 33 点与插值，不受当前选区限制，不转移速度/路由/其他 LFO；Copy 不改声音。无效内容有错误提示、声音和历史不变。平台模拟测试不能代替此项 |
| T08 原生曲线文件 | 用原生 SAVE/LOAD 保存到新 `.anycurve`，再加载；另试取消、损坏/超限文件和明确不可写的测试目标；导入任意 float 后重新导出 | 保存不改声音，加载整条曲线选 Custom；重新导出实际已量化的点值。文件最多 16 KiB，失败不部分写入、不损坏旧文件；取消无声音改动。只使用测试文件，不覆盖唯一原件 |
| T09 迟到回复与禁用 | 打开 LOAD 后改变音色/相关参数、切 LFO、隐藏 TOOLS 或关闭编辑器，再返回选择结果；切 Chaos 尝试操作 | 旧请求不覆盖新状态或访问已销毁界面。Chaos 禁止绘图、点编辑、替换；切回 Custom 保留点值。原生窗口模态阻止的路径记不适用，自动回调测试覆盖情况另记 |
| T10 声音、存档与布局 | 将自绘 LFO 路由到 Cutoff/Pan，执行一笔+选区变换+Undo/Redo；保存 `.anyvoc`，重开加载；默认/最小窗口检查 TOOLS | 参数与可听变化对应，音色保存恢复点、插值、形状和路由；工具设置不要求恢复。图、选区、全部按钮、错误提示及琴键可用；电平或截图不代替真人听音 |

手动步骤中若平台输入、对话框或剪贴板无法实际完成，记录“未测”及原因，不把可控测试服务的成功换算成原生通过。记录实际读回的点值；不要因 0.125→0.13 等已约定的宿主量化而悄然扩展参数范围或更新旧 schema。

### Chaos 专项人工用例

从 INIT 或指定工厂预设开始，记录 LFO 序号、算法、播放模式与速度。至少覆盖 LFO1 和 LFO10；自动测试负责全部十条源路径，但不能替代真实窗口的操作记录。

| ID | 操作 | 预期 / 失败判定 |
|---|---|---|
| C01 算法与声音 | 打开 LFO Lab，在同一持续音与同一路由上依次选 Lorenz、Rossler；试听 Motion / Butterfly Current 与 Texture / Spiral Drift | 统一 Shape 共 8 项；两算法能持续改变调制和声音，图显示实际 XYZ；不是播放固定循环波形或静态装饰图 |
| C02 Free 连续性 | Free 下保持音频处理，松键/再按键；在 DAW 中另测播放头跳转 | 演化不因 MIDI 或播放头跳转复位；松键后若音频仍处理，Free 轨迹可继续变化，不能误判为假动画 |
| C03 Trigger 与连奏 | Trigger 下释放至声部结束后重按同音；另测 Mono 重触发、Legato 重叠音与松开最新音后的回退 | 同一 LFO 新声部从确定初态开始；Mono 重触发会重置，Legato 重叠及回退保持旧轨迹。不同 LFO 的初态略不同，不要求彼此相同 |
| C04 Envelope 与切换 | Envelope、速度 2 下持续按键超过 0.5 秒，再继续保持；改算法或 Free/Trigger/Envelope 模式；全松键后再换算法 | 一个名义周期后 XYZ 与图轨迹保持；算法/模式切换重新初始化，下一次处理/发声显示新状态；不得把旧算法的闲置快照标成新算法数据 |
| C05 速度与同步 | 在同一 Chaos 路由上比较 Free Hz、Tempo、Audio Rate；改 BPM、分割、Follow Swing 与 Swing Amount，再切回 | 速度改变演化快慢；三个模式保留各自参数；Sync 优先于 Audio Rate，Follow Swing 仅 Tempo 下有效。速度不是固定重复周期，快设定也不保证实时负载足够 |
| C06 禁用与保存值 | 先在周期形状设 Phase/Direction 并画 Custom；切 Chaos，尝试拖 Phase、修改 Direction、绘图和五个曲线预置，再切回 Custom | Chaos 中上述编辑禁用、Phase 拒绝源拖放，既有 Phase 调制不影响 Chaos；保存的 Phase/Direction/33 点仍在，返回周期/Custom 后恢复可编辑 |
| C07 真实轨迹快照 | 持音截取两帧，记录 XYZ；Envelope 到达保持点后再等数秒并多次截图/缩放；确认停止音频处理时也检查 | 演化时读数和轨迹会更新；保持或音频处理确实停止后，不因重绘、缩放、等待而制造新运动。自动测试另将 UI 属性与引擎 XYZ 快照逐轴比较 |
| C08 撤销与重开 | 从已绘制 Custom 切 Lorenz、再切 Rossler，逐步 Undo/Redo；另存 `.anyvoc`，关实例重开并加载；DAW 再做 D05 | 每次算法选择一次撤销，隐藏的旧 Shape/Kind/Custom 点不丢失；加载恢复算法、速度、模式、路由及保存值。运行中的混沌积分位置不是音色存档，不要求加载后接续同一瞬间 |

出现轨迹冻结时，先确认是 Envelope 已完成、音频停止，还是持续处理的 Free/Trigger 意外冻结；将原因和实际 XYZ 一并写入记录。不能只凭两张图片“看起来不同”认定独立 ODE 数值验证通过。

## 6. DAW 验收矩阵

| 宿主 / 平台 | 当前状态 | 下一步 |
|---|---|---|
| FL Studio，Windows x64 | 未完成扫描/加载/工程重开；早期用户目录扫描未发现插件 | 安装完整 bundle 到宿主支持的 VST3 路径，重新扫描，执行 D01–D09 |
| REAPER，Windows x64 | 未验证 | 配置 VST3 扫描路径，扫描后执行 D01–D09 |
| Ableton Live，Windows x64 | 未验证 | 启用 VST3 插件扫描，执行 D01–D09 |
| Cubase / Studio One / Bitwig，Windows x64 | 未验证 | 各宿主分别记录准确版本并执行 D01–D09，不能相互代替 |
| Logic / macOS | 当前不支持 | 未提供 macOS/AU 产物；不适用 Windows VST3 测试 |
| Pro Tools / AAX | 当前不支持 | 未提供 AAX 产物 |

安装时复制**整个** `Anyvoc.vst3` 文件夹，保留 `Contents/x86_64-win`。FL Studio 的具体菜单因版本而异，可按 [用户指南的安装步骤](USER_GUIDE.md#在-fl-studio-里使用)。系统级目录可能需用户确认管理员权限。记录实际扫描路径，不把目录存在当成宿主已经识别。

| ID | 操作 | 通过标准 |
|---|---|---|
| D01 扫描 | 清楚记录安装路径、扫描结果、插件标识/版本 | 被识别为立体声乐器；无黑名单、超时或扫描崩溃 |
| D02 发声 | 新工程插入实例；钢琴卷帘写音符与和弦，播放/停止 | 能听到且有输出；无卡音，编辑器可反复开关 |
| D03 节拍 | Tempo 模式下改 BPM，暂停/启动/循环/跳转播放头 | Follow Swing 和 LFO 模式符合约定；Free 同步位置与 Trigger/Envelope 的逐音触发区别可确认；无异常重触发 |
| D04 自动化 | 录制/绘制 Cutoff、Macro base、Phase、Rate Mode、`lfoNChaos`、LFO Point，以及新 `modNMacroEnabled/Target` 自动化；编辑时触发宿主写入，隐藏/显示 UI | 声音和显示跟随；旧 1192 参数映射保持；宏有效值不写回 base 自动化，宿主写入不会被过时 UI/文件回复覆盖 |
| D05 工程状态 | 使用导入素材、Custom LFO、包络和路由；保存工程，完全关闭宿主，重新打开 | 音色名称、参数、嵌入素材、自动化及声音恢复；原素材路径失效后仍可恢复已嵌入内容 |
| D06 多实例 | 至少两个不同音色，复制实例，独立修改/Undo | 实例不串音色或历史；一个实例关闭不影响另一个 |
| D07 渲染 | 同一 MIDI 工程实时与离线导出，记录尾音、随机参数设置和增益 | 无静音/漏音/截断异常；关闭随机性并设置确定触发时再做严格波形比较，有随机性不强求逐字节相同 |
| D08 配置 | 44.1/48/96 kHz 与 64/128/256/512 样本中设备支持的组合，开关旁通/换预设 | 无崩溃/非有限值/卡音；不足以实时运行的组合记实际掉音与负载，不能当通过 |
| D09 MIDI/界面 | 力度、弯音、CC1、延音、缩放、键盘焦点、关闭重开 UI | MIDI 功能与焦点可用，宿主快捷键和插件快捷键行为明确；记录冲突 |

每个宿主独立填写结果。缺工具、无许可证、无硬件或未执行记为“未测”，不要写“预计兼容”。

## 7. 性能与发布门槛

**CPU 必须测量，不能设一个未经测量的“低于 10% 就通过”。**记录 CPU 型号/核心数、电源模式、操作系统、音频设备/驱动、采样率、缓冲、宿主版本、实例数、复音、Unison、颗粒密度、Warp、调制/效果配置及 UI 是否打开。用固定 MIDI 和同一音色分别测单音、16 复音、三振荡器/高 Unison、颗粒、32 路调制、Audio Rate/相位反馈等代表性负载。

建议预热后每项持续演奏至少 60 秒、重复三次，报告平均值、峰值、掉音/underrun 次数和测量工具。宿主 DSP 百分比与任务管理器 CPU 百分比要分开记录。音频回调时间若有测量，报告最坏块；块期限为 `缓冲样本数 / 采样率`，例如 48 kHz、256 样本约 5.33 ms。平均离线渲染时间不能证明所有音频块都按时完成。

Chaos 的可复现离线基准入口如下。它不属于当前 75 组 EngineTests，退出 0 只说明基准完成且测试音频未意外静音，没有内置“实时性能通过”阈值。[0.17 基准记录](benchmark-chaos-0.17.txt)仅对应当时产物，不能作为本轮新测量。

```powershell
$engineTests = Join-Path $repoRoot 'build/AnyvocEngineTests_artefacts/Release/AnyvocEngineTests.exe'
& $engineTests --benchmark-chaos 2>&1 |
    Tee-Object -FilePath (Join-Path $runDir 'benchmark-chaos.txt')
if ($LASTEXITCODE -ne 0) { throw 'Chaos benchmark failed to complete' }
```

基准固定 Release、48 kHz、256 样本、16 个错开发声的持续声部、10 个 LFO、一个波表振荡器、Unison 1、滤波/效果关闭。八种场景包括 Sine/Lorenz/Rossler 的 10 Hz Trigger、Sine 1000 Hz Trigger、Lorenz 1000 Hz Free、两种 Chaos 1000 Hz Trigger，以及无任何路由的 Rossler 1000 Hz Trigger。最后一项仍计算 160 个声部/LFO 状态，用于暴露“没有路由也有成本”的情况。每项预热后计算 2 秒音频、三轮轮换顺序，输出每轮耗时和中位数 `wall/audio`。

宏目标另有独立离线入口，同样不计入 75 组，也不以退出码 0 宣称实时负载已通过：

```powershell
& $engineTests --benchmark-macros 2>&1 |
    Tee-Object -FilePath (Join-Path $runDir 'benchmark-macros.txt')
if ($LASTEXITCODE -ne 0) { throw 'Macro benchmark failed to complete' }
```

宏基准使用 48 kHz、256 样本、16 个不同力度且错开发声的持续声部、32 行路由、单波表振荡器、Unison 1、关闭效果。四项分别为旧宏源路由、带主/AUX 曲线的宏反馈及 Phase 反馈、997 Hz LFO→宏→Pitch/Pan、997 Hz LFO→宏→双滤波；仅最后一项开启双滤波。每次计算 2 秒音频，共三轮轮换顺序，计时包含每块 `setSettings()`，不含构造与预热。保留每轮数值、中位数及 CHECKSUM；它不覆盖 UI/宿主调度、设备回调期限，也不证明并发路由编辑原子一致。

**记录标准：**保留全部场景、CHECKSUM、硬件与当时后台负载；比较同机同配置数据，不删掉最慢场景。`wall/audio < 1` 只代表整段离线吞吐快于音频时长，不能证明最坏回调准时、UI 开启时无掉音、16 声部以外负载可用，或某个 DAW 的 DSP 百分比。实际实时测试仍按上述至少 60 秒、三次和掉音计数执行。

发布前应预先声明目标硬件和可支持的工程负载，在该条件下验证无掉音、无持续爆音和无超时；若只有平均离线数据，只能发布该数据与限制，不能声称最坏实时性能已经通过。复杂音色混叠和音质仍需单独听音/频谱评价。

| 门槛 | 必须保留的证据 | 阻止相应发布声明的情况 |
|---|---|---|
| 版本一致 | 源码提交、构建日志、两个二进制 FileVersion 与 SHA-256 | 版本不符、混用旧模块、未构建的功能混入文档 |
| 回归 | 四套测试零失败、失败后的修复与复测记录 | 未解释失败、删除断言、无依据放宽容差 |
| 参数兼容 | 完整保留 0.18 的 **1192** 参数定义和顺序；尾部逐行追加 `MacroEnabled/Target` 共 64，VersionHint14，总 **1256**；旧 33 点仍为 **0.01** 步长 | 以工具精度为由改宿主范围、重排/重用 ID 或改变旧映射 |
| 状态兼容 | schema14 必需全部参数；schema1–13 缺少新尾参数时补 false/0；未来 schema15 和损坏/重复/缺失当前字段原子拒绝；`.anyvoc`/工程保留整条 19 字段路由、素材与声音 | 损坏状态部分覆盖声音、旧内嵌素材丢失，或把 `.anycurve` 混同完整音色 |
| 工厂与默认声音 | 旧 26 个工厂预设及序号不变，追加 Prism Handle / Breathing Prism，总 28；旧 `--demo` 与真正留存 **0.18 EXE 新渲染**的基线逐字节一致 | 静音、重排旧预设、用候选 WAV 假作旧基线或无说明更新基线 |
| 宏目标与有效值 | 七组 Engine、六组 Processor 新回归；八宏独立声音参考、主/AUX 一致快照、生命周期、reset pending、32 行完整移动及回调；原生与性能结果分开 | 用 UI 本地计算代替实际快照、EV 写回 base/历史、旧声部泄漏、满表覆盖，或把完成移动后的精确比较误称并发原子发布 |
| 曲线工具与资产 | 七组独立模型测试、六组真实控件集成测试；原生专项结果另记；Custom 替换语义、量化、一次 Undo、严格 ≤16 KiB XML 和失败保护 | 身份操作意外改声音、导入未选 Custom、迟到写入、未选点/其他 LFO 被改、解析失败部分覆盖、只用平台模拟宣称原生通过 |
| 新版扩展 | 新参数只能追加；新 schema 明确旧版本缺省值及未来版本拒绝规则 | 用加 choice 项目的方式悄然改变旧宿主自动化映射；未经实现/测试即改 schema |
| 模块规范 | 最终模块 pluginval strictness5 SUCCESS | 只检验较早二进制，或日志空白/失败/超时 |
| 音频/UI | 演示检查、最小尺寸、真实独立版操作；DAW范围另记 | 用截图替代交互，用电平替代听音，用 pluginval 替代 DAW |
| 发布包 | ZIP CRC、无重复/越界路径、精确文件集合、逐文件字节匹配、许可证、版本/hash、必需演示 | 漏文件、旧二进制、新源码混入旧版、损坏压缩包 |

构建、演示和文档冻结后才打包：

```powershell
# 使用第 3 节已经渲染并审核的本轮文件；这里只更新 dist 根级暂存名。
New-Item -ItemType Directory -Force -Path (Join-Path $repoRoot 'dist') | Out-Null
Copy-Item -LiteralPath (Join-Path $runDir 'Legacy.wav') -Destination (Join-Path $repoRoot 'dist/Anyvoc-Demo.wav') -Force
Copy-Item -LiteralPath (Join-Path $runDir 'Lfo-Motion.wav') -Destination (Join-Path $repoRoot 'dist/Anyvoc-v0.16-Lfo-Motion-Demo.wav') -Force
Copy-Item -LiteralPath (Join-Path $runDir 'Chaos.wav') -Destination (Join-Path $repoRoot 'dist/Anyvoc-v0.17-Chaos-Demo.wav') -Force
Copy-Item -LiteralPath (Join-Path $runDir 'Macro.wav') -Destination (Join-Path $repoRoot 'dist/Anyvoc-v0.19-Macro-Demo.wav') -Force
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/package.ps1
if ($LASTEXITCODE -ne 0) { throw 'Windows packaging failed' }
python ./scripts/package-source.py
if ($LASTEXITCODE -ne 0) { throw 'Source packaging failed' }
python ./scripts/verify-release.py
if ($LASTEXITCODE -ne 0) { throw 'Release package verification failed' }
```

Windows 打包脚本会检查二进制版本。0.19 明确必需三个演示：`Anyvoc-v0.16-Lfo-Motion-Demo.wav`、`Anyvoc-v0.17-Chaos-Demo.wav`、`Anyvoc-v0.19-Macro-Demo.wav`；旧名字仅是打包约定，本次内容来自当前 0.19 程序重新渲染。其他历史演示只在已有时收录。先完成音频检查，不能用空文件满足存在性要求。同版本打包会覆盖 ZIP 并更新输出目录。以上是读者复现流程；本次 CI 文档归档没有执行它。复现时使用独立副本或先归档已有产物，避免残留文件混入；**已交付的 0.19 及更早发行目录、ZIP 和历史报告保持冻结**。

[发布包校验脚本](../scripts/verify-release.py) 从 CMake 读取版本，核对两个 ZIP 的 CRC、Windows 安全且唯一的路径、精确文件集合与每个文件的原始字节；同时核对解压目录、两个产物与构建目录的四个 FileVersion、二进制哈希及三个必需演示的完整 20 秒/48 kHz/24-bit/双声道 PCM。源码包必须包含 `.gitattributes`、`.gitmodules`、`.github/`、Chaos 模型、`LfoCurveDocument.h/.cpp` 及固定 JUCE 源码/许可。脚本不替代 CTest、pluginval、听音或性能测试；未知将来版本不会自动获得某个新功能已验收的结论。

**包校验通过标准：**脚本退出 0，并在包外生成 `dist/Anyvoc-0.19.0-PackageVerification.json`，其中 CRC、安全唯一路径、精确集合、逐文件匹配全部通过，版本一致，三个必需 demo 均有格式及哈希记录。0.19 本机两包已按交付时冻结输入达到该标准。本次后补 CI 文档不写入原包；今后若制作包含更新内容的新包，须另行归档、重新打包与校验，大小与哈希以对应外置报告为准。报告不放入 ZIP，避免包含自身哈希。可单独运行 `python scripts/verify-release.py --self-test-paths` 检查路径规范逻辑；这不是发布包验收。分析与校验工具现已随源码提供，不再要求使用临时 `build/validation-baseline` 下的旧脚本。

文档修改后重新打包会改变 ZIP 哈希；重新核对对应文件及包报告。若没有改变声音/生产代码，不需要为了文档重跑所有 DSP，但不得继续引用旧包哈希作为新包的校验值。

## 8. 验收与缺陷记录模板

复制到单独的验收记录，每次执行保留独立文件；敏感目录/用户名可在公开副本中脱敏，原始记录另存。

```text
日期/时区：
测试人员：
Anyvoc 版本 / Git commit / 工作区是否干净：
Standalone / VST3 文件版本、SHA-256：
Windows / CPU / 内存 / 电源模式 / DPI：
宿主与准确版本（独立版填 Standalone）：
声卡、驱动、采样率、缓冲、MIDI 设备：
音色/素材/MIDI 工程及其 hash：

检查 ID / 套件名：
状态：PASS / FAIL / 未测 / 不适用
完整命令或逐步操作：
预期结果：
实际结果（数值、可听现象、UI行为）：
退出码 / 失败组名 / pluginval seed：
日志、截图、WAV、工程文件位置：
可复现次数 / 总尝试次数：
性能数据与测量工具：
问题链接、影响范围、是否阻止发布：
修复 commit：
复测版本、步骤、结果、证据（保留首次失败记录）：
恢复用户设置/测试文件清理情况：
```

报错时优先给最小工程、最少音符和最短操作序列；保留崩溃日志与原始文件。对偶发错误记录触发频率，不通过反复重跑直到一次成功来宣称已经修复。
