# 🌐 Icosahedral 5 slices — 0.16.02

## 🖱️ 操作

在NetCDF或矩形GRIB的变量上右键，选择 **Visual Analysis / 可视分析 → Icosahedral 5 slices**。
默认打开五条展开带，可切换五张矩形卡、OpenGL 3D、面积、源网格还原和误差。
已打开同变量Image时复用当前层与Mask，否则自动读取该变量；支持全球或局地真实经纬轴。
调整L0–L6会自动重算，连续调整合并到最后精度；计算时保留上一图并禁用旧值探针/导出，完成后替换。无需再点Run，期间可切换2D/3D。**Run / 运行**可手动重算；Image中的Mask或源修改后旧结果失效，Run建立新快照。
层工具栏可切层、Play/Pause和设置间隔；播放等待源读取、分析与当前显示帧完成。
换帧保留上一张完整图，旧值探针/导出失效，新帧完成后替换；末帧停止。
单时次文件禁用Play。顶部View不再提供本功能入口。

五切面复用原项目`ico_atlas`和`ico_render`，每片4个基础面，共20个面。
**5 Unfolded Strips**保持展开三角带；**5 Rectangular Charts**将节点按半开边界
组成`[5,n,2n]`矩阵，`n=2^L`，节点不重复；节点5/6由独立Apex附件保留。
当前网格方向不旋转，Apex不是地理南北极。滚轮缩放、左键拖动平移、双击/Fit适配。
**Seams**显示配对接缝；区域外灰显NaN。2D与3D使用同一份节点结果，不重复重映射。

OpenGL 3.3 Core画布直接复制并适配已编译通过的`HDFStudio_to_Isosahedral`
项目`Ico3DCanvas`和`ico_3d_mesh`，来源原文件与hash保留在
`third_party/icosahedral/qt-reference/`。鼠标左键拖动自由旋转、右/中键平移、
滚轮缩放，双击或Fit恢复初始视角；Turntable为固定地轴转台。
Icosphere/Polyhedron和滑块支持球面与平直二十面体连续变形，保持相同节点/面编号。
20 Faces/Edges/Seams/Graticule分别控制基础主棱、细分网格、五卡接缝和参考线；预设支持南北极、
0°/105°/180°E。默认Unlit保留色表亮度，Shading仅改变显示亮度。
显示模式包含GL节点场、单位球面积、源行列还原图和还原−源误差。
节点模式在三个有效节点之间插值颜色；任一节点缺测则整面灰显，绝不填物理零。
悬停/单击拾取正面三角面与最近节点，报告经纬度/面积/原单位值，不称为片元插值物理值。
**S2S Ocean / S2S Anomaly / S2S Rain** 同时加入普通图像色表选择器；
原有默认Jet不改变。三张新表是项目原始sRGB控制点，不内置物理阈值，
未声称经过感知均匀性验证。残差使用自身范围与零中心发散色，保留原单位。

## 🔌 输入输出

输入为当前Image已解码、已Mask的不可变`float64[latitude,longitude]`，
保留原单位、源变量和前导层索引。坐标通过现有隔离CLI或GRIB坐标读取验证；
不根据shape或变量名造坐标。经度至少4个唯一点，可为局地或经验证全球周期，可降序或
含数值/Mask一致的重复端点。纬度可非等距/降序（包括矩形Gaussian纬轴），
至少2行；不超出首末纬度中心外推，四个采样角点任一缺测则输出NaN。
精确等距经度调用原C双线性核；全球GRIB编码舍入或可信坐标bounds确认的
非等距经度使用同项目Python公式的原生移植，保留实际中心，不吸附到理想网格。
导出`method`属性区分该路径。局地场直接嵌入全球网格，仅源经纬中心范围内采样，
范围外为NaN；不人为连通两侧边界，不分配高分辨率全球填充矩阵。

| 数组 | shape | 类型/单位 |
| --- | --- | --- |
| `node_xyz` | `(V,3)` | float64，单位球 |
| `node_lon`, `node_lat` | `(V,)` | float64，度 |
| `face_node`, `edge_node` | `(F,3)`, `(E,2)` | int32，0基索引 |
| `face_area` | `(F,)` | float64，steradian |
| `node_value`, `edge_value`, `face_vertex_mean` | `(V,)`, `(E,)`, `(F,)` | 原单位点值/端点均值/顶点均值 |
| `source_value`, `restored`, `residual` | `(rows,cols)` | float64，原行列、原单位、NaN缺测 |
| `forward_row_offset` | `(V+1,)` | int32 CSR偏移 |
| `forward_source_index`, `forward_weight` | `(links,)` | 原源行优先0基索引、float64权重 |
| `face_chart`, `face_chart_quad`, `face_chart_uv` | `(F,)`, `(F,)`, `(F,3,2)` | int32卡号/基础面号，float64 UV |
| `chart_node`, `chart_value` | `(5,n,2n)` | int32原节点编号、float64原单位节点值 |
| `chart_apex_node`, `chart_apex_value` | `(2,)` | 独立端点节点5/6及其值，非地理极点 |

层级L满足 `V=10*4^L+2`, `F=20*4^L`, `E=30*4^L`。
例如L3为642节点、1280面、1920边；L6为40962节点、81920面、122880边。
`edge_node`给出无向图边，训练程序可显式拼接反向边；文件不擅自添加自环。
CSR每个有效行四项，空行缺测；`valid_link_count`区分全缺测时的单项存储占位。
Bias/MAE/RMSE只统计原源与还原共同有限点，源缺测掩膜保留。

## 🧮 原生复用和科学边界

现有项目06 C17网格、球面几何、稀疏权重、守恒、分辨率与SCRIP NetCDF模块
原样复制到`third_party/icosahedral/`，来源hash见其`source-manifest.json`，
原始SCRIP Notice见`licenses/icosahedral/`。CLI/GUI共用适配器
`src/core/icosahedral.*`，运行不依赖Python。已有项目Python逆向重心公式移植到
原生适配层；层级四子面定位失败会报错，不静默回退最近面。

本界面选择**点值双线性 → 节点 → 径向重心还原**；边和面值为节点算术均值。
这不将格点值解释为单元平均值，不声称面积守恒。原C守恒API保留用于明确边界、
归一化和单元平均语义的原生调用，不把它应用于语义未知的当前场。
球面是单位球，面积不是平方米；未进行源datum转换。本功能准备网格和分析数据，
不替用户决定次季节模型网络结构、预报信号或物理方法。

## 💾 导出和CLI

**Export mesh + fields NetCDF**创建新NetCDF文件，包含上述拓扑/值/权重、
源轴、算法和单位等元信息。拒绝已有目标和源覆盖；失败清理临时文件。
PNG从当前GL帧缓冲导出并附真实色标；五切面导出1200×1570完整图及色标，不受屏幕缩放/平移裁切；还原/误差模式导出源网格图。
换源层、Mask、级别、关闭源或文件stamp改变后旧结果不可导出。
3D需要OpenGL 3.3 Core上下文；初始化失败明确诊断，不把CPU图冒充GL。
`offscreen`/`minimal`平台保留数值分析和2D导出，GL功能须在原生窗口平台验证。

```text
hdf_cli ico-demo new_mesh.nc 3
hdf_cli ico input.nc /temperature new_analysis.nc 3 0
```

CLI最后为每个前导轴的0基索引，省略时全部取0；输出须不存在。
源文件只读，CLI也支持已解析的GRIB对象路径。
当前交互限制L0–L6、源平面最多4194304格点，使用共享预算和有限后台任务。
取消、再次Run和关窗使旧代次失效；数值worker不调用HDF5/netCDF-C。

## 📚 English

Right-click a variable and choose **Visual Analysis → Icosahedral 5 slices**.
The default is five unfolded strips (four base faces each); rectangular cards,
3D, unit-sphere face area, source-grid reconstruction and error share one Result.
The atlas uses the original UV/half-open node owners, shape `(5,n,2n)` with
`n=2^L`. Apex nodes 5/6 remain separate and are not geographic poles in the
unchanged native mesh orientation. Zoom/pan and full-resolution PNG are available.
The OpenGL 3.3 Core canvas and mesh buffers are copied/adapted from the existing
compiled `HDFStudio_to_Isosahedral` project: arcball/turntable, pan/zoom, camera
presets, sphere/polyhedron morph, depth-tested mesh edges and nearest-node picking.
PNG uses the actual GL framebuffer with the color bar. Unlit preserves palette
brightness; optional shading changes display brightness only. Unsupported GL
platforms retain numerical/2D analysis and report the unavailable 3D capability.
Run analyzes an explicit immutable layer/Mask snapshot, reusing an existing Image
or owning its hidden source reader. Both global and regional coordinates are
supported. Regional exterior and masked stencils remain NaN without boundary
wrapping. Layer/Play waits for analysis and rendering, keeps the previous image
with stale probes/exports disabled and stops at the last frame. Reconstruction uses
radial barycentric coordinates. Face vertex means are not conservative cell means.

Export a new NetCDF with zero-based node/face/edge connectivity, unit-sphere
coordinates, steradian areas, original-unit point values, forward CSR weights and
round-trip diagnostics. The same core is available through `hdf_cli ico` and
`ico-demo`. No Python runtime, guessed physical units or automatic model design.
