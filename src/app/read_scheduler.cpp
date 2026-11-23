#include "read_scheduler.hpp"
#include <QCoreApplication>
#include <QTimer>
ReadScheduler* ReadScheduler::instance(){static auto* scheduler=new ReadScheduler;return scheduler;}
bool ReadScheduler::acquire(){if(m_active>=2)return false;++m_active;return true;}
void ReadScheduler::release(){Q_ASSERT(m_active>0);--m_active;QTimer::singleShot(0,this,&ReadScheduler::available);}
