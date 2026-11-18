# Anyvoc

面向 Windows 的原创波表、采样与颗粒合成器，提供**独立程序和 VST3 乐器插件**，优先完善初学者的调音体验。

**当前版本：0.20.1，本机限定范围验收及 Windows／完整源码包校验通过。** 完整 213 组测试、VST3 校验、四份音频回归和独立版启动检查通过；本轮证据见[验证记录](docs/VALIDATION-0.20.1.md)。旧版安装包保持冻结。

在独立版中，点 **CHOOSE A SOUND → 选音色 → LOAD SOUND**，再按住底部琴键就能试听。第一次使用从[新手开始](docs/快速开始.txt)读起。

项目以 Serum 2 的功能与操作体验为长期参考。**目前没有达到完整功能或声音等价，不支持 Serum 预设，也不包含 Xfer 的代码、素材或品牌界面。**

## 当前做到了什么

0.20.1 修复一种可复现的持续音：开启琶音器、踩下延音踏板、松键后，收到 MIDI“重置所有控制器”（CC121）仍继续产生音符。现在会清除仅由踏板保持的键，保留实际按住的键和正常尾音。参数、音色格式和旧预设数量不变。

- **先选音色，再调声音。** 默认 PLAY 页提供 `CHOOSE A SOUND → LOAD SOUND → 底部琴键`，集中显示 TONE、NOTE SHAPE、SPACE；SYNTH、FILTER、MOD、FX 完整页面继续保留，编辑的是同一个音色。
- **独立复制调制路由。** COPY 将一行的全部 19 个设置复制到第一条完全空行，支持一次撤销。不会覆盖保存了设置的草稿；启用的副本会立即参与调制。旋钮菜单也支持一次删除通往该目标的所有有效来源路由。
- **让音频接收完整接线。** 32 行共 608 个路由字段统一发布；组合编辑进行中，音频继续使用上一份完整接线，完成后才接收新接线。范围与中断规则见[路由说明](docs/ROUTE_PUBLICATION.md)。

已有能力包括三个振荡器、双滤波器、双层 Warp、四条包络、十个 LFO、32 行调制矩阵、八个可受调制的宏、四类效果、琶音器和 28 个工厂音色。波表、采样、颗粒、Custom／Chaos LFO 与曲线工具继续可用。**本轮不新增声音参数或音色格式：仍为 1,256 个宿主参数、schema 14、28 个工厂音色。**

0.20 的 **210 组本机与云端测试、pluginval strictness 5、四份音频回归及包校验**已通过；[指定提交的云端证据](docs/GITHUB-CI-0.20.md)已归档。另完成含采样和调制的[真实退出／重启／加载检查](docs/NATIVE_RESTART-0.20.md)。这些历史证据只对应 0.20 二进制；0.20.1 的测试与产物单独记录。

| 想做什么 | 从这里看 |
| --- | --- |
| 第一次选音色、发声、调音 | [新手开始](docs/快速开始.txt) · [PLAY 操作说明](docs/UI_WORKFLOW.md) |
| 了解现在的能力与未测范围 | [项目现状](docs/STATUS.md) |
| 了解接下来怎么做 | [开发路线](docs/ROADMAP.md) |
| 使用已有完整编辑功能 | [使用手册](docs/USER_GUIDE.md) |
| 编译或参与开发 | [构建指南](docs/BUILDING.md) · [测试指南](docs/TESTING.md) |
| 查验证结果与历史证据 | [0.20.1 当前记录](docs/VALIDATION-0.20.1.md) · [0.20 归档](docs/VALIDATION-0.20.md) · [0.20 云端记录](docs/GITHUB-CI-0.20.md) |

## 运行与构建

本机入口：`dist/Anyvoc-0.20.1-Windows/Anyvoc.exe`。使用 Windows 发布 ZIP 时，先完整解压再运行根目录的 `Anyvoc.exe`。Windows 与完整源码包已核对 CRC、路径、文件集合、逐文件内容及二进制版本，最终记录为包外 `dist/Anyvoc-0.20.1-PackageVerification.json`。0.20 及更早版本不覆盖；0.20.1 推送后的云端结果须按新提交单独核对。

Git 仓库保存源码、文档和验证记录，不提交 EXE、VST3、WAV 或发布 ZIP；尚未上传 GitHub Release 附件。GitHub 的 “Download ZIP” 是源码，不能直接运行。

安装 Visual Studio 2022 C++ Build Tools、Windows SDK、CMake ≥ 3.22 和 Git 后，可在 PowerShell 中构建所检出的版本：

```powershell
git clone --recurse-submodules https://github.com/Bad0RANG3/Anyvoc.git
cd Anyvoc
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
```

版本以检出提交的 `CMakeLists.txt` 为准。独立版输出为 `build/Anyvoc_artefacts/Release/Standalone/Anyvoc.exe`；VST3 输出为 `build/Anyvoc_artefacts/Release/VST3/Anyvoc.vst3`，安装时须保留整个插件目录。JUCE 固定为 8.0.12，详细环境与验证命令见构建指南。

## 还缺什么

更多调制目标、多采样/SFZ、频谱声源、效果总线、Clip、MPE 和其他平台尚未完成。**FL Studio 等 DAW、外接 MIDI 和真人听音仍待实测**；pluginval 与屏幕输出电平不能代替这些检查。已核对的重启案例仅覆盖一份内嵌采样和两条调制；多声源素材组合和实时高负载仍需验证。琶音器目前合并输入通道及同音高键的所有权，CC121 修复没有改变这项限制。

开发须保留旧参数 ID、顺序和状态兼容，为行为变化补充音频、状态或界面验证。原创代码采用 [AGPL-3.0](LICENSE)，依赖与分发要求见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。本项目独立于 Xfer Records。
