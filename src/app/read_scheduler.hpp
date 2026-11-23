// GUI-thread admission shared by ordinary GRIB tasks and CLI section processes.
#pragma once
#include <QObject>
class ReadScheduler:public QObject {
  Q_OBJECT
public:
  static ReadScheduler* instance();
  bool acquire();
  void release();
signals:
  void available();
private:
  int m_active=0;
};
