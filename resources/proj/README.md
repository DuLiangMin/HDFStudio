# 🌍 PROJ依赖身份

`dependency-lock.json`记录PROJ 9.9.0 / SQLite 3.54.1、vcpkg固定baseline、MSVC x64工具集、MD/MDd和各配置DLL/数据库SHA256。

输入前缀为独立`ext/vcpkg/installed/x64-windows`；`cmake/Projection.cmake`通过PROJ_DIR局部定位，构建不改变既有Qt/netCDF工具链。数据库复制到`build/output/<配置>/resources/proj/proj.db`，匹配DLL到EXE同目录，许可到`licenses/proj/`。

运行时context只设程序相对资源路径并关闭网络。显式同模型投影不把数据库存在误当数学计算前提；`Transform::database_status`单独检查需要CRS查询的入口。应用支持范围与限制见[投影说明](../../docs/PROJECTION.md)。
