#include "slice_job_controller.hpp"
#include "raster_budget.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QFile>
#include <QTimer>
#include <QStandardPaths>
#include <QThreadPool>
#include <QDateTime>

namespace {
QList<QPointer<SliceJobController>> sessions;
int running_processes=0;
}
SliceJobController::SliceJobController():QObject(QCoreApplication::instance()) {
  sessions.append(this);
  connect(this,&QObject::destroyed,[]{sessions.removeAll(nullptr);});
}
bool SliceJobController::any_busy(){for(const auto &session:sessions)if(session&&session->busy())return true;return false;}
void SliceJobController::cancel_all(){for(const auto &session:sessions)if(session)session->cancel();}
void SliceJobController::submit(QJsonObject request,quint64 bytes) {
  ++m_generation;m_pending=std::move(request);m_bytes=bytes;
  if(m_process&&m_process->state()!=QProcess::NotRunning){m_process->terminate();QPointer<QProcess> process=m_process;QTimer::singleShot(100,this,[process]{if(process&&process->state()!=QProcess::NotRunning)process->kill();});}
  pump();
}
void SliceJobController::cancel() {
  ++m_generation;m_pending={};
  if(m_process&&m_process->state()!=QProcess::NotRunning){m_process->terminate();QPointer<QProcess> process=m_process;QTimer::singleShot(100,this,[process]{if(process&&process->state()!=QProcess::NotRunning)process->kill();});}
  if(!m_running){emit drained();if(m_dispose)deleteLater();}
}
void SliceJobController::dispose(){m_dispose=true;cancel();}
void SliceJobController::pump() {
  if(m_running||m_pending.isEmpty()||m_dispose||running_processes>=2)return;
  m_lease=reserve_raster_bytes(m_bytes);
  if(!m_lease){m_pending={};emit failed("MemoryBudgetExceeded: close another image or select a smaller section");emit drained();return;}
  const QString base=QStandardPaths::writableLocation(QStandardPaths::TempLocation)+"/HDFStudio";QDir().mkpath(base);
  m_directory=std::make_shared<QTemporaryDir>(base+"/tmp-hovmoller-XXXXXX");
  if(!m_directory->isValid()){m_pending={};m_lease.reset();emit failed("Cannot create task directory");emit drained();return;}
  const auto request=m_pending;m_pending={};m_active_generation=m_generation;m_stderr.clear();
  m_expected_id=request["request_id"].toString().toULongLong();m_active_request=request;
  QFile file(m_directory->path()+"/request.json");const auto bytes=QJsonDocument(request).toJson(QJsonDocument::Compact);
  if(bytes.size()>4*1024*1024||!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()){file.close();m_lease.reset();m_directory.reset();emit failed("Cannot write task request");emit drained();return;}file.close();
  auto *process=new QProcess(this);m_process=process;m_running=true;m_reaping=false;++running_processes;
  m_timer.start();
  connect(process,&QProcess::readyReadStandardError,this,[this,process]{m_stderr+=process->readAllStandardError();if(m_stderr.size()>1024*1024)m_stderr=m_stderr.right(1024*1024);});
  connect(process,&QProcess::readyReadStandardOutput,this,[process]{process->readAllStandardOutput();});
  connect(process,&QProcess::finished,this,[this](int code,QProcess::ExitStatus status){finished(status==QProcess::NormalExit,code);});
  connect(process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart)finished(false,-1);});
  process->start(QCoreApplication::applicationDirPath()+"/hdf_cli.exe",{"slice-worker",m_directory->path()+"/request.json"});
}
void SliceJobController::finished(bool normal,int code) {
  if(m_reaping)return;m_reaping=true;--running_processes;
  if(m_process){m_process->deleteLater();m_process=nullptr;}
  if(m_active_generation!=m_generation){released();return;}
  if(!normal||code!=0) {
    QString error=QString("Slice worker failed (exit %1): %2").arg(code).arg(QString::fromUtf8(m_stderr.left(4096)));
    QFile result(m_directory->path()+"/result.json");if(result.open(QIODevice::ReadOnly)&&result.size()<=4*1024*1024){const auto object=QJsonDocument::fromJson(result.readAll()).object();error=object["error_code"].toString()+": "+object["message"].toString();}
    result.close();log_failure(error);emit failed(error);released();return;
  }
  const auto directory=m_directory;const auto lease=m_lease;const quint64 generation=m_active_generation;
  const quint64 request_id=m_expected_id;const auto request=m_active_request;const auto process_ms=m_timer.elapsed();
  QPointer<SliceJobController> self=this;
  QThreadPool::globalInstance()->start([self,directory,lease,generation,request_id,request,process_ms]{
    QElapsedTimer timer;timer.start();
    auto data=std::make_shared<SliceWorkerData>();
    try{*data=slice_load_result(directory->path(),request_id);}catch(const std::exception &exception){data->error=QString("Result load failed: %1").arg(exception.what());}
    if(data->error.isEmpty()&&(data->metadata["source_revision"]!=request["source_revision"]||((request["operation"]=="read")!=data->metadata.contains("data_file"))))data->error="ProtocolError: operation or source revision mismatch";
    auto timing=data->metadata["timing_ms"].toObject();timing["process_total"]=double(process_ms);timing["load"]=double(timer.elapsed());data->metadata["timing_ms"]=timing;
    QMetaObject::invokeMethod(QCoreApplication::instance(),[self,data,lease,generation]{if(!self)return;
      if(generation==self->m_generation){if(data->error.isEmpty())emit self->completed(data,lease);else {self->log_failure(data->error);emit self->failed(data->error);}}self->released();},Qt::QueuedConnection);
  });
  for(const auto &session:sessions)if(session&&session!=this)session->pump();
}
void SliceJobController::log_failure(const QString &message){
  const QString root=qEnvironmentVariable("HDFSTUDIO_LOG_DIR",QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/logs")+"/S4-Hovmoller";
  QDir().mkpath(root);QTemporaryDir evidence(root+"/failure-"+QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss")+"-XXXXXX");if(!evidence.isValid())return;evidence.setAutoRemove(false);
  for(const QString &name:{QString("request.json"),QString("result.json")})if(m_directory){QFile input(m_directory->path()+"/"+name);if(input.open(QIODevice::ReadOnly)){QFile output(evidence.path()+"/"+name);if(output.open(QIODevice::WriteOnly))output.write(input.read(4*1024*1024));}}
  QFile error(evidence.path()+"/stderr.txt");if(error.open(QIODevice::WriteOnly))error.write(message.toUtf8()+"\n"+m_stderr);
}
void SliceJobController::released() {
  m_running=false;m_reaping=false;m_lease.reset();m_directory.reset();
  for(const auto &session:sessions)if(session)session->pump();
  if(m_dispose){emit drained();deleteLater();return;}
  if(!busy())emit drained();
}
