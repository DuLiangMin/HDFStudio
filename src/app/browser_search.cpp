#include "browser_search.hpp"
#include "browser_item.hpp"
#include <QTreeWidget>
#include <QVariantMap>
#include "hdf5.h"

namespace BrowserSearch {
void set_hdf_metadata(QTreeWidgetItem *item, qint64 dataset_id) {
  QVariantMap metadata;
  QStringList notes;
  for(const char *key:{"long_name","standard_name","description","units"}) {
    if(H5Aexists(hid_t(dataset_id),key)<=0)continue;
    const hid_t attribute=H5Aopen(hid_t(dataset_id),key,H5P_DEFAULT);
    if(attribute<0)continue;
    const hid_t type=H5Aget_type(attribute), space=H5Aget_space(attribute);
    if(type>=0 && space>=0 && H5Tget_class(type)==H5T_STRING && H5Sget_simple_extent_npoints(space)==1) {
      if(H5Tis_variable_str(type)>0) {
        // HDF5拥有临时分配；只保留4096字节，随后立即归还。
        // H5Aread不支持自定义vlen分配器，此上限不是库内部瞬时分配上限。
        char *text=nullptr;
        if(H5Aread(attribute,type,&text)>=0 && text) {
          int length=0;while(length<4096 && text[length]!='\0')++length;
          metadata[QString::fromLatin1(key)]=QString::fromUtf8(text,length).trimmed();
          if(length==4096)notes.append(QString::fromLatin1(key)+": text limited to 4096 bytes");
        }
        if(text)H5free_memory(text);
      }
      else {
        const auto size=H5Tget_size(type);
        if(size>0 && size<=4096) {
          QByteArray bytes(int(size),'\0');
          if(H5Aread(attribute,type,bytes.data())>=0) {
            const int zero=bytes.indexOf('\0');if(zero>=0)bytes.truncate(zero);
            metadata[QString::fromLatin1(key)]=QString::fromUtf8(bytes).trimmed();
          }
        } else notes.append(QString::fromLatin1(key)+": text exceeds 4096-byte search limit");
      }
    }
    if(space>=0)H5Sclose(space);
    if(type>=0)H5Tclose(type);
    H5Aclose(attribute);
  }
  metadata["notes"]=notes.join("; ");
  item->setData(0,MetadataRole,metadata);
}
QVector<Entry> entries(QTreeWidget *tree, QTreeWidgetItem *root) {
  QVector<Entry> result;
  if(!tree || !root)return result;
  // 普通变量树已完整建立；GRIB只取Variables，避免Messages懒加载页重复。
  QVector<QTreeWidgetItem*> pending{root};
  while(!pending.isEmpty()) {
    auto *node=pending.takeLast();
    auto *data=node->data(0,Qt::UserRole).value<ItemData*>();
    if(!data)continue;
    if(data->m_grib && data->m_object.object_id=="/messages")continue;
    if(data->m_kind==ItemData::Variable) {
      Entry entry;
      auto &variable=entry.variable;
      variable.name=node->text(0);
      if(data->m_grib)variable.path=data->m_object.object_id;
      else if(data->m_dataset) {
        variable.path=QString::fromLocal8Bit(data->m_dataset->m_path.c_str());
        QStringList shape;
        for(auto size:data->m_dataset->m_dim)shape<<QString::number(size);
        variable.shape="("+shape.join(", ")+")";
      }
      const auto metadata=node->data(0,MetadataRole).toMap();
      variable.name=metadata.value("name",variable.name).toString();
      variable.description=metadata.value("long_name").toString();
      const auto description=metadata.value("description").toString();
      if(!description.isEmpty() && description!=variable.description) {
        if(!variable.description.isEmpty())variable.description+="; ";
        variable.description+=description;
      }
      variable.standard_name=metadata.value("standard_name").toString();
      variable.units=metadata.value("units").toString();
      if(metadata.contains("shape"))variable.shape=metadata.value("shape").toString();
      variable.parameter_id=metadata.value("paramId").toString();
      if(metadata.contains("fields"))variable.fields=metadata.value("fields").toLongLong();
      if(variable.description.isEmpty())variable.description=variable.standard_name;
      const auto notes=metadata.value("notes").toString();
      if(!notes.isEmpty())variable.description+=" ["+notes+"]";
      entry.index=QPersistentModelIndex(tree->indexFromItem(node));
      result.append(std::move(entry));
    } else {
      for(int i=node->childCount()-1;i>=0;--i)pending.append(node->child(i));
    }
  }
  return result;
}
}
