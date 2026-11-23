// GRIB analysis gates. Inputs: read-only fixtures and optional external sample/cases environment.
// Output: HDFSTUDIO_GRIB_EVIDENCE; one completed analysis feature per local commit.
#include <QtTest>
#include <QScopeGuard>
#include <cmath>
#include <numeric>
#include "hdf_explorer.hpp"
#include "histogram_view.hpp"
#include "data_read_controller.hpp"
#include "raster_budget.hpp"
#include "texture_entropy_view.hpp"
#include "raster_view.hpp"
#include "profile_view.hpp"
#include "image_metrics.hpp"
#include "compare_panel.hpp"
#include "residual_view.hpp"
#include "workspace_sync.hpp"
#include "nc_reader.hpp"

class GribAnalysisTest:public QObject {
  Q_OBJECT
  QString fixture(const QString &name)const{return QString(HDFSTUDIO_TEST_SOURCE_DIR)+"/testdata/grib/"+name;}
  QSpinBox *option(HistogramView &view,const QString &name)const{
    for(auto *spin:view.findChildren<QSpinBox*>())if(spin->accessibleName()==name)return spin;
    return nullptr;
  }

  ProfileView *open_profile(RasterView &view, const std::vector<QPointF> &controls) const {
    auto *canvas = view.findChild<QGraphicsView*>();
    view.findChild<QAction*>("fitImageAction")->trigger();
    view.findChild<QAction*>("profileModeAction")->setChecked(true);
    for(const auto &point : controls)
      QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, canvas->mapFromScene(point));
    view.findChild<QAction*>("showProfileAction")->trigger();
    return view.findChild<ProfileView*>();
  }

private slots:
  void comparisonIdentityDiagnostics() {
    PlaneSnapshot a, b;
    a.units = b.units = "unknown";
    QVERIFY(plane_pair_diagnostics(a, b).contains("Units unverified"));
    a.units = "K"; b.units = "m";
    a.grid_identity = "grid-a"; b.grid_identity = "grid-b";
    a.time_identity = "time-a"; b.time_identity = "time-b";
    const auto diagnostic = plane_pair_diagnostics(a, b);
    QVERIFY(diagnostic.contains("Unit conflict"));
    QVERIFY(diagnostic.contains("Grid identity conflict"));
    QVERIFY(diagnostic.contains("Time identity conflict"));
    b = a;
    QVERIFY(plane_pair_diagnostics(a, b).isEmpty());
  }


  void comparisonMaskMissingExportAndLifetime() {
    const auto file = fixture("mini_missing_slice.grb2");
    const auto target = GribData::open(file)->cubes.front().id;
    RasterView a(file, target, GribData::describe(file, target));
    auto b = std::make_unique<RasterView>(file, target, GribData::describe(file, target));
    for(auto *view : {&a, b.get()}) { view->resize(900, 650); view->show(); }
    QTRY_VERIFY(a.plane_snapshot().values && b->plane_snapshot().values);
    WorkspaceSyncController sync;
    ComparePanel panel(&sync);
    panel.set_views({&a, b.get()}); panel.show(); panel.refresh_now();
    auto *text = panel.findChild<QPlainTextEdit*>("compareMetricsText");
    QVERIFY(text->toPlainText().contains("Status: Ready"));
    QVERIFY(text->toPlainText().contains("Pairing: by index"));
    panel.findChild<QPushButton*>("compareResidualButton")->click();
    auto *residual = panel.findChild<ResidualView*>();
    QVERIFY(residual && residual->residual().valid);
    QCOMPARE(residual->residual().absolute_max, 0.);
    const auto a_reads = a.property("planeReadCount"), b_reads = b->property("planeReadCount");
    b->findChild<QLineEdit*>("maskStart")->setText("50000");
    b->findChild<QLineEdit*>("maskEnd")->setText("50002");
    QVERIFY(QMetaObject::invokeMethod(b->findChild<QLineEdit*>("maskEnd"), "editingFinished"));
    QTRY_COMPARE(residual->residual().excluded_pairs, size_t(3));
    QCOMPARE(residual->residual().valid_pairs, size_t(9));
    QCOMPARE(a.property("planeReadCount"), a_reads);
    QCOMPARE(b->property("planeReadCount"), b_reads);
    const auto computations = panel.property("metricsComputeCount");
    a.findChild<QAction*>("zoomInAction")->trigger();
    QCoreApplication::processEvents();
    QCOMPARE(panel.property("metricsComputeCount"), computations);
    QTemporaryDir dir;
    QVERIFY(panel.export_csv(dir.filePath("metrics.csv")).isEmpty());
    QVERIFY(residual->export_png(dir.filePath("residual.png")).isEmpty());
    QVERIFY(panel.export_csv(file).contains("overwrite"));
    QVERIFY(residual->export_png(file).contains("overwrite"));
    a.set_layer_index(0, 1);
    QVERIFY(!a.plane_snapshot().values);
    QVERIFY(!residual->residual().valid);
    QVERIFY(!residual->findChild<QAction*>("residualExportPngAction")->isEnabled());
    QVERIFY(text->toPlainText().contains("RMSE: N/A"));
    QVERIFY(!residual->export_png(dir.filePath("invalid.png")).isEmpty());
    QTRY_VERIFY(!a.render_pending() && !b->render_pending());
    a.set_layer_index(0, 0);
    QTRY_VERIFY(a.plane_snapshot().values && b->plane_snapshot().values);
    QTRY_VERIFY(residual->residual().valid);
    b.reset();
    QVERIFY(!residual->residual().valid);
    QTRY_VERIFY(text->toPlainText().contains("Select two different"));
  }


  void comparisonCrossFormatDecodedIndexPairing() {
    QTemporaryDir dir;
    const auto filename = dir.filePath("decoded.nc");
    int file = -1, y = -1, x = -1, variable = -1;
    QCOMPARE(nc_create(nc_path_bytes(filename).constData(), NC_CLOBBER, &file), NC_NOERR);
    QCOMPARE(nc_def_dim(file, "y", 3, &y), NC_NOERR);
    QCOMPARE(nc_def_dim(file, "x", 4, &x), NC_NOERR);
    int dimensions[] = {y, x};
    QCOMPARE(nc_def_var(file, "field", NC_DOUBLE, 2, dimensions, &variable), NC_NOERR);
    QCOMPARE(nc_put_att_text(file, variable, "units", 1, "m"), NC_NOERR);
    QCOMPARE(nc_enddef(file), NC_NOERR);
    std::vector<double> values(12);
    for(size_t i = 0; i < values.size(); ++i) values[i] = 2 * (85000. + double(i)) + 3;
    QCOMPARE(nc_put_var_double(file, variable, values.data()), NC_NOERR);
    QCOMPARE(nc_close(file), NC_NOERR);
    const auto grib = fixture("mini_simple.grb2");
    RasterView a(grib, "/field/0", GribData::describe(grib, "/field/0"));
    RasterView b(filename.toLocal8Bit().toStdString(), "/field", {3, 4});
    a.show(); b.show();
    QTRY_VERIFY(a.plane_snapshot().values && b.plane_snapshot().values);
    WorkspaceSyncController sync;
    ComparePanel panel(&sync); panel.set_views({&a, &b}); panel.show(); panel.refresh_now();
    auto *text = panel.findChild<QPlainTextEdit*>("compareMetricsText");
    QVERIFY(text->toPlainText().contains("Unit conflict"));
    QVERIFY(text->toPlainText().contains("Grid correspondence unverified"));
    QVERIFY(text->toPlainText().contains("Pearson r: 1"));
    panel.findChild<QPushButton*>("compareResidualButton")->click();
    const auto &result = panel.findChild<ResidualView*>()->residual();
    QVERIFY(result.valid);
    for(size_t i = 0; i < values.size(); ++i) QCOMPARE(result.values[i], 85003. + double(i));
    QVERIFY(panel.comparison_csv().contains("\"by index\""));
    QVERIFY(panel.export_csv(filename).contains("overwrite"));
  }


  void comparisonExternalCorpus_data() {
    QTest::addColumn<int>("edition");
    QTest::newRow("grib1") << 1;
    QTest::newRow("grib2") << 2;
  }


  void comparisonExternalCorpus() {
    QFETCH(int, edition);
    const auto prefix = QString("HDFSTUDIO_GRIB%1_").arg(edition);
    const auto source = qEnvironmentVariable((prefix + "SAMPLE").toLatin1());
    if(source.isEmpty()) QSKIP("Optional external corpus not configured");
    QFile input(qEnvironmentVariable((prefix + "CASES").toLatin1()));
    QVERIFY(input.open(QIODevice::ReadOnly));
    const auto entries = QJsonDocument::fromJson(input.readAll()).object()["fields"].toArray();
    const auto evidence = qEnvironmentVariable("HDFSTUDIO_GRIB_EVIDENCE");
    QVERIFY(!evidence.isEmpty());
    RasterView anchor(source, "/field/0", GribData::describe(source, "/field/0"));
    anchor.resize(900, 650); anchor.show();
    QTRY_VERIFY_WITH_TIMEOUT(anchor.plane_snapshot().values && !anchor.render_pending(), 15000);
    for(const auto entry : entries) {
      const auto item = entry.toObject();
      const auto target = item["target"].toString(), tag = item["tag"].toString();
      RasterView other(source, target, GribData::describe(source, target));
      other.resize(900, 650); other.show();
      QTRY_VERIFY_WITH_TIMEOUT(other.plane_snapshot().values && !other.render_pending(), 15000);
      WorkspaceSyncController sync;
      ComparePanel panel(&sync);
      panel.set_views({&anchor, &other}); panel.resize(750, 750); panel.show(); panel.refresh_now();
      panel.findChild<QPushButton*>("compareResidualButton")->click();
      auto *residual = panel.findChild<ResidualView*>();
      QVERIFY(residual && residual->residual().valid);
      const auto stem = evidence + QString("/compare-grib%1-%2").arg(edition).arg(tag);
      QVERIFY(panel.export_csv(stem + ".csv").isEmpty());
      QFile output(stem + ".f64");
      QVERIFY(output.open(QIODevice::WriteOnly));
      QDataStream stream(&output); stream.setByteOrder(QDataStream::LittleEndian);
      for(double value : residual->residual().values) stream << value;
      QCOMPARE(stream.status(), QDataStream::Ok);
      QVERIFY(residual->export_png(stem + ".png").isEmpty());
      if(target == "/field/0") {
        QVERIFY(panel.grab().save(stem + "-panel.png"));
        QVERIFY(residual->grab().save(stem + "-window.png"));
      }
    }
  }


  void profileDecodedMaskMissingAndRecovery() {
    const auto file = fixture("mini_missing_slice.grb2");
    const auto target = GribData::open(file)->cubes.front().id;
    RasterView view(file, target, GribData::describe(file, target));
    view.resize(1200, 850);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    QTRY_VERIFY(view.plane_snapshot().values && !view.render_pending());
    auto *profile = open_profile(view, {{.25, .25}, {2.75, 1.75}});
    QVERIFY(profile);
    auto *canvas = profile->findChild<ProfileCanvas*>();
    QVERIFY(canvas->profile().valid);
    QVERIFY(canvas->profile().finite_count > 2);
    for(const auto &point : canvas->profile().points)
      QVERIFY(std::abs(point.value - (50000. + 4 * point.y + point.x)) < 2e-11);
    QVERIFY(profile->findChild<QAction*>("exportProfileCsvAction")->isEnabled());

    const auto reads = view.property("planeReadCount");
    view.findChild<QLineEdit*>("maskStart")->setText("50000");
    view.findChild<QLineEdit*>("maskEnd")->setText("50005");
    QVERIFY(QMetaObject::invokeMethod(view.findChild<QLineEdit*>("maskEnd"), "editingFinished"));
    QVERIFY(canvas->profile().missing_count > 0);
    QCOMPARE(view.property("planeReadCount"), reads);
    QCOMPARE(view.property("entropyBits").toDouble(), nc_gray_entropy(*view.plane_snapshot().values).entropy_bits);

    view.set_layer_index(0, 1);
    QVERIFY(!canvas->profile().valid);
    QVERIFY(canvas->profile().points.empty());
    QVERIFY(!profile->findChild<QAction*>("exportProfileCsvAction")->isEnabled());
    QVERIFY(!view.property("entropyBits").isValid());
    QTRY_VERIFY(!view.render_pending());
    QVERIFY(view.findChild<QLabel*>("rasterLegend")->text().contains("MissingSlice"));
    view.findChild<QLineEdit*>("maskStart")->clear();
    view.findChild<QLineEdit*>("maskEnd")->clear();
    view.set_layer_index(0, 0);
    QTRY_VERIFY(view.plane_snapshot().values && !view.render_pending());
    QVERIFY(canvas->profile().valid);
    QCOMPARE(canvas->profile().missing_count, size_t(0));
    view.findChild<QAction*>("clearProfileAction")->trigger();
    QVERIFY(!canvas->profile().valid);
    QVERIFY(!profile->findChild<QAction*>("exportProfileCsvAction")->isEnabled());
  }


  void profileRetainsPlaneLeaseUntilDataRelease() {
    auto lease = reserve_raster_bytes(64 * 1024);
    QVERIFY(lease);
    std::weak_ptr<void> retained = lease;
    auto values = std::make_shared<const std::vector<double>>(std::vector<double>{1, 2, 3, 4});
    ProfileView profile("lease", values, 2, 2, {{0, 0}, {1, 1}}, CurvePathMode::Polyline, nullptr, lease);
    lease.reset();
    values.reset();
    QVERIFY(!retained.expired());
    profile.update_data({}, 0, 0, {});
    QVERIFY(retained.expired());
    QVERIFY(!profile.findChild<QAction*>("exportProfileCsvAction")->isEnabled());
  }


  void profileExternalCorpus_data() {
    QTest::addColumn<int>("edition");
    QTest::newRow("grib1") << 1;
    QTest::newRow("grib2") << 2;
  }


  void profileExternalCorpus() {
    QFETCH(int, edition);
    const auto prefix = QString("HDFSTUDIO_GRIB%1_").arg(edition);
    const auto source = qEnvironmentVariable((prefix + "SAMPLE").toLatin1());
    if(source.isEmpty()) QSKIP("Optional external corpus not configured");
    QFile input(qEnvironmentVariable((prefix + "CASES").toLatin1()));
    QVERIFY(input.open(QIODevice::ReadOnly));
    const auto entries = QJsonDocument::fromJson(input.readAll()).object()["fields"].toArray();
    const auto evidence = qEnvironmentVariable("HDFSTUDIO_GRIB_EVIDENCE");
    QVERIFY(!evidence.isEmpty());
    QJsonObject report;
    for(const auto entry : entries) {
      const auto item = entry.toObject();
      const auto target = item["target"].toString(), tag = item["tag"].toString();
      const auto info = GribData::describe(source, target);
      RasterView view(source, target, info);
      view.resize(1200, 850);
      view.show();
      QVERIFY(QTest::qWaitForWindowExposed(&view));
      QTRY_VERIFY_WITH_TIMEOUT(view.plane_snapshot().values && !view.render_pending(), 15000);
      const auto snapshot = view.plane_snapshot();
      const double cols = double(snapshot.cols - 1), rows = double(snapshot.rows - 1);
      auto *profile = open_profile(view, {{.15 * cols, .25 * rows}, {.5 * cols, .75 * rows}, {.85 * cols, .35 * rows}});
      QVERIFY(profile);
      const auto &result = profile->findChild<ProfileCanvas*>()->profile();
      QVERIFY(result.valid);
      QJsonArray points;
      for(const auto &point : result.points)
        points.append(QJsonArray{point.distance, point.x, point.y, std::isfinite(point.value) ? QJsonValue(point.value) : QJsonValue()});
      QFile output(evidence + QString("/profile-grib%1-%2.json").arg(edition).arg(tag));
      QVERIFY(output.open(QIODevice::WriteOnly));
      QVERIFY(output.write(QJsonDocument(QJsonObject{{"points", points}, {"finite", double(result.finite_count)}, {"missing", double(result.missing_count)}}).toJson()) > 0);
      report[tag] = QJsonObject{{"entropy_bits", view.property("entropyBits").toDouble()}, {"rows", double(snapshot.rows)}, {"cols", double(snapshot.cols)}};
      if(target == "/field/0") {
        QVERIFY(profile->grab().save(evidence + QString("/profile-grib%1.png").arg(edition)));
        QVERIFY(view.grab().save(evidence + QString("/raster-profile-grib%1.png").arg(edition)));
      }
    }
    QFile output(evidence + QString("/profile-grib%1.json").arg(edition));
    QVERIFY(output.open(QIODevice::WriteOnly));
    QVERIFY(output.write(QJsonDocument(report).toJson()) > 0);
  }


  void textureDecodedMaskCacheAndExport(){
    const auto file=fixture("mini_simple.grb2");TextureEntropyView view(file,"/field/0",GribData::describe(file,"/field/0"));
    QVERIFY(view.property("reading").toBool());QVERIFY(!view.findChild<QAction*>("textureExportCsvAction")->isEnabled());QTRY_VERIFY(view.entropy_plane().valid);
    QCOMPARE(view.property("reader").toString(),QString("ecCodes"));QCOMPARE(view.property("decodeProfile").toString(),QString("GribDecodedV1"));
    const auto reads=view.property("planeReadCount").toULongLong();QCOMPARE(reads,1ull);QCOMPARE(view.entropy_plane().valid_count,size_t(12));
    auto *start=view.findChild<QLineEdit*>("textureMaskStart"),*end=view.findChild<QLineEdit*>("textureMaskEnd");start->setText("85000");end->setText("85001");view.findChild<QPushButton*>("textureApplyMask")->click();
    QCOMPARE(view.property("planeReadCount").toULongLong(),reads);QCOMPARE(view.entropy_plane().excluded_count,size_t(2));QVERIFY(std::isnan(view.entropy_plane().values[0]));
    QTemporaryDir temporary;QVERIFY(view.export_csv(temporary.filePath("masked.csv")).isEmpty());QVERIFY(view.export_png(temporary.filePath("masked.png")).isEmpty());QCOMPARE(QImage(temporary.filePath("masked.png")).size(),QSize(4,3));
    QVERIFY(view.export_csv(file).contains("overwrite"));QVERIFY(view.export_png(file).contains("overwrite"));
    start->clear();end->clear();view.findChild<QPushButton*>("textureApplyMask")->click();QCOMPARE(view.entropy_plane().excluded_count,size_t(0));QCOMPARE(view.property("planeReadCount").toULongLong(),reads);
    for(const auto &name:{"mini_bitmap.grb2","mini_constant.grb2","mini_unknown.grb2"}){
      const auto source=fixture(name);TextureEntropyView special(source,"/field/0",GribData::describe(source,"/field/0"));QTRY_VERIFY(!special.property("reading").toBool());
      const auto decoded=GribData::read(source,"/field/0",{},false,false);const auto expected=nc_texture_entropy(decoded.values,decoded.rows,decoded.cols);QCOMPARE(special.entropy_plane().valid,expected.valid);QCOMPARE(special.entropy_plane().excluded_count,expected.excluded_count);
      if(name==QString("mini_unknown.grb2"))QCOMPARE(special.property("units").toString(),QString("unknown"));
      if(name==QString("mini_constant.grb2"))for(double value:special.entropy_plane().values)QCOMPARE(value,0.);
    }
  }
  void textureLatestSourceBudgetAndLifetime(){
    const auto file=fixture("mini_missing_slice.grb2"),target=GribData::open(file)->cubes.front().id;
    {TextureEntropyView view(file,target,GribData::describe(file,target));QTRY_VERIFY(view.entropy_plane().valid);const auto first=view.entropy_plane().source_minimum;
      view.set_layer_index(0,1);QVERIFY(!view.entropy_plane().valid);QVERIFY(view.image_for_export().isNull());QVERIFY(!view.findChild<QAction*>("textureExportCsvAction")->isEnabled());
      QTRY_VERIFY(!view.property("reading").toBool());QVERIFY(view.property("readError").toString().contains("MissingSlice"));
      for(int index=0;index<14;++index)view.set_layer_index(0,index%2);view.set_layer_index(0,0);QTRY_VERIFY(view.entropy_plane().valid);QCOMPARE(view.entropy_plane().source_minimum,first);
    }QTRY_VERIFY(!DataReadController::any_busy());
    QTemporaryDir temporary;const auto changed=temporary.filePath("changed.grb2");QVERIFY(QFile::copy(fixture("mini_cube.grb2"),changed));auto release=qScopeGuard([&]{GribData::release(changed);});
    const auto cube=GribData::open(changed)->cubes.front().id;
    {TextureEntropyView stale(changed,cube,GribData::describe(changed,cube));QTRY_VERIFY(stale.entropy_plane().valid);
      {QFile output(changed);QVERIFY(output.open(QIODevice::Append));QVERIFY(output.write("changed")>0);}
      QVERIFY(stale.export_csv(temporary.filePath("stale.csv")).contains("SourceChanged"));QVERIFY(!QFileInfo::exists(temporary.filePath("stale.csv")));
      stale.set_layer_index(0,1);QVERIFY(!stale.entropy_plane().valid);QVERIFY(stale.property("readError").toString().contains("SourceChanged"));
    }
    auto reservation=reserve_raster_bytes(1024ull*1024*1024);QVERIFY(reservation);
    {TextureEntropyView denied(file,target,GribData::describe(file,target));QTRY_VERIFY(!denied.property("reading").toBool());QVERIFY(denied.property("readError").toString().contains("MemoryBudget"));}reservation.reset();
    auto *closing=new TextureEntropyView(file,target,GribData::describe(file,target));delete closing;QTRY_VERIFY(!DataReadController::any_busy());
  }
  void textureTreeAndFollowingSnapshot(){
    auto *window=new MainWindow;window->resize(1200,800);window->show();QVERIFY(QTest::qWaitForWindowExposed(window));QCOMPARE(window->read_file(fixture("mini_cube.grb2")),0);
    auto *tree=window->findChild<FileTreeWidget*>();auto *root=tree->topLevelItem(0);root->setExpanded(true);auto *variables=root->child(0);variables->setExpanded(true);auto *item=variables->child(0);tree->setCurrentItem(item);
    bool enabled=false;QTimer timer;timer.setInterval(20);connect(&timer,&QTimer::timeout,window,[&]{for(auto *menu:window->findChildren<QMenu*>())if(auto *action=menu->findChild<QAction*>("actionGribTextureEntropy")){enabled=action->isEnabled();action->trigger();menu->close();}});timer.start();
    QVERIFY(QMetaObject::invokeMethod(tree,"show_context_menu",Qt::DirectConnection,Q_ARG(QPoint,tree->visualItemRect(item).center())));timer.stop();QVERIFY(enabled);
    auto *independent=window->findChild<TextureEntropyView*>();QVERIFY(independent);QTRY_VERIFY(independent->entropy_plane().valid);QVERIFY(!independent->source_view());window->close_file_of(root);QVERIFY(!window->findChild<TextureEntropyView*>());delete window;QTRY_VERIFY(!DataReadController::any_busy());
    const auto file=fixture("mini_cube.grb2"),target=GribData::open(file)->cubes.front().id;auto *source=new RasterView(file,target,GribData::describe(file,target));
    QTRY_VERIFY(source->plane_snapshot().values);auto *following=new TextureEntropyView(source);QVERIFY(following->entropy_plane().valid);QCOMPARE(following->source_view(),source);
    source->set_layer_index(0,1);QVERIFY(!following->entropy_plane().valid);QTRY_VERIFY(following->entropy_plane().valid);QCOMPARE(following->entropy_plane().source_minimum,50600.);
    source->findChild<QLineEdit*>("maskStart")->setText("50600");source->findChild<QLineEdit*>("maskEnd")->setText("50602");QVERIFY(QMetaObject::invokeMethod(source->findChild<QLineEdit*>("maskEnd"),"editingFinished"));QTRY_COMPARE(following->entropy_plane().excluded_count,size_t(3));
    delete source;QVERIFY(!following->entropy_plane().valid);delete following;QTRY_VERIFY(!DataReadController::any_busy());
    MainWindow host;auto *mdi=host.findChild<QMdiArea*>();auto *raster=new RasterView(file,target,GribData::describe(file,target));auto *sub=mdi->addSubWindow(raster);sub->show();QTRY_VERIFY(raster->plane_snapshot().values);mdi->setActiveSubWindow(sub);QVERIFY(QMetaObject::invokeMethod(&host,"show_texture_entropy"));QVERIFY(host.findChild<TextureEntropyView*>());
  }
  void textureExternalCorpus_data(){QTest::addColumn<int>("edition");QTest::newRow("grib1")<<1;QTest::newRow("grib2")<<2;}
  void textureExternalCorpus(){
    QFETCH(int,edition);const auto prefix=QString("HDFSTUDIO_GRIB%1_").arg(edition);const auto source=qEnvironmentVariable((prefix+"SAMPLE").toLatin1()),cases=qEnvironmentVariable((prefix+"CASES").toLatin1());
    if(source.isEmpty())QSKIP("Optional external corpus not configured");QFile input(cases);QVERIFY(input.open(QIODevice::ReadOnly));const auto reference=QJsonDocument::fromJson(input.readAll()).object();
    const auto evidence=qEnvironmentVariable("HDFSTUDIO_GRIB_EVIDENCE");QVERIFY(!evidence.isEmpty());QJsonObject report;
    for(const auto entry:reference["fields"].toArray()){
      const auto item=entry.toObject();const auto target=item["target"].toString(),tag=item["tag"].toString();TextureEntropyView view(source,target,GribData::describe(source,target));QTRY_VERIFY_WITH_TIMEOUT(!view.property("reading").toBool(),15000);QVERIFY2(view.entropy_plane().valid,qPrintable(view.property("readError").toString()));
      const auto &plane=view.entropy_plane();QJsonObject data{{"rows",double(plane.rows)},{"cols",double(plane.cols)},{"valid",double(plane.valid_count)},{"excluded",double(plane.excluded_count)}};
      QFile binary(evidence+QString("/texture-grib%1-%2.f64").arg(edition).arg(tag));QVERIFY(binary.open(QIODevice::WriteOnly));QCOMPARE(binary.write(reinterpret_cast<const char*>(plane.values.data()),qint64(plane.values.size()*sizeof(double))),qint64(plane.values.size()*sizeof(double)));
      const auto decoded=GribData::read(source,target,{},false,false);QVERIFY(decoded.error.isEmpty());QFile decoded_output(evidence+QString("/decoded-grib%1-%2.f64").arg(edition).arg(tag));QVERIFY(decoded_output.open(QIODevice::WriteOnly));QCOMPARE(decoded_output.write(reinterpret_cast<const char*>(decoded.values.data()),qint64(decoded.values.size()*sizeof(double))),qint64(decoded.values.size()*sizeof(double)));
      if(target=="/field/0"){
        const auto base=evidence+QString("/texture-grib%1-%2").arg(edition).arg(tag);QVERIFY(view.export_csv(base+".csv").isEmpty());QVERIFY(view.export_png(base+".png").isEmpty());
        view.resize(1000,760);view.show();QVERIFY(QTest::qWaitForWindowExposed(&view));QVERIFY(view.grab().save(base+"-window.png"));
      }
      report[tag]=data;
    }
    QFile output(evidence+QString("/texture-grib%1.json").arg(edition));QVERIFY(output.open(QIODevice::WriteOnly));QVERIFY(output.write(QJsonDocument(report).toJson())>0);
  }
  void histogramDecodedSamplingAndMissing(){
    const auto source=fixture("mini_simple.grb2");HistogramView view(source,"/field/0",GribData::describe(source,"/field/0"));
    QVERIFY(view.property("reading").toBool());QTRY_COMPARE(view.property("planeValues").toULongLong(),12ull);
    QCOMPARE(view.property("reader").toString(),QString("ecCodes"));QCOMPARE(view.property("decodeProfile").toString(),QString("GribDecodedV1"));
    QCOMPARE(view.property("units").toString(),QString("K"));QCOMPARE(view.property("sampled").toULongLong(),1ull);
    const auto reads=view.property("planeReadCount").toULongLong();auto *every=option(view,"Sample 1 in N"),*bins=option(view,"Bins");QVERIFY(every);QVERIFY(bins);
    every->setValue(1);bins->setValue(6);const auto full=view.findChild<HistogramCanvas*>()->histogram();QVERIFY(full.valid);
    QCOMPARE(full.total,size_t(12));QCOMPARE(full.missing,size_t(0));QCOMPARE(full.minimum,85000.);QCOMPARE(full.maximum,85011.);QCOMPARE(full.mean,85005.5);
    QCOMPARE(full.counts,std::vector<size_t>({2,2,2,2,2,2}));
    every->setValue(3);QCOMPARE(view.property("sampled").toULongLong(),4ull);bins->setValue(12);QCOMPARE(view.property("planeReadCount").toULongLong(),reads);
    for(const auto &name:{"mini_bitmap.grb2","mini_grid_complex_spatial_differencing.grb2","mini_constant.grb2","mini_unknown.grb2"}){
      const auto file=fixture(name);QVERIFY(QFileInfo::exists(file));
      HistogramView missing(file,"/field/0",GribData::describe(file,"/field/0"));QTRY_VERIFY(!missing.property("reading").toBool());
      auto expected=GribData::read(file,"/field/0",{},false,false);QVERIFY(expected.error.isEmpty());option(missing,"Sample 1 in N")->setValue(1);
      const auto histogram=missing.findChild<HistogramCanvas*>()->histogram();QCOMPARE(histogram.total+histogram.missing,expected.values.size());
      QCOMPARE(histogram.missing,size_t(std::count_if(expected.values.begin(),expected.values.end(),[](double value){return !std::isfinite(value);})));
    }
    const auto evidence=qEnvironmentVariable("HDFSTUDIO_GRIB_EVIDENCE");if(!evidence.isEmpty()){view.resize(1000,650);view.show();QVERIFY(QTest::qWaitForWindowExposed(&view));QVERIFY(view.grab().save(evidence+"/histogram-fixture.png"));}
  }
  void histogramLatestMissingSourceAndBudget(){
    const auto file=fixture("mini_missing_slice.grb2"),target=GribData::open(file)->cubes.front().id;
    auto *view=new HistogramView(file,target,GribData::describe(file,target));QTRY_VERIFY(view->property("planeValues").toULongLong()>0);
    option(*view,"Sample 1 in N")->setValue(1);
    const double first_minimum=view->property("minimum").toDouble();
    view->set_layer_index(0,1);QVERIFY(!view->property("sampled").isValid());QVERIFY(!view->findChild<HistogramCanvas*>()->histogram().valid);
    QTRY_VERIFY(!view->property("reading").toBool());QVERIFY(view->property("readError").toString().contains("MissingSlice"));
    view->set_layer_index(0,0);QTRY_VERIFY(view->property("planeValues").toULongLong()>0);
    for(int index=0;index<18;++index)view->set_layer_index(0,index%2);view->set_layer_index(0,0);
    QTRY_VERIFY(!view->property("reading").toBool());QCOMPARE(view->property("minimum").toDouble(),first_minimum);
    delete view;QTRY_VERIFY(!DataReadController::any_busy());
    QTemporaryDir temporary;const auto changed=temporary.filePath("changed.grb2");QVERIFY(QFile::copy(fixture("mini_cube.grb2"),changed));
    auto release=qScopeGuard([&]{GribData::release(changed);});const auto cube=GribData::open(changed)->cubes.front().id;
    HistogramView stale(changed,cube,GribData::describe(changed,cube));QTRY_VERIFY(stale.property("planeValues").toULongLong()>0);
    {QFile output(changed);QVERIFY(output.open(QIODevice::Append));QVERIFY(output.write("changed")>0);}
    stale.set_layer_index(0,1);QVERIFY(!stale.property("minimum").isValid());QTRY_VERIFY(!stale.property("reading").toBool());QVERIFY(stale.property("readError").toString().contains("SourceChanged"));
    auto reservation=reserve_raster_bytes(1024ull*1024*1024);QVERIFY(reservation);
    HistogramView denied(file,target,GribData::describe(file,target));QTRY_VERIFY(!denied.property("reading").toBool());QVERIFY(denied.property("readError").toString().contains("MemoryBudget"));reservation.reset();
    auto *closing=new HistogramView(file,target,GribData::describe(file,target));delete closing;QTRY_VERIFY(!DataReadController::any_busy());
  }
  void histogramTreeMenuAndMainWindowLifetime(){
    auto *window=new MainWindow;window->resize(1200,800);window->show();QVERIFY(QTest::qWaitForWindowExposed(window));QCOMPARE(window->read_file(fixture("mini_cube.grb2")),0);
    auto *tree=window->findChild<FileTreeWidget*>();auto *root=tree->topLevelItem(0);root->setExpanded(true);auto *variables=root->child(0);variables->setExpanded(true);auto *item=variables->child(0);tree->setCurrentItem(item);
    bool enabled=false;QTimer timer;timer.setInterval(20);connect(&timer,&QTimer::timeout,window,[&]{for(auto *menu:window->findChildren<QMenu*>())if(auto *action=menu->findChild<QAction*>("actionGribHistogram")){enabled=action->isEnabled();action->trigger();menu->close();}});timer.start();
    QVERIFY(QMetaObject::invokeMethod(tree,"show_context_menu",Qt::DirectConnection,Q_ARG(QPoint,tree->visualItemRect(item).center())));timer.stop();QVERIFY(enabled);
    auto *view=window->findChild<HistogramView*>();QVERIFY(view);QTRY_VERIFY(view->property("planeValues").toULongLong()>0);QCOMPARE(window->current_layer_view(),static_cast<LayerNavigation*>(view));
    window->close_file_of(root);QVERIFY(!window->findChild<HistogramView*>());delete window;QTRY_VERIFY(!DataReadController::any_busy());
  }
  void histogramExternalCorpus_data(){QTest::addColumn<int>("edition");QTest::newRow("grib1")<<1;QTest::newRow("grib2")<<2;}
  void histogramExternalCorpus(){
    QFETCH(int,edition);const auto prefix=QString("HDFSTUDIO_GRIB%1_").arg(edition);const auto source=qEnvironmentVariable((prefix+"SAMPLE").toLatin1()),cases=qEnvironmentVariable((prefix+"CASES").toLatin1());
    if(source.isEmpty())QSKIP("Optional external corpus not configured");QVERIFY(QFileInfo::exists(source));QFile input(cases);QVERIFY(input.open(QIODevice::ReadOnly));const auto reference=QJsonDocument::fromJson(input.readAll()).object();
    const auto evidence=qEnvironmentVariable("HDFSTUDIO_GRIB_EVIDENCE");QVERIFY(!evidence.isEmpty());QJsonObject report;
    for(const auto entry:reference["fields"].toArray()){
      const auto item=entry.toObject();const auto target=item["target"].toString();HistogramView view(source,target,GribData::describe(source,target));QTRY_VERIFY_WITH_TIMEOUT(!view.property("reading").toBool(),15000);
      QVERIFY2(view.property("readError").toString().isEmpty(),qPrintable(view.property("readError").toString()));option(view,"Sample 1 in N")->setValue(1);option(view,"Bins")->setValue(32);
      const auto histogram=view.findChild<HistogramCanvas*>()->histogram();QVERIFY(histogram.valid);QJsonObject result;
      for(const auto key:{"sampled","total","missing","minimum","maximum","mean","stddev","entropyBits","units","reader","decodeProfile"})result[key]=QJsonValue::fromVariant(view.property(key));
      QJsonArray counts,edges;for(const auto count:histogram.counts)counts.append(double(count));for(const auto edge:histogram.edges)edges.append(edge);result["counts"]=counts;result["edges"]=edges;
      const auto reads=view.property("planeReadCount").toULongLong();option(view,"Sample 1 in N")->setValue(20);QCOMPARE(view.property("sampled").toULongLong(),qulonglong(std::max<size_t>(1,size_t(std::llround(double(histogram.total)/20.)))));QCOMPARE(view.property("planeReadCount").toULongLong(),reads);
      report[item["tag"].toString()]=result;
      if(target=="/field/0"){view.resize(1050,700);view.show();QVERIFY(QTest::qWaitForWindowExposed(&view));QVERIFY(view.grab().save(evidence+QString("/histogram-grib%1.png").arg(edition)));}
    }
    QFile output(evidence+QString("/histogram-grib%1.json").arg(edition));QVERIFY(output.open(QIODevice::WriteOnly));QVERIFY(output.write(QJsonDocument(report).toJson())>0);
  }
};
QTEST_MAIN(GribAnalysisTest)
#include "grib_analysis_test.moc"
