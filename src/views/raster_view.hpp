// Scalar raster preview: select leading indices, read the last two axes only.
#pragma once
#include <QtWidgets>
#include "hdf5.h"
#include "nc_reader.hpp"
#include "layer_navigation.hpp"
#include "curve_profile.hpp"
#include <QPointer>
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

class RasterView : public QWidget, public LayerNavigation {
  Q_OBJECT
public:
  RasterView(std::string file, std::string path, std::vector<hsize_t> dims);
  // LayerNavigation: the leading-axis spin boxes are the selection; the main Layer toolbar drives them
  int layer_axis_count() const override;
  QString layer_axis_name(int axis) const override;
  int layer_count(int axis) const override;
  int layer_index(int axis) const override;
  void set_layer_index(int axis, int index) override;
signals:
  void layer_changed();
protected:
  bool eventFilter(QObject *watched,QEvent *event) override;
private:
  void refresh();
  void redraw_profile_overlay();
  void show_profile_window();
  std::string file, path;
  std::vector<hsize_t> dims;
  std::vector<QSpinBox*> layers;
  QStringList axis_names;   // names of the leading dimensions, e.g. time, lev
  QGraphicsView *canvas;
  QGraphicsScene *scene;
  QLabel *legend;
  QComboBox *palette_selector;
  QCheckBox *reverse_palette;
  QComboBox *range_mode;
  QDoubleSpinBox *fixed_minimum;
  QDoubleSpinBox *fixed_maximum;
  // Mask Range in decoded units (stored x scale_factor + add_offset): values in [start, end] are
  // masked (white); an empty field is open (-inf / +inf); both empty = no mask
  QLineEdit *mask_start;
  QLineEdit *mask_end;
  QAction *flip_v_action = nullptr;
  QAction *flip_h_action = nullptr;
  QAction *profile_mode_action = nullptr;
  QAction *show_profile_action = nullptr;
  QAction *clear_profile_action = nullptr;
  std::vector<QPointF> profile_points;
  int dragging_point_index = -1;
  std::vector<double> last_values;
  size_t last_rows = 0;
  size_t last_cols = 0;
  QPointer<QWidget> profile_window;
  bool discrete_mode = false;
  std::vector<double> category_values;
  QStringList category_labels;
  bool geographic = false;
  bool classic = false;   // NetCDF-3 file: every read goes through netCDF-C, HDF5 is never opened
  NcGeographicGrid geo_grid;
};
