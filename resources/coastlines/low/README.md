# 🌊 本地低精度海岸线

2026-11-24按用户要求拷贝到本项目。来源为Basemap **v2.0.0**、固定提交`6fed00b9b6146d19ffa98fd00a19cbb0ee448141`，精度`l`（low）。上游说明源数据为GSHHG 2.3.6，数据许可LGPL-3.0-or-later；完整原文见[UPSTREAM_README](UPSTREAM_README.md)，许可见[许可目录说明](../../../licenses/coastlines/README.md)。

## 📂 本地文件

| 文件 | 字节数 | 用途 |
| --- | --- | --- |
| `gshhs_l.dat` | 719016 | 原始低精度经纬顶点，little-endian float32[N,2] |
| `gshhsmeta_l.dat` | 562915 | 要素level、面积、点数、纬度范围、二进制offset/bytecount及ID |
| `UPSTREAM_README.md` | 1508 | 上游来源和许可说明，原字节保留 |
| [manifest.json](manifest.json) | 由内容确定 | 固定URL、提交、Git blob与SHA256、结构核查结果 |

两份数据共1281931字节（约1.22 MiB）。共10621条记录、89877个顶点；逐记录offset、字节数和全部顶点有限性/角度范围已检查。筛选level 1/5且面积>1000 km²，对应302条原始记录、31559个顶点（含E/W分片和南极人工封口），转换后为31547顶点。原包56条负面积记录均为level 2，原样保留，不参与首版海岸线筛选。

## 🔒 保存与离线使用

文件保持上游原字节，`.gitattributes`禁止数据/上游说明/许可证的换行转换。manifest中的路径相对产品仓库根。若校验失败，不能用新的hash覆盖旧manifest掩盖差异。

开发工具`tools/26NOV24_v01_fetch_coastline.py`缺省仅补缺失的固定资源；`--check`逐文件离线验证，不下载、不安装Basemap。该工具位于本地开发tools目录，现有公开源码策略会排除tools；因此来源URL、准确版本、哈希、格式说明和原始数据都随本目录保留，恢复资源不依赖私有路径。

## 🧭 接入状态

原始数据已完成本地保存与完整性核查；公开转换器`../26NOV24_v01_prepare_coastline.py`已生成`coastlines_l.json`及`derived_manifest.json`，运行时由Qt/PROJ地图视图加载，CMake按程序相对目录部署。归一化结果为302要素/302段/31547顶点；精确移除12条可追溯的成对日期线边和南极人工封口边，没有额外坐标简化。原始多边形仍完整保留。处理与验收见[ADR-0004](../../../docs/adr/0004-map-projection-display.md)。运行时不依赖网络或Python。

离线重建：在仓库根执行`python resources/coastlines/26NOV24_v01_prepare_coastline.py`；增加`--check`只在内存重建并逐字节核对派生数据/manifest。转换源码和说明随本目录部署，原始资源manifest仍保留原采集阶段字段，派生状态由独立derived manifest记录。尚未创建新发布包。
