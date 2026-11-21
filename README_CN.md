# HDFStudio — Windows 上的 NetCDF / HDF5 查看器

**Unleash Your NetCDF Power.**

[English](README.md) | **简体中文**

🔢 **版本：0.12.08**，增加独立的二维纹理熵图，并包含科学色表、图像缩放、超前/滞后比较、双图指标、残差图与逐层联合播放。

⬇️ **下载：** 在[最新发布页](https://github.com/DuLiangMin/HDFStudio/releases/latest)下载 `HDFStudio-<版本>-win64.zip`。**绿色安装**：解压到任意目录，直接双击 `HDFStudio.exe` 即可运行，无需安装，也不需要管理员权限。

[![Release](https://img.shields.io/github/v/release/DuLiangMin/HDFStudio?label=release&color=3d99a4)](https://github.com/DuLiangMin/HDFStudio/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/DuLiangMin/HDFStudio/total?color=3d99a4)](https://github.com/DuLiangMin/HDFStudio/releases)
[![License: GPL v3](https://img.shields.io/badge/license-GPL--3.0-blue)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%2010%2F11%20x64-lightgrey)](#-下载与运行)
[![Qt](https://img.shields.io/badge/Qt-6.8-41cd52)](https://www.qt.io/)

HDFStudio 是面向气象、气候与地球科学的 Windows 桌面 **NetCDF 查看器**与 **HDF5 浏览器**，免费开源。打开一个 `.nc`、`.nc4`、`.h5` 或 `.hdf5` 文件，即可浏览组、变量与属性，在表格中查看数值，把二维切片画成图像，按文件自带的日历把时间坐标解码成真实日期，并检查 CF 元信息，全程不需要写代码。

![HDFStudio 用 cmocean Thermal 播放2米气温，旁边的二维局部纹理熵图同步更新](docs/demo-analysis.gif)

*从 Windows 程序实际录制：2米气温（`TMP_2maboveground`，384 × 190，共480层）采用 cmocean Thermal 配色，旁边是 Viridis 配色的4×4局部纹理熵图。绿色 Play 按钮逐层播放，两图同步更新；熵图使用独立的0～4 bits固定色标。[查看静态截图](docs/screenshot-main.png)。*

### 🆕 0.12.08 新增功能速览

| 能力 | 使用方法 |
|---|---|
| 🖼️ 独立的**二维纹理熵图** | **View → Texture Entropy Image** 或变量右键打开，查看4×4局部复杂度，支持切层、Mask、原尺寸PNG和数值CSV导出。 |
| 🔗 **超前/滞后与多图联动** | 在 **Compare & Metrics** 中选择Anchor/Targets，各自设置整数lag；联动层、缩放/平移/翻转，并可选择同步色表与范围。 |
| 📐 **实时比较指标** | 同时查看Pearson r、RMSE、MAE、Bias；复制报告或导出含文件、变量、层索引和单位的CSV。 |
| ➖ **二维残差图** | **Residual map** 用零中心发散色标显示`Target − Anchor`，跟随当前层和Mask更新。 |
| ▶️ **绿色播放/暂停** | 可调帧间隔，单图放映或多图联合播放；指标、残差和跟随源图的纹理熵同步更新。 |

**验证情况：** clean Debug/Release构建各通过 **83项QtTest**，零失败、零跳过，并通过CTest；局部熵数值及导出PNG颜色均与独立NumPy逐值对账，覆盖缺测与Mask。

## 🤔 为什么用 HDFStudio

拿到模型输出、卫星产品或同事发来的 `.nc` 文件，在敢用之前，你通常得先弄清**里面到底是什么**。常见疑问：

- `time` 的单位是秒还是天？从哪一刻起算？用的什么日历？
- 纬度是南→北还是北→南？经度是 0–360 还是 −180–180？
- 这个文件符合 CF 规范吗，画出来的图会不会上下颠倒？

HDFStudio 几秒内就能回答，不需要开 Python、不需要 notebook、也不需要装整套科学环境。它只是一个小巧的免安装程序（约 15 MB），打开文件就把事实摆出来。

### 🔬 从查看切片到对比模式结果

HDFStudio 在同一工作区中提供以下分析能力：

- **二维纹理熵图**：**View → Texture Entropy Image** 或变量右键同名选项，单变量即可输出真正的二维局部熵图。每个不重叠4×4块计算Shannon熵，双线性插值熵值至原图尺寸后着色，固定0～4 bits色标；可缩放、平移、切层及导出原尺寸PNG/数值CSV。从当前Image View打开时跟随该图的层和Mask，不需要比较对象。
- **同轴滑动与超前/滞后间隔比较**：选定一个 Anchor 和一个或多个 Target 图像，选择 `time` 等前置轴，拖动 Anchor 层滑块即可联动浏览。每个 Target 独立设置正、零、负整数偏移，满足 `target_index = anchor_index + lag`；可同步缩放、平移、翻转，色表与数值范围同步可选。越界目标清空，回到合法范围后自动恢复。
- **相关系数与误差一起比较**：**Compare & Metrics** 非模态面板实时显示 Pearson r、RMSE、MAE、Bias（`Target − Anchor`）及有效/排除配对数。支持复制报告和原子 CSV 导出，记录源路径、变量、层索引、revision 与单位；CLI 与界面共用核心算法，便于独立数值对账。单图熵不再作为比较结果。
- **残差图比较**：点击 **Residual map**，用零中心对称发散色标定位 `Target − Anchor` 的正负差异。残差跟随当前层与 Mask 更新，对比无效时清空；支持缩放、平移和完整分辨率 PNG 导出。

**使用入口**：打开两个图像视图（可来自同一文件或不同文件）→ 点击主工具栏可见的 **Compare & Metrics**（或 **Window → Compare & Metrics**）→ 选择 Anchor、Target 和索引轴 → 设置 lag 并拖动 Anchor 滑块。本面板显示的层索引为 **零基索引**。

对于不同起报时刻的两份预报，先查看解码后的时间坐标，再选取符合比较目的的索引偏移。切换面板中的 Target 会保留其他 Target 各自的偏移设置，便于多个预报序列同时跟随一个 Anchor。

**科学边界**：按整数层索引对齐，即使轴名为 `time`，也不自动换算小时/天或匹配时间坐标。双图平面 shape 必须相同，只计算当前解码值与 Mask 后两图同时有限的像元交集；不隐式插值、裁剪、地理配准或换算单位。单位保留原标签，不同时标为未核实；常量场 Pearson r 不可定义，但误差指标仍可有效。

### 🖼️ 纹理熵图怎样读

有效平面的解码值按统一min/max量化为256级灰度，再统计每个4×4块的灰度概率分布，`H=-Σp·log2(p)`。常量块为0，16个不同灰度且各出现一次时为4 bits；边缘不足4×4按实际像元计算，不补零。全缺测块无效，源缺测/Mask像元保持灰色；插值跳过无效邻块并归一化权重，不填补缺测区域。输出横纵索引与源平面一致，PNG恰好与源图同尺寸；4倍插值用于显示，不增加真实空间细节。

较高值表示局部灰度分布更丰富，常量/变化较小的区域通常较低；噪声也可高熵，平滑渐变也可非零，相同局部直方图的不同空间排列熵相同。因此它是局部复杂度表征，不能单独认定有效信号来源。量化范围随当前层有效值重算，与显示色表、反色和缩放无关。旧标量现明确标为 **Whole-plane histogram entropy**，仅作全图直方图诊断。

### ▶️ 第5项：单数据放映与双图联合播放

主工具栏新增绿色位图 **Play / Pause（播放/暂停）** 按钮和 **Frame interval（帧间隔）** 设置，默认500毫秒，可设100～10000毫秒；**Layer** 菜单也有同一动作。打开图像、选定一个前置轴后点击Play，逐层放映；到末层自动停止，在末层再次点击Play从首层重新播放。**当前播放轴少于2帧时按钮置灰、不可播放；比较模式按Anchor的帧数判断**。帧间隔是显示延迟，不代表数据的实际时间间隔。

**双图联合放映**：打开两幅图 → **Window → Compare & Metrics** → 选定Anchor/Target并勾选 **Sync layer indices** → 设置各Target的lag → 点击主工具栏 **Play**。无论当前选中Anchor还是已同步的Target，都会推进Anchor，各图按各自lag联动，指标和已打开的残差图随之更新。**Next/Previous/First/Last、Go to Layer与主工具栏层选择框也遵循相同联动规则**；未绑定或关闭层同步的图像仍独立导航。暂停保持当前层，切换播放对象/轴或改变比较绑定时停止。本功能在窗口中放映切片序列，不生成视频文件。

| | HDFStudio | Panoply | ncview | HDFView |
|---|---|---|---|---|
| Windows 原生、免安装 zip | ✅ | 需 Java | ❌ | 需 Java |
| 同时支持 NetCDF **和** HDF5 | ✅ | ✅ | 仅 NetCDF | ✅ |
| 用文件自带日历解码时间 | ✅（`noleap`、`standard` 等） | ✅ | ❌ | ❌ |
| 经纬度方向报告 | ✅ | ❌ | ❌ | ❌ |
| 内置 CF-1.8 规范检查 | ✅ | ❌ | ❌ | ❌ |
| 压缩值与原值对照 | ✅ 解码 / 存储 / 并排三种表格 | ❌ | ❌ | ❌ |
| 单层直方图 | ✅ 随机抽样、箱数可调 | ❌ | ❌ | ❌ |
| 自定义曲线剖面 | ✅ 样条 / 折线、双线性插值、导出 CSV | ❌ | ❌ | ❌ |
| 地图投影与海岸线 | ❌ 暂未支持 | ✅ | ❌ | ❌ |

*对比表反映作者的理解，具体能力请以各工具官方文档为准。“压缩值与原值对照”指同一张表里把 `存储值 × scale_factor + add_offset` 和文件里的整数并排列出；四种工具都能显示解码值，但只有 HDFStudio 把两个数摆在一起。*

## ✨ 功能

- 📂 **打开 NetCDF 与 HDF5 文件**：NetCDF-3（CDF-1、CDF-2、CDF-5，0.10.27 起经 netCDF-C 读取）、NetCDF-4 / NetCDF-4 classic、普通 HDF5 容器；可同时打开多个文件。可以从资源管理器把文件拖到窗口任意位置（变量树、视图、信息面板、工具栏）打开；以管理员身份运行时 Windows 会阻止从资源管理器拖放，HDFStudio 会在状态栏提示。格式按文件头识别，不看 `.nc` 扩展名。
- 🗂️ **干净地关闭文件**：**File → Close File**（`Ctrl+F4` 或 `Ctrl+W`）、**Close All Files**，或在任意节点右键 **Close File**；该文件的视图会随之关闭。
- 🌳 **变量树**：组、维度、变量与属性，带类型和维度大小。
- 🔢 **数值表格**：精确值表格，三维、四维、五维数组可选层。NetCDF-3 文件只读取当前显示的二维平面，700 MB 的变量不到 0.1 秒即可打开。
- 🔀 **两种数值，三种表格**（0.10.30）：一个变量可以“存的是一个数、代表另一个数”（`解码值 = 存储值 × scale_factor + add_offset`），所以右键菜单提供三种表格：
  - **Data Grid (decoded to real)...**：真实值，即图像视图和图例上的数。
  - **Stored Grid (raw value)...**：文件里原样存储的数，未经 `scale_factor`/`add_offset`。
  - **Both Grids (decoded and raw)...**：一张表里解码值列后面紧跟它的存储值列，同一行就能对照“这个数是从哪个数来的”。

  表头都写明是哪种数（`Value (decoded to real)`、`Value (raw)`，并排时是 `Value 1 (decoded)` / `Value 1 (raw)`），窗口标题也重复一遍。解码是从存储的数值算起，不从被截短的文本算起，所以 `float32` 的 123456789 不会先被压成 6 位再乘系数。已在 1201 × 1401 的打包产品上与 Python netCDF4 逐格对账：解码、存储、并排三种表格各 1,682,601 格全部一致。
- ⬆️⬇️ **主工具栏层导航**：打开按钮后面是上一层、下一层、最后一层、第一层与可选的维度/层号下拉框，工具栏实时显示 `Layer n of N`；另有 **Layer** 菜单（`Alt+←`/`Alt+→`/`Alt+Home`/`Alt+End`、`Ctrl+G` 跳转到指定层）。多轴变量（如 `time` 与 `lev`）可先选要翻的维度，再逐层查看。
- 🔍 **属性详情**：类似 `print(netCDF4.Dataset(...))`，给出维度名、unlimited 维度，数组属性会展开显示。
- 📋 **可拷贝的 Properties，为提取元数据而优化**（0.10.29）：在文件、组或变量上右键 → **Properties...**，弹出的是纯文本报告，可以用鼠标选择后按 `Ctrl+C` 拷贝，也可以点 **Copy All** 一次拷走全部内容。报告开头是文件路径和对象路径，后面依次是形状、类型、大小、维度、存储布局和全部属性。直接粘贴到搜索框、Issue 或 LLM 对话里，就能询问该文件的 NetCDF 元数据和数据信息，不必打开 Python。弹窗不是模态的，可以同时打开几个对照查看；`hdf_cli props` 输出相同的内容。
- 🪟 **Window 菜单**：**Cascade** 把视图按顺序层叠排列，**Tile** 平铺；菜单下方是编号的已打开视图列表（当前视图打勾），点击即可切到该视图。
- 🗺️ **图像视图**：二维切片支持连续与离散调色板（Viridis、Inferno、Turbo 等），CF 经纬网悬停可读出经纬度。
- 🔍 **放大 / 缩小**：第二行工具栏最左侧的放大镜按钮，分别以视口中心放大 1.25 倍、缩小至 0.8 倍；滚轮围绕鼠标位置缩放。旁边 **Fit** 完整显示图像并保留翻转，**Reset** 恢复原方向与适应窗口。缩放保留曲线控制点，复用缓存，不重读文件。
- 🎨 **CMaps 色卡**：新增 13 张科学配色（标准 Viridis；cmocean 的 Rain、Thermal、Balance、Speed、Curl、Phase；Crameri Batlow；5 张 NCL 气象色表），并保留原版 5 张兼容配色。在 `CMaps` 下拉框选预览色带，`…` 打开色卡/映射设置或导入本地 NCL `.rgb`。颜色条与图像共用映射，支持反色、连续/分级、自定义边界、中心值、循环起点/周期、低于/高于范围的颜色。配色名称不自动定义单位、降水阈值或累计时段。`Projs` 保留原经纬度回查功能，不表示已增加投影转换。换色和调整显示范围复用解码缓存；内置色卡运行无需 Python/NCL，许可与来源附在 `licenses/colormaps/`。
- 🎭 **Mask Range（屏蔽区间）**（0.10.30）：图像工具栏下方单独一行，填开始值和结束值。落在 [开始, 结束]（含两端）内的值变成 NaN，显示为**白色**，不参与色标和最小/最大值。开始值留空表示负无穷，结束值留空表示正无穷。例如某降水产品用 `99990000` 存特殊值、`scale_factor = 0.0001`：开始填 `9999`、结束留空，即可把它白化，只给真实降水上色。Mask Range 按**解码后的数值**判断（`存储值 × scale_factor + add_offset`，即图例上的数），不按文件里存的整数。`valid_range`、`_FillValue` 等属性是按存储值写的，为免混淆，Properties 和 Mask Range 行会同时给出解码值，例如 `valid_range decoded: [0, 9999] (stored [0, 99990000])`。
- ⏱️ **时间解码**：`seconds since 1970-01-01`、`days since 2024-10-01` 这类 `time` 变量会显示为 `yyyymmdd` 与 `yyyymmdd hh:mm:ss`，经 `netCDF4.num2date` 按文件自身日历解码。
- 🧭 **坐标方向**：By NetCDF4 页报告纬度是南→北还是北→南、经度是东→西还是西→东，这正是地图上下颠倒的常见原因。
- ✅ **CF 规范检查**：调用 IOOS Compliance Checker 生成 CF-1.8 报告，另附内置的只读 CF 元信息诊断。
- 🌬️ **矢量视图**：U/V 风或洋流分量；并提供 **UGRID** 三角网格预览。
- 📊 **单层直方图**（0.12.02）：在至少二维的变量上右键 → **Histogram...**。窗口只读取所选那一层，给出分布：柱状图加一行摘要（抽样值个数、分箱数、最小/最大、均值、标准差、NaN 个数）。**Sample 1 in N** 缺省为 20（即取 1/20），在整层里**均匀随机**抽 `round(有限值 / N)` 个值——不是按固定步长取每第 N 个点——所以 1201 × 1401 的一层约 70 ms 出图；填 `1` 则用全部数值。改抽样步长或分箱数只是对已在内存里的数值重新抽样、重新分箱，不再读文件；**Draw a new sample** 在同样大小下换一组随机样本。种子保证可复现：同样的设置总是同样的样本。三维、四维、五维变量可直接选层，并跟随主工具栏的层导航。`hdf_cli hist` 输出同样的文本。
- 💾 **导出**：数值导出为文本，切片导出为 PNG；命令行可批量出图。
  PNG 导出使用该数据集当前图像的图层、色表、Mask 和翻转，保持完整数据分辨率。批量 `--preview ... --png ...` 遇到已有 PNG 或 JSON 伴随文件时拒绝覆盖。

## 📥 下载与运行

1. 在 [Releases](https://github.com/DuLiangMin/HDFStudio/releases/latest) 页面下载 `HDFStudio-<版本>-win64.zip`。
2. 解压到任意目录，运行 `HDFStudio.exe`。无需安装、无需管理员权限，Qt 与 Visual C++ 运行库已随包提供。

变量树、表格、图像视图与 File Info 页开箱即用。**By NetCDF4** 页、**CF Check** 页和时间解码需要 `PATH` 中的 Python 3（或设置 `HDFSTUDIO_PYTHON`）：

```
pip install netCDF4 numpy compliance-checker
```

命令行可直接打开文件，也可无界面批量出图：

```
HDFStudio.exe a.nc b.nc
HDFStudio.exe --preview PRECT --layer 0 --png out.png data.nc
```

压缩包里还有 `hdf_cli.exe`：它调用与图形界面相同的读取代码，文件打不开或数值可疑时可以先用它排查：

```
hdf_cli probe  data.nc              按文件头判断格式
hdf_cli info   data.nc              维度、变量与属性，排版接近 netCDF4-python
hdf_cli props  data.nc /ttr         Properties 对话框里的那几行
hdf_cli export data.nc /lat         数值导出为文本（加文件名则写入文件）
hdf_cli csv    data.nc /ttr plane.csv 7 42    图像视图所读的那一个二维平面
hdf_cli entropy data.nc /precipitation 256 0   完整解码平面的熵（bits）及归一化熵
hdf_cli texture-entropy data.nc /precipitation texture.csv 0   二维4×4局部纹理熵：原尺寸数值和块熵
hdf_cli corr a.nc /field b.nc /field --anchor-idx 0 --target-idx 1
                                              shape严格一致，有限交集，误差定义target-anchor
hdf_cli mask   data.nc /precipitation 9999 - 0    按解码值做 Mask Range（"-" 表示开区间端点），
                                                 输出屏蔽个数和剩余数值的最小/最大值
hdf_cli hist   data.nc /precipitation 0.05 32 0   第 0 层直方图：随机取 1/20、32 个箱
                                                 （依次是抽样比例、箱数、各前导轴的层号），
                                                 输出与 Histogram 窗口逐行一致
```

## 🛠️ 从源码构建（Windows）

需要 Visual Studio 2022 x64、CMake、Qt 6.8（`CMAKE_PREFIX_PATH`）、编译到 `build/hdf5-1.8.20` 的 HDF5 1.8.20，以及 `ext/netcdf-c-4.9.3/include` 中的 netCDF-C 4.9.3 头文件和 `build/native-nc/Release` 下的 `netcdf.lib`/`netcdf.dll`。详见 `CMakeLists.txt`；`tools/package-windows.ps1` 用于生成发布 zip。`build.cmake.sh` 是继承而来的 Linux 脚本，本版本未验证。

源码按“做什么”分成四个目录，不再平铺在一个文件夹里：

```
src/app/     hdf_explorer.cpp/.hpp（主窗口）、layer_navigation、iterate、visit
src/core/    nc_reader、nc_classic、properties_inspector、text_export、plane_reader
             （平面解码与直方图，图形界面和 hdf_cli 共用）
src/views/   raster_view、vector_view、mesh_view、histogram_view
src/cli/     hdf_cli.cpp，无界面的排查工具
resources/   Qt 资源、图标、sample.ico/.icns、netcdf_report.py、images/
docs/        FORMAT_CAPABILITY.md、STAGE4_ACCEPTANCE.md
```

头文件按文件名包含（`#include "nc_reader.hpp"`），`CMakeLists.txt` 把 `src/app`、`src/core`、`src/views` 加入头文件搜索路径，所以在这几个目录之间移动文件不必改动调用处。

## 📌 范围

HDFStudio 是查看器与检查器，从不修改输入文件。CF 诊断仅供参考，不构成合规认证。地图投影与任意非结构网格暂不支持。NetCDF-3 的表格按二维平面读取；NetCDF-4 / HDF5 的表格目前仍整体读入变量，分块读取列在路线图中。已验证的格式边界见 `FORMAT_CAPABILITY.md`。

## 🗺️ 路线图

- NetCDF-4 / HDF5 大变量的切片与分块读取
- 地图投影与海岸线叠加
- 更完整的时间轴与日历检查（bounds、不等间隔）
- Linux 与 macOS 版本

欢迎在 [Issues](https://github.com/DuLiangMin/HDFStudio/issues) 提问题和建议。若某个文件打开有误，请附上 `hdf_cli probe file.nc` 与 `hdf_cli info file.nc` 的输出（或用 `ncdump -h file.nc`），这些只有文件头，不含数据。

## 🙏 致谢与许可

作者：DuLiangMin。HDFStudio 派生自 Pedro Vicente 的 [HDF Explorer QT Edition](https://github.com/pedro-vicente/hdf_explorer_qt)（2016），以 GNU GPL v3 发布（见 `LICENSE`）。随包分发的第三方组件（Qt 6.8 LGPL-3.0、netCDF-C 4.9.3、HDF5 1.8.20、zlib）及其许可证列在 `THIRD_PARTY_NOTICES.md`。

如果 HDFStudio 帮你省了时间，给仓库点个 ⭐ 能让更多同行找到它。

## 🔎 关键词

NetCDF 查看器, NetCDF 浏览器, NetCDF4 viewer Windows, nc 文件查看, 打开 nc 文件, HDF5 查看器, HDF5 浏览器, HDF 查看器, 科学数据查看, 气象数据, 气候数据, 大气科学, 海洋学, 地球科学, 模式输出, CMIP, 再分析, GRIB2 转 NetCDF, CF 规范, CF-1.8 检查, netCDF4-python, num2date, 时间解码, noleap 日历, 经纬度网格, Qt6 桌面程序, 免费开源, Panoply 替代, ncview 替代, HDFView 替代。
