// Read-only HDF5 object and attribute summaries for Properties and tests.
#pragma once
#include <QString>
#include <QStringList>
#include <vector>

namespace HdfProperties {
struct Summary {
  QString objectType;
  QString dataType;
  QString shape;
  QString storage;
  QString compression;
  QStringList attributes;
  quint64 elements = 0;
  quint64 bytes = 0;
  bool byteCountOverflow = false;
  bool attributeListTruncated = false;
  bool valid = false;
};

Summary inspect(const QString &filePath, const QString &objectPath);
QString formatAttribute(const QString &name, const QString &type,
                        const QString &shape, const QString &value,
                        int maximumCharacters = 256);
}
