// Shared read-only domain API. Arrays use source-axis row-major order; no implicit geographic flip.
#pragma once
#include <QtCore>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

namespace grib {
constexpr double missing = std::numeric_limits<double>::quiet_NaN();
struct Error : std::runtime_error {
    QString code, message;
    Error(QString type, QString detail)
        : std::runtime_error(detail.toStdString()), code(type), message(detail) {}
};
struct Field {
    int index = 0, sub_field = 0, edition = 0;
    qint64 offset = 0, length = 0;
    QString parameter, short_name, name, units, level_type, grid_type, grid_hash, packing;
    QString time_key, time_label, step_type, centre;
    long ni = 0, nj = 0, points = 0, scan = 0, bits = 0, decimal = 0, binary = 0, member = 0;
    double level = 0, reference = 0;
    bool bitmap = false, has_member = false;
    QMap<QString, QString> keys;
};
struct Axis {
     QString name;
     QStringList labels;
};
struct Cube {
    QString id, title;
    std::vector<Axis> axes;
    std::vector<int> lookup;
    int representative = 0;
};
struct Catalog {
    QString path, session, format;
    qint64 size = 0, modified = 0;
    std::vector<Field> fields;
    std::vector<Cube> cubes;
    QStringList warnings;
};
struct Plane {
    int rows = 0, cols = 0;
    bool rectangular = false, exact_stored = false;
    QString raw_description, grid_type, units;
    std::vector<double> values, stored, latitudes, longitudes;
    std::vector<size_t> source_indices;
    std::shared_ptr<void> memory_lease;
};
struct Probe {
    QString kind;
    qint64 offset = -1;
    bool valid = false;
};
Probe probe_file(const QString& path);
std::shared_ptr<Catalog> scan_file(const QString& path,
                                   const std::function<bool()>& cancelled = {});
void build_cubes(Catalog& catalog);
int resolve(const Catalog& catalog, const QString& target, const std::vector<int>& indices = {});
Plane read_plane(const Catalog& catalog, int field_index, bool coordinates = true, bool quantized = true);
std::shared_ptr<void> reserve_memory(quint64 bytes);
QString properties(const Catalog& catalog, const QString& target);
QString keys_dump(const Catalog& catalog, int field_index, const QString& name_space = {});
QJsonObject catalog_json(const Catalog& catalog);
QString api_version();
size_t mask_values(std::vector<double>& values, double low, double high);
bool parse_bound(const QString& text, bool lower, double& value);
struct Statistics {
    size_t finite = 0, missing = 0;
    double minimum = grib::missing, maximum = grib::missing, mean = grib::missing,
           stddev = grib::missing;
};
Statistics statistics(const std::vector<double>& values);
struct Histogram {
    Statistics population, sample;
    std::vector<double> edges;
    std::vector<quint64> counts;
    quint64 seed = 1;
    double fraction = 1;
};
Histogram histogram(const std::vector<double>& values, double fraction = 0.05, int bins = 32,
                    quint64 seed = 1);
QString histogram_text(const Histogram& hist);
void export_csv(const Plane& plane, const QString& target, bool stored = false);
void export_text(const Plane& plane, QIODevice& output, bool stored = false);
struct Point {
    double x = 0, y = 0;
};
struct ProfileSample {
    double distance = 0, x = 0, y = 0, value = grib::missing;
};
std::vector<ProfileSample> profile(const Plane& plane, const std::vector<Point>& controls,
                                   bool spline = true, bool bilinear = true, double spacing = 0.5);
void export_profile(const std::vector<ProfileSample>& samples, const QString& target);
bool vector_compatible(const Field& u, const Field& v);
int vector_partner(const Catalog& catalog, int u_index);
Plane vector_magnitude(const Plane& u, const Plane& v);
Point vector_direction(const Field& u, double east, double north);
QString number(double value);
} // namespace grib
