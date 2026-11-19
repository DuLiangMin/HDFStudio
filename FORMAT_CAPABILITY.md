# 🔎 格式能力矩阵

核验日期：2026-11-19。规范依据：[Unidata NetCDF-C 4.9.3 format FAQ](https://docs.unidata.ucar.edu/netcdf-c/4.9.3/faq.html#formatsdatamodelssoftwarereleases)、`nc_inq_format_extended` API，以及项目 HDF5 1.8.20 `H5Fis_hdf5`/`H5Fopen` 路径。

| 类别 | 探测/样本证据 | HDFStudio GUI 当前行为 | 能力边界 |
|---|---|---|---|
| CDF-1 / classic | `nc_inq_format_extended`: `NC_FORMATX_NC3` + mode 0; 合成文件可由 netCDF-C reader 打开 | 当前主窗口统一调用 `H5Fopen`，不支持 | 原生 NetCDF reader 能读；GUI 无 NetCDF-3 元数据树接入 |
| CDF-2 / 64-bit offset | `NC_FORMATX_NC3` + `NC_64BIT_OFFSET`; 合成文件可读 | 不支持（非 HDF5 容器） | reader 库能力不等同 GUI 能力 |
| CDF-5 / 64-bit data | `NC_FORMATX_NC3` + `NC_64BIT_DATA`; 合成文件可读 | 不支持（非 HDF5 容器） | 运行库支持且可创建的构建已验证 |
| NetCDF-4 classic model | HDF5 signature，netCDF probe reports `NC_FORMAT_NETCDF4` + `NC_FORMAT_CLASSIC_MODEL` | HDF5 结构可列出、预览走 HDF5 路径 | 不等于 netCDF-C 语义解码；CF 语义只有专用代码覆盖子集 |
| NetCDF-4 enhanced | HDF5 signature，`NC_FORMAT_NETCDF4` + `NC_FORMAT_NC4` | HDF5 group/object 树和数值预览 | HDF5 对象视图不承诺所有 netCDF enhanced 类型和语义 |
| 普通 HDF5 | HDF5 signature/`H5Fis_hdf5` true，netCDF probe fails | HDF5 对象树和支持的数据集预览 | 用户定义类型/过滤器按已有 inspector/reader 能力拒绝或有限显示 |
| NASA CDF、FITS、HDF4、remote DAP | 无 Stage 4 端到端样本或 reader 路径；工作区 FITS 文件仅是 NumPy 测试数据 | 不支持承诺 | 扩展名、反编译符号、第三方依赖存在均不作为支持证据 |

“可探测/可打开/可列对象/可读数值/语义已校验”是不同等级。NetCDF-C 的 CDF reader 能力不改变 GUI 当前基于 HDF5 的入口。普通 HDF5 user-block signature 当前只扫描前 4096 字节；超出该范围的 HDF5 user-block 文件身份可能报告 Unknown。生成的格式夹具仅在临时目录创建，不修改业务样本。

2026-11-19 实测：netCDF-C 4.9.3 `nc_open` 会把缺少 `_NCProperties` 的普通 HDF5 文件作为 enhanced NetCDF 打开；因此探测器对该路径返回 `HDF5 container (NetCDF identity unverified)`，而不是从 `nc_open` 成功推断 NetCDF-4。`_NCProperties` 是实现层证据，不作为所有 NetCDF-4 文件普遍规范判据；缺标记仅代表本工具无法认证身份。
