#pragma once
#include <QGraphicsView>
#include <QGraphicsPixmapItem>
#include <QLabel>
#include <QGridLayout>
#include "section_snapshot.hpp"
class SectionAxis;
class SectionCanvas : public QWidget {
  Q_OBJECT
public:
  explicit SectionCanvas(QWidget *parent=nullptr);
  void set_snapshot(std::shared_ptr<const SectionSnapshot> snapshot);
  void clear();
  void fit();
  void zoom(double scale);
  void set_flips(bool horizontal,bool vertical);
  void set_date_labels(const QMap<QString,QString> &labels);
  std::vector<size_t> visible_indices(size_t dimension) const;
  QGraphicsView *view() const {return m_view;}
signals:
  void probe_changed(size_t row,size_t col,QString text);
  void viewport_changed();
protected:
  bool eventFilter(QObject *,QEvent *) override;
  void resizeEvent(QResizeEvent *) override;
private:
  friend class SectionAxis;
  void paint_axis(QPainter &painter,bool horizontal,QWidget *axis_widget);
  QString coordinate(size_t axis,size_t index) const;
  void axes();
  void update_probe(size_t row,size_t col);
  QGraphicsView *m_view;
  QGraphicsPixmapItem *m_item;
  SectionAxis *m_x,*m_y;
  QLabel *m_probe;
  std::shared_ptr<const SectionSnapshot> m_snapshot;
  QMap<QString,QString> m_dates;
  bool m_h=false,m_v=false,m_fit=true,m_fitting=false;
  bool m_probe_valid=false;
  size_t m_probe_row=0,m_probe_col=0;
};
