// Scalar raster preview: select leading indices, read the last two axes only.
#pragma once
#include <QtWidgets>
#include "hdf5.h"
#include "nc_reader.hpp"
#include <vector>
#include <string>
#include <map>

namespace RasterColorMap {
enum class Palette { Viridis, Plasma, Inferno, Turbo, Grayscale };
struct Range { double minimum; double maximum; bool valid; };
Range resolveRange(bool automatic, double dataMinimum, double dataMaximum,
                   double fixedMinimum, double fixedMaximum, bool hasValidValues);
QColor color(Palette palette, double fraction, bool reversed);
QColor colorValue(Palette palette, double value, double minimum, double maximum,
                  bool reversed);
QColor categoryColor(Palette palette, size_t categoryIndex, size_t categoryCount,
                     bool reversed);
QStringList categoryLabels(const std::vector<double> &values,
                           const QStringList &meanings);
QColor discreteValueColor(Palette palette, double value,
                          const std::vector<double> &categories,
                          bool reversed, const QColor &unknownColor);
}

class RasterView : public QWidget {
  Q_OBJECT
public:
  RasterView(std::string file, std::string path, std::vector<hsize_t> dims);
protected:
  bool eventFilter(QObject *watched,QEvent *event) override;
private:
  void refresh();
  std::string file, path;
  std::vector<hsize_t> dims;
  std::vector<QSpinBox*> layers;
  QGraphicsView *canvas;
  QGraphicsScene *scene;
  QLabel *legend;
  QComboBox *palette_selector;
  QCheckBox *reverse_palette;
  QComboBox *range_mode;
  QDoubleSpinBox *fixed_minimum;
  QDoubleSpinBox *fixed_maximum;
  bool discrete_mode = false;
  std::vector<double> category_values;
  QStringList category_labels;
  bool geographic = false;
  bool classic = false;   // NetCDF-3 file: every read goes through netCDF-C, HDF5 is never opened
  NcGeographicGrid geo_grid;
};
