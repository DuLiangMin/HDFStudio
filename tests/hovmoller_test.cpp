// Hovmoller deterministic unit tests; generated fixtures live only in QTemporaryDir.
#include <QtTest>
#include <QTemporaryDir>
#include <QProcess>
#include <numeric>
#include <cmath>
#include "slice_io.hpp"
#include "slice_worker.hpp"
#include "hovmoller_view.hpp"
#include "raster_budget.hpp"
#include "layer_playback.hpp"
#include "hdf_explorer.hpp"
#include <QJsonDocument>
#include <QJsonArray>
#include <cstring>

class HovmollerTest : public QObject {
  Q_OBJECT
private slots:
  void allAxisPairs();
  void invalidRequestsAndCsv();
  void packedAndWideTypes();
  void plainHdf();
  void coordinatesAndNearest();
  void workerProtocol();
  void viewAxesMaskExport();
  void viewLatestAndPlayback();
  void viewRankTwoAndBudget();
  void viewCloseLifecycle();
  void mainWindowIntegration();
  void businessSamples();
  void datesAndNumericFallback();
  void largeFixedAxisAndSourceChange();
  void missingMaskAndProbe();
  void canvasFillsShortSections();
  void heartbeatAndCancel();
  void corruptProtocolRejected();
  void largeToolbarAndMissingPlane();
  void cleanup();
};
static QString fixture(const QString &folder,int format,const std::vector<size_t> &shape) {
  const QString file=folder+"/fixture-"+QString::number(format)+".nc";
  int id=-1;if(nc_create(nc_path_bytes(file).constData(),NC_CLOBBER|format,&id)!=NC_NOERR)return {};
  std::vector<int> dims(shape.size());
  for(size_t axis=0;axis<shape.size();++axis)nc_def_dim(id,("dim"+std::to_string(axis)).c_str(),shape[axis],&dims[axis]);
  int variable=-1;nc_def_var(id,"field",NC_DOUBLE,int(shape.size()),dims.data(),&variable);nc_enddef(id);
  size_t count=1;for(auto length:shape)count*=length;std::vector<double> values(count);std::iota(values.begin(),values.end(),0.0);
  nc_put_var_double(id,variable,values.data());nc_close(id);return file;
}
void HovmollerTest::allAxisPairs() {
  QTemporaryDir folder;
  for(int format:{0,NC_64BIT_OFFSET,NC_64BIT_DATA,NC_NETCDF4,NC_NETCDF4|NC_CLASSIC_MODEL}) {
    const std::vector<size_t> shape={2,3,4,5};const auto file=fixture(folder.path(),format,shape);QVERIFY(!file.isEmpty());
    for(size_t row=0;row<4;++row)for(size_t col=0;col<4;++col)if(row!=col) {
      NcSliceSpec spec{row,col,{1,1,2,3}};
      auto out=nc_read_slice_decoded(file,"/field",{2,3,4,5},spec);QVERIFY2(out.error.isEmpty(),qPrintable(out.error));
      QCOMPARE(out.plane.rows,shape[row]);QCOMPARE(out.plane.cols,shape[col]);
      for(size_t r=0;r<shape[row];++r)for(size_t c=0;c<shape[col];++c){auto index=spec.fixed_indices;index[row]=r;index[col]=c;const auto flat=((index[0]*3+index[1])*4+index[2])*5+index[3];QCOMPARE(out.plane.values[r*shape[col]+c],double(flat));}
    }
  }
}
void HovmollerTest::invalidRequestsAndCsv() {
  QTemporaryDir folder;const auto file=fixture(folder.path(),NC_NETCDF4,{3,4,5});
  for(NcSliceSpec spec:std::vector<NcSliceSpec>{{0,0,{0,0,0}},{3,0,{0,0,0}},{0,1,{0,0,5}},{0,1,{0,0}}})QVERIFY(!nc_read_slice_decoded(file,"/field",{3,4,5},spec).error.isEmpty());
  QCOMPARE(nc_read_slice_decoded(file,"/field",{3,4,6},{0,1,{0,0,0}}).error_code,QString("ShapeChanged"));
  QCOMPARE(slice_validate({size_t(-1),2},{0,1,{0,0}}),QString("PixelLimitExceeded"));
  QProcess cli;const QString output=folder.path()+"/section.csv";
  cli.start(QCoreApplication::applicationDirPath()+"/hdf_cli.exe",{"slice",file,"/field","1","0",output,"0","0","2"});QVERIFY(cli.waitForFinished());QCOMPARE(cli.exitCode(),0);
  QFile csv(output);QVERIFY(csv.open(QIODevice::ReadOnly));const auto before=csv.readAll();QVERIFY(before.contains("row,col,row_coord,col_coord,value,state"));csv.close();
  cli.start(QCoreApplication::applicationDirPath()+"/hdf_cli.exe",{"slice",file,"/field","1","0",output,"0","0","2"});QVERIFY(cli.waitForFinished());QCOMPARE(cli.exitCode(),4);
  QVERIFY(csv.open(QIODevice::ReadOnly));QCOMPARE(csv.readAll(),before);
}
void HovmollerTest::packedAndWideTypes() {
  QTemporaryDir folder;const auto file=folder.path()+"/packed.nc";int id,dims[3],variable,wide;
  QCOMPARE(nc_create(nc_path_bytes(file).constData(),NC_NETCDF4|NC_CLOBBER,&id),NC_NOERR);
  for(int i=0;i<3;++i)QCOMPARE(nc_def_dim(id,("a"+std::to_string(i)).c_str(),2,&dims[i]),NC_NOERR);
  nc_def_var(id,"packed",NC_SHORT,3,dims,&variable);short fill=-99;double scale=.25,offset=10;
  nc_put_att_short(id,variable,"_FillValue",NC_SHORT,1,&fill);nc_put_att_double(id,variable,"scale_factor",NC_DOUBLE,1,&scale);nc_put_att_double(id,variable,"add_offset",NC_DOUBLE,1,&offset);
  nc_def_var(id,"wide",NC_UINT64,3,dims,&wide);unsigned long long bigfill=9007199254740993ull;nc_put_att_ulonglong(id,wide,"_FillValue",NC_UINT64,1,&bigfill);nc_enddef(id);
  short values[8]={0,1,2,-99,4,5,6,7};nc_put_var_short(id,variable,values);
  unsigned long long big[8]={9007199254740992ull,9007199254740993ull,9007199254740994ull,9007199254740995ull,5,6,7,8};nc_put_var_ulonglong(id,wide,big);nc_close(id);
  auto out=nc_read_slice_decoded(file,"/packed",{2,2,2},{1,2,{0,0,0}});QVERIFY(out.error.isEmpty());QCOMPARE(out.plane.values[0],10.0);QCOMPARE(out.plane.values[2],10.5);QVERIFY(std::isnan(out.plane.values[3]));
  out=nc_read_slice_decoded(file,"/wide",{2,2,2},{1,2,{0,0,0}});QVERIFY(out.error.isEmpty());QVERIFY(std::isfinite(out.plane.values[0]));QVERIFY(std::isnan(out.plane.values[1]));QVERIFY(out.precision_warning);
}
void HovmollerTest::plainHdf() {
  QTemporaryDir folder;const QString file=qEnvironmentVariable("HDFSTUDIO_HOVMOLLER_FIXTURE",folder.path()+"/plain.h5");
  hid_t creation=H5Pcreate(H5P_FILE_CREATE);H5Pset_userblock(creation,512);
  hid_t id=H5Fcreate(nc_path_bytes(file).constData(),H5F_ACC_TRUNC,creation,H5P_DEFAULT);H5Pclose(creation);QVERIFY(id>=0);
  hsize_t shape[3]={3,4,5};hid_t space=H5Screate_simple(3,shape,nullptr);hid_t variable=H5Dcreate2(id,"field",H5T_NATIVE_SHORT,space,H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
  short raw[60];for(int i=0;i<60;++i)raw[i]=short(i);raw[(1*4+2)*5+3]=-999;H5Dwrite(variable,H5T_NATIVE_SHORT,H5S_ALL,H5S_ALL,H5P_DEFAULT,raw);
  hid_t scalar=H5Screate(H5S_SCALAR);short fill=-999;double scale=.125,offset=17;
  auto attr=[&](const char *name,hid_t type,const void *data){hid_t attribute=H5Acreate2(variable,name,type,scalar,H5P_DEFAULT,H5P_DEFAULT);H5Awrite(attribute,type,data);H5Aclose(attribute);};
  attr("_FillValue",H5T_NATIVE_SHORT,&fill);attr("scale_factor",H5T_NATIVE_DOUBLE,&scale);attr("add_offset",H5T_NATIVE_DOUBLE,&offset);
  H5Sclose(scalar);H5Dclose(variable);H5Sclose(space);H5Fclose(id);
  auto out=nc_read_slice_decoded(file,"/field",{3,4,5},{1,0,{0,0,3}});QVERIFY2(out.error.isEmpty(),qPrintable(out.error));QCOMPARE(out.plane.rows,size_t(4));QCOMPARE(out.plane.cols,size_t(3));QVERIFY(std::isnan(out.plane.values[2*3+1]));QCOMPARE(out.plane.values[0],17.375);
}
void HovmollerTest::coordinatesAndNearest() {
  size_t index=99;QVERIFY(slice_nearest({3,2,1},2.5,index).isEmpty());QCOMPARE(index,size_t(0));QVERIFY(!slice_nearest({0,1,1},1,index).isEmpty());QVERIFY(!slice_nearest({0,1},2,index).isEmpty());
  QTemporaryDir folder;QString file=folder.path()+"/coords.nc";int id,dim[3],variable;
  nc_create(nc_path_bytes(file).constData(),NC_NETCDF4|NC_CLOBBER,&id);
  const char *names[]={"time","lat","lon"};const char *units[]={"days since 2000-01-01","degrees_north","degrees_east"};
  for(int axis=0;axis<3;++axis){nc_def_dim(id,names[axis],3,&dim[axis]);int coordinate;nc_def_var(id,names[axis],NC_DOUBLE,1,&dim[axis],&coordinate);nc_put_att_text(id,coordinate,"units",strlen(units[axis]),units[axis]);double values[3]={0,1,2};nc_put_var_double(id,coordinate,values);}
  nc_def_var(id,"field",NC_DOUBLE,3,dim,&variable);nc_close(id);
  const auto info=slice_inspect(file,"/field");QVERIFY(info.error.isEmpty());QCOMPARE(info.axes[0].role,SliceAxisRole::Time);QCOMPARE(info.axes[1].role,SliceAxisRole::Latitude);QCOMPARE(info.axes[2].role,SliceAxisRole::Longitude);
  QString error;QCOMPARE(slice_read_coordinate(file,info.axes[0],error).size(),size_t(3));QVERIFY(error.isEmpty());
}
static QString geographic_fixture(const QString &folder) {
  const QString path=folder+"/geographic.nc";int id,dimensions[3],variable,coordinates[3];
  if(nc_create(nc_path_bytes(path).constData(),NC_NETCDF4|NC_CLOBBER,&id)!=NC_NOERR)return {};
  const char *names[]={"time","latitude","longitude"};const size_t lengths[]={3,4,5};const char *units[]={"days since 2000-01-01","degrees_north","degrees_east"};
  for(int axis=0;axis<3;++axis){nc_def_dim(id,names[axis],lengths[axis],&dimensions[axis]);nc_def_var(id,names[axis],NC_DOUBLE,1,&dimensions[axis],&coordinates[axis]);nc_put_att_text(id,coordinates[axis],"units",strlen(units[axis]),units[axis]);}
  nc_def_var(id,"field",NC_DOUBLE,3,dimensions,&variable);const char *unit="K";nc_put_att_text(id,variable,"units",1,unit);nc_enddef(id);
  for(int axis=0;axis<3;++axis){std::vector<double> values(lengths[axis]);for(size_t i=0;i<values.size();++i)values[i]=axis==1?30.-i*10.:double(i);nc_put_var_double(id,coordinates[axis],values.data());}
  double values[60];for(int t=0;t<3;++t)for(int y=0;y<4;++y)for(int x=0;x<5;++x)values[(t*4+y)*5+x]=100*t+10*y+x;
  values[0]=NAN;nc_put_var_double(id,variable,values);nc_close(id);return path;
}
static QJsonObject worker_request(const QString &path,const SliceInfo &info,NcSliceSpec spec) {
  QJsonArray axes;for(size_t i=0;i<info.shape.size();++i)axes.append(int(i));auto metadata=slice_metadata(path,"/field",info,spec);
  return {{"schema",1},{"operation","read"},{"file",path},{"variable","/field"},{"source_stamp",slice_stamp(path)},{"request_id","12"},{"source_revision","1"},{"expected_shape",metadata["shape"]},{"spec",metadata["spec"]},{"coordinate_axes",axes}};
}
void HovmollerTest::workerProtocol(){
  QTemporaryDir folder;const auto source=geographic_fixture(folder.path());const auto info=slice_inspect(source,"/field");NcSliceSpec spec{1,0,{0,0,2}};
  auto run=[&](const QJsonObject &request,const QString &directory){QDir().mkpath(directory);QFile file(directory+"/request.json");if(!file.open(QIODevice::WriteOnly))return -1;file.write(QJsonDocument(request).toJson());file.close();QProcess process;process.start(QCoreApplication::applicationDirPath()+"/hdf_cli.exe",{"slice-worker",file.fileName()});if(!process.waitForFinished(10000))return -1;return process.exitCode();};
  auto request=worker_request(source,info,spec);const QString task=folder.path()+"/tmp-task";QCOMPARE(run(request,task),0);
  auto data=slice_load_result(task,12);QVERIFY2(data.error.isEmpty(),qPrintable(data.error));QCOMPARE(data.plane.rows,size_t(4));QCOMPARE(data.plane.cols,size_t(3));QCOMPARE(data.plane.values[2*3+1],122.);QCOMPARE(data.coordinates.at(1)[0],30.);
  QVERIFY(!slice_load_result(task,13).error.isEmpty());QFile binary(task+"/plane.bin");QVERIFY(binary.open(QIODevice::Append));binary.write("x");binary.close();QVERIFY(!slice_load_result(task,12).error.isEmpty());
  request["schema"]=2;QCOMPARE(run(request,folder.path()+"/tmp-bad-version"),2);request["schema"]=1;
  request["source_stamp"]=QJsonObject{{"size_bytes","1"},{"mtime_utc_ms","0"}};QCOMPARE(run(request,folder.path()+"/tmp-changed"),3);
}
void HovmollerTest::viewAxesMaskExport(){
  QTemporaryDir folder;HovmollerView view(geographic_fixture(folder.path()),"/field");view.resize(1050,800);view.show();
  QTRY_VERIFY_WITH_TIMEOUT(view.ready()||view.state()=="Error",10000);QVERIFY2(view.ready(),qPrintable(view.error()));
  auto snapshot=view.snapshot();QCOMPARE(snapshot->spec.row_axis,size_t(1));QCOMPARE(snapshot->spec.col_axis,size_t(0));QCOMPARE(snapshot->info.units,QString("K"));QCOMPARE(snapshot->image.size(),QSize(3,4));QCOMPARE(snapshot->missing,size_t(1));
  QCOMPARE(view.layer_axis_count(),1);QCOMPARE(view.layer_count(0),5);QCOMPARE(snapshot->image.pixel(0,0),QRgb(HdfColorMap::missing_color));
  QCOMPARE(snapshot->palette.id,HdfColorMap::default_palette_id);
  for(size_t row=0;row<4;++row)for(size_t col=0;col<3;++col)if(row||col)QCOMPARE((*snapshot->decoded)[row*3+col],double(100*col+10*row));
  const quint64 reads=view.read_count();auto *start=view.findChild<QLineEdit*>("hovmollerMaskStart"),*end=view.findChild<QLineEdit*>("hovmollerMaskEnd");start->setText("10");end->setText("120");view.findChild<QPushButton*>("hovmollerApply")->click();
  QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);snapshot=view.snapshot();QCOMPARE(view.read_count(),reads);QCOMPARE(snapshot->missing+snapshot->masked+snapshot->valid,size_t(12));QVERIFY(snapshot->masked>0);QCOMPARE(snapshot->image.pixel(0,1),QRgb(HdfColorMap::masked_color));
  view.findChild<QCheckBox*>("hovmollerFlipH")->setChecked(true);QVERIFY(view.export_png(folder.path()+"/section.png").isEmpty());QVERIFY(view.export_csv(folder.path()+"/section.csv").isEmpty());
  QImage png(folder.path()+"/section.png");QCOMPARE(png.size(),QSize(3,4));QVERIFY(!png.text("HovmollerMetadata").isEmpty());QCOMPARE(png.pixel(2,0),QRgb(HdfColorMap::missing_color));
  QVERIFY(!view.export_png(snapshot->file,true).isEmpty());
  start->setText("500");end->setText("100");view.findChild<QPushButton*>("hovmollerApply")->click();QCOMPARE(view.state(),QString("Error"));QVERIFY(!view.snapshot());
  start->clear();end->clear();view.findChild<QPushButton*>("hovmollerApply")->click();QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);QCOMPARE(view.read_count(),reads);
  view.set_axes(0,2);QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);snapshot=view.snapshot();QCOMPARE(snapshot->image.size(),QSize(5,3));QCOMPARE(view.layer_axis_name(0),QString("latitude"));
  view.set_layer_index(0,2);QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);QCOMPARE((*view.snapshot()->decoded)[1*5+3],123.);
  view.set_axes(2,0);QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);QCOMPARE(view.snapshot()->image.size(),QSize(3,5));QCOMPARE((*view.snapshot()->decoded)[3*3+1],123.);
}
void HovmollerTest::viewLatestAndPlayback(){
  QTemporaryDir folder;HovmollerView view(geographic_fixture(folder.path()),"/field");QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);
  for(int i=0;i<20;++i)view.set_layer_index(0,i%5);view.set_layer_index(0,3);QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);QCOMPARE(view.layer_index(0),3);QCOMPARE((*view.snapshot()->decoded)[0],3.);
  LayerPlayback playback;playback.set_interval_ms(100);playback.set_ready_check([&view]{return view.ready();});QSignalSpy frames(&view,&HovmollerView::ready_changed);QVERIFY(playback.start(&view,0));
  QTRY_VERIFY_WITH_TIMEOUT(!playback.is_playing(),10000);QVERIFY(view.ready());QCOMPARE(view.layer_index(0),4);
  QVERIFY(playback.start(&view,0));QCOMPARE(view.layer_index(0),0);QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);playback.stop();
  view.set_layer_index(0,2);view.cancel();QCOMPARE(view.state(),QString("Cancelled"));QTRY_VERIFY_WITH_TIMEOUT(!SliceJobController::any_busy(),5000);QVERIFY(!view.snapshot());
}
void HovmollerTest::viewRankTwoAndBudget(){
  QTemporaryDir folder;HovmollerView view(fixture(folder.path(),NC_NETCDF4,{3,4}),"/field");QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);QCOMPARE(view.layer_axis_count(),0);QCOMPARE(view.snapshot()->image.size(),QSize(4,3));
  LayerPlayback playback;QVERIFY(!playback.start(&view,0));
  const auto prior=qgetenv("HDFSTUDIO_RASTER_BUDGET_MIB");qputenv("HDFSTUDIO_RASTER_BUDGET_MIB","1");{HovmollerView denied(view.snapshot()->file,"/field");QTRY_COMPARE_WITH_TIMEOUT(denied.state(),QString("Error"),5000);QVERIFY(denied.error().contains("MemoryBudgetExceeded"));}if(prior.isNull())qunsetenv("HDFSTUDIO_RASTER_BUDGET_MIB");else qputenv("HDFSTUDIO_RASTER_BUDGET_MIB",prior);
}
void HovmollerTest::viewCloseLifecycle(){
  QTemporaryDir folder;const auto file=geographic_fixture(folder.path());for(int i=0;i<6;++i){auto *view=new HovmollerView(file,"/field");if(i%2)QTRY_VERIFY_WITH_TIMEOUT(view->ready(),10000);else QTest::qWait(2);view->set_layer_index(0,1);delete view;}
  QTRY_VERIFY_WITH_TIMEOUT(!SliceJobController::any_busy(),5000);
  const auto prior=qgetenv("HDFSTUDIO_RASTER_BUDGET_MIB");qputenv("HDFSTUDIO_RASTER_BUDGET_MIB","1");auto entire=reserve_raster_bytes(1024*1024);QVERIFY(entire);QVERIFY(!reserve_raster_bytes(1));entire.reset();if(prior.isNull())qunsetenv("HDFSTUDIO_RASTER_BUDGET_MIB");else qputenv("HDFSTUDIO_RASTER_BUDGET_MIB",prior);
}
void HovmollerTest::mainWindowIntegration(){
  QTemporaryDir folder;const auto file=geographic_fixture(folder.path());auto *window=new MainWindow;window->show();QCOMPARE(window->read_file(file),0);
  auto *tree=window->findChild<FileTreeWidget*>();QVERIFY(tree);const auto targets=tree->findItems("field",Qt::MatchExactly|Qt::MatchRecursive);QCOMPARE(targets.size(),1);tree->setCurrentItem(targets.first());
  auto *action=window->findChild<QAction*>("actionSpaceTimeSection");QVERIFY(action);action->trigger();auto views=window->findChildren<HovmollerView*>();QCOMPARE(views.size(),1);auto *view=views[0];QTRY_VERIFY_WITH_TIMEOUT(view->ready(),10000);QTRY_VERIFY_WITH_TIMEOUT(window->findChild<QComboBox*>("layerAxisCombo")->count()==1,3000);
  view->set_layer_index(0,1);delete window;QTRY_VERIFY_WITH_TIMEOUT(!SliceJobController::any_busy(),5000);
}
void HovmollerTest::businessSamples(){
  const QString file=qEnvironmentVariable("HDFSTUDIO_FORECAST_ANCHOR");if(file.isEmpty())return;
  HovmollerView view(file,"/TMP_2maboveground");QTRY_VERIFY_WITH_TIMEOUT(view.ready()||view.state()=="Error",20000);QVERIFY2(view.ready(),qPrintable(view.error()));QCOMPARE(view.snapshot()->image.size(),QSize(480,190));
  const QString directory=qEnvironmentVariable("HDFSTUDIO_HOVMOLLER_EXPORT");if(!directory.isEmpty()){QDir().mkpath(directory);QVERIFY(view.export_csv(directory+"/lat-time.csv").isEmpty());QVERIFY(view.export_png(directory+"/lat-time.png").isEmpty());}
  view.set_axes(0,2);QTRY_VERIFY_WITH_TIMEOUT(view.ready(),20000);QCOMPARE(view.snapshot()->image.size(),QSize(384,480));
  if(!directory.isEmpty()){QVERIFY(view.export_csv(directory+"/lon-time.csv").isEmpty());QVERIFY(view.export_png(directory+"/lon-time.png").isEmpty());}
}
void HovmollerTest::datesAndNumericFallback(){
  const auto python=qgetenv("HDFSTUDIO_PYTHON");
  QTemporaryDir folder;auto file=geographic_fixture(folder.path());
  {qputenv("HDFSTUDIO_PYTHON","nonexistent-hovmoller-python.exe");HovmollerView view(file,"/field");QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);auto *status=view.findChild<QLabel*>("hovmollerDateStatus");QTRY_VERIFY_WITH_TIMEOUT(status->text().contains("unavailable"),7000);QCOMPARE(status->accessibleName(),status->text());QVERIFY(view.ready());}
  if(python.isNull())qunsetenv("HDFSTUDIO_PYTHON");else qputenv("HDFSTUDIO_PYTHON",python);
  if(!python.isEmpty()){HovmollerView view(file,"/field");view.resize(1100,850);view.show();QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);QTRY_VERIFY_WITH_TIMEOUT(view.findChild<QLabel*>("hovmollerDateStatus")->text().contains("labels available"),7000);view.set_axes(1,2);QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);auto *fixed=view.findChild<QLineEdit*>("hovmollerFixedIndex_0");QVERIFY(fixed);QTRY_VERIFY_WITH_TIMEOUT([&]{for(auto *label:view.findChildren<QLabel*>())if(label->text().contains("2000-01-01 00:00:00"))return true;return false;}(),7000);}
  QTRY_VERIFY_WITH_TIMEOUT(!SectionDates::any_busy(),5000);
}
void HovmollerTest::largeFixedAxisAndSourceChange(){
  QTemporaryDir folder;const QString file=folder.path()+"/huge-fixed.nc";int id,dims[3],variable;
  nc_create(nc_path_bytes(file).constData(),NC_NETCDF4|NC_CLOBBER,&id);nc_def_dim(id,"rows",2,&dims[0]);nc_def_dim(id,"cols",3,&dims[1]);nc_def_dim(id,"huge",size_t(INT_MAX)+1,&dims[2]);nc_def_var(id,"field",NC_DOUBLE,3,dims,&variable);size_t chunks[]={2,3,1};nc_def_var_chunking(id,variable,NC_CHUNKED,chunks);nc_enddef(id);const size_t starts[]={0,0,0},counts[]={2,3,1};double values[]={1,2,3,4,5,6};nc_put_vara_double(id,variable,starts,counts,values);nc_close(id);
  {HovmollerView view(file,"/field");QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);QCOMPARE(view.layer_axis_count(),1);QCOMPARE(view.layer_count(0),0);QCOMPARE(view.snapshot()->info.shape[2],hsize_t(INT_MAX)+1);QVERIFY(!view.findChild<QLineEdit*>("hovmollerFixedIndex_2")->text().isEmpty());}
  const auto source=geographic_fixture(folder.path());HovmollerView view(source,"/field");QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);
  QFile change(source);QVERIFY(change.open(QIODevice::Append));change.write("x");change.close();view.set_layer_index(0,1);QCOMPARE(view.state(),QString("Error"));QVERIFY(view.error().contains("SourceChanged"));QVERIFY(!view.snapshot());
}
void HovmollerTest::missingMaskAndProbe(){
  QTemporaryDir folder;const auto file=geographic_fixture(folder.path());HovmollerView view(file,"/field");view.resize(1100,850);view.show();QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);
  auto *graphics=view.findChild<QGraphicsView*>("hovmollerCanvas");auto *canvas=view.findChild<SectionCanvas*>();QSignalSpy probe(canvas,&SectionCanvas::probe_changed);
  auto move=[&](QPointF scene){const QPointF local=graphics->mapFromScene(scene);QMouseEvent event(QEvent::MouseMove,local,graphics->viewport()->mapToGlobal(local.toPoint()),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(graphics->viewport(),&event);};
  move(QPointF(1.5,2.5));QVERIFY(!probe.isEmpty());QCOMPARE(probe.last()[0].value<size_t>(),size_t(2));QCOMPARE(probe.last()[1].value<size_t>(),size_t(1));
  const auto probe_label=view.findChild<QLabel*>("hovmollerProbe");QCOMPARE(probe_label->accessibleName(),probe_label->text());
  view.findChild<QCheckBox*>("hovmollerFlipH")->setChecked(true);move(QPointF(.5,2.5));QCOMPARE(probe.last()[1].value<size_t>(),size_t(2));
  canvas->set_date_labels({{"0:2","2000-01-03 00:00:00"}});QVERIFY(view.findChild<QLabel*>("hovmollerProbe")->text().contains("2000-01-03"));const auto all=canvas->visible_indices(0);QVERIFY(!all.empty());canvas->zoom(3);move(QPointF(.5,2.5));QCOMPARE(probe.last()[1].value<size_t>(),size_t(2));
  const auto reads=view.read_count();view.findChild<QLineEdit*>("hovmollerMaskStart")->setText("-100000");view.findChild<QLineEdit*>("hovmollerMaskEnd")->setText("100000");view.findChild<QPushButton*>("hovmollerApply")->click();QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);QCOMPARE(view.snapshot()->valid,size_t(0));QCOMPARE(view.snapshot()->masked,size_t(11));QCOMPARE(view.snapshot()->missing,size_t(1));QVERIFY(!view.snapshot()->has_range);QCOMPARE(view.read_count(),reads);
  auto *range=view.findChild<QComboBox*>("hovmollerRangeMode");range->setCurrentIndex(1);view.findChild<QLineEdit*>("hovmollerMinimum")->setText("1e-12");view.findChild<QLineEdit*>("hovmollerMaximum")->setText("2e-12");view.findChild<QPushButton*>("hovmollerApply")->click();QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);QCOMPARE(view.snapshot()->mapping.minimum,1e-12);
}
void HovmollerTest::canvasFillsShortSections(){
  QTemporaryDir folder;
  for(const auto &shape:std::vector<std::vector<size_t>>{{40,2},{2,40},{40,1},{1,40}}){
    const auto file=fixture(folder.path(),NC_NETCDF4,shape);
    if(shape==std::vector<size_t>{40,2}){int id,variable;QCOMPARE(nc_open(nc_path_bytes(file).constData(),NC_WRITE,&id),NC_NOERR);nc_inq_varid(id,"field",&variable);std::vector<double> contrast(80);for(size_t i=0;i<contrast.size();++i)contrast[i]=i%2?100.:0.;nc_put_var_double(id,variable,contrast.data());nc_close(id);}
    HovmollerView view(file,"/field");view.resize(1100,800);view.show();
    QTRY_VERIFY_WITH_TIMEOUT(view.ready(),10000);const auto original=view.snapshot();const auto values=*original->decoded;const auto reads=view.read_count();
    auto *canvas=view.findChild<SectionCanvas*>();auto *graphics=canvas->view();
    auto filled=[&]{const auto rect=graphics->mapFromScene(QRectF(0,0,shape[1],shape[0])).boundingRect();return std::abs(rect.width()-graphics->viewport()->width())<=6&&std::abs(rect.height()-graphics->viewport()->height())<=6;};
    QTRY_VERIFY(filled());QVERIFY(graphics->transform().m11()!=graphics->transform().m22());
    QTest::qWait(150);QVERIFY(graphics->viewport()->hasMouseTracking());
    QGraphicsPixmapItem *item=nullptr;for(auto *entry:graphics->scene()->items())if(auto *pixmap=qgraphicsitem_cast<QGraphicsPixmapItem*>(entry))item=pixmap;
    QVERIFY(item);QCOMPARE(item->transformationMode(),Qt::SmoothTransformation);
    view.resize(1300,900);QTRY_VERIFY(filled());view.resize(950,700);QTRY_VERIFY(filled());
    canvas->zoom(2);QVERIFY(!filled());view.findChild<QPushButton*>("hovmollerFit")->click();QTRY_VERIFY(filled());
    QSignalSpy probe(canvas,&SectionCanvas::probe_changed);const auto local=graphics->mapFromScene(QPointF(shape[1]-.5,shape[0]-.5));
    QMouseEvent event(QEvent::MouseMove,local,graphics->viewport()->mapToGlobal(local),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(graphics->viewport(),&event);
    QVERIFY(!probe.isEmpty());QCOMPARE(probe.last()[0].value<size_t>(),shape[0]-1);QCOMPARE(probe.last()[1].value<size_t>(),shape[1]-1);
    QCOMPARE(*view.snapshot()->decoded,values);QCOMPARE(view.read_count(),reads);QCOMPARE(view.snapshot()->image,original->image);
    const auto path=folder.path()+"/native-"+QString::number(shape[0])+"-"+QString::number(shape[1])+".png";QVERIFY(view.export_png(path).isEmpty());QCOMPARE(QImage(path).size(),QSize(int(shape[1]),int(shape[0])));
    // 数值仍在原网格；屏幕两端之间出现平滑过渡色，而非两块放大的像素。
    if(shape==std::vector<size_t>{40,2}){
      const auto screenshot=graphics->viewport()->grab().toImage();const auto position=graphics->mapFromScene(QPointF(1.,10.5));
      const auto pixel=screenshot.pixelColor(position);const auto left=original->image.pixelColor(0,10),right=original->image.pixelColor(1,10);
      QVERIFY(pixel!=left&&pixel!=right);
    }
    if(const auto evidence=qEnvironmentVariable("HDFSTUDIO_GRIB_EVIDENCE");!evidence.isEmpty())QVERIFY(view.grab().save(evidence+QString("/short-%1x%2.png").arg(shape[0]).arg(shape[1])));
  }
}
void HovmollerTest::heartbeatAndCancel(){
  QTemporaryDir folder;const auto file=fixture(folder.path(),NC_NETCDF4,{2,1000,1000});
  HovmollerView view(file,"/field");QElapsedTimer clock;clock.start();qint64 previous=0,worst=0;QTimer pulse;pulse.setInterval(50);connect(&pulse,&QTimer::timeout,this,[&]{const auto current=clock.elapsed();if(previous)worst=std::max(worst,current-previous);previous=current;});pulse.start();
  QTRY_VERIFY_WITH_TIMEOUT(view.ready(),20000);view.set_axes(1,2);QTRY_VERIFY_WITH_TIMEOUT(view.ready(),20000);QVERIFY2(worst<=250,qPrintable(QString("Worst isolated GUI heartbeat %1 ms").arg(worst)));
  view.set_layer_index(0,1);clock.restart();view.cancel();QCOMPARE(view.state(),QString("Cancelled"));QVERIFY(clock.elapsed()<250);QTRY_VERIFY_WITH_TIMEOUT(!SliceJobController::any_busy(),2000);
  qInfo()<<"GUI heartbeat worst ms"<<worst;
}
void HovmollerTest::corruptProtocolRejected(){
  QTemporaryDir folder;const auto source=geographic_fixture(folder.path());const auto info=slice_inspect(source,"/field");const auto request=worker_request(source,info,{1,0,{0,0,2}});
  QFile input(folder.path()+"/request.json");QVERIFY(input.open(QIODevice::WriteOnly));input.write(QJsonDocument(request).toJson());input.close();QProcess worker;worker.start(QCoreApplication::applicationDirPath()+"/hdf_cli.exe",{"slice-worker",input.fileName()});QVERIFY(worker.waitForFinished(10000));QCOMPARE(worker.exitCode(),0);
  QFile json(folder.path()+"/result.json"),binary(folder.path()+"/plane.bin");QVERIFY(json.open(QIODevice::ReadOnly));const auto original=json.readAll();json.close();QVERIFY(binary.open(QIODevice::ReadOnly));const auto bytes=binary.readAll();binary.close();
  auto write=[&](QFile &file,const QByteArray &content){if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate))return false;const bool ok=file.write(content)==content.size();file.close();return ok;};
  for(int variant=0;variant<8;++variant){auto object=QJsonDocument::fromJson(original).object();
    if(variant==0)object["source_revision"]=1;
    if(variant==1)object["decode_profile"]="unknown";
    if(variant==2)object.remove("coordinate_files");
    if(variant==3)object["data_file"]="../plane.bin";
    if(variant==4)object["rows"]="999";
    if(variant==5){auto spec=object["spec"].toObject();spec["col_axis"]=.5;object["spec"]=spec;}
    if(variant==6){auto axes=object["coordinate_files"].toArray();auto axis=axes[0].toObject();axis["file"]="../axis-0.f64";axes[0]=axis;object["coordinate_files"]=axes;}
    if(variant==7){auto axes=object["axes"].toArray();auto axis=axes[0].toObject();axis["length"]="999";axes[0]=axis;object["axes"]=axes;}
    QVERIFY(write(json,QJsonDocument(object).toJson()));QVERIFY2(!slice_load_result(folder.path(),12).error.isEmpty(),qPrintable(QString::number(variant)));
  }
  QVERIFY(write(json,original));
  for(int variant=0;variant<5;++variant){auto corrupt=bytes;if(variant==0)corrupt[0]='X';if(variant==1)corrupt[8]=2;if(variant==2)corrupt[12]=1;if(variant==3)corrupt.chop(1);if(variant==4)corrupt.append('x');QVERIFY(write(binary,corrupt));QVERIFY(!slice_load_result(folder.path(),12).error.isEmpty());}
  QVERIFY(write(binary,bytes));QVERIFY(slice_load_result(folder.path(),12).error.isEmpty());
}
void HovmollerTest::largeToolbarAndMissingPlane(){
  QTemporaryDir folder;const auto file=fixture(folder.path(),NC_NETCDF4,{2,3,10001});MainWindow window;window.show();QCOMPARE(window.read_file(file),0);auto *tree=window.findChild<FileTreeWidget*>();tree->setCurrentItem(tree->findItems("field",Qt::MatchExactly|Qt::MatchRecursive).first());window.findChild<QAction*>("actionSpaceTimeSection")->trigger();auto *view=window.findChild<HovmollerView*>();QVERIFY(view);QTRY_VERIFY_WITH_TIMEOUT(view->ready(),10000);auto *indices=window.findChild<QComboBox*>("layerIndexCombo");QVERIFY(indices);QTRY_COMPARE_WITH_TIMEOUT(indices->count(),0,3000);QVERIFY(!indices->isEnabled());QCOMPARE(view->layer_count(0),10001);view->set_layer_index(0,10000);QTRY_VERIFY_WITH_TIMEOUT(view->ready(),10000);QCOMPARE(view->layer_index(0),10000);
  const auto missing=geographic_fixture(folder.path());int id,variable;QCOMPARE(nc_open(nc_path_bytes(missing).constData(),NC_WRITE,&id),NC_NOERR);nc_inq_varid(id,"field",&variable);std::vector<double> values(60,NAN);nc_put_var_double(id,variable,values.data());nc_close(id);HovmollerView empty(missing,"/field");QTRY_VERIFY_WITH_TIMEOUT(empty.ready(),10000);QCOMPARE(empty.snapshot()->valid,size_t(0));QCOMPARE(empty.snapshot()->missing,size_t(12));QCOMPARE(empty.snapshot()->masked,size_t(0));QVERIFY(!empty.snapshot()->has_range);QCOMPARE(empty.snapshot()->image.pixel(0,0),QRgb(HdfColorMap::missing_color));QVERIFY(empty.export_csv(folder.path()+"/missing.csv").isEmpty());
}
void HovmollerTest::cleanup(){
  if(QTest::currentTestFailed()){
    const auto folder=qEnvironmentVariable("HDFSTUDIO_TEST_LOG_DIR",QDir::tempPath()+"/HDFStudio-test-failures")+"/hovmoller-"+QTest::currentTestFunction();QDir().mkpath(folder);
    if(auto *screen=QGuiApplication::primaryScreen())screen->grabWindow(0).save(folder+"/desktop.png");QFile list(folder+"/widgets.txt");if(list.open(QIODevice::WriteOnly)){for(auto *widget:QApplication::allWidgets())list.write((widget->objectName()+" | "+widget->metaObject()->className()+" | "+widget->windowTitle()+"\n").toUtf8());}
  }
}
QTEST_MAIN(HovmollerTest)
#include "hovmoller_test.moc"
