// Offline palette contract: sRGB RGBA nodes; values always stay in decoded units.
#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <array>
#include <cstdint>
#include <vector>

namespace HdfColorMap {
using Rgba = std::array<double, 4>;
struct Palette {
  QString id, name, group, kind, source, license, revision, source_hash, modification;
  QStringList authors;
  std::vector<Rgba> colors;
  std::vector<double> positions;
  bool stepped = false;
};
enum class RgbScale { Auto, Unit, Byte };
enum class Mode { Continuous, Stepped };
enum class Normalization { Linear, Centered, Cyclic };
struct Mapping {
  Mode mode = Mode::Continuous;
  Normalization normalization = Normalization::Linear;
  bool reversed = false;
  double minimum = 0, maximum = 1, center = 0, origin = 0, period = 360;
  std::vector<double> boundaries;
  bool custom_under = false, custom_over = false;
  uint32_t under = 0xff000000u, over = 0xffffffffu;
};
constexpr uint32_t missing_color = 0xff6e6e6eu;
constexpr uint32_t masked_color = 0xffffffffu;
QString validate(const Palette &palette);
QString validate(const Palette &palette, const Mapping &mapping);
QString parse_json(const QByteArray &data, Palette &out);
QByteArray to_json(const Palette &palette);
QString parse_rgb(const QByteArray &data, RgbScale scale, Palette &out);
std::vector<Palette> load_registry(QString &error);
QString import_rgb(const QString &filename, RgbScale scale, Palette &out);
double fraction(double value, const Mapping &mapping);
double value_at(double fraction, const Mapping &mapping);
uint32_t sample(const Palette &palette, double fraction, bool reversed = false, bool stepped = false);
uint32_t map_value(const Palette &palette, const Mapping &mapping, double value, bool masked = false);
}
