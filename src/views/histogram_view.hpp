// Histogram of one layer of a variable: the plane is read once, then sampled at random on every
// setting change, so a 1.7 M cell plane stays responsive.
#pragma once
#include <QtWidgets>
#include "hdf5.h"
#include "nc_reader.hpp"
#include "plane_reader.hpp"
#include "image_metrics.hpp"
#include "layer_navigation.hpp"
#include "data_types.hpp"
#include <optional>
#include <vector>
#include <string>

// Bars of one NcHistogram; a separate widget so a test can grab it and read its properties.
class HistogramCanvas : public QWidget
{
  Q_OBJECT
public:
  explicit HistogramCanvas(QWidget *parent = nullptr);
  void set_histogram(const NcHistogram &histogram);
  const NcHistogram &histogram() const { return m_histogram; }
protected:
  void paintEvent(QPaintEvent *event) override;
private:
  NcHistogram m_histogram;
};

class HistogramView : public QWidget, public LayerNavigation
{
  Q_OBJECT
public:
  HistogramView(std::string file, std::string path, std::vector<hsize_t> dims);
  HistogramView(QString file,QString target,const DataSource::ArrayInfo &info);
  ~HistogramView() override;
  // LayerNavigation: the leading-axis spin boxes pick the plane; the main Layer toolbar drives them
  int layer_axis_count() const override;
  QString layer_axis_name(int axis) const override;
  int layer_count(int axis) const override;
  int layer_index(int axis) const override;
  void set_layer_index(int axis, int index) override;
signals:
  void layer_changed();
private:
  HistogramView(std::string file,std::string path,std::vector<hsize_t> dims,
                std::optional<DataSource::ArrayInfo> info);
  void clear_plane(const QString &error);
  void read_plane();      // read the selected plane once and keep its decoded values
  void update_histogram(); // sample the kept values and redraw, after a setting change
  double keep_fraction() const;

  std::string m_file, m_path;
  std::vector<hsize_t> m_dims;
  std::vector<QSpinBox *> m_layers;
  QStringList m_axis_names;
  std::shared_ptr<const std::vector<double>> m_values; // decoded values retained with the plane lease
  std::optional<DataSource::ArrayInfo> m_grib_info;
  QString m_grib_file;
  class DataReadController *m_grib_reader=nullptr;
  std::shared_ptr<void> m_budget;
  NcGrayEntropy m_entropy;
  HistogramCanvas *m_canvas;
  QLabel *m_summary;
  QSpinBox *m_every_nth;           // initial sampling: 1 in 20 (0.05)
  QSpinBox *m_bins;
  quint32 m_seed = 1;
};
