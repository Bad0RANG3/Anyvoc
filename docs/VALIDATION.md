# Anyvoc 0.20 验证记录 — 2026-11-18

本版增加默认 PLAY 页、独立复制调制路由与完整路由图发布，保持 **1,256 个宿主参数、schema 14、28 个工厂音色**。最终四套自动测试 **210 组全部通过**，pluginval、音频分析和下述限定原生操作通过。这是 Windows 独立版／VST3 的限定验收，不代表完整 Serum 2 功能或声音等价。上一版见 [0.19 归档](VALIDATION-0.19.md)。发行包完整性另列在本页末尾。

## 改动范围

PLAY 使用已有真实参数，集中提供 Filter A、振幅 ADSR、Delay／Reverb Mix 和完整页面入口。Filter A 旁通时禁用对应控制；包络 BPM 模式下，时间旋钮显示节拍并禁止秒值写入。导航与原页面索引兼容，默认页变化所影响的测试显式选择所需页面。[工作流说明](UI_WORKFLOW.md)记录操作及后续持音试听的生命周期要求。

COPY 从矩阵行、详情及旋钮菜单进入，把全部 19 字段复制到第一条完全默认的空行；保留 Off／None 草稿，成功形成一次 Undo。启用的副本立即参与调制。按目标 Remove-all 将匹配的有效来源行作为一次批量编辑，包含旁通行，保留 Source Off 草稿。

处理器将 32×19 路由字段发布为完整图。音频每块只尝试一次读取；遇到写入中途继续使用上一份完整图。组合编辑、Undo／Redo、工厂音色与验证后的恢复被明确包围；连续旋钮手势的每次实际修改仍可进入下一音频块。宿主重入或视图失效时停止后续写入，**不会回滚已通知宿主的值**。合同与边界见[完整路由发布](ROUTE_PUBLICATION.md)。这不是整个音色原子切换，也不合并宿主分开提交的自动化事件。

[源码定义核对](source-compatibility-0.20.json)确认参数布局和 28 个工厂预设与真实 0.19 基线 `bf3efc6ecde3044409ec6693c6b98f7eb65371b3` 在统一换行后逐字节一致。它不代替运行时状态和声音检查。

## 构建、失败与自动测试

环境为 Windows x64、Visual Studio 2022／MSVC 19.44、Windows SDK 10.0.26100、CMake 4.4.4、固定 JUCE 8.0.12。保留已有类型转换等警告，不宣称零警告。[配置](configure-0.20.txt)、[首次生产构建](build-0.20-production.txt)、[访问权限修复后构建](build-0.20-production-fix.txt)、[PLAY 与批量清空构建](build-0.20-play-production.txt)及[全部目标构建](build-0.20-initial.txt)均留原始记录。首次因直接访问私有控件编译失败，改用公开焦点方法后通过。

[首次完整 CTest](ctest-0.20-initial.txt)用时 **196.11 秒**，共 **209 PASS／1 FAIL**；[逐组日志](test-details-0.20-initial.txt)与 [JUnit](ctest-0.20-initial.xml)保留失败。Engine 75、Processor 118／119、Wavetable 10、PresetLibrary 6。失败项是 COPY UI 生命周期测试。

该失败定位为两处测试辅助代码问题：销毁编辑器时，后来注册的测试监听器被 JUCE 的反向列表遍历重复访问；切页后，查找助手又跳过了隐藏矩阵。将测试监听器注册移至编辑器创建之前，并在切页前保存矩阵 SafePointer 后修复。生产代码无需改动，参数、通知配对与取消断言全部保留。[诊断一](processor-0.20-copy-diagnostic-1.txt)、[诊断二](processor-0.20-copy-diagnostic-2.txt)、[测试目标重建](build-0.20-copy-fix-2.txt)和[定向复测](processor-0.20-copy-fix-2.txt)记录修复过程；该组最终 **1／1 通过**。

新增九组 Processor 测试覆盖完整图并发发布、带平滑历史的移动、实际宿主通知值、COPY 全字段与重入保护、真实控件生命周期、PLAY 接线／禁用／拖放，以及按目标批量清空。并发检查在 begin／value／end 通知内暂停修改线程，由另一个实际工作线程运行音频，确认中途保持旧图、结束后接收新图；不是只在 helper 返回后渲染。移动回归保留逐样本严格相等检查，其他音频期望使用独立数值参照。

修复后的[最终完整 CTest](ctest-0.20-final.txt)退出码 0，**210 PASS／0 FAIL，用时 198.69 秒**。[逐组明细](test-details-0.20-final.txt)保留全部结果；[JUnit](ctest-0.20-final.xml)记录四个套件。定向复测不重复计入总数。

| 套件 | 通过组数 | 耗时 |
| --- | ---: | ---: |
| EngineTests | 75 | 4.85 秒 |
| ProcessorTests | 119 | 193.14 秒 |
| WavetableTests | 10 | 0.06 秒 |
| PresetLibraryTests | 6 | 0.63 秒 |

[VST3 pluginval strictness 5](pluginval-0.20-initial.txt)已 **SUCCESS**，随机种子 `0x7910988`，退出码 0、stderr 为空。它验证的是当前 0.20.0 模块，不代表 FL Studio 实际加载或工程恢复。

## 音频兼容与二进制

用真实留存的 0.19 ProcessorTests EXE [重新渲染旧演示](render-0.19-before-0.20.txt)，再用 0.20 重新生成 [Legacy](render-0.20-legacy.txt)、[Motion](render-0.20-motion.txt)、[Chaos](render-0.20-chaos.txt)、[Macro](render-0.20-macro.txt)。[独立比较](demo-compatibility-0.20.json)确认四份当前 WAV 均与冻结 0.19 Windows ZIP 中对应文件逐字节一致。没有因为文件已存在就当作本轮重新渲染。

[宏演示标准库分析](audio-analysis-0.20.json)显式使用 `--version 0.20.0 --baseline-version 0.19.0`，结果 PASS。Macro 为 20 秒、48 kHz、24-bit 双声道，RMS 0.013233501，峰值 0.047289729，无 PCM 满幅样本；相邻片段差值 RMS 为 0.003614645、0.002561561、0.001428569。数值差异不证明主观音质或所有音色兼容；本轮没有真人听音，也没有重新进行性能基准。0.19 性能结果仍只属于其原始条件。

| 当前文件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| Standalone `Anyvoc.exe` | 9,682,944 | `c366f6a6e829fa30d424252dae802024257a69d19f2388a0d7db0d947a25dcb9` |
| VST3 模块 `Contents/x86_64-win/Anyvoc.vst3` | 10,222,080 | `92d1102d542887dc54ee0d1be221dfbadaabe4d591847e7f6d3d0d9672a92634` |

两文件版本均为 **0.20.0**。COPY 测试修复没有改动生产源码或以上二进制；pluginval 与下面两场原生操作均对应这些文件。

## 预览与独立版原生操作

[预览生成](preview-0.20-initial.txt)完成 **92 张 PNG**。实际视觉复核六张：PLAY 默认／最小尺寸、SYNTH 默认尺寸，以及 Matrix detail／expanded／last-page 的最小尺寸；导航、COPY、卡片、行控制和底部区域无观察到的裁切。其余 86 张虽已生成，不声称逐张人工看过。当前图例见 [PLAY](images/Play.png)、[最小矩阵详情](images/Matrix-detail-small.png)。

两场操作均使用实际 Windows 独立版窗口，结束后关闭自建实例并恢复用户设置，恢复前后 SHA-256 一致。原生深度值设置包含 UI Automation 操作，不冒充鼠标拖拽。

- [PLAY 原生记录](ui-play-native-0.20.json)：默认页面；滤波旁通及撤销；鼠标改变 cutoff 后跨页同步／撤销；BPM 包络显示；包络面板打开时拖 LFO 2 到共振并留在 PLAY；最小内容尺寸 1008×684；C4 与绿色输出活动；打开浏览器。浏览器检查没有加载另一预设，绿色电平不等于听音。
- [COPY 原生记录](native-copy-0.20.json)：行复制、一次 Undo／Redo、独立修改副本、详情复制、最小布局、PLAY 旋钮菜单复制并进入副本，以及四条匹配路由批量清空后一次 Undo 全部恢复。

原生范围不包含全 19 字段手工核对、满表、最后一页复制、宿主回调中断、外接 MIDI、本轮完整 SAVE／退出／重启／LOAD 或 DAW；自动测试覆盖其中部分，但不能冒充原生执行记录。

## 交付与后续验收

Windows 包与包含固定 JUCE 的完整源码包已生成，并通过 `scripts/verify-release.py` 校验：ZIP 成员集合准确、CRC 正常、全部文件逐字节匹配当前输入，解压目录及 EXE／VST3 与构建产物一致，包内文档与本次交付文档一致。Motion、Chaos、Macro 三份必需试听的格式、长度和完整性已检查。更新交付状态后的文档随最终重新打包再校验，不沿用较早包的文件哈希。

本机产物为 `dist/Anyvoc-0.20.0-Windows/`、`dist/Anyvoc-0.20.0-Windows-x64.zip` 和 `dist/Anyvoc-0.20.0-Source.zip`。逐文件报告与 ZIP 的最终字节数／SHA-256 位于 **`dist/Anyvoc-0.20.0-PackageVerification.json`**，该报告留在 ZIP 外，避免自身哈希递归依赖。验证命令与分发边界见 [BUILDING.md](BUILDING.md)。

冻结 0.19 包再次核对未改变：Windows ZIP SHA-256 为 `774391504e7e1f3d9925ee1ab1d7189a9c7c0c89c3fc7a9a60dadfdf56638ef7`，源码 ZIP 为 `cf7066e3f736c569e9583a849c7c68f989fb258b555e73c04801ec6d9efcf1b0`。后补文档不追溯改变历史包。

对应 0.20 提交的 GitHub Actions 尚未记录结果；[0.19 云端绿灯](GITHUB_CI-0.19.md)只适用于其明确提交。GitHub 仓库存代码、文档和证据，没有上传 EXE／VST3／WAV／ZIP 或 GitHub Release 附件。

下一步按[路线图](ROADMAP.md)先验证真实保存与进程重启恢复（含导入素材）、演奏生命周期和复杂路由回调峰值，再扩展声音能力。FL Studio 扫描、自动化、工程重开、不同采样率／缓冲和实时无掉音必须单独实测；操作步骤与通过标准见[验收指南](TESTING.md)。macOS／Linux、其他 DAW 与 Serum 预设兼容均未获得本轮验证。
