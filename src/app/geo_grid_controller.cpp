#include "geo_grid_controller.hpp"
#include "read_scheduler.hpp"
#include "raster_budget.hpp"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QTimer>
#include <QThreadPool>
namespace {QList<QPointer<GeoGridController>> sessions;}
GeoGridController::GeoGridController():QObject(QCoreApplication::instance()){sessions.append(this);connect(ReadScheduler::instance(),&ReadScheduler::available,this,&GeoGridController::pump);connect(this,&QObject::destroyed,[]{sessions.removeAll(nullptr);});}
bool GeoGridController::any_busy(){for(const auto &session:sessions)if(session&&session->busy())return true;return false;}
void GeoGridController::cancel_all(){for(const auto &session:sessions)if(session)session->cancel();}
void GeoGridController::cancel(){++generation;pending={};if(process&&process->state()!=QProcess::NotRunning){process->terminate();const auto guard=process;QTimer::singleShot(100,this,[guard]{if(guard&&guard->state()!=QProcess::NotRunning)guard->kill();});}if(!running&&disposing)deleteLater();}
void GeoGridController::dispose(){disposing=true;cancel();}
void GeoGridController::read(const QString &file,const QString &variable,int rows,int cols){cancel();pending={{"schema",1},{"request_id",QString::number(generation)},{"file",file},{"variable",variable},{"source_stamp",GeoGrid::stamp(file)},{"expected_shape",QJsonArray{rows,cols}}};pump();}
void GeoGridController::pump(){if(running||pending.isEmpty()||disposing)return;if(!ReadScheduler::instance()->acquire())return;
  // Covers result JSON, parser/DTO copies, decoded bounds/axes and IPC buffers.
  reservation=reserve_raster_bytes(192ull*1024*1024);if(!reservation){ReadScheduler::instance()->release();pending={};emit failed("Coordinate memory budget exceeded");return;}
  directory=std::make_shared<QTemporaryDir>();request=pending;pending={};active=generation;
  QFile file(directory->path()+"/request.json");const auto bytes=QJsonDocument(request).toJson(QJsonDocument::Compact);
  if(!directory->isValid()||!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()){ReadScheduler::instance()->release();reservation.reset();directory.reset();emit failed("Cannot write coordinate request");return;}file.close();
  running=true;reaping=false;auto *child=new QProcess(this);process=child;
  connect(child,&QProcess::readyReadStandardError,this,[child]{child->readAllStandardError();});connect(child,&QProcess::readyReadStandardOutput,this,[child]{child->readAllStandardOutput();});
  connect(child,&QProcess::finished,this,[this](int code,QProcess::ExitStatus status){finished(status==QProcess::NormalExit,code);});
  connect(child,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart)finished(false,-1);});
  child->start(QCoreApplication::applicationDirPath()+"/hdf_cli.exe",{"geo-grid-worker",directory->path()+"/request.json"});
}
void GeoGridController::finished(bool normal,int code){if(reaping)return;reaping=true;ReadScheduler::instance()->release();if(process){process->deleteLater();process=nullptr;}
  const auto finish=[this]{running=false;reaping=false;reservation.reset();directory.reset();if(disposing){deleteLater();return;}pump();};
  if(active!=generation){finish();return;}
  if(!normal||code){emit failed(QString("Coordinate worker failed: %1").arg(code));finish();return;}
  const auto task_directory=directory;const auto lease=reservation;const auto expected=request;const auto revision=active;const QPointer<GeoGridController> self(this);
  QThreadPool::globalInstance()->start([self,task_directory,lease,expected,revision]{auto grid=std::make_shared<GeoGrid::Grid>();QFile result(task_directory->path()+"/grid.json");
    if(!result.open(QIODevice::ReadOnly)||result.size()>64*1024*1024)grid->error="Coordinate result missing/oversized";
    else {const auto root=QJsonDocument::fromJson(result.readAll()).object();for(const auto &key:{"schema","request_id","source_stamp","file","variable","expected_shape"})if(root[key]!=expected[key])grid->error="Coordinate protocol identity mismatch";
      if(grid->error.isEmpty())*grid=GeoGrid::from_json(root["grid"].toObject());const auto shape=expected["expected_shape"].toArray();if(grid->error.isEmpty()&&(grid->latitude.centers.size()!=size_t(shape[0].toInt())||grid->longitude.centers.size()!=size_t(shape[1].toInt())))grid->error="Coordinate result shape mismatch";
      if(GeoGrid::stamp(expected["file"].toString())!=expected["source_stamp"].toString())grid->error="SourceChanged after coordinate read";}
    QMetaObject::invokeMethod(QCoreApplication::instance(),[self,grid,lease,revision]{if(!self)return;self->running=false;self->reaping=false;self->reservation.reset();self->directory.reset();
      if(revision==self->generation){if(grid->error.isEmpty())emit self->completed(grid,lease);else emit self->failed(grid->error);}if(self->disposing)self->deleteLater();else self->pump();},Qt::QueuedConnection);
  });
}
