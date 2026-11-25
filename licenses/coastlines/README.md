# 📜 海岸线数据许可与署名

本目录为`resources/coastlines/low/`原始数据保存上游的[GNU GPL v3全文](COPYING)及[GNU LGPL v3附加条款](COPYING.LESSER)，均逐字节拷贝。

另保留[上游LICENSE.mit](LICENSE.mit)，用于其README等非GSHHG配套内容的原许可与Jeffrey Whitaker署名；它不替代海岸线数据的LGPL标记。

- 数据来源：Basemap v2.0.0中的GSHHG 2.3.6低精度海岸数据；GSHHG由Paul Wessel、Walter H. F. Smith及相关上游贡献者维护，完整来源沿上游说明保留。
- 上游对海岸数据标注LGPL-3.0-or-later；不是Basemap Python代码的MIT许可。来源原文见[UPSTREAM_README](../../resources/coastlines/low/UPSTREAM_README.md)。
- 固定提交、下载URL、原文件大小和SHA256见[manifest](../../resources/coastlines/low/manifest.json)。两份数据和本目录许可未经修改，未复制Basemap程序实现。
- 原始数据与派生折线均已接入开发版地图及程序相对部署。派生数据的转换源码、来源与变更清单在`resources/coastlines/`随资源保留；原始完整字节未改动，不将数据重新标为项目自有资产。
- 低精度岸线用作图形定位参考，不代表源科学数据已完成基准转换、精密配准或当前岸线监测。
