// 文件名与变量描述匹配；只访问文件系统，不读取科学数据或QtWidgets。
#pragma once
#include <QDateTime>
#include <QStringList>
#include <QVector>
#include <functional>

namespace Search {
struct FileQuery {
  QString directory, pattern;
  bool recursive = true;
  int max_results = 10000;
  quint64 max_entries = 1000000;
};
struct FileResult {
  QString path, name, directory;
  qint64 bytes = 0;
  QDateTime modified;
};
struct FileResults {
  QVector<FileResult> files;
  quint64 scanned = 0, errors = 0;
  QStringList diagnostics;
  bool cancelled = false, truncated = false;
};
struct Variable {
  QString name, path, description, standard_name, units, shape, parameter_id;
  qint64 fields = -1;
};
bool filename_matches(const QString &name, const QString &pattern);
bool variable_matches(const Variable &variable, const QString &query, bool exact);
FileResults find_files(const FileQuery &query, const std::function<bool()> &cancelled = {},
                       const std::function<void(quint64, int)> &progress = {});
}
