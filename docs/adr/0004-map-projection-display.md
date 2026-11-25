# 🌐 ADR-0004：采用PROJ C接口增加气象栅格投影显示

- 状态：Accepted（2026-11-24用户授权实施）；极射赤面（Polar Stereographic，亦称极射赤平）、东亚Lambert和低精度海岸线为用户明确要求。显示预设可编辑，不代表数据源定义。
- 日期：2026-11-23。
- 关联需求：图形增加地图投影；内部任务PROJ-001。
- 替代关系：无。沿现有编号新增，不修改ADR-0002或ADR-0003。
- 实施状态：已实现PROJ封装、NC坐标CLI/GRIB适配、五种地图模式、独立地图画布/探针、低精度岸线转换与部署；最终定向验收见实施记录。未创建新发布包。
- 审核修订：2026-11-23；保留极区与东亚LCC必选，补齐角度图/米制投影、坐标适配、源索引、渲染状态和验收边界。建议参数仍待真实样本评价，审核完成不自动把Proposed改为Accepted。
- 追加与复核：2026-11-24，按用户要求将Basemap低精度海岸线纳入首版；采用离线线条参考层，日期变更线/极区、资源接口与许可见专节。设计与审核跨日，原决策日期保留。

## 🎯 背景与约束

设计前`src/views/raster_view.cpp`的`Projs`按钮只启用受限的一维CF经纬度查询和图例说明，像素仍按源行列显示。GRIB界面为source-grid index image。原有读取与`source_indices`并不足以证明地图已验证地理定位；本轮通过独立适配与测试建立准入。

审核发现现有`NcReader::geographic_grid()`只从`coordinates`查候选，要求特定standard_name/单位拼写，且未建立packing、单调性、bounds和地球模型的完整检查；它的`valid`不能直接作为新地图功能的准入标记。现有`PlaneSnapshot`保存的是**当前Mask后的**解码平面；GRIB的源消息索引与此平面的行主序索引必须区分。

目标是在同一标量图像窗口选择投影、范围和参数，叠加可开关的低精度海岸线，查看极区环流和东亚区域场，同时保持原格点数值、Mask、统计及源文件只读。显示投影与数据源CRS、科学重网格、矢量旋转分别定义。

输入为用户选择的只读科学文件；源码入口为`src/{core,app,views}/`；实际依赖在独立`ext/vcpkg/installed/x64-windows`前缀，以`PROJ_DIR`定位，产物到`build/output/<配置>/`，定向证据到`logs/PROJ-001/`。原始/派生海岸线在`resources/coastlines/low/`，许可在`licenses/{coastlines,proj}/`，详见[资源说明](../../resources/coastlines/low/README.md)与[依赖锁定](../../resources/proj/dependency-lock.json)。

## 🔎 现成库与备选方案

| 方案 | 核查结果与代价 | 建议 |
| --- | --- | --- |
| PROJ原生库，通过`proj.h`调用C API | 官方提供正/逆变换、线程context及CMake目标`PROJ::proj`；不是Qt绘图库，也不负责栅格采样 | 采用 |
| OSGeo4W的PROJ包 | 官方安装说明提供Windows渠道，适合独立命令行试用；接入需核对编译器、配置和运行依赖 | 可作开发参考，不依赖用户安装 |
| vcpkg的`proj` port | 有正式包配方与CMake用法；可为MSVC x64构建Debug/Release，可能本地编译，不保证总有预编译缓存 | 首选工程获取路径 |
| GDAL或完整GIS框架 | 本轮仅需坐标转换与已有Qt栅格显示，引入范围偏大 | 暂不引入 |
| 自写生产投影公式 | 增加投影、逆变换和奇点维护负担 | 不采用；独立公式仅用于测试参考 |

使用现代C API；库本身含C++代码，不能把“可由C调用”等同于“纯C实现”。已在独立vcpkg前缀安装`proj[core,tools]:x64-windows`，禁用默认net/tiff功能。实际PROJ 9.9.0、SQLite 3.54.1、baseline与Release/Debug DLL/数据库hash固定在dependency-lock中。

`cmake/Projection.cmake`通过`find_package(PROJ CONFIG REQUIRED)`及`PROJ::proj`局部接入，不切换产品全局工具链。随程序携带配套数据库、所需DLL和许可；每个context显式设置资源路径并禁用网络，不借用QGIS/conda资源。本轮不下载datum-grid合集，不执行跨基准转换。

接入时在独立依赖前缀试装，通过`PROJ_DIR`定位；不直接给已有产品build切换全局vcpkg toolchain，以免同时替换Qt/netCDF/HDF5依赖。锁定CRT及Debug/Release链接方式，数据库与DLL按同一包部署。`proj.db`是CRS查询资源，不是每个显式数学投影必经的运算输入；数据库缺失测试应针对需要它的入口，不能假定所有点变换必然报错。显式投影可用与数据库可用分别诊断。

## ✅ 投影范围建议

首版为**四类、五个地图选项**；`Source grid / 源格点`额外保留，默认进入该模式，绝不按文件名猜投影。

| 菜单选项 | PROJ方法 | 用途和取舍 | 优先级 |
| --- | --- | --- | --- |
| 经纬度平面 / Lon–lat | 角度坐标显示，不调用`eqc` | 经度为横轴、纬度为纵轴，单位度；全球概览，不声明米制距离或面积比例 | 首版建议 |
| 北极极射赤面 / North Polar Stereographic | `stere`，`lat_0=90°` | 北半球高纬和极区环流；局部保角，不保面积 | 首版必选 |
| 南极极射赤面 / South Polar Stereographic | `stere`，`lat_0=-90°` | 南半球高纬与南极；与北极分开预设，复用算法 | 首版必选 |
| 东亚Lambert正形圆锥 / East Asia LCC | `lcc`，2SP | 东西向展开的中纬度区域；保角，不保面积；本预设用于东亚，不作极区概览 | 首版必选 |
| Mercator / 墨卡托 | `merc` | 低纬、热带及海洋区域；极区畸变大，极点不可用 | 首版建议 |

这组选择借鉴WRF对lat-lon、Lambert、Polar和Mercator的用途划分，不表示首版自动支持WRF原始网格。经纬度平面明确为角度图；避免把“经纬度线性坐标”与PROJ不同版本的球面/椭球`eqc`米制实现混用。以后若需要米制等距圆柱，应另设明确方法和模型的选项。

**第二批候选**：Mollweide（`moll`）优先，补全球等面积概览；PROJ该方法为球面，须明确等积纬度/球面模型方案，不能把椭球纬度直接套球面公式后宣称椭球等面积。Robinson（`robin`）适合全球展示，但不是等面积，与现有概览需求重叠，后置。Albers等积圆锥、Lambert方位等积分别留给区域/极区面积专题。UTM/高斯、Web Mercator、Orthographic及任意EPSG浏览不进入首版；需小区域测绘、瓦片底图或球面观感时再提出具体需求。

### 🧭 可编辑显示预设（建议值，不是数据源定义）

| 预设 | 中央经线`lon_0` | 原点纬度`lat_0` | 标准纬线 | 初始可见范围 |
| --- | --- | --- | --- | --- |
| 经纬度平面 | 0°；可切180°太平洋居中 | 不适用 | 不适用 | 全球，或当前数据范围 |
| 北极 | 0°；可旋转至105°E等方向 | 固定90°N | `lat_ts=60°N` | 20°N–90°N，圆形边界 |
| 南极 | 0°；可编辑 | 固定90°S | `lat_ts=60°S` | 90°S–20°S，圆形边界 |
| 东亚LCC | 105°E | 35°N | `lat_1=30°N`、`lat_2=60°N` | 70°E–150°E、10°N–65°N |
| Mercator | 0°；可编辑 | 不暴露无效参数 | `lat_ts=0°` | 当前数据范围与80°S–80°N的交集 |

只允许修改该模式有意义的参数；北/南极原点纬度固定。东亚数值仅为候选显示预设，不声称国家标准、EPSG定义或文件原投影。LCC的`lat_0`决定投影原点，不保证它恰好位于裁切后画布中心；其显示中心由范围和等比例Fit决定。米制投影固定false easting/northing为0米，极射/Mercator只暴露`lat_ts`、LCC只暴露标准纬线，不再同时暴露`k_0`。80°裁切是产品默认值，不是Web Mercator的85.0511°规则。

2026-11-24 PROJ-003修订：极射、LCC和Mercator优先使用经过核验的源球体/椭球。初版对未知模型一律禁用米制模式，导致实际PRECT虽有明确经纬轴，选择极区时却退回源图；该限制现只保留在物理变换核心。**显示层**可对经纬轴有效、模型未声明的源使用`a=b=1`单位球生成归一化投影形状，并明确标记“Unit-sphere display only; source Earth model unknown; distances unavailable”。单位球仅属于MapDisplay的显示Spec，不作为源模型，不写入GeoGrid、INI或科学输出，不声称米制距离、面积或椭球精度；每个新源独立决定显示模型。显式非法/冲突模型仍拒绝，不得将其当作“未声明”绕过。任何情形均不得静默套WGS84或PROJ默认椭球。

预览与确认前拒绝NaN/无穷、非正半径/非法椭球和零面积范围。极射原点固定±90°，`0<abs(lat_ts)<90°`且同半球，外圈为同半球`0<abs(boundary)<90°`；东亚LCC限定`0<lat_1<lat_2<90°`、`-90°<lat_0<90°`及不含极点的显示纬度范围；Mercator要求`abs(lat_ts)<90°`及`-90°<south<north<90°`。这些是本软件预设限制，不是所有数学投影的完整有效域。

经度用`west`与`span`表示，`0<span≤360°`，明确区分零宽和全球；区域跨180°时仍合法。东亚LCC首版限制`span<180°`，所选区域须在`lon_0±180°`同一连续分支内，不跨背面接缝。极区预设显示完整经圈与圆形外边界。PROJ创建、逐点变换及非有限输出仍要检查，参数通过不保证每个点有效。

## 🖱️ 用户操作与行为

1. 打开变量的Image，顶部独立`Map projection`栏提供“源格点/经纬度/北极/南极/东亚Lambert/Mercator”选择，选中即验证坐标、重绘并保存，不需要打开窗口或点击OK。标签、选择、Options和进度/错误**只占一行**；状态长文省略并以tooltip完整显示，配置错误也不得增加新行。900/640宽检查入口和选项可见，不依赖溢出菜单。
2. 旁边`Options…`打开窗口模态详细设置，投影、参数与岸线改动立即更新图像，不等待OK；只保留`OK / Cancel`。OK仅确认详细选项并关闭；Cancel、Esc和标题栏关闭撤销本次预览，恢复打开前的模式、视口和岸线样式，Enter对应OK。非法参数显示原因并禁用OK；极区可调边界纬度和旋转方向，东亚可调两条标准纬线。
3. 状态栏给出投影与就绪状态，参数详情显示源模型或单位球显示限定、坐标验证证据和“源网格最近邻”语义；经纬网及边界标签随同一变换绘制。投影保持x/y同一投影单位/像素，未知模型不得称米制；角度图保持经/纬同一度/像素；Fit加留白，不横纵各自拉伸。
4. 鼠标取值报告点击位置经纬度、用于着色的画布像素所对应源行列、源格点经纬度与该快照值。缺测或Mask命中显示相应状态，不绕过Mask输出有效读数；如显示Mask前值，应单独标注并持有对应解码快照。源范围外不向边缘无限钳位。
5. 参数错误在预览与确认前校验；切层/切源后旧投影结果不得冒充新数据。任务创建或整体渲染失败清空地图/探针并显示原因；个别域外点仅置无效状态，不令整幅任务失败。零覆盖结果显示“当前范围无源数据”，不保留未标记的旧图，可显式返回源格点。

### 💾 显示偏好（2026-11-24用户补充）

程序根目录`HDFStudio.ini`的`[MapProjection]`组以版本1保存稳定模式键、投影参数/范围及岸线样式。主栏成功选择立即保存，详细设置仅OK写入；取消/预览不写入。新图像自动读取，在当前文件坐标/模型准入后恢复；不持久化源模型、数据路径或业务值。文件损坏/不支持版本显示警告并用源图；目录不可写显示失败，不能假报已保存。新源不支持已存模式时保留偏好并给原因，不用旧模型补全。临时缩放/平移不跨图像恢复。测试经`HDFSTUDIO_SETTINGS_FILE`隔离，不修改产品用户配置。

对话框语义依据Microsoft Windows桌面UX指南的`Dialog Boxes`与`User Interface Text`：设置窗口使用OK/Cancel，标题栏关闭与Cancel同效；Apply为可选的应用但不关闭操作。这里采用可撤销实时预览，因此省去Apply、Close及重复Source grid按钮。

入口允许请求坐标验证；只有最后两维与真实纬/经同序且准入通过，才允许进入地图。时间—纬度、垂直剖面和任意二维矩阵不能冒充地表地图。纬经范围投影后对边界加密取样再求显示范围，不只变换四个角；跨接缝和极区范围按各自裁切规则处理。

首版地图模式禁用源图的Flip H/V和索引空间Profile绘制，保留原模式状态，返回源格点后恢复；不能只翻转位图而不更新经纬网和探针。缩放/平移作用于同一个地理视口；地图采用最近邻纹理显示，不能再由Qt平滑缩放混合颜色后宣称对应一个源值。地图模式不传递源图索引缩放/翻转给其他窗口；选层同步仍沿现有规则。

经纬网与低精度海岸线共同作为首版定位元素。海岸线为独立可开关的线层；只有地图模式显示，源格点、时间/垂直剖面不叠加。海岸线参数和图像参数共同显示于Projection面板，具体约定如下。

## 🌊 低精度海岸线（首版必选，2026-11-24补充）

### 📦 数据选择与分发

采用**Basemap `resolution='l'`（low，小写字母l）同源数据**。本地输入已锁定Basemap `v2.0.0`、提交`6fed00b9b6146d19ffa98fd00a19cbb0ee448141`中basemap-data的`gshhs_l.dat`和`gshhsmeta_l.dat`，上游说明源为GSHHG 2.3.6。`l`是矢量简化等级，不是0.1°等固定网格，也不是承诺某个米级定位精度；不下载高精度`h/f`资源，不随着放大自动升级。

| 选择 | 取舍 |
| --- | --- |
| Basemap低精度同源数据，预处理成离线折线 | 采用；满足用户熟悉的细节程度，运行时只用Qt与PROJ |
| 应用内调用Python/Basemap或引入GEOS/GDAL绘图栈 | 本轮不采用，避免为单一线层扩大运行时和分发依赖 |
| 改用Natural Earth等其他数据 | 本轮不替换，避免未经说明改变用户指定的低精度来源 |

只画海陆边界与南极冰缘：选择GSHHG level 1和level 5，排除湖岸/湖中岛等level 2–4、grounding line level 6及国界/河流。沿用Basemap低精度的**默认面积阈值1000 km²，保留面积大于阈值的要素**；面积使用上游元数据，不按屏幕计算。该内容筛选比Basemap完整drawcoastlines更窄，属于本软件明确选择。南极使用数据版本中的冰缘线，标明非当前海冰边缘或实时冰架监测。

实际资源位置：`resources/coastlines/low/`已保存两份原始数据、manifest及来源说明；`licenses/coastlines/`已保存上游`COPYING`、`COPYING.LESSER`及署名。数据分别为719016/562915字节，共1281931字节，10621条记录/89877顶点；完整offset/bytecount、有限性和角度范围检查通过。level 1/5且面积>1000 km²的候选为302条记录/31559顶点，仍含E/W分片与南极人工封口，不等于最终折线数。原始完整文件保留，运行时内容筛选不修改原文件。

已生成归一折线302段、31547顶点，移除12条成对日期线边及固定南极人工封口边；完整删除来源与理由见derived manifest。CMake复制资源至`build/output/<配置>/resources/coastlines/low/`及对应许可证目录。应用按可执行文件相对目录读取，离线可用，不依赖Python/Basemap或首次启动下载；本轮只验开发部署，不创建发布包。

海岸线数据按上游标注的**LGPL-3.0-or-later**记录，不能标成Basemap主代码的MIT许可；原始数据、转换工具、变更说明和许可一同保留，归一数据仍保留上游权利说明，不作重新许可推断。THIRD_PARTY_NOTICES、依赖文档与本地manifest已登记来源/版本及各文件SHA256；从固定Git提交直接取文件，未下载或安装wheel，不虚填包hash。未来派生数据的转换源码必须随资源公开或提供明确公开来源，不能只放进被现有发布规则排除的tools目录。

### 🔌 预处理和运行时接口

原始资源下载与离线核查工具已为`tools/26NOV24_v01_fetch_coastline.py`，仅使用Python标准库；缺省补齐缺失固定文件，已有不一致内容拒绝覆盖，`--check`绝不联网。`.gitattributes`保持原数据/许可字节，避免换行转换破坏hash。

公开转换入口为`resources/coastlines/26NOV24_v01_prepare_coastline.py`，随资源保留。输入为已校验的本地文件，不自动下载；原数据是Basemap的little-endian float32经纬对。无参数确定性生成，`--check`在内存重建并逐字节核对输出。归一输出与原始资源manifest分开：

| 对象 | 内容与shape |
| --- | --- |
| `CoastlineAsset v1` | `coastlines_l.json`，UTF-8；`schema_version=1`、dataset/version/hash、`resolution='l'`、`area_threshold_km2=1000`、`coordinate_order='lon,lat'`、`units='degree'`、参考子午线和来源CRS说明；`features[K]`各带source_id、level、area_km2和`parts[P]`，每段`coordinates:float64[N,2]`、N≥2 |
| `CoastlineData` | 运行时只读顶点`float64[V,2]`＋`offsets:uint64[P+1]`、段/要素ID与dataset revision；首尾重复只表达真实闭合岸线，所有段都按显式顶点逐段连线，不由画布自动closeSubpath |
| `CoastlineSpec` | `enabled`、固定数据等级l/面积阈值、线色与逻辑像素线宽；不混入ProjectionSpec的地球模型，也不改变plane revision |
| `CoastlineOverlay` | 投影/裁剪后的`float64[M,2]`＋段offsets、相同ViewportSpec及overlay generation、dataset hash、状态/诊断与lease；没有科学值或plane_index |

Basemap资源的跨180°E/W分片和南极封口包含为多边形准备的结构，**不能把原闭合多边形全部边都画成海岸线**。预处理须依据配对ID和固定版本结构重建真实边界，只删除可追溯的人工接缝/南极封口，保存被移除边的来源ID和理由；无法明确识别则转换失败，不凭“线太长/沿经线”删除真实岸线。保留原始float32坐标提升为double的精确值，不再作地理简化；与固定版本Basemap参考轮廓、北极和南极用例核对。归一折线不必首尾闭合，跨接缝处应成为独立段。

### 🖊️ 显示、模型与裁剪

- 控件仅增加`Coastlines / 海岸线`开关，地图模式默认开；参数详情给出低精度来源、面积阈值与“仅作定位参考”。默认深灰`#303030`、0.8逻辑像素线宽；线色/宽可在投影设置中修改，放大不把线宽按地图比例放粗。绘制顺序为标量图→经纬网→海岸线→标签/探针，线层不截获鼠标事件；线条抗锯齿仅影响叠加，不改变源图的最近邻采样。
- GSHHG参考位置与科学源网格的datum/历元不一定相同。首版按相同经纬角直接送入当前投影，采用**角度对齐的示意叠加**：模型球体也可显示，但明确“参考岸线，未做基准转换/精密配准”。不把GSHHG曲线改称源模型的真实海岸线，不用它判定格点海陆、制作Mask、做距离/面积计算或自动校正源坐标。科学源明确存在未处理的参考子午线冲突时禁用叠加；角度坐标已验证但datum未知可显示并注明状态。
- 岸线和栅格共用ProjectionSpec及ViewportSpec；先按当前经度分支展开与拆线，再对曲线自适应细分和投影。细分在展开后的经纬线段上做线性插值，不擅自把上游折线解释为大圆航线。目标为曲线在屏幕上的弦误差≤0.75逻辑像素，这是显示误差而非数据地理精度；同时检查中点及四分点，不能仅靠两个端点判断。跨接缝/无效区域断线，切线进入/离开视口要求交，不能只保留“端点都在里面”的线段。
- 裁剪区域为投影有效域、用户地理范围和画布；北/南极再裁圆形外圈，Mercator遵守纬度限值。岸线可出现在图内没有源值的区域，用于参照，不以数据Mask裁断。全球图换0°/180°中心、东西经跨界及南极封口不得产生横贯地图或指向极点的假线，不对裁剪后的开放段重新封口。

### 🔄 缓存、失败与验收

原始只读折线按dataset hash共享一次；投影几何键含dataset hash、内容筛选、完整ProjectionSpec、extent/clip、DPR及细分策略。线色/开关只重画叠加；换层/改色表不重新读取岸线，几何条件相同时可复用。投影/范围变更则使旧overlay失效，与新图按同一视口发布；晚到的岸线不得盖到新投影上。资源、原始坐标、投影临时缓冲和Qt路径都计入共用预算，不设无界多视口缓存。

首版候选资源上限为文件16 MiB、输入200000顶点、单视口投影500000顶点，细分深度上限12；这些为工程限额，PROJ-05用实际低精度资产核对后冻结。任一超限、坏hash/错误schema或资源缺失仅禁用岸线并显示原因，标量图和经纬网继续；不能默默截断线条或退换高精度文件。浮点/域外错误使相应段断开并报告不完整；不得把未完成overlay标为成功。后续允许替换资源时校验新manifest并标注自定义身份，不以原数据hash冒充官方数据。

增加PROJ-05：验收资源版本/许可/尺寸/段数、低精度及1000 km²筛选；东亚大陆与日本/台湾/海南等主要岛屿、北极格陵兰/跨日期线、南极冰缘无封口假线；跨180°与0°中心、极区圆边界、DPR/缩放/pan；开关前后源值/探针/统计/Mask及源PNG一致；损坏/缺失/离线/任意工作目录启动、缓存命中和关闭清理。Basemap同源参考仅检验资源转换与几何接线，不替代独立投影控制点或海岸线真实位置精度验证。

## 🔌 输入输出与模块所有权

| 接口 | 数据与语义 |
| --- | --- |
| `GeoGridDescriptor` | source/session、object、field/grid、coordinate revision；`H,W`及行/列对应的原维度身份；首版`lat:float64[H]`、`lon:float64[W]`与当前plane同序，可选bounds为`[H,2]`/`[W,2]`。含坐标解码/单位、升降序、周期、覆盖区、地球模型、prime meridian和证据/失败原因 |
| `ProjectionSpec` | 版本化模式及完整有效参数；经度以已核验参考子午线表达。物理投影使用源球体`R`或椭球`a,b`（米），`a≥b>0`；显示层可单独持有`a=b=1`归一化球体并标记display-only，不写回源/INI。角度图无需半径，不补造datum |
| `ViewportSpec` | 投影平面范围`xmin,xmax,ymin,ymax`与单位标签（米或度），地理裁切区，物理像素`Hp,Wp`，DPR及屏幕到画布映射。缩放/平移/resize不是只改像素尺寸 |
| 点变换输入/输出 | `float64[N,2]`：外部`(lon,lat)`均为度；角度图输出相对中央经线展开的`(lon,lat)`度，其他模式输出`(x,y)`米。返回逐点状态；低层度/弧度转换只在PROJ封装内发生 |
| `RenderInput` | 不可变当前Mask后的`PlaneSnapshot`与原storage lease，`source_cell_state:uint8[H,W]`及lease、`GeoGridDescriptor`及lease、投影、视口及色表/色阶/分类/Mask状态身份；背景任务不借用QWidget或可变树对象 |
| 显示结果 | `RGBA:uint8[Hp,Wp,4]`、`plane_index:int64[Hp,Wp]`和`pixel_state:uint8[Hp,Wp]`。无源覆盖/变换失败索引=-1；missing/masked仍保留可定位的源索引。携带输入身份、物理像素映射与预算lease |

`plane_index=r*W+c`是当前解码平面的行主序索引，**不是GRIB `source_indices`中的原消息扫描序号**；如需原扫描位置，只经已验证的后端映射另行查取。首版不转置数值快照；遇到`[lon,lat]`等轴排列应明确拒绝或先完成单独适配，不按相同长度猜轴。

`source_cell_state`在原解码值和本次Mask均可用时生成，区分valid、source-missing、user-masked；不能只从Mask后的NaN反推原因。该状态数组与plane同revision，纳入预算；若显示Mask前值，还需额外持有其只读值及lease。`pixel_state`再补投影域外、地理裁切外和源覆盖外，探针与颜色共同消费。

### 🔄 发布、缓存与资源

几何缓存键包含source/session、对象与field/grid、坐标revision、覆盖/周期校验revision、全部ProjectionSpec、地理裁切与完整ViewportSpec；重复周期端点一致性依赖当前帧时，校验revision必须随plane变化。着色/发布键再包含plane revision、Mask及色表/数值范围/分类设置revision、generation。改色表不能复用旧RGBA；同shape、同像素尺寸不足以复用不同范围、不同层的结果。

视图展示状态与科学平面状态分开：只改投影/视口时，当前源plane及源统计仍有效，地图探针和地图导出等待新render；切层/切源沿现有规则使旧数值不可用。渲染期间可保留标明“上一视图”的位图；成功时同批切换图像、经纬网、探针映射和图例，失败时清空地图输出但不抹掉仍有效的源数据。无效参数不提交，保留已应用状态并禁用OK。

2026-11-24 PROJ-004播放修订：正常读取/着色/重投影保留一个已完成的`painted`结果及其ProjectionSpec、viewport和预算lease；`presented/result`与地图探针仍立即失效，不把旧图数值当新层。等待状态明确“previous image; map values unavailable”。新投影就绪时原子替换图像/几何，并通过`frame_ready`发布相应色标与图例，等待时色标保留旧图范围。实际错误、源变更、越界及返回源模式清空保留画面；晚到结果仍须generation匹配。`RasterView::render_pending()`包含活动地图busy，主窗口原有Play ready check因而等待投影完成，避免快于计算速度的播放持续取消全部地图帧；不积压播放请求。旧/新结果的同时存活仍分别受原共享预算约束，不增加多帧缓存。

工作队列使用active＋latest，逐块检查取消；任务持有全部输入lease，在完成/取消后释放。预算先申请再分配，含源快照、源状态`H*W`字节、坐标、至少`13*Hp*Wp`字节的RGBA/索引/状态、临时缓冲及同时存活的新旧图像和GUI副本。乘法先做溢出检查；申请失败明确拒绝，不偷偷缩小图像。画布使用实际viewport物理像素，不按源H×W另造无界地图；每视图至多一个任务在跑、一个最新待办，多视图仍服从共用调度和进程预算。关闭时断开发布、使代次失效，后台任务自行释放，禁止捕获已析构视图或强杀线程。

`core/map_projection.*`封装PROJ，不依赖QtWidgets；每个工作线程独占`PJ_CONTEXT`和`PJ`，先释放PJ再销毁context。首版以显式同模型数学投影创建operation，传入`a,b`及完整参数，检查正逆方向的角度单位、错误码和非有限值；不能同时套低层弧度转换与CRS可视化归一造成双重转换。无datum时只说明“同模型显示转换”，不创建虚假的EPSG:4326源CRS。

原文件若有WKT/CF定义，保留原文和解析证据；用于执行的PROJ operation字符串不能替代完整CRS身份。首版坐标接口限定已核验的Greenwich经度；已声明非Greenwich来源应由验证过的适配器归一，否则拒绝地图模式。完整CRS间转换、ballpark/网格改正和动态基准均不进入本轮。

`app`适配器负责坐标与源快照校验，`views`负责控件、画布和取值。后台显示任务只处理已准备的不可变坐标和值；新增NetCDF/HDF5坐标读取通过既有同目录CLI进程边界提供，不能把当前同步`geographic_grid()`直接搬进GUI后台线程。扩展的坐标结果须有协议版本、source/session/轴身份、shape/字节数及lease校验；协议细节在PROJ-02编码前任务契约冻结。GRIB坐标请求复用现有读取控制器，不另开一条无身份、无预算的读取路径。

### 🧩 源网格与目标投影分开支持

- **首批源**：有可靠一维真实经纬度、与当前`[H,W]`同序的NetCDF规则网格，以及验证后的GRIB `regular_ll`。校验坐标解码/缺测、升降序、扫描序、经度接缝、重复周期端点和地球模型；不能只依变量名lat/lon。检查coordinate bounds与格点中心/单元位置；缺少有效边界时限制到已验证中心坐标覆盖区，不向外猜半个网格、补到极点或补满全球。

2026-11-24 PROJ-005扩展：准入完整矩形`regular_gg`，逐点验证每行纬度/每列经度可分离、保留真实Gaussian非均匀纬轴，不以行号线性生成纬度；源值/Mask/探针索引/导出不变，不扩展reduced/rotated网格。ecCodes 2.47 Gaussian继承Regular的行主序坐标迭代，交替/列优先扫描只对值应用`source_indices`，坐标保留迭代网格顺序；与`regular_ll`按j-consecutive转置的适配明确区分。六种扫描夹具与独立Gaussian根/值序核对。用户请求的地图文字取消矩形底框，最终截图采用无描边的实心文字，按背景明暗选浅/深色，避免细字空心感。

同轮截图细查补充：源球体/椭球长度通过ecCodes double接口保留17位roundtrip精度，禁止从6位有效数字的string accessor反解模型。实际Gaussian源经度末点舍入使严格360°判定失效；仅对已通过坐标校验、ecCodes明确核验`gridType=regular_gg/global=1`的网格保留经向周期，不修改源经纬度或给区域网格补全球。模型精度、接缝最近邻及JSON往返均有直接断言；地图文字框之间做局部碰撞避让。
- **不是自动获得的能力**：源文件本身为Lambert/极射网格、WRF曲线网格、旋转经纬度、二维经纬度或reduced/point-list，都需各自适配器和测试。首版不把逐点经纬度扁平数组误当可分离规则网格。
- 通用HDF5或元信息不足保留源格点；不默认EPSG:4326。C7F已在实施阶段只读核验`regular_ll`坐标、模型及同序平面，测试输入由环境变量提供，私有路径不进入公开源码。

坐标适配至少包含以下独立检查，不复用一个含义模糊的`valid`：

1. **NetCDF关联与解码**：先检查维度坐标变量，再检查`coordinates`，按组作用域和维度身份关联，不能只比长度或要求必有`coordinates`。支持CF-1.8经纬单位规定的等价拼写；`standard_name`可辅助但不设为所有合法文件的强制属性。坐标先处理存储域缺测及scale/offset，再验证范围、严格单调与有限值；旋转轴`grid_latitude/grid_longitude`不当真经纬度。
2. **GRIB对齐**：edition-aware读取球体/椭球及扫描信息，对纬/经数组各自检查返回长度与`H*W`。ecCodes 2.47 `regular_ll`坐标迭代器不反转交替扫描，故坐标仅按j-consecutive转置，值仍按原`source_indices`重排；六种扫描夹具用解析经纬度和原扫描值逐点核对。验证每行纬度、每列经度可分离后压缩成`[H]`/`[W]`。不能笼统把值的原扫描映射再次应用于坐标。
3. **模型证据**：分别记录坐标关联、数值解码、地球模型与datum状态。CF的grid_mapping/WKT或GRIB规定提供模型；不得将GRIB2形状表直接用于GRIB1，不从名字推半径。冲突、不支持、缺失分别诊断。没有地球模型限制物理米制输出；屏幕可使用明确标记的单位球，不否认已明确的角坐标。
4. **范围与周期**：源坐标在自己的连续经度分支验证，裁切窗口另取分支；不按归一化后min/max推断跨日期变更线范围。仅在坐标/格距及覆盖证据一致时标记周期。重复0°/360°端点仅在同帧解码值（有限值逐值相等）和Mask/缺测状态一致时建立显示别名，保留原数据；不一致拒绝周期拼接并报告。该帧相关校验参与缓存身份，不能只按grid identity复用。

### 🎨 显示采样与科学输出

目标画布**像素中心**→逆变换→地理裁切与源覆盖检查→源网格索引→着色。画布左上为`(i=0,j=0)`，米制例：`x=xmin+(i+0.5)*(xmax-xmin)/Wp`、`y=ymax-(j+0.5)*(ymax-ymin)/Hp`；角度图使用同式但单位为度。鼠标经DPR及viewport映射后查询该像素的`plane_index`，从而与实际颜色一致；连续鼠标经纬度和所采样格点坐标分别标注。

“最近邻”定义为在可分离源轴上按纬度差和展开经度差分别选最近坐标，非球面最短距离搜索；恰好等距取较小原plane索引。遇到最近点缺测/Mask则保留该状态，不继续寻找别的有效值。bounds须通过长度、有限性、方向、坐标落入对应区间及重叠/间隙检查，覆盖判断按有效区间并集，不能以外包矩形填补空洞；源网格不满足首版约束时明确拒绝。bounds只限定覆盖，首版不承诺单元守恒重网格。缺bounds的非周期轴只覆盖首末格点中心，不推半格；退化单点轴不人为扩展出二维区域。

只有验证为周期的经度轴才环绕拼接。极点处经度不唯一，探针标记“经度未定义”；若源确有极点行，显示采样按`lon_0`确定列并说明约定，不把该列当唯一物理代表，不平均其他列。无极点数据不补值。经纬网在接缝和无效区断开；圆形极区外、源外、投影失败、缺测、Mask用独立状态，不能都误报为0或同一种NaN原因。

这是显示重采样，会改变屏幕像素布局，不产生新的科学网格。原统计、Mask、直方图、比较和CSV仍基于当前Mask后的原plane；颜色范围也沿源平面设置，不随地图可见范围自动重算。首版保留“导出源格点PNG”，必须从对应源渲染缓存取得，不能直接复用地图pixmap冒充原分辨率图；等待相应revision有效且拒绝覆盖源文件。新增投影视图PNG按钮后置，届时需定义参数注记、图例和输出尺寸。GeoTIFF、投影坐标CSV、面积加权和保守重网格不纳入首版。

## 📦 后果

同一个全球标量场可从北极、南极或东亚视角看，并有低精度海岸线作定位参考，无需转换业务文件；原格点模式始终可用。新增PROJ/数据库、岸线数据与许可分发、显示映射预算和坐标验证成本。支持一种目标投影不等于读懂同名源投影，参考岸线叠加不代表科学配准或面积统计正确。

## 🧪 验证、分步落地与复审

| 步骤 | 可验证交付与停止条件 |
| --- | --- |
| PROJ-01：依赖与点变换 | 固定MSVC x64包/数据库及CRT，最小C调用例Release/Debug可链接；离线、非工作目录启动；显式投影与CRS数据库入口分别检查缺资源行为；球体/椭球的轴序、度/弧度、正逆、错误点 |
| PROJ-02：一维地理网格 | 合成NC涵盖无coordinates属性、单位别名、分组/维度身份、packed坐标、未知模型；GRIB涵盖不同扫描序、返回长度和可分离性；0..360/-180..180、区域跨接缝、重复端点一致/冲突及bounds；随后只读核验C7F模型和值-坐标对齐 |
| PROJ-03：极区和东亚必选 | 北/南极中心、经度不唯一、同纬圆和圆边界；LCC原点、标准纬线局部比例、经线对称及区域边界；独立控制点与精确源索引，检查missing/Mask不被填补；原统计/CSV和源格点PNG保持 |
| PROJ-04：角度图/Mercator及生命周期 | 角度图无模型可用但不虚报米制，Mercator奇点/裁切；跨180°、DPR、Fit/zoom/pan、同尺寸不同extent；改色表/Mask与切层latest、预算拒绝/取消/关源/直接析构；真实桌面操作与截图；相关两配置通过即停 |
| PROJ-05：低精度海岸线 | 固定Basemap l同源资产与许可；预处理真实线段、E/W与南极封口；投影裁剪、默认开关/线宽、缓存/预算/失败降级；上述海岸线专项两配置及必要桌面检查。资源准备可在PROJ-01后独立推进，显示接入依赖03/04 |
| PROJ-06：选择即重绘与可发现性 | Image顶部独立单行投影栏，标签/下拉/Options/状态或错误共一行；900/640宽检查；NC/GRIB全部五模式只切下拉框，不点击二次应用；真实鼠标点弹出项，比较栅格像素和plane_index映射，不能只断言状态文字变化；无声明模型NC自动采用明确标记的单位球显示；快速选择仅呈现最后请求，无效坐标/冲突模型仍拒绝，源PNG/值不变；真实桌面包括实际PRECT，裁取画布排除状态文字后比较 |
| PROJ-07：标准确认与可撤销预览 | 详细设置仅OK/Cancel；改模式、参数和岸线立即预览；OK/Enter保存并关闭，Cancel/Esc/标题栏关闭恢复原模式、视口和岸线；非法参数禁止确认，修正可继续；取消预览期间旧任务不得回写；验证实际像素及按钮集合 |
| PROJ-08：便携式偏好 | 程序根目录INI，主栏成功选择及窗口OK写入；预览/取消字节不变；独立进程从无关工作目录启动自动恢复模式/参数/岸线；换源重新读取模型，不用旧模型；源不支持不改偏好；损坏/未知版本/非法参数/不可写均有可见行为；测试不污染用户配置 |

独立参考采用有精度说明的公开控制点或独立公式，并覆盖球体和椭球。测试前固定模型、参数、单位、有效域及参考舍入误差：1毫米与1e-8度仅为内部正常点候选阈值，不套给精度较低的来源或奇点。经度往返比较取模360°的最小差；极点不比较无定义经度。源索引、Mask/缺测状态和输出身份要求精确一致；PROJ CLI与同库API对账只验证接线，不能作为独立数学参考。软件参数测试不能替代真实数据地理定位验收。

跨datum/动态坐标、二维/非结构源网格、矢量方向旋转、精密地图叠加配准、科学重网格或大规模地图缓存引入时另审ADR。当前只实现首版五个地图选项及参考岸线；第二批投影和扩展项仍未启动。

### ✅ 2026-11-24实施与验收

以下为PROJ-001首轮历史验收，覆盖当时点击Apply的操作路径；用户随后发现“只切下拉框不重绘”和入口隐蔽，不能用这些记录宣称PROJ-06至08通过。新增要求与修复验收由PROJ-002任务记录，保留原失败与修后证据。

- PROJ-01至05已实现；`tests/projection_test.cpp`的20个功能槽在Release/Debug均通过（QtTest各22项，含init/cleanup）。覆盖独立球体/椭球公式、坐标/模型/bounds/周期、六种GRIB扫描、像素状态/索引、源PNG、岸线、latest/取消、已删除任务预算及应用关窗。C7F只读核验得到361×720与GRIB1声明球体，未由文件名推断模型。
- ADR-0003的12个搜索功能槽双配置通过；直接受影响的2个源图/坐标准入与3个GRIB图像槽通过。最终相关构建与专项证据在开发日志`logs/PROJ-001/verify-20261124-014939-804293/`，包含程序SHA256与真实退出码。
- `deployment-20261124-014947/`独立副本、无关工作目录及仅系统PATH下验证通过；核对PROJ/SQLite/数据库/许可hash与MD/MDd实际导入，缺失/损坏可选资源时栅格继续。显式投影与数据库入口分别测试，不把显式投影不需数据库误报为缺资源检测失败。
- `desktop-20261124-015021/`通过产品真实鼠标五种投影、岸线开关、返回源图及正常退出码0，输入SHA256不变；北/南极与东亚截图已人工查看。截图只验证显示和接线，不证明海岸线精密配准。
- 原始岸线离线核查与公开转换器`--check`通过；归一资产SHA256为`7cc253122fe36ccc810ffef894af1d53b558e2a384c404d77f565b8550c89efd`，含原始/派生manifest的JSON与原字节资源使用Git `-text`保持重建身份。早期失败日志保留，最终证据不覆盖失败轮。版本仍0.14.06，本轮只本地提交，不推送或创建发布包。

### ✅ PROJ-06至08补充实施与验收（2026-11-24）

- 完成顶部独立投影栏、选择立即重绘、参数/岸线实时预览、仅OK/Cancel及全部取消入口回退。INI保存确认偏好并在新图自动恢复，严格重验当前源坐标和模型，未知/非法配置与不可写均可见。
- 最终`interaction-20261124-024056/`的Release/Debug各10个相关功能槽通过（QtTest各12项含initTestCase/cleanupTestCase）；包含NC/GRIB五模式像素及plane_index变化、GRIB首帧自动恢复、独立子进程从无关目录读取INI、源模型更新、取消字节保持、版本/参数/写失败与4个直接生命周期回归。未重跑不受影响的数学/岸线转换或无关全量测试。
- `direct-desktop-20261124-024504/`当前Release产品真实鼠标50项通过；五模式排除状态文字后的画布像素差异分别为100%、24.56%、30.49%、53.27%、53.17%，返回源图精确一致。OK写盘、Cancel不改INI、程序重启自动恢复北极及两次正常退出0通过。截图已读回，输入文件SHA256保持。
- 保留旧下拉不重绘复现、GRIB请求升级与Source配置校验遗漏的失败证据；桌面前台失败属于验证器焦点，修正后只续验桌面。完整证据与失败范围见开发任务PROJ-002。版本仍0.14.06，仅本地提交。

## 📚 一手依据（2026-11-23初稿，2026-11-24复核与补充）

- [Microsoft Dialog Boxes](https://learn.microsoft.com/en-us/windows/win32/uxguide/win-dialog-box)、[User Interface Text](https://learn.microsoft.com/en-us/windows/win32/uxguide/text-ui)：引用其Windows桌面设置对话框确认/取消语义；该UX指南源自Windows 7，不称其为新版Windows的全套视觉规范。
- [Qt 6.8 QSettings](https://doc.qt.io/qt-6.8/qsettings.html)：显式文件名+IniFormat、保留其他设置组、`sync()`后检查`status()`，保留默认原子同步要求，不为写入成功关闭锁或原子保护。
- [PROJ C API quickstart](https://proj.org/en/stable/development/quickstart.html)、[CMake接入](https://proj.org/en/stable/development/cmake.html)、[Windows安装与构建要求](https://proj.org/en/stable/install.html)。
- [PROJ函数参考：operation/CRS、角度单位与逐点错误](https://proj.org/en/stable/development/reference/functions.html)。本记录按接口语义设计，在线stable和port主分支是可变来源，不是已冻结的本机版本。
- [vcpkg包配方](https://github.com/microsoft/vcpkg/blob/master/ports/proj/vcpkg.json)、[port构建选项](https://github.com/microsoft/vcpkg/blob/master/ports/proj/portfile.cmake)、[用法](https://github.com/microsoft/vcpkg/blob/master/ports/proj/usage)。精确版本与baseline在实际接入时冻结。
- PROJ：[Stereographic](https://proj.org/en/stable/operations/projections/stere.html)、[LCC](https://proj.org/en/stable/operations/projections/lcc.html)、[Plate Carrée](https://proj.org/en/stable/operations/projections/eqc.html)、[Mercator](https://proj.org/en/stable/operations/projections/merc.html)、[Mollweide](https://proj.org/en/stable/operations/projections/moll.html)、[Robinson](https://proj.org/en/stable/operations/projections/robin.html)。
- [USGS Mollweide球面等面积实现说明](https://isis.astrogeology.usgs.gov/8.1.0/Object/Developer/class_isis_1_1_mollweide.html)、[Robinson非等面积/非保角说明](https://isis.astrogeology.usgs.gov/10.0.0/Object/Programmer/class_isis_1_1_robinson.html)。
- [WRF WPS官方投影选择说明](https://www2.mmm.ucar.edu/wrf/site/documentation/users_guide/wps.html)、[NCL极射赤平示例](https://www.ncl.ucar.edu/Applications/polar.shtml)。
- [Unidata GEMPAK投影参数表](https://unidata.github.io/gempak/man/parm/proj.html)：现成气象绘图工具同时提供北/南极射、LCC、Mercator和等距圆柱，可作为本次菜单范围的参考，不认定本软件预设等于业务规范。
- [CF-1.8坐标与Grid Mappings](https://cfconventions.org/Data/cf-conventions/cf-conventions-1.8/cf-conventions.html#grid-mappings-and-projections)、[PROJ资源查找与proj.db](https://proj.org/en/stable/resource_files.html)、[PROJ许可证](https://github.com/OSGeo/PROJ/blob/master/COPYING)。
- [ECMWF GRIB2地球形状表](https://codes.ecmwf.int/grib/format/grib2/ctables/3/2/)仅用于Edition 2；Edition 1须使用对应规范与实际ecCodes键验证。
- Basemap：[API中resolution/area_thresh说明](https://matplotlib.org/basemap/stable/api/basemap_api.html)、[v2.0.0边界读取实现](https://github.com/matplotlib/basemap/blob/v2.0.0/src/mpl_toolkits/basemap/__init__.py)、[固定版本低精度元数据](https://raw.githubusercontent.com/matplotlib/basemap/v2.0.0/data/basemap_data/src/mpl_toolkits/basemap_data/gshhsmeta_l.dat)。
- [basemap-data 2.0.0来源与许可](https://github.com/matplotlib/basemap/blob/v2.0.0/data/basemap_data/README.md)、[数据LGPL许可](https://github.com/matplotlib/basemap/blob/v2.0.0/data/basemap_data/COPYING.LESSER)、[GSHHG层次/南极定义](https://www.soest.hawaii.edu/pwessel/gshhg/)、[源坐标及不确定性说明](https://www.soest.hawaii.edu/pwessel/gshhg/README.TXT)。
