// Independent 2D texture entropy image, or a live image of a RasterView's masked snapshot.
#pragma once
#include <QtWidgets>
#include "layer_navigation.hpp"
#include "plane_snapshot.hpp"
#include "texture_entropy.hpp"
#include "color_bar.hpp"
#include "hdf5.h"
#include "data_types.hpp"
#include <optional>

class RasterView;

class TextureEntropyView : public QWidget, public LayerNavigation {
  Q_OBJECT
public:
  TextureEntropyView(std::string file, std::string path, std::vector<hsize_t> dims);
  TextureEntropyView(QString file,QString target,const DataSource::ArrayInfo &info);
  explicit TextureEntropyView(RasterView *source);
  ~TextureEntropyView() override;
  int layer_axis_count() const override;
  QString layer_axis_name(int axis) const override;
  int layer_count(int axis) const override;
  int layer_index(int axis) const override;
  void set_layer_index(int axis, int index) override;
  RasterView *source_view() const;
  const NcTextureEntropyPlane &entropy_plane() const { return m_entropy; }
  QImage image_for_export() const;
  QString export_png(const QString &path) const;
  QString export_csv(const QString &path) const;
signals:
  void layer_changed();
protected:
  bool eventFilter(QObject *watched, QEvent *event) override;
private:
  void setup();
  void refresh();
  void clear_result(const QString &error);
  QString source_error() const;
  std::string m_file, m_path;
  std::vector<hsize_t> m_dims;
  std::vector<QSpinBox*> m_layers;
  QStringList m_axis_names;
  bool m_following = false;
  QPointer<RasterView> m_source;
  PlaneSnapshot m_snapshot;
  std::shared_ptr<const std::vector<double>> m_decoded;
  std::optional<DataSource::ArrayInfo> m_grib_info;
  QString m_grib_file;
  class DataReadController *m_grib_reader=nullptr;
  std::shared_ptr<void> m_plane_budget;
  std::vector<size_t> m_read_indices;
  std::shared_ptr<void> m_reservation;
  std::vector<size_t> m_cached_indices;
  quint64 m_reserved_pixels = 0;
  NcTextureEntropyPlane m_entropy;
  QGraphicsView *m_canvas;
  QGraphicsPixmapItem *m_image;
  ColorBar *m_bar;
  QLabel *m_summary;
  QLineEdit *m_mask_start = nullptr, *m_mask_end = nullptr;
  QAction *m_export_png, *m_export_csv;
};
