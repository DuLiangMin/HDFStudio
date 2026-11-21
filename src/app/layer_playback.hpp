// Sequential slice playback; routing and group readiness belong to the workspace host.
#pragma once
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <functional>

class LayerPlayback : public QObject {
  Q_OBJECT
public:
  explicit LayerPlayback(QObject *parent=nullptr);
  bool start(QWidget *view,int axis);
  void stop();
  bool is_playing() const;
  QWidget *target() const;
  int axis() const;
  int interval_ms() const;
  void set_interval_ms(int interval);
  void set_ready_check(std::function<bool()> check);
public slots:
  void advance_frame();
signals:
  void playing_changed(bool playing);
private:
  QPointer<QWidget> m_target;
  int m_axis=0;
  QTimer m_timer;
  QMetaObject::Connection m_destroyed;
  std::function<bool()> m_ready_check;
  bool m_advancing=false;
};
