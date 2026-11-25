// Map viewport consumes immutable source pixels/values; it never exports scientific data.
#pragma once
#include "map_render.hpp"
#include <QWidget>
#include <QTimer>
#include <QPointer>
#include <atomic>
#include <functional>
#include <optional>
class QLabel;
class MapDisplay:public QWidget {
  Q_OBJECT
public:
  explicit MapDisplay(QWidget *parent=nullptr);
  ~MapDisplay()override;
  void set_grid(std::shared_ptr<const GeoGrid::Grid> grid,std::shared_ptr<void> lease);
  void set_source(QImage source,std::shared_ptr<const std::vector<double>> values,
                  std::shared_ptr<const std::vector<double>> original,std::shared_ptr<void> lease);
  void invalidate(const QString &reason);
  void hold_frame(const QString &message);
  void show_settings();
  MapProjection::Mode mode()const{return spec.mode;}
  QString select_projection(MapProjection::Mode mode);
  bool has_saved_preferences()const{return saved_preferences.has_value();}
  MapProjection::Mode saved_mode()const{return saved_preferences?saved_preferences->projection.mode:MapProjection::Mode::Source;}
  QString restore_preferences();
  void fit_view();void zoom(double factor);
  bool active()const{return spec.mode!=MapProjection::Mode::Source;}
  bool busy()const{return running||bool(pending)||timer.isActive();}
  static bool any_busy();
  static void cancel_all();
  std::shared_ptr<const MapRender::Output> result()const{return presented;}
signals:
  void mode_changed(bool map);
  void status_changed(const QString &message);
  void preferences_error(const QString &message);
  void frame_ready();
protected:
  void paintEvent(QPaintEvent*)override;void resizeEvent(QResizeEvent*)override;
  void wheelEvent(QWheelEvent*)override;void mousePressEvent(QMouseEvent*)override;
  void mouseMoveEvent(QMouseEvent*)override;void mouseReleaseEvent(QMouseEvent*)override;
private:
  void schedule(bool fit=false);void launch();void pump();void set_status(const QString &text);
  QString apply_projection(const MapProjection::Spec &proposed);
  void load_preferences();
  QString save_preferences();
  struct Preferences {
    MapProjection::Spec projection;
    bool coast_enabled=true;
    QColor coast_color=QColor("#303030");
    double coast_width=.8;
  };
  std::optional<Preferences> saved_preferences;
  QString settings_file;
  MapProjection::Spec spec;
  MapProjection::Viewport viewport;
  std::shared_ptr<const GeoGrid::Grid> grid;
  std::shared_ptr<void> grid_lease,source_lease,coast_lease,geometry_lease;
  std::shared_ptr<const std::vector<double>> values,original;
  QImage source;
  std::shared_ptr<const Coastline::Data> coastline;
  // painted retains only one completed frame while presented/probes await the new generation.
  std::shared_ptr<const MapRender::Output> presented,painted;
  MapProjection::Spec painted_spec;
  bool painted_model_known=false;
  std::shared_ptr<const MapRender::Geometry> geometry;
  QString geometry_key,status="Choose a projection";
  std::shared_ptr<std::atomic<quint64>> generation=std::make_shared<std::atomic<quint64>>(0);
  std::function<std::shared_ptr<MapRender::Output>()> pending;
  quint64 pending_revision=0;
  QTimer timer;
  bool running=false,needs_fit=true,coast_enabled=true,dragging=false;
  QColor coast_color=QColor("#303030");double coast_width=.8;
  QPoint drag_start;MapProjection::Viewport drag_viewport;
  QPointer<QWidget> settings_dialog;
};
