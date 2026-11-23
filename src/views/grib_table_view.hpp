// Read-only GRIB grid. Inputs: QString source/object, mode 0 decoded / 1 quantized / 2 both.
// Outputs: asynchronous model, layer navigation and atomic current-revision CSV.
#pragma once
#include <QtWidgets>
#include "layer_navigation.hpp"
#include "data_read_controller.hpp"

class GribTableView : public QWidget, public LayerNavigation {
  Q_OBJECT
public:
  GribTableView(QString file,QString target,int mode=0);
  ~GribTableView() override;
  bool ready() const {return property("ready").toBool();}
  bool read_pending() const {return m_reader && m_reader->busy();}
  QString export_csv(const QString &path,bool overwrite=false) const;
  int layer_axis_count() const override;
  QString layer_axis_name(int axis) const override;
  int layer_count(int axis) const override;
  int layer_index(int axis) const override;
  void set_layer_index(int axis,int index) override;
signals:
  void layer_changed();
private:
  void refresh();
  QString m_file,m_target;
  DataSource::ArrayInfo m_info;
  int m_mode=0;
  std::vector<QSpinBox*> m_layers;
  QLabel *m_status=nullptr;
  QAbstractTableModel *m_model=nullptr;
  DataReadController *m_reader=nullptr;
  QAction *m_export=nullptr;
  QJsonObject m_source_stamp;
  QJsonObject m_loaded_stamp;
};
