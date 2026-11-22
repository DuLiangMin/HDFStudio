// Application-owned per-window sessions; at most two read processes globally.
#pragma once
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include "slice_worker.hpp"

class SliceJobController : public QObject {
  Q_OBJECT
public:
  explicit SliceJobController();
  void submit(QJsonObject request,quint64 bytes);
  void cancel();
  void dispose();
  bool busy() const { return m_running || !m_pending.isEmpty(); }
  static bool any_busy();
  static void cancel_all();
signals:
  void completed(std::shared_ptr<SliceWorkerData> data,std::shared_ptr<void> lease);
  void failed(QString error);
  void drained();
private:
  void pump();
  void finished(bool normal,int code);
  void released();
  QPointer<QProcess> m_process;
  QJsonObject m_pending;
  quint64 m_bytes=0,m_generation=0,m_active_generation=0;
  quint64 m_expected_id=0;
  QJsonObject m_active_request;
  QElapsedTimer m_timer;
  void log_failure(const QString &message);
  bool m_running=false,m_dispose=false,m_reaping=false;
  std::shared_ptr<QTemporaryDir> m_directory;
  std::shared_ptr<void> m_lease;
  QByteArray m_stderr;
};
