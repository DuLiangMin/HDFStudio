#include "data_read_controller.hpp"
#include "read_scheduler.hpp"
#include "raster_budget.hpp"
#include <QCoreApplication>
#include <QThreadPool>
#include <QTimer>
namespace {QList<QPointer<DataReadController>> sessions;}
DataReadController::DataReadController():QObject(QCoreApplication::instance()) {
  sessions.append(this);connect(ReadScheduler::instance(),&ReadScheduler::available,this,&DataReadController::pump);
  connect(this,&QObject::destroyed,[]{sessions.removeAll(nullptr);});
}
bool DataReadController::any_busy(){for(const auto& item:sessions)if(item&&item->busy())return true;return false;}
void DataReadController::cancel_all(){for(const auto& item:sessions)if(item)item->cancel();}
void DataReadController::cancel(){++m_generation;m_pending.reset();if(m_cancel)m_cancel->store(true);if(!m_running){emit drained();if(m_dispose)deleteLater();}}
void DataReadController::dispose(){m_dispose=true;cancel();}
void DataReadController::index(const QString& file){cancel();m_pending=std::make_shared<Request>();m_pending->file=file;m_pending->index=true;pump();}
void DataReadController::read(const QString& file,const QString& target,const std::vector<size_t>& indices,bool coordinates,bool quantized){
  cancel();m_pending=std::make_shared<Request>();m_pending->file=file;m_pending->target=target;m_pending->indices=indices;m_pending->coordinates=coordinates;m_pending->quantized=quantized;pump();
}
void DataReadController::pump(){
  if(m_running||!m_pending||m_dispose)return;
  if(!ReadScheduler::instance()->acquire())return;
  const auto request=m_pending;m_pending.reset();m_running=true;const auto generation=m_generation;
  const auto cancelled=std::make_shared<std::atomic_bool>(false);m_cancel=cancelled;
  QPointer<DataReadController> self=this;
  QThreadPool::globalInstance()->start([self,request,generation,cancelled]{
    std::shared_ptr<const grib::Catalog> catalog;auto plane=std::make_shared<DataSource::PlaneData>();std::shared_ptr<void> budget;QString error;
    try {
      if(request->index)catalog=GribData::open(request->file,[cancelled]{return cancelled->load();});
      else {
        catalog=GribData::open(request->file,[cancelled]{return cancelled->load();});
        const int field_index=[&]{std::vector<int> indices;for(auto value:request->indices){if(value>INT_MAX)throw grib::Error("InvalidSelection","Index overflow");indices.push_back(int(value));}return grib::resolve(*catalog,request->target,indices);}();
        const auto& field=catalog->fields.at(size_t(field_index));
        if(field.points<=0||quint64(field.points)>4194304)throw grib::Error("ResourceLimit","GRIB plane exceeds 4,194,304 point decode limit");
        budget=reserve_raster_bytes(quint64(field.points)*192+quint64(field.length));
        if(!budget)throw grib::Error("MemoryBudgetExceeded","Close another image or select a smaller GRIB plane");
        if(!cancelled->load())*plane=GribData::read(*catalog,request->target,request->indices,request->coordinates,request->quantized);
        if(!plane->error.isEmpty())error=plane->error_code+": "+plane->error;
      }
    }catch(const grib::Error& exception){error=exception.code+": "+exception.message;}catch(const std::exception& exception){error=QString::fromUtf8(exception.what());}
    QMetaObject::invokeMethod(QCoreApplication::instance(),[self,request,generation,cancelled,catalog,plane,budget,error]{
      ReadScheduler::instance()->release();if(!self)return;self->m_running=false;self->m_cancel.reset();
      if(generation==self->m_generation&&!cancelled->load()) {
        if(!error.isEmpty())emit self->failed(error);
        else if(request->index)emit self->indexed(catalog);else emit self->completed(plane,budget);
      }
      if(self->m_dispose){emit self->drained();self->deleteLater();return;}
      self->pump();if(!self->busy())emit self->drained();
    },Qt::QueuedConnection);
  });
}
