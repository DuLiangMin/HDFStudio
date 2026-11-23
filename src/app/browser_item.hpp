//Copyright (C) 2016 Pedro Vicente
//GNU General Public License (GPL) Version 3 described in the LICENSE file

// Internal browser payload. ItemData owns the dataset; grids borrow it.
// File close must destroy dependent views before deleting tree payloads.
#pragma once
#include <QObject>
#include <QMetaType>
#include <QString>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include "hdf5.h"
#include "data_types.hpp"

class hdf_dataset_t
{
public:
  hdf_dataset_t(const char* path, const std::vector< hsize_t> &dim,
    size_t size, H5T_sign_t sign, H5T_class_t datatype_class) :
    m_path(path),
    m_dim(dim),
    m_datatype_size(size),
    m_datatype_sign(sign),
    m_datatype_class(datatype_class)
  {
    m_buf = NULL;
  }

  ~hdf_dataset_t()
  {
    free(m_buf);
  }

  void store(void *buf)
  {
    m_buf = buf;
  }
  std::string m_path;
  std::vector<hsize_t> m_dim;

  /////////////////////////////////////////////////////////////////////////////////////////////////////
  //needed to access HDF5 buffer data
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  size_t m_datatype_size;
  H5T_sign_t m_datatype_sign;
  H5T_class_t  m_datatype_class;
  void *m_buf;

  /////////////////////////////////////////////////////////////////////////////////////////////////////
  //NetCDF-3 (CDF-1/2/5) items are read through netCDF-C instead of HDF5.
  //m_slice_mode: for rank > 2 m_buf holds only the plane picked by the layer selectors, so a
  //multi-gigabyte variable is never loaded whole into the grid.
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  bool m_classic = false;
  bool m_slice_mode = false;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
//ItemData
//used by QTreeWidgetItem::setData to store custom data
//contains information to load a dataset from an HDF file:
//1) the file name
//2) the dataset name
//3) that's all :-)
/////////////////////////////////////////////////////////////////////////////////////////////////////

class ItemData
{
public:
  enum ItemKind
  {
    Root,
    Group,
    Variable,
    Attribute
  };

  ItemData(ItemKind kind, const std::string& file_name, const std::string& item_nm, hdf_dataset_t *dataset) :
    m_file_name(file_name),
    m_item_nm(item_nm),
    m_kind(kind),
    m_dataset(dataset)
  {
  }
  ~ItemData()
  {
    delete m_dataset;
  }
  std::string m_file_name;  // (Root/Variable/Group/Attribute) file name
  std::string m_item_nm; // (Root/Variable/Group/Attribute ) item name to display on tree
  ItemKind m_kind; // (Root/Variable/Group/Attribute) type of item
  hdf_dataset_t *m_dataset; // (Variable) HDF variable to display
  // GRIB objects keep their own descriptor; they never masquerade as an HDF dataset.
  bool m_grib = false;
  QString m_source_path, m_grib_details;
  DataSource::ObjectRef m_object;
  DataSource::ArrayInfo m_array;
  std::unique_ptr<QObject> m_grib_session;
};

Q_DECLARE_METATYPE(ItemData*);
