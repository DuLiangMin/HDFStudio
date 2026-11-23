// Format-neutral read-only array contract. Values are decoded row-major doubles.
#pragma once
#include <QString>
#include <QStringList>
#include <QMap>
#include <memory>
#include <vector>
#include <cstdint>

namespace DataSource {
enum class Kind { Unknown, Hdf5, NetCdf, Grib };
enum class NodeKind { File, Group, Array, Field, Information };
enum class StoredSemantics { NativeStored, ReconstructedQuantized, Unavailable };
enum class CoordinateKind { None, Separable, PerPoint, PointList };
struct SourceRef {
  QString file_path, session_id, evidence;
  Kind kind = Kind::Unknown;
  quint64 revision = 1, size = 0;
  qint64 modified = 0;
};
struct ObjectRef {
  QString source_session_id, object_id;
  NodeKind node_kind = NodeKind::Array;
};
struct AxisInfo {
  QString name, units;
  QStringList labels;
  std::vector<double> coordinates;
  size_t length = 0;
};
struct ArrayInfo {
  ObjectRef object;
  QString title, units, grid_kind, reader, error, error_code;
  std::vector<size_t> shape;
  std::vector<AxisInfo> axes;
  bool rectangular = false, spectral = false, can_image = false, can_section = false;
  QMap<QString, QString> disabled_reasons;
};
struct ReadSelection {
  size_t row_axis = 0, col_axis = 1;
  std::vector<size_t> fixed_indices;
  bool coordinates = false, quantized = false;
};
struct CoordinateData {
  CoordinateKind kind = CoordinateKind::None;
  QString status = "not-requested", diagnostic;
  std::vector<double> latitudes, longitudes;
};
struct PlaneData {
  size_t rows = 0, cols = 0;
  std::vector<double> values, stored_x;
  CoordinateData coordinates;
  StoredSemantics stored_semantics = StoredSemantics::Unavailable;
  QString units, reader, decode_profile, error, error_code, stored_description;
  QStringList diagnostics;
  quint64 source_revision = 0;
  size_t missing_fields = 0;
  std::shared_ptr<void> lease;
  QString grid_identity, time_identity; // Current resolved field; empty means unverified.
};
}
