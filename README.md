# Anyvoc

面向 Windows 的原创波表、采样与颗粒合成器，提供 **独立程序和 VST3 乐器插件**。当前版本为 **0.17.0**，优先完善独立版的调音体验。

项目以 Serum 2 的功能与操作体验为长期参考，目前仍在开发中。**尚未实现完整功能或声音一致性，不支持 Serum 预设，也不包含 Xfer 的代码、素材或品牌界面。**

![Anyvoc Chaos LFO Lab](docs/images/LFO-Chaos-Evolved.png)

| 想了解什么 | 从这里看 |
| --- | --- |
| 目前做到哪、哪些已经验证 | [项目现状](docs/STATUS.md) |
| 下一步做什么、每阶段怎样算完成 | [开发路线](docs/ROADMAP.md) |
| 如何测试、验收、记录失败 | [测试与验收](docs/TESTING.md) |
| 从 GitHub 克隆、编译、运行、打包 | [构建与贡献](docs/BUILDING.md) |
| 小白如何发声和调音 | [三步开始](docs/快速开始.txt) · [完整使用手册](docs/USER_GUIDE.md) |
| 版本验证结果和原始日志 | [验证报告](docs/VALIDATION.md) · [0.16 归档](docs/VALIDATION-0.16.md) |

当前能力包括 3 个振荡器、双滤波器、双层 Warp 与 FM/PM/AM/RM、4 个可绘制包络、10 个 LFO、32 行调制矩阵、8 个宏、4 类效果、琶音器和 26 个工厂音色。LFO 支持自绘曲线、相位、方向、Swing 和 20–1000 Hz 高速模式；0.17 新增 Lorenz／Rössler 连续混沌与实际引擎轨迹显示。Chaos 的 Rate 表示演化速度，Phase／Direction 在该模式下不适用。

**验证范围：**0.17 四套共 175 个自动测试组全部通过；[提交 f20c1aa 的云端干净构建及 175 组测试](docs/GITHUB_CI-0.17.md)也已通过。VST3 通过本机 pluginval strictness 5；旧演示音频逐字节一致，完成新音频独立分析、六张新增界面图检查及独立版原生操作复查。原生发现的异步轨迹切换问题已修复；具体步骤、未测项和原始失败记录见验证报告。测试通过不能替代每个 DAW 的实际验收。

**尚未验证/完成：**FL Studio 和其他 DAW 的完整工作流尚未验证；macOS、AU、AAX 尚未提供。更多绘图工具、宏作为调制目标、多重采样/SFZ、频谱振荡器、效果总线、Clip 编曲、MPE 等仍在路线图。

## 运行与构建

已有本地发布包的用户可直接解压运行 `Anyvoc.exe`。Git 仓库保存源码、文档、截图和测试记录，**不提交 EXE、VST3、WAV 或历史发布 ZIP**；GitHub 的 “Download ZIP” 是源码包。0.17 的打包与验收步骤见构建指南；旧版包保留。

在安装 Visual Studio 2022 C++ Build Tools、Windows SDK、CMake ≥ 3.22 和 Git 后，使用 PowerShell：

```powershell
git clone --recurse-submodules https://github.com/Bad0RANG3/Anyvoc.git
cd Anyvoc
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
```

生成的独立版：`build/Anyvoc_artefacts/Release/Standalone/Anyvoc.exe`。VST3：`build/Anyvoc_artefacts/Release/VST3/Anyvoc.vst3`，它是整个插件目录。

JUCE 固定为 8.0.12 的 commit `29396c22c93392d6738e021b83196283d6e4d850`。GitHub Actions 配置为 Windows 2022 上构建和运行四套自动测试；**具体提交是否通过以 Actions 的实际结果为准**。它不会替代 pluginval、本机音频设备和 DAW 测试。

## 开发约定与许可

保持旧参数 ID、顺序、选项和默认值；新参数追加并提供状态迁移。改动需要相应的音频、状态或 UI 验证，已知问题不能通过删除断言或放宽容差掩盖。详见[构建与贡献](docs/BUILDING.md)和[测试与验收](docs/TESTING.md)。

原创代码采用 [AGPL-3.0](LICENSE)，依赖及分发要求见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。本项目独立于 Xfer Records。
