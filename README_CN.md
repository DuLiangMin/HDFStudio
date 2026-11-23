# HDFStudio — Windows 上的 NetCDF、HDF5 与 GRIB 查看器

**Unleash the Power of Your NetCDF and GRIB Data**

[English](README.md) | **简体中文** · **版本 0.14.02**

## ⬇️ Download

**[Download](https://github.com/DuLiangMin/HDFStudio/releases/download/v0.14.02/HDFStudio-0.14.02-win64.zip)** — **0.14.02 Windows x64 绿色二进制 ZIP** · [全部版本](https://github.com/DuLiangMin/HDFStudio/releases)

包含程序、CLI、GRIB 解码器与定义资源、运行库和离线帮助。解压即可运行，基本浏览无需另行下载依赖。

HDFStudio 是面向气象、气候与科研数据的免费开源 Windows 工具。在同一工作区浏览 NetCDF-3/4、HDF5 和原生 GRIB1/GRIB2，检查元信息、查看数值与图像、比较不同数据层。源文件始终只读。

**🤖 对现代 LLM 和 AI Agent 友好：** HDFStudio 能有效提取 NetCDF、GRIB 等受支持数据的信息，通过可复制的 Properties 报告、CLI 输出和数值 CSV，为模型提供参考，帮助其快速掌握维度、变量、单位、打包方式与缺测值等数据特点。操作流程与示例见[帮助网页](doc/help.html#zh-ai)。

![HDFStudio 逐层播放与同步纹理熵图](docs/demo-analysis.gif)

*NetCDF 气温逐层播放，以及跟随其变化的 4×4 纹理熵图。*

## 🚀 快速开始

1. 点击上方 **Download**，解压后运行 `HDFStudio.exe`，无需安装或管理员权限。
2. 使用 **File → Open** 或拖入文件，展开左侧树，右键变量打开 **Properties**、**Data Grid** 或 **Image View**。
3. 点击 **Help → Help**（位于 About 前）、按 **F1**，或直接打开 [doc/help.html](doc/help.html)，阅读离线中英文帮助。

请保留程序、配套 `hdf_cli.exe`、依赖目录与 `doc`。基本浏览及原生分析无需 Python；可选 NetCDF 报告、CF 检查和日期标签需相应的 `netCDF4`、`numpy`、`compliance-checker` 模块，可通过 `HDFSTUDIO_PYTHON` 指定解释器。

## 🌦️ 原生 GRIB 功能

- **直接浏览 GRIB1 / GRIB2：** 虚拟 **Variables** 组织兼容场；分页 **Messages** 保留物理消息、子场、元信息与来源。
- **查看数值与图像：** 解码值、重建的 **Quantized X** 和并排表格；矩形场图像、科学色表、解码值 Mask，以及 CSV/PNG 导出。Quantized X 仅适用于支持的打包方式，不是原始字节转储。
- **分析与播放：** 直方图、曲线剖面、全平面灰度熵、二维纹理熵、比较指标、残差图、整数索引同步与逐层播放。
- **CLI 剖面：** 矩形 GRIB 数组支持任意有序两轴剖面，缺槽显式记录。GRIB GUI 剖面、可靠地理坐标接入和 U/V 矢量仍待后续完成。

未知名称/单位保持未知；缺场或越界会清空旧结果。当前 GRIB 图像采用源网格索引。比较要求平面形状一致，按整数索引对齐，不自动转换单位、匹配物理时间、投影或重采样。CF 检查不适用于 GRIB。

## 🧰 常用工具

| 任务 | 功能 |
|---|---|
| 检查数值 | 可复制 Properties；NetCDF/HDF5 解码/存储/并排表格；自动列宽、单元格选择与 Ctrl+C |
| 查看图像 | 科学色表、缩放/平移、翻转、解码值 Mask、原分辨率 PNG |
| 分析分布 | 抽样或全平面直方图、曲线 CSV、纹理熵图 |
| 比较数据 | Pearson r、RMSE、MAE、Bias、`Target − Anchor` 残差、各目标独立整数 lag |
| 浏览序列 | 层号控件、Play/Pause；联动视图等待当前数据就绪 |
| NetCDF/HDF5 剖面 | 独立 **Space-Time Section (Hovmöller)**，任意两轴、固定维度切换及 PNG/CSV |
| 整理工作区 | **Window → Tile / Cascade / Hide Infobar** |

NetCDF/HDF5 剖面保留源索引等宽显示和已验证坐标标签；通用 HDF5 使用索引含义。CF 诊断仅供参考，不支持的编码与内存限制会明确提示。细节见[离线帮助](doc/help.html)。

## ⌨️ 命令行提取信息

```text
hdf_cli probe data.grib2
hdf_cli info data.grib2
hdf_cli props data.grib2 /field/0
hdf_cli csv data.grib2 /field/0 plane.csv
hdf_cli props data.nc /temperature
```

变量/对象 ID 以实际文件报告为准。`hdf_cli` 与界面共用读取核心，也提供直方图、熵、比较及任意轴剖面命令。已验证行为与当前边界见 [GRIB 说明](docs/GRIB.md)。

## 🛠️ 构建与反馈

Windows 开发使用 Visual Studio 2022 x64、CMake、Qt 6.8、HDF5、netCDF-C 和 ecCodes。依赖位置与目标见 `CMakeLists.txt`、`cmake/Grib.cmake`；ecCodes 默认位于 `ext/eccodes`，可用 `HDFSTUDIO_ECCODES_ROOT` 配置。源码位于 `src/app`、`src/core`、`src/views`、`src/cli`，测试位于 `tests/`。构建会把帮助页复制到 `build/output/<配置>/doc/help.html`。

公开源码不附第三方代码副本；构建前可用 `python tools/26NOV23_v01_restore_hdf_filters.py` 恢复固定版本的 HDF 解码依赖。详见[依赖说明](docs/DEPENDENCIES.md)和[项目结构树](docs/PROJECT_STRUCTURE.md)。二进制 ZIP 保留完整运行库与许可证。

问题请提交至 [Issues](https://github.com/DuLiangMin/HDFStudio/issues)，附格式、对象 ID、shape 和相关 `probe`/`info`/`props` 输出。历史更新见 [Changelog.md](Changelog.md)。

## 📜 许可与致谢

采用 GNU GPL v3，见 [LICENSE](LICENSE)。基于 Pedro Vicente 的 [HDF Explorer QT Edition](https://github.com/pedro-vicente/hdf_explorer_qt)。依赖与色表署名保留于 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 和 `licenses/`。
