#include "section_dates.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QTimer>
#include <cmath>
#include <algorithm>
namespace {
QList<QPointer<SectionDates>> sessions;
QString cache_key(const SectionDateValue &value){return value.units+"\n"+value.calendar+"\n"+QString::number(value.value,'g',17);}
}
SectionDates::SectionDates():QObject(QCoreApplication::instance()){sessions.append(this);connect(this,&QObject::destroyed,[]{sessions.removeAll(nullptr);});}
bool SectionDates::any_busy(){for(const auto &session:sessions)if(session&&session->m_process)return true;return false;}
void SectionDates::cancel_all(){for(const auto &session:sessions)if(session)session->cancel();}
void SectionDates::submit(quint64 revision,const std::vector<SectionDateValue> &values){
  ++m_generation;m_revision=revision;m_pending=values;if(m_pending.size()>256)m_pending.resize(256);m_pending_valid=true;
  if(m_process)m_process->kill();pump();
}
void SectionDates::cancel(){++m_generation;m_pending.clear();m_pending_valid=false;if(m_process)m_process->kill();}
void SectionDates::dispose(){m_dispose=true;cancel();if(!m_process)deleteLater();}
void SectionDates::pump(){
  if(m_process||m_dispose||!m_pending_valid)return;m_pending_valid=false;m_active=std::move(m_pending);m_pending.clear();m_labels.clear();m_active_generation=m_generation;
  std::vector<SectionDateValue> needed;
  for(const auto &value:m_active){auto found=m_cache.find(cache_key(value));if(found!=m_cache.end())m_labels[value.key]=found.value();else if(std::isfinite(value.value))needed.push_back(value);}
  m_active=std::move(needed);
  if(m_active.empty()){emit labels_ready(m_revision,m_labels,true);return;}
  // Each batch has one units/calendar pair; other pairs remain numeric until requested next.
  const auto units=m_active.front().units,calendar=m_active.front().calendar;
  m_active.erase(std::remove_if(m_active.begin(),m_active.end(),[&](const auto &value){return value.units!=units||value.calendar!=calendar;}),m_active.end());
  QFile script(":/netcdf_report.py");if(!script.open(QIODevice::ReadOnly)){emit labels_ready(m_revision,m_labels,false);return;}
  QJsonArray values;for(const auto &value:m_active)values.append(value.value);
  QJsonObject request{{"schema",1},{"request_id",QString::number(m_active_generation)},{"units",units},{"calendar",calendar},{"values",values}};
  auto *process=new QProcess(this);m_process=process;m_output.clear();const auto generation=m_active_generation;
  connect(process,&QProcess::readyReadStandardOutput,this,[this,process]{const auto bytes=process->readAllStandardOutput();if(m_output.size()+bytes.size()>1024*1024){m_output.clear();process->kill();}else m_output+=bytes;});
  connect(process,&QProcess::readyReadStandardError,process,[process]{process->readAllStandardError();});
  connect(process,&QProcess::started,process,[process,request]{process->write(QJsonDocument(request).toJson(QJsonDocument::Compact));process->closeWriteChannel();});
  connect(process,&QProcess::finished,this,[this](int code,QProcess::ExitStatus status){finished(status==QProcess::NormalExit,code);});
  connect(process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart)finished(false,-1);});
  process->start(qEnvironmentVariable("HDFSTUDIO_PYTHON","python"),{"-I","-c",QString::fromUtf8(script.readAll()),"labels"});
  QPointer<QProcess> guarded=process;QTimer::singleShot(5000,this,[guarded]{if(guarded&&guarded->state()!=QProcess::NotRunning)guarded->kill();});
}
void SectionDates::finished(bool normal,int code){
  if(!m_process)return;m_output+=m_process->readAllStandardOutput();m_process->deleteLater();m_process=nullptr;
  if(m_active_generation==m_generation&&!m_dispose){
    QJsonParseError parse;const auto object=QJsonDocument::fromJson(m_output,&parse).object();const auto labels=object["labels"].toArray();
    bool valid=normal&&code==0&&m_output.size()<=1024*1024&&parse.error==QJsonParseError::NoError&&object["schema"].toInt()==1&&object["request_id"].toString()==QString::number(m_active_generation)&&object["error"].isNull()&&labels.size()==int(m_active.size());
    if(valid)for(int i=0;i<labels.size();++i){if(!labels[i].isNull()&&!labels[i].isString()){valid=false;break;}if(labels[i].isString()){m_labels[m_active[i].key]=labels[i].toString();m_cache[cache_key(m_active[i])]=labels[i].toString();}}
    while(m_cache.size()>4096)m_cache.erase(m_cache.begin());emit labels_ready(m_revision,m_labels,valid);
  }
  if(m_dispose)deleteLater();else pump();
}
