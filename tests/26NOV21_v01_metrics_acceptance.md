# 🧪 Stage 4.5 分析与同步验收

## 📂 数据接口与证据

输入：`src/`、QtTest生成的临时解析夹具、经环境变量传入的只读业务文件。输出：`build/output/{Debug,Release}/`、`logs/S4-Metrics-Sync/<stage>-<timestamp>/`。不覆盖 `S4-FullRegression-20261121/`，不改业务文件。

## 🔬 分步闸门

| 阶段 | 必须检查 | 进度 |
|---|---|---|
| P1a | 熵常量0、256级均匀8bits、缺测、固定范围、非法参数；双配置CLI/NumPy | 已通过，`entropy-20261121-183344` |
| P1b | r正负1、已知误差、有限交集、shape、常量r无效、大均值；PRECT双层/非标双文件 | 已通过，`pair-20261121-183703` |
| P2 | Histogram/Raster完整平面熵；换层、全缺测N/A、换色不读盘、不修改旧快照 | 已通过，`gui-20261121-183820` |
| P3 | A[3]+2→B[5]、A[8]+2越界、A[5]+2→B[7]；负lag、多目标、视口、色表可选、防重入、关闭 | 双配置通过，`sync-20261121-184252` |
| P4a | 残差算法逐值正确，缺测/shape/全零/有限减法溢出 | 双配置通过，`residual-20261121-184624` |
| P4b | Dock实时r/RMSE/两图熵/对齐、残差图、复制/原子CSV、多Follower、关闭 | 双配置专项通过，`panel-20261121-185104`、`panel-20261121-190006`；最终完整QtTest已通过 |
| 最终 | 双配置完整QtTest/CTest、独立进程时序重复、真实鼠标和截图、README中英文 | 全部通过：clean构建、两配置各75 passed/0 failed/0 skipped、CTest各1/1、时序各3次，`final-20261121-190415`；桌面各16/16，README已同步 |

## 📏 科学边界

- 输入double已解码并应用源缺测/用户Mask，双图只用有限交集，不插值、不裁剪。
- lag是前置轴零基整数索引步长，未执行时间坐标匹配或物理时长换算。
- 残差和Bias方向为Target−Anchor；单位未知/不同时保留真实标签，不做换算。
- 数值对账预设容差 `1e-12*max(1,abs(reference))`；计数与shape须精确相等。

## 🖱️ 桌面断言

`26NOV21_v01_metrics_desktop.ps1` 两配置顺序执行：真实鼠标打开两个图像→Window/Compare & Metrics→相同图r=1/RMSE=0→lag+2与Anchor[3]/Target[5]→残差视窗→复制→lag10000越界/N/A→恢复→键盘操作已聚焦的滑块联动→保存CSV→源SHA256不变→正常退出。失败保留整屏截图、UIA控件列表、进程与Qt插件日志。

## 🌡️ 用户指定真实预报

- 两份 `tmp2m_f*.nc`：480层/180层，`TMP_2maboveground`，K，190×384；经纬坐标逐值相同，时间间隔21600秒。长序列从1999-01-01 06:00开始，短序列从1999-01-01 12:00开始；共有180个相同有效时刻。
- 同时刻lag=-1：Anchor[1]/Target[0]、[91]/[90]、[180]/[179]；另加同索引[0]/[0]及lag+2的[3]/[5]。双配置10组r/RMSE/MAE/Bias/两图熵全部与NumPy匹配，初验 `forecast-20261121-185849`，数值修正后复验 `forecast-20261121-191046`。
- `forecastPairResidualMatchesEveryDecodedPixel` 读取实际GUI快照，经控制器lag=-1，导出3组残差double CSV和RGB PNG；独立脚本按目标减锚点、对称范围/离线色表逐值逐像素核验。
- ✅ `forecast-gui-20261121-190257` 双配置通过，独立脚本6组共437760个double及437760个RGB像素全部精确匹配；180层Target越界清图验证通过。
- ✅ 最终clean构建导出在 `final-20261121-190415/forecast-residual-reconciliation.json` 再次完成6组全值/全像素独立对账；该轮全部源码、6个二进制和6份业务样本的哈希读回一致。
- 桌面脚本可设 `HDFSTUDIO_COMPARE_TARGET` 打开第二个真实文件，通过环境变量给定预先数值对账的r/RMSE参考前缀；额外验证同有效时刻首组。

## 🔎 边界发现记录

`precision-review/actual.txt` 复现极大背景吞小误差；改按差值最大绝对值缩放后，新增 `RMSE/(sqrt(.75)*1e-100)` 等相对比值断言。修正后双配置P1至P4专项全通过，证据 `panel-20261121-190006`。首次final主动中止，`final-20261121-185529`保留但不计PASS。

首次桌面 `desktop-Debug-20261121-191447` 的指标/lag/残差/越界恢复/共同时间/滑块全部通过，但原生保存框路径被SendKeys逐字输入截断；保留失败截图、UIA控件和结果，不计整轮PASS。后续 `desktop-Debug-20261121-191649`、`desktop-Debug-20261121-191830` 已核对完整路径，但递归AutomationId=1误命中目录项cmake，仍未保存。最终脚本等待保存框就绪、剪贴板粘贴/完整路径核对，以GetDlgItem(dialogHwnd,1)定位直接子保存按钮后真实鼠标点击；每轮失败记录保留。

## ✅ 最终桌面通过

- Debug `desktop-Debug-20261121-192018`、Release `desktop-Release-20261121-192047`：各16/16，实际CSV保存、两份样本哈希不变和正常退出均通过。
- 最终截图已查看；保存后的CSV再次独立读取，Anchor[2]/Target[2]、lag=0、K、72960配对的r/RMSE/MAE/Bias与netCDF4/NumPy匹配，汇总 `final-20261121-190415/desktop-verified.json`。
- 失败为桌面脚本输入/定位问题，修正期间产品源码未变；双配置完整回归的源码与二进制哈希仍对应最终产品。
