# 🔎 Find files and variables / 找文件与找变量

Included in 0.14.08 / 0.14.08 已包含文件与变量查找。

## 🗂️ Find files

Choose **File → Find files...**, enter a directory and a file-name query, then click **Search**. `C7F` finds names containing that text; `*C7F*` and `*.grib2` use wildcards. Matching ignores case and includes extensionless and hidden files. **Subdirectories** is enabled by default; directory links are skipped.

The result table shows file name, directory, bytes and modification time. Double-click a row or select **Open** to use the existing file reader; results remain available. **Copy path** copies the full path; **Show in folder** reveals it in the file manager. Search matches names only and does not inspect scientific content or restrict results by extension.

Queries match the base name, not the full directory path. Wildcards match the whole name, and an empty query includes all files. Entries recognized as symbolic links are skipped, including file links; not every Windows reparse-point type has been verified. Results are a scan-time list, not a live or atomic filesystem snapshot.

Scanning runs outside the GUI thread. **Stop** keeps completed matches with a partial-results notice. Results appear when scanning finishes or stops. Up to 10,000 matches and 1,000,000 directory entries are scanned; limits and access errors are reported as incomplete. Stopping waits for the current filesystem call to return, which can take longer on an unavailable network drive.

## 🧪 Find variables

Choose **File → Find variables...**. The label shows the current open file; select a different file in the browser tree to change scope. Search covers variables under collapsed groups as well as expanded ones, without loading their numeric arrays.

Scope follows the selected tree node's file root, falling back to the last open root when no node is selected; it does not follow the active image window. The table displays at most 10,000 matches and asks you to refine the query at that limit. The complete in-memory variable catalog and filtering still use the GUI thread, so this display limit is not a catalog memory or latency guarantee.

Type a name, full path or description. For GRIB, the short name, parameter name and parameter ID are searchable. **Exact name / path / ID** matches those identity fields exactly; descriptions are used only in ordinary text mode. For example, exact `t` excludes `ta`, while ordinary `temperature` lists matching descriptions.

Select a result to locate its original tree node. **Image**, **Table** and **Properties** route to the existing actions and retain their existing format/shape restrictions. GRIB results count source fields in each virtual variable; the Messages tree is not counted a second time. Multiple groups with the same short name remain separate. Hover the name for its object path.

Layer and ensemble-member selection continues in the existing view controls. Search does not infer physical equivalence, merge candidates or choose a member. Closing the file invalidates its results. No persistent index or cross-file variable scan is created. Text metadata is limited to 4096 retained bytes per supported attribute; oversized fixed strings are omitted with a note, and HDF5 scalar variable-length strings are read using HDF5's temporary allocation and truncated for the search catalog.

## 🇨🇳 操作示例

1. **File → Find files...**：选数据目录，输入 `C7F` 或 `*C7F*`，点击 **Search**；无扩展名文件也能找到。
2. 选择结果，点击 **Open**：使用原有格式识别和打开路径；文件查找结果保留。
3. **Find variables** 页签：确认顶部当前文件，输入 `ta` 并勾选 **Exact name / path / ID**。
4. 选变量结果定位文件树，再点击 **Image / Table / Properties**。场数量独立显示，后续层次/成员沿用原控件。

普通匹配不区分大小写，按文字包含；精确匹配只用于名称、路径或参数ID。未知单位显示unknown，查不到显示零条结果。访问错误、停止和数量限制均明确提示结果可能不完整。首版没有按文件内部物理量反查文件、中文物理量别名、索引数据库或自动选层。

“当前文件”跟随文件树，非活动图像窗口。变量表最多显示10000条；文本缺失或截短时，零命中不代表不存在该物理量。隐藏查找面板保留结果且扫描可继续；点击Stop才请求停止，销毁面板会请求取消。当前同一面板只运行一轮扫描，不能在Stop尚未完成时另开一轮。

## 🧱 Implementation references

Input/output locations are in [PROJECT_STRUCTURE](PROJECT_STRUCTURE.md); ownership and the choice of two scopes are recorded in [ADR-0003](adr/0003-search-boundaries.md). Implementation: `src/core/search.*`, `src/app/browser_search.*`, `src/views/search_panel.*`; targeted QtTest: `tests/search_test.cpp`, target `search_tests`. Optional read-only C7F verification uses `HDFSTUDIO_SEARCH_SAMPLE`.
