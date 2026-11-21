// Anchor -> followers synchronization with checked integer layer offsets and lifetime guards.
#include "workspace_sync.hpp"
#include <QScopeGuard>
#include <algorithm>

WorkspaceSyncController::WorkspaceSyncController(QObject *parent):QObject(parent) {}
RasterView *WorkspaceSyncController::anchor() const {return m_anchor.data();}
RasterView *WorkspaceSyncController::layer_navigation_target(RasterView *active) const {
  if(m_anchor && active)for(const auto &entry:m_followers)
    if(entry.binding.view==active && entry.binding.sync_layer)return m_anchor.data();
  return active;
}
std::vector<SyncBinding> WorkspaceSyncController::followers() const {
  std::vector<SyncBinding> result;
  for(const auto &entry:m_followers) if(entry.binding.view) result.push_back(entry.binding);
  return result;
}
void WorkspaceSyncController::set_anchor(RasterView *view) {
  if(m_anchor==view) return;
  if(!view) {clear();return;}
  for(const auto &connection:m_anchor_connections) disconnect(connection);
  m_anchor_connections.clear();
  remove_follower(view);m_anchor=view;
  m_anchor_connections.push_back(connect(view,&RasterView::plane_snapshot_changed,this,[this]{synchronize();}));
  m_anchor_connections.push_back(connect(view,&RasterView::layer_changed,this,[this]{synchronize();}));
  m_anchor_connections.push_back(connect(view,&RasterView::viewport_changed,this,[this]{synchronize();}));
  m_anchor_connections.push_back(connect(view,&RasterView::color_mapping_changed,this,[this]{synchronize();}));
  m_anchor_connections.push_back(connect(view,&QObject::destroyed,this,[this]{clear();}));
  synchronize();emit sync_changed();
}
void WorkspaceSyncController::add_or_update_follower(const SyncBinding &binding) {
  if(!binding.view || binding.view==m_anchor || !m_anchor) return;
  for(auto &entry:m_followers) if(entry.binding.view==binding.view) {
    const auto &old=entry.binding;
    if(old.lag_axis==binding.lag_axis && old.lag_offset==binding.lag_offset &&
       old.sync_layer==binding.sync_layer && old.sync_viewport==binding.sync_viewport && old.sync_colormap==binding.sync_colormap)return;
    entry.binding=binding;synchronize();emit sync_changed();return;
  }
  Entry entry;entry.binding=binding;
  auto *view=binding.view.data();
  entry.connections.push_back(connect(view,&RasterView::plane_snapshot_changed,this,[this]{if(!m_applying_sync) emit metrics_updated();}));
  entry.connections.push_back(connect(view,&RasterView::layer_changed,this,[this]{synchronize();}));
  entry.connections.push_back(connect(view,&QObject::destroyed,this,[this]{remove_follower(nullptr);}));
  m_followers.push_back(std::move(entry));synchronize();emit sync_changed();
}
void WorkspaceSyncController::remove_follower(RasterView *view) {
  for(auto it=m_followers.begin();it!=m_followers.end();) {
    if(!it->binding.view || it->binding.view==view) {
      const auto guarded=it->binding.view;
      for(const auto &connection:it->connections) disconnect(connection);
      it=m_followers.erase(it);
      if(guarded) guarded->set_sync_out_of_range(false,{});
    } else ++it;
  }
  emit sync_changed();emit metrics_updated();
}
void WorkspaceSyncController::clear() {
  for(const auto &connection:m_anchor_connections) disconnect(connection);
  m_anchor_connections.clear();m_anchor.clear();
  auto previous=std::move(m_followers);m_followers.clear();
  for(auto &entry:previous) {
    for(const auto &connection:entry.connections) disconnect(connection);
    if(entry.binding.view) entry.binding.view->set_sync_out_of_range(false,{});
  }
  emit sync_changed();emit metrics_updated();
}
void WorkspaceSyncController::synchronize() {
  if(m_applying_sync || !m_anchor) return;
  m_applying_sync=true;
  {
    auto guard=qScopeGuard([this]{m_applying_sync=false;});
    const auto viewport=m_anchor->viewport_state();
    const auto colors=m_anchor->color_state();
    for(const auto &entry:m_followers) {
      const auto binding=entry.binding;
      const auto view=binding.view;
      if(!view) continue;
      if(binding.sync_layer) {
        QString error;
        std::vector<size_t> indices;
        const int axes=m_anchor->layer_axis_count();
        if(view->layer_axis_count()!=axes) error=tr("Leading-axis counts do not match");
        else if((axes==0 && binding.lag_offset!=0) || (axes>0 && (binding.lag_axis<0 || binding.lag_axis>=axes))) error=tr("Lag axis is unavailable");
        else for(int axis=0;axis<axes;++axis) {
          const qint64 target=qint64(m_anchor->layer_index(axis))+(axis==binding.lag_axis ? qint64(binding.lag_offset) : 0);
          if(target<0 || target>=view->layer_count(axis)) {
            error=tr("axis %1 requested %2; valid indices 0..%3").arg(axis).arg(target).arg(view->layer_count(axis)-1);break;
          }
          indices.push_back(size_t(target));
        }
        if(!error.isEmpty()) view->set_sync_out_of_range(true,error);
        else {view->apply_layer_indices(indices);view->set_sync_out_of_range(false,{});}
      } else view->set_sync_out_of_range(false,{});
      if(binding.sync_colormap) {
        const auto current=view->color_state();
        if(current.palette.id!=colors.palette.id || current.palette.source_hash!=colors.palette.source_hash ||
           current.discrete!=colors.discrete || HdfColorMap::to_json(current.palette)!=HdfColorMap::to_json(colors.palette) ||
           current.mapping.minimum!=colors.mapping.minimum || current.mapping.maximum!=colors.mapping.maximum ||
           current.mapping.reversed!=colors.mapping.reversed || current.mapping.mode!=colors.mapping.mode ||
           current.mapping.normalization!=colors.mapping.normalization || current.mapping.center!=colors.mapping.center ||
           current.mapping.origin!=colors.mapping.origin || current.mapping.period!=colors.mapping.period ||
           current.mapping.boundaries!=colors.mapping.boundaries || current.mapping.custom_under!=colors.mapping.custom_under ||
           current.mapping.custom_over!=colors.mapping.custom_over || current.mapping.under!=colors.mapping.under || current.mapping.over!=colors.mapping.over)
          view->apply_color_state(colors);
      }
      if(binding.sync_viewport) view->apply_viewport_state(viewport);
    }
  }
  emit metrics_updated();
}
