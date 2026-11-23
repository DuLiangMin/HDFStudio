// Non-modal analysis dock; workspace bindings belong exclusively to the controller.
#pragma once
#include <QtWidgets>
#include "workspace_sync.hpp"
#include "residual_view.hpp"

class ComparePanel : public QDockWidget {
  Q_OBJECT
public:
  explicit ComparePanel(WorkspaceSyncController *controller,QWidget *parent=nullptr);
  void set_views(const QList<RasterView*> &views);
  QString comparison_csv() const;
  QString export_csv(const QString &path);
  void refresh_now();
private:
  RasterView *selected_view(QComboBox *combo) const;
  void select_anchor();
  void select_target();
  void apply_binding();
  void invalidate_current();
  void schedule_refresh();
  void show_residual();
  WorkspaceSyncController *m_controller;
  QList<QPointer<RasterView>> m_views;
  std::vector<QMetaObject::Connection> m_connections;
  QComboBox *m_anchor,*m_target,*m_axis;
  QSpinBox *m_lag,*m_layer;
  QSlider *m_slider;
  QCheckBox *m_sync_layer,*m_sync_viewport,*m_sync_colormap;
  QPlainTextEdit *m_metrics;
  QPushButton *m_residual_button;
  QPointer<ResidualView> m_residual;
  PlaneSnapshot m_a,m_b;
  PlaneSnapshot m_computed_a,m_computed_b;
  NcPairMetrics m_pair;
  bool m_computed=false;
  QString m_csv,m_alignment;
  bool m_refresh_pending=false,m_editing=false,m_initialized=false;
};
