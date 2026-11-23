// GRIB index synchronization/playback gates; sources stay read-only.
// Inputs: testdata/grib and optional HDFSTUDIO_GRIB{1,2}_SAMPLE.
// Outputs: HDFSTUDIO_GRIB_EVIDENCE/playback-grib<edition>.json and screenshots.
#include <QtTest>
#include <QScopeGuard>
#include <cmath>
#include "hdf_explorer.hpp"
#include "raster_view.hpp"
#include "grib_table_view.hpp"
#include "analysis_table_view.hpp"
#include "workspace_sync.hpp"
#include "layer_playback.hpp"
#include "compare_panel.hpp"
#include "residual_view.hpp"
#include "read_scheduler.hpp"
#include "data_read_controller.hpp"
#include "nc_reader.hpp"

class GribPlaybackTest : public QObject {
  Q_OBJECT
  QString fixture(const QString &name) const { return QString(HDFSTUDIO_TEST_SOURCE_DIR) + "/testdata/grib/" + name; }
  QTreeWidgetItem *cube_item(MainWindow &window) const {
    auto *tree = window.findChild<FileTreeWidget*>();
    if(!tree || !tree->topLevelItemCount()) return nullptr;
    auto *root = tree->topLevelItem(0);
    for(int index = 0; index < root->childCount(); ++index)
      if(root->child(index)->text(0).startsWith("Variables") && root->child(index)->childCount())
        return root->child(index)->child(0);
    return nullptr;
  }
  RasterView *open_image(MainWindow &window) const {
    auto *item = cube_item(window);
    if(!item) return nullptr;
    window.findChild<FileTreeWidget*>()->setCurrentItem(item);
    const auto before = window.findChildren<RasterView*>();
    window.findChild<QAction*>("actionImageView")->trigger();
    for(auto *view : window.findChildren<RasterView*>()) if(!before.contains(view)) return view;
    return nullptr;
  }

private slots:
  void syncLagMissingAndDisplay() {
    const auto file = fixture("mini_cube.grb2"), absent = fixture("mini_missing_slice.grb2");
    const auto target = GribData::open(file)->cubes.front().id;
    RasterView a(file, target, GribData::describe(file, target));
    RasterView b(absent, GribData::open(absent)->cubes.front().id, GribData::describe(absent, GribData::open(absent)->cubes.front().id));
    a.show(); b.show();
    QTRY_VERIFY(a.plane_snapshot().values && b.plane_snapshot().values);
    WorkspaceSyncController sync; sync.set_anchor(&a);
    SyncBinding binding; binding.view = &b; binding.lag_offset = -1;
    sync.add_or_update_follower(binding);
    QVERIFY(b.plane_snapshot().out_of_range); QVERIFY(!b.plane_snapshot().values);
    a.set_layer_index(0, 1);
    QCOMPARE(b.layer_index(0), 0);
    QTRY_VERIFY(a.plane_snapshot().values && b.plane_snapshot().values);
    QCOMPARE((*a.plane_snapshot().values)[0], 50600.);
    QCOMPARE((*b.plane_snapshot().values)[0], 50000.);
    a.findChild<QAction*>("flipHorizontalAction")->trigger();
    QCOMPARE(b.viewport_state().flip_h, a.viewport_state().flip_h);
    binding.lag_offset = 0; binding.sync_colormap = true; sync.add_or_update_follower(binding);
    QTRY_VERIFY(!b.render_pending());
    QVERIFY(!b.plane_snapshot().values); // Missing slot [1,0], never the old [0,0] plane.
    QVERIFY(!b.plane_snapshot().out_of_range);
    a.set_layer_index(0, 0);
    QTRY_VERIFY(a.plane_snapshot().values && b.plane_snapshot().values);
    QCOMPARE(b.color_state().mapping.minimum, a.color_state().mapping.minimum);
    QCOMPARE(b.color_state().mapping.maximum, a.color_state().mapping.maximum);
    sync.clear();
    a.set_layer_index(0, 1); QCOMPARE(b.layer_index(0), 0);
  }

  void rasterPlaybackWaitsAndRoutesAnchor() {
    MainWindow window; window.resize(1450, 900); window.show();
    QCOMPARE(window.read_file(fixture("mini_cube.grb2")), 0);
    auto *a = open_image(window), *b = open_image(window); QVERIFY(a); QVERIFY(b);
    QTRY_VERIFY(a->plane_snapshot().values && b->plane_snapshot().values);
    auto *sync = window.findChild<WorkspaceSyncController*>(); sync->set_anchor(a);
    SyncBinding binding; binding.view = b; sync->add_or_update_follower(binding);
    auto *mdi = window.findChild<QMdiArea*>();
    for(auto *sub : mdi->subWindowList()) if(sub->widget() == b) mdi->setActiveSubWindow(sub);
    QTRY_COMPARE(window.current_layer_view(), static_cast<LayerNavigation*>(a));
    auto *play = window.findChild<QAction*>("actionLayerPlay");
    auto *player = window.findChild<LayerPlayback*>();
    window.findChild<QSpinBox*>("playbackIntervalSpin")->setValue(10000);
    QTRY_VERIFY(play->isEnabled()); play->trigger();
    QVERIFY(player->is_playing()); QCOMPARE(player->target(), a);
    auto *scheduler = ReadScheduler::instance();
    QVERIFY(scheduler->acquire()); QVERIFY(scheduler->acquire());
    bool held = true;
    auto release = qScopeGuard([&]{ if(held) { scheduler->release(); scheduler->release(); } });
    player->advance_frame();
    QCOMPARE(a->layer_index(0), 1); QCOMPARE(b->layer_index(0), 1);
    QVERIFY(!a->plane_snapshot().values); QVERIFY(!b->plane_snapshot().values);
    for(int tick = 0; tick < 8; ++tick) player->advance_frame();
    QVERIFY(player->is_playing()); QCOMPARE(a->layer_index(0), 1);
    held = false; scheduler->release(); scheduler->release();
    QTRY_VERIFY(!a->render_pending() && !b->render_pending());
    QCOMPARE((*a->plane_snapshot().values)[0], 50600.);
    QCOMPARE((*b->plane_snapshot().values)[0], 50600.);
    player->advance_frame(); QVERIFY(!player->is_playing());
    play->trigger(); QCOMPARE(a->layer_index(0), 0); QVERIFY(player->is_playing());
    play->trigger(); QVERIFY(!player->is_playing());
    QTRY_VERIFY(!a->render_pending() && !b->render_pending());
    player->advance_frame(); QCOMPARE(a->layer_index(0), 0);
    window.close_all_files(); QVERIFY(!player->is_playing());
  }

  void gridMissingLastFrameStops() {
    MainWindow window; window.resize(1250, 850); window.show();
    QCOMPARE(window.read_file(fixture("mini_missing_slice.grb2")), 0);
    auto *tree = window.findChild<FileTreeWidget*>(); tree->setCurrentItem(cube_item(window));
    QVERIFY(QMetaObject::invokeMethod(tree, "add_grid", Qt::DirectConnection));
    auto *grid = window.findChild<GribTableView*>(); QVERIFY(grid);
    QTRY_VERIFY(grid->ready());
    auto *play = window.findChild<QAction*>("actionLayerPlay");
    auto *player = window.findChild<LayerPlayback*>();
    window.findChild<QSpinBox*>("playbackIntervalSpin")->setValue(10000);
    QTRY_VERIFY(play->isEnabled()); play->trigger(); QVERIFY(player->is_playing());
    player->advance_frame(); QCOMPARE(grid->layer_index(0), 1);
    QTRY_VERIFY(grid->findChild<QLabel*>("gribGridStatus")->text().contains("MissingSlice"));
    QVERIFY(!grid->ready());
    QCOMPARE(grid->findChild<AnalysisTableView*>()->model()->rowCount(), 0);
    player->advance_frame(); QVERIFY(!player->is_playing());
    play->trigger(); QCOMPARE(grid->layer_index(0), 0);
    QTRY_VERIFY(grid->ready()); play->trigger(); QVERIFY(!player->is_playing());
    window.close_all_files();
  }

  void mixedFormatIndexSynchronization() {
    QTemporaryDir directory;
    const auto filename = directory.filePath("mixed.nc");
    int file = -1, t = -1, level = -1, y = -1, x = -1, variable = -1;
    QCOMPARE(nc_create(nc_path_bytes(filename).constData(), NC_CLOBBER, &file), NC_NOERR);
    nc_def_dim(file, "time", 2, &t); nc_def_dim(file, "level", 2, &level);
    nc_def_dim(file, "y", 3, &y); nc_def_dim(file, "x", 4, &x);
    int dimensions[] = {t, level, y, x}; nc_def_var(file, "field", NC_DOUBLE, 4, dimensions, &variable);
    nc_enddef(file);
    std::vector<double> values(48);
    for(int time = 0; time < 2; ++time) for(int lev = 0; lev < 2; ++lev) for(int position = 0; position < 12; ++position)
      values[size_t((time * 2 + lev) * 12 + position)] = 50003. + 600. * time + 35000. * lev + position;
    QCOMPARE(nc_put_var_double(file, variable, values.data()), NC_NOERR); QCOMPARE(nc_close(file), NC_NOERR);
    const auto grib = fixture("mini_cube.grb2"), target = GribData::open(grib)->cubes.front().id;
    RasterView a(grib, target, GribData::describe(grib, target));
    RasterView b(filename.toLocal8Bit().toStdString(), "/field", {2, 2, 3, 4}); a.show(); b.show();
    QTRY_VERIFY(a.plane_snapshot().values && b.plane_snapshot().values);
    WorkspaceSyncController sync; ComparePanel panel(&sync); panel.set_views({&a, &b}); panel.show();
    a.set_layer_index(0, 1); a.set_layer_index(1, 1);
    QTRY_VERIFY(a.plane_snapshot().values && b.plane_snapshot().values);
    QCOMPARE(b.layer_index(0), 1); QCOMPARE(b.layer_index(1), 1);
    panel.refresh_now(); panel.findChild<QPushButton*>("compareResidualButton")->click();
    auto *residual = panel.findChild<ResidualView*>(); QVERIFY(residual && residual->residual().valid);
    for(double value : residual->residual().values) QCOMPARE(value, 3.);
    QVERIFY(panel.comparison_csv().contains("by index"));
    QVERIFY(panel.comparison_csv().contains("Grid correspondence unverified"));
  }

  void stopsOnBindingCloseAndDirectDestruction() {
    auto window = std::make_unique<MainWindow>(); window->show();
    QCOMPARE(window->read_file(fixture("mini_cube.grb2")), 0);
    auto *a = open_image(*window), *b = open_image(*window); QVERIFY(a); QVERIFY(b);
    QTRY_VERIFY(a->plane_snapshot().values && b->plane_snapshot().values);
    auto *sync = window->findChild<WorkspaceSyncController*>(); sync->set_anchor(a);
    SyncBinding binding; binding.view = b; sync->add_or_update_follower(binding);
    auto *player = window->findChild<LayerPlayback*>();
    auto *play = window->findChild<QAction*>("actionLayerPlay");
    window->findChild<QSpinBox*>("playbackIntervalSpin")->setValue(10000);
    QTRY_VERIFY(play->isEnabled()); play->trigger(); QVERIFY(player->is_playing());
    binding.lag_offset = 1; sync->add_or_update_follower(binding); QVERIFY(!player->is_playing());
    play->trigger(); QVERIFY(player->is_playing());
    QPointer<LayerPlayback> guarded_player = player;
    window.reset(); QVERIFY(!guarded_player);
    QTRY_VERIFY(!DataReadController::any_busy());
    auto *source = new RasterView(fixture("mini_cube.grb2"), GribData::open(fixture("mini_cube.grb2"))->cubes.front().id,
                                 GribData::describe(fixture("mini_cube.grb2"), GribData::open(fixture("mini_cube.grb2"))->cubes.front().id));
    LayerPlayback isolated; isolated.set_interval_ms(10000); QVERIFY(isolated.start(source, 0));
    delete source; QVERIFY(!isolated.is_playing()); QVERIFY(!isolated.target()); isolated.advance_frame();
    QTRY_VERIFY(!DataReadController::any_busy());
  }

  void externalNavigation_data() {
    QTest::addColumn<int>("edition"); QTest::newRow("grib1") << 1; QTest::newRow("grib2") << 2;
  }
  void externalNavigation() {
    QFETCH(int, edition);
    const auto source = qEnvironmentVariable(QString("HDFSTUDIO_GRIB%1_SAMPLE").arg(edition).toLatin1());
    if(source.isEmpty()) QSKIP("Optional read-only external GRIB sample");
    const auto catalog = GribData::open(source);
    const grib::Cube *selected = nullptr;
    int selected_axis = -1;
    for(const auto &cube : catalog->cubes) {
      for(size_t axis = 0; axis < cube.axes.size(); ++axis) if(cube.axes[axis].labels.size() > 1) {
        selected = &cube; selected_axis = int(axis); break;
      }
      if(selected) break;
    }
    const auto target = selected ? selected->id : QString("/field/0");
    MainWindow window; window.resize(1350, 850); window.show();
    auto *view = new RasterView(source, target, GribData::describe(source, target));
    auto *sub = window.findChild<QMdiArea*>()->addSubWindow(view); sub->show();
    QTRY_VERIFY_WITH_TIMEOUT(view->plane_snapshot().values && !view->render_pending(), 15000);
    auto *player = window.findChild<LayerPlayback*>(); auto *play = window.findChild<QAction*>("actionLayerPlay");
    window.findChild<QSpinBox*>("playbackIntervalSpin")->setValue(10000);
    QJsonArray frames;
    if(selected) {
      auto *axis_combo = window.findChild<QComboBox*>("layerAxisCombo");
      axis_combo->setCurrentIndex(selected_axis); emit axis_combo->activated(selected_axis);
      QTRY_VERIFY(play->isEnabled()); play->trigger(); QVERIFY(player->is_playing());
      for(int tick = 0; tick < 3 && player->is_playing(); ++tick) {
        player->advance_frame(); QTRY_VERIFY_WITH_TIMEOUT(!view->render_pending(), 15000);
        const auto snapshot = view->plane_snapshot();
        std::vector<int> indices; for(int axis = 0; axis < view->layer_axis_count(); ++axis) indices.push_back(view->layer_index(axis));
        int field = -1; try { field = grib::resolve(*catalog, target, indices); } catch(const grib::Error &error) { QCOMPARE(error.code, QString("MissingSlice")); }
        QJsonArray json_indices; for(int index : indices) json_indices.append(index);
        QJsonObject record{{"indices0", json_indices}, {"field", field}, {"available", bool(snapshot.values)}};
        if(field >= 0) {
          QVERIFY(snapshot.values); const auto reference = GribData::read(*catalog, "/field/" + QString::number(field), {});
          QVERIFY(reference.error.isEmpty()); QCOMPARE(snapshot.values->size(), reference.values.size());
          for(size_t position = 0; position < reference.values.size(); ++position) {
            if(std::isfinite(reference.values[position])) QCOMPARE((*snapshot.values)[position], reference.values[position]);
            else QVERIFY(std::isnan((*snapshot.values)[position]));
          }
          record["positions"] = QString::number(reference.values.size());
        } else QVERIFY(!snapshot.values);
        frames.append(record);
      }
      if(player->is_playing()) play->trigger(); QVERIFY(!player->is_playing());
    } else { QTRY_VERIFY(!play->isEnabled()); QVERIFY(!player->start(view, 0)); }
    const auto evidence = qEnvironmentVariable("HDFSTUDIO_GRIB_EVIDENCE"); QVERIFY(!evidence.isEmpty());
    QJsonObject report{{"edition", edition}, {"target", target}, {"title", selected ? selected->title : catalog->cubes.front().title},
                       {"playable", selected != nullptr}, {"axis", selected_axis}, {"frames", frames}};
    QFile output(evidence + QString("/playback-grib%1.json").arg(edition)); QVERIFY(output.open(QIODevice::WriteOnly));
    output.write(QJsonDocument(report).toJson());
    QVERIFY(window.grab().save(evidence + QString("/playback-grib%1.png").arg(edition)));
  }
};
QTEST_MAIN(GribPlaybackTest)
#include "grib_playback_test.moc"
