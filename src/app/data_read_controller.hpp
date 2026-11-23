// App-owned GRIB sessions: one active + latest pending; only GRIB enters worker threads.
#pragma once
#include <QObject>
#include <QPointer>
#include <atomic>
#include "grib/grib_data.hpp"
class DataReadController:public QObject {
  Q_OBJECT
public:
  DataReadController();
  void index(const QString& file);
  void read(const QString& file,const QString& target,const std::vector<size_t>& indices,bool coordinates=false,bool quantized=false);
  void cancel();
  void dispose();
  bool busy()const{return m_running||bool(m_pending);}
  static bool any_busy();
  static void cancel_all();
signals:
  void indexed(std::shared_ptr<const grib::Catalog> catalog);
  void completed(std::shared_ptr<DataSource::PlaneData> plane,std::shared_ptr<void> budget);
  void failed(QString error);
  void drained();
private:
  struct Request {QString file,target;std::vector<size_t> indices;bool coordinates=false,quantized=false,index=false;};
  void pump();
  std::shared_ptr<Request> m_pending;
  std::shared_ptr<std::atomic_bool> m_cancel;
  quint64 m_generation=0;
  bool m_running=false,m_dispose=false;
};
