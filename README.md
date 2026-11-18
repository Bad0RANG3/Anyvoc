# Anyvoc

面向 Windows 的原创波表、采样与颗粒合成器，提供**独立程序和 VST3 乐器插件**，优先完善初学者的调音体验。

**当前版本：0.20.0，本机限定范围验收及 Windows／源码包交付已完成。** 运行入口见下方，未测范围见文末。

在 0.20 独立版中，点 **CHOOSE A SOUND → 选音色 → LOAD SOUND**，再按住底部琴键就能试听。第一次使用从[新手开始](docs/快速开始.txt)读起。

项目以 Serum 2 的功能与操作体验为长期参考。**目前没有达到完整功能或声音等价，不支持 Serum 预设，也不包含 Xfer 的代码、素材或品牌界面。**

## 0.20 做了什么

- **先选音色，再调声音。** 默认 PLAY 页提供 `CHOOSE A SOUND → LOAD SOUND → 底部琴键`，集中显示 TONE、NOTE SHAPE、SPACE；SYNTH、FILTER、MOD、FX 完整页面继续保留，编辑的是同一个音色。
- **独立复制调制路由。** COPY 将一行的全部 19 个设置复制到第一条完全空行，支持一次撤销。不会覆盖保存了设置的草稿；启用的副本会立即参与调制。旋钮菜单也支持一次删除通往该目标的所有有效来源路由。
- **让音频接收完整接线。** 32 行共 608 个路由字段统一发布；组合编辑进行中，音频继续使用上一份完整接线，完成后才接收新接线。范围与中断规则见[路由说明](docs/ROUTE_PUBLICATION.md)。

已有能力包括三个振荡器、双滤波器、双层 Warp、四条包络、十个 LFO、32 行调制矩阵、八个可受调制的宏、四类效果、琶音器和 28 个工厂音色。波表、采样、颗粒、Custom／Chaos LFO 与曲线工具继续可用。**本轮不新增声音参数或音色格式：仍为 1,256 个宿主参数、schema 14、28 个工厂音色。**

当前独立版与 VST3 已构建，**完整 210 组测试和 pluginval strictness 5 通过**；四份演示与冻结的 0.19 文件逐字节一致，宏演示独立分析通过。实际窗口检查涵盖 PLAY、COPY 三个入口、撤销与副本独立编辑、按目标批量删除和最小窗口；发行包完整性校验已通过。初轮失败、修正与验证边界见[0.20 记录](docs/VALIDATION-0.20.md)。对应提交的云端验证尚未记录。

| 想做什么 | 从这里看 |
| --- | --- |
| 第一次选音色、发声、调音 | [新手开始](docs/快速开始.txt) · [PLAY 操作说明](docs/UI_WORKFLOW.md) |
| 了解现在的能力与未测范围 | [项目现状](docs/STATUS.md) |
| 了解接下来怎么做 | [开发路线](docs/ROADMAP.md) |
| 使用已有完整编辑功能 | [使用手册](docs/USER_GUIDE.md) |
| 编译或参与开发 | [构建指南](docs/BUILDING.md) · [测试指南](docs/TESTING.md) |
| 查验证结果与历史证据 | [0.20 当前记录](docs/VALIDATION-0.20.md) · [0.19 归档](docs/VALIDATION-0.19.md) · [0.19 云端记录](docs/GITHUB_CI-0.19.md) |

## 运行与构建

本机 0.20 入口：`dist/Anyvoc-0.20.0-Windows/Anyvoc.exe`。使用 Windows 发布 ZIP 时，先完整解压再运行根目录的 `Anyvoc.exe`。Windows／完整源码包已核对文件集合、逐文件内容和 0.20.0 版本；最终记录在包外的 `dist/Anyvoc-0.20.0-PackageVerification.json`。0.19 及更早的冻结包保持原样。

Git 仓库保存源码、文档和验证记录，不提交 EXE、VST3、WAV 或发布 ZIP；尚未上传 GitHub Release 附件。GitHub 的 “Download ZIP” 是源码，不能直接运行。

安装 Visual Studio 2022 C++ Build Tools、Windows SDK、CMake ≥ 3.22 和 Git 后，可在 PowerShell 中构建所检出的版本：

```powershell
git clone --recurse-submodules https://github.com/Bad0RANG3/Anyvoc.git
cd Anyvoc
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
```

版本以检出提交的 `CMakeLists.txt` 为准。独立版输出为 `build/Anyvoc_artefacts/Release/Standalone/Anyvoc.exe`；VST3 输出为 `build/Anyvoc_artefacts/Release/VST3/Anyvoc.vst3`，安装时须保留整个插件目录。JUCE 固定为 8.0.12，详细环境与验证命令见构建指南。

## 还缺什么

更多调制目标、多采样/SFZ、频谱声源、效果总线、Clip、MPE 和其他平台尚未完成。**FL Studio 等 DAW、外接 MIDI 和真人听音仍待实测**；pluginval 与屏幕输出电平不能代替这些检查。完整素材恢复、程序重启后恢复和实时高负载体验也需要继续验证。

开发须保留旧参数 ID、顺序和状态兼容，为行为变化补充音频、状态或界面验证。原创代码采用 [AGPL-3.0](LICENSE)，依赖与分发要求见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。本项目独立于 Xfer Records。
