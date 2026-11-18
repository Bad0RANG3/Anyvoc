<div align="center">
  <img src="docs/images/Anyvoc-banner.svg" width="100%" alt="Anyvoc · 波表、采样与颗粒合成器">

  <p><strong>从第一个音符，到自己的声音。</strong><br>面向 Windows 的原创混合合成器，提供独立程序与 VST3 乐器插件。</p>

  <p>
    <a href="https://github.com/Bad0RANG3/Anyvoc/actions/workflows/windows.yml"><img src="https://github.com/Bad0RANG3/Anyvoc/actions/workflows/windows.yml/badge.svg?branch=main" alt="Windows build and tests"></a>
    <a href="docs/VALIDATION-0.20.3.md"><img src="https://img.shields.io/badge/version-0.20.3_candidate-e9ba78?style=flat-square&labelColor=1b2130" alt="源码版本 0.20.3 候选版"></a>
    <img src="https://img.shields.io/badge/platform-Windows_x64-75baff?style=flat-square&labelColor=1b2130" alt="Windows x64">
    <a href="LICENSE"><img src="https://img.shields.io/badge/license-AGPL--3.0-b59bff?style=flat-square&labelColor=1b2130" alt="AGPL-3.0 license"></a>
  </p>

  <p><a href="docs/快速开始.txt"><strong>新手开始</strong></a> · <a href="docs/USER_GUIDE.md">使用手册</a> · <a href="docs/STATUS.md">项目进度</a> · <a href="docs/ROADMAP.md">开发路线</a> · <a href="docs/TESTING.md">测试与验收</a></p>

  <p><strong>3</strong> 个振荡器 · <strong>10</strong> 个 LFO · <strong>32</strong> 条调制路由 · <strong>8</strong> 个宏 · <strong>28</strong> 个工厂音色</p>
</div>

![Anyvoc PLAY 页面：音色选择、音色塑形、空间效果和屏幕键盘](docs/images/Play.png)

<p align="center"><sub>PLAY 页面预览 · 图来自 0.20 文档快照，当前版本的实际验收范围见版本记录。</sub></p>

## 🎹 先弹起来

**选一个音色，按下琴键，再慢慢调出自己的声音。**

1. 运行独立版 `Anyvoc.exe`，默认进入 **PLAY**。
2. 点 **CHOOSE A SOUND → 选音色 → LOAD SOUND**。
3. 按住底部钢琴键，用 **TONE / NOTE SHAPE / SPACE** 调整明暗、起音和空间感。
4. 点 **SAVE** 保存为 `.anyvoc`，用 **UNDO / REDO** 撤销或恢复修改。

**当前源码为 0.20.3 候选版，真实声卡验收受阻；尚未宣布整版交付。** 首轮 Windows／完整源码候选包已通过完整性校验，本机候选入口：`dist/Anyvoc-0.20.3-Windows/Anyvoc.exe`。文档收口后会重打包复验；旧版包保持冻结。收到 Windows ZIP 后请先完整解压，再运行里面的程序。

> **关于下载**：GitHub 仓库目前提供源码、文档与验证记录，尚未上传可直接运行的 Release 附件。“Download ZIP” 下载的是源码。自行构建见[构建指南](docs/BUILDING.md)。

## ✨ 把声音设计展开

PLAY 集中常用控制，深入时再进入 **SYNTH / FILTER / MOD / FX**。所有页面编辑同一个音色，切页不会重置声音。

| 声音设计 | 已有能力 |
| :--- | :--- |
| **声源与质感** | 三个波表／采样／颗粒振荡器，Sub、Noise，16 复音与 1–7 路 Unison。 |
| **波表与变形** | 波形、谐波和相位编辑；双层 Warp；跨声源 FM / PM / AM / RM。 |
| **运动与调制** | 四条包络、十个 LFO、Custom／Chaos 曲线、32 行矩阵、八个可受调制的宏。 |
| **滤波与空间** | 双滤波器、串并联及声源路由；Drive、Chorus、Delay、Reverb。 |
| **演奏与保存** | Poly / Mono / Legato、Glide、轮控、踏板、琶音器；音色浏览、收藏、保存与撤销。 |
| **更顺手的接线** | 拖拽分配调制、独立 COPY、按目标批量清空、完整路由图发布。 |

<details>
<summary><strong>查看波表编辑与调制矩阵预览</strong></summary>

| 波表编辑 | 调制路由 |
| :---: | :---: |
| [![Anyvoc Wavetable Lab](docs/images/Wavetable-Lab.png)](docs/images/Wavetable-Lab.png) | [![Anyvoc 调制路由详情](docs/images/Matrix-detail.png)](docs/images/Matrix-detail.png) |
| 绘制波形，编辑谐波与相位。 | 设置主来源、AUX、曲率和调制深度。 |

以上为 0.20 的界面文档快照，点击图片可查看原图。完整操作见[使用手册](docs/USER_GUIDE.md)。

</details>

## 🧪 当前进度

**当前源码版本为 0.20.3 候选版：减少密集调制矩阵中重复的曲率计算，保留原声音公式。** [缓存合同](docs/MODULATION_CACHE.md)和[本版验证](docs/VALIDATION-0.20.3.md)分别说明改动与证据。

| 验证项目 | 当前结果 |
| :--- | :--- |
| Windows Release 构建 | **通过**：最终独立版与 VST3 已重建。 |
| 完整自动回归 | **220 / 220 通过**，218.75 秒；75 / 129 / 10 / 6 组。 |
| 精确声音回归 | **27 / 27 原始 float32 文件逐字节相同**，最终版对修改前真实参考；不等于所有音色或硬件性能证明。 |
| 参数与音色兼容 | 保持 **1,256 个宿主参数 · schema 14 · 28 个工厂音色**。 |
| VST3 与四份固定演示 | 最终 pluginval strictness 5 **PASS**（seed `0x481d1f1`）；四演示最终新渲染与冻结 0.20.2 ZIP 全字节一致，宏独立分析 **PASS**。 |
| 原生与实际声卡 | 窗口、PLAY、0.20.3 页脚和 INIT 已确认；**输出设备无法打开**，琴键未见电平／EV 活动，演奏及正式 6×3 回调复测受阻。用户确认同期有其他程序独占声卡，待释放后重验。 |
| 0.20.3 包与 CI | 首轮候选包校验 **PASS**，文档更新后重打包复验；[`a956c5e3` 的 CI](https://github.com/Bad0RANG3/Anyvoc/actions/runs/44292647323)记录时运行中，结果待核实，不声明整体验收通过。 |
| 指定历史 CI | [`63a8192` 探针工具提交](docs/GITHUB-CI-REALTIME-0.20.2.md)整体 **失败**：220 组 CTest、20 项分析器通过，runner 6/7；本地路径别名回归已修复，不能改写旧云端结果。 |
| 旧设备基线 | [0.20.2 的 6 种负载 × 3 轮](docs/REALTIME-0.20.2.md)共 206 个超预算块；不能据新离线数据称其已解决。 |

**还在路上：**更多调制目标、多采样／SFZ、频谱声源、效果总线、Clip、MPE 和其他平台。FL Studio 等 DAW、外接 MIDI、真人听音及实时高负载仍待分别验收。完整缺口和完成条件见[项目现状](docs/STATUS.md)与[路线图](docs/ROADMAP.md)。

## 🛠 构建与参与

准备 **Visual Studio 2022 C++ Build Tools、Windows SDK、CMake ≥ 3.22 和 Git**，在 PowerShell 中执行：

```powershell
git clone --recurse-submodules https://github.com/Bad0RANG3/Anyvoc.git
cd Anyvoc
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1
```

| 构建产物 | 输出路径 |
| :--- | :--- |
| 独立程序 | `build/Anyvoc_artefacts/Release/Standalone/Anyvoc.exe` |
| VST3 插件 | `build/Anyvoc_artefacts/Release/VST3/Anyvoc.vst3`，安装时保留整个文件夹。 |

版本以检出提交的 `CMakeLists.txt` 为准，JUCE 固定为 **8.0.12**。打包与分析另需 Python 3。开发时请保留旧参数 ID、顺序和状态兼容，为行为变化留下可复现的测试与验证记录。

| 文档入口 | 内容 |
| :--- | :--- |
| [构建指南](docs/BUILDING.md) | 环境、编译、演示渲染与打包。 |
| [测试与验收](docs/TESTING.md) | 操作步骤、通过标准、自动测试与人工验收边界。 |
| [0.20.3 候选验证](docs/VALIDATION-0.20.3.md) | 已通过分项、声卡受阻情况与待验范围；[0.20.2 交付记录](docs/VALIDATION-0.20.2.md)独立保留。 |
| [历史记录](docs/VALIDATION.md) | 各版本本机验证、指定提交 CI 与原生操作证据。 |

---

<p align="center"><strong>Anyvoc</strong> · 原创声音设计工具 · <a href="LICENSE">AGPL-3.0</a><br><sub>依赖与分发要求见 <a href="THIRD_PARTY_NOTICES.md">第三方声明</a>。本项目独立于 Xfer Records；以 Serum 2 的功能与操作体验为长期参考，目前未达到完整等价，不支持 Serum 预设，也不包含 Xfer 的代码、素材或品牌界面。</sub></p>
