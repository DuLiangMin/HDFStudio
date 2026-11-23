// Scalar raster preview: select leading indices, read the last two axes only.
#pragma once
#include <QtWidgets>
#include "hdf5.h"
#include "nc_reader.hpp"
#include "layer_navigation.hpp"
#include "curve_profile.hpp"
#include "color_map.hpp"
#include "color_bar.hpp"
#include "plane_snapshot.hpp"
#include "viewport_state.hpp"
#include "data_types.hpp"
#include <QPointer>
#include <memory>
#include <atomic>
#include <functional>
#include <vector>
#include <string>
#include <map>
#include <optional>

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

struct RasterColorState {
  HdfColorMap::Palette palette;
  HdfColorMap::Mapping mapping;
  bool discrete = false;
};

class RasterView : public QWidget, public LayerNavigation {
  Q_OBJECT
public:
  RasterView(std::string file, std::string path, std::vector<hsize_t> dims);
  // GRIB source already indexed by the browser; all reads retain PlaneData and budget leases.
  RasterView(QString file,QString target,const DataSource::ArrayInfo &info);
  ~RasterView() override;
  // LayerNavigation: the leading-axis spin boxes are the selection; the main Layer toolbar drives them
  int layer_axis_count() const override;
  QString layer_axis_name(int axis) const override;
  int layer_count(int axis) const override;
  int layer_index(int axis) const override;
  void set_layer_index(int axis, int index) override;
  // Used by the native dialog and deterministic GUI tests; imports are user-local.
  QString import_color_map(const QString &filename,HdfColorMap::RgbScale scale);
  // Complete data-resolution raster, with current Mask/mapping/layer and flips; no UI chrome.
  QImage image_for_export();
  PlaneSnapshot plane_snapshot() const;
  bool render_pending() const;
  ViewportState viewport_state() const;
  void apply_viewport_state(const ViewportState &state);
  RasterColorState color_state() const;
  void apply_color_state(const RasterColorState &state);
  void set_sync_out_of_range(bool out_of_range, const QString &message);
  bool apply_layer_indices(const std::vector<size_t> &indices);
signals:
  void layer_changed();
  void plane_snapshot_changed(quint64 revision);
  void viewport_changed();
  void color_mapping_changed();
protected:
  bool eventFilter(QObject *watched,QEvent *event) override;
private:
  RasterView(std::string file,std::string path,std::vector<hsize_t> dims,
             std::optional<DataSource::ArrayInfo> grib_info);
  void refresh();
  void redraw_profile_overlay();
  void show_profile_window();
  void fill_palette_selector();
  void choose_color_mapping();
  void choose_rgb_file();
  void clear_plane(const QString &error);
  void start_pending_render();
  void notify_viewport_changed();
  std::string file, path;
  std::optional<DataSource::ArrayInfo> grib_info;
  QString grib_file;
  class DataReadController *grib_reader=nullptr;
  std::shared_ptr<const DataSource::PlaneData> grib_plane;
  bool grib_read_pending=false,grib_first_fit=true;
  std::vector<size_t> grib_requested_indices;
  std::vector<hsize_t> dims;
  std::vector<QSpinBox*> layers;
  QStringList axis_names;   // names of the leading dimensions, e.g. time, lev
  QGraphicsView *canvas;
  QGraphicsScene *scene;
  QGraphicsPixmapItem *image_item = nullptr;
  QLabel *legend;
  QLabel *entropy_summary;
  bool snapshot_available = false;
  quint64 snapshot_revision = 0;
  bool sync_out_of_range = false;
  QString sync_range_message;
  bool applying_viewport = false;
  std::optional<std::pair<double,double>> synced_color_range;
  ColorBar *color_bar;
  std::vector<HdfColorMap::Palette> color_maps;
  HdfColorMap::Mapping color_mapping;
  quint64 render_revision = 0;
  std::shared_ptr<std::atomic<quint64>> render_generation = std::make_shared<std::atomic<quint64>>(0);
  std::function<QImage()> pending_render;
  std::function<void(const QImage &)> pending_apply;
  quint64 pending_revision = 0;
  bool render_running = false;
  bool display_valid = false;
  bool reading_valid = false;
  std::vector<size_t> cached_indices;
  QString cached_mask_start, cached_mask_end, cached_units, cached_meanings;
  std::vector<double> declared_categories;
  std::shared_ptr<const std::vector<double>> decoded_values;
  std::shared_ptr<void> raster_reservation;
  bool mask_valid = false;
  size_t masked_count = 0;
  double data_low = 0, data_high = 0;
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
  std::shared_ptr<const std::vector<double>> last_values;
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
