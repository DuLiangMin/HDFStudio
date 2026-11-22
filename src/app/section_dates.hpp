// Optional bounded Python date conversion; one running process and one latest request.
#pragma once
#include <QObject>
#include <QProcess>
#include <QPointer>
#include <QMap>
#include <QJsonObject>
#include <vector>

struct SectionDateValue {QString key,units,calendar;double value;};
class SectionDates : public QObject {
  Q_OBJECT
public:
  SectionDates();
  void submit(quint64 revision,const std::vector<SectionDateValue> &values);
  void cancel();
  void dispose();
  static bool any_busy();
  static void cancel_all();
signals:
  void labels_ready(quint64 revision,QMap<QString,QString> labels,bool available);
private:
  void pump();
  void finished(bool normal,int code);
  QPointer<QProcess> m_process;
  QMap<QString,QString> m_cache,m_labels;
  std::vector<SectionDateValue> m_pending,m_active;
  QByteArray m_output;
  quint64 m_generation=0,m_active_generation=0,m_revision=0;
  bool m_dispose=false,m_pending_valid=false;
};
