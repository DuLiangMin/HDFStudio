//Copyright (C) 2016 Pedro Vicente
//GNU General Public License (GPL) Version 3 described in the LICENSE file

// Legacy table model and window. No MainWindow or tree-widget dependency.
#include "legacy_grid.hpp"
#include "browser_item.hpp"
#include "legacy_grid_data.hpp"
#include "analysis_table_view.hpp"
#include "layer_navigation.hpp"
#include "nc_reader.hpp"
#include <QtWidgets>
#include <QProcess>
#include <cassert>
#include <algorithm>
#include <vector>
#include <climits>

class TableModel;
class ChildWindow : public QMainWindow, public LayerNavigation
{
  Q_OBJECT
public:
  ChildWindow(QWidget *parent, ItemData *item_data);
  std::vector<int> m_layer;  // current selected layer of a dimension > 2

  // LayerNavigation: the main window's Layer toolbar/menu drive this grid through its own combos
  int layer_axis_count() const override;
  QString layer_axis_name(int axis) const override;
  int layer_count(int axis) const override;
  int layer_index(int axis) const override;
  void set_layer_index(int axis, int index) override;

signals:
  void layer_changed();

  private slots:
  void previous_layer(int);
  void next_layer(int);
  void combo_layer(int);

private:
  QToolBar *m_tool_bar;
  std::vector<QComboBox *> m_vec_combo;
  QStringList m_axis_names;  // names of the leading dimensions, e.g. time, lev

protected:
  TableModel *m_model;
  hdf_dataset_t *m_dataset; // HDF variable to display (convenience pointer to data in ItemData)
};


// Starts the embedded netcdf_report.py worker owned by `owner`; returns nullptr if the resource
// is missing. Python runs outside the GUI thread. Arguments are passed directly without a shell.
static QProcess *start_python_worker(QObject *owner, const QStringList &arguments) {
  QFile script(":/netcdf_report.py");
  if(!script.open(QIODevice::ReadOnly)) return nullptr;
  auto *job=new QProcess(owner);
  job->start(qEnvironmentVariable("HDFSTUDIO_PYTHON","python"),QStringList{"-I","-c",QString::fromUtf8(script.readAll())}+arguments);
  return job;
}

class TableModel : public QAbstractTableModel
{
public:
  // GridDecoded / GridStored / GridBoth, described in hdf_explorer.hpp
  TableModel(QObject *parent, ItemData *item_data, GridValueMode mode = GridDecoded);
  GridValueMode mode() const { return m_mode; }
  int rowCount(const QModelIndex &parent = QModelIndex()) const;
  int columnCount(const QModelIndex &parent = QModelIndex()) const;
  QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const;
  QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const;

  hdf_dataset_t *m_dataset; // HDF variable to display (convenience pointer to data in ItemData)
  ChildWindow* m_widget; //get layers in toolbar
  int m_nbr_rows;   // number of rows
  int m_nbr_cols;   // number of numeric columns (date columns come after these)
  void data_changed(); //update table view when change of layer
  // Appends "yyyymmdd" and "yyyymmdd hh:mm:ss" columns decoded by netCDF4.num2date from the
  // variable's own units/calendar; each list must have one entry per row.
  void set_time_columns(const QStringList &days, const QStringList &moments, const QString &source);
  QStringList m_days;    // decoded yyyymmdd per row; empty when the variable is not a time coordinate
  QStringList m_moments; // decoded yyyymmdd hh:mm:ss per row
  QString m_time_source; // "units | calendar" shown as the date header tooltip
  // which of the two numbers this grid shows (decoded, stored, or both side by side)
  GridValueMode m_mode = GridDecoded;
  // CF packing of a decoded grid: decoded = stored x scale + offset (1 / 0 = not packed)
  double m_scale = 1, m_offset = 0;
  bool m_packed = false;
  QString m_value_header;   // "Value (decoded to real)", "Value (raw)", …
  QString m_value_tooltip;  // decoding rule, shown on the value header
  QString value_header_for(bool stored) const;
  void update_value_headers();
  // Numeric text of one buffer cell: stored values keep their own integer/float formatting
  QString stored_text(size_t index) const;
  // Numeric value of one buffer cell as a double (false for text and unsupported types)
  bool raw_number(size_t index, double &value) const;
  // Decoded text of one buffer cell: stored x scale + offset at 15 significant digits
  QString decoded_text(size_t index) const;
  // Same cell as shown by this grid (decoded unless the mode asks for the stored number)
  QString displayed_text(size_t index, bool stored) const;
private:
  ItemData *m_item_data; // the tree item that generated this grid
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
//TableModel::TableModel
/////////////////////////////////////////////////////////////////////////////////////////////////////

TableModel::TableModel(QObject *parent, ItemData *item_data, GridValueMode mode) :
QAbstractTableModel(parent),
m_dataset(item_data->m_dataset),
m_widget(NULL),
m_mode(mode),
m_item_data(item_data)
{
  assert(m_dataset->m_dim.size() <= H5S_MAX_RANK);

  // The file stores packed values: a decoded grid applies scale_factor/add_offset, the stored grid
  // shows the values as written. Attributes such as valid_range and _FillValue are in stored units,
  // so every header says which number it shows.
  if(item_data->m_kind == ItemData::Variable)
  {
    const NcPacking packing = nc_packing(classic_file_of(item_data), m_dataset->m_path);
    m_packed = packing.packed();
    m_scale = packing.scale;
    m_offset = packing.offset;
  }
  update_value_headers();
  if(m_packed && (m_mode == GridDecoded || m_mode == GridBoth))
    m_value_tooltip = QObject::tr("Decoded: stored x %1 + %2 (scale_factor / add_offset of the file)")
        .arg(m_scale,0,'g',15).arg(m_offset,0,'g',15);
  else if(item_data->m_kind == ItemData::Variable && m_mode != GridBoth)
    m_value_tooltip = QObject::tr("Stored values exactly as written in the file "
                                  "(no scale_factor/add_offset)");

  /////////////////////////////////////////////////////////////////////////////////////////////////////
  //define grid
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  if(m_dataset->m_dim.size() == 0)
  {
    m_nbr_rows = 1;
    m_nbr_cols = 1;
  }
  else if(m_dataset->m_dim.size() == 1)
  {
    m_nbr_rows = m_dataset->m_dim[0];
    m_nbr_cols = 1;
  }
  else
  {
    m_nbr_rows = m_dataset->m_dim[m_dataset->m_dim.size() - 2]; //index 1 for 3D
    m_nbr_cols = m_dataset->m_dim[m_dataset->m_dim.size() - 1]; //index 2 for 3D
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//TableModel::rowCount
/////////////////////////////////////////////////////////////////////////////////////////////////////

int TableModel::rowCount(const QModelIndex &parent) const
{
  return parent.isValid() ? 0 : m_nbr_rows;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//TableModel::columnCount
/////////////////////////////////////////////////////////////////////////////////////////////////////

int TableModel::columnCount(const QModelIndex &parent) const
{
  if(parent.isValid()) return 0;
  // side-by-side mode doubles the value columns: decoded, stored, decoded, stored, …
  const int value_columns = m_mode == GridBoth ? m_nbr_cols * 2 : m_nbr_cols;
  return value_columns + (m_days.isEmpty() ? 0 : 2);
}

void TableModel::set_time_columns(const QStringList &days, const QStringList &moments, const QString &source)
{
  if(!m_days.isEmpty() || days.size() != m_nbr_rows || moments.size() != m_nbr_rows || m_nbr_cols != 1) return;
  const int value_columns = m_mode == GridBoth ? m_nbr_cols * 2 : m_nbr_cols;
  beginInsertColumns(QModelIndex(), value_columns, value_columns + 1);
  m_days = days;
  m_moments = moments;
  m_time_source = source;
  endInsertColumns();
}

// In GridBoth every plane column is followed by its own stored column, so the date columns move
// to the right of 2 x m_nbr_cols. Headers name the pair: "Value 1 (decoded)", "Value 1 (raw)", …
QVariant TableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
  const int value_columns = m_mode == GridBoth ? m_nbr_cols * 2 : m_nbr_cols;
  if(orientation == Qt::Horizontal) {
    if(role == Qt::DisplayRole) {
      if(section >= value_columns) {
        if(m_days.isEmpty()) return QVariant();
        if(section == value_columns) return QStringLiteral("yyyymmdd");
        if(section == value_columns + 1) return QStringLiteral("yyyymmdd hh:mm:ss");
        return QVariant();
      }
      if(m_mode == GridBoth) {
        const int column = section / 2;
        const QString kind = section % 2 ? tr("raw") : tr("decoded");
        return tr("%1\n%2").arg(column + 1).arg(kind);
      }
      if(section == 0) return m_packed ? tr("Value\n%1").arg(m_mode == GridStored ? tr("raw") : tr("decoded")) : m_value_header;
    }
    if(role == Qt::ToolTipRole) {
      if(section < value_columns && !m_value_tooltip.isEmpty()) return m_value_tooltip;
      if(m_mode == GridBoth && section < value_columns)
        return section % 2 ? tr("Stored value exactly as written in the file")
                           : tr("Decoded: stored x %1 + %2").arg(m_scale,0,'g',15).arg(m_offset,0,'g',15);
      if(!m_days.isEmpty() && section >= value_columns)
        return tr("Decoded by netCDF4.num2date: %1").arg(m_time_source);
    }
  }
  return QAbstractTableModel::headerData(section, orientation, role);
}

// value_header_for: one place that decides the header text of a single-value grid
QString TableModel::value_header_for(bool stored) const
{
  if(!m_packed) return tr("Value");
  return stored ? tr("Value (raw)") : tr("Value (decoded to real)");
}

void TableModel::update_value_headers()
{
  m_value_header = value_header_for(m_mode == GridStored);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//TableModel::data_changed
//update table view when change of layer
/////////////////////////////////////////////////////////////////////////////////////////////////////

void TableModel::data_changed()
{
  // A NetCDF-3 plane is read on demand: load the newly selected layer before the view repaints.
  if(m_dataset->m_classic && m_dataset->m_slice_mode && m_widget)
  {
    const QString error = classic_load_variable(m_item_data, m_widget->m_layer);
    if(!error.isEmpty()) QMessageBox::warning(nullptr, QObject::tr("Grid"), error);
  }
  if(m_nbr_rows <= 0 || columnCount() <= 0) return;
  QModelIndex top = index(0, 0, QModelIndex());
  QModelIndex bottom = index(m_nbr_rows - 1, columnCount() - 1, QModelIndex());
  dataChanged(top, bottom);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//TableModel::data
/////////////////////////////////////////////////////////////////////////////////////////////////////

QVariant TableModel::data(const QModelIndex &index, int role) const
{
  if(!index.isValid() || index.row() < 0 || index.row() >= rowCount() ||
      index.column() < 0 || index.column() >= columnCount()) return {};
  if(role == Qt::ToolTipRole) return data(index, Qt::DisplayRole);
  if(role == Qt::TextAlignmentRole) return int(Qt::AlignRight | Qt::AlignVCenter);
  if(role != Qt::DisplayRole) return {};
  ChildWindow* parent = m_widget;
  size_t idx_buf = 0;
  const int value_columns = m_mode == GridBoth ? m_nbr_cols * 2 : m_nbr_cols;

  if(index.column() >= value_columns) {
    if(role != Qt::DisplayRole || index.row() >= m_days.size()) return QVariant();
    return index.column() == value_columns ? m_days.at(index.row()) : m_moments.at(index.row());
  }

  // side-by-side mode: decoded column then stored column for each plane column
  int value_column = index.column();
  bool stored = m_mode == GridStored;
  if(m_mode == GridBoth)
  {
    value_column = index.column() / 2;
    stored = (index.column() % 2) != 0;
  }

  //start of offset
  //a NetCDF-3 slice buffer already holds only the selected plane
  if(m_dataset->m_slice_mode)
  {
    idx_buf = 0;
  }
  //3D
  else if(parent->m_layer.size() == 1)
  {
    idx_buf = parent->m_layer[0] * m_nbr_rows * m_nbr_cols;
  }
  //4D
  else if(parent->m_layer.size() == 2)
  {
    idx_buf = parent->m_layer[0] * m_dataset->m_dim[1] + parent->m_layer[1];
    idx_buf *= m_nbr_rows * m_nbr_cols;
  }
  //5D
  else if(parent->m_layer.size() == 3)
  {
    idx_buf = parent->m_layer[0] * m_dataset->m_dim[1] * m_dataset->m_dim[2]
      + parent->m_layer[1] * m_dataset->m_dim[2]
      + parent->m_layer[2];
    idx_buf *= m_nbr_rows * m_nbr_cols;
  }

  //into current index
  idx_buf += index.row() * m_nbr_cols + value_column;

  if(role != Qt::DisplayRole)
  {
    return QVariant();
  }

  return displayed_text(idx_buf, stored);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//TableModel::stored_text
//numeric text of one buffer cell exactly as stored in the file
/////////////////////////////////////////////////////////////////////////////////////////////////////

QString TableModel::stored_text(size_t idx_buf) const
{
  QString str;
  switch(m_dataset->m_datatype_class)
  {
    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_STRING
    ///////////////////////////////////////////////////////////////////////////////////////

  case H5T_STRING:

    break;

    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_FLOAT
    ///////////////////////////////////////////////////////////////////////////////////////

  case H5T_FLOAT:
    if(sizeof(float) == m_dataset->m_datatype_size)
    {
      float *buf_ = static_cast<float*> (m_dataset->m_buf);
      str = QString::asprintf("%g", buf_[idx_buf]);
      return str;
    }
    else if(sizeof(double) == m_dataset->m_datatype_size)
    {
      // 15 significant digits: "%g" (6 digits) turned epoch seconds such as 915170400 into 9.1517e+08.
      double *buf_ = static_cast<double*> (m_dataset->m_buf);
      str = QString::asprintf("%.15g", buf_[idx_buf]);
      return str;
    }
#if H5_SIZEOF_LONG_DOUBLE !=0
    else if(sizeof(long double) == m_dataset->m_datatype_size)
    {
      long double *buf_;
      buf_ = static_cast<long double*> (m_dataset->m_buf);
      str = QString::asprintf("%Lf", buf_[idx_buf]);
      return str;
    }
#endif
    break;

  case H5T_INTEGER:

    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_INTEGER
    ///////////////////////////////////////////////////////////////////////////////////////

    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_NATIVE_SCHAR H5T_NATIVE_UCHAR
    ///////////////////////////////////////////////////////////////////////////////////////

    if(sizeof(char) == m_dataset->m_datatype_size)
    {
      if(H5T_SGN_NONE == m_dataset->m_datatype_sign)
      {
        unsigned char *buf_ = static_cast<unsigned char*> (m_dataset->m_buf);
        str = QString::asprintf("%u", buf_[idx_buf]);
        return str;
      }
      else
      {
        signed char *buf_ = static_cast<signed char*> (m_dataset->m_buf);
        str = QString::asprintf("%hhd", buf_[idx_buf]);
        return str;
      }
    }

    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_NATIVE_SHORT H5T_NATIVE_USHORT
    ///////////////////////////////////////////////////////////////////////////////////////

    else if(sizeof(short) == m_dataset->m_datatype_size)
    {
      if(H5T_SGN_NONE == m_dataset->m_datatype_sign)
      {
        unsigned short *buf_ = static_cast<unsigned short*> (m_dataset->m_buf);
        str = QString::asprintf("%u", buf_[idx_buf]);
        return str;

      }
      else
      {
        short *buf_ = static_cast<short*> (m_dataset->m_buf);
        str = QString::asprintf("%d", buf_[idx_buf]);
        return str;

      }

    }

    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_NATIVE_INT H5T_NATIVE_UINT
    ///////////////////////////////////////////////////////////////////////////////////////

    else if(sizeof(int) == m_dataset->m_datatype_size)
    {
      if(H5T_SGN_NONE == m_dataset->m_datatype_sign)
      {
        unsigned int* buf_ = static_cast<unsigned int*> (m_dataset->m_buf);
        str = QString::asprintf("%u", buf_[idx_buf]);
        return str;

      }
      else
      {
        int* buf_ = static_cast<int*> (m_dataset->m_buf);
        str = QString::asprintf("%d", buf_[idx_buf]);
        return str;
      }
    }

    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_NATIVE_LONG H5T_NATIVE_ULONG
    ///////////////////////////////////////////////////////////////////////////////////////

    else if(sizeof(long) == m_dataset->m_datatype_size)
    {

      if(H5T_SGN_NONE == m_dataset->m_datatype_sign)
      {
        unsigned long* buf_ = static_cast<unsigned long*> (m_dataset->m_buf);
        str = QString::asprintf("%lu", buf_[idx_buf]);
        return str;

      }
      else
      {
        long* buf_ = static_cast<long*> (m_dataset->m_buf);
        str = QString::asprintf("%ld", buf_[idx_buf]);
        return str;

      }

    }

    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_NATIVE_LLONG H5T_NATIVE_ULLONG
    ///////////////////////////////////////////////////////////////////////////////////////

    else if(sizeof(long long) == m_dataset->m_datatype_size)
    {

      if(H5T_SGN_NONE == m_dataset->m_datatype_sign)
      {
        unsigned long long* buf_ = static_cast<unsigned long long*> (m_dataset->m_buf);
        str = QString::asprintf("%llu", buf_[idx_buf]);
        return str;

      }
      else
      {
        long long* buf_ = static_cast<long long*> (m_dataset->m_buf);
        str = QString::asprintf("%lld", buf_[idx_buf]);
        return str;

      }

    }

    break;

    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_COMPOUND
    ///////////////////////////////////////////////////////////////////////////////////////

  case H5T_COMPOUND:

    break;


    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_ENUM
    ///////////////////////////////////////////////////////////////////////////////////////

  case H5T_ENUM:

    break;

    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_ARRAY
    ///////////////////////////////////////////////////////////////////////////////////////

  case H5T_ARRAY:

    break;

    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_VLEN
    ///////////////////////////////////////////////////////////////////////////////////////

  case H5T_VLEN:

    break;

    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_TIME H5T_BITFIELD H5T_OPAQUE
    ///////////////////////////////////////////////////////////////////////////////////////

  case H5T_TIME:
  case H5T_BITFIELD:
  case H5T_OPAQUE:

    break;

    ///////////////////////////////////////////////////////////////////////////////////////
    //H5T_REFERENCE
    ///////////////////////////////////////////////////////////////////////////////////////

  case H5T_REFERENCE:


    break;

  default:


    break;

  }; //switch

  return str;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//TableModel::displayed_text
//decoded values apply the file's scale_factor/add_offset (CF 8.1); the stored grid does not
/////////////////////////////////////////////////////////////////////////////////////////////////////

QString TableModel::displayed_text(size_t idx_buf, bool stored) const
{
  // stored numbers, unpacked variables and text types keep the exact stored text
  if(stored || !m_packed) return stored_text(idx_buf);
  return decoded_text(idx_buf);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//TableModel::decoded_text
//decoded = stored x scale_factor + add_offset, computed from the stored number (not from its text)
/////////////////////////////////////////////////////////////////////////////////////////////////////

QString TableModel::decoded_text(size_t idx_buf) const
{
  double stored = 0;
  if(!raw_number(idx_buf, stored)) return stored_text(idx_buf);
  return QString::asprintf("%.15g", stored * m_scale + m_offset);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//TableModel::raw_number
//one buffer cell as a double; false for text, long double and the other HDF5 classes.
//Decoding must start from the stored number, not from its "%g" text (6 significant digits).
/////////////////////////////////////////////////////////////////////////////////////////////////////

bool TableModel::raw_number(size_t idx_buf, double &value) const
{
  // One byte pointer for every width: a ternary over int*/unsigned int* would let MSVC choose one
  // of the two pointer types for both branches and read a negative stored value as unsigned.
  const size_t size = m_dataset->m_datatype_size;
  const unsigned char *cell = static_cast<const unsigned char*>(m_dataset->m_buf) + idx_buf * size;
  if(m_dataset->m_datatype_class == H5T_FLOAT)
  {
    if(size == sizeof(float)) { value = *reinterpret_cast<const float*>(cell); return true; }
    if(size == sizeof(double)) { value = *reinterpret_cast<const double*>(cell); return true; }
    return false;
  }
  if(m_dataset->m_datatype_class != H5T_INTEGER) return false;
  const bool is_signed = m_dataset->m_datatype_sign != H5T_SGN_NONE;
  if(size == 1) value = is_signed ? double(*reinterpret_cast<const signed char*>(cell))
                                  : double(*reinterpret_cast<const unsigned char*>(cell));
  else if(size == 2) value = is_signed ? double(*reinterpret_cast<const short*>(cell))
                                       : double(*reinterpret_cast<const unsigned short*>(cell));
  else if(size == 4) value = is_signed ? double(*reinterpret_cast<const int*>(cell))
                                       : double(*reinterpret_cast<const unsigned int*>(cell));
  else if(size == 8)
  {
    if(is_signed)
    {
      const long long stored = *reinterpret_cast<const long long*>(cell);
      // beyond 2^53 a double cannot hold every integer: show the stored text unchanged
      if(stored > 9007199254740992LL || stored < -9007199254740992LL) return false;
      value = static_cast<double>(stored);
    }
    else
    {
      const unsigned long long stored = *reinterpret_cast<const unsigned long long*>(cell);
      if(stored > 9007199254740992ULL) return false;
      value = static_cast<double>(stored);
    }
  }
  else return false;
  return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
//ChildWindowTable
//model/view
/////////////////////////////////////////////////////////////////////////////////////////////////////

// Window titles of the three grids; the same words as the context menu entries.
QString grid_mode_name(GridValueMode mode)
{
  switch(mode)
  {
  case GridStored: return QObject::tr("Stored Grid (raw value)");
  case GridBoth:   return QObject::tr("Both Grids (decoded and raw)");
  default:                     return QObject::tr("Data Grid (decoded to real)");
  }
}

class ChildWindowTable : public ChildWindow
{
public:
  // mode: GridDecoded (default), GridStored or GridBoth (side by side)
  ChildWindowTable(QWidget *parent, ItemData *item_data, GridValueMode mode = GridDecoded) :
    ChildWindow(parent, item_data)
  {
    //the title says which grid this is, so two tables of one variable cannot be mixed up
    if(item_data->m_kind == ItemData::Variable)
      setWindowTitle(windowTitle() + tr(" - ") + grid_mode_name(mode));

    //each new table widget has its own model
    m_model = new TableModel(this, item_data, mode);
    m_model->m_widget = this;
    m_table = new AnalysisTableView(this);
    m_table->setModel(m_model);
    setCentralWidget(m_table);
    m_table->setObjectName("gridTable");
    m_table->setAccessibleName(tr("Data grid cells"));
    auto *tools = addToolBar(tr("Grid analysis"));
    tools->setObjectName("gridAnalysisToolbar");
    tools->setMovable(false);
    tools->addAction(m_table->autoFitAction());
    tools->addAction(m_table->copyAction());
    auto *selection = new QLabel(m_table->selectionSummary(), tools);
    selection->setObjectName("gridSelectionSummary");
    tools->addWidget(selection);
    connect(m_table, &AnalysisTableView::selectionSummaryChanged, selection, &QLabel::setText);
    connect(m_table, &AnalysisTableView::copyFailed, this, [this](const QString &error) {
      QMessageBox::warning(this, tr("Copy selection"), error);
    });
    start_time_decoding(item_data);
  }
private:
  // A one-dimensional variable is offered to netcdf_report.py "dates"; the worker exits with 3
  // unless the variable declares "UNIT since DATE" units, in which case the grid stays numeric.
  void start_time_decoding(ItemData *item_data)
  {
    if(item_data->m_kind != ItemData::Variable || m_dataset->m_dim.size() != 1) return;
    const QString file = QFileInfo(QString::fromStdString(item_data->m_file_name)).absoluteFilePath();
    QProcess *job = start_python_worker(this, {"dates", file, QString::fromStdString(m_dataset->m_path)});
    if(!job) return;
    connect(job, &QProcess::errorOccurred, job, [job](QProcess::ProcessError error) {
      if(error == QProcess::FailedToStart) job->deleteLater();
    });
    connect(job, &QProcess::finished, this, [this, job](int code, QProcess::ExitStatus status) {
      job->deleteLater();
      if(status != QProcess::NormalExit || code != 0) return;
      const QStringList lines = QString::fromUtf8(job->readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
      if(lines.isEmpty() || !lines.first().startsWith("#\t")) return;
      const QStringList head = lines.first().trimmed().split('\t');
      QStringList days, moments;
      for(int i = 1; i < lines.size(); i++) {
        const QStringList cells = lines.at(i).trimmed().split('\t');
        if(cells.size() != 2) return;
        days.append(cells.at(0));
        moments.append(cells.at(1));
      }
      m_model->set_time_columns(days, moments, head.size() == 3 ? head.at(1) + " | calendar=" + head.at(2) : QString());
    });
  }
  AnalysisTableView *m_table;
};


ChildWindow::ChildWindow(QWidget *parent, ItemData *item_data) :
QMainWindow(parent),
m_dataset(item_data->m_dataset)
{
  QString str;
  str = QString::asprintf(" : %s", item_data->m_item_nm.c_str());
  this->setWindowTitle(QFileInfo(item_data->m_file_name.c_str()).fileName() + str);

  //currently selected layers for dimensions greater than two are the first layer
  if(m_dataset->m_dim.size() > 2)
  {
    for(size_t idx_dmn = 0; idx_dmn < m_dataset->m_dim.size() - 2; idx_dmn++)
    {
      m_layer.push_back(0);
    }
  }

  //names of the leading dimensions (time, lev, ...) from netCDF-C; "axis N" when unknown (plain HDF5)
  const std::vector<std::string> dimension_names =
      nc_dimension_names(QString::fromLocal8Bit(item_data->m_file_name.c_str()), m_dataset->m_path);
  for(size_t idx_dmn = 0; idx_dmn < m_layer.size(); idx_dmn++)
  {
    if(dimension_names.size() == m_dataset->m_dim.size())
      m_axis_names << QString::fromStdString(dimension_names[idx_dmn]);
    else
      m_axis_names << tr("axis %1").arg(idx_dmn);
  }

  QSignalMapper *signal_mapper_next = NULL;
  QSignalMapper *signal_mapper_previous = NULL;
  QSignalMapper *signal_mapper_combo = NULL;

  //data has layers
  if(m_dataset->m_dim.size() > 2)
  {
    m_tool_bar = addToolBar(tr("Layers"));

    signal_mapper_next = new QSignalMapper(this);
    signal_mapper_previous = new QSignalMapper(this);
    signal_mapper_combo = new QSignalMapper(this);
    connect(signal_mapper_next, &QSignalMapper::mappedInt, this, &ChildWindow::next_layer);
    connect(signal_mapper_previous, &QSignalMapper::mappedInt, this, &ChildWindow::previous_layer);
    connect(signal_mapper_combo, &QSignalMapper::mappedInt, this, &ChildWindow::combo_layer);
  }

  //number of dimensions above a two-dimensional dataset
  for(size_t idx_dmn = 0; idx_dmn < m_layer.size(); idx_dmn++)
  {
    ///////////////////////////////////////////////////////////////////////////////////////
    //next layer
    ///////////////////////////////////////////////////////////////////////////////////////

    QAction *action_next  = new QAction(tr("&Next layer..."), this);
    action_next->setIcon(QIcon(":/images/right.png"));
    action_next->setStatusTip(tr("Next layer"));
    connect(action_next, SIGNAL(triggered()), signal_mapper_next, SLOT(map()));
    signal_mapper_next->setMapping(action_next, idx_dmn);

    ///////////////////////////////////////////////////////////////////////////////////////
    //previous layer
    ///////////////////////////////////////////////////////////////////////////////////////

    QAction *action_previous = new QAction(tr("&Previous layer..."), this);
    action_previous->setIcon(QIcon(":/images/left.png"));
    action_previous->setStatusTip(tr("Previous layer"));
    connect(action_previous, SIGNAL(triggered()), signal_mapper_previous, SLOT(map()));
    signal_mapper_previous->setMapping(action_previous, idx_dmn);

    ///////////////////////////////////////////////////////////////////////////////////////
    //add to toolbar
    ///////////////////////////////////////////////////////////////////////////////////////

    m_tool_bar->addAction(action_next);
    m_tool_bar->addAction(action_previous);

    ///////////////////////////////////////////////////////////////////////////////////////
    //add combo box with layers, fill with possible coordinate variables and store combo in vector
    ///////////////////////////////////////////////////////////////////////////////////////

    QComboBox *combo = new QComboBox;
    QFont font = combo->font();
    font.setPointSize(9);
    combo->setFont(font);
    QStringList list;

    for(unsigned int idx = 0; idx < m_dataset->m_dim[idx_dmn]; idx++)
    {
      str = QString::asprintf("%u", idx + 1);
      list.append(str);
    }


    combo->addItems(list);
    combo->setToolTip(tr("Layer of %1").arg(m_axis_names.at(int(idx_dmn))));
    connect(combo, SIGNAL(currentIndexChanged(int)), signal_mapper_combo, SLOT(map()));
    signal_mapper_combo->setMapping(combo, idx_dmn);
    m_tool_bar->addWidget(combo);
    m_vec_combo.push_back(combo);
  }


}

///////////////////////////////////////////////////////////////////////////////////////
//ChildWindow::previous_layer
///////////////////////////////////////////////////////////////////////////////////////

void ChildWindow::previous_layer(int idx_layer)
{
  set_layer_index(idx_layer, layer_step_target(layer_index(idx_layer), layer_count(idx_layer), LayerStep::Previous));
}

///////////////////////////////////////////////////////////////////////////////////////
//ChildWindow::next_layer
///////////////////////////////////////////////////////////////////////////////////////

void ChildWindow::next_layer(int idx_layer)
{
  set_layer_index(idx_layer, layer_step_target(layer_index(idx_layer), layer_count(idx_layer), LayerStep::Next));
}

///////////////////////////////////////////////////////////////////////////////////////
//ChildWindow::combo_layer
//every layer change ends here: the combo is the single source of the selection, so the
//plane is read once and the main window's Layer toolbar is told through layer_changed()
///////////////////////////////////////////////////////////////////////////////////////

void ChildWindow::combo_layer(int idx_layer)
{
  QComboBox *combo = m_vec_combo.at(idx_layer);
  m_layer[idx_layer] = combo->currentIndex();
  m_model->data_changed();
  update();
  emit layer_changed();
}

///////////////////////////////////////////////////////////////////////////////////////
//ChildWindow: LayerNavigation
///////////////////////////////////////////////////////////////////////////////////////

int ChildWindow::layer_axis_count() const
{
  return int(m_layer.size());
}

QString ChildWindow::layer_axis_name(int axis) const
{
  return m_axis_names.value(axis);
}

int ChildWindow::layer_count(int axis) const
{
  if(axis < 0 || axis >= layer_axis_count()) return 0;
  return int(std::min<hsize_t>(m_dataset->m_dim[size_t(axis)], hsize_t(INT_MAX)));
}

int ChildWindow::layer_index(int axis) const
{
  if(axis < 0 || axis >= layer_axis_count()) return 0;
  return m_layer[size_t(axis)];
}

void ChildWindow::set_layer_index(int axis, int index)
{
  if(axis < 0 || axis >= layer_axis_count()) return;
  const int target = layer_clamp(index, layer_count(axis));
  QComboBox *combo = m_vec_combo.at(size_t(axis));
  if(combo->currentIndex() == target) return;
  combo->setCurrentIndex(target);   // -> combo_layer(): reads the plane and emits layer_changed()
}


QWidget *create_legacy_grid(QWidget *parent, ItemData *item_data, GridValueMode mode)
{
  return new ChildWindowTable(parent, item_data, mode);
}

#include "legacy_grid.moc"
