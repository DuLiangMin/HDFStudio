// Read-only decoded, currently masked plane. Values are row-major shape(rows,cols).
#pragma once
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <memory>
#include <vector>

struct PlaneSnapshot {
  QString file_path, variable_path;
  std::vector<size_t> leading_indices;
  size_t rows = 0, cols = 0;
  QString units = "unspecified";
  std::shared_ptr<const std::vector<double>> values;
  std::shared_ptr<void> storage_lease; // Budget remains reserved while consumers retain the plane.
  bool out_of_range = false;
  quint64 revision = 0;
  QString grid_identity, time_identity;
};

inline bool plane_units_known(const QString &units) {
  const auto value = units.trimmed().toLower();
  return !value.isEmpty() && value != "unknown" && value != "unspecified";
}

inline QString plane_pair_diagnostics(const PlaneSnapshot &anchor, const PlaneSnapshot &target) {
  QStringList messages;
  if(!plane_units_known(anchor.units) || !plane_units_known(target.units)) messages << "Units unverified";
  else if(anchor.units != target.units) messages << "Unit conflict (no conversion)";
  if(anchor.grid_identity.isEmpty() || target.grid_identity.isEmpty()) messages << "Grid correspondence unverified";
  else if(anchor.grid_identity != target.grid_identity) messages << "Grid identity conflict";
  if(anchor.time_identity.isEmpty() || target.time_identity.isEmpty()) messages << "Time correspondence unverified";
  else if(anchor.time_identity != target.time_identity) messages << "Time identity conflict";
  return messages.join("; ");
}
