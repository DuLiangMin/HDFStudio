#include "search.hpp"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <filesystem>
#include <vector>

namespace {
std::filesystem::path native_path(const QString &value) {
#ifdef _WIN32
  return std::filesystem::path(value.toStdWString());
#else
  return std::filesystem::u8path(value.toUtf8().constData());
#endif
}
QString qt_path(const std::filesystem::path &value) {
#ifdef _WIN32
  return QDir::fromNativeSeparators(QString::fromStdWString(value.wstring()));
#else
  return QString::fromUtf8(value.u8string().c_str());
#endif
}
QRegularExpression filename_expression(const QString &pattern) {
  return QRegularExpression(QRegularExpression::wildcardToRegularExpression(pattern),
                            QRegularExpression::CaseInsensitiveOption);
}
}

namespace Search {
bool filename_matches(const QString &name, const QString &pattern) {
  if(pattern.contains('*') || pattern.contains('?'))return filename_expression(pattern).match(name).hasMatch();
  return name.contains(pattern, Qt::CaseInsensitive);
}
bool variable_matches(const Variable &variable, const QString &query, bool exact) {
  if(query.isEmpty())return true;
  const QStringList identities{variable.name, variable.path, variable.parameter_id};
  for(const auto &value:identities)
    if(exact ? value.compare(query, Qt::CaseInsensitive)==0 : value.contains(query, Qt::CaseInsensitive))return true;
  return !exact && (variable.description.contains(query, Qt::CaseInsensitive) ||
                    variable.standard_name.contains(query, Qt::CaseInsensitive));
}
FileResults find_files(const FileQuery &query, const std::function<bool()> &cancelled,
                       const std::function<void(quint64, int)> &progress) {
  FileResults result;
  auto failure=[&](const QString &path, const QString &message) {
    ++result.errors;
    if(result.diagnostics.size()<20)result.diagnostics.append(path+": "+message);
  };
  if(query.max_results<1 || query.max_entries<1) {
    failure(query.directory, QStringLiteral("Invalid search limits"));return result;
  }
  const bool wildcard=query.pattern.contains('*') || query.pattern.contains('?');
  const auto expression=wildcard ? filename_expression(query.pattern) : QRegularExpression();
  std::vector<std::filesystem::path> directories{native_path(QFileInfo(query.directory).absoluteFilePath())};
  while(!directories.empty()) {
    if(cancelled && cancelled()){result.cancelled=true;break;}
    const auto directory=directories.back();directories.pop_back();
    std::error_code error;
    std::filesystem::directory_iterator iterator(directory, error), end;
    if(error){failure(qt_path(directory),QString::fromStdString(error.message()));continue;}
    while(iterator!=end) {
      if(cancelled && cancelled()){result.cancelled=true;return result;}
      if(result.scanned>=query.max_entries){result.truncated=true;return result;}
      const auto entry=*iterator;
      ++result.scanned;
      const auto state=entry.symlink_status(error);
      if(error)failure(qt_path(entry.path()),QString::fromStdString(error.message()));
      else if(std::filesystem::is_directory(state)) {
        if(query.recursive)directories.push_back(entry.path());
      } else if(std::filesystem::is_regular_file(state)) {
        const auto path=qt_path(entry.path());
        const QFileInfo info(path);
        const auto name=info.fileName();
        if(wildcard ? expression.match(name).hasMatch() : name.contains(query.pattern,Qt::CaseInsensitive)) {
          result.files.append({info.absoluteFilePath(),name,info.absolutePath(),info.size(),info.lastModified()});
          if(result.files.size()>=query.max_results){result.truncated=true;return result;}
        }
      }
      if(progress)progress(result.scanned,result.files.size());
      iterator.increment(error);
      if(error){failure(qt_path(directory),QString::fromStdString(error.message()));break;}
    }
  }
  return result;
}
}
