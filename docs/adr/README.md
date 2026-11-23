# 🧭 Architecture decision records / 架构决策

ADR解释重要结构、依赖、接口和资源取舍。接口细节见[模块边界](../architecture/application-boundaries.md)，源码与数据入口见[项目结构](../PROJECT_STRUCTURE.md)。内部任务与日志不随公开源码发布。

| ID | 决策 | 状态 |
| --- | --- | --- |
| [ADR-0001](0001-document-authority.md) | 分开现行契约、任务状态与历史证据 | Accepted，2026-11-23 |
| [ADR-0002](0002-application-module-boundaries.md) | 独立表格与启动编译单元，保持数据所有权 | Accepted，2026-11-23 |

## 🔄 使用与演进

一个ADR只记录一个重要决定。状态采用Proposed、Accepted、Rejected、Superseded；被替代记录保留原理由并链接后继记录。Accepted表示采用方案，不代表所有目标已经实现；实现和验证另行记录。

旧`ADR-S4-001`至`ADR-S4-007`保留在内部Stage4原始归档，属于当时设计意图；本轮不将尚未全局实现的目标自动宣称为现状。编号不复用。

后续复用[TEMPLATE](TEMPLATE.md)。仅在改变模块边界、数据语义、状态/资源所有权、依赖或发布边界时新增ADR；普通局部修改不要求ADR。

## 📚 方法来源

采用背景、备选、决定、后果和复审条件的轻量结构。本项目沿用户已有授权实施；ADR记录理由，任务卡记录实际完成情况。
