// Chunked, read-only HDF5 numeric text export.
#pragma once
#include <QString>
#include <QIODevice>
#include <functional>

namespace HdfTextExport {
struct Result {
  bool ok = false;
  QString error;
  quint64 valuesWritten = 0;
};
Result write(const QString &filePath, const QString &objectPath,
             QIODevice &output, size_t chunkValues = 65536,
             const std::function<bool(quint64,quint64)> &continueWriting = {});
}
