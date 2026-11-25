// 将已建立的树适配为纯值检索目录；持久索引随关源失效，不借用ItemData。
#pragma once
#include "search.hpp"
#include <QPersistentModelIndex>
class QTreeWidget;
class QTreeWidgetItem;
namespace BrowserSearch {
enum { MetadataRole = Qt::UserRole + 30 };
struct Entry {
  Search::Variable variable;
  QPersistentModelIndex index;
};
QVector<Entry> entries(QTreeWidget *tree, QTreeWidgetItem *root);
void set_hdf_metadata(QTreeWidgetItem *item, qint64 dataset_id);
}
