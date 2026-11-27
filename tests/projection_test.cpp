// PROJ-001 targeted tests. Optional C7F fixture is read-only via HDFSTUDIO_GRIB1_SAMPLE.
#include <QtTest>
#include <QtWidgets>
#include <QScopeGuard>
#include <netcdf.h>
#include "map_projection.hpp"
#include "geo_grid.hpp"
#include "map_render.hpp"
#include "map_display.hpp"
#include "raster_view.hpp"
#include "geo_grid_controller.hpp"
#include "read_scheduler.hpp"
#include "raster_budget.hpp"
#include "grib/grib_data.hpp"
#include "hdf_explorer.hpp"
#include "layer_playback.hpp"
#include "profile_view.hpp"
#include <eccodes.h>
#include <cmath>

namespace {
using namespace MapProjection;
constexpr double pi=3.14159265358979323846;
EarthModel sphere(){return {6371000,6371000,"test sphere"};}
void nc_ok(int code){if(code!=NC_NOERR)qFatal("NC fixture: %s",nc_strerror(code));}
void fixture(const QString &path,bool model=true,bool packed=false,bool grouped=false,bool conflict=false){
  int root,group,dy,dx,y,x,value,mapping;nc_ok(nc_create(path.toLocal8Bit().constData(),grouped?NC_NETCDF4:NC_CLOBBER,&root));group=root;if(grouped)nc_ok(nc_def_grp(root,"forecast",&group));
  nc_ok(nc_def_dim(group,"lat",3,&dy));nc_ok(nc_def_dim(group,"lon",5,&dx));nc_ok(nc_def_var(group,"lat",packed?NC_SHORT:NC_DOUBLE,1,&dy,&y));nc_ok(nc_def_var(group,"lon",NC_DOUBLE,1,&dx,&x));const int dims[]={dy,dx};nc_ok(nc_def_var(group,"t",NC_DOUBLE,2,dims,&value));
  nc_ok(nc_put_att_text(group,y,"units",7,"degreeN"));nc_ok(nc_put_att_text(group,x,"units",12,"degrees_east"));
  if(conflict)nc_ok(nc_put_att_text(group,y,"standard_name",13,"grid_latitude"));
  if(packed){double scale=.01;nc_ok(nc_put_att_double(group,y,"scale_factor",NC_DOUBLE,1,&scale));}
  if(model){nc_ok(nc_def_var(group,"earth",NC_INT,0,nullptr,&mapping));nc_ok(nc_put_att_text(group,value,"grid_mapping",5,"earth"));nc_ok(nc_put_att_text(group,mapping,"grid_mapping_name",18,"latitude_longitude"));double radius=6371000;nc_ok(nc_put_att_double(group,mapping,"earth_radius",NC_DOUBLE,1,&radius));}
  nc_ok(nc_enddef(root));const double lat[]={packed? -6000.:-60.,0,packed?6000.:60.},lon[]={0,90,180,270,360};double values[15];for(int i=0;i<15;++i)values[i]=i;for(int r=0;r<3;++r)values[r*5+4]=values[r*5];nc_ok(nc_put_var_double(group,y,lat));nc_ok(nc_put_var_double(group,x,lon));nc_ok(nc_put_var_double(group,value,values));nc_ok(nc_close(root));
}
MapRender::Input input(){MapRender::Input in;auto g=std::make_shared<GeoGrid::Grid>();g->latitude.centers={-60,0,60};g->longitude.centers={0,90,180,270};g->earth=sphere();GeoGrid::validate(*g);in.grid=g;
  auto values=std::make_shared<std::vector<double>>();for(int i=0;i<12;++i)values->push_back(i);in.values=values;in.original=values;in.source=QImage(4,3,QImage::Format_RGB32);for(int r=0;r<3;++r)for(int c=0;c<4;++c)in.source.setPixel(c,r,qRgb(r*70,c*60,10));in.spec=preset(Mode::LonLat);QString error;in.viewport=fit(in.spec,360,180,1,error);return in;}
QPushButton *ok_button(QWidget &parent){return parent.findChild<QPushButton*>("projectionOk");}
}
class ProjectionTest:public QObject {
  Q_OBJECT
  std::unique_ptr<QTemporaryDir> settings_directory;
  QByteArray previous_settings_file;
private slots:
  void init();
  void cleanup();
  void pointTransformsIndependentSphere();
  void ellipsoidMercatorAndErrors();
  void extentAndParameterValidation();
  void gridCoverageTieBoundsAndPeriodicity();
  void netcdfCoordinatesPackingGroupsAndUnknownModel();
  void cliIdentityAndLifecycle();
  void samplingStatesAndPixelIndices();
  void coastAssetIntegrityAndSeams();
  void guiModesSourcePngMaskAndLatest();
  void guiBudgetCancelAndDirectDestruction();
  void gribAlignmentAndRealSample();
  void ellipsoidPolarLambertIndependent();
  void viewportDprClipAndGeometryReuse();
  void declaredBoundsAndModelConflicts();
  void boundsLookupAgainstLinearReference();
  void gribScanCoordinateValueAlignment();
  void gribGaussianCoordinatesAndScanAlignment();
  void guiGaussianRealSourceModes();
  void guiMaskProbeLatestLayersAndClose();
  void cliRejectsStaleAndMalformedRequests();
  void optionalCoastFailureKeepsRaster();
  void deletedMapTaskRetainsAndReleasesBudget();
  void guiDialogSelectionChangesCanvasWithoutApply();
  void guiVisibleSelectorRedrawsNetcdfAndGrib();
  void guiSingleRowMouseSelectionUnknownSource();
  void guiRetainsCompletedMapUntilReplacement();
  void guiPlaybackWaitsForProjectedFrame();
  void guiSelectorLatestAndVisibleRejection();
  void guiSettingsOkCancelKeyboardAndRollback();
  void preferencesPersistConfirmAndRestoreFreshProcess();
  void preferencesInvalidAndUnwritableRemainVisible();
  void preferencesRestoreChildSession();
  void guiProfileFromProjectedView_data();
  void guiProfileFromProjectedView();
};
void ProjectionTest::init(){
  previous_settings_file=qgetenv("HDFSTUDIO_SETTINGS_FILE");
  if(!qEnvironmentVariableIsEmpty("HDFSTUDIO_PREF_CHILD"))return;
  settings_directory=std::make_unique<QTemporaryDir>();QVERIFY(settings_directory->isValid());
  qputenv("HDFSTUDIO_SETTINGS_FILE",settings_directory->filePath("HDFStudio.ini").toUtf8());
}
void ProjectionTest::cleanup(){
  if(previous_settings_file.isNull())qunsetenv("HDFSTUDIO_SETTINGS_FILE");else qputenv("HDFSTUDIO_SETTINGS_FILE",previous_settings_file);
  settings_directory.reset();
}
void ProjectionTest::pointTransformsIndependentSphere(){
  for(auto mode:{Mode::NorthPolar,Mode::SouthPolar,Mode::Lambert,Mode::Mercator}){auto s=preset(mode,sphere());Transform transform(s);QVERIFY2(transform.error().isEmpty(),qPrintable(transform.error()));
    const double sign=mode==Mode::SouthPolar?-1.:1.;const double lon=mode==Mode::Lambert?120:30,lat=sign*50;QPointF actual;QVERIFY(transform.forward(lon,lat,actual));
    const double phi=lat*pi/180,lambda=(lon-s.lon0)*pi/180,R=s.earth.a;QPointF reference;
    if(mode==Mode::NorthPolar||mode==Mode::SouthPolar){const double rho=R*(1+std::sin(std::abs(s.lat_ts)*pi/180))*std::tan(pi/4-std::abs(phi)/2);reference={rho*std::sin(lambda),-sign*rho*std::cos(lambda)};}
    if(mode==Mode::Mercator)reference={R*lambda,R*std::log(std::tan(pi/4+phi/2))};
    if(mode==Mode::Lambert){const double p1=s.lat1*pi/180,p2=s.lat2*pi/180,p0=s.lat0*pi/180;const double n=std::log(std::cos(p1)/std::cos(p2))/std::log(std::tan(pi/4+p2/2)/std::tan(pi/4+p1/2));const double F=std::cos(p1)*std::pow(std::tan(pi/4+p1/2),n)/n;const double rho=R*F/std::pow(std::tan(pi/4+phi/2),n),rho0=R*F/std::pow(std::tan(pi/4+p0/2),n);reference={rho*std::sin(n*lambda),rho0-rho*std::cos(n*lambda)};}
    QVERIFY2(QLineF(actual,reference).length()<.001,qPrintable(QString("%1: %2").arg(name(mode)).arg(QLineF(actual,reference).length(),0,'g',16)));
    double back_lon=0,back_lat=0;QVERIFY(transform.inverse(actual,back_lon,back_lat));QVERIFY(std::abs(std::remainder(back_lon-lon,360))<1e-8);QVERIFY(std::abs(back_lat-lat)<1e-8);
    if(mode==Mode::NorthPolar||mode==Mode::SouthPolar){QPointF pole;QVERIFY(transform.forward(s.lon0,s.lat0,pole));QVERIFY(std::hypot(pole.x(),pole.y())<.001);}
  }
}
void ProjectionTest::guiDialogSelectionChangesCanvasWithoutApply(){
  QTemporaryDir directory;const auto file=directory.filePath("selection.nc");fixture(file);
  RasterView raster(file.toStdString(),"/t",{3,5});raster.resize(900,700);raster.show();
  QTRY_VERIFY(raster.plane_snapshot().values);raster.findChild<QAction*>("projsAction")->trigger();
  QTRY_VERIFY_WITH_TIMEOUT(ok_button(raster),15000);
  auto *mode=raster.findChild<QComboBox*>("projectionMode");auto *map=raster.findChild<MapDisplay*>();QVERIFY(mode&&map);
  mode->setCurrentIndex(int(Mode::NorthPolar));
  // User changes the dropdown only: no hidden second Apply action.
  QTRY_VERIFY_WITH_TIMEOUT(map->result()&&map->isVisible(),3000);
  const auto north=map->result()->image;
  mode->setCurrentIndex(int(Mode::SouthPolar));
  QTRY_VERIFY_WITH_TIMEOUT(map->result()&&map->isVisible()&&map->result()->image!=north,5000);
}
void ProjectionTest::guiRetainsCompletedMapUntilReplacement(){
  QTRY_VERIFY_WITH_TIMEOUT(!MapDisplay::any_busy(),15000);
  MapDisplay map;map.setProperty("externalStatus",true);map.resize(640,400);map.show();
  auto in=input();in.source.fill(Qt::red);map.set_grid(in.grid,{});map.set_source(in.source,in.values,in.original,{});
  QVERIFY(map.select_projection(Mode::LonLat).isEmpty());QTRY_VERIFY_WITH_TIMEOUT(map.result()&&!map.busy(),15000);
  const auto painted=map.grab().toImage();
  auto probe=[&]{QMouseEvent move(QEvent::MouseMove,QPointF(320,200),QPointF(320,200),Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&map,&move);};
  probe();QVERIFY(!map.property("mapProbe").toString().isEmpty());
  auto *pool=QThreadPool::globalInstance();const int threads=pool->maxThreadCount();pool->setMaxThreadCount(1);
  auto gate=std::make_shared<QSemaphore>(),entered=std::make_shared<QSemaphore>();pool->start([gate,entered]{entered->release();gate->acquire();});
  const auto release=qScopeGuard([&]{gate->release();pool->waitForDone(15000);pool->setMaxThreadCount(threads);});
  QTRY_VERIFY_WITH_TIMEOUT(entered->available(),15000);
  // Reading and recolouring invalidate values, not the last completed bitmap or its overlays.
  map.hold_frame("Reading requested layer (previous image; values unavailable)");
  QVERIFY(!map.result());probe();QVERIFY(map.property("mapProbe").toString().isEmpty());QCOMPARE(map.grab().toImage(),painted);
  in.source.fill(Qt::blue);auto next_values=std::make_shared<std::vector<double>>(12,123.);
  map.set_source(in.source,next_values,next_values,{});QVERIFY(map.busy());QVERIFY(!map.result());
  QTest::qWait(80);QCOMPARE(map.grab().toImage(),painted);probe();QVERIFY(map.property("mapProbe").toString().isEmpty());
  // New projection parameters must not relabel the retained old image before replacement.
  QVERIFY(map.select_projection(Mode::NorthPolar).isEmpty());QCOMPARE(map.grab().toImage(),painted);
  gate->release();QTRY_VERIFY_WITH_TIMEOUT(map.result()&&!map.busy(),15000);
  QVERIFY(map.grab().toImage()!=painted);QVERIFY(map.result()->covered>0);
  for(size_t pixel=0;pixel<map.result()->indices.size();++pixel)if(map.result()->indices[pixel]>=0)QCOMPARE(map.result()->image.pixel(int(pixel%map.result()->viewport.width),int(pixel/map.result()->viewport.width)),qRgb(0,0,255));
  // True failures clear held pixels, cancel pending work and cannot publish a stale completion.
  entered=std::make_shared<QSemaphore>();pool->start([gate,entered]{entered->release();gate->acquire();});QTRY_VERIFY_WITH_TIMEOUT(entered->available(),15000);
  in.source.fill(Qt::green);map.set_source(in.source,in.values,in.original,{});QTest::qWait(80);
  map.invalidate("SourceChanged: reopen source");QVERIFY(!map.result());const auto cleared=map.grab().toImage();QCOMPARE(cleared.pixelColor(320,200),QColor("#f7f7f7"));
  gate->release();QTRY_VERIFY_WITH_TIMEOUT(!map.busy(),15000);QVERIFY(!map.result());QCOMPARE(map.grab().toImage(),cleared);
}
void ProjectionTest::guiPlaybackWaitsForProjectedFrame(){
  QTemporaryDir directory;const auto file=directory.filePath("play-map.nc");fixture(file);
  int root,time,lat,lon,var;nc_ok(nc_open(file.toLocal8Bit().constData(),NC_WRITE,&root));nc_ok(nc_redef(root));nc_ok(nc_def_dim(root,"time",4,&time));nc_ok(nc_inq_dimid(root,"lat",&lat));nc_ok(nc_inq_dimid(root,"lon",&lon));
  const int dims[]={time,lat,lon};nc_ok(nc_def_var(root,"frames",NC_DOUBLE,3,dims,&var));nc_ok(nc_put_att_text(root,var,"grid_mapping",5,"earth"));nc_ok(nc_enddef(root));
  double values[60];for(int layer=0;layer<4;++layer)for(int i=0;i<15;++i)values[layer*15+i]=100*layer+((i%5==4?i-4:i)+layer*3)%15;
  nc_ok(nc_put_var_double(root,var,values));nc_ok(nc_close(root));
  MainWindow window;window.resize(1000,760);window.show();auto *mdi=window.findChild<QMdiArea*>();
  auto *raster=new RasterView(file.toStdString(),"/frames",{4,3,5});auto *sub=mdi->addSubWindow(raster);sub->show();
  QTRY_VERIFY(raster->plane_snapshot().values);auto *map=raster->findChild<MapDisplay*>();raster->findChild<QComboBox*>("mapProjectionSelector")->setCurrentIndex(int(Mode::LonLat));
  QTRY_VERIFY_WITH_TIMEOUT(map->result()&&!raster->render_pending(),15000);const auto first=map->grab().toImage();const auto geometry=map->result()->geometry;
  auto *colour_bar=raster->findChild<ColorBar*>();auto *legend=raster->findChild<QLabel*>("rasterLegend");QVERIFY(colour_bar&&legend);const auto first_scale=colour_bar->grab().toImage();const auto first_legend=legend->text();
  auto *pool=QThreadPool::globalInstance();const int threads=pool->maxThreadCount();pool->setMaxThreadCount(1);
  auto gate=std::make_shared<QSemaphore>(),entered=std::make_shared<QSemaphore>();pool->start([gate,entered]{entered->release();gate->acquire();});
  const auto release=qScopeGuard([&]{gate->release();pool->waitForDone(15000);pool->setMaxThreadCount(threads);});QTRY_VERIFY_WITH_TIMEOUT(entered->available(),15000);
  auto *play=window.findChild<QAction*>("actionLayerPlay");auto *player=window.findChild<LayerPlayback*>();QVERIFY(play&&player);QTRY_VERIFY(play->isEnabled());window.findChild<QSpinBox*>("playbackIntervalSpin")->setValue(10000);
  play->trigger();QVERIFY(player->is_playing());player->advance_frame();QCOMPARE(raster->layer_index(0),1);QVERIFY(raster->render_pending());QVERIFY(!map->result());QCOMPARE(map->grab().toImage(),first);
  QTest::qWait(80);player->advance_frame();QCOMPARE(raster->layer_index(0),1);QCOMPARE(map->grab().toImage(),first);QCOMPARE(colour_bar->grab().toImage(),first_scale);QCOMPARE(legend->text(),first_legend);
  gate->release();QTRY_VERIFY_WITH_TIMEOUT(!raster->render_pending()&&map->result(),15000);QCOMPARE(map->result()->geometry,geometry);QCOMPARE(raster->plane_snapshot().values->at(0),103.);QVERIFY(colour_bar->grab().toImage()!=first_scale);QVERIFY(legend->text()!=first_legend);QVERIFY(legend->text().contains("Min 100"));
  player->advance_frame();QCOMPARE(raster->layer_index(0),2);QVERIFY(raster->render_pending());QTRY_VERIFY_WITH_TIMEOUT(!raster->render_pending(),15000);
  player->advance_frame();QCOMPARE(raster->layer_index(0),3);QVERIFY(player->is_playing());QTRY_VERIFY_WITH_TIMEOUT(!raster->render_pending()&&map->result(),15000);player->advance_frame();QVERIFY(!player->is_playing());
  QCOMPARE(raster->plane_snapshot().values->at(0),309.);const auto paused=map->grab().toImage();QTest::qWait(120);QCOMPARE(map->grab().toImage(),paused);QCOMPARE(raster->layer_index(0),3);
  raster->set_layer_index(0,0);raster->set_layer_index(0,2);QTRY_VERIFY_WITH_TIMEOUT(!raster->render_pending()&&map->result(),15000);QCOMPARE(raster->plane_snapshot().values->at(0),206.);
  auto *mask_start=raster->findChild<QLineEdit*>("maskStart"),*mask_end=raster->findChild<QLineEdit*>("maskEnd");mask_start->setText("10");mask_end->setText("5");QMetaObject::invokeMethod(mask_end,"editingFinished",Qt::DirectConnection);
  QVERIFY(!map->result());QVERIFY(!raster->plane_snapshot().values);QVERIFY(raster->image_for_export().isNull());QCOMPARE(map->grab().toImage().pixelColor(map->width()/2,map->height()/2),QColor("#f7f7f7"));
}
void ProjectionTest::guiVisibleSelectorRedrawsNetcdfAndGrib(){
  QTemporaryDir directory;const auto nc_file=directory.filePath("direct.nc");fixture(nc_file);
  const auto grib_file=directory.filePath("global.grb2");
  QFile input_grib(QString(HDFSTUDIO_TEST_SOURCE_DIR)+"/testdata/grib/mini_simple.grb2");QVERIFY(input_grib.open(QIODevice::ReadOnly));const auto encoded=input_grib.readAll();
  auto *handle=codes_handle_new_from_message_copy(nullptr,encoded.constData(),size_t(encoded.size()));QVERIFY(handle);const auto delete_handle=qScopeGuard([handle]{codes_handle_delete(handle);});
  for(const auto &entry:std::vector<std::pair<const char*,double>>{{"latitudeOfFirstGridPointInDegrees",60},{"latitudeOfLastGridPointInDegrees",-60},{"longitudeOfFirstGridPointInDegrees",0},{"longitudeOfLastGridPointInDegrees",270},{"iDirectionIncrementInDegrees",90},{"jDirectionIncrementInDegrees",60}})QCOMPARE(codes_set_double(handle,entry.first,entry.second),0);
  const void *message=nullptr;size_t length=0;QCOMPARE(codes_get_message(handle,&message,&length),0);QFile output_grib(grib_file);QVERIFY(output_grib.open(QIODevice::WriteOnly));QCOMPARE(output_grib.write(static_cast<const char*>(message),qint64(length)),qint64(length));output_grib.close();
  auto catalog=GribData::open(grib_file);
  for(bool grib:{false,true}){
    std::unique_ptr<RasterView> raster=grib?std::make_unique<RasterView>(grib_file,QString("/field/0"),GribData::describe(*catalog,"/field/0")):std::make_unique<RasterView>(nc_file.toStdString(),"/t",std::vector<hsize_t>{3,5});
    raster->resize(900,700);raster->show();QTRY_VERIFY_WITH_TIMEOUT(raster->plane_snapshot().values,15000);
    auto *selector=raster->findChild<QComboBox*>("mapProjectionSelector");auto *bar=raster->findChild<QFrame*>("projectionBar");auto *map=raster->findChild<MapDisplay*>();QVERIFY(selector&&bar&&map);
    QVERIFY(bar->isVisible()&&selector->isVisible());QVERIFY(selector->height()>=30);QVERIFY(raster->rect().contains(selector->mapTo(raster.get(),selector->rect().bottomRight())));
    const auto source=raster->image_for_export();const auto source_values=*raster->plane_snapshot().values;QImage previous;std::vector<qint64> previous_indices;
    for(auto mode:{Mode::LonLat,Mode::NorthPolar,Mode::SouthPolar,Mode::Lambert,Mode::Mercator}){
      selector->setFocus();QTest::keyClick(selector,Qt::Key_Home);for(int i=0;i<int(mode);++i)QTest::keyClick(selector,Qt::Key_Down);QTest::keyClick(selector,Qt::Key_Return);
      QTRY_VERIFY_WITH_TIMEOUT(map->result()&&map->isVisible()&&map->mode()==mode,15000);
      QVERIFY(!raster->findChild<QDialog*>("projectionDialog"));
      QVERIFY(map->parentWidget()->isVisible());QCOMPARE(qobject_cast<QStackedWidget*>(map->parentWidget())->currentWidget(),static_cast<QWidget*>(map));
      if(!previous.isNull()){QVERIFY2(previous!=map->result()->image,"Mode change must change raster pixels, not only its status label");QVERIFY(previous_indices!=map->result()->indices);}
      previous=map->result()->image;previous_indices=map->result()->indices;
      QCOMPARE(raster->image_for_export(),source);QCOMPARE(*raster->plane_snapshot().values,source_values);
    }
    selector->setCurrentIndex(int(Mode::Source));QVERIFY(!map->isVisible());QVERIFY(raster->findChild<QGraphicsView*>()->isVisible());QCOMPARE(raster->image_for_export(),source);
    raster->resize(640,600);QTest::qWait(20);QVERIFY(bar->isVisible()&&selector->isVisible());QVERIFY(raster->rect().contains(selector->mapTo(raster.get(),selector->rect().bottomRight())));
    if(grib){
      selector->setCurrentIndex(int(Mode::NorthPolar));QTRY_VERIFY_WITH_TIMEOUT(map->result()&&map->mode()==Mode::NorthPolar,15000);raster.reset();
      RasterView reopened(grib_file,QString("/field/0"),GribData::describe(*catalog,"/field/0"));reopened.resize(900,700);reopened.show();auto *restored=reopened.findChild<MapDisplay*>();
      QTRY_VERIFY_WITH_TIMEOUT(restored->result()&&restored->isVisible()&&restored->mode()==Mode::NorthPolar,15000);
      QCOMPARE(reopened.findChild<QComboBox*>("mapProjectionSelector")->currentData().toInt(),int(Mode::NorthPolar));QVERIFY(restored->result()->covered>0);
    }
  }
  GribData::release(grib_file);
}
void ProjectionTest::guiSelectorLatestAndVisibleRejection(){
  QTemporaryDir directory;const auto file=directory.filePath("latest.nc");fixture(file);
  RasterView raster(file.toStdString(),"/t",{3,5});raster.resize(800,650);raster.show();QTRY_VERIFY(raster.plane_snapshot().values);
  auto *selector=raster.findChild<QComboBox*>("mapProjectionSelector");auto *map=raster.findChild<MapDisplay*>();QVERIFY(selector&&map);
  selector->setCurrentIndex(int(Mode::NorthPolar));selector->setCurrentIndex(int(Mode::SouthPolar));selector->setCurrentIndex(int(Mode::Lambert));
  QTRY_VERIFY_WITH_TIMEOUT(map->result()&&map->mode()==Mode::Lambert,15000);QCOMPARE(selector->currentData().toInt(),int(Mode::Lambert));
  raster.findChild<QAction*>("projsAction")->trigger();QTRY_VERIFY(ok_button(raster));auto *dialog_mode=raster.findChild<QComboBox*>("projectionMode");dialog_mode->setCurrentIndex(int(Mode::NorthPolar));QTRY_VERIFY_WITH_TIMEOUT(map->result()&&map->mode()==Mode::NorthPolar,15000);QCOMPARE(selector->currentData().toInt(),int(Mode::NorthPolar));
  auto *parallel=raster.findChild<QDoubleSpinBox*>("projectionLatTs");const auto before=map->result();parallel->setValue(-60);QVERIFY(!raster.findChild<QLabel*>("projectionError")->text().isEmpty());QVERIFY(!ok_button(raster)->isEnabled());QCOMPARE(map->result(),before);QCOMPARE(map->mode(),Mode::NorthPolar);
  QPointer<QDialog> dialog=raster.findChild<QDialog*>("projectionDialog");dialog->reject();QTRY_VERIFY(dialog.isNull());
  const auto unknown=directory.filePath("unknown.nc");fixture(unknown,false);RasterView no_model(unknown.toStdString(),"/t",{3,5});no_model.resize(800,650);no_model.show();QTRY_VERIFY(no_model.plane_snapshot().values);
  auto *unknown_selector=no_model.findChild<QComboBox*>("mapProjectionSelector");auto *unknown_map=no_model.findChild<MapDisplay*>();unknown_selector->setCurrentIndex(int(Mode::NorthPolar));
  QTRY_VERIFY_WITH_TIMEOUT(unknown_map->result()&&unknown_map->isVisible(),15000);QCOMPARE(unknown_selector->currentData().toInt(),int(Mode::NorthPolar));
  QVERIFY(no_model.findChild<QLabel*>("projectionStatus")->text().contains("Unit-sphere display only"));
  // Bad coordinates still reject a map; shape-only display must not bypass coordinate validation.
  const auto invalid=directory.filePath("invalid-axis.nc");fixture(invalid,false);int root=-1,latitude=-1;nc_ok(nc_open(invalid.toLocal8Bit().constData(),NC_WRITE,&root));nc_ok(nc_inq_varid(root,"lat",&latitude));const double bad_lat[]={0,60,30};nc_ok(nc_put_var_double(root,latitude,bad_lat));nc_ok(nc_close(root));
  QFile::remove(qEnvironmentVariable("HDFSTUDIO_SETTINGS_FILE"));
  RasterView bad_axis(invalid.toStdString(),"/t",{3,5});bad_axis.show();QTRY_VERIFY(bad_axis.plane_snapshot().values);bad_axis.findChild<QComboBox*>("mapProjectionSelector")->setCurrentIndex(int(Mode::NorthPolar));
  auto *bad_status=bad_axis.findChild<QLabel*>("projectionStatus");QTRY_VERIFY_WITH_TIMEOUT(bad_status->text().contains("Cannot display"),15000);QVERIFY(bad_status->text().contains("monotonic"));QCOMPARE(bad_axis.findChild<QComboBox*>("mapProjectionSelector")->currentData().toInt(),int(Mode::Source));
  // A large rejected source recolours asynchronously: its hidden map must not overwrite the error.
  const auto large=directory.filePath("large-invalid-axis.nc");int large_root,large_y,large_x,large_lat,large_lon,large_var;nc_ok(nc_create(large.toLocal8Bit().constData(),NC_CLOBBER,&large_root));nc_ok(nc_def_dim(large_root,"lat",256,&large_y));nc_ok(nc_def_dim(large_root,"lon",300,&large_x));nc_ok(nc_def_var(large_root,"lat",NC_DOUBLE,1,&large_y,&large_lat));nc_ok(nc_def_var(large_root,"lon",NC_DOUBLE,1,&large_x,&large_lon));const int large_dims[]={large_y,large_x};nc_ok(nc_def_var(large_root,"t",NC_DOUBLE,2,large_dims,&large_var));nc_ok(nc_put_att_text(large_root,large_lat,"units",13,"degrees_north"));nc_ok(nc_put_att_text(large_root,large_lon,"units",12,"degrees_east"));nc_ok(nc_enddef(large_root));std::vector<double> latitudes(256),longitudes(300),large_values(256*300,1.);for(size_t i=0;i<256;++i)latitudes[i]=-80.+i*.6;latitudes[128]=latitudes[127];for(size_t i=0;i<300;++i)longitudes[i]=i;nc_ok(nc_put_var_double(large_root,large_lat,latitudes.data()));nc_ok(nc_put_var_double(large_root,large_lon,longitudes.data()));nc_ok(nc_put_var_double(large_root,large_var,large_values.data()));nc_ok(nc_close(large_root));
  RasterView large_bad(large.toStdString(),"/t",{256,300});large_bad.show();QTRY_VERIFY_WITH_TIMEOUT(large_bad.plane_snapshot().values,15000);large_bad.findChild<QComboBox*>("mapProjectionSelector")->setCurrentIndex(int(Mode::NorthPolar));auto *large_status=large_bad.findChild<QLabel*>("projectionStatus");QTRY_VERIFY_WITH_TIMEOUT(large_status->text().contains("Cannot display"),15000);auto *reverse=large_bad.findChild<QCheckBox*>("reverseColorMap");QVERIFY(reverse);reverse->toggle();QTRY_VERIFY_WITH_TIMEOUT(!large_bad.render_pending(),15000);QVERIFY(large_status->text().contains("Cannot display"));
}
void ProjectionTest::guiSingleRowMouseSelectionUnknownSource(){
  QTemporaryDir directory;const auto source=directory.filePath("unknown-model.nc");fixture(source,false);
  const auto grid=GeoGrid::read_netcdf(source,"/t",3,5);QVERIFY(grid.error.isEmpty());QVERIFY(!grid.earth.valid());
  RasterView raster(source.toStdString(),"/t",{3,5});raster.resize(900,700);raster.show();QTRY_VERIFY(raster.plane_snapshot().values);
  auto *selector=raster.findChild<QComboBox*>("mapProjectionSelector");auto *bar=raster.findChild<QFrame*>("projectionBar");auto *status=raster.findChild<QLabel*>("projectionStatus");auto *options=raster.findChild<QToolButton*>("projectionSettings");auto *map=raster.findChild<MapDisplay*>();QVERIFY(selector&&bar&&status&&options&&map);
  const auto original=raster.image_for_export();const auto original_values=*raster.plane_snapshot().values;
  for(int width:{900,640}){
    raster.resize(width,650);QTest::qWait(30);
    QVERIFY(!status->wordWrap());QVERIFY(bar->height()<=selector->height()+10);
    QVERIFY(status->geometry().intersects(QRect(0,selector->y(),bar->width(),selector->height())));
    QVERIFY(bar->rect().contains(selector->geometry()));QVERIFY(bar->rect().contains(options->geometry()));
  }
  QImage previous;std::vector<qint64> previous_indices;
  for(auto selected:{Mode::NorthPolar,Mode::SouthPolar,Mode::Lambert,Mode::LonLat,Mode::Mercator}){
    QTest::mouseClick(selector,Qt::LeftButton);auto *popup=selector->view();QTRY_VERIFY(popup->isVisible());const auto index=selector->model()->index(int(selected),0);popup->scrollTo(index);QTest::qWait(10);
    const auto selected_rect=popup->visualRect(index);QVERIFY2(popup->viewport()->rect().contains(selected_rect.center()),qPrintable(QString("Popup target %1 outside viewport %2").arg(name(selected)).arg(popup->viewport()->height())));
    QTest::mouseMove(popup->viewport(),selected_rect.center());QTest::qWait(20);QTest::mouseClick(popup->viewport(),Qt::LeftButton,Qt::NoModifier,selected_rect.center());
    QTRY_VERIFY2_WITH_TIMEOUT(map->result()&&map->isVisible()&&map->mode()==selected,qPrintable(QString("requested=%1 selected=%2 status=%3").arg(name(selected),selector->currentText(),status->text())),15000);QVERIFY(map->result()->covered>0);
    QVERIFY(!raster.findChild<QDialog*>("projectionDialog"));QCOMPARE(selector->currentData().toInt(),int(selected));
    QVERIFY(status->text().contains("source Earth model unknown"));
    if(selected!=Mode::LonLat){QVERIFY(map->property("mapOperation").toString().contains("+a=1 +b=1"));}
    if(!previous.isNull()){QVERIFY(previous!=map->result()->image);QVERIFY(previous_indices!=map->result()->indices);}
    previous=map->result()->image;previous_indices=map->result()->indices;
    QCOMPARE(raster.image_for_export(),original);QCOMPARE(*raster.plane_snapshot().values,original_values);
    QSettings saved(qEnvironmentVariable("HDFSTUDIO_SETTINGS_FILE"),QSettings::IniFormat);QVERIFY(!saved.value("MapProjection/mode").toString().isEmpty());QVERIFY(!saved.contains("MapProjection/earth"));
  }
  selector->setCurrentIndex(int(Mode::Source));QVERIFY(!map->isVisible());QCOMPARE(raster.image_for_export(),original);
  // Physical core operations still require a verified model; only MapDisplay normalizes shape.
  QVERIFY(!MapProjection::validate(preset(Mode::NorthPolar)).isEmpty());
  QVERIFY(!GeoGrid::read_netcdf(source,"/t",3,5).earth.valid());
  for(auto selected:{Mode::NorthPolar,Mode::SouthPolar,Mode::Lambert,Mode::Mercator}){
    const Transform physical(preset(selected,sphere())),normalized(preset(selected,{1,1,"display only"}));
    for(double latitude:{25.,40.,60.}){
      const double signed_latitude=selected==Mode::SouthPolar?-latitude:latitude;QPointF metres,unit;
      QVERIFY(physical.forward(115,signed_latitude,metres));QVERIFY(normalized.forward(115,signed_latitude,unit));
      QVERIFY(QLineF(metres/6371000,unit).length()<1e-12);
    }
  }
}
void ProjectionTest::guiSettingsOkCancelKeyboardAndRollback(){
  MapDisplay map;map.resize(720,540);auto in=input();map.set_grid(in.grid,{});map.set_source(in.source,in.values,in.original,{});map.show();
  map.show_settings();QPointer<QDialog> dialog=map.findChild<QDialog*>("projectionDialog");QVERIFY(dialog);QVERIFY(dialog->isModal());
  auto *buttons=dialog->findChild<QDialogButtonBox*>("projectionButtons");QVERIFY(buttons);QCOMPARE(buttons->buttons().size(),2);
  QVERIFY(buttons->button(QDialogButtonBox::Ok));QVERIFY(buttons->button(QDialogButtonBox::Cancel));
  QVERIFY(!buttons->button(QDialogButtonBox::Apply));QVERIFY(!buttons->button(QDialogButtonBox::Close));QVERIFY(!buttons->button(QDialogButtonBox::Reset));
  dialog->findChild<QComboBox*>("projectionMode")->setCurrentIndex(int(Mode::NorthPolar));QTRY_VERIFY_WITH_TIMEOUT(map.result(),15000);
  dialog->findChild<QPushButton*>("projectionCancel")->click();QTRY_VERIFY(dialog.isNull());QCOMPARE(map.mode(),Mode::Source);QVERIFY(!map.result());

  QVERIFY(map.select_projection(Mode::LonLat).isEmpty());QTRY_VERIFY_WITH_TIMEOUT(map.result(),15000);map.zoom(1.4);QTRY_VERIFY_WITH_TIMEOUT(map.result(),15000);
  const auto opening=map.result();const auto opening_paint=map.grab().toImage();
  map.show_settings();dialog=map.findChild<QDialog*>("projectionDialog");QVERIFY(dialog);
  dialog->findChild<QComboBox*>("projectionMode")->setCurrentIndex(int(Mode::SouthPolar));
  dialog->findChild<QCheckBox*>("projectionCoastlines")->setChecked(false);
  dialog->findChild<QDoubleSpinBox*>("projectionCoastWidth")->setValue(2.5);
  QTRY_VERIFY_WITH_TIMEOUT(map.result()&&map.mode()==Mode::SouthPolar,15000);QVERIFY(map.result()->image!=opening->image);
  QTest::keyClick(dialog,Qt::Key_Escape);QTRY_VERIFY(dialog.isNull());QTRY_VERIFY_WITH_TIMEOUT(map.result()&&map.mode()==Mode::LonLat,15000);
  QCOMPARE(map.result()->image,opening->image);QCOMPARE(map.result()->indices,opening->indices);
  QCOMPARE(map.result()->viewport.xmin,opening->viewport.xmin);QCOMPARE(map.result()->viewport.ymax,opening->viewport.ymax);
  QCOMPARE(map.grab().toImage(),opening_paint);

  map.show_settings();dialog=map.findChild<QDialog*>("projectionDialog");QVERIFY(dialog);
  QVERIFY(dialog->findChild<QCheckBox*>("projectionCoastlines")->isChecked());QCOMPARE(dialog->findChild<QDoubleSpinBox*>("projectionCoastWidth")->value(),.8);
  dialog->findChild<QComboBox*>("projectionMode")->setCurrentIndex(int(Mode::Mercator));QTRY_VERIFY_WITH_TIMEOUT(map.result()&&map.mode()==Mode::Mercator,15000);
  dialog->close();QTRY_VERIFY(dialog.isNull());QTRY_VERIFY_WITH_TIMEOUT(map.result()&&map.mode()==Mode::LonLat,15000);QCOMPARE(map.result()->indices,opening->indices);

  map.show_settings();dialog=map.findChild<QDialog*>("projectionDialog");QVERIFY(dialog);
  auto *mode=dialog->findChild<QComboBox*>("projectionMode");mode->setCurrentIndex(int(Mode::NorthPolar));
  auto *parallel=dialog->findChild<QDoubleSpinBox*>("projectionLatTs");parallel->setValue(-60);QVERIFY(!ok_button(map)->isEnabled());QVERIFY(!dialog->findChild<QLabel*>("projectionError")->text().isEmpty());
  parallel->setValue(65);QVERIFY(ok_button(map)->isEnabled());dialog->findChild<QCheckBox*>("projectionCoastlines")->setChecked(false);
  dialog->findChild<QDoubleSpinBox*>("projectionCoastWidth")->setValue(1.6);
  QTRY_VERIFY_WITH_TIMEOUT(map.result()&&map.mode()==Mode::NorthPolar,15000);QTest::keyClick(dialog,Qt::Key_Return);QTRY_VERIFY(dialog.isNull());QCOMPARE(map.mode(),Mode::NorthPolar);
  map.show_settings();dialog=map.findChild<QDialog*>("projectionDialog");QVERIFY(dialog);
  QCOMPARE(dialog->findChild<QDoubleSpinBox*>("projectionLatTs")->value(),65.);QVERIFY(!dialog->findChild<QCheckBox*>("projectionCoastlines")->isChecked());QCOMPARE(dialog->findChild<QDoubleSpinBox*>("projectionCoastWidth")->value(),1.6);
  dialog->findChild<QComboBox*>("projectionMode")->setCurrentIndex(int(Mode::Source));QVERIFY(!map.active());QTest::keyClick(dialog,Qt::Key_Escape);QTRY_VERIFY(dialog.isNull());QTRY_VERIFY_WITH_TIMEOUT(map.result()&&map.mode()==Mode::NorthPolar,15000);
  map.show_settings();dialog=map.findChild<QDialog*>("projectionDialog");QVERIFY(dialog);
  dialog->findChild<QComboBox*>("projectionMode")->setCurrentIndex(int(Mode::SouthPolar));QTRY_VERIFY_WITH_TIMEOUT(map.result()&&map.mode()==Mode::SouthPolar,15000);ok_button(map)->click();QTRY_VERIFY(dialog.isNull());QCOMPARE(map.mode(),Mode::SouthPolar);
}
void ProjectionTest::preferencesPersistConfirmAndRestoreFreshProcess(){
  const auto settings_file=qEnvironmentVariable("HDFSTUDIO_SETTINGS_FILE");
  {QSettings initial(settings_file,QSettings::IniFormat);initial.setValue("Unrelated/keep","unchanged");initial.sync();}
  MapDisplay map;map.resize(640,480);auto in=input();map.set_grid(in.grid,{});map.set_source(in.source,in.values,in.original,{});map.show();
  QVERIFY(map.select_projection(Mode::NorthPolar).isEmpty());QTRY_VERIFY_WITH_TIMEOUT(map.result(),15000);
  auto bytes=[&]{QFile file(settings_file);if(!file.open(QIODevice::ReadOnly))return QByteArray();return file.readAll();};
  const auto confirmed=bytes();QVERIFY(confirmed.contains("north-polar"));QVERIFY(!confirmed.contains("earth")&&!confirmed.contains("6371000"));
  map.show_settings();QPointer<QDialog> dialog=map.findChild<QDialog*>("projectionDialog");QVERIFY(dialog);
  dialog->findChild<QComboBox*>("projectionMode")->setCurrentIndex(int(Mode::SouthPolar));QTRY_VERIFY_WITH_TIMEOUT(map.result()&&map.mode()==Mode::SouthPolar,15000);
  QCOMPARE(bytes(),confirmed);dialog->findChild<QPushButton*>("projectionCancel")->click();QTRY_VERIFY(dialog.isNull());QCOMPARE(bytes(),confirmed);
  map.show_settings();dialog=map.findChild<QDialog*>("projectionDialog");QVERIFY(dialog);
  dialog->findChild<QDoubleSpinBox*>("projectionLon0")->setValue(25);
  dialog->findChild<QDoubleSpinBox*>("projectionLatTs")->setValue(65);
  dialog->findChild<QCheckBox*>("projectionCoastlines")->setChecked(false);
  dialog->findChild<QDoubleSpinBox*>("projectionCoastWidth")->setValue(1.7);
  QCOMPARE(bytes(),confirmed);ok_button(map)->click();QTRY_VERIFY(dialog.isNull());QVERIFY(bytes()!=confirmed);
  {QSettings saved(settings_file,QSettings::IniFormat);QCOMPARE(saved.value("Unrelated/keep").toString(),QString("unchanged"));QCOMPARE(saved.value("MapProjection/lon0").toDouble(),25.);}
  const auto after_ok=bytes();
  const auto source=settings_directory->filePath("reopen.nc");fixture(source);
  // A different source model must win over any previous source's model.
  int root=-1,mapping=-1;nc_ok(nc_open(source.toLocal8Bit().constData(),NC_WRITE,&root));nc_ok(nc_inq_varid(root,"earth",&mapping));double radius=6400000;nc_ok(nc_put_att_double(root,mapping,"earth_radius",NC_DOUBLE,1,&radius));nc_ok(nc_close(root));
  QProcess child;auto environment=QProcessEnvironment::systemEnvironment();environment.insert("HDFSTUDIO_PREF_CHILD",source);child.setProcessEnvironment(environment);
  child.setWorkingDirectory(settings_directory->path());child.start(QCoreApplication::applicationFilePath(),{"preferencesRestoreChildSession"});
  QVERIFY(child.waitForFinished(30000));QVERIFY2(child.exitStatus()==QProcess::NormalExit&&child.exitCode()==0,child.readAllStandardOutput()+child.readAllStandardError());QCOMPARE(bytes(),after_ok);
  const auto unknown=settings_directory->filePath("unknown.nc");fixture(unknown,false);
  {RasterView raster(unknown.toStdString(),"/t",{3,5});raster.resize(800,650);raster.show();auto *restored=raster.findChild<MapDisplay*>();QTRY_VERIFY_WITH_TIMEOUT(restored->result()&&restored->isVisible(),15000);QCOMPARE(restored->mode(),Mode::NorthPolar);QVERIFY(restored->property("mapOperation").toString().contains("+a=1 +b=1"));QVERIFY(raster.findChild<QLabel*>("projectionStatus")->text().contains("source Earth model unknown"));QCOMPARE(bytes(),after_ok);}
  QVERIFY(map.select_projection(Mode::Source).isEmpty());MapDisplay reopened;QVERIFY(reopened.has_saved_preferences());QCOMPARE(reopened.saved_mode(),Mode::Source);
}
void ProjectionTest::preferencesRestoreChildSession(){
  const auto source=qEnvironmentVariable("HDFSTUDIO_PREF_CHILD");if(source.isEmpty())QSKIP("Parent test launches this slot in an independent process");
  RasterView raster(source.toStdString(),"/t",{3,5});raster.resize(800,650);raster.show();auto *map=raster.findChild<MapDisplay*>();QVERIFY(map);
  QTRY_VERIFY_WITH_TIMEOUT(map->result()&&map->isVisible()&&map->mode()==Mode::NorthPolar,15000);
  QCOMPARE(raster.findChild<QComboBox*>("mapProjectionSelector")->currentData().toInt(),int(Mode::NorthPolar));
  QVERIFY(map->property("mapOperation").toString().contains("+lon_0=25"));QVERIFY(map->property("mapOperation").toString().contains("+lat_ts=65"));QVERIFY(map->property("mapOperation").toString().contains("+a=6400000"));
  raster.findChild<QAction*>("projsAction")->trigger();QTRY_VERIFY(ok_button(raster));QVERIFY(!raster.findChild<QCheckBox*>("projectionCoastlines")->isChecked());QCOMPARE(raster.findChild<QDoubleSpinBox*>("projectionCoastWidth")->value(),1.7);
  raster.findChild<QDialog*>("projectionDialog")->reject();
}
void ProjectionTest::preferencesInvalidAndUnwritableRemainVisible(){
  const auto settings_file=qEnvironmentVariable("HDFSTUDIO_SETTINGS_FILE");QFile corrupt(settings_file);QVERIFY(corrupt.open(QIODevice::WriteOnly));corrupt.write("[MapProjection]\nversion=1\nmode=bad-mode\nspan=nan\n");corrupt.close();
  const auto source=settings_directory->filePath("invalid.nc");fixture(source);
  {RasterView raster(source.toStdString(),"/t",{3,5});raster.resize(640,600);raster.show();QTRY_VERIFY(raster.plane_snapshot().values);auto *error=raster.findChild<QLabel*>("projectionPreferenceError");QVERIFY(error->isVisible());QVERIFY(error->text().contains("invalid projection preferences"));QVERIFY(!error->wordWrap());QCOMPARE(error->toolTip(),error->text());QVERIFY(raster.findChild<QFrame*>("projectionBar")->height()<=raster.findChild<QComboBox*>("mapProjectionSelector")->height()+10);QCOMPARE(raster.findChild<QComboBox*>("mapProjectionSelector")->currentIndex(),0);}
  for(const auto &invalid:std::vector<std::pair<QString,QVariant>>{{"version",99},{"span",0.},{"coast_width",-1.},{"lon0","nan"},{"mode","unknown"}}){
    const auto candidate=settings_directory->filePath("invalid-"+invalid.first+".ini");qputenv("HDFSTUDIO_SETTINGS_FILE",candidate.toUtf8());
    {MapDisplay seed;QVERIFY(seed.select_projection(Mode::Source).isEmpty());}
    {QSettings settings(candidate,QSettings::IniFormat);settings.setValue("MapProjection/"+invalid.first,invalid.second);settings.sync();}
    MapDisplay rejected;QVERIFY2(!rejected.has_saved_preferences(),qPrintable(invalid.first));QCOMPARE(rejected.mode(),Mode::Source);QVERIFY(rejected.property("settingsError").toString().contains("invalid projection preferences"));
  }
  // Verify the production default path without writing to the real user's INI.
  qunsetenv("HDFSTUDIO_SETTINGS_FILE");{MapDisplay default_path;QCOMPARE(default_path.property("settingsFile").toString(),QCoreApplication::applicationDirPath()+"/HDFStudio.ini");}
  const auto blocked=settings_directory->filePath("blocked.ini");QVERIFY(QDir().mkdir(blocked));qputenv("HDFSTUDIO_SETTINGS_FILE",blocked.toUtf8());
  {RasterView raster(source.toStdString(),"/t",{3,5});raster.show();QTRY_VERIFY(raster.plane_snapshot().values);raster.findChild<QComboBox*>("mapProjectionSelector")->setCurrentIndex(int(Mode::LonLat));
    QTRY_VERIFY_WITH_TIMEOUT(raster.findChild<MapDisplay*>()->result(),15000);QVERIFY(raster.findChild<QLabel*>("projectionPreferenceError")->isVisible());QVERIFY(raster.findChild<QLabel*>("projectionPreferenceError")->text().contains("Cannot save"));}
  MapDisplay map;map.resize(640,480);auto in=input();map.set_grid(in.grid,{});map.set_source(in.source,in.values,in.original,{});map.show();
  QVERIFY(map.select_projection(Mode::LonLat).isEmpty());QTRY_VERIFY_WITH_TIMEOUT(map.result(),15000);QVERIFY(map.property("settingsError").toString().contains("Cannot save"));
  map.show_settings();QPointer<QDialog> dialog=map.findChild<QDialog*>("projectionDialog");QVERIFY(dialog);ok_button(map)->click();QVERIFY(dialog&&dialog->isVisible());QVERIFY(dialog->findChild<QLabel*>("projectionError")->text().contains("Cannot save"));dialog->reject();QTRY_VERIFY(dialog.isNull());
}
void ProjectionTest::ellipsoidMercatorAndErrors(){auto s=preset(Mode::Mercator,{6378137,6356752.314245179,"WGS84 axes for test only"});Transform t(s);QPointF p;QVERIFY(t.forward(30,40,p));const double e=std::sqrt(1-s.earth.b*s.earth.b/(s.earth.a*s.earth.a)),phi=40*pi/180;
  const double y=s.earth.a*std::log(std::tan(pi/4+phi/2)*std::pow((1-e*std::sin(phi))/(1+e*std::sin(phi)),e/2));QVERIFY(std::abs(p.x()-s.earth.a*pi/6)<.001);QVERIFY(std::abs(p.y()-y)<.001);QVERIFY(!t.forward(0,91,p));QVERIFY(!t.forward(NAN,0,p));
  QTemporaryDir empty;Transform explicit_without_db(s,empty.path());QVERIFY(explicit_without_db.error().isEmpty());QVERIFY(explicit_without_db.forward(30,40,p));QVERIFY(!Transform::database_status(empty.path()).isEmpty());QVERIFY(Transform::database_status({}).isEmpty());
}
void ProjectionTest::extentAndParameterValidation(){auto s=preset(Mode::Lambert,sphere());QVERIFY(validate(s).isEmpty());s.lat2=s.lat1;QVERIFY(!validate(s).isEmpty());s=preset(Mode::NorthPolar,sphere());s.lat_ts=-60;QVERIFY(!validate(s).isEmpty());s=preset(Mode::LonLat);QVERIFY(validate(s).isEmpty());s.span=0;QVERIFY(!validate(s).isEmpty());
  s=preset(Mode::NorthPolar,sphere());s.south=0;QVERIFY(!validate(s).isEmpty());s=preset(Mode::SouthPolar,sphere());s.north=0;QVERIFY(!validate(s).isEmpty());
  QString error;const auto v=fit(preset(Mode::NorthPolar,sphere()),800,500,1,error);QVERIFY2(error.isEmpty(),qPrintable(error));QVERIFY(std::abs((v.xmax-v.xmin)/800-(v.ymax-v.ymin)/500)<1e-9);QVERIFY(v.xmin<0&&v.xmax>0);
}
void ProjectionTest::boundsLookupAgainstLinearReference(){
  for(bool descending:{false,true}){GeoGrid::Grid grid;grid.latitude.centers={-80,-40,0,40,80};grid.latitude.bounds={{-90,-60},{-60,-20},{-10,10},{20,60},{60,90}};grid.longitude.centers={0,90,180,270};if(descending){std::reverse(grid.latitude.centers.begin(),grid.latitude.centers.end());std::reverse(grid.latitude.bounds.begin(),grid.latitude.bounds.end());for(auto &b:grid.latitude.bounds)std::swap(b.first,b.second);}QVERIFY(GeoGrid::validate(grid).isEmpty());
    for(int half_degree=-182;half_degree<=182;++half_degree){const double value=half_degree/2.;bool covered=false;for(const auto &bound:grid.latitude.bounds)covered=covered||(value>=std::min(bound.first,bound.second)&&value<=std::max(bound.first,bound.second));qint64 reference=-1;double distance=INFINITY;if(covered)for(size_t i=0;i<grid.latitude.centers.size();++i){const double delta=std::abs(grid.latitude.centers[i]-value);if(delta<distance){reference=qint64(i);distance=delta;}}QCOMPARE(GeoGrid::nearest(grid.latitude,value),reference);}
  }
}
void ProjectionTest::gribScanCoordinateValueAlignment(){
  for(int scanning:{0,64,128,16,32,48}){const auto path=QString(HDFSTUDIO_TEST_SOURCE_DIR)+QString("/testdata/grib/mini_scan_%1.grb2").arg(scanning);auto catalog=GribData::open(path);const auto raw=grib::read_plane(*catalog,0,true,false);const auto data=GribData::read(*catalog,"/field/0",{},true,false);QVERIFY(data.error.isEmpty());const auto grid=GeoGrid::from_grib(data,data.geographic_keys,"regular_ll");QVERIFY2(grid.error.isEmpty(),qPrintable(grid.error));
    QFile encoded(path);QVERIFY(encoded.open(QIODevice::ReadOnly));const auto bytes=encoded.readAll();int error=0;codes_handle *handle=codes_handle_new_from_message_copy(nullptr,bytes.constData(),size_t(bytes.size()));QVERIFY(handle);const auto cleanup=qScopeGuard([&]{codes_handle_delete(handle);GribData::release(path);});codes_iterator *iterator=codes_grib_iterator_new(handle,0,&error);QVERIFY(iterator);std::vector<double> latitudes,longitudes,values;double lat,lon,value;while(codes_grib_iterator_next(iterator,&lat,&lon,&value)){latitudes.push_back(lat);longitudes.push_back(lon);values.push_back(value);}codes_grib_iterator_delete(iterator);QCOMPARE(values.size(),data.values.size());
    for(size_t i=0;i<data.values.size();++i){const auto source=raw.source_indices[i];QVERIFY(source<values.size());QCOMPARE(data.values[i],values[source]);const size_t r=i/data.cols,c=i%data.cols;const double expected_lat=scanning==64?double(r):2.-double(r),expected_lon=scanning==128?3.-double(c):double(c);QCOMPARE(data.coordinates.latitudes[i],expected_lat);QCOMPARE(data.coordinates.longitudes[i],expected_lon);}
    // Fixed fixture values are 85000 + encoded position; the matrices below
    // come directly from the GRIB scan-bit definitions, not backend indices.
    const std::vector<size_t> expected_positions=scanning==16?std::vector<size_t>{0,1,2,3,7,6,5,4,8,9,10,11}:scanning==32?std::vector<size_t>{0,3,6,9,1,4,7,10,2,5,8,11}:scanning==48?std::vector<size_t>{0,5,6,11,1,4,7,10,2,3,8,9}:std::vector<size_t>{0,1,2,3,4,5,6,7,8,9,10,11};
    for(size_t i=0;i<expected_positions.size();++i){QCOMPARE(raw.source_indices[i],expected_positions[i]);QCOMPARE(data.values[i],85000.+double(expected_positions[i]));}
    auto in=input();in.grid=std::make_shared<GeoGrid::Grid>(grid);in.values=std::make_shared<const std::vector<double>>(data.values);in.original=in.values;in.source=QImage(int(data.cols),int(data.rows),QImage::Format_RGB32);in.source.fill(Qt::blue);QString extent_error;in.spec=preset(Mode::LonLat);in.spec.west=grid.longitude.centers.front()-1;in.spec.span=std::abs(grid.longitude.centers.back()-grid.longitude.centers.front())+2;in.spec.south=std::min(grid.latitude.centers.front(),grid.latitude.centers.back())-1;in.spec.north=std::max(grid.latitude.centers.front(),grid.latitude.centers.back())+1;in.viewport=fit(in.spec,128,96,1,extent_error);auto result=MapRender::render(in);QVERIFY2(result->error.isEmpty(),qPrintable(result->error));QVERIFY(result->covered>0);for(const auto index:result->indices)if(index>=0)QVERIFY(size_t(index)<data.values.size());
  }
}
void ProjectionTest::gribGaussianCoordinatesAndScanAlignment(){
  QTemporaryDir directory;
  const double gaussian_latitudes[]={59.44440828916677,19.875719147440904,-19.875719147440904,-59.44440828916677};
  for(int scanning:{0,64,128,16,32,48}){
    auto *handle=codes_grib_handle_new_from_samples(nullptr,"regular_gg_sfc_grib2");QVERIFY(handle);const auto delete_handle=qScopeGuard([handle]{codes_handle_delete(handle);});
    for(const auto &entry:std::vector<std::pair<const char*,long>>{{"N",2},{"Ni",8},{"Nj",4},{"scanningMode",scanning}})QCOMPARE(codes_set_long(handle,entry.first,entry.second),0);
    for(const auto &entry:std::vector<std::pair<const char*,double>>{{"latitudeOfFirstGridPointInDegrees",scanning==64?gaussian_latitudes[3]:gaussian_latitudes[0]},{"latitudeOfLastGridPointInDegrees",scanning==64?gaussian_latitudes[0]:gaussian_latitudes[3]},{"longitudeOfFirstGridPointInDegrees",scanning==128?315.:0.},{"longitudeOfLastGridPointInDegrees",scanning==128?0.:315.},{"iDirectionIncrementInDegrees",45}})QCOMPARE(codes_set_double(handle,entry.first,entry.second),0);
    double encoded_values[32];for(int i=0;i<32;++i)encoded_values[i]=85000+i;QCOMPARE(codes_set_double_array(handle,"values",encoded_values,32),0);
    const void *message=nullptr;size_t length=0;QCOMPARE(codes_get_message(handle,&message,&length),0);const auto path=directory.filePath(QString("gaussian-%1.grb2").arg(scanning));QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(static_cast<const char*>(message),qint64(length)),qint64(length));file.close();
    const auto catalog=GribData::open(path);const auto release=qScopeGuard([path]{GribData::release(path);});QVERIFY(catalog);const auto data=GribData::read(*catalog,"/field/0",{},true,false);QVERIFY2(data.error.isEmpty(),qPrintable(data.error));QCOMPARE(data.rows,size_t(4));QCOMPARE(data.cols,size_t(8));
    auto grid=GeoGrid::from_grib(data,data.geographic_keys,"regular_gg");QVERIFY2(grid.error.isEmpty(),qPrintable(grid.error));QVERIFY(grid.longitude.periodic);QVERIFY(grid.earth.valid());QCOMPARE(grid.earth.a,6371229.);QCOMPARE(grid.earth.b,6371229.);
    const std::vector<size_t> positions=scanning==16?std::vector<size_t>{0,1,2,3,4,5,6,7,15,14,13,12,11,10,9,8,16,17,18,19,20,21,22,23,31,30,29,28,27,26,25,24}:scanning==32?std::vector<size_t>{0,4,8,12,16,20,24,28,1,5,9,13,17,21,25,29,2,6,10,14,18,22,26,30,3,7,11,15,19,23,27,31}:scanning==48?std::vector<size_t>{0,7,8,15,16,23,24,31,1,6,9,14,17,22,25,30,2,5,10,13,18,21,26,29,3,4,11,12,19,20,27,28}:std::vector<size_t>{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};
    for(size_t i=0;i<32;++i){QCOMPARE(data.values[i],85000.+double(positions[i]));QVERIFY(std::abs(data.coordinates.latitudes[i]-gaussian_latitudes[scanning==64?3-i/8:i/8])<1e-11);QCOMPARE(data.coordinates.longitudes[i],scanning==128?315.-45.*(i%8):45.*(i%8));}
    QVERIFY(std::abs(std::abs(grid.latitude.centers[0]-grid.latitude.centers[1])-std::abs(grid.latitude.centers[1]-grid.latitude.centers[2]))>.1);
    QVERIFY(!GeoGrid::from_grib(data,data.geographic_keys,"reduced_gg").error.isEmpty());
    auto malformed=data;malformed.coordinates.latitudes[9]+=.1;QVERIFY(!GeoGrid::from_grib(malformed,malformed.geographic_keys,"regular_gg").error.isEmpty());malformed=data;malformed.coordinates.longitudes.pop_back();QVERIFY(!GeoGrid::from_grib(malformed,malformed.geographic_keys,"regular_gg").error.isEmpty());
    for(int latitude=-60;latitude<=60;++latitude){qint64 reference=-1;double distance=INFINITY;if(latitude>=-gaussian_latitudes[0]&&latitude<=gaussian_latitudes[0])for(size_t row=0;row<4;++row){const double delta=std::abs(latitude-grid.latitude.centers[row]);if(delta<distance-1e-10){reference=qint64(row);distance=delta;}}QCOMPARE(GeoGrid::nearest(grid.latitude,latitude),reference);}
  }
}
void ProjectionTest::guiGaussianRealSourceModes(){
  const auto path=qEnvironmentVariable("HDFSTUDIO_GAUSSIAN_SAMPLE");if(path.isEmpty())QSKIP("Optional read-only Gaussian GRIB sample not configured");
  const auto catalog=GribData::open(path);const auto release=qScopeGuard([path]{GribData::release(path);});QVERIFY(catalog);QCOMPARE(catalog->fields.size(),size_t(480));QCOMPARE(catalog->cubes.size(),size_t(1));
  const auto object=catalog->cubes.front().id;const auto data=GribData::read(*catalog,object,{0},true,false);QVERIFY2(data.error.isEmpty(),qPrintable(data.error));QCOMPARE(data.rows,size_t(190));QCOMPARE(data.cols,size_t(384));const auto grid=GeoGrid::from_grib(data,data.geographic_keys,"regular_gg");QVERIFY2(grid.error.isEmpty(),qPrintable(grid.error));QCOMPARE(grid.earth.a,6371229.);QCOMPARE(grid.earth.b,6371229.);
  QCOMPARE(data.geographic_keys.value("global"),QString("1"));QVERIFY(grid.longitude.periodic);QCOMPARE(GeoGrid::nearest(grid.longitude,-.1,true),qint64(0));QCOMPARE(GeoGrid::nearest(grid.longitude,359.9,true),qint64(0));QVERIFY(GeoGrid::from_json(GeoGrid::json(grid)).longitude.periodic);
  auto without_topology=grid;without_topology.source_definition.remove("global");QVERIFY(GeoGrid::validate(without_topology).isEmpty());QVERIFY(!without_topology.longitude.periodic);QCOMPARE(GeoGrid::nearest(without_topology.longitude,359.9,true),qint64(-1));
  // Independent Legendre recurrence: Gaussian nodes are roots of P_190(sin(latitude)).
  for(size_t row=0;row<data.rows;++row){const double node=std::sin(grid.latitude.centers[row]*pi/180);double previous=1,current=node;for(int order=2;order<=190;++order){const double next=((2*order-1)*node*current-(order-1)*previous)/order;previous=current;current=next;}QVERIFY2(std::abs(current)<2e-11,qPrintable(QString("Gaussian root row %1 residual %2").arg(row).arg(current,0,'g',15)));for(size_t column=0;column<data.cols;++column){const size_t index=row*data.cols+column;QCOMPARE(data.coordinates.latitudes[index],grid.latitude.centers[row]);QCOMPARE(data.coordinates.longitudes[index],grid.longitude.centers[column]);}}
  RasterView raster(path,object,GribData::describe(*catalog,object));raster.resize(1000,760);raster.show();QTRY_VERIFY_WITH_TIMEOUT(raster.plane_snapshot().values,30000);const auto source=raster.image_for_export();const auto source_values=*raster.plane_snapshot().values;QCOMPARE(source_values,data.values);auto *selector=raster.findChild<QComboBox*>("mapProjectionSelector");auto *map=raster.findChild<MapDisplay*>();QImage previous;
  for(auto mode:{Mode::NorthPolar,Mode::SouthPolar,Mode::Lambert,Mode::LonLat,Mode::Mercator}){selector->setCurrentIndex(int(mode));QTRY_VERIFY2_WITH_TIMEOUT(map->result()&&!raster.render_pending()&&map->mode()==mode,qPrintable(raster.findChild<QLabel*>("projectionStatus")->text()),30000);QVERIFY(map->result()->covered>0);if(!previous.isNull())QVERIFY(map->result()->image!=previous);previous=map->result()->image;QCOMPARE(*raster.plane_snapshot().values,source_values);QCOMPARE(raster.image_for_export(),source);}
  raster.set_layer_index(0,1);QTRY_VERIFY_WITH_TIMEOUT(map->result()&&!raster.render_pending(),30000);QVERIFY(*raster.plane_snapshot().values!=source_values);QVERIFY(map->result()->image!=previous);
  selector->setCurrentIndex(int(Mode::Source));QVERIFY(!map->isVisible());
}
void ProjectionTest::guiMaskProbeLatestLayersAndClose(){
  QTemporaryDir temp;const auto file=temp.filePath("layers.nc");fixture(file);int root;nc_ok(nc_open(file.toLocal8Bit().constData(),NC_WRITE,&root));nc_ok(nc_redef(root));int time,lat,lon,var;nc_ok(nc_def_dim(root,"time",3,&time));nc_ok(nc_inq_dimid(root,"lat",&lat));nc_ok(nc_inq_dimid(root,"lon",&lon));const int dims[]={time,lat,lon};nc_ok(nc_def_var(root,"layers",NC_DOUBLE,3,dims,&var));nc_ok(nc_put_att_text(root,var,"grid_mapping",5,"earth"));nc_ok(nc_enddef(root));double values[45];for(int layer=0;layer<3;++layer)for(int i=0;i<15;++i)values[layer*15+i]=100*layer+(i%5==4?i-4:i);nc_ok(nc_put_var_double(root,var,values));nc_ok(nc_close(root));
  RasterView raster(file.toStdString(),"/layers",{3,3,5});raster.resize(720,540);raster.show();QTRY_VERIFY(raster.plane_snapshot().values);raster.findChild<QAction*>("flipHorizontalAction")->setChecked(true);const auto source_state=raster.viewport_state();raster.findChild<QAction*>("projsAction")->trigger();QTRY_VERIFY_WITH_TIMEOUT(ok_button(raster),15000);auto *map=raster.findChild<MapDisplay*>();auto *mode=raster.findChild<QComboBox*>("projectionMode");QVERIFY(map&&mode);mode->setCurrentIndex(int(Mode::LonLat));QTRY_VERIFY_WITH_TIMEOUT(map->result(),15000);const auto original_export=raster.image_for_export();const auto statistics=raster.property("minimum");
  auto *mask_start=raster.findChild<QLineEdit*>("maskStart"),*mask_end=raster.findChild<QLineEdit*>("maskEnd");QVERIFY(mask_start&&mask_end);mask_start->setText("5");mask_end->setText("8");QMetaObject::invokeMethod(mask_end,"editingFinished",Qt::DirectConnection);QVERIFY(!map->result());QTRY_VERIFY_WITH_TIMEOUT(map->result(),15000);bool masked=false;for(auto state:map->result()->states)masked=masked||state==quint8(MapRender::State::Masked);QVERIFY(masked);QVERIFY(raster.image_for_export()!=original_export);const auto masked_export=raster.image_for_export();
  const auto cached_geometry=map->result()->geometry;auto *coast=raster.findChild<QCheckBox*>("projectionCoastlines");coast->setChecked(false);QCOMPARE(map->result()->geometry,cached_geometry);QCOMPARE(raster.image_for_export(),masked_export);QCOMPARE(raster.property("minimum"),statistics);
  const auto old_colors=map->result()->image;auto *palette=raster.findChild<QComboBox*>("colorMapSelector");QVERIFY(palette);palette->setCurrentIndex((palette->currentIndex()+1)%palette->count());QVERIFY(!map->result());QVERIFY(map->property("mapProbe").toString().isEmpty());QTRY_VERIFY_WITH_TIMEOUT(map->result(),15000);QVERIFY(map->result()->image!=old_colors);QCOMPARE(map->result()->geometry,cached_geometry);
  raster.set_layer_index(0,1);QVERIFY(!map->result());raster.set_layer_index(0,2);QTRY_VERIFY_WITH_TIMEOUT(map->result(),15000);QCOMPARE(raster.layer_index(0),2);QCOMPARE(raster.plane_snapshot().values->at(0),200.);for(auto state:map->result()->states)QVERIFY(state!=quint8(MapRender::State::Masked));
  const auto result=map->result();size_t chosen=0;while(chosen<result->states.size()&&result->states[chosen]!=quint8(MapRender::State::Valid))++chosen;QVERIFY(chosen<result->states.size());const QPointF position((chosen%result->viewport.width+.5)/result->viewport.dpr,(chosen/result->viewport.width+.5)/result->viewport.dpr);QMouseEvent move(QEvent::MouseMove,position,position,Qt::NoButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(map,&move);QVERIFY(map->property("mapProbe").toString().contains("decoded"));
  mode->setCurrentIndex(0);QVERIFY(!map->active());QVERIFY(raster.findChild<QAction*>("flipHorizontalAction")->isChecked());QCOMPARE(raster.viewport_state().transform,source_state.transform);
  // Hold the pool so close must drain a real admitted task, not only a timer.
  auto *pool=QThreadPool::globalInstance();const int previous_threads=pool->maxThreadCount();pool->setMaxThreadCount(1);auto gate=std::make_shared<QSemaphore>();auto entered=std::make_shared<QSemaphore>();pool->start([gate,entered]{entered->release();gate->acquire();});const auto release_pool=qScopeGuard([pool,previous_threads,gate]{gate->release();pool->waitForDone(15000);pool->setMaxThreadCount(previous_threads);});QTRY_VERIFY_WITH_TIMEOUT(entered->available(),15000);
  MainWindow window;window.show();auto *closing=new MapDisplay(&window);closing->resize(640,480);auto in=input();closing->set_grid(in.grid,{});closing->set_source(in.source,in.values,in.original,{});closing->show_settings();closing->findChild<QComboBox*>("projectionMode")->setCurrentIndex(int(Mode::NorthPolar));ok_button(*closing)->click();QTest::qWait(100);window.close();QVERIFY(window.isVisible());QVERIFY(MapDisplay::any_busy());gate->release();QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible()&&!MapDisplay::any_busy(),15000);
}
void ProjectionTest::cliRejectsStaleAndMalformedRequests(){
  QTemporaryDir temp;const auto source=temp.filePath("source.nc");fixture(source);const auto request_file=temp.filePath("request.json");QJsonObject request{{"schema",1},{"request_id","test-stale"},{"file",source},{"variable","/t"},{"source_stamp","stale"},{"expected_shape",QJsonArray{3,5}}};auto run=[&]{QFile file(request_file);if(!file.open(QIODevice::WriteOnly))return -1;file.write(QJsonDocument(request).toJson());file.close();QProcess process;process.start(QCoreApplication::applicationDirPath()+"/hdf_cli.exe",{"geo-grid-worker",request_file});if(!process.waitForFinished(10000))return -2;return process.exitCode();};QCOMPARE(run(),0);QFile result_file(temp.filePath("grid.json"));QVERIFY(result_file.open(QIODevice::ReadOnly));auto response=QJsonDocument::fromJson(result_file.readAll()).object();result_file.close();QCOMPARE(response["request_id"],request["request_id"]);QVERIFY(!GeoGrid::from_json(response["grid"].toObject()).error.isEmpty());request["source_stamp"]=GeoGrid::stamp(source);request["expected_shape"]=QJsonArray{5,3};QCOMPARE(run(),0);QVERIFY(result_file.open(QIODevice::ReadOnly));response=QJsonDocument::fromJson(result_file.readAll()).object();result_file.close();QVERIFY(!GeoGrid::from_json(response["grid"].toObject()).error.isEmpty());
  QTemporaryDir empty;QVERIFY(!Transform::database_status(empty.path()).isEmpty());QFile corrupt(empty.filePath("proj.db"));QVERIFY(corrupt.open(QIODevice::WriteOnly));corrupt.write("not sqlite");corrupt.close();QVERIFY(!Transform::database_status(empty.path()).isEmpty());Transform explicit_projection(preset(Mode::NorthPolar,sphere()),empty.path());QPointF point;QVERIFY(explicit_projection.forward(30,70,point));
}
void ProjectionTest::optionalCoastFailureKeepsRaster(){auto in=input();QTemporaryDir temp;const auto file=temp.filePath("coast.json");for(bool corrupt:{false,true}){if(corrupt){QFile bytes(file);QVERIFY(bytes.open(QIODevice::WriteOnly));bytes.write("broken resource");}in.coastline=std::make_shared<Coastline::Data>(Coastline::load(file,Coastline::bundled_hash()));QVERIFY(!in.coastline->error.isEmpty());const auto result=MapRender::render(in);QVERIFY(result->error.isEmpty());QVERIFY(result->covered>0);QVERIFY(result->geometry->coast.empty());QVERIFY(result->geometry->diagnostic.contains(in.coastline->error));QVERIFY(!result->geometry->graticule.empty());}}
void ProjectionTest::deletedMapTaskRetainsAndReleasesBudget(){
  QTRY_VERIFY_WITH_TIMEOUT(!MapDisplay::any_busy(),15000);const auto previous=qgetenv("HDFSTUDIO_RASTER_BUDGET_MIB");const auto restore=qScopeGuard([previous]{if(previous.isNull())qunsetenv("HDFSTUDIO_RASTER_BUDGET_MIB");else qputenv("HDFSTUDIO_RASTER_BUDGET_MIB",previous);});qputenv("HDFSTUDIO_RASTER_BUDGET_MIB","160");QVERIFY(reserve_raster_bytes(128ull*1024*1024));
  auto *pool=QThreadPool::globalInstance();const int previous_threads=pool->maxThreadCount();pool->setMaxThreadCount(1);auto gate=std::make_shared<QSemaphore>();auto entered=std::make_shared<QSemaphore>();pool->start([gate,entered]{entered->release();gate->acquire();});const auto release_pool=qScopeGuard([pool,previous_threads,gate]{gate->release();pool->waitForDone(15000);pool->setMaxThreadCount(previous_threads);});QTRY_VERIFY_WITH_TIMEOUT(entered->available(),15000);
  auto map=std::make_unique<MapDisplay>();map->resize(640,480);auto in=input();map->set_grid(in.grid,{});map->set_source(in.source,in.values,in.original,{});map->show_settings();map->findChild<QComboBox*>("projectionMode")->setCurrentIndex(int(Mode::LonLat));ok_button(*map)->click();QTest::qWait(100);QVERIFY(!reserve_raster_bytes(128ull*1024*1024));map.reset();QVERIFY(MapDisplay::any_busy());QVERIFY(!reserve_raster_bytes(128ull*1024*1024));gate->release();QTRY_VERIFY_WITH_TIMEOUT(!MapDisplay::any_busy(),15000);QVERIFY(reserve_raster_bytes(128ull*1024*1024));
}
void ProjectionTest::ellipsoidPolarLambertIndependent(){const EarthModel model{6378137,6356752.314245179,"test ellipsoid"};const double e=std::sqrt(1-model.b*model.b/(model.a*model.a));
  auto conformal=[e](double phi){return std::tan(pi/4-phi/2)/std::pow((1-e*std::sin(phi))/(1+e*std::sin(phi)),e/2);};
  auto m=[e](double phi){return std::cos(phi)/std::sqrt(1-e*e*std::sin(phi)*std::sin(phi));};
  for(auto mode:{Mode::NorthPolar,Mode::SouthPolar,Mode::Lambert}){auto s=preset(mode,model);Transform transform(s);const double sign=mode==Mode::SouthPolar?-1.:1.,lon=mode==Mode::Lambert?125.:40.,lat=sign*55,phi=lat*pi/180,lambda=(lon-s.lon0)*pi/180;QPointF point,reference;QVERIFY(transform.forward(lon,lat,point));
    if(mode==Mode::Lambert){const double p1=s.lat1*pi/180,p2=s.lat2*pi/180,n=std::log(m(p1)/m(p2))/std::log(conformal(p1)/conformal(p2)),F=m(p1)/(n*std::pow(conformal(p1),n)),rho=model.a*F*std::pow(conformal(phi),n),rho0=model.a*F*std::pow(conformal(s.lat0*pi/180),n);reference={rho*std::sin(n*lambda),rho0-rho*std::cos(n*lambda)};}
    else {const double standard=std::abs(s.lat_ts)*pi/180,rho=model.a*m(standard)*conformal(std::abs(phi))/conformal(standard);reference={rho*std::sin(lambda),-sign*rho*std::cos(lambda)};}
    QVERIFY2(QLineF(point,reference).length()<.001,qPrintable(QString("ellipsoid %1 error %2").arg(name(mode)).arg(QLineF(point,reference).length(),0,'g',16)));double back_lon,back_lat;QVERIFY(transform.inverse(point,back_lon,back_lat));QVERIFY(std::abs(back_lat-lat)<1e-8);QVERIFY(std::abs(std::remainder(back_lon-lon,360))<1e-8);
  }
}
void ProjectionTest::viewportDprClipAndGeometryReuse(){auto in=input();QString error;in.viewport=fit(in.spec,720,360,2,error);auto first=MapRender::render(in);QVERIFY(first->error.isEmpty());QCOMPARE(first->viewport.dpr,2.);in.geometry=first->geometry;auto second=MapRender::render(in);QVERIFY(second->geometry==first->geometry);
  bool clip_found=false;for(auto state:first->states)clip_found=clip_found||state==quint8(MapRender::State::ClipOutside);QVERIFY(clip_found);
  in.geometry.reset();in.spec.west=170;in.spec.span=30;in.viewport=fit(in.spec,300,180,1,error);auto dateline=MapRender::render(in);QVERIFY(dateline->error.isEmpty());QVERIFY(dateline->covered>0);const auto before=dateline->indices;in.viewport.xmin+=3;in.viewport.xmax+=3;auto shifted=MapRender::render(in);QVERIFY(shifted->indices!=before);
}
void ProjectionTest::declaredBoundsAndModelConflicts(){QTemporaryDir temp;const auto file=temp.filePath("bounds.nc");fixture(file);int group=-1;nc_ok(nc_open(file.toLocal8Bit().constData(),NC_WRITE,&group));nc_ok(nc_redef(group));int latitude=-1;nc_ok(nc_inq_varid(group,"lat",&latitude));int dimension=-1,two=-1,bounds=-1;nc_ok(nc_inq_vardimid(group,latitude,&dimension));nc_ok(nc_def_dim(group,"two",2,&two));const int dims[]={dimension,two};nc_ok(nc_def_var(group,"lat_bounds",NC_DOUBLE,2,dims,&bounds));nc_ok(nc_put_att_text(group,latitude,"bounds",10,"lat_bounds"));nc_ok(nc_enddef(group));const double limits[]={-90,-30,-30,30,30,90};nc_ok(nc_put_var_double(group,bounds,limits));nc_ok(nc_close(group));auto grid=GeoGrid::read_netcdf(file,"/t",3,5);QVERIFY2(grid.error.isEmpty(),qPrintable(grid.error));QCOMPARE(grid.latitude.bounds.size(),size_t(3));QCOMPARE(GeoGrid::nearest(grid.latitude,80),qint64(2));
  nc_ok(nc_open(file.toLocal8Bit().constData(),NC_WRITE,&group));nc_ok(nc_redef(group));int mapping=-1;nc_ok(nc_inq_varid(group,"earth",&mapping));double a=6378137,b=6356752;nc_ok(nc_put_att_double(group,mapping,"semi_major_axis",NC_DOUBLE,1,&a));nc_ok(nc_put_att_double(group,mapping,"semi_minor_axis",NC_DOUBLE,1,&b));nc_ok(nc_close(group));QVERIFY(!GeoGrid::read_netcdf(file,"/t",3,5).error.isEmpty());
  QString error;const auto model=Transform::model_from_wkt("GEOGCS[\"WGS 84\",DATUM[\"WGS_1984\",SPHEROID[\"WGS 84\",6378137,298.257223563]],PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]]",error);QVERIFY2(error.isEmpty(),qPrintable(error));QVERIFY(model.valid());Transform::model_from_wkt("broken",error);QVERIFY(!error.isEmpty());
}
void ProjectionTest::gridCoverageTieBoundsAndPeriodicity(){GeoGrid::Grid g;g.latitude.centers={60,0,-60};g.longitude.centers={170,180,-170};QVERIFY(GeoGrid::validate(g).isEmpty());QCOMPARE(g.longitude.centers.back(),190.);QCOMPARE(GeoGrid::nearest(g.longitude,-175,true),qint64(1));QCOMPARE(GeoGrid::nearest(g.latitude,30),qint64(0));QCOMPARE(GeoGrid::nearest(g.latitude,70),qint64(-1));
  g.latitude.bounds={{70,50},{10,-10},{-50,-70}};QVERIFY(GeoGrid::validate(g).isEmpty());QCOMPARE(GeoGrid::nearest(g.latitude,30),qint64(-1));QCOMPARE(GeoGrid::nearest(g.latitude,65),qint64(0));g.latitude.bounds[1]={55,-10};QVERIFY(!GeoGrid::validate(g).isEmpty());
  g.latitude.bounds.clear();g.longitude.centers={0,90,180,270,360};QVERIFY(GeoGrid::validate(g).isEmpty());QVERIFY(g.duplicate_endpoint);std::vector<double> values(15,1);QVERIFY(GeoGrid::validate_frame(g,values,values).isEmpty());values[4]=2;QVERIFY(!GeoGrid::validate_frame(g,values,values).isEmpty());
}
void ProjectionTest::netcdfCoordinatesPackingGroupsAndUnknownModel(){QTemporaryDir temp;for(bool group:{false,true})for(bool packed:{false,true}){const auto file=temp.filePath(QString("fixture-%1-%2.nc").arg(group).arg(packed));fixture(file,true,packed,group);auto g=GeoGrid::read_netcdf(file,group?"/forecast/t":"/t",3,5);QVERIFY2(g.error.isEmpty(),qPrintable(g.error));QVERIFY(g.earth.valid());QCOMPARE(g.latitude.centers.front(),-60.);QVERIFY(g.duplicate_endpoint);}
  const auto unknown=temp.filePath("unknown.nc");fixture(unknown,false);const auto g=GeoGrid::read_netcdf(unknown,"/t",3,5);QVERIFY(g.error.isEmpty());QVERIFY(!g.earth.valid());const auto conflict=temp.filePath("rotated.nc");fixture(conflict,true,false,false,true);QVERIFY(!GeoGrid::read_netcdf(conflict,"/t",3,5).error.isEmpty());
}
void ProjectionTest::cliIdentityAndLifecycle(){QTemporaryDir temp;const auto file=temp.filePath("geo.nc");fixture(file);auto *controller=new GeoGridController;QPointer<GeoGridController> guard(controller);int received=0;QString error;std::shared_ptr<void> held;
  connect(controller,&GeoGridController::completed,this,[&](auto grid,auto lease){QVERIFY(grid->earth.valid());held=lease;++received;});connect(controller,&GeoGridController::failed,this,[&](const QString &message){error=message;});
  controller->read(file,"/t",3,5);controller->read(file,"/t",3,5);QTRY_VERIFY_WITH_TIMEOUT(received==1||!error.isEmpty(),15000);QVERIFY2(error.isEmpty(),qPrintable(error));QCOMPARE(received,1);QVERIFY(held);QTRY_VERIFY(!controller->busy());controller->read(file,"/t",3,5);controller->dispose();QTRY_VERIFY_WITH_TIMEOUT(!guard,15000);QCOMPARE(received,1);held.reset();QVERIFY(!GeoGridController::any_busy());
}
void ProjectionTest::samplingStatesAndPixelIndices(){auto in=input();auto values=std::make_shared<std::vector<double>>(*in.values),original=std::make_shared<std::vector<double>>(*in.original);(*values)[1]=NAN;(*values)[2]=NAN;(*original)[2]=NAN;in.values=values;in.original=original;const auto out=MapRender::render(in);QVERIFY2(out->error.isEmpty(),qPrintable(out->error));bool masked=false,missing=false,outside=false;
  Transform transform(in.spec);for(int y=0;y<out->image.height();++y)for(int x=0;x<out->image.width();++x){const size_t i=size_t(y)*out->image.width()+x;const auto state=MapRender::State(out->states[i]);masked=masked||state==MapRender::State::Masked;missing=missing||state==MapRender::State::Missing;outside=outside||state==MapRender::State::SourceOutside;
    if(out->indices[i]>=0){double lon,lat;QVERIFY(transform.inverse(in.viewport.at(x+.5,y+.5),lon,lat));const auto r=GeoGrid::nearest(in.grid->latitude,lat),c=GeoGrid::nearest(in.grid->longitude,lon,true);QCOMPARE(out->indices[i],r*4+c);QCOMPARE(out->image.pixel(x,y),in.source.pixel(int(c),int(r)));}}
  QVERIFY(masked&&missing&&outside);auto cancelled=MapRender::render(in,[]{return true;});QCOMPARE(cancelled->error,QString("Cancelled"));
}
void ProjectionTest::coastAssetIntegrityAndSeams(){const auto file=QCoreApplication::applicationDirPath()+"/resources/coastlines/low/coastlines_l.json";const auto coast=Coastline::load(file,Coastline::bundled_hash());QVERIFY2(coast.error.isEmpty(),qPrintable(coast.error));QCOMPARE(coast.parts.size(),size_t(302));size_t count=0;for(const auto &part:coast.parts)for(size_t i=0;i<part.size();++i){++count;QVERIFY(part[i].y()>-90);if(i)QVERIFY(!(std::abs(part[i].x())==180&&part[i].x()==part[i-1].x()));}QCOMPARE(count,size_t(31547));QVERIFY(!Coastline::load(file,"bad-hash").error.isEmpty());
  for(auto mode:{Mode::LonLat,Mode::NorthPolar,Mode::SouthPolar,Mode::Lambert,Mode::Mercator}){auto in=input();in.spec=preset(mode,sphere());QString error;in.viewport=fit(in.spec,400,300,1,error);in.coastline=std::make_shared<Coastline::Data>(coast);const auto result=MapRender::render(in);QVERIFY2(result->error.isEmpty(),qPrintable(result->error));QVERIFY(result->geometry);QVERIFY2(result->geometry->diagnostic.isEmpty(),qPrintable(result->geometry->diagnostic));QVERIFY(!result->geometry->coast.empty());for(const auto &line:result->geometry->coast)QVERIFY(line.length()<500);}
}
void ProjectionTest::guiModesSourcePngMaskAndLatest(){QTemporaryDir temp;const auto file=temp.filePath("display.nc");fixture(file);RasterView raster(file.toStdString(),"/t",{3,5});raster.resize(1000,760);raster.show();QTRY_VERIFY(raster.plane_snapshot().values);const auto source=raster.image_for_export();const auto before=*raster.plane_snapshot().values;
  raster.findChild<QAction*>("projsAction")->trigger();QTRY_VERIFY_WITH_TIMEOUT(ok_button(raster),15000);auto *mode=raster.findChild<QComboBox*>("projectionMode");auto *map=raster.findChild<MapDisplay*>();QVERIFY(mode&&map);
  for(auto option:{Mode::NorthPolar,Mode::SouthPolar,Mode::Lambert,Mode::LonLat,Mode::Mercator}){mode->setCurrentIndex(int(option));QTRY_VERIFY_WITH_TIMEOUT(map->result()||!map->busy(),15000);QVERIFY2(map->result(),qPrintable(map->property("mapStatus").toString()));QCOMPARE(raster.image_for_export(),source);QCOMPARE(*raster.plane_snapshot().values,before);QVERIFY(!raster.findChild<QAction*>("flipHorizontalAction")->isEnabled());}
  const auto revision=map->property("mapReadyRevision").toULongLong();map->zoom(1.25);map->zoom(.8);QTRY_VERIFY_WITH_TIMEOUT(map->property("mapReadyRevision").toULongLong()>revision&&map->result(),15000);
  mode->setCurrentIndex(0);QVERIFY(!map->active());QCOMPARE(raster.image_for_export(),source);
}
void ProjectionTest::guiBudgetCancelAndDirectDestruction(){auto *map=new MapDisplay;map->resize(640,480);auto in=input();map->set_grid(in.grid,{});map->set_source(in.source,in.values,in.original,{});map->show_settings();map->findChild<QComboBox*>("projectionMode")->setCurrentIndex(1);ok_button(*map)->click();QTRY_VERIFY_WITH_TIMEOUT(map->result(),10000);map->zoom(1.25);delete map;QTest::qWait(150);
  const auto previous=qgetenv("HDFSTUDIO_RASTER_BUDGET_MIB");const auto restore=qScopeGuard([previous]{if(previous.isNull())qunsetenv("HDFSTUDIO_RASTER_BUDGET_MIB");else qputenv("HDFSTUDIO_RASTER_BUDGET_MIB",previous);});qputenv("HDFSTUDIO_RASTER_BUDGET_MIB","1");MapDisplay limited;limited.resize(640,480);limited.set_grid(in.grid,{});limited.set_source(in.source,in.values,in.original,{});limited.show_settings();limited.findChild<QComboBox*>("projectionMode")->setCurrentIndex(1);ok_button(limited)->click();QTRY_VERIFY_WITH_TIMEOUT(limited.property("mapStatus").toString().contains("budget"),10000);QVERIFY(!limited.result());
}
void ProjectionTest::gribAlignmentAndRealSample(){const auto fixture_file=QString(HDFSTUDIO_TEST_SOURCE_DIR)+"/testdata/grib/mini_simple.grb2";auto catalog=GribData::open(fixture_file);auto plane=GribData::read(*catalog,"/field/0",{},true,false);QVERIFY2(plane.error.isEmpty(),qPrintable(plane.error));auto grid=GeoGrid::from_grib(plane,plane.geographic_keys,"regular_ll");QVERIFY2(grid.error.isEmpty(),qPrintable(grid.error));QVERIFY(grid.earth.valid());plane.coordinates.latitudes.pop_back();QVERIFY(!GeoGrid::from_grib(plane,plane.geographic_keys,"regular_ll").error.isEmpty());GribData::release(fixture_file);
  const auto real=qEnvironmentVariable("HDFSTUDIO_GRIB1_SAMPLE");if(real.isEmpty())return;catalog=GribData::open(real);const grib::Field *selected=nullptr;for(const auto &field:catalog->fields)if(field.grid_type=="regular_ll"){selected=&field;break;}QVERIFY(selected);const auto target=QString("/field/%1").arg(selected->index);plane=GribData::read(*catalog,target,{},true,false);QVERIFY2(plane.error.isEmpty(),qPrintable(plane.error));grid=GeoGrid::from_grib(plane,plane.geographic_keys,"regular_ll");QVERIFY2(grid.error.isEmpty(),qPrintable(grid.error));QVERIFY2(grid.earth.valid(),qPrintable(QString::fromUtf8(QJsonDocument(GeoGrid::json(grid)).toJson())));qInfo()<<"C7F"<<plane.rows<<plane.cols<<grid.earth.a<<grid.earth.b<<grid.earth.evidence;GribData::release(real);
}
void ProjectionTest::guiProfileFromProjectedView_data(){
  QTest::addColumn<bool>("grib");
  QTest::newRow("netcdf")<<false;
  QTest::newRow("grib")<<true;
}
void ProjectionTest::guiProfileFromProjectedView(){
  QFETCH(bool,grib);QTemporaryDir directory;
  const auto file=grib?QString(HDFSTUDIO_TEST_SOURCE_DIR)+"/testdata/grib/mini_scan_0.grb2":directory.filePath("profile-map.nc");
  if(!grib)fixture(file);
  auto raster=grib?std::make_unique<RasterView>(file,"/field/0",GribData::describe(file,"/field/0")):std::make_unique<RasterView>(file.toStdString(),"/t",std::vector<hsize_t>{3,5});
  raster->resize(1180,760);raster->show();QTRY_VERIFY_WITH_TIMEOUT(!raster->render_pending()&&raster->plane_snapshot().values,15000);
  auto *map=raster->findChild<MapDisplay*>();auto *selector=raster->findChild<QComboBox*>("mapProjectionSelector");
  auto *profile=raster->findChild<QAction*>("profileModeAction");auto *show=raster->findChild<QAction*>("showProfileAction");auto *clear=raster->findChild<QAction*>("clearProfileAction");
  auto *canvas=raster->findChild<QGraphicsView*>("rasterCanvas");QVERIFY(map&&selector&&profile&&show&&clear);
  if(!canvas)canvas=raster->findChild<QGraphicsView*>();QVERIFY(canvas);
  auto *tools=qobject_cast<QToolBar*>(profile->parent());QVERIFY(tools);auto *button=qobject_cast<QToolButton*>(tools->widgetForAction(profile));QVERIFY(button);QVERIFY(button->isVisible());
  QString output=qEnvironmentVariable("HDFSTUDIO_PROFILE_EVIDENCE_DIR");if(!output.isEmpty()){output=QDir(output).filePath(QString::fromLatin1(QTest::currentDataTag()));QVERIFY(QDir().mkpath(output));}
  auto capture=[&](const QString &name){return output.isEmpty()||raster->grab().save(QDir(output).filePath(name+".png"));};
  // Profile selected during coordinate discovery must win over the pending map request.
  selector->setCurrentIndex(int(Mode::LonLat));QTest::mouseClick(button,Qt::LeftButton);
  QTRY_VERIFY_WITH_TIMEOUT(!GeoGridController::any_busy()&&!raster->render_pending(),15000);
  QVERIFY(!map->active());QCOMPARE(selector->currentData().toInt(),int(Mode::Source));QVERIFY(profile->isChecked());
  std::vector<double> initial_values;QVariant initial_mean;
  for(auto selected:{Mode::LonLat,Mode::NorthPolar,Mode::SouthPolar,Mode::Lambert,Mode::Mercator}){
    selector->setCurrentIndex(int(selected));QTRY_VERIFY_WITH_TIMEOUT(map->result()&&!map->busy()&&map->isVisible(),20000);
    QVERIFY(capture("projected-"+QString::number(int(selected))));
    QVERIFY2(profile->isEnabled(),"Profile must remain available when a projection is displayed");
    // Operate the actual toolbar button, not the QAction signal or a private slot.
    QTest::mouseClick(button,Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(!map->active()&&canvas->isVisible()&&!raster->render_pending(),15000);
    QCOMPARE(selector->currentData().toInt(),int(Mode::Source));QVERIFY(profile->isChecked());
    if(initial_values.empty()){
      const QPoint a=canvas->mapFromScene(QPointF(.25,.3)),b=canvas->mapFromScene(QPointF(2.5,1.5));
      QTest::mouseClick(canvas->viewport(),Qt::LeftButton,Qt::NoModifier,a);QTest::mouseClick(canvas->viewport(),Qt::LeftButton,Qt::NoModifier,b);QVERIFY(show->isEnabled());
    }else QVERIFY(show->isEnabled());
    show->trigger();auto *window=raster->findChild<ProfileView*>();QVERIFY(window&&window->isVisible());auto *graph=window->findChild<ProfileCanvas*>();QVERIFY(graph&&graph->profile().valid);
    std::vector<double> current;for(const auto &point:graph->profile().points)current.push_back(point.value);
    if(initial_values.empty()){initial_values=current;initial_mean=window->property("mean");}else {QCOMPARE(current,initial_values);QCOMPARE(window->property("mean"),initial_mean);}
    if(!output.isEmpty())QVERIFY(window->grab().save(QDir(output).filePath("profile-"+QString::number(int(selected))+".png")));
    window->hide();QVERIFY(capture("source-profile-"+QString::number(int(selected))));
    // Returning to projected display leaves editing off; next Profile click re-enters once.
  }
  const QPoint original=canvas->mapFromScene(QPointF(.25,.3)),moved=canvas->mapFromScene(QPointF(.75,.7));
  QTest::mousePress(canvas->viewport(),Qt::RightButton,Qt::NoModifier,original);QTest::mouseMove(canvas->viewport(),moved);QTest::mouseRelease(canvas->viewport(),Qt::RightButton,Qt::NoModifier,moved);
  show->trigger();auto *moved_window=raster->findChild<ProfileView*>();QVERIFY(moved_window);auto *moved_graph=moved_window->findChild<ProfileCanvas*>();QVERIFY(moved_graph&&moved_graph->profile().valid);
  QVERIFY(std::abs(moved_graph->profile().points.front().value-initial_values.front())>1e-6);
  clear->trigger();QVERIFY(!show->isEnabled());QVERIFY(!clear->isEnabled());
  raster.reset();QTRY_VERIFY_WITH_TIMEOUT(!MapDisplay::any_busy()&&!GeoGridController::any_busy(),15000);
}
QTEST_MAIN(ProjectionTest)
#include "projection_test.moc"
