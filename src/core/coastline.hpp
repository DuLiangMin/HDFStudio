// Immutable normalized coastline; failure disables only the reference overlay.
#pragma once
#include <QPointF>
#include <QString>
#include <vector>
#include <memory>
namespace Coastline {
struct Data {std::vector<std::vector<QPointF>> parts;QString revision,error;std::shared_ptr<void> storage_lease;};
Data load(const QString &filename,const QByteArray &expected_hash);
QByteArray bundled_hash();
}
