// SEARCH-001: search_tests [slot]; optional read-only sample: HDFSTUDIO_SEARCH_SAMPLE.
#include <QtTest>
#include <QtWidgets>
#include <QAbstractItemModelTester>
#include "search.hpp"
#include "search_panel.hpp"
#include "hdf_explorer.hpp"
#include "browser_search.hpp"
#include "browser_item.hpp"
#include "raster_view.hpp"
#include "grib/grib_data.hpp"
#include <netcdf.h>
#include "hdf5.h"

namespace {
void write_file(const QString &path) {QFile file(path);if(!file.open(QIODevice::WriteOnly))qFatal("fixture write failed");file.write("fixture");}
void nc_ok(int result){if(result!=NC_NOERR)qFatal("netCDF fixture: %s",nc_strerror(result));}
void create_nc(const QString &path,bool grouped=false) {
  int file,group,lat,lon,temp,other;
  nc_ok(nc_create(path.toLocal8Bit().constData(),grouped?NC_NETCDF4:NC_CLOBBER,&file));
  group=file;if(grouped)nc_ok(nc_def_grp(file,"forecast",&group));
  nc_ok(nc_def_dim(group,"y",2,&lat));nc_ok(nc_def_dim(group,"x",3,&lon));
  int dims[]{lat,lon};nc_ok(nc_def_var(group,"t",NC_DOUBLE,2,dims,&temp));
  nc_ok(nc_def_var(group,"ta",NC_DOUBLE,2,dims,&other));
  const QByteArray name("Air temperature"),standard("air_temperature"),units("K");
  nc_ok(nc_put_att_text(group,temp,"long_name",name.size(),name.constData()));
  nc_ok(nc_put_att_text(group,temp,"standard_name",standard.size(),standard.constData()));
  nc_ok(nc_put_att_text(group,temp,"units",units.size(),units.constData()));
  nc_ok(nc_put_att_text(group,other,"long_name",19,"Temperature anomaly"));
  nc_ok(nc_enddef(file));double values[]{0,1,2,3,4,5};nc_ok(nc_put_var_double(group,temp,values));nc_ok(nc_put_var_double(group,other,values));nc_ok(nc_close(file));
}
SearchPanel *panel(MainWindow &window) {
  auto *action=window.findChild<QAction*>("actionFindVariables");if(!action)qFatal("missing search action");action->trigger();
  return window.findChild<SearchPanel*>();
}

class SearchTest:public QObject {
  Q_OBJECT
private slots:
  void filenameAndVariableRules();
  void directoryScanLimitsAndCancellation();
  void fileResultsOpenAndCopyPath();
  void stopAndDestroyDuringScan();
  void closeMainWindowDuringScan();
  void classicAndGroupedVariablesLocateAndOpen();
  void hdfTextMetadataAndDuplicateNames();
  void contextSwitchAndCloseInvalidateResults();
  void gribVariablesAndRealC7f();
  void metadataTextBoundaries();
  void variableLimitAndReopenedIdentity();
  void hideDuringScanKeepsTask();
};

void SearchTest::filenameAndVariableRules() {
  QVERIFY(Search::filename_matches("EC_C7F_no_suffix","c7f"));
  QVERIFY(Search::filename_matches("EC_C7F_no_suffix","*C7F*"));
  QVERIFY(!Search::filename_matches("EC_C7F_no_suffix","*.grib2"));
  QVERIFY(Search::filename_matches("A.GRIB2","*.grib2"));
  QVERIFY(Search::filename_matches("anything",""));
  Search::Variable variable;variable.name="ta";variable.path="/forecast/ta";variable.description="Temperature anomaly";variable.parameter_id="130";
  QVERIFY(Search::variable_matches(variable,"temperature",false));
  QVERIFY(Search::variable_matches(variable,"TA",true));
  QVERIFY(!Search::variable_matches(variable,"t",true));
  QVERIFY(!Search::variable_matches(variable,"temperature",true));
  QVERIFY(Search::variable_matches(variable,"/forecast/ta",true));
  QVERIFY(Search::variable_matches(variable,"130",true));
}

void SearchTest::metadataTextBoundaries(){
  QTemporaryDir temp;const auto path=temp.filePath("text-boundary.h5");
  const hid_t file=H5Fcreate(path.toLocal8Bit().constData(),H5F_ACC_TRUNC,H5P_DEFAULT,H5P_DEFAULT),space=H5Screate(H5S_SCALAR);QVERIFY(file>=0&&space>=0);
  for(bool variable:{false,true})for(int length:{4095,4096,4097}){
    const auto name=QString("%1_%2").arg(variable?"vlen":"fixed").arg(length).toLatin1();const hid_t dataset=H5Dcreate2(file,name.constData(),H5T_NATIVE_DOUBLE,space,H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT),type=H5Tcopy(H5T_C_S1);QVERIFY(dataset>=0&&type>=0);QVERIFY(H5Tset_size(type,variable?H5T_VARIABLE:size_t(length))>=0);
    const hid_t attribute=H5Acreate2(dataset,"long_name",type,space,H5P_DEFAULT,H5P_DEFAULT);QVERIFY(attribute>=0);const QByteArray bytes(length,'x');const char *pointer=bytes.constData();QVERIFY(H5Awrite(attribute,type,variable?static_cast<const void*>(&pointer):static_cast<const void*>(pointer))>=0);
    H5Aclose(attribute);QTreeWidgetItem item;BrowserSearch::set_hdf_metadata(&item,dataset);const auto metadata=item.data(0,BrowserSearch::MetadataRole).toMap();
    QCOMPARE(metadata.value("long_name").toString().size(),!variable&&length>4096?0:std::min(length,4096));
    QCOMPARE(!metadata.value("notes").toString().isEmpty(),variable?length>=4096:length>4096);H5Tclose(type);H5Dclose(dataset);
  }H5Sclose(space);H5Fclose(file);
}

void SearchTest::variableLimitAndReopenedIdentity(){
  SearchPanel bounded;QStandardItemModel identities(10001,1);QVector<BrowserSearch::Entry> entries;entries.reserve(10001);for(int i=0;i<10001;++i){BrowserSearch::Entry entry;entry.index=QPersistentModelIndex(identities.index(i,0));entry.variable.name=QString("field_%1").arg(i);entry.variable.path="/"+entry.variable.name;entry.variable.parameter_id=QString::number(i);entries.append(entry);}bounded.set_context("synthetic",entries);
  QCOMPARE(bounded.findChild<QTableView*>("searchVariableResults")->model()->rowCount(),10000);QVERIFY(bounded.findChild<QLabel*>("searchVariableStatus")->text().contains("Display limit"));bounded.findChild<QCheckBox*>("searchVariableExact")->setChecked(true);bounded.findChild<QLineEdit*>("searchVariableQuery")->setText("field_10000");QCOMPARE(bounded.findChild<QTableView*>("searchVariableResults")->model()->rowCount(),1);
  QTemporaryDir temp;const auto path=temp.filePath("reopen.nc");create_nc(path);MainWindow window;auto *search=panel(window);QCOMPARE(window.read_file(path),0);auto *table=search->findChild<QTableView*>("searchVariableResults");
  const auto old_index=table->model()->index(0,0).data(Qt::UserRole).value<QPersistentModelIndex>();QVERIFY(old_index.isValid());window.close_all_files();QVERIFY(!old_index.isValid());QCOMPARE(window.read_file(path),0);QVERIFY(!old_index.isValid());QCOMPARE(table->model()->rowCount(),2);window.close_all_files();
}

void SearchTest::hideDuringScanKeepsTask(){
  QTemporaryDir temp;for(int i=0;i<100;++i)write_file(temp.filePath(QString("C7F_%1").arg(i)));SearchPanel search;search.show();search.findChild<QLineEdit*>("searchDirectory")->setText(temp.path());search.findChild<QPushButton*>("searchStart")->click();search.hide();QTRY_VERIFY_WITH_TIMEOUT(!search.property("searchBusy").toBool(),10000);search.show();QCOMPARE(search.findChild<QTableView*>("searchFileResults")->model()->rowCount(),100);QVERIFY(search.findChild<QLabel*>("searchFileStatus")->text().startsWith("Finished"));
}

void SearchTest::directoryScanLimitsAndCancellation() {
  QTemporaryDir temp;QVERIFY(temp.isValid());QVERIFY(QDir(temp.path()).mkdir("sub"));
  write_file(temp.filePath("EC_C7F"));write_file(temp.filePath("sub/EC_C7F"));write_file(temp.filePath("ignore.nc"));write_file(temp.filePath(".hidden_C7F"));
  Search::FileQuery query;query.directory=temp.path();query.pattern="C7F";
  auto result=Search::find_files(query);QCOMPARE(result.files.size(),3);QCOMPARE(result.errors,quint64(0));
  QSet<QString> paths;for(const auto &file:result.files)paths.insert(file.path);QCOMPARE(paths.size(),3);
  query.recursive=false;QCOMPARE(Search::find_files(query).files.size(),2);
  query.recursive=true;query.max_results=1;result=Search::find_files(query);QVERIFY(result.truncated);QCOMPARE(result.files.size(),1);
  query.max_results=100;query.max_entries=1;QVERIFY(Search::find_files(query).truncated);
  query.max_entries=100;int checks=0;result=Search::find_files(query,[&]{return ++checks>2;});QVERIFY(result.cancelled);
  query.directory=temp.filePath("does-not-exist");QVERIFY(Search::find_files(query).errors>0);
}

void SearchTest::fileResultsOpenAndCopyPath() {
  QTemporaryDir temp;QVERIFY(temp.isValid());const auto path=temp.filePath("C7F_fixture");create_nc(path);
  MainWindow window;window.resize(1200,850);window.show();
  window.findChild<QAction*>("actionFindFiles")->trigger();auto *search=window.findChild<SearchPanel*>();QVERIFY(search);
  search->findChild<QLineEdit*>("searchDirectory")->setText(temp.path());search->findChild<QLineEdit*>("searchFilename")->setText("*c7f*");
  auto *table=search->findChild<QTableView*>("searchFileResults");
  QAbstractItemModelTester model_test(table->model(),QAbstractItemModelTester::FailureReportingMode::QtTest);
  search->findChild<QPushButton*>("searchStart")->click();
  QTRY_VERIFY_WITH_TIMEOUT(!search->property("searchBusy").toBool(),10000);QCOMPARE(table->model()->rowCount(),1);
  table->selectRow(0);search->findChild<QPushButton*>("searchCopyPath")->click();
  QCOMPARE(QApplication::clipboard()->text(),QDir::toNativeSeparators(path));
  search->findChild<QPushButton*>("searchOpenFile")->click();
  QVERIFY(window.property("openSourceFiles").toStringList().contains(path));
  auto *variables=search->findChild<QTableView*>("searchVariableResults");QCOMPARE(variables->model()->rowCount(),2);
  QCOMPARE(search->findChild<QTabWidget*>("searchTabs")->currentIndex(),1);
  QCOMPARE(table->model()->rowCount(),1);
  QFile::remove(path);table->selectRow(0);search->findChild<QPushButton*>("searchOpenFile")->click();
  QVERIFY(search->findChild<QLabel*>("searchFileStatus")->text().contains("no longer"));
  window.close_all_files();
}

void SearchTest::stopAndDestroyDuringScan() {
  QTemporaryDir temp;QVERIFY(temp.isValid());for(int i=0;i<1500;++i)write_file(temp.filePath(QString("file_%1").arg(i)));
  auto *search=new SearchPanel;search->findChild<QLineEdit*>("searchDirectory")->setText(temp.path());
  search->findChild<QPushButton*>("searchStart")->click();search->findChild<QPushButton*>("searchStop")->click();
  QTRY_VERIFY_WITH_TIMEOUT(!search->property("searchBusy").toBool(),10000);
  QVERIFY(search->findChild<QLabel*>("searchFileStatus")->text().contains("Stopped"));
  search->findChild<QLineEdit*>("searchFilename")->setText("never-matches");search->findChild<QPushButton*>("searchStart")->click();
  QTRY_VERIFY_WITH_TIMEOUT(!search->property("searchBusy").toBool(),10000);
  QCOMPARE(search->findChild<QTableView*>("searchFileResults")->model()->rowCount(),0);
  search->findChild<QPushButton*>("searchStart")->click();delete search;
  // 工作线程不归面板销毁；等待其协作取消完成，不访问已释放面板。
  QTest::qWait(100);
}

void SearchTest::closeMainWindowDuringScan() {
  QTemporaryDir temp;QVERIFY(temp.isValid());
  for(int i=0;i<1500;++i)write_file(temp.filePath(QString("file_%1").arg(i)));
  auto *window=new MainWindow;
  window->findChild<QAction*>("actionFindFiles")->trigger();
  auto *search=window->findChild<SearchPanel*>();QVERIFY(search);
  search->findChild<QLineEdit*>("searchDirectory")->setText(temp.path());
  search->findChild<QPushButton*>("searchStart")->click();
  QVERIFY(search->property("searchBusy").toBool());
  delete window;
  // 处理完成信号与deleteLater；主窗/面板销毁后不得再回调其派生成员。
  QTest::qWait(100);
}

void SearchTest::classicAndGroupedVariablesLocateAndOpen() {
  QTemporaryDir temp;QVERIFY(temp.isValid());
  for(bool grouped:{false,true}) {
    const auto path=temp.filePath(grouped?"grouped.nc":"classic.nc");create_nc(path,grouped);
    MainWindow window;window.resize(1200,850);window.show();auto *search=panel(window);QVERIFY(search);QCOMPARE(window.read_file(path),0);
    auto *table=search->findChild<QTableView*>("searchVariableResults");auto *query=search->findChild<QLineEdit*>("searchVariableQuery");auto *exact=search->findChild<QCheckBox*>("searchVariableExact");
    // NC4维度尺度x/y也是现有HDF树中的数据集，搜索不能隐藏它们。
    QCOMPARE(table->model()->rowCount(),grouped?4:2);query->setText("air_temperature");QCOMPARE(table->model()->rowCount(),1);
    query->setText("Temperature");QCOMPARE(table->model()->rowCount(),2);
    exact->setChecked(true);query->setText("t");QCOMPARE(table->model()->rowCount(),1);
    auto *tree=window.findChild<FileTreeWidget*>();tree->collapseAll();table->selectRow(0);
    search->findChild<QPushButton*>("searchVariableAction0")->click();QVERIFY(tree->currentItem());QCOMPARE(tree->currentItem()->text(0),QString("t"));QVERIFY(tree->currentItem()->parent()->isExpanded());
    search->findChild<QPushButton*>("searchVariableAction1")->click();auto *raster=window.findChild<RasterView*>();QVERIFY(raster);QTRY_VERIFY_WITH_TIMEOUT(raster->plane_snapshot().values,10000);QCOMPARE(raster->plane_snapshot().values->size(),size_t(6));
    search->findChild<QPushButton*>("searchVariableAction2")->click();QCOMPARE(window.findChild<QMdiArea*>()->subWindowList().size(),2);
    search->findChild<QPushButton*>("searchVariableAction3")->click();QVERIFY(!window.findChildren<QDialog*>().isEmpty());
    query->setText(grouped?"/forecast/t":"/t");QCOMPARE(table->model()->rowCount(),1);
    window.close_all_files();QCOMPARE(table->model()->rowCount(),0);
  }
}

void SearchTest::hdfTextMetadataAndDuplicateNames() {
  QTemporaryDir temp;QVERIFY(temp.isValid());const auto path=temp.filePath("metadata.h5");
  const hid_t file=H5Fcreate(path.toLocal8Bit().constData(),H5F_ACC_TRUNC,H5P_DEFAULT,H5P_DEFAULT);QVERIFY(file>=0);
  const hid_t scalar=H5Screate(H5S_SCALAR);QVERIFY(scalar>=0);
  for(const char *group_name:{"a","b"}) {
    const hid_t group=H5Gcreate2(file,group_name,H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);QVERIFY(group>=0);
    const hid_t dataset=H5Dcreate2(group,"t",H5T_NATIVE_DOUBLE,scalar,H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);QVERIFY(dataset>=0);
    const hid_t text_type=H5Tcopy(H5T_C_S1);QVERIFY(text_type>=0);QVERIFY(H5Tset_size(text_type,H5T_VARIABLE)>=0);
    const hid_t attribute=H5Acreate2(dataset,"long_name",text_type,scalar,H5P_DEFAULT,H5P_DEFAULT);QVERIFY(attribute>=0);
    const char *description=group_name[0]=='a'?"Air temperature":"Ocean temperature";
    QVERIFY(H5Awrite(attribute,text_type,&description)>=0);
    H5Aclose(attribute);H5Tclose(text_type);H5Dclose(dataset);H5Gclose(group);
  }
  H5Sclose(scalar);H5Fclose(file);
  MainWindow window;auto *search=panel(window);QCOMPARE(window.read_file(path),0);
  auto *table=search->findChild<QTableView*>("searchVariableResults");auto *query=search->findChild<QLineEdit*>("searchVariableQuery");
  QCOMPARE(table->model()->rowCount(),2);query->setText("temperature");QCOMPARE(table->model()->rowCount(),2);
  QSet<QString> paths;for(int row=0;row<2;++row){paths.insert(table->model()->index(row,5).data().toString());QCOMPARE(table->model()->index(row,2).data().toString(),QString("unknown"));}
  QCOMPARE(paths,QSet<QString>({"/a/t","/b/t"}));
  query->setText("Ocean");QCOMPARE(table->model()->rowCount(),1);
  search->findChild<QCheckBox*>("searchVariableExact")->setChecked(true);query->setText("/a/t");QCOMPARE(table->model()->rowCount(),1);
  window.close_all_files();QCOMPARE(table->model()->rowCount(),0);
}

void SearchTest::contextSwitchAndCloseInvalidateResults() {
  QTemporaryDir temp;QVERIFY(temp.isValid());const auto first=temp.filePath("first.nc"),second=temp.filePath("second.nc");create_nc(first);create_nc(second);
  MainWindow window;auto *search=panel(window);QCOMPARE(window.read_file(first),0);QCOMPARE(window.read_file(second),0);
  auto *table=search->findChild<QTableView*>("searchVariableResults");QCOMPARE(table->model()->rowCount(),2);
  auto *tree=window.findChild<FileTreeWidget*>();tree->setCurrentItem(tree->topLevelItem(0));
  QVERIFY(search->findChild<QLabel*>("searchCurrentFile")->text().contains("first.nc"));
  table->selectRow(0);const auto index=table->model()->index(0,0).data(Qt::UserRole).value<QPersistentModelIndex>();QVERIFY(index.isValid());
  window.close_file_of(tree->topLevelItem(0));QVERIFY(!index.isValid());QCOMPARE(table->model()->rowCount(),2);
  QVERIFY(search->findChild<QLabel*>("searchCurrentFile")->text().contains("second.nc"));
  window.close_all_files();QCOMPARE(table->model()->rowCount(),0);QVERIFY(search->findChild<QLabel*>("searchCurrentFile")->text().endsWith("none"));
}

void SearchTest::gribVariablesAndRealC7f() {
  QString sample=qEnvironmentVariable("HDFSTUDIO_SEARCH_SAMPLE");
  const bool real=!sample.isEmpty();
  if(!real)sample=QDir(HDFSTUDIO_TEST_SOURCE_DIR).absoluteFilePath("testdata/grib/mini_simple.grb2");
  QVERIFY2(QFileInfo::exists(sample),qPrintable(sample));MainWindow window;auto *search=panel(window);QCOMPARE(window.read_file(sample),0);
  auto *table=search->findChild<QTableView*>("searchVariableResults");auto *query=search->findChild<QLineEdit*>("searchVariableQuery");
  QVERIFY(table->model()->rowCount()>0);
  auto *tree=window.findChild<FileTreeWidget*>();const auto count=table->model()->rowCount();
  // 展开Messages也不应使变量搜索重复。
  auto *messages=tree->topLevelItem(0)->child(1);messages->setExpanded(true);if(messages->childCount())messages->child(0)->setExpanded(true);
  query->setText("temperature");query->clear();QCOMPARE(table->model()->rowCount(),count);
  if(real) {
    QCOMPARE(count,50);auto *exact=search->findChild<QCheckBox*>("searchVariableExact");exact->setChecked(true);
    query->setText("ta");QCOMPARE(table->model()->rowCount(),1);QCOMPARE(table->model()->index(0,4).data().toString(),QString("364"));
    query->setText("t");QCOMPARE(table->model()->rowCount(),1);QCOMPARE(table->model()->index(0,4).data().toString(),QString("7"));
    table->selectRow(0);search->findChild<QPushButton*>("searchVariableAction0")->click();
    QCOMPARE(tree->currentItem()->data(0,BrowserSearch::MetadataRole).toMap().value("name").toString(),QString("t"));
    exact->setChecked(false);query->setText("temperature");QVERIFY(table->model()->rowCount()>1);
    Search::FileQuery files;files.directory=QFileInfo(sample).absolutePath();files.pattern="C7F";
    auto result=Search::find_files(files);bool found=false;for(const auto &file:result.files)if(QFileInfo(file.path)==QFileInfo(sample))found=true;QVERIFY(found);
    files.pattern="*C7F*";QCOMPARE(Search::find_files(files).files.size(),result.files.size());
  }
  window.close_all_files();QCOMPARE(table->model()->rowCount(),0);
}
}
QTEST_MAIN(SearchTest)
#include "search_test.moc"
