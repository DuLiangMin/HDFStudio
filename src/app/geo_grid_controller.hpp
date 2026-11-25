// CLI lifecycle controller: bounded protocol, active/latest and application-owned reaping.
#pragma once
#include "geo_grid.hpp"
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTemporaryDir>
class GeoGridController:public QObject {
  Q_OBJECT
public:
  GeoGridController();
  void read(const QString &file,const QString &variable,int rows,int cols);
  void cancel();void dispose();
  bool busy()const{return running||!pending.isEmpty();}
  static bool any_busy();static void cancel_all();
signals:
  void completed(std::shared_ptr<const GeoGrid::Grid> grid,std::shared_ptr<void> lease);
  void failed(const QString &error);
private:
  void pump();void finished(bool normal,int code);
  quint64 generation=0,active=0;
  bool running=false,disposing=false,reaping=false;
  QJsonObject pending,request;
  QPointer<QProcess> process;
  std::shared_ptr<QTemporaryDir> directory;
  std::shared_ptr<void> reservation;
};
