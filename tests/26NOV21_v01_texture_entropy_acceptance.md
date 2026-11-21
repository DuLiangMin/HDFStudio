# 🖼️ 二维纹理熵图：接口与逐步验收

## 🎯 已确认的输出

独立的 **Texture Entropy Image** 二维图像，与 Image View 同级；主菜单 View 和变量右键均可打开。一个变量即可使用，不需要比较对象。横纵索引与源平面一致，显示色标（bits），支持缩放、平移、切层、PNG 和数值 CSV 导出。

## 🔌 数据接口

- 输入：共享解码器得到的 row-major `vector<double>`，shape(H,W)；NaN/Inf 和用户 Mask 排除。可选通过当前 RasterView 快照接收当前层与 Mask，持续随该图更新；从变量独立打开时自行读层并提供 Mask 控件。
- 量化：当前有效平面统一 min/max，256 级，`floor(256*fraction)` 后限定0..255；常量场为同一级。量化独立于图像显示色表/反色/缩放。
- 分块：从左上角开始，不重叠4×4；shape(ceil(H/4),ceil(W/4))。每块 `H=-sum(p*log2(p))`，有效像元数至多16，上限4 bits；一个有效像元/常量块为0，全缺测块NaN。边缘按实际像元计算，不补零。
- 插值：在实际块中心之间对**熵数值**双线性插值至shape(H,W)，然后着色；外缘取最近块中心。无效邻块不参与并归一化权重；源缺测/Mask像元、所属全缺测块保持NaN，禁止填补缺测区域。
- 输出：`NcTextureEntropyPlane`保存块熵与全分辨率熵double；GUI固定0..4 bits色标、低值暗/高值亮、缺测灰色，PNG像素尺寸恰好W×H。插值不增加原始空间分辨率。
- 预算：GUI使用`reserve_raster`共享进程预算，最多16兆像元；只读快照保留storage lease；关闭释放内存。同步窗口源关闭后清图，不保留过期层。
- 科学含义：局部灰度分布复杂度；不区分相同直方图的空间排列，噪声也可高熵，平滑渐变也可非零，不能独自归因为有效信号来源。

## 🧪 顺序闸门

1. 🧮 T1：纯算法与`hdf_cli texture-entropy <file> <variable> <csv> [index ...]`，先完成Debug/Release定向测试、NumPy逐块/逐像元对账，再接GUI。
2. 🖼️ T2：独立二维画布、菜单/右键、色标、Mask/缺测、切层与当前图快照更新、导出及预算/关闭测试。
3. 🖱️ T3：双配置完整QtTest/CTest、真实鼠标打开二维图/切层/导出；两份真实预报与只读PRECT核对；本地完整便携版本验证。

## 🧭 窗口行为细则

从变量树或View主菜单独立打开时，窗口提供前置维度选层、Mask起止值以及PNG/CSV导出。若当前图像匹配该变量，则直接使用该图的只读快照（包含当前Mask），窗口显示“Following Image View”，源切层/Mask变化立即重算，主层导航通过同一源图路由；无须打开比较面板。源图关闭立即清空派生图；关闭文件删除该文件的派生窗口。色标固定0..4 bits，鼠标提示行列和局部熵；源缺测和Mask合并为灰色缺测，不把源变量单位当成熵单位。

全图最小/最大灰度范围每层重算，因此跨层颜色表示同一熵单位，但量化阈值依赖当前层，不宣称绝对物理纹理或信息来源。均匀灰度分布的4×4块至多16种样本；双线性放大只是显示重建，不能生成新细节。

## 📂 全局输入输出位置

✅ T1 `core-20261121-203851`：Debug/Release构建与专项各5/0/0；共20个有效平面、327014个熵double及对应块熵与独立NumPy逐值一致；常量/混合/随机/非4倍边缘/邻近大double/正负极值/1像元、全缺测拒绝与防覆盖全部通过；PRECT与两份真实预报源哈希不变。

✅ T2 `gui-20261121-204613`：Debug/Release专项各8/0/0，包括独立二维画布、同级菜单/右键入口、逐像元颜色、PNG/CSV与CLI一致、Mask/无效恢复、切层、源图关闭与预算回收。四幅真实预报熵图shape(190,384)的291840个熵double与291840个RGB像素独立NumPy全量一致；T1所有CLI场景再次通过。

✅ T2补强 `gui-20261121-205153`：源图关闭后Next/Play禁用、非对称二维场横纵插值断言加入；双配置各8/0/0，CLI与四幅真实熵PNG/CSV全量对账再次通过。

✅ 生命周期修复 `gui-20261121-205927`：MainWindow析构体断开派生图到自身连接，覆盖源/派生图同时保留的直接退出及独立熵图两帧播放；双配置专项各8/0/0、四幅预报291840个熵值及同数量RGB像素再次全量一致。固定构建输入、业务样本和二进制SHA256均记录在该轮`result.json`。

✅ 真实桌面`desktop-Debug-20261121-210110`与`desktop-Release-20261121-210205`各24/24通过，包括独立二维图、切层/播放暂停、Mask/无效恢复、原生PNG/CSV保存、主菜单/右键、源图跟随、About与正常退出。六组导出437760个熵值及同数量RGB像素与NumPy全量一致，源哈希保持。最终clean完整回归和便携包随后记录。

源码：`src/core/texture_entropy.*`、`src/views/texture_entropy_view.*`；CLI：`src/cli/hdf_cli.cpp`；GUI入口：`src/app/hdf_explorer.*`。
构建：`build/vs2022/`→`build/output/{Debug,Release}/`。确定性夹具和证据：`logs/S4-TextureEntropy/<stage>-<timestamp>/`。
业务文件通过`HDFSTUDIO_ZOOM_SAMPLE`和`HDFSTUDIO_FORECAST_ANCHOR/TARGET`传入，只读、不入库。
比较面板移除单图熵字段；已有全图标量仅保留为明确标注的Whole-plane histogram entropy诊断。
