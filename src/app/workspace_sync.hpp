// One-direction anchor broadcasts; views never know their workspace/controller.
#pragma once
#include "raster_view.hpp"
#include <QObject>
#include <QPointer>
#include <vector>

struct SyncBinding {
  QPointer<RasterView> view;
  int lag_axis = 0, lag_offset = 0;
  bool sync_layer = true, sync_viewport = true, sync_colormap = false;
};

class WorkspaceSyncController : public QObject {
  Q_OBJECT
public:
  explicit WorkspaceSyncController(QObject *parent=nullptr);
  void set_anchor(RasterView *view);
  RasterView *anchor() const;
  // Main navigation on a layer-synced follower advances its anchor instead.
  RasterView *layer_navigation_target(RasterView *active) const;
  void add_or_update_follower(const SyncBinding &binding);
  void remove_follower(RasterView *view);
  void clear();
  std::vector<SyncBinding> followers() const;
  void synchronize();
signals:
  void sync_changed();
  void metrics_updated();
private:
  struct Entry {
    SyncBinding binding;
    std::vector<QMetaObject::Connection> connections;
  };
  QPointer<RasterView> m_anchor;
  std::vector<QMetaObject::Connection> m_anchor_connections;
  std::vector<Entry> m_followers;
  bool m_applying_sync = false;
};
