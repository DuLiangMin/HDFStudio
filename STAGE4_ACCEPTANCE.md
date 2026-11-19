# 📋 Stage 4 本地验收台账

## 🧩 S4-R08：UGRID

- 规范：[UGRID v1.0](https://ugrid-conventions.github.io/ugrid-conventions/)，2026-11-19 核对 2D triangular topology、face data、零/一起始索引章节。只实现同组引用、显式 CF 经纬度、face-first 定宽三节点和一维 `location=face` 数值。经纬度作为平面坐标，不是投影图；不解释旋转坐标、跨经度接缝、混合面、边/节点数据、分层或 3D。
- 旧版 `inventory/v2022d/commands.json:36` 只证实 `View mesh...` 菜单存在，不能证明输入类型、命令路由或输出。扫描工作区 `.nc/.nc4`（含第三方测试数据，原扫描包含重复路径）：251 次打开尝试，无拓扑/连接标记命中，4 次异常均为 netCDF-C 自带测试样本。没有业务 UGRID 样本；超出子集及旧版行为复原暂缓，需可分享的样例与可核对的输入/输出。
- `tests/run-stage4.ps1 S4-R08`：VS 2022 x64 Debug/Release 各 21 项 QtTest 通过、CTest 1/1；合成夹具检查零/一起始、索引和值、填充/越界、四边形、数组型索引属性、错误 location 和 Qt 场景构造。报告在忽略的 `logs/S4-R08/`。真实业务 mesh/旧版对照未验证。

## 🔎 S4-R09：格式能力

- 规范：[Unidata netCDF-C 4.9.3 FAQ](https://docs.unidata.ucar.edu/netcdf-c/4.9.3/faq.html#formatsdatamodelssoftwarereleases) 的 formats / format identification。合成文件验证 CDF-1/2/5、NetCDF-4 classic/enhanced，netCDF-C 4.9.3 与 HDF5 1.8.20 的打开路径差异；确切结果见 `FORMAT_CAPABILITY.md`。
- 普通空 HDF5 可被该 netCDF-C 构建打开且报告 enhanced，因此探测器要求 `_NCProperties` 作为本产品认证标记；标记缺失只说“identity unverified”，不宣称文件不是 NetCDF。CDF-1/2/5 可由 netCDF-C reader 打开，但 GUI `H5Fopen` 路径拒绝，故 GUI 仍不支持 NetCDF-3。
- 工作区找到一个 FITS 文件 `packages/test-env/.../recarray_from_file.fits`，归属 NumPy 测试数据，无 FITS reader / 用户任务证据；不扩展 FITS。无其它 CDF/FITS/HDF4 reader 路径，不根据旧版函数名作支持声明。

## 🧾 S4-R10：CF-1.8 诊断

- 规则子集：根 `Conventions` 声明；递归列 group 的变量；same-group `bounds`/`climatology`/`coordinates` 和简式 `grid_mapping` 引用存在性；`_FillValue` 标量且外部类型与变量一致。引用解析不检查 ancestor/external/lateral 规则，因此缺失本组名称发 warning，不冒充标准违规。`cf_summary` 与 Help 报告均明确“partial/advisory / not a conformance certification”。数据宽检查、UDUNITS、standard-name 表、cell methods、calendar、groups 继承解析、recommendations 均未覆盖。
- 规范：[CF-1.8](https://cfconventions.org/Data/cf-conventions/cf-conventions-1.8/cf-conventions.html)，requirements summary [CF-1.8 conformance](https://cfconventions.org/Data/cf-documents/requirements-recommendations/conformance-1.8.html)，2026-11-19 核对。独立 checker 是 IOOS Compliance Checker 6.1.0 `cf:1.8`，运行 `C:\Python312\python.exe C:\Python312\Scripts\cchecker.py -t cf:1.8 -c normal -f text -o <report> <fixture>`。其 valid fixture 仍报告 title/history recommendations；invalid fixture 报告非法 units、standard_name、axis、missing coordinates。本地规则只对 coordinates 引用有重叠，明确保留其余差异为未覆盖，不以 checker 总分等同本地结果。
- checker 对照：valid synthetic file 由 IOOS Compliance Checker 6.1.0 `cf:1.8` 报告 All tests passed；invalid file 报告非法 UDUNITS、standard_name、axis 和 coordinates 引用。本地子集命中 coordinates 引用问题，但不覆盖前三条；符合已声明范围。checker 的 valid exit=0、invalid exit=1。文件和原始输出在忽略的 `logs/S4-R10/`；Qt 合成 fixture 在 `tests/drop_targets_test.cpp`，不修改业务样本。
- 最终验证：VS 2022 x64 Debug/Release 各 QtTest 24/24、CTest 1/1；包含打开只读业务 NetCDF 后触发报告 dialog 并核对覆盖边界。报告 `logs/S4-R09-R10/test-{Debug,Release}.txt`，Qt/plugin/process 日志 `process-{Debug,Release}.txt`。未做人工鼠标验收。
