// Snapshot analysis; requested layer playback waits for a complete source and GL frame.
#pragma once
#include "icosahedral.hpp"
#include "color_bar.hpp"
#include "layer_navigation.hpp"
#include <QtWidgets>
#include <atomic>
class RasterView;
class IcosahedralCanvas;
class Ico3DCanvas;
class IcosahedralView:public QWidget,public LayerNavigation {
  Q_OBJECT
public:
  explicit IcosahedralView(RasterView *source=nullptr,bool own_source=false);
  ~IcosahedralView()override;
  std::shared_ptr<const Icosahedral::Result> result()const{return presented;}
  static bool any_busy();
  static void cancel_all();
  QString export_data(const QString &path)const;
  QImage image_for_export()const;
  bool ready()const;
  int layer_axis_count()const override;
  QString layer_axis_name(int axis)const override;
  int layer_count(int axis)const override;
  int layer_index(int axis)const override;
  void set_layer_index(int axis,int index)override;
signals:
  void layer_changed();
  void stop_playback_requested();
public slots:
  void run_analysis();
private:
  void invalidate(const QString &reason,bool keep_frame=false);
  void launch(PlaneSnapshot snapshot,std::shared_ptr<const GeoGrid::Grid> grid,std::shared_ptr<void> grid_lease);
  void pump();
  void try_source_coordinates();
  void recolor();
  QPointer<RasterView> source;
  bool following=false,awaiting_grid=false,running=false,auto_requested=false,camera_initialized=false;
  QTimer *source_wait,*level_update;
  QString source_stamp;
  PlaneSnapshot requested;
  std::shared_ptr<const GeoGrid::Grid> waiting_grid;
  std::shared_ptr<void> waiting_grid_lease;
  std::shared_ptr<const Icosahedral::Result> presented;
  std::shared_ptr<std::atomic<quint64>> generation=std::make_shared<std::atomic<quint64>>(0);
  std::function<std::shared_ptr<Icosahedral::Result>()> pending;
  quint64 pending_revision=0;
  QSpinBox *level;
  QComboBox *mode,*palette;
  QCheckBox *wire,*reverse;
  QLabel *status;
  ColorBar *bar;
  IcosahedralCanvas *canvas;
  Ico3DCanvas *gl_canvas=nullptr;
  QStackedWidget *canvas_stack;
  QLabel *gl_status;
  QPushButton *export_nc,*export_png;
  std::vector<HdfColorMap::Palette> palettes;
};
