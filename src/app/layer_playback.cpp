#include "layer_playback.hpp"
#include "layer_navigation.hpp"
#include <QScopeGuard>
#include <algorithm>

LayerPlayback::LayerPlayback(QObject *parent):QObject(parent) {
  setObjectName("layerPlayback");m_timer.setInterval(500);
  connect(&m_timer,&QTimer::timeout,this,&LayerPlayback::advance_frame);
}
bool LayerPlayback::start(QWidget *view,int axis) {
  stop();auto *navigation=dynamic_cast<LayerNavigation*>(view);
  if(!navigation || axis<0 || axis>=navigation->layer_axis_count() || navigation->layer_count(axis)<2)return false;
  m_target=view;m_axis=axis;
  m_destroyed=connect(view,&QObject::destroyed,this,&LayerPlayback::stop);
  if(navigation->layer_index(axis)>=navigation->layer_count(axis)-1)navigation->set_layer_index(axis,0);
  m_timer.start();emit playing_changed(true);return true;
}
void LayerPlayback::stop() {
  const bool was_playing=m_timer.isActive();m_timer.stop();
  disconnect(m_destroyed);m_destroyed={};m_target.clear();
  if(was_playing)emit playing_changed(false);
}
bool LayerPlayback::is_playing() const {return m_timer.isActive();}
QWidget *LayerPlayback::target() const {return m_target.data();}
int LayerPlayback::axis() const {return m_axis;}
int LayerPlayback::interval_ms() const {return m_timer.interval();}
void LayerPlayback::set_interval_ms(int interval) {m_timer.setInterval(std::clamp(interval,100,10000));}
void LayerPlayback::set_ready_check(std::function<bool()> check) {m_ready_check=std::move(check);}
void LayerPlayback::advance_frame() {
  if(!is_playing() || m_advancing)return;
  auto *navigation=dynamic_cast<LayerNavigation*>(m_target.data());
  if(!navigation || m_axis<0 || m_axis>=navigation->layer_axis_count()) {stop();return;}
  const int index=navigation->layer_index(m_axis),count=navigation->layer_count(m_axis);
  if(count<2 || index>=count-1) {stop();return;}
  if(m_ready_check && !m_ready_check())return; // Wait for rendered frames; never queue skipped frames.
  m_advancing=true;auto guard=qScopeGuard([this]{m_advancing=false;});
  navigation->set_layer_index(m_axis,index+1);
  if(!m_target || index+1>=count-1)stop();
}
