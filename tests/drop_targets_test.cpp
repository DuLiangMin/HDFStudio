// 26NOV19: Qt Test coverage for local-file drag/drop dispatch.
// Run: ctest --test-dir build/hdf_explorer -C Debug -R hdf_explorer_drop_tests --output-on-failure
// Input: read-only HDFSTUDIO_ZOOM_SAMPLE and HDFSTUDIO_FORECAST_ANCHOR/TARGET.
#include <QApplication>
#include <QBuffer>
#include <QProcess>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTemporaryFile>
#include <QtTest>
#include <limits>
#include <numeric>

#include "hdf_explorer.hpp"
#include "raster_view.hpp"
#include "profile_view.hpp"
#include "vector_view.hpp"
#include "curve_profile.hpp"
#include "properties_inspector.hpp"
#include "text_export.hpp"
#include "nc_reader.hpp"
#include "plane_reader.hpp"
#include "histogram_view.hpp"
#include "image_metrics.hpp"
#include "texture_entropy.hpp"
#include "texture_entropy_view.hpp"
#include "raster_budget.hpp"
#include "workspace_sync.hpp"
#include "compare_panel.hpp"
#include "layer_playback.hpp"
#include "mesh_view.hpp"
#include "color_map.hpp"
#include "color_map_picker.hpp"
#include "analysis_table_view.hpp"
#include "hdf_filters_fixture.hpp"
#include <QAbstractItemModelTester>
#include <QClipboard>
#include <QStandardItemModel>
#include <QScopeGuard>
#include <QPlainTextEdit>

class DropTargetsTest : public QObject {
  Q_OBJECT

private slots:
  void bitshuffleImageGridAndDecodedValues();
  void analysisGridFitsContentsAndKeepsManualWidths();
  void analysisGridMouseSelectionAndClipboardMatchCells();
  void analysisGridSparseSelectionAndCopyLimits();
  void analysisGridSizingIsBoundedOnLargeModels();
  void analysisGridModesLayersAttributesAndDatesReuseControls();
  void textureEntropyBlocksAndInterpolation();
  void textureEntropyViewIsIndependent2DImage();
  void textureEntropySnapshotFollowsMaskAndSourceLifetime();
  void textureEntropyBudgetAndInvalidRecovery();
  void imageMetricsEntropyAccuracy();
  void imageMetricsEntropyAndPairAccuracy();
  void histogramOrRasterShowsGrayEntropy();
  void workspaceSyncAnchorLagAndOutOfRange();
  void imageMetricsResidualAccuracy();
  void comparePanelComputesLivePairMetricsAndExportsCsv();
  void forecastPairResidualMatchesEveryDecodedPixel();
  void linkedLayerToolbarNavigatesAnchorFromEitherImage();
  void layerPlaybackSingleAndLinkedPlanes();
  void layerPlaybackStopsOnContextChanges();
  void layerPlaybackFrameCountBoundaries();
  void cleanup();
  void acceptsExistingLocalFile();
  void rejectsMissingLocalFile();
  void opensNetcdfFromEachTarget();
  void continuousPalettesMapEndpointsAndReverse();
  void colorMapResourcesMatchGoldenValuesAndLicenses();
  void colorMapRgbAndMappingHandleBoundaries();
  void colorMapUiUsesCachedDecodedPlane();
  void colorMapLatestAsyncRenderWins();
  void colorMapMissingCategoriesColorbarAndBudget();
  void invalidFractionsAreClamped();
  void automaticAndFixedRangesAreValidated();
  void discreteCategoriesAreStableAndHandleUnknowns();
  void propertyAttributePreviewMarksTruncation();
  void propertiesInspectorReadsDatasetLayoutAndAttributes();
  void textExportStreamsValuesInRowsAndRejectsUnsupportedTypes();
  void vectorPlanePairRequiresMatchingCompleteShapes();
  void geographicCoordinatesRequireExplicitCfMetadataAndMatchingDimensions();
  void ugridTriangleReaderHonorsStartIndexAndRejectsNonTriangles();
  void ugridTriangleReaderHonorsStartIndexAndRejectsNonTriangles_data();
  void ugridMeshViewRendersVerifiedTriangleFixture();
  void formatProbeDistinguishesNetcdfModelsAndHdf5Containers();
  void cf18DiagnosticsReportScopedReferencesAndMissingDeclaration();
  void cf18DiagnosticReportDialogStatesItsCoverageBoundary();
  void bottomFileInfoPanelShowsCurrentFileMetadata();
  void informationTabsExposeNetcdf4StructureAndCfVerdict();
  void startupSplashIsPlainVisibleWidgetNotQSplashScreen();
  void timeGridShowsDecodedDatesBesideRawValues();
  void closeFileRemovesItsViewsAndReleasesTheFile();
  void classicNetcdfOpensWithTreeGridImagePropertiesAndExport();
  void externalClassicSampleMatchesPython();
  void classicNetcdfOpensFromChinesePath();
  void layerStepTargetMatchesOldHdfExplorerButtons();
  void mainToolbarStepsLayersOfTheCurrentView();
  void propertiesDialogCopiesReportAndWindowMenuListsViews();
  void maskRangeUsesDecodedValuesWithOpenEnds();
  void decodedAndStoredGridsShowScaleFactorAndOffset();
  void bothGridsShowDecodedAndRawSideBySide();
  void histogramSamplesOneInTwentyAndBinsDecodedValues();
  void rasterFlipAndResetRestoreOriginalView();
  void rasterZoomButtonsWheelAndCyclesPreserveData();
  void curveProfileSamplesPolylineAndSplineOnDecodedPlane();
  void externalPackedSampleMatchesPython();
  void regressionCliRejectsInvalidParameters_data();
  void regressionCliRejectsInvalidParameters();
  void regressionBatchPreservesExistingOutputs();
  void regressionGuiExportsCurrentFullResolutionRaster();
  void regressionMappingDialogAndImportRecovery();
  void regressionAsyncLayersMasksAndRecovery();
  void regressionFiveDimensionalSlicesAndTypes();
  void regressionBudgetIsReleasedAfterClose();
  void regressionCorruptFileDoesNotBreakOpenSession();
  void regressionVectorViewChangesLayerAndOmitsMissingPairs();
  void regressionProfileCsvMatchesAnalyticField();
  void fileDropOpensFromEveryAreaOfTheMainWindow();
};

void DropTargetsTest::acceptsExistingLocalFile() {
  MainWindow window;
  QTemporaryFile file;
  QVERIFY(file.open());
  QMimeData mime;
  mime.setUrls({QUrl::fromLocalFile(file.fileName())});
  QVERIFY(window.accepts_file_drop(&mime));
}

void DropTargetsTest::rejectsMissingLocalFile() {
  MainWindow window;
  QMimeData mime;
  mime.setUrls({QUrl::fromLocalFile("Z:/missing/hdf-explorer-drop-test.nc")});
  QVERIFY(!window.accepts_file_drop(&mime));
}

void DropTargetsTest::opensNetcdfFromEachTarget() {
  MainWindow window;
  const QString sample = qEnvironmentVariable("HDFSTUDIO_ZOOM_SAMPLE");
  QVERIFY2(QFileInfo(sample).isFile(), qPrintable(sample));
  auto tree = window.findChild<FileTreeWidget *>();
  auto mdi = window.findChild<QMdiArea *>();
  QVERIFY(tree);
  QVERIFY(mdi);

  const QList<QWidget *> targets = {&window, tree->viewport(), mdi->viewport()};
  for(QWidget *target : targets) {
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(sample)});
    QDragEnterEvent enter(QPoint(4, 4), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &enter);
    QVERIFY(enter.isAccepted());

    QDropEvent drop(QPointF(4, 4), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &drop);
    QVERIFY(drop.isAccepted());
    QTRY_COMPARE(tree->findItems("PRECT", Qt::MatchExactly | Qt::MatchRecursive).size(), 1);

    auto roots = tree->findItems(QFileInfo(sample).fileName(),
                                 Qt::MatchContains | Qt::MatchRecursive);
    QCOMPARE(roots.size(), 1);
    delete roots.first();
  }
}

void DropTargetsTest::continuousPalettesMapEndpointsAndReverse() {
  using RasterColorMap::Palette;
  const QList<Palette> palettes = {Palette::Viridis, Palette::Plasma,
    Palette::Inferno, Palette::Turbo, Palette::Grayscale};
  for(Palette palette : palettes) {
    const QColor low = RasterColorMap::color(palette, 0.0, false);
    const QColor high = RasterColorMap::color(palette, 1.0, false);
    QVERIFY(low != high);
    QCOMPARE(RasterColorMap::color(palette, 0.0, true), high);
    QCOMPARE(RasterColorMap::color(palette, 1.0, true), low);
    QCOMPARE(RasterColorMap::color(palette, 0.37, true),
             RasterColorMap::color(palette, 0.63, false));
  }
  QVERIFY(RasterColorMap::color(Palette::Viridis, 0.5, false) !=
          RasterColorMap::color(Palette::Plasma, 0.5, false));
  QCOMPARE(RasterColorMap::colorValue(Palette::Inferno,
             std::numeric_limits<double>::quiet_NaN(), 0.0, 1.0, false),
           QColor(110,110,110));
}

void DropTargetsTest::colorMapResourcesMatchGoldenValuesAndLicenses() {
  using namespace HdfColorMap;
  QStandardPaths::setTestModeEnabled(true);
  QString error;const auto palettes=load_registry(error);QVERIFY2(error.isEmpty(),qPrintable(error));
  QVERIFY(palettes.size()>=18);
  QFile golden(":/colormaps/golden.json");QVERIFY(golden.open(QIODevice::ReadOnly));
  const auto references=QJsonDocument::fromJson(golden.readAll()).array();QCOMPARE(references.size(),13);
  for(const auto &reference:references) {
    const auto object=reference.toObject();
    auto found=std::find_if(palettes.begin(),palettes.end(),[&](const Palette &palette){return palette.id==object["id"].toString();});
    QVERIFY(found!=palettes.end());QVERIFY(!found->source.isEmpty());QVERIFY(!found->authors.isEmpty());
    QCOMPARE(found->source_hash.size(),64);QVERIFY(!found->license.isEmpty());
    for(const auto &value:object["samples"].toArray()) {
      const auto point=value.toObject();const auto rgba=point["rgba8"].toArray();
      const uint32_t expected=qRgba(rgba[0].toInt(),rgba[1].toInt(),rgba[2].toInt(),rgba[3].toInt());
      QCOMPARE(sample(*found,point["fraction"].toDouble()),expected);
      QCOMPARE(sample(*found,1-point["fraction"].toDouble(),true),expected);
    }
  }
  const std::vector<size_t> counts={256,256,256,512,256,512,256,256,12,13,20,18,254};
  for(size_t index=0;index<counts.size();++index) QCOMPARE(palettes[index+5].colors.size(),counts[index]);
  for(int index=0;index<5;++index) for(double position:{0.,.13,.37,.5,1.})
    QCOMPARE(sample(palettes[size_t(index)],position),RasterColorMap::color(RasterColorMap::Palette(index),position,false).rgba());
  QFile manifest(":/colormaps/manifest.json");QVERIFY(manifest.open(QIODevice::ReadOnly));
  const auto object=QJsonDocument::fromJson(manifest.readAll()).object();
  for(const auto &entry:object["licenses"].toArray()) {
    const auto item=entry.toObject();QFile license(":/colormaps/licenses/"+item["file"].toString());QVERIFY(license.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(license.readAll(),QCryptographicHash::Sha256).toHex()),item["sha256"].toString());
  }
  QProcess cli;cli.start(QDir(QCoreApplication::applicationDirPath()).filePath("hdf_cli.exe"),{"cmap","bids:viridis","0"});
  QVERIFY(cli.waitForFinished());QCOMPARE(cli.exitCode(),0);QCOMPARE(cli.readAllStandardOutput().trimmed(),QByteArray("68,1,84,255"));
}

void DropTargetsTest::bitshuffleImageGridAndDecodedValues() {
  QTemporaryDir dir; QVERIFY(dir.isValid());
  const QString file = dir.filePath("compressed.h5");
  QFile output(file); QVERIFY(output.open(QIODevice::WriteOnly));
  const auto bytes = QByteArray::fromBase64(HdfFilterFixture::file_zstd);
  QCOMPARE(output.write(bytes), bytes.size()); output.close();
  MainWindow window; window.resize(1200,800); window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window)); QCOMPARE(window.read_file(file),0);
  auto *tree=window.findChild<FileTreeWidget*>();auto *mdi=window.findChild<QMdiArea*>();
  const auto items=tree->findItems("signed",Qt::MatchExactly|Qt::MatchRecursive);QCOMPARE(items.size(),1);
  tree->setCurrentItem(items.first());
  auto *image_action=window.findChild<QAction*>("actionImageView");QVERIFY(image_action);image_action->trigger();
  auto *image=window.findChild<RasterView*>();QVERIFY(image);
  QTRY_VERIFY(image->plane_snapshot().values);QTRY_VERIFY(!image->render_pending());
  QCOMPARE(image->image_for_export().size(),QSize(67,35));
  const auto snapshot=image->plane_snapshot();
  for(size_t index=0;index<snapshot.values->size();++index){
    const int raw=int(index%200)-100;
    if(raw==-100||raw==-99)QVERIFY(std::isnan((*snapshot.values)[index]));
    else QCOMPARE((*snapshot.values)[index],raw*.0001+2.0);
  }
  for(const char *slot:{"add_grid","add_stored_grid","add_both_grids"}){
    tree->setCurrentItem(items.first());QVERIFY(QMetaObject::invokeMethod(tree,slot));
    auto *table=mdi->subWindowList().last()->findChild<AnalysisTableView*>();QVERIFY(table);
    QCOMPARE(table->model()->rowCount(),35);
    const bool both=QByteArray(slot)=="add_both_grids",raw=QByteArray(slot)=="add_stored_grid";
    QCOMPARE(table->model()->data(table->model()->index(0,both?4:2)).toString(),raw?QString("-98"):QString("1.9902"));
  }
  window.close_all_files();
  const QString sample=qEnvironmentVariable("HDFSTUDIO_BITSHUFFLE_SAMPLE");
  if(!sample.isEmpty()){
    const QString reference=qEnvironmentVariable("HDFSTUDIO_BITSHUFFLE_REFERENCE");
    QVERIFY(QFileInfo(sample).isFile());QVERIFY(QFileInfo(reference).isFile());
    RasterView actual(sample.toLocal8Bit().toStdString(),"/precipitation",{1,1201,1401});
    RasterView expected(reference.toLocal8Bit().toStdString(),"/precipitation",{1,1201,1401});
    actual.resize(1100,780);actual.show();
    QTRY_VERIFY(actual.plane_snapshot().values);QTRY_VERIFY(expected.plane_snapshot().values);
    QCOMPARE(*actual.plane_snapshot().values,*expected.plane_snapshot().values);
    QCOMPARE(actual.image_for_export(),expected.image_for_export());
    for(auto *view : {&actual,&expected}) {
      auto *start=view->findChild<QLineEdit*>("maskStart");auto *end=view->findChild<QLineEdit*>("maskEnd");
      start->setText("9999");end->setText("9999");QMetaObject::invokeMethod(end,"editingFinished");
    }
    QCOMPARE(actual.image_for_export(),expected.image_for_export());
    const auto evidence=qEnvironmentVariable("HDFSTUDIO_GRID_EVIDENCE");
    if(!evidence.isEmpty()){QVERIFY(actual.image_for_export().save(QDir(evidence).filePath("bitshuffle-image.png")));QVERIFY(actual.grab().save(QDir(evidence).filePath("bitshuffle-window.png")));}
  }
}

void DropTargetsTest::analysisGridFitsContentsAndKeepsManualWidths() {
  QStandardItemModel model(10, 5);
  for(int row = 0; row < 10; ++row)
    for(int column = 0; column < 5; ++column)
      model.setData(model.index(row, column), QString::number(row + column));
  AnalysisTableView table;
  table.setModel(&model);
  table.resize(800, 360);
  table.show();
  QVERIFY(QTest::qWaitForWindowExposed(&table));
  QTRY_VERIFY(table.columnWidth(0) < 80);
  QVERIFY(table.autoFitAction()->isChecked());
  QVERIFY(!table.horizontalHeader()->stretchLastSection());
  QCOMPARE(table.editTriggers(), QAbstractItemView::EditTriggers(QAbstractItemView::NoEditTriggers));
  const QString full = "-1.23456789012345e+100";
  model.setData(model.index(4, 1), full);
  QTRY_VERIFY(table.columnWidth(1) >= table.fontMetrics().horizontalAdvance(full) + 4);
  QVERIFY(table.columnWidth(1) > table.columnWidth(0));
  model.setHeaderData(2, Qt::Horizontal, "yyyymmdd hh:mm:ss");
  model.setData(model.index(0, 2), "19990101 06:00:00");
  QTRY_VERIFY(table.columnWidth(2) >= table.fontMetrics().horizontalAdvance("19990101 06:00:00") + 4);
  QFont selected_header_font = table.horizontalHeader()->font(); selected_header_font.setBold(true);
  QTRY_VERIFY(table.columnWidth(2) >= AnalysisTableView::estimateTextWidth("yyyymmdd hh:mm:ss", selected_header_font, table.horizontalHeader()) + 4);
  table.autoFitAction()->setChecked(false);
  table.setColumnWidth(1, 65);
  model.setData(model.index(4, 1), "-9.87654321098765e-200");
  QTest::qWait(80);
  QCOMPARE(table.columnWidth(1), 65);
  table.autoFitAction()->setChecked(true);
  QTRY_VERIFY(table.columnWidth(1) > 65);
  // A different model must not retain old model signal connections or stale selection state.
  QStandardItemModel replacement(2, 2);
  replacement.setData(replacement.index(0, 0), "8");
  table.setModel(&replacement);
  QTRY_VERIFY(table.columnWidth(0) < 80);
  QVERIFY(!table.copyAction()->isEnabled());
  QCOMPARE(table.selectionSummary(), QString("Selected: 0 cells"));

  // 同一显示值随字号自动增宽；不按数值量级或固定字符宽度猜测。
  QStandardItemModel font_model(3, 4);
  const QStringList values = {"1", "-12345.6789", "-1.23456789e-100", "19990101 06:00:00"};
  for(int column = 0; column < values.size(); ++column) {
    font_model.setHeaderData(column, Qt::Horizontal, column == 2 ? "3\ndecoded" : QString::number(column + 1));
    for(int row = 0; row < 3; ++row) font_model.setData(font_model.index(row, column), values[column]);
  }
  table.setModel(&font_model);
  QJsonArray measurements;
  int previous_width = 0;
  for(int points : {9, 12, 18}) {
    QFont value_font = table.font(); value_font.setPointSize(points);
    table.setFont(value_font);
    const int expected_text = AnalysisTableView::estimateTextWidth(values[1], value_font, table.viewport());
    QTRY_VERIFY(table.columnWidth(1) >= expected_text + 4);
    QTRY_VERIFY(table.columnWidth(1) > previous_width);
    QTRY_VERIFY(table.columnWidth(0) < table.columnWidth(1));
    // 给其他列与表头的重绘/调整事件一个完整事件循环。
    QTest::qWait(50);
    QVERIFY(table.rowHeight(0) >= table.fontMetrics().height() + 6);
    QJsonArray columns;
    for(int column = 0; column < values.size(); ++column) {
      const int measured = AnalysisTableView::estimateTextWidth(values[column], value_font, table.viewport());
      QVERIFY2(table.columnWidth(column) >= measured + 4, qPrintable(values[column]));
      QVERIFY(table.columnWidth(column) <= qMax(24, measured + 18));
      columns.append(QJsonObject{{"text", values[column]}, {"text_pixels", measured}, {"column_pixels", table.columnWidth(column)}});
    }
    previous_width = table.columnWidth(1);
    measurements.append(QJsonObject{{"font", value_font.family()}, {"point_size", points}, {"columns", columns}});
    const QString evidence = qEnvironmentVariable("HDFSTUDIO_GRID_EVIDENCE");
    if(!evidence.isEmpty()) QVERIFY(table.grab().save(QDir(evidence).filePath(QString("font-width-%1pt.png").arg(points))));
  }
  const QFont value_font = table.font();
  QCOMPARE(AnalysisTableView::estimateTextWidth("3\ndecoded", value_font, table.viewport()),
           AnalysisTableView::estimateTextWidth("decoded", value_font, table.viewport()));
  QFont bigger = value_font; bigger.setPointSize(22); bigger.setBold(true);
  font_model.setData(font_model.index(0, 1), bigger, Qt::FontRole);
  QTRY_VERIFY(table.columnWidth(1) >= AnalysisTableView::estimateTextWidth(values[1], bigger, table.viewport()) + 4);
  font_model.setData(font_model.index(0, 1), QVariant(), Qt::FontRole);
  font_model.setData(font_model.index(0, 1), "2");
  font_model.setData(font_model.index(1, 1), "2");
  font_model.setData(font_model.index(2, 1), "2");
  QTRY_VERIFY(table.columnWidth(1) < previous_width);
  font_model.setHeaderData(0, Qt::Horizontal, "Scientific header");
  QTRY_VERIFY(table.columnWidth(0) >= AnalysisTableView::estimateTextWidth("Scientific header", table.horizontalHeader()->font(), table.horizontalHeader()) + 4);
  QFont header_font = table.horizontalHeader()->font(); header_font.setPointSize(20); header_font.setBold(true);
  font_model.setHeaderData(0, Qt::Horizontal, header_font, Qt::FontRole);
  QTRY_VERIFY(table.columnWidth(0) >= AnalysisTableView::estimateTextWidth("Scientific header", header_font, table.horizontalHeader()) + 4);
  const QString evidence = qEnvironmentVariable("HDFSTUDIO_GRID_EVIDENCE");
  if(!evidence.isEmpty()) {
    QFile file(QDir(evidence).filePath("font-width-estimates.json")); QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(measurements).toJson());
  }
}

void DropTargetsTest::analysisGridMouseSelectionAndClipboardMatchCells() {
  QStandardItemModel model(8, 6);
  for(int row = 0; row < model.rowCount(); ++row)
    for(int column = 0; column < model.columnCount(); ++column)
      model.setData(model.index(row, column), QString::number(row * 10 + column));
  AnalysisTableView table;
  table.setModel(&model);
  table.resize(700, 360);
  table.show();
  QVERIFY(QTest::qWaitForWindowExposed(&table));
  QTRY_VERIFY(table.columnWidth(0) < 80);
  const QPoint first = table.visualRect(model.index(1, 1)).center();
  const QPoint last = table.visualRect(model.index(3, 3)).center();
  QTest::mouseClick(table.viewport(), Qt::LeftButton, Qt::NoModifier, first);
  QCOMPARE(table.selectionModel()->selectedIndexes().size(), 1);
  QCOMPARE(table.selectionText(), QString("11"));
  QTest::mousePress(table.viewport(), Qt::LeftButton, Qt::NoModifier, first);
  QMouseEvent drag(QEvent::MouseMove, last, table.viewport()->mapToGlobal(last), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(table.viewport(), &drag);
  QTest::mouseRelease(table.viewport(), Qt::LeftButton, Qt::NoModifier, last);
  QCOMPARE(table.selectionModel()->selectedIndexes().size(), 9);
  QCOMPARE(table.selectionText(), QString("11\t12\t13\n21\t22\t23\n31\t32\t33"));
  QCOMPARE(table.selectionSummary(), QString("Selected: 9 cells"));
  table.copyAction()->trigger();
  QCOMPARE(QApplication::clipboard()->text(), table.selectionText());
  QTest::mouseClick(table.viewport(), Qt::LeftButton, Qt::ControlModifier,
                    table.visualRect(model.index(5, 5)).center());
  QCOMPARE(table.selectionModel()->selectedIndexes().size(), 10);
  QVERIFY(table.selectionModel()->isSelected(model.index(5, 5)));
  QTest::mouseClick(table.viewport(), Qt::LeftButton, Qt::NoModifier, first);
  QTest::mouseClick(table.viewport(), Qt::LeftButton, Qt::ShiftModifier, last);
  QCOMPARE(table.selectionModel()->selectedIndexes().size(), 9);
  QApplication::clipboard()->clear();
  table.setFocus();
  QTest::keyClick(&table, Qt::Key_C, Qt::ControlModifier);
  QTRY_COMPARE(QApplication::clipboard()->text(), table.selectionText());
  const int x = table.horizontalHeader()->sectionViewportPosition(2) + table.columnWidth(2) / 2;
  QTest::mouseClick(table.horizontalHeader()->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(x, 10));
  QCOMPARE(table.selectionModel()->selectedIndexes().size(), 8);
  const int y = table.verticalHeader()->sectionViewportPosition(4) + table.rowHeight(4) / 2;
  QTest::mouseClick(table.verticalHeader()->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(10, y));
  QCOMPARE(table.selectionModel()->selectedIndexes().size(), 6);
  QCOMPARE(table.selectionText(), QString("40\t41\t42\t43\t44\t45"));
  QCOMPARE(model.data(model.index(1, 1)).toString(), QString("11"));
}

void DropTargetsTest::analysisGridSparseSelectionAndCopyLimits() {
  QStandardItemModel model(3, 3);
  model.setData(model.index(0, 0), "a\tb");
  model.setData(model.index(1, 1), "not selected");
  model.setData(model.index(2, 2), "a\"b\nc");
  AnalysisTableView table;
  table.setModel(&model);
  table.selectionModel()->select(model.index(0, 0), QItemSelectionModel::Select);
  table.selectionModel()->select(model.index(2, 2), QItemSelectionModel::Select);
  QCOMPARE(table.selectionText(), QString("\"a\tb\"\t\t\n\t\t\n\t\t\"a\"\"b\nc\""));
  QVERIFY(!table.selectionText().contains("not selected"));
  model.setData(model.index(0, 0), QString(16 * 1024 * 1024 + 1, 'x'));
  table.selectionModel()->select(model.index(0, 0), QItemSelectionModel::ClearAndSelect);
  QString error;
  QVERIFY(table.selectionText(&error).isEmpty());
  QVERIFY(error.contains("32 MiB"));
  table.clearSelection();
  QVERIFY(table.selectionText(&error).isEmpty());
  QVERIFY(error.isEmpty());
  QVERIFY(!table.copyAction()->isEnabled());
}

void DropTargetsTest::analysisGridSizingIsBoundedOnLargeModels() {
  auto checkpoint = [](const char *phase) {
    const QString folder = qEnvironmentVariable("HDFSTUDIO_GRID_EVIDENCE");
    if(folder.isEmpty()) return;
    QFile file(QDir(folder).filePath("large-grid-phases.txt"));
    if(file.open(QIODevice::WriteOnly | QIODevice::Append)) {
      file.write(phase); file.write("\n"); file.flush();
    }
  };
  checkpoint("start");
  class LargeGridModel : public QAbstractTableModel {
  public:
    mutable int reads = 0;
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : 20000; }
    int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : 20000; }
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override {
      if(role != Qt::DisplayRole || !index.isValid()) return {};
      ++reads;
      return index.row() == 12345 ? QString("-123456789.123456") : QString("1");
    }
  } model;
  AnalysisTableView table;
  table.setModel(&model);
  checkpoint("model attached");
  table.resize(700, 350);
  checkpoint("before show");
  table.show();
  checkpoint("show returned");
  QVERIFY(QTest::qWaitForWindowExposed(&table));
  checkpoint("window exposed");
  QTRY_VERIFY(table.columnWidth(0) < 80);
  QVERIFY2(model.reads < 30000, "Auto-fit scanned too much of the large model");
  checkpoint("initial sizing bounded");
  const int short_width = table.columnWidth(0);
  table.scrollTo(model.index(12345, 0), QAbstractItemView::PositionAtTop);
  QTRY_VERIFY(table.columnWidth(0) > short_width);
  table.scrollTo(model.index(12345, 1500), QAbstractItemView::PositionAtTop);
  QTRY_VERIFY(table.columnWidth(1500) >= table.fontMetrics().horizontalAdvance("-123456789.123456") + 4);
  checkpoint("scroll sizing passed");
  QElapsedTimer select_clock;
  select_clock.start();
  table.selectAll();
  checkpoint("select all returned");
  QVERIFY2(select_clock.elapsed() < 2000, "Large range selection expanded into individual cells");
  QString error;
  QVERIFY(table.selectionText(&error).isEmpty());
  QVERIFY(error.contains("1,000,000"));
  QCOMPARE(table.selectionSummary(), QString("Selected: 400000000 cells"));
  checkpoint("copy rejected and count checked");
}

void DropTargetsTest::colorMapRgbAndMappingHandleBoundaries() {
  using namespace HdfColorMap;
  Palette palette;
  QVERIFY(parse_rgb("# sample\nncolors=3\n0 0 255\n255 255 255 # middle\n255 0 0\n",RgbScale::Byte,palette).isEmpty());
  QCOMPARE(palette.colors.size(),size_t(3));QCOMPARE(sample(palette,0),uint32_t(qRgb(0,0,255)));
  const auto before=palette.id;
  QVERIFY(!parse_rgb("ncolors=4\n0 0 0\n255 255 255",RgbScale::Byte,palette).isEmpty());QCOMPARE(palette.id,before);
  Palette unit;QVERIFY(parse_rgb("ncolor=2\n0 0.5 1\n1 0 0",RgbScale::Unit,unit).isEmpty());
  QVERIFY(!parse_rgb("0 0 0\n1 1 1",RgbScale::Auto,unit).isEmpty());
  QVERIFY(!parse_rgb("0 0 0\nnan 1 1",RgbScale::Unit,unit).isEmpty());
  QVERIFY(!parse_rgb("0 0 0\n256 1 1",RgbScale::Byte,unit).isEmpty());
  QVERIFY(!parse_rgb("0 0 0\n1 1 1\nncolor=3",RgbScale::Byte,unit).isEmpty());
  Mapping mapping;mapping.minimum=-1;mapping.maximum=3;mapping.mode=Mode::Stepped;mapping.boundaries={-1,0,1,3};
  QVERIFY(validate(palette,mapping).isEmpty());
  QCOMPARE(map_value(palette,mapping,0),uint32_t(qRgb(255,255,255)));
  QCOMPARE(map_value(palette,mapping,1),uint32_t(qRgb(255,0,0)));
  QCOMPARE(map_value(palette,mapping,3),uint32_t(qRgb(255,0,0)));
  mapping.reversed=true;QCOMPARE(map_value(palette,mapping,3),uint32_t(qRgb(0,0,255)));
  mapping.custom_under=true;mapping.under=qRgb(1,2,3);QCOMPARE(map_value(palette,mapping,-2),mapping.under);
  mapping.custom_over=true;mapping.over=qRgb(4,5,6);QCOMPARE(map_value(palette,mapping,4),mapping.over);
  QCOMPARE(map_value(palette,mapping,NAN),missing_color);QCOMPARE(map_value(palette,mapping,0,true),masked_color);
  mapping.boundaries={0,0,1,3};QVERIFY(!validate(palette,mapping).isEmpty());
  mapping={};mapping.minimum=-2;mapping.maximum=6;mapping.normalization=Normalization::Centered;
  QVERIFY(validate(palette,mapping).isEmpty());QCOMPARE(fraction(0,mapping),.5);QCOMPARE(value_at(.5,mapping),0.);
  mapping.normalization=Normalization::Cyclic;mapping.origin=0;mapping.period=360;
  QCOMPARE(map_value(palette,mapping,0),map_value(palette,mapping,360));
  QCOMPARE(map_value(palette,mapping,-90),map_value(palette,mapping,270));
  mapping.period=0;QVERIFY(!validate(palette,mapping).isEmpty());
  mapping={};mapping.minimum=mapping.maximum=4;
  QCOMPARE(map_value(palette,mapping,4),sample(palette,.5));
  mapping.minimum=-std::numeric_limits<double>::max();mapping.maximum=std::numeric_limits<double>::max();
  QCOMPARE(fraction(0,mapping),.5);QCOMPARE(fraction(mapping.minimum/2,mapping),.25);
  Palette decoded;QVERIFY(parse_json(to_json(palette),decoded).isEmpty());QCOMPARE(decoded.colors,palette.colors);
}

void DropTargetsTest::colorMapUiUsesCachedDecodedPlane() {
  using namespace HdfColorMap;
  QStandardPaths::setTestModeEnabled(true);
  QTemporaryDir directory;QVERIFY(directory.isValid());const QString filename=directory.filePath("color-maps.nc");
  int file=-1, time=-1, row=-1, col=-1, variable=-1;
  QCOMPARE(nc_create(nc_path_bytes(filename).constData(),NC_CLOBBER,&file),NC_NOERR);
  QCOMPARE(nc_def_dim(file,"time",2,&time),NC_NOERR);QCOMPARE(nc_def_dim(file,"y",3,&row),NC_NOERR);QCOMPARE(nc_def_dim(file,"x",4,&col),NC_NOERR);
  int dimensions[]={time,row,col};QCOMPARE(nc_def_var(file,"field",NC_SHORT,3,dimensions,&variable),NC_NOERR);
  const double scale=.5,offset=-2;const short fill=-999;
  nc_put_att_double(file,variable,"scale_factor",NC_DOUBLE,1,&scale);nc_put_att_double(file,variable,"add_offset",NC_DOUBLE,1,&offset);
  nc_put_att_short(file,variable,"_FillValue",NC_SHORT,1,&fill);nc_put_att_text(file,variable,"units",1,"K");
  QCOMPARE(nc_enddef(file),NC_NOERR);short raw[24];for(int index=0;index<24;++index) raw[index]=short(index*2);raw[3]=fill;
  QCOMPARE(nc_put_var_short(file,variable,raw),NC_NOERR);QCOMPARE(nc_close(file),NC_NOERR);
  RasterView view(filename.toLocal8Bit().toStdString(),"/field",{2,3,4});view.resize(1100,750);view.show();QVERIFY(QTest::qWaitForWindowExposed(&view));
  QTRY_COMPARE(view.property("planeReadCount").toULongLong(),qulonglong(1));
  auto *selector=view.findChild<QComboBox*>("colorMapSelector");QVERIFY(selector);QVERIFY(selector->count()>=18);
  QCOMPARE(view.findChild<QAction*>("projsAction")->text(),QString("Projs"));
  QCOMPARE(view.findChild<QLabel*>("colorMapsLabel")->text(),QString("CMaps"));
  auto *canvas=view.findChild<QGraphicsView*>();QVERIFY(canvas);
  QGraphicsPixmapItem *image=nullptr;for(auto *item:canvas->scene()->items()) if(item->type()==QGraphicsPixmapItem::Type) image=static_cast<QGraphicsPixmapItem*>(item);
  QVERIFY(image);const QTransform transform=canvas->transform();
  auto *flip=view.findChild<QAction*>("flipHorizontalAction");flip->setChecked(true);
  const auto flipped=canvas->transform();
  selector->setCurrentIndex(selector->findData("bids:viridis"));
  QCOMPARE(view.property("minimum").toDouble(),-2.);QCOMPARE(view.property("maximum").toDouble(),9.);
  QCOMPARE(image->pixmap().toImage().pixel(0,0),uint32_t(qRgb(68,1,84)));
  QCOMPARE(image->pixmap().toImage().pixel(3,0),missing_color);
  auto *reverse=view.findChild<QCheckBox*>("reverseColorMap");reverse->setChecked(true);
  QCOMPARE(image->pixmap().toImage().pixel(0,0),uint32_t(qRgb(253,231,37)));
  for(int index=0;index<18;++index) selector->setCurrentIndex(index);
  QCOMPARE(view.property("planeReadCount").toULongLong(),qulonglong(1));QCOMPARE(canvas->transform(),flipped);
  selector->setCurrentIndex(selector->findData("bids:viridis"));reverse->setChecked(false);
  auto *profile=view.findChild<QAction*>("profileModeAction");profile->setChecked(true);
  QTest::mouseClick(canvas->viewport(),Qt::LeftButton,Qt::NoModifier,canvas->mapFromScene(QPointF(.5,.5)));
  QTest::mouseClick(canvas->viewport(),Qt::LeftButton,Qt::NoModifier,canvas->mapFromScene(QPointF(2.5,1.5)));
  view.findChild<QAction*>("showProfileAction")->trigger();auto *profile_window=view.findChild<ProfileView*>();QVERIFY(profile_window);
  const auto profile_mean=profile_window->property("mean").toDouble();
  QList<QGraphicsItem*> overlay_items;
  for(auto *item:canvas->scene()->items()) if(item->data(0).toString()=="profileOverlay") overlay_items<<item;
  QVERIFY(overlay_items.size()>=3);
  selector->setCurrentIndex(selector->findData("cmocean:thermal"));QCOMPARE(profile_window->property("mean").toDouble(),profile_mean);
  for(auto *item:overlay_items) QVERIFY(canvas->scene()->items().contains(item));
  auto *mask_start=view.findChild<QLineEdit*>("maskStart");auto *mask_end=view.findChild<QLineEdit*>("maskEnd");
  mask_start->setText("-2");mask_end->setText("-2");QMetaObject::invokeMethod(mask_end,"editingFinished");
  QCOMPARE(image->pixmap().toImage().pixel(0,0),masked_color);QCOMPARE(view.property("masked").toULongLong(),qulonglong(1));
  QCOMPARE(view.property("planeReadCount").toULongLong(),qulonglong(1));
  mask_start->clear();mask_end->clear();QMetaObject::invokeMethod(mask_end,"editingFinished");
  QCOMPARE(view.property("minimum").toDouble(),-2.);QCOMPARE(view.property("masked").toULongLong(),qulonglong(0));
  view.set_layer_index(0,1);QCOMPARE(view.property("planeReadCount").toULongLong(),qulonglong(2));
  QVERIFY(profile_window->property("mean").toDouble()>profile_mean+10);
  const auto count=view.property("planeReadCount").toULongLong();
  auto *range=view.findChild<QComboBox*>("colorRangeMode");
  view.findChild<QDoubleSpinBox*>("colorMinimum")->setValue(-10);view.findChild<QDoubleSpinBox*>("colorMaximum")->setValue(50);range->setCurrentIndex(1);
  QCOMPARE(view.property("planeReadCount").toULongLong(),count);
  QTemporaryFile rgb;QVERIFY(rgb.open());rgb.write("ncolor=2\n0 0 0\n255 255 255\n");rgb.flush();
  QVERIFY(view.import_color_map(rgb.fileName(),RgbScale::Byte).isEmpty());
  const QString imported=selector->currentData().toString();QVERIFY(imported.startsWith("import:"));
  QVERIFY(QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/colormaps/"+imported.mid(7)+".json"));
  QCOMPARE(view.property("planeReadCount").toULongLong(),count);
  // Exercise real widgets for centered/cyclic and custom boundaries, with no modal interaction shortcuts.
  QString error;const auto palettes=load_registry(error);Mapping settings;settings.minimum=-10;settings.maximum=50;
  ColorMapPicker picker(palettes,5,settings);picker.show();QVERIFY(QTest::qWaitForWindowExposed(&picker));
  picker.findChild<QComboBox*>("colorNormalization")->setCurrentIndex(1);
  picker.findChild<QLineEdit*>("colorCenter")->setText("0");
  auto *buttons=picker.findChild<QDialogButtonBox*>();QTest::mouseClick(buttons->button(QDialogButtonBox::Ok),Qt::LeftButton);
  QCOMPARE(picker.result(),int(QDialog::Accepted));QCOMPARE(picker.selected_mapping().normalization,Normalization::Centered);
  const QString log=qEnvironmentVariable("HDFSTUDIO_TEST_LOG_DIR");
  if(!log.isEmpty()) {profile_window->hide();view.raise();QTest::qWait(100);QVERIFY(view.grab().save(QDir(log).filePath("color-map-"+qEnvironmentVariable("HDFSTUDIO_TEST_CONFIG")+".png")));}
  Q_UNUSED(transform);
}

void DropTargetsTest::colorMapLatestAsyncRenderWins() {
  QTemporaryDir directory;const QString filename=directory.filePath("async.nc");
  int file=-1,row=-1,col=-1,variable=-1;QCOMPARE(nc_create(nc_path_bytes(filename).constData(),NC_CLOBBER,&file),NC_NOERR);
  nc_def_dim(file,"y",300,&row);nc_def_dim(file,"x",300,&col);int dimensions[]={row,col};nc_def_var(file,"field",NC_DOUBLE,2,dimensions,&variable);nc_enddef(file);
  std::vector<double> values(90000);std::iota(values.begin(),values.end(),0.);nc_put_var_double(file,variable,values.data());nc_close(file);
  RasterView view(filename.toLocal8Bit().toStdString(),"/field",{300,300});view.resize(900,650);view.show();QVERIFY(QTest::qWaitForWindowExposed(&view));
  QTRY_VERIFY(view.property("planeReadCount").isValid());
  auto *selector=view.findChild<QComboBox*>("colorMapSelector");for(int iteration=0;iteration<40;++iteration) selector->setCurrentIndex(iteration%18);
  selector->setCurrentIndex(selector->findData("bids:viridis"));
  auto *canvas=view.findChild<QGraphicsView*>();QGraphicsPixmapItem *item=nullptr;
  for(auto *candidate:canvas->scene()->items()) if(candidate->type()==QGraphicsPixmapItem::Type) item=static_cast<QGraphicsPixmapItem*>(candidate);
  QVERIFY(item);
  QTRY_VERIFY(!item->pixmap().isNull());
  QTRY_COMPARE(item->pixmap().toImage().pixel(0,0),uint32_t(qRgb(68,1,84)));
  QCOMPARE(view.property("planeReadCount").toULongLong(),qulonglong(1));
  // Close while recoloring; queued completion must not touch a destroyed view.
  auto *closing=new RasterView(filename.toLocal8Bit().toStdString(),"/field",{300,300});closing->show();
  QTRY_VERIFY(closing->property("planeReadCount").isValid());delete closing;QTest::qWait(100);
  // Existing batch preview must save a complete large image before the process exits.
  QProcess batch;
  const QString output=directory.filePath("preview.png");
  batch.start(QDir(QCoreApplication::applicationDirPath()).filePath("HDFStudio.exe"),
              {"--preview","field","--png",output,filename});
  QVERIFY(batch.waitForFinished(30000));QCOMPARE(batch.exitCode(),0);
  const QImage exported(output);QCOMPARE(exported.size(),QSize(300,300));
  QCOMPARE(exported.pixel(0,0),RasterColorMap::color(RasterColorMap::Palette::Viridis,0,false).rgba());
  QCOMPARE(exported.pixel(299,299),RasterColorMap::color(RasterColorMap::Palette::Viridis,1,false).rgba());
}

void DropTargetsTest::colorMapMissingCategoriesColorbarAndBudget() {
  using namespace HdfColorMap;
  QTemporaryDir directory;const QString filename=directory.filePath("edge.nc");
  int file=-1,time=-1,row=-1,col=-1,variable=-1;
  QCOMPARE(nc_create(nc_path_bytes(filename).constData(),NC_NETCDF4|NC_CLOBBER,&file),NC_NOERR);
  nc_def_dim(file,"time",3,&time);nc_def_dim(file,"y",2,&row);nc_def_dim(file,"x",3,&col);
  int dimensions[]={time,row,col};nc_def_var(file,"field",NC_DOUBLE,3,dimensions,&variable);
  const double fill=-999,flags[]={10,2};const char *unit="K";
  nc_put_att_double(file,variable,"_FillValue",NC_DOUBLE,1,&fill);
  nc_put_att_double(file,variable,"flag_values",NC_DOUBLE,2,flags);
  nc_put_att_text(file,variable,"flag_meanings",8,"high low");
  nc_put_att_string(file,variable,"units",1,&unit);nc_enddef(file);
  double values[]={4,4,4,4,4,4,fill,fill,fill,fill,fill,fill,10,2,10,2,10,2};
  QCOMPARE(nc_put_var_double(file,variable,values),NC_NOERR);QCOMPARE(nc_close(file),NC_NOERR);
  RasterView view(filename.toLocal8Bit().toStdString(),"/field",{3,2,3});view.resize(1100,700);view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));QTRY_COMPARE(view.property("planeReadCount").toULongLong(),qulonglong(1));
  auto *canvas=view.findChild<QGraphicsView*>();QGraphicsPixmapItem *image=nullptr;
  for(auto *item:canvas->scene()->items()) if(item->type()==QGraphicsPixmapItem::Type) image=static_cast<QGraphicsPixmapItem*>(item);
  QVERIFY(image);QCOMPARE(view.property("minimum").toDouble(),4.);QCOMPARE(view.property("maximum").toDouble(),4.);
  QCOMPARE(image->pixmap().toImage().pixel(0,0),RasterColorMap::color(RasterColorMap::Palette::Viridis,.5,false).rgba());
  QVERIFY(view.findChild<ColorBar*>()->toolTip().contains("K"));
  view.set_layer_index(0,1);QVERIFY(!image->pixmap().isNull());QCOMPARE(image->pixmap().toImage().pixel(0,0),missing_color);
  QVERIFY(!view.property("minimum").isValid());
  view.set_layer_index(0,2);view.findChild<QCheckBox*>("discreteColors")->setChecked(true);
  const QString labels=view.findChild<QLabel*>("rasterLegend")->text();
  QVERIFY(labels.contains("10 = high"));QVERIFY(labels.contains("2 = low"));
  QCOMPARE(image->pixmap().toImage().pixel(0,0),RasterColorMap::categoryColor(RasterColorMap::Palette::Viridis,0,2,false).rgba());
  auto *mask_start=view.findChild<QLineEdit*>("maskStart"),*mask_end=view.findChild<QLineEdit*>("maskEnd");
  mask_start->setText("-1000");mask_end->setText("1000");QMetaObject::invokeMethod(mask_end,"editingFinished");
  QCOMPARE(image->pixmap().toImage().pixel(0,0),masked_color);QCOMPARE(view.property("planeReadCount").toULongLong(),qulonglong(3));
  mask_start->clear();mask_end->clear();QMetaObject::invokeMethod(mask_end,"editingFinished");
  QCOMPARE(image->pixmap().toImage().pixel(0,0),RasterColorMap::categoryColor(RasterColorMap::Palette::Viridis,0,2,false).rgba());

  QString error;const auto palettes=load_registry(error);QVERIFY(error.isEmpty());
  ColorBar bar;bar.resize(600,94);bar.show();QVERIFY(QTest::qWaitForWindowExposed(&bar));
  Mapping settings;settings.minimum=-2;settings.maximum=6;
  for(int mode=0;mode<4;++mode) {
    settings.mode=mode==1?Mode::Stepped:Mode::Continuous;
    settings.normalization=mode==2?Normalization::Centered:mode==3?Normalization::Cyclic:Normalization::Linear;
    const auto &palette=palettes[5];bar.set_mapping(palette,settings,"K");
    const auto bitmap=bar.grab().toImage();const double dpr=bitmap.devicePixelRatio();
    for(int x:{44,100,237,400,556}) {
      const double fraction=double(x-44)/(556-44);
      // The outer 1-pixel frame is intentional; compare interior color pixels.
      if(x==44||x==556) continue;
      QCOMPARE(bitmap.pixel(qRound(x*dpr),qRound(32*dpr)),map_value(palette,settings,value_at(fraction,settings)));
    }
  }
  const QByteArray previous=qgetenv("HDFSTUDIO_RASTER_BUDGET_MIB");
  qputenv("HDFSTUDIO_RASTER_BUDGET_MIB","1");
  const auto restore=qScopeGuard([previous]{if(previous.isNull()) qunsetenv("HDFSTUDIO_RASTER_BUDGET_MIB");else qputenv("HDFSTUDIO_RASTER_BUDGET_MIB",previous);});
  RasterView blocked(filename.toLocal8Bit().toStdString(),"/field",{3,1000,1000});blocked.show();
  QTRY_VERIFY(blocked.findChild<QLabel*>("rasterLegend")->text().contains("memory budget"));
  QVERIFY(!blocked.property("planeReadCount").isValid());
}

void DropTargetsTest::invalidFractionsAreClamped() {
  using RasterColorMap::Palette;
  QCOMPARE(RasterColorMap::color(Palette::Viridis, -1.0, false),
           RasterColorMap::color(Palette::Viridis, 0.0, false));
  QCOMPARE(RasterColorMap::color(Palette::Viridis, 2.0, false),
           RasterColorMap::color(Palette::Viridis, 1.0, false));
  QCOMPARE(RasterColorMap::color(Palette::Viridis,
                                 std::numeric_limits<double>::quiet_NaN(), false),
           RasterColorMap::color(Palette::Viridis, 0.5, false));
}

void DropTargetsTest::automaticAndFixedRangesAreValidated() {
  using RasterColorMap::Palette;
  auto automatic = RasterColorMap::resolveRange(true, 2.0, 8.0, -1.0, 1.0, true);
  QVERIFY(automatic.valid);
  QCOMPARE(automatic.minimum, 2.0);
  QCOMPARE(automatic.maximum, 8.0);
  auto constant = RasterColorMap::resolveRange(true, 4.0, 4.0, 0.0, 1.0, true);
  QVERIFY(constant.valid);
  QCOMPARE(RasterColorMap::colorValue(Palette::Viridis, 4.0,
                                     constant.minimum, constant.maximum, false),
           RasterColorMap::color(Palette::Viridis, 0.5, false));
  QVERIFY(!RasterColorMap::resolveRange(true, 0.0, 0.0, 0.0, 1.0, false).valid);
  auto fixed = RasterColorMap::resolveRange(false, 2.0, 8.0, 0.0, 10.0, true);
  QVERIFY(fixed.valid);
  QCOMPARE(fixed.minimum, 0.0);
  QCOMPARE(fixed.maximum, 10.0);
  QCOMPARE(RasterColorMap::colorValue(Palette::Plasma, 5.0,
                                     fixed.minimum, fixed.maximum, false),
           RasterColorMap::color(Palette::Plasma, 0.5, false));
  QVERIFY(!RasterColorMap::resolveRange(false, 0.0, 1.0, 5.0, 5.0, true).valid);
  QVERIFY(!RasterColorMap::resolveRange(false, 0.0, 1.0, 6.0, 5.0, true).valid);
}

void DropTargetsTest::discreteCategoriesAreStableAndHandleUnknowns() {
  using RasterColorMap::Palette;
  const std::vector<double> categories={-5.0,2.0,100.0};
  const auto labels=RasterColorMap::categoryLabels(categories, {"dry","wet"});
  QCOMPARE(labels.size(),3);
  QCOMPARE(labels[0],QString("-5 = dry"));
  QCOMPARE(labels[1],QString("2 = wet"));
  QCOMPARE(labels[2],QString("100"));
  const QColor unknown(255,0,255);
  const QColor negative=RasterColorMap::discreteValueColor(Palette::Turbo,-5.0,categories,false,unknown);
  const QColor middle=RasterColorMap::discreteValueColor(Palette::Turbo,2.0,categories,false,unknown);
  QVERIFY(negative!=middle);
  QCOMPARE(negative,RasterColorMap::discreteValueColor(Palette::Turbo,-5.0,categories,false,unknown));
  QCOMPARE(RasterColorMap::discreteValueColor(Palette::Turbo,77.0,categories,false,unknown),unknown);
  QCOMPARE(RasterColorMap::discreteValueColor(Palette::Turbo,
      std::numeric_limits<double>::quiet_NaN(),categories,false,unknown),QColor(110,110,110));
  QCOMPARE(RasterColorMap::discreteValueColor(Palette::Turbo,-5.0,categories,true,unknown),
           RasterColorMap::categoryColor(Palette::Turbo,0,categories.size(),true));
}

void DropTargetsTest::propertyAttributePreviewMarksTruncation() {
  const QString shortValue=HdfProperties::formatAttribute(
      "units","8-byte string","Scalar","mm/day",8);
  QVERIFY(shortValue.contains("units | 8-byte string | Scalar | mm/day"));
  QVERIFY(!shortValue.contains("truncated"));
  const QString longValue=HdfProperties::formatAttribute(
      "long_name","string","Scalar",QString(300,'x'),24);
  QVERIFY(longValue.contains("[truncated]"));
  QVERIFY(longValue.contains(QString::fromUtf8("…")));
  QCOMPARE(longValue.size(),QString("long_name | string | Scalar | ").size()+24+1+QString(" [truncated]").size());
}

void DropTargetsTest::propertiesInspectorReadsDatasetLayoutAndAttributes() {
  QTemporaryFile temporary;
  QVERIFY(temporary.open());
  const QString filePath=temporary.fileName();
  temporary.close();
  QByteArray nativePath=filePath.toLocal8Bit();
  hid_t file=H5Fcreate(nativePath.constData(),H5F_ACC_TRUNC,H5P_DEFAULT,H5P_DEFAULT);
  QVERIFY(file>=0);
  hsize_t dims[2]={3,4};
  hid_t space=H5Screate_simple(2,dims,nullptr);
  QVERIFY(space>=0);
  hid_t creation=H5Pcreate(H5P_DATASET_CREATE);
  QVERIFY(creation>=0);
  hsize_t chunks[2]={2,2};
  QCOMPARE(H5Pset_chunk(creation,2,chunks),herr_t(0));
  QCOMPARE(H5Pset_deflate(creation,1),herr_t(0));
  hid_t dataset=H5Dcreate2(file,"/field",H5T_STD_I16LE,space,H5P_DEFAULT,creation,H5P_DEFAULT);
  QVERIFY(dataset>=0);
  hid_t stringType=H5Tcopy(H5T_C_S1);
  QVERIFY(stringType>=0);
  QCOMPARE(H5Tset_size(stringType,8),herr_t(0));
  hid_t scalar=H5Screate(H5S_SCALAR);
  QVERIFY(scalar>=0);
  hid_t attribute=H5Acreate2(dataset,"units",stringType,scalar,H5P_DEFAULT,H5P_DEFAULT);
  QVERIFY(attribute>=0);
  char units[8]={'m','m','/','d','a','y',' ',' '};
  QCOMPARE(H5Awrite(attribute,stringType,units),herr_t(0));
  H5Aclose(attribute); H5Sclose(scalar); H5Tclose(stringType);
  H5Dclose(dataset); H5Pclose(creation); H5Sclose(space); H5Fclose(file);

  const auto summary=HdfProperties::inspect(filePath,"/field");
  QVERIFY(summary.valid);
  QCOMPARE(summary.objectType,QString("Dataset"));
  QCOMPARE(summary.dataType,QString("16-bit signed integer"));
  QCOMPARE(summary.shape,QString("[3 x 4]"));
  QCOMPARE(summary.elements,quint64(12));
  QCOMPARE(summary.bytes,quint64(24));
  QVERIFY(summary.storage.startsWith("Chunked"));
  QVERIFY(summary.compression.contains("deflate"));
  QVERIFY(summary.attributes.join("\n").contains("units | 8-byte string | Scalar | mm/day"));
  const auto rootSummary=HdfProperties::inspect(filePath,"/");
  QVERIFY(rootSummary.valid);
  QCOMPARE(rootSummary.objectType,QString("Group"));
  QFile::remove(filePath);
}

void DropTargetsTest::textExportStreamsValuesInRowsAndRejectsUnsupportedTypes() {
  QTemporaryFile source;
  QVERIFY(source.open());
  const QString sourcePath=source.fileName();
  source.close();
  QByteArray nativePath=sourcePath.toLocal8Bit();
  hid_t file=H5Fcreate(nativePath.constData(),H5F_ACC_TRUNC,H5P_DEFAULT,H5P_DEFAULT);
  QVERIFY(file>=0);
  hsize_t dims[2]={2,3};
  hid_t space=H5Screate_simple(2,dims,nullptr);
  hid_t dataset=H5Dcreate2(file,"/values",H5T_STD_I32LE,space,H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
  QVERIFY(dataset>=0);
  qint32 numbers[6]={-3,0,5,8,13,21};
  QCOMPARE(H5Dwrite(dataset,H5T_NATIVE_INT32,H5S_ALL,H5S_ALL,H5P_DEFAULT,numbers),herr_t(0));
  hid_t textType=H5Tcopy(H5T_C_S1);
  H5Tset_size(textType,H5T_VARIABLE);
  hid_t textSpace=H5Screate_simple(1,dims,nullptr);
  hid_t textDataset=H5Dcreate2(file,"/strings",textType,textSpace,H5P_DEFAULT,H5P_DEFAULT,H5P_DEFAULT);
  QVERIFY(textDataset>=0);
  H5Dclose(textDataset); H5Sclose(textSpace); H5Tclose(textType);
  H5Dclose(dataset); H5Sclose(space); H5Fclose(file);

  QByteArray output;
  QBuffer buffer(&output);
  QVERIFY(buffer.open(QIODevice::WriteOnly));
  auto result=HdfTextExport::write(sourcePath,"/values",buffer,2);
  QVERIFY2(result.ok,qPrintable(result.error));
  QCOMPARE(result.valuesWritten,quint64(6));
  QCOMPARE(QString::fromUtf8(output),QString("-3,0,5\n8,13,21\n"));
  QByteArray rejected;
  QBuffer rejectedBuffer(&rejected);
  QVERIFY(rejectedBuffer.open(QIODevice::WriteOnly));
  auto unsupported=HdfTextExport::write(sourcePath,"/strings",rejectedBuffer,2);
  QVERIFY(!unsupported.ok);
  QVERIFY(rejected.isEmpty());
  QByteArray cancelledBytes;
  QBuffer cancelledBuffer(&cancelledBytes);
  QVERIFY(cancelledBuffer.open(QIODevice::WriteOnly));
  auto cancelled=HdfTextExport::write(sourcePath,"/values",cancelledBuffer,2,
      [](quint64 written,quint64){return written==0;});
  QVERIFY(!cancelled.ok);
  QCOMPARE(cancelled.error,QString("Export cancelled"));
  QVERIFY(cancelled.valuesWritten>0);
  QFile::remove(sourcePath);
}

void DropTargetsTest::vectorPlanePairRequiresMatchingCompleteShapes() {
  NcVectorPlane pair;
  pair.u={2,3,{1,2,3,4,5,6}};
  pair.v={2,3,{6,5,4,3,2,1}};
  QVERIFY(valid_vector_pair(pair));
  pair.v.cols=2;
  QVERIFY(!valid_vector_pair(pair));
  pair.v={2,3,{1,2,3}};
  QVERIFY(!valid_vector_pair(pair));
  pair.v={0,3,{}};
  QVERIFY(!valid_vector_pair(pair));
}

void DropTargetsTest::geographicCoordinatesRequireExplicitCfMetadataAndMatchingDimensions() {
  QTemporaryFile temporary;
  QVERIFY(temporary.open());
  const QString filePath=temporary.fileName();
  temporary.close();
  int ncid=-1,latitudeDim=-1,longitudeDim=-1,latitudeVar=-1,longitudeVar=-1,dataVar=-1;
  const QByteArray path=filePath.toUtf8();
  QCOMPARE(nc_create(path.constData(),NC_CLOBBER,&ncid),NC_NOERR);
  QCOMPARE(nc_def_dim(ncid,"latitude",2,&latitudeDim),NC_NOERR);
  QCOMPARE(nc_def_dim(ncid,"longitude",3,&longitudeDim),NC_NOERR);
  QCOMPARE(nc_def_var(ncid,"latitude",NC_DOUBLE,1,&latitudeDim,&latitudeVar),NC_NOERR);
  QCOMPARE(nc_def_var(ncid,"longitude",NC_DOUBLE,1,&longitudeDim,&longitudeVar),NC_NOERR);
  const int dimensions[2]={latitudeDim,longitudeDim};
  QCOMPARE(nc_def_var(ncid,"field",NC_FLOAT,2,dimensions,&dataVar),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,latitudeVar,"standard_name",8,"latitude"),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,latitudeVar,"units",13,"degrees_north"),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,longitudeVar,"standard_name",9,"longitude"),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,longitudeVar,"units",12,"degrees_east"),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,dataVar,"coordinates",18,"latitude longitude"),NC_NOERR);
  QCOMPARE(nc_enddef(ncid),NC_NOERR);
  const double latitude[2]={-10,10},longitude[3]={100,110,120};
  QCOMPARE(nc_put_var_double(ncid,latitudeVar,latitude),NC_NOERR);
  QCOMPARE(nc_put_var_double(ncid,longitudeVar,longitude),NC_NOERR);
  QCOMPARE(nc_close(ncid),NC_NOERR);
  NcReader reader(filePath);
  auto grid=reader.geographic_grid("/field",{2,3});
  QVERIFY2(grid.valid,qPrintable(grid.error));
  QCOMPARE(grid.latitudePath,std::string("latitude"));
  QCOMPARE(grid.longitudePath,std::string("longitude"));
  const auto coordinate=geographic_coordinate(grid,1,2);
  QVERIFY(coordinate.valid);
  QCOMPARE(coordinate.latitude,10.0);
  QCOMPARE(coordinate.longitude,120.0);
  QVERIFY(!geographic_coordinate(grid,2,0).valid);
  auto mismatch=reader.geographic_grid("/field",{3,2});
  QVERIFY(!mismatch.valid);
  NcGeographicGrid invalidLongitude=grid;
  invalidLongitude.longitude[0]=999;
  invalidLongitude.valid=false;
  QCOMPARE(geographic_coordinate(invalidLongitude,0,0).valid,false);
  QFile::remove(filePath);
}

void DropTargetsTest::ugridTriangleReaderHonorsStartIndexAndRejectsNonTriangles_data() {
  QTest::addColumn<int>("corners");
  QTest::addColumn<int>("startLength");
  QTest::addColumn<QByteArray>("location");
  QTest::addColumn<int>("lastNode");
  QTest::addColumn<QString>("error");
  QTest::newRow("one-based") << 3 << 1 << QByteArray("face") << 4 << QString();
  QTest::newRow("out-of-range") << 3 << 1 << QByteArray("face") << 9 << QString("out-of-range");
  QTest::newRow("fill-index") << 3 << 1 << QByteArray("face") << -99 << QString("out-of-range");
  QTest::newRow("quadrilateral") << 4 << 1 << QByteArray("face") << 4 << QString("three-node");
  QTest::newRow("array-start-index") << 3 << 2 << QByteArray("face") << 4 << QString("scalar integer");
  QTest::newRow("node-location") << 3 << 1 << QByteArray("node") << 4 << QString("location=face");
}

void DropTargetsTest::ugridTriangleReaderHonorsStartIndexAndRejectsNonTriangles() {
  QFETCH(int,corners);
  QFETCH(int,startLength);
  QFETCH(QByteArray,location);
  QFETCH(int,lastNode);
  QFETCH(QString,error);
  QTemporaryFile temporary;
  QVERIFY(temporary.open());
  const QString filePath=temporary.fileName();temporary.close();
  QByteArray path=filePath.toUtf8();
  int ncid=-1,nodeDim=-1,faceDim=-1,threeDim=-1,meshVar=-1,latVar=-1,lonVar=-1,connectivityVar=-1,dataVar=-1;
  QCOMPARE(nc_create(path.constData(),NC_CLOBBER,&ncid),NC_NOERR);
  QCOMPARE(nc_def_dim(ncid,"node",4,&nodeDim),NC_NOERR);
  QCOMPARE(nc_def_dim(ncid,"face",2,&faceDim),NC_NOERR);
  QCOMPARE(nc_def_dim(ncid,"corners",corners,&threeDim),NC_NOERR);
  QCOMPARE(nc_def_var(ncid,"mesh",NC_INT,0,nullptr,&meshVar),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,meshVar,"cf_role",13,"mesh_topology"),NC_NOERR);
  const int topologyDimension=2;
  QCOMPARE(nc_put_att_int(ncid,meshVar,"topology_dimension",NC_INT,1,&topologyDimension),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,meshVar,"node_coordinates",7,"lat lon"),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,meshVar,"face_node_connectivity",4,"conn"),NC_NOERR);
  QCOMPARE(nc_def_var(ncid,"lat",NC_DOUBLE,1,&nodeDim,&latVar),NC_NOERR);
  QCOMPARE(nc_def_var(ncid,"lon",NC_DOUBLE,1,&nodeDim,&lonVar),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,latVar,"standard_name",8,"latitude"),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,latVar,"units",13,"degrees_north"),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,lonVar,"standard_name",9,"longitude"),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,lonVar,"units",12,"degrees_east"),NC_NOERR);
  const int connectivityDims[2]={faceDim,threeDim};
  QCOMPARE(nc_def_var(ncid,"conn",NC_INT,2,connectivityDims,&connectivityVar),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,connectivityVar,"cf_role",22,"face_node_connectivity"),NC_NOERR);
  const int startIndex[]={1,0};
  const int fill=-99;
  QCOMPARE(nc_put_att_int(ncid,connectivityVar,"start_index",NC_INT,startLength,startIndex),NC_NOERR);
  QCOMPARE(nc_put_att_int(ncid,connectivityVar,"_FillValue",NC_INT,1,&fill),NC_NOERR);
  QCOMPARE(nc_def_var(ncid,"value",NC_FLOAT,1,&faceDim,&dataVar),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,dataVar,"mesh",4,"mesh"),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,dataVar,"location",location.size(),location.constData()),NC_NOERR);
  QCOMPARE(nc_enddef(ncid),NC_NOERR);
  const double lat[4]={0,0,1,1},lon[4]={0,1,1,0};
  const int faces[8]={1,2,3,1,3,lastNode,2,4};
  const float values[2]={10,20};
  QCOMPARE(nc_put_var_double(ncid,latVar,lat),NC_NOERR);
  QCOMPARE(nc_put_var_double(ncid,lonVar,lon),NC_NOERR);
  QCOMPARE(nc_put_var_int(ncid,connectivityVar,faces),NC_NOERR);
  QCOMPARE(nc_put_var_float(ncid,dataVar,values),NC_NOERR);
  QCOMPARE(nc_close(ncid),NC_NOERR);
  NcReader reader(filePath);
  const auto mesh=reader.read_ugrid_faces("/value");
  if(!error.isEmpty()) {
    QVERIFY(!mesh.valid);
    QVERIFY2(mesh.error.contains(error),qPrintable(mesh.error));
    return;
  }
  QVERIFY2(mesh.valid,qPrintable(mesh.error));
  QCOMPARE(mesh.faceCount,size_t(2));
  QCOMPARE(mesh.nodeCount,size_t(4));
  QCOMPARE(mesh.nodesPerFace,size_t(3));
  QCOMPARE(mesh.faceNodes,std::vector<size_t>({0,1,2,0,2,3}));
  QCOMPARE(mesh.faceValues,std::vector<double>({10,20}));
}

void DropTargetsTest::ugridMeshViewRendersVerifiedTriangleFixture() {
  QTemporaryFile temporary;
  QVERIFY(temporary.open());
  const QString filePath=temporary.fileName();temporary.close();
  QByteArray path=filePath.toUtf8();
  int ncid=-1,nodeDim=-1,faceDim=-1,threeDim=-1,meshVar=-1,latVar=-1,lonVar=-1,connectivityVar=-1,dataVar=-1;
  QCOMPARE(nc_create(path.constData(),NC_CLOBBER,&ncid),NC_NOERR);
  QCOMPARE(nc_def_dim(ncid,"node",3,&nodeDim),NC_NOERR);
  QCOMPARE(nc_def_dim(ncid,"face",1,&faceDim),NC_NOERR);
  QCOMPARE(nc_def_dim(ncid,"three",3,&threeDim),NC_NOERR);
  QCOMPARE(nc_def_var(ncid,"mesh",NC_INT,0,nullptr,&meshVar),NC_NOERR);
  const int topologyDimension=2;
  QCOMPARE(nc_put_att_text(ncid,meshVar,"cf_role",13,"mesh_topology"),NC_NOERR);
  QCOMPARE(nc_put_att_int(ncid,meshVar,"topology_dimension",NC_INT,1,&topologyDimension),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,meshVar,"node_coordinates",7,"lat lon"),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,meshVar,"face_node_connectivity",4,"conn"),NC_NOERR);
  QCOMPARE(nc_def_var(ncid,"lat",NC_DOUBLE,1,&nodeDim,&latVar),NC_NOERR);
  QCOMPARE(nc_def_var(ncid,"lon",NC_DOUBLE,1,&nodeDim,&lonVar),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,latVar,"standard_name",8,"latitude"),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,latVar,"units",13,"degrees_north"),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,lonVar,"standard_name",9,"longitude"),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,lonVar,"units",12,"degrees_east"),NC_NOERR);
  const int dims[2]={faceDim,threeDim};
  QCOMPARE(nc_def_var(ncid,"conn",NC_INT,2,dims,&connectivityVar),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,connectivityVar,"cf_role",22,"face_node_connectivity"),NC_NOERR);
  const int start=0;
  QCOMPARE(nc_put_att_int(ncid,connectivityVar,"start_index",NC_INT,1,&start),NC_NOERR);
  QCOMPARE(nc_def_var(ncid,"value",NC_FLOAT,1,&faceDim,&dataVar),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,dataVar,"mesh",4,"mesh"),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,dataVar,"location",4,"face"),NC_NOERR);
  QCOMPARE(nc_enddef(ncid),NC_NOERR);
  const double lat[3]={0,0,1},lon[3]={0,1,0};const int face[3]={0,1,2};const float value=7;
  nc_put_var_double(ncid,latVar,lat);nc_put_var_double(ncid,lonVar,lon);
  nc_put_var_int(ncid,connectivityVar,face);nc_put_var_float(ncid,dataVar,&value);nc_close(ncid);
  MeshView view(filePath,"/value");
  QVERIFY(view.findChild<QGraphicsScene*>());
  QVERIFY(view.findChild<QLabel*>()->text().contains("UGRID 2D triangles"));
  QFile::remove(filePath);
}

void DropTargetsTest::formatProbeDistinguishesNetcdfModelsAndHdf5Containers() {
  H5Eset_auto(H5E_DEFAULT,nullptr,nullptr);
  struct FormatCase { int mode; QString expected; };
  const QList<FormatCase> cases={
    {NC_CLOBBER,"NetCDF CDF-1"},
    {NC_CLOBBER|NC_64BIT_OFFSET,"NetCDF CDF-2"},
    {NC_CLOBBER|NC_64BIT_DATA,"NetCDF CDF-5"},
    {NC_CLOBBER|NC_NETCDF4|NC_CLASSIC_MODEL,"NetCDF-4 classic model"},
    {NC_CLOBBER|NC_NETCDF4,"NetCDF-4 enhanced"}
  };
  for(const auto &testCase:cases) {
    QTemporaryFile temporary;QVERIFY(temporary.open());
    const QByteArray path=temporary.fileName().toUtf8();temporary.close();
    int ncid=-1;
    QCOMPARE(nc_create(path.constData(),testCase.mode,&ncid),NC_NOERR);
    QCOMPARE(nc_close(ncid),NC_NOERR);
    const auto probe=probe_data_format(QString::fromUtf8(path));
    QVERIFY(probe.netcdf);
    QCOMPARE(probe.kind,testCase.expected);
    const hid_t hdfAttempt=H5Fopen(path.constData(),H5F_ACC_RDONLY,H5P_DEFAULT);
    QCOMPARE(hdfAttempt<0,testCase.expected.startsWith("NetCDF CDF-"));
    if(hdfAttempt>=0) H5Fclose(hdfAttempt);
    QFile::remove(QString::fromUtf8(path));
  }
  QTemporaryFile hdfTemporary;QVERIFY(hdfTemporary.open());
  const QByteArray hdfPath=hdfTemporary.fileName().toUtf8();hdfTemporary.close();
  hid_t file=H5Fcreate(hdfPath.constData(),H5F_ACC_TRUNC,H5P_DEFAULT,H5P_DEFAULT);
  QVERIFY(file>=0);QCOMPARE(H5Fclose(file),herr_t(0));
  QCOMPARE(probe_data_format(QString::fromUtf8(hdfPath)).kind,
           QString("HDF5 container (NetCDF identity unverified)"));
  QFile::remove(QString::fromUtf8(hdfPath));
  QTemporaryFile unknown;QVERIFY(unknown.open());
  QVERIFY(unknown.write("not a data file")>0);unknown.flush();
  QCOMPARE(probe_data_format(unknown.fileName()).kind,QString("Unknown"));
}

void DropTargetsTest::cf18DiagnosticsReportScopedReferencesAndMissingDeclaration() {
  QTemporaryFile temporary;QVERIFY(temporary.open());
  const QByteArray path=temporary.fileName().toUtf8();temporary.close();
  int ncid=-1,group=-1,dimension=-1,field=-1,coordinate=-1;
  QCOMPARE(nc_create(path.constData(),NC_NETCDF4|NC_CLOBBER,&ncid),NC_NOERR);
  QCOMPARE(nc_def_grp(ncid,"nested",&group),NC_NOERR);
  QCOMPARE(nc_def_dim(group,"x",2,&dimension),NC_NOERR);
  QCOMPARE(nc_def_var(group,"field",NC_FLOAT,1,&dimension,&field),NC_NOERR);
  QCOMPARE(nc_def_var(group,"x",NC_FLOAT,1,&dimension,&coordinate),NC_NOERR);
  int badBoundsVariable=-1;
  QCOMPARE(nc_def_var(group,"bad_bounds",NC_FLOAT,1,&dimension,&badBoundsVariable),NC_NOERR);
  const float fill=-999;
  QCOMPARE(nc_put_att_text(group,field,"coordinates",3,"bad"),NC_NOERR);
  QCOMPARE(nc_put_att_text(group,field,"bounds",8,"missing"),NC_NOERR);
  QCOMPARE(nc_put_att_float(group,field,"_FillValue",NC_FLOAT,1,&fill),NC_NOERR);
  QCOMPARE(nc_put_att_text(group,badBoundsVariable,"bounds",8,"no_bounds"),NC_NOERR);
  QCOMPARE(nc_enddef(ncid),NC_NOERR);
  QCOMPARE(nc_close(ncid),NC_NOERR);
  NcReader reader(QString::fromUtf8(path));
  const auto diagnostics=reader.diagnose_cf18();
  auto contains=[&](const QString &rule,const QString &pathFragment) {
    return std::any_of(diagnostics.begin(),diagnostics.end(),[&](const CfDiagnostic &diagnostic) {
      return diagnostic.rule==rule && diagnostic.path.contains(pathFragment);
    });
  };
  QVERIFY(contains("CF18-GLOBAL-001","/"));
  QVERIFY(contains("CF18-BOUNDS-001","/nested/field"));
  QVERIFY(contains("CF18-COORD-001","/nested/field"));
  QVERIFY(!contains("CF18-MISSING-001","/nested/field"));
  QVERIFY(contains("CF18-BOUNDS-001","/nested/bad_bounds"));
  QVERIFY(reader.cf_summary().contains("partial/advisory"));
  QVERIFY(reader.cf_summary().contains("not a conformance certification"));
}

void DropTargetsTest::cf18DiagnosticReportDialogStatesItsCoverageBoundary() {
  MainWindow window;
  const QString sample=qEnvironmentVariable("HDFSTUDIO_ZOOM_SAMPLE");
  QVERIFY2(QFileInfo(sample).isFile(),qPrintable(sample));
  QCOMPARE(window.read_file(sample),0);
  QVERIFY(QMetaObject::invokeMethod(&window,"show_cf_diagnostics",Qt::DirectConnection));
  auto *dialog=window.findChild<QDialog*>();
  QVERIFY(dialog);
  auto *report=dialog->findChild<QPlainTextEdit*>();
  QVERIFY(report);
  QVERIFY(report->toPlainText().contains("not a conformance certification"));
  QVERIFY(report->toPlainText().contains("Not checked:"));
  dialog->close();
}

void DropTargetsTest::bottomFileInfoPanelShowsCurrentFileMetadata() {
  MainWindow window;
  const QString sample=qEnvironmentVariable("HDFSTUDIO_ZOOM_SAMPLE");
  const QFileInfo expected(sample);
  QVERIFY(expected.isFile());
  QCOMPARE(window.read_file(sample),0);
  auto *text=window.findChild<QPlainTextEdit*>("fileInfoText");
  QVERIFY(text);
  QVERIFY(text->isReadOnly());
  const QString content=text->toPlainText();
  QVERIFY(content.contains(expected.fileName()));
  QVERIFY(content.contains(expected.absoluteFilePath()));
  QVERIFY(content.contains("NetCDF-4 enhanced"));
  QVERIFY(content.contains(QString::number(expected.size())));
  QCOMPARE(content.count(QLatin1Char('\n')),4);

  // The panel is a splitter page: dragging the handle must change its height.
  auto *splitter=window.findChild<QSplitter*>("mainSplitter");
  QVERIFY(splitter);
  QCOMPARE(splitter->orientation(),Qt::Vertical);
  QCOMPARE(splitter->count(),2);
  QVERIFY(!splitter->childrenCollapsible());
  QCOMPARE(splitter->widget(1)->findChild<QPlainTextEdit*>("fileInfoText"),text);

  window.resize(1000,800);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  QCoreApplication::processEvents();
  const int initial=splitter->sizes().at(1);
  QVERIFY(initial>0);

  QSplitterHandle *handle=splitter->handle(1);
  QVERIFY(handle);
  const QPoint start=handle->rect().center();
  QTest::mousePress(handle,Qt::LeftButton,Qt::NoModifier,start);
  const QPoint up=start+QPoint(0,-80);
  QMouseEvent grow(QEvent::MouseMove,up,handle->mapToGlobal(up),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
  QCoreApplication::sendEvent(handle,&grow);
  QTest::mouseRelease(handle,Qt::LeftButton,Qt::NoModifier,up);
  QCoreApplication::processEvents();
  const int grown=splitter->sizes().at(1);
  QVERIFY(grown>=initial+40);

  handle=splitter->handle(1);
  const QPoint start2=handle->rect().center();
  QTest::mousePress(handle,Qt::LeftButton,Qt::NoModifier,start2);
  const QPoint down=start2+QPoint(0,60);
  QMouseEvent shrink(QEvent::MouseMove,down,handle->mapToGlobal(down),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
  QCoreApplication::sendEvent(handle,&shrink);
  QTest::mouseRelease(handle,Qt::LeftButton,Qt::NoModifier,down);
  QCoreApplication::processEvents();
  QVERIFY(splitter->sizes().at(1)<=grown-30);
}

// The bottom panel exposes three tabs; tab 2 must describe the netCDF4 structure and tab 3 must
// state a CF verdict, while tab 1 keeps the metadata that was already verified above.
void DropTargetsTest::informationTabsExposeNetcdf4StructureAndCfVerdict() {
  MainWindow window;
  const QString sample=qEnvironmentVariable("HDFSTUDIO_ZOOM_SAMPLE");
  QVERIFY2(QFileInfo(sample).isFile(),qPrintable(sample));
  auto *tabs=window.findChild<QTabWidget*>("infoTabs");
  QVERIFY(tabs);
  QCOMPARE(tabs->count(),3);
  QCOMPARE(tabs->tabText(0),QString("File Info"));
  QCOMPARE(tabs->tabText(1),QString("By NetCDF4"));
  QCOMPARE(tabs->tabText(2),QString("CF Check"));
  QCOMPARE(window.read_file(sample),0);

  auto *netcdf4=window.findChild<QPlainTextEdit*>("netcdf4Text");
  auto *cf=window.findChild<QPlainTextEdit*>("cfCheckText");
  QVERIFY(netcdf4);QVERIFY(cf);
  QVERIFY(netcdf4->isReadOnly());
  QVERIFY(cf->isReadOnly());
  tabs->setCurrentIndex(1);
  QTRY_VERIFY_WITH_TIMEOUT(netcdf4->toPlainText().contains("/latitude: south to north"),20000);
  QVERIFY(netcdf4->toPlainText().contains("/longitude: west to east"));
  QVERIFY(netcdf4->toPlainText().contains("PRECT float"));
  tabs->setCurrentIndex(2);
  QTRY_VERIFY_WITH_TIMEOUT(cf->toPlainText().contains("CF-1.8 result: FAIL"),60000);
  auto *tree=window.findChild<FileTreeWidget*>();
  const auto items=tree->findItems("time",Qt::MatchExactly|Qt::MatchRecursive);
  QCOMPARE(items.size(),1);
  tree->setCurrentItem(items.first());
  QVERIFY(QMetaObject::invokeMethod(&window,"show_time_values"));
  auto *times=window.findChild<QPlainTextEdit*>("timeValuesText");
  QVERIFY(times);
  QTRY_VERIFY_WITH_TIMEOUT(times->toPlainText().contains("20241001 00:00:00"),20000);
  QVERIFY(times->toPlainText().contains("calendar=noleap"));
}

void DropTargetsTest::startupSplashIsPlainVisibleWidgetNotQSplashScreen() {
  QWidget *splash=create_startup_splash();
  QVERIFY(splash);
  QCOMPARE(splash->objectName(),QString("startupSplash"));
  // QSplashScreen::event() waits up to 1000 ms in waitForWidgetMapped() on Show, which is what
  // hid the waiting box on a cold start here. Keep it a plain widget.
  QVERIFY(!qobject_cast<QSplashScreen*>(splash));
  QVERIFY(splash->windowFlags().testFlag(Qt::WindowStaysOnTopHint));
  QVERIFY(splash->testAttribute(Qt::WA_ShowWithoutActivating));
  const auto labels=splash->findChildren<QLabel*>();
  QCOMPARE(labels.size(),2);
  QVERIFY(labels.at(0)->text().contains("HDFStudio"));
  QCOMPARE(labels.at(1)->text(),QString("Unleash Your NetCDF Power."));
  delete splash;
}

// Oracle values come from netCDF4.num2date on the same read-only samples:
// tmp2m ... 1999010100: time = seconds since 1970-01-01, standard calendar, 480 steps of 6 h.
void DropTargetsTest::timeGridShowsDecodedDatesBesideRawValues() {
  const QString sample=qEnvironmentVariable("HDFSTUDIO_FORECAST_ANCHOR");
  QVERIFY2(QFileInfo(sample).isFile(),qPrintable(sample));
  MainWindow window;
  QCOMPARE(window.read_file(sample),0);
  auto *tree=window.findChild<FileTreeWidget*>();
  auto open_grid=[&](const QString &name) -> QAbstractItemModel* {
    const auto items=tree->findItems(name,Qt::MatchExactly|Qt::MatchRecursive);
    if(items.size()!=1) return nullptr;
    tree->setCurrentItem(items.first());
    emit tree->itemDoubleClicked(items.first(),0);
    const auto tables=window.findChildren<QTableView*>("gridTable");
    return tables.isEmpty() ? nullptr : tables.last()->model();
  };

  QAbstractItemModel *time=open_grid("time");
  QVERIFY(time);
  QCOMPARE(time->rowCount(),480);
  QCOMPARE(time->data(time->index(0,0)).toString(),QString("915170400"));
  QTRY_COMPARE_WITH_TIMEOUT(time->columnCount(),3,20000);
  QCOMPARE(time->headerData(0,Qt::Horizontal).toString(),QString("Value"));
  QCOMPARE(time->headerData(2,Qt::Horizontal).toString(),QString("yyyymmdd hh:mm:ss"));
  QVERIFY(time->headerData(2,Qt::Horizontal,Qt::ToolTipRole).toString().contains("seconds since 1970-01-01"));
  QCOMPARE(time->data(time->index(0,1)).toString(),QString("19990101"));
  QCOMPARE(time->data(time->index(0,2)).toString(),QString("19990101 06:00:00"));
  QCOMPARE(time->data(time->index(3,2)).toString(),QString("19990102 00:00:00"));
  QCOMPARE(time->data(time->index(479,2)).toString(),QString("19990501 00:00:00"));

  // A plain coordinate without "since" units keeps its single numeric column.
  QAbstractItemModel *latitude=open_grid("latitude");
  QVERIFY(latitude && latitude!=time);
  QTest::qWait(3000);
  QCOMPARE(latitude->columnCount(),1);

  // Full-table dumps for the independent Python reconciliation (logs/S4-UI-TimeGrid/).
  const QDir logs(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../../../logs/S4-UI-TimeGrid"));
  QVERIFY(QDir().mkpath(logs.absolutePath()));
  const QStringList samples{sample,qEnvironmentVariable("HDFSTUDIO_FORECAST_TARGET")};
  for(const QString &path : samples) {
    QVERIFY2(QFileInfo(path).isFile(),qPrintable(path));
    const QString name=QFileInfo(path).fileName();
    MainWindow dump_window;
    QCOMPARE(dump_window.read_file(path),0);
    auto *dump_tree=dump_window.findChild<FileTreeWidget*>();
    const auto items=dump_tree->findItems("time",Qt::MatchExactly|Qt::MatchRecursive);
    QCOMPARE(items.size(),1);
    dump_tree->setCurrentItem(items.first());
    emit dump_tree->itemDoubleClicked(items.first(),0);
    QAbstractItemModel *model=dump_window.findChild<QTableView*>("gridTable")->model();
    QTRY_COMPARE_WITH_TIMEOUT(model->columnCount(),3,20000);
    QFile out(logs.absoluteFilePath("grid-"+name+".tsv"));
    QVERIFY(out.open(QIODevice::WriteOnly|QIODevice::Truncate));
    for(int row=0;row<model->rowCount();row++)
      out.write(QString("%1\t%2\t%3\n").arg(model->data(model->index(row,0)).toString(),
          model->data(model->index(row,1)).toString(),model->data(model->index(row,2)).toString()).toUtf8());
  }
}

// Two copies of the business sample are opened with a grid and an image view each. Closing one
// file must remove only its tree root and views, leave the other file working, and release the
// file on disk: on Windows QFile::remove() fails while any handle to the file is still open.
void DropTargetsTest::closeFileRemovesItsViewsAndReleasesTheFile() {
  const QString sample=qEnvironmentVariable("HDFSTUDIO_ZOOM_SAMPLE");
  QVERIFY2(QFileInfo(sample).isFile(),qPrintable(sample));
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString first=dir.filePath("first_copy.nc"),second=dir.filePath("second_copy.nc");
  QVERIFY(QFile::copy(sample,first));
  QVERIFY(QFile::copy(sample,second));

  MainWindow window;
  auto *tree=window.findChild<FileTreeWidget*>();
  auto *mdi=window.findChild<QMdiArea*>();
  auto *close_file=window.findChild<QAction*>("actionCloseFile");
  auto *close_all=window.findChild<QAction*>("actionCloseAllFiles");
  QVERIFY(tree);QVERIFY(mdi);QVERIFY(close_file);QVERIFY(close_all);
  QVERIFY(!close_file->isEnabled());
  QVERIFY(!close_all->isEnabled());
  QVERIFY(close_file->shortcuts().contains(QKeySequence("Ctrl+W")));
  QVERIFY(close_file->shortcuts().contains(QKeySequence("Ctrl+F4")));

  QCOMPARE(window.read_file(first),0);
  QCOMPARE(window.read_file(second),0);
  QCOMPARE(tree->topLevelItemCount(),2);
  QVERIFY(close_file->isEnabled());

  auto child_named=[](QTreeWidgetItem *root,const QString &name) -> QTreeWidgetItem* {
    for(int i=0;i<root->childCount();i++)
      if(root->child(i)->text(0)==name) return root->child(i);
    return nullptr;
  };
  QTreeWidgetItem *first_root=tree->topLevelItem(0),*second_root=tree->topLevelItem(1);
  QTreeWidgetItem *first_prect=child_named(first_root,"PRECT"),*first_time=child_named(first_root,"time");
  QTreeWidgetItem *second_prect=child_named(second_root,"PRECT");
  QVERIFY(first_prect);QVERIFY(first_time);QVERIFY(second_prect);
  emit tree->itemDoubleClicked(first_prect,0);   // image view of the first file
  QCOMPARE(mdi->subWindowList().size(),1);
  tree->setCurrentItem(first_time);              // a real double-click selects the item first
  emit tree->itemDoubleClicked(first_time,0);    // grid of the first file
  QCOMPARE(mdi->subWindowList().size(),2);
  emit tree->itemDoubleClicked(second_prect,0);  // image view of the second file
  QCOMPARE(mdi->subWindowList().size(),3);

  // Close the first file through its time node (any node below a root selects the file).
  tree->setCurrentItem(first_time);
  close_file->trigger();
  QCOMPARE(tree->topLevelItemCount(),1);
  QCOMPARE(mdi->subWindowList().size(),1);
  auto *info=window.findChild<QPlainTextEdit*>("fileInfoText");
  QVERIFY(info);
  QVERIFY2(info->toPlainText().contains("second_copy.nc"),qPrintable(info->toPlainText()));
  QVERIFY2(QFile::remove(first),"first file is still held open after Close File");
  QVERIFY(QFileInfo(second).exists());

  // Last file: panels return to their empty state and both actions are disabled again.
  close_file->trigger();
  QCOMPARE(tree->topLevelItemCount(),0);
  QCOMPARE(mdi->subWindowList().size(),0);
  QCOMPARE(info->toPlainText(),QString("No file opened."));
  QVERIFY(!close_file->isEnabled());
  QVERIFY(!close_all->isEnabled());
  QVERIFY2(QFile::remove(second),"second file is still held open after Close File");

  // Close All Files after reopening: nothing is left behind.
  QVERIFY(QFile::copy(sample,first));
  QVERIFY(QFile::copy(sample,second));
  QCOMPARE(window.read_file(first),0);
  QCOMPARE(window.read_file(second),0);
  emit tree->itemDoubleClicked(child_named(tree->topLevelItem(0),"PRECT"),0);
  close_all->trigger();
  QCOMPARE(tree->topLevelItemCount(),0);
  QCOMPARE(mdi->subWindowList().size(),0);
  QVERIFY(QFile::remove(first));
  QVERIFY(QFile::remove(second));
}

// NetCDF-3 fixture shaped like the business forecast file that failed to open in 0.10.26:
// unlimited time, lev, lat, lon, a 4-D float field, an integer plane and text/numeric attributes.
// ttr[t][l][y][x] = t*10000 + l*1000 + y*10 + x so every cell identifies its own indices.
static void write_classic_fixture(const QString &file, int mode) {
  int ncid=-1,timeDim=-1,levDim=-1,latDim=-1,lonDim=-1;
  int timeVar=-1,levVar=-1,latVar=-1,lonVar=-1,ttrVar=-1,countVar=-1;
  const QByteArray path=nc_path_bytes(file);
  QCOMPARE(nc_create(path.constData(),mode,&ncid),NC_NOERR);
  QCOMPARE(nc_def_dim(ncid,"time",NC_UNLIMITED,&timeDim),NC_NOERR);
  QCOMPARE(nc_def_dim(ncid,"lev",4,&levDim),NC_NOERR);
  QCOMPARE(nc_def_dim(ncid,"lat",5,&latDim),NC_NOERR);
  QCOMPARE(nc_def_dim(ncid,"lon",6,&lonDim),NC_NOERR);
  QCOMPARE(nc_def_var(ncid,"time",NC_DOUBLE,1,&timeDim,&timeVar),NC_NOERR);
  QCOMPARE(nc_def_var(ncid,"lev",NC_FLOAT,1,&levDim,&levVar),NC_NOERR);
  QCOMPARE(nc_def_var(ncid,"lat",NC_FLOAT,1,&latDim,&latVar),NC_NOERR);
  QCOMPARE(nc_def_var(ncid,"lon",NC_FLOAT,1,&lonDim,&lonVar),NC_NOERR);
  const int fieldDims[4]={timeDim,levDim,latDim,lonDim};
  QCOMPARE(nc_def_var(ncid,"ttr",NC_FLOAT,4,fieldDims,&ttrVar),NC_NOERR);
  const int planeDims[2]={latDim,lonDim};
  QCOMPARE(nc_def_var(ncid,"count",NC_SHORT,2,planeDims,&countVar),NC_NOERR);
  const char description[]="Fengshun AI model ttr Product";
  QCOMPARE(nc_put_att_text(ncid,NC_GLOBAL,"description",strlen(description),description),NC_NOERR);
  const char timeUnits[]="days since 2026-11-02 12:00:00";
  QCOMPARE(nc_put_att_text(ncid,timeVar,"units",strlen(timeUnits),timeUnits),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,latVar,"units",13,"degrees_north"),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,lonVar,"units",12,"degrees_east"),NC_NOERR);
  QCOMPARE(nc_put_att_text(ncid,ttrVar,"units",5,"W/m^2"),NC_NOERR);
  const float validRange[2]={-5.0f,5.0f};
  QCOMPARE(nc_put_att_float(ncid,ttrVar,"valid_range",NC_FLOAT,2,validRange),NC_NOERR);
  const float numberForms[5]={0.1f,50.0f,1e-5f,9.96921e36f,273.15f};
  QCOMPARE(nc_put_att_float(ncid,ttrVar,"number_forms",NC_FLOAT,5,numberForms),NC_NOERR);
  QCOMPARE(nc_enddef(ncid),NC_NOERR);
  const double times[3]={0.0,0.25,0.5};
  const float levs[4]={1,2,3,4},lats[5]={-20,-10,0,10,20},lons[6]={100,110,120,130,140,150};
  for(size_t record=0;record<3;++record) {
    QCOMPARE(nc_put_var1_double(ncid,timeVar,&record,&times[record]),NC_NOERR);
  }
  QCOMPARE(nc_put_var_float(ncid,levVar,levs),NC_NOERR);
  QCOMPARE(nc_put_var_float(ncid,latVar,lats),NC_NOERR);
  QCOMPARE(nc_put_var_float(ncid,lonVar,lons),NC_NOERR);
  std::vector<float> ttr(3*4*5*6);
  for(int t=0;t<3;++t) for(int l=0;l<4;++l) for(int y=0;y<5;++y) for(int x=0;x<6;++x)
    ttr[((size_t(t)*4+l)*5+y)*6+x]=float(t*10000+l*1000+y*10+x);
  const size_t start[4]={0,0,0,0},count[4]={3,4,5,6};
  QCOMPARE(nc_put_vara_float(ncid,ttrVar,start,count,ttr.data()),NC_NOERR);
  short counts[30];
  for(int i=0;i<30;++i) counts[i]=short(i*3);
  QCOMPARE(nc_put_var_short(ncid,countVar,counts),NC_NOERR);
  QCOMPARE(nc_close(ncid),NC_NOERR);
}

// A modal warning (the 0.10.26 behaviour for these files) would hang the test run, so any modal
// widget is closed by a watchdog and the missing tree then fails the assertions instead.
// A QInputDialog is left alone: when no warning appears the watchdog is still pending later in the
// test and used to close the Go to Layer dialog before the test answered it (intermittent failure).
void DropTargetsTest::analysisGridModesLayersAttributesAndDatesReuseControls() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString file = dir.filePath("grid-analysis.nc");
  write_classic_fixture(file, NC_CLOBBER);
  MainWindow window;
  window.resize(1200, 800);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  QCOMPARE(window.read_file(file), 0);
  auto *tree = window.findChild<FileTreeWidget*>();
  auto *mdi = window.findChild<QMdiArea*>();
  QVERIFY(tree); QVERIFY(mdi);
  auto open = [&](const QString &name, const char *slot) -> AnalysisTableView* {
    const auto items = tree->findItems(name, Qt::MatchExactly | Qt::MatchRecursive);
    if(items.size() != 1) return nullptr;
    tree->setCurrentItem(items.first());
    if(!QMetaObject::invokeMethod(tree, slot)) return nullptr;
    return mdi->subWindowList().last()->widget()->findChild<AnalysisTableView*>("gridTable");
  };
  for(const char *slot : {"add_grid", "add_stored_grid", "add_both_grids"}) {
    auto *table = open("ttr", slot);
    QVERIFY(table);
    QTRY_VERIFY(table->columnWidth(0) != 80);
    QCOMPARE(table->selectionMode(), QAbstractItemView::ExtendedSelection);
    auto *sub = mdi->subWindowList().last()->widget();
    QVERIFY(sub->findChild<QToolBar*>("gridAnalysisToolbar"));
    QVERIFY(sub->findChild<QLabel*>("gridSelectionSummary"));
    QVERIFY(table->autoFitAction()->isChecked());
    table->selectionModel()->select(QItemSelection(table->model()->index(0, 0), table->model()->index(1, 1)),
                                     QItemSelectionModel::ClearAndSelect);
    QCOMPARE(table->selectionSummary(), QString("Selected: 4 cells"));
    const auto layers = sub->findChildren<QComboBox*>();
    QCOMPARE(layers.size(), 2);
    layers.at(0)->setCurrentIndex(2);
    layers.at(1)->setCurrentIndex(3);
    QTRY_COMPARE(table->model()->data(table->model()->index(0, 0)).toString(), QString("23000"));
    QTRY_VERIFY(table->columnWidth(0) >= table->fontMetrics().horizontalAdvance("23000") + 4);
    QCOMPARE(table->model()->data(table->model()->index(0, 0), Qt::ToolTipRole).toString(), QString("23000"));
    if(QByteArray(slot) == "add_both_grids") QCOMPARE(table->selectionText(), QString("23000\t23000\n23010\t23010"));
    else QCOMPARE(table->selectionText(), QString("23000\t23001\n23010\t23011"));
    mdi->closeAllSubWindows();
  }
  auto *attribute = open("valid_range", "add_grid");
  QVERIFY(attribute);
  QTRY_VERIFY(attribute->columnWidth(0) < 80);
  attribute->selectAll();
  QCOMPARE(attribute->selectionText(), QString("-5\n5"));
  mdi->closeAllSubWindows();
  auto *dates = open("time", "add_both_grids");
  QVERIFY(dates);
  QAbstractItemModelTester tester(dates->model(), QAbstractItemModelTester::FailureReportingMode::QtTest);
  QTRY_COMPARE_WITH_TIMEOUT(dates->model()->columnCount(), 4, 20000);
  QTRY_VERIFY(dates->columnWidth(3) >= dates->fontMetrics().horizontalAdvance("20261102 12:00:00") + 4);
  QCOMPARE(dates->model()->headerData(2, Qt::Horizontal).toString(), QString("yyyymmdd"));
  dates->selectAll();
  QVERIFY(dates->selectionText().startsWith("0\t0\t20261102\t20261102 12:00:00"));
  const auto *sub = mdi->subWindowList().last();
  QVERIFY(sub->width() <= mdi->viewport()->width());
  const QString evidence = qEnvironmentVariable("HDFSTUDIO_GRID_EVIDENCE");
  if(!evidence.isEmpty()) {
    QVERIFY(QDir().mkpath(evidence));
    QTest::qWait(50);
    QVERIFY(window.grab().save(QDir(evidence).filePath("dates-grid.png")));
  }
  window.close_all_files();
}

static void close_modal_dialogs_later() {
  QTimer::singleShot(1500,[]{
    QWidget *modal=QApplication::activeModalWidget();
    if(modal && !qobject_cast<QInputDialog*>(modal)) modal->close();
  });
}

void DropTargetsTest::classicNetcdfOpensWithTreeGridImagePropertiesAndExport() {
  struct Case { int mode; const char *kind; };
  const Case cases[]={{NC_CLOBBER,"NetCDF CDF-1"},
                      {NC_CLOBBER|NC_64BIT_OFFSET,"NetCDF CDF-2"},
                      {NC_CLOBBER|NC_64BIT_DATA,"NetCDF CDF-5"}};
  for(const Case &testCase:cases) {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString file=dir.filePath("classic.nc");
    write_classic_fixture(file,testCase.mode);
    QCOMPARE(probe_data_format(file).kind,QString(testCase.kind));

    MainWindow window;
    auto *tree=window.findChild<FileTreeWidget*>();
    auto *mdi=window.findChild<QMdiArea*>();
    QVERIFY(tree);QVERIFY(mdi);
    close_modal_dialogs_later();
    QCOMPARE(window.read_file(file),0);
    QCOMPARE(tree->topLevelItemCount(),1);
    QTreeWidgetItem *root=tree->topLevelItem(0);
    auto child_named=[](QTreeWidgetItem *parent,const QString &name) -> QTreeWidgetItem* {
      for(int i=0;i<parent->childCount();i++)
        if(parent->child(i)->text(0)==name) return parent->child(i);
      return nullptr;
    };
    for(const char *name:{"time","lev","lat","lon","ttr","count","description"})
      QVERIFY2(child_named(root,name),name);
    QTreeWidgetItem *ttr=child_named(root,"ttr");
    QVERIFY(child_named(ttr,"units"));
    QVERIFY(child_named(ttr,"valid_range"));

    // Image view: the first plane is y*10+x, then the second plane is selected through the axes.
    tree->setCurrentItem(ttr);
    emit tree->itemDoubleClicked(ttr,0);
    QCOMPARE(mdi->subWindowList().size(),1);
    QWidget *raster=mdi->subWindowList().last()->widget();
    QTRY_COMPARE_WITH_TIMEOUT(raster->property("reader").toString(),QString("netCDF-C"),5000);
    QCOMPARE(raster->property("minimum").toDouble(),0.0);
    QCOMPARE(raster->property("maximum").toDouble(),45.0);
    const auto axes=raster->findChildren<QSpinBox*>();
    QCOMPARE(axes.size(),2);
    axes.at(0)->setValue(2);
    axes.at(1)->setValue(3);
    QTRY_COMPARE_WITH_TIMEOUT(raster->property("minimum").toDouble(),23000.0,5000);
    QCOMPARE(raster->property("maximum").toDouble(),23045.0);
    mdi->closeAllSubWindows();

    // Grid: only the displayed plane is held in memory and follows the layer selectors.
    tree->setCurrentItem(ttr);
    QVERIFY(QMetaObject::invokeMethod(tree,"add_grid"));
    QCOMPARE(mdi->subWindowList().size(),1);
    auto *table=mdi->subWindowList().last()->widget()->findChild<QTableView*>("gridTable");
    QVERIFY(table);
    QAbstractItemModel *model=table->model();
    QCOMPARE(model->rowCount(),5);
    QCOMPARE(model->columnCount(),6);
    QCOMPARE(model->data(model->index(0,0)).toString(),QString("0"));
    QCOMPARE(model->data(model->index(4,5)).toString(),QString("45"));
    const auto combos=mdi->subWindowList().last()->widget()->findChildren<QComboBox*>();
    QCOMPARE(combos.size(),2);
    combos.at(0)->setCurrentIndex(2);
    combos.at(1)->setCurrentIndex(3);
    QCOMPARE(model->data(model->index(0,0)).toString(),QString("23000"));
    QCOMPARE(model->data(model->index(3,4)).toString(),QString("23034"));
    mdi->closeAllSubWindows();

    // Numeric attribute and integer variable through the same grid.
    tree->setCurrentItem(child_named(ttr,"valid_range"));
    QVERIFY(QMetaObject::invokeMethod(tree,"add_grid"));
    QCOMPARE(mdi->subWindowList().size(),1);
    QAbstractItemModel *attributeModel=mdi->subWindowList().last()->widget()->findChild<QTableView*>("gridTable")->model();
    QCOMPARE(attributeModel->rowCount(),2);
    QCOMPARE(attributeModel->data(attributeModel->index(0,0)).toString(),QString("-5"));
    QCOMPARE(attributeModel->data(attributeModel->index(1,0)).toString(),QString("5"));
    mdi->closeAllSubWindows();
    tree->setCurrentItem(child_named(root,"count"));
    QVERIFY(QMetaObject::invokeMethod(tree,"add_grid"));
    QAbstractItemModel *countModel=mdi->subWindowList().last()->widget()->findChild<QTableView*>("gridTable")->model();
    QCOMPARE(countModel->data(countModel->index(4,5)).toString(),QString("87"));
    mdi->closeAllSubWindows();

    // Properties and export read the same file through netCDF-C.
    auto summary=HdfProperties::inspect(file,"/ttr");
    QVERIFY(summary.valid);
    QCOMPARE(summary.objectType,QString("Dataset"));
    QCOMPARE(summary.shape,QString("[3 x 4 x 5 x 6]"));
    QCOMPARE(summary.dataType,QString("32-bit floating point (float32)"));
    QCOMPARE(summary.reader,QString("netCDF-C"));
    QCOMPARE(summary.dimensions,QString("ttr(time, lev, lat, lon)"));
    QCOMPARE(summary.unlimitedDimensions,QString("time"));
    // attribute values as Python netCDF4 prints them: arrays expanded, float32 in shortest form
    QVERIFY2(summary.attributes.contains("valid_range | float32 | [2] | [-5.0, 5.0]"),
             qPrintable(summary.attributes.join('\n')));
    QVERIFY(HdfProperties::describe(summary).contains("Dimensions: ttr(time, lev, lat, lon)"));
    QCOMPARE(summary.elements,quint64(360));
    QCOMPARE(summary.bytes,quint64(1440));
    QVERIFY(summary.attributes.join('\n').contains("units | "));
    QVERIFY(summary.attributes.join('\n').contains("W/m^2"));
    auto global=HdfProperties::inspect(file,"/");
    QVERIFY(global.valid);
    QCOMPARE(global.objectType,QString("Group"));
    QCOMPARE(global.dimensions,QString("time(3), lev(4), lat(5), lon(6)"));
    QVERIFY(global.attributes.join('\n').contains("Fengshun AI model ttr Product"));

    QBuffer exported;
    QVERIFY(exported.open(QIODevice::WriteOnly));
    auto result=HdfTextExport::write(file,"/ttr",exported);
    QVERIFY2(result.ok,qPrintable(result.error));
    QCOMPARE(result.valuesWritten,quint64(360));
    const auto lines=exported.data().split('\n');
    QCOMPARE(lines.size(),61);
    QCOMPARE(lines.at(0),QByteArray("0,1,2,3,4,5"));
    QCOMPARE(lines.at(59),QByteArray("23040,23041,23042,23043,23044,23045"));

    if(testCase.mode==NC_CLOBBER) {
      // hdf_cli prints what the GUI functions return, for debugging a file without the GUI.
      const QString cli=QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("hdf_cli.exe");
      QVERIFY2(QFileInfo(cli).isFile(),qPrintable(cli));
      auto run_cli=[&cli](const QStringList &arguments) -> QString {
        QProcess process;
        process.start(cli,arguments);
        if(!process.waitForFinished(30000) || process.exitCode()!=0) return QString("exit %1").arg(process.exitCode());
        return QString::fromUtf8(process.readAllStandardOutput());
      };
      const QString props=run_cli({"props",file,"/ttr"});
      for(const QString &line:HdfProperties::describe(summary)) QVERIFY2(props.contains(line),qPrintable(line));
      const QString info=run_cli({"info",file});
      QVERIFY2(info.contains("root group (file format NETCDF3_CLASSIC):"),qPrintable(info));
      QVERIFY(info.contains("    description: Fengshun AI model ttr Product"));
      QVERIFY(info.contains("    dimensions(sizes): time(3), lev(4), lat(5), lon(6)"));
      QVERIFY(info.contains("variable float32 ttr(time, lev, lat, lon)"));
      QVERIFY(info.contains("    valid_range: [-5.0, 5.0]"));
      // numpy rules: positional for 1e-4 <= |x| < 1e16, otherwise scientific
      QVERIFY2(info.contains("    number_forms: [0.1, 50.0, 1e-05, 9.96921e+36, 273.15]"),qPrintable(info));
      QVERIFY(info.contains("current shape = (3, 4, 5, 6)"));
      QVERIFY(info.contains("unlimited dimensions: time"));
      QCOMPARE(run_cli({"export",file,"/lat"}).trimmed(),QString("-20,-10,0,10,20"));
      QVERIFY(run_cli({"probe",file}).contains("kind: NetCDF CDF-1"));

      // The time coordinate is decoded with the variable's own units, as for NetCDF-4 files.
      tree->setCurrentItem(child_named(root,"time"));
      emit tree->itemDoubleClicked(child_named(root,"time"),0);
      QAbstractItemModel *timeModel=mdi->subWindowList().last()->widget()->findChild<QTableView*>("gridTable")->model();
      QCOMPARE(timeModel->rowCount(),3);
      QTRY_COMPARE_WITH_TIMEOUT(timeModel->columnCount(),3,20000);
      QCOMPARE(timeModel->data(timeModel->index(0,2)).toString(),QString("20261102 12:00:00"));
      QCOMPARE(timeModel->data(timeModel->index(1,2)).toString(),QString("20261102 18:00:00"));
      mdi->closeAllSubWindows();
    }

    // Closing the file removes the tree and releases the file on disk.
    window.close_all_files();
    QCOMPARE(tree->topLevelItemCount(),0);
    QVERIFY2(QFile::remove(file),"classic file is still held open after Close All Files");
  }
}

// Directory and file names are built from code points so the test does not depend on the source
// file encoding or the shell code page.
void DropTargetsTest::classicNetcdfOpensFromChinesePath() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString folder=dir.filePath(QString::fromUtf16(u"\u4e2d\u6587\u76ee\u5f55"));
  QVERIFY(QDir().mkpath(folder));
  const QString file=folder+QString::fromUtf16(u"/\u51e4\u987a_\u6837\u4f8b.nc");
  write_classic_fixture(file,NC_CLOBBER);

  MainWindow window;
  auto *tree=window.findChild<FileTreeWidget*>();
  auto *mdi=window.findChild<QMdiArea*>();
  close_modal_dialogs_later();
  QCOMPARE(window.read_file(file),0);
  QCOMPARE(tree->topLevelItemCount(),1);
  QTreeWidgetItem *root=tree->topLevelItem(0);
  QTreeWidgetItem *ttr=nullptr;
  for(int i=0;i<root->childCount();i++) if(root->child(i)->text(0)=="ttr") ttr=root->child(i);
  QVERIFY(ttr);
  tree->setCurrentItem(ttr);
  QVERIFY(QMetaObject::invokeMethod(tree,"add_grid"));
  auto *table=mdi->subWindowList().last()->widget()->findChild<QTableView*>("gridTable");
  QVERIFY(table);
  QCOMPARE(table->model()->data(table->model()->index(4,5)).toString(),QString("45"));
  mdi->closeAllSubWindows();
  QCOMPARE(HdfProperties::inspect(file,"/ttr").shape,QString("[3 x 4 x 5 x 6]"));
  window.close_all_files();
}

// Old HDF Explorer handlers (v2007 ViewGrid2D.c / ViewImageSds2D.c): 32776 +1 clamped at the end,
// 32777 -1 clamped at 0, 32778 last, 32779 first.
void DropTargetsTest::layerStepTargetMatchesOldHdfExplorerButtons() {
  QCOMPARE(layer_step_target(0,3,LayerStep::Next),1);
  QCOMPARE(layer_step_target(2,3,LayerStep::Next),2);
  QCOMPARE(layer_step_target(1,3,LayerStep::Previous),0);
  QCOMPARE(layer_step_target(0,3,LayerStep::Previous),0);
  QCOMPARE(layer_step_target(1,3,LayerStep::Last),2);
  QCOMPARE(layer_step_target(2,3,LayerStep::First),0);
  QCOMPARE(layer_step_target(0,0,LayerStep::Next),0);
  QCOMPARE(layer_step_target(0,1,LayerStep::Next),0);
  QCOMPARE(layer_clamp(7,3),2);
  QCOMPARE(layer_clamp(-1,3),0);
}

// Interactive use simulated in-process: real clicks on the main toolbar buttons, choices in its
// combos, the Go to Layer dialog and switching between views; every step is checked against the
// fixture values ttr[t][l][y][x] = t*10000 + l*1000 + y*10 + x.
void DropTargetsTest::mainToolbarStepsLayersOfTheCurrentView() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString file=dir.filePath("layers.nc");
  write_classic_fixture(file,NC_CLOBBER);

  MainWindow window;
  window.resize(1200,800);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  auto *tree=window.findChild<FileTreeWidget*>();
  auto *mdi=window.findChild<QMdiArea*>();
  auto *bar=window.findChild<QToolBar*>("mainToolBar");
  QVERIFY(tree);QVERIFY(mdi);QVERIFY(bar);
  auto action=[&window](const char *name){ return window.findChild<QAction*>(name); };
  QAction *next=action("actionLayerNext"), *previous=action("actionLayerPrevious");
  QAction *last=action("actionLayerLast"), *first=action("actionLayerFirst"), *go=action("actionLayerGoTo");
  QVERIFY(next);QVERIFY(previous);QVERIFY(last);QVERIFY(first);QVERIFY(go);
  auto *axisCombo=window.findChild<QComboBox*>("layerAxisCombo");
  auto *indexCombo=window.findChild<QComboBox*>("layerIndexCombo");
  auto *label=window.findChild<QLabel*>("layerLabel");
  QVERIFY(axisCombo);QVERIFY(indexCombo);QVERIFY(label);

  // Buttons sit right after Open in the old order: next, previous, last, first; all in the Layer menu.
  const QList<QAction*> barActions=bar->actions();
  QCOMPARE(barActions.at(0)->text(),QString("&Open..."));
  QVERIFY(barActions.at(1)->isSeparator());
  QCOMPARE(barActions.indexOf(next),2);
  QCOMPARE(barActions.indexOf(previous),3);
  QCOMPARE(barActions.indexOf(last),4);
  QCOMPARE(barActions.indexOf(first),5);
  QMenu *layerMenu=nullptr;
  for(QAction *menuAction:window.menuBar()->actions())
    if(menuAction->text()=="&Layer") layerMenu=menuAction->menu();
  QVERIFY(layerMenu);
  for(QAction *a:{next,previous,last,first,go}) QVERIFY(layerMenu->actions().contains(a));

  // No view yet: nothing to step.
  QVERIFY(!next->isEnabled());
  QCOMPARE(label->text(),QString("No layers"));

  close_modal_dialogs_later();
  QCOMPARE(window.read_file(file),0);
  QTreeWidgetItem *root=tree->topLevelItem(0);
  auto child_named=[root](const QString &name) -> QTreeWidgetItem* {
    for(int i=0;i<root->childCount();i++) if(root->child(i)->text(0)==name) return root->child(i);
    return nullptr;
  };
  tree->setCurrentItem(child_named("ttr"));
  QVERIFY(QMetaObject::invokeMethod(tree,"add_grid"));
  QMdiSubWindow *gridSub=mdi->subWindowList().last();
  QWidget *grid=gridSub->widget();
  QAbstractItemModel *model=grid->findChild<QTableView*>("gridTable")->model();
  auto cell=[model](int row,int column){ return model->data(model->index(row,column)).toString(); };
  const auto gridCombos=grid->findChildren<QComboBox*>();
  QCOMPARE(gridCombos.size(),2);

  QTRY_COMPARE(label->text(),QString("Layer 1 of 3"));
  QCOMPARE(axisCombo->count(),2);
  QCOMPARE(axisCombo->itemText(0),QString("time (3)"));
  QCOMPARE(axisCombo->itemText(1),QString("lev (4)"));
  QCOMPARE(indexCombo->count(),3);
  QVERIFY(next->isEnabled());QVERIFY(last->isEnabled());
  QVERIFY(!previous->isEnabled());QVERIFY(!first->isEnabled());

  auto click=[bar](QAction *a){ QTest::mouseClick(bar->widgetForAction(a),Qt::LeftButton); };
  click(next);                                   // time 1
  QCOMPARE(cell(0,0),QString("10000"));
  QCOMPARE(gridCombos.at(0)->currentIndex(),1);  // the grid's own selector follows
  QCOMPARE(label->text(),QString("Layer 2 of 3"));
  click(last);                                   // time 2
  QCOMPARE(cell(4,5),QString("20045"));
  QVERIFY(!next->isEnabled());
  click(previous);                               // time 1
  QCOMPARE(cell(0,0),QString("10000"));
  click(first);                                  // time 0
  QCOMPARE(cell(0,0),QString("0"));
  QCOMPARE(label->text(),QString("Layer 1 of 3"));

  // Choose the lev axis and pick a layer from the toolbar list.
  axisCombo->setCurrentIndex(1); emit axisCombo->activated(1);
  QCOMPARE(indexCombo->count(),4);
  QCOMPARE(label->text(),QString("Layer 1 of 4"));
  indexCombo->setCurrentIndex(3); emit indexCombo->activated(3);
  QCOMPARE(cell(0,0),QString("3000"));
  QCOMPARE(gridCombos.at(1)->currentIndex(),3);
  QCOMPARE(label->text(),QString("Layer 4 of 4"));

  // The grid's own selector changes the toolbar too (time axis selected again).
  axisCombo->setCurrentIndex(0); emit axisCombo->activated(0);
  gridCombos.at(0)->setCurrentIndex(2);
  QCOMPARE(indexCombo->currentIndex(),2);
  QCOMPARE(label->text(),QString("Layer 3 of 3"));
  QCOMPARE(cell(0,0),QString("23000"));

  // Go to Layer...: the dialog is answered like a user typing 2 and pressing OK.
  QTimer::singleShot(300,[]{
    if(auto *input=qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) { input->setIntValue(2); input->accept(); }
  });
  go->trigger();
  QCOMPARE(cell(0,0),QString("13000"));
  QCOMPARE(label->text(),QString("Layer 2 of 3"));

  // An image view of the same variable has its own position; the toolbar follows the current view.
  emit tree->itemDoubleClicked(child_named("ttr"),0);
  QMdiSubWindow *imageSub=mdi->subWindowList().last();
  QWidget *image=imageSub->widget();
  QTRY_COMPARE(mdi->currentSubWindow(),imageSub);
  QTRY_COMPARE(label->text(),QString("Layer 1 of 3"));
  QCOMPARE(image->property("minimum").toDouble(),0.0);
  click(next);
  QTRY_COMPARE(image->property("minimum").toDouble(),10000.0);
  QCOMPARE(image->property("maximum").toDouble(),10045.0);
  QCOMPARE(image->findChildren<QSpinBox*>().at(0)->value(),1);
  axisCombo->setCurrentIndex(1); emit axisCombo->activated(1);
  click(last);                                   // lev 3 of the image
  QTRY_COMPARE(image->property("minimum").toDouble(),13000.0);

  // Back to the grid: its own layer (time 2 of 3, axis time) comes back on the toolbar.
  mdi->setActiveSubWindow(gridSub);
  QTRY_COMPARE(label->text(),QString("Layer 2 of 3"));
  QCOMPARE(axisCombo->currentIndex(),0);
  QCOMPARE(cell(0,0),QString("13000"));

  // A 2-D variable has no layers; closing every view leaves nothing to step.
  tree->setCurrentItem(child_named("count"));
  QVERIFY(QMetaObject::invokeMethod(tree,"add_grid"));
  QTRY_COMPARE(label->text(),QString("No layers"));
  QVERIFY(!next->isEnabled());QVERIFY(!axisCombo->isEnabled());
  mdi->closeAllSubWindows();
  QTRY_COMPARE(label->text(),QString("No layers"));
  window.close_all_files();
}

// Properties dialog: selectable text, Copy All puts the whole report (file, object, CF lines) on the
// clipboard. Window menu: Cascade/Tile/Close All Views and one numbered entry per view that activates it.
void DropTargetsTest::propertiesDialogCopiesReportAndWindowMenuListsViews() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString file=dir.filePath("props.nc");
  write_classic_fixture(file,NC_CLOBBER);

  MainWindow window;
  window.resize(1200,800);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  auto *tree=window.findChild<FileTreeWidget*>();
  auto *mdi=window.findChild<QMdiArea*>();
  QVERIFY(tree);QVERIFY(mdi);
  close_modal_dialogs_later();
  QCOMPARE(window.read_file(file),0);
  QTreeWidgetItem *root=tree->topLevelItem(0);
  auto child_named=[root](const QString &name) -> QTreeWidgetItem* {
    for(int i=0;i<root->childCount();i++) if(root->child(i)->text(0)==name) return root->child(i);
    return nullptr;
  };

  // --- Properties ---
  tree->setCurrentItem(child_named("ttr"));
  QVERIFY(QMetaObject::invokeMethod(tree,"show_item_properties"));
  auto *dialog=window.findChild<QDialog*>("propertiesDialog");
  QVERIFY(dialog);
  QTRY_VERIFY(dialog->isVisible());
  QVERIFY(!dialog->isModal());
  auto *text=dialog->findChild<QPlainTextEdit*>("propertiesText");
  auto *copy=dialog->findChild<QPushButton*>("propertiesCopyAll");
  auto *copied=dialog->findChild<QLabel*>("propertiesCopied");
  QVERIFY(text);QVERIFY(copy);QVERIFY(copied);
  QVERIFY(text->isReadOnly());
  QVERIFY(text->textInteractionFlags() & Qt::TextSelectableByMouse);
  const QStringList lines=text->toPlainText().split('\n');
  QVERIFY(lines.at(0).startsWith("HDFStudio "));
  QCOMPARE(lines.at(1),QString("File: ")+QDir::toNativeSeparators(file));
  QCOMPARE(lines.at(2),QString("Object: /ttr"));
  QVERIFY(lines.contains("Name: ttr"));
  QVERIFY(lines.contains("Dimensions: ttr(time, lev, lat, lon)"));
  // same detail lines as hdf_cli props
  for(const QString &line:HdfProperties::describe(HdfProperties::inspect(file,"/ttr")))
    QVERIFY2(lines.contains(line),qPrintable(line));
  QApplication::clipboard()->clear();
  QTest::mouseClick(copy,Qt::LeftButton);
  QCOMPARE(QApplication::clipboard()->text(),text->toPlainText());
  QCOMPARE(copied->text(),QString("Copied %1 lines to the clipboard.").arg(lines.size()));
  dialog->close();
  QTRY_VERIFY(!window.findChild<QDialog*>("propertiesDialog"));

  // file root: Object is "/"
  QCOMPARE(tree->item_properties_text(root).split('\n').at(2),QString("Object: /"));

  // --- Window menu ---
  QMenu *windowMenu=nullptr;
  for(QAction *menuAction:window.menuBar()->actions())
    if(menuAction->text()=="&Window") windowMenu=menuAction->menu();
  QVERIFY(windowMenu);
  auto refresh=[&window]{ QVERIFY(QMetaObject::invokeMethod(&window,"update_window_menu")); };
  auto *cascade=window.findChild<QAction*>("actionCascade");
  QVERIFY(cascade);
  refresh();
  QVERIFY(!cascade->isEnabled());
  QCOMPARE(windowMenu->actions().size(),4);   // Cascade, Tile, Close All Views, Compare & Metrics
  QVERIFY(window.findChild<QAction*>("actionCompareMetrics")->isEnabled());

  for(const char *name:{"ttr","count","ttr"}) {
    tree->setCurrentItem(child_named(name));
    QVERIFY(QMetaObject::invokeMethod(tree,"add_grid"));
  }
  const QList<QMdiSubWindow*> views=mdi->subWindowList(QMdiArea::CreationOrder);
  QCOMPARE(views.size(),3);
  refresh();
  QVERIFY(cascade->isEnabled());
  QCOMPARE(windowMenu->actions().at(0),cascade);
  QVERIFY(windowMenu->actions().at(4)->isSeparator());
  for(int i=0;i<3;i++) {
    QAction *entry=window.findChild<QAction*>(QString("actionWindow%1").arg(i+1));
    QVERIFY(entry);
    QCOMPARE(entry->text(),QString("&%1 %2").arg(i+1).arg(views.at(i)->windowTitle()));
    QCOMPARE(entry->isChecked(),views.at(i)==mdi->currentSubWindow());
  }
  window.findChild<QAction*>("actionWindow1")->trigger();
  QTRY_COMPARE(mdi->currentSubWindow(),views.at(0));
  refresh();
  QVERIFY(window.findChild<QAction*>("actionWindow1")->isChecked());

  // Cascade: creation order, same 2/3 size for every view, offset down-right; the active view stays in front
  views.at(1)->showMinimized();
  mdi->setActiveSubWindow(views.at(0));
  cascade->trigger();
  const QRect area=mdi->viewport()->rect();
  for(int i=0;i<3;i++) {
    QVERIFY(!views.at(i)->isMinimized());
    QCOMPARE(views.at(i)->width(),qMax(320,area.width()*2/3));
    QCOMPARE(views.at(i)->height(),qMax(240,area.height()*2/3));
    QVERIFY(area.contains(views.at(i)->geometry()));
  }
  QCOMPARE(views.at(0)->pos(),area.topLeft());
  QVERIFY(views.at(1)->x()>views.at(0)->x() && views.at(1)->y()>views.at(0)->y());
  QVERIFY(views.at(2)->x()>views.at(1)->x() && views.at(2)->y()>views.at(1)->y());
  QCOMPARE(mdi->currentSubWindow(),views.at(0));
  QCOMPARE(mdi->subWindowList(QMdiArea::StackingOrder).last(),views.at(0));

  mdi->closeAllSubWindows();
  refresh();
  QCOMPARE(windowMenu->actions().size(),4);
  window.close_all_files();
}

// Mask Range on decoded values. Fixture packed like the CMPA FAST product: int32, scale_factor 1e-4,
// valid_range [0, 99990000] stored = [0, 9999] decoded, sentinel stored 99990000 = decoded 9999.
// precipitation[y][x] stored = x*10000 (decoded x) for x < 4, sentinel at x == 4; 3 x 5 plane.
void DropTargetsTest::maskRangeUsesDecodedValuesWithOpenEnds() {
  // pure functions: inclusive range, open ends, already-missing values untouched
  std::vector<double> values={0,1,2,9999,NAN,-1};
  QCOMPARE(nc_mask_range(values,9999,INFINITY),size_t(1));
  QVERIFY(std::isnan(values[3]));
  QCOMPARE(values[2],2.0);
  QCOMPARE(nc_mask_range(values,-INFINITY,0),size_t(2));   // 0 and -1
  QCOMPARE(nc_mask_range(values,1,2),size_t(2));
  double bound=0;
  QVERIFY(nc_mask_bound("",-INFINITY,bound)); QVERIFY(std::isinf(bound) && bound<0);
  QVERIFY(nc_mask_bound(" - ",INFINITY,bound)); QVERIFY(std::isinf(bound) && bound>0);
  QVERIFY(nc_mask_bound("9999",INFINITY,bound)); QCOMPARE(bound,9999.0);
  QVERIFY(nc_mask_bound("1e-4",INFINITY,bound)); QCOMPARE(bound,1e-4);
  QVERIFY(!nc_mask_bound("abc",INFINITY,bound));
  QVERIFY(!nc_mask_bound("inf",INFINITY,bound));

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString file=dir.filePath("packed.nc");
  {
    const QByteArray path=file.toLocal8Bit();
    int ncid=-1,timeDim=-1,latDim=-1,lonDim=-1,var=-1;
    QCOMPARE(nc_create(path.constData(),NC_NETCDF4|NC_CLASSIC_MODEL|NC_CLOBBER,&ncid),NC_NOERR);
    QCOMPARE(nc_def_dim(ncid,"time",1,&timeDim),NC_NOERR);
    QCOMPARE(nc_def_dim(ncid,"lat",3,&latDim),NC_NOERR);
    QCOMPARE(nc_def_dim(ncid,"lon",5,&lonDim),NC_NOERR);
    const int dims[3]={timeDim,latDim,lonDim};
    QCOMPARE(nc_def_var(ncid,"precipitation",NC_INT,3,dims,&var),NC_NOERR);
    const double scale=0.0001, offset=0.0;
    const int validRange[2]={0,99990000};
    QCOMPARE(nc_put_att_double(ncid,var,"scale_factor",NC_DOUBLE,1,&scale),NC_NOERR);
    QCOMPARE(nc_put_att_double(ncid,var,"add_offset",NC_DOUBLE,1,&offset),NC_NOERR);
    QCOMPARE(nc_put_att_int(ncid,var,"valid_range",NC_INT,2,validRange),NC_NOERR);
    QCOMPARE(nc_enddef(ncid),NC_NOERR);
    int stored[15];
    for(int y=0;y<3;++y) for(int x=0;x<5;++x) stored[y*5+x]= x<4 ? x*10000 : 99990000;
    QCOMPARE(nc_put_var_int(ncid,var,stored),NC_NOERR);
    QCOMPARE(nc_close(ncid),NC_NOERR);
  }

  // decoding stated in decoded and stored units (Properties / hdf_cli props)
  const NcPacking packing=nc_packing(file,"/precipitation");
  QVERIFY(packing.found && packing.packed());
  QCOMPARE(packing.decode(99990000),9999.0);
  const QStringList lines=nc_packing_lines(packing);
  QCOMPARE(lines.value(0),QString("Decoding: decoded = stored x scale_factor + add_offset = stored x 0.0001 + 0"));
  QCOMPARE(lines.value(1),QString("valid_range decoded: [0, 9999] (stored [0, 99990000])"));
  const QStringList props=HdfProperties::describe(HdfProperties::inspect(file,"/precipitation"));
  QVERIFY(props.contains(lines.value(1)));

  // Image View: the legend and the mask fields use decoded values
  RasterView view(file.toLocal8Bit().toStdString(),"/precipitation",{1,3,5});
  view.resize(900,400);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));
  auto *start=view.findChild<QLineEdit*>("maskStart");
  auto *end=view.findChild<QLineEdit*>("maskEnd");
  QVERIFY(start);QVERIFY(end);
  QCOMPARE(start->placeholderText(),QString("-inf"));
  QCOMPARE(end->placeholderText(),QString("+inf"));
  QVERIFY(start->toolTip().contains("stored [0, 99990000] = decoded [0, 9999]"));
  // on its own row, so it is never hidden behind the first toolbar's overflow button
  auto *maskBar=view.findChild<QToolBar*>("maskBar");
  QVERIFY(maskBar);
  QVERIFY(maskBar->isAncestorOf(start));
  QVERIFY(start->isVisible() && end->isVisible());
  QVERIFY(view.findChild<QLabel*>("maskHint")->text().contains("valid_range decoded [0, 9999], stored [0, 99990000]"));
  QTRY_COMPARE(view.property("maximum").toDouble(),9999.0);   // sentinel stretches the colors
  QCOMPARE(view.property("masked").toULongLong(),0ull);

  auto apply=[](QLineEdit *edit,const QString &text){ edit->setText(text); emit edit->editingFinished(); };
  apply(start,"9999");                                         // [9999, +inf]
  QCOMPARE(view.property("masked").toULongLong(),3ull);
  QCOMPARE(view.property("minimum").toDouble(),0.0);
  QCOMPARE(view.property("maximum").toDouble(),3.0);
  auto *legend=view.findChild<QLabel*>("rasterLegend");
  QVERIFY(legend);
  QVERIFY2(legend->text().contains("Masked [9999, +inf] (decoded): 3, white"),qPrintable(legend->text()));
  // the masked column is white, the colored ones are not
  QGraphicsView *canvas=view.findChild<QGraphicsView*>();
  QImage image;
  for(QGraphicsItem *item:canvas->scene()->items())
    if(auto *pixmap=dynamic_cast<QGraphicsPixmapItem*>(item)) image=pixmap->pixmap().toImage();
  QCOMPARE(image.size(),QSize(5,3));
  for(int y=0;y<3;++y) {
    QCOMPARE(image.pixel(4,y),qRgb(255,255,255));
    QVERIFY(image.pixel(0,y)!=qRgb(255,255,255));
  }

  apply(start,""); apply(end,"0");                             // [-inf, 0]
  QCOMPARE(view.property("masked").toULongLong(),3ull);
  QCOMPARE(view.property("minimum").toDouble(),1.0);
  QCOMPARE(view.property("maximum").toDouble(),9999.0);
  apply(start,"1"); apply(end,"2");                            // inclusive inner range
  QCOMPARE(view.property("masked").toULongLong(),6ull);
  apply(start,"99990000"); apply(end,"");                      // stored value: no decoded value that large
  QCOMPARE(view.property("masked").toULongLong(),0ull);
  QCOMPARE(view.property("maximum").toDouble(),9999.0);
  apply(start,"5"); apply(end,"1");                            // start > end
  QVERIFY(legend->text().contains("start must not be greater than end"));
  apply(start,"x");
  QVERIFY(legend->text().contains("must be numbers"));
  apply(start,""); apply(end,"");                              // no mask
  QCOMPARE(view.property("masked").toULongLong(),0ull);
  QCOMPARE(view.property("maximum").toDouble(),9999.0);
}

// Two grids of one variable: Data Grid (decoded to real) applies scale_factor/add_offset, Stored
// Grid (raw value) shows the values exactly as the file holds them. Decoding starts from the stored
// number, not from its "%g" text, so float32 precision survives.
void DropTargetsTest::decodedAndStoredGridsShowScaleFactorAndOffset() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString file=dir.filePath("packed_grids.nc");
  {
    const QByteArray path=file.toLocal8Bit();
    int ncid=-1,latDim=-1,lonDim=-1,precipVar=-1,tempVar=-1;
    QCOMPARE(nc_create(path.constData(),NC_NETCDF4|NC_CLASSIC_MODEL|NC_CLOBBER,&ncid),NC_NOERR);
    QCOMPARE(nc_def_dim(ncid,"lat",2,&latDim),NC_NOERR);
    QCOMPARE(nc_def_dim(ncid,"lon",3,&lonDim),NC_NOERR);
    const int dims[2]={latDim,lonDim};
    QCOMPARE(nc_def_var(ncid,"precipitation",NC_INT,2,dims,&precipVar),NC_NOERR);
    QCOMPARE(nc_def_var(ncid,"temperature",NC_FLOAT,2,dims,&tempVar),NC_NOERR);
    const double precipScale=0.0001, precipOffset=0.0;
    const float tempScale=2.5f, tempOffset=-100.0f;
    QCOMPARE(nc_put_att_double(ncid,precipVar,"scale_factor",NC_DOUBLE,1,&precipScale),NC_NOERR);
    QCOMPARE(nc_put_att_double(ncid,precipVar,"add_offset",NC_DOUBLE,1,&precipOffset),NC_NOERR);
    QCOMPARE(nc_put_att_float(ncid,tempVar,"scale_factor",NC_FLOAT,1,&tempScale),NC_NOERR);
    QCOMPARE(nc_put_att_float(ncid,tempVar,"add_offset",NC_FLOAT,1,&tempOffset),NC_NOERR);
    QCOMPARE(nc_enddef(ncid),NC_NOERR);
    const int stored[6]={0,10000,123800,250,99990000,-5000};
    QCOMPARE(nc_put_var_int(ncid,precipVar,stored),NC_NOERR);
    // 123456789 needs more than the 6 significant digits of "%g": a decoded grid that parsed the
    // stored text would print 123457000 * 2.5 - 100 instead of the exact product
    const float raw[6]={123456789.0f,0.0f,-1.5f,1e-5f,86399.0f,250000.5f};
    QCOMPARE(nc_put_var_float(ncid,tempVar,raw),NC_NOERR);
    QCOMPARE(nc_close(ncid),NC_NOERR);
  }

  MainWindow window;
  auto *tree=window.findChild<FileTreeWidget*>();
  auto *mdi=window.findChild<QMdiArea*>();
  QVERIFY(tree);QVERIFY(mdi);
  close_modal_dialogs_later();
  QCOMPARE(window.read_file(file),0);
  QTreeWidgetItem *root=tree->topLevelItem(0);
  QVERIFY(root);
  QTreeWidgetItem *precip=nullptr;
  for(int i=0;i<root->childCount();i++)
    if(root->child(i)->text(0)=="precipitation") precip=root->child(i);
  QVERIFY(precip);

  auto grid_model=[](QMdiSubWindow *sub) {
    return sub->widget()->findChild<QTableView*>("gridTable")->model();
  };
  auto cell=[](QAbstractItemModel *model,int row,int column) {
    return model->data(model->index(row,column)).toString();
  };

  // decoded: stored x 0.0001, the numbers the image view and the legend show
  tree->setCurrentItem(precip);
  QVERIFY(QMetaObject::invokeMethod(tree,"add_grid"));
  QCOMPARE(mdi->subWindowList().size(),1);
  QMdiSubWindow *decoded=mdi->subWindowList().last();
  QAbstractItemModel *decodedModel=grid_model(decoded);
  QCOMPARE(cell(decodedModel,0,1),QString("1"));
  QCOMPARE(cell(decodedModel,0,2),QString("12.38"));
  QCOMPARE(cell(decodedModel,1,0),QString("0.025"));
  QCOMPARE(cell(decodedModel,1,1),QString("9999"));
  QCOMPARE(cell(decodedModel,1,2),QString("-0.5"));
  QCOMPARE(decodedModel->headerData(0,Qt::Horizontal).toString(),QString("Value\ndecoded"));
  QVERIFY(decodedModel->headerData(0,Qt::Horizontal,Qt::ToolTipRole).toString().contains("stored x 0.0001 + 0"));
  QVERIFY(decoded->windowTitle().contains("Data Grid (decoded to real)"));

  // stored: the integers exactly as written in the file
  tree->setCurrentItem(precip);
  QVERIFY(QMetaObject::invokeMethod(tree,"add_stored_grid"));
  QCOMPARE(mdi->subWindowList().size(),2);
  QMdiSubWindow *stored=mdi->subWindowList().last();
  QAbstractItemModel *storedModel=grid_model(stored);
  QCOMPARE(cell(storedModel,0,1),QString("10000"));
  QCOMPARE(cell(storedModel,0,2),QString("123800"));
  QCOMPARE(cell(storedModel,1,0),QString("250"));
  QCOMPARE(cell(storedModel,1,1),QString("99990000"));
  QCOMPARE(cell(storedModel,1,2),QString("-5000"));
  QCOMPARE(storedModel->headerData(0,Qt::Horizontal).toString(),QString("Value\nraw"));
  QVERIFY(stored->windowTitle().contains("Stored Grid (raw value)"));
  // both windows stay open side by side, each showing its own numbers
  QCOMPARE(cell(decodedModel,1,1),QString("9999"));

  // float32 packing keeps its precision: the exact stored value is used, not its 6-digit text
  mdi->closeAllSubWindows();
  QTreeWidgetItem *temp=nullptr;
  for(int i=0;i<root->childCount();i++)
    if(root->child(i)->text(0)=="temperature") temp=root->child(i);
  QVERIFY(temp);
  tree->setCurrentItem(temp);
  QVERIFY(QMetaObject::invokeMethod(tree,"add_grid"));
  QAbstractItemModel *tempDecoded=grid_model(mdi->subWindowList().last());
  const QString exact=QString::asprintf("%.15g",double(123456789.0f)*2.5-100.0);
  const QString fromText=QString::asprintf("%.15g",double(QString::asprintf("%g",123456789.0f).toDouble())*2.5-100.0);
  QCOMPARE(cell(tempDecoded,0,0),exact);
  QVERIFY2(exact!=fromText,qPrintable(exact));
  QCOMPARE(cell(tempDecoded,0,2),QString("-103.75"));
  QCOMPARE(cell(tempDecoded,1,0),QString("-99.9999750000006"));
  QCOMPARE(cell(tempDecoded,1,2),QString("624901.25"));
  mdi->closeAllSubWindows();
  window.close_all_files();
}

// A file dragged from Explorer can be released anywhere in the window: the tree, the empty MDI area,
// the File Info / By NetCDF4 / CF Check panes, a grid or image view, a toolbar text field. Events go
// through QApplication::notify, which (as for a real OLE drop) starts at the widget under the cursor
// and propagates to parents that accept drops.
void DropTargetsTest::fileDropOpensFromEveryAreaOfTheMainWindow() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString fixture=dir.filePath("first.nc");
  const QString dropped=dir.filePath("dropped.nc");
  write_classic_fixture(fixture,NC_CLOBBER);
  write_classic_fixture(dropped,NC_CLOBBER);

  MainWindow window;
  window.resize(1200,800);
  window.show();
  QVERIFY(QTest::qWaitForWindowExposed(&window));
  auto *tree=window.findChild<FileTreeWidget*>();
  auto *mdi=window.findChild<QMdiArea*>();
  close_modal_dialogs_later();
  QCOMPARE(window.read_file(fixture),0);
  QTreeWidgetItem *root=tree->topLevelItem(0);
  QTreeWidgetItem *ttr=nullptr;
  for(int i=0;i<root->childCount();i++) if(root->child(i)->text(0)=="ttr") ttr=root->child(i);
  QVERIFY(ttr);
  tree->setCurrentItem(ttr);
  QVERIFY(QMetaObject::invokeMethod(tree,"add_grid"));
  emit tree->itemDoubleClicked(ttr,0);   // image view
  QTRY_COMPARE(mdi->subWindowList().size(),2);
  QWidget *grid=mdi->subWindowList().at(0)->widget();
  QWidget *image=mdi->subWindowList().at(1)->widget();

  QList<QPair<QString,QWidget*>> targets;
  targets << qMakePair(QString("tree"),tree->viewport());
  targets << qMakePair(QString("empty MDI area"),mdi->viewport());
  for(QPlainTextEdit *pane:window.findChildren<QPlainTextEdit*>())
    if(!pane->window()->inherits("QDialog")) targets << qMakePair(QString("info pane ")+pane->objectName(),pane->viewport());
  targets << qMakePair(QString("grid table"),static_cast<QWidget*>(grid->findChild<QTableView*>("gridTable")->viewport()));
  targets << qMakePair(QString("image canvas"),static_cast<QWidget*>(image->findChild<QGraphicsView*>()->viewport()));
  targets << qMakePair(QString("mask field"),static_cast<QWidget*>(image->findChild<QLineEdit*>("maskStart")));
  targets << qMakePair(QString("layer combo"),static_cast<QWidget*>(window.findChild<QComboBox*>("layerIndexCombo")));
  QVERIFY(targets.size()>=8);

  QStringList failed;
  for(const auto &target:targets) {
    const int before=tree->topLevelItemCount();
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(dropped)});
    mime.setText(QUrl::fromLocalFile(dropped).toString());   // Explorer also offers the path as text
    const QPoint point=target.second->rect().center();
    QDragEnterEvent enter(point,Qt::CopyAction|Qt::MoveAction|Qt::LinkAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(target.second,&enter);
    QDragMoveEvent move(point,Qt::CopyAction|Qt::MoveAction|Qt::LinkAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(target.second,&move);
    QDropEvent drop(QPointF(point),Qt::CopyAction|Qt::MoveAction|Qt::LinkAction,&mime,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(target.second,&drop);
    const bool opened=QTest::qWaitFor([&]{ return tree->topLevelItemCount()==before+1; },2000);
    if(!enter.isAccepted() || !drop.isAccepted() || !opened)
      failed << QString("%1 (enter %2, drop %3, opened %4)").arg(target.first)
                  .arg(enter.isAccepted()).arg(drop.isAccepted()).arg(opened);
    if(opened) {
      QTreeWidgetItem *added=tree->topLevelItem(tree->topLevelItemCount()-1);
      window.close_file_of(added);
    }
  }
  QVERIFY2(failed.isEmpty(),qPrintable(failed.join("; ")));
  mdi->closeAllSubWindows();
  window.close_all_files();
}

// Optional reconciliation on a real, large NetCDF-3 product; set HDFSTUDIO_CLASSIC_SAMPLE to its
// path. The dump (one grid plane plus the decoded time column) is compared with netCDF4 by
// tests/reconcile_classic_grid.py. Without the variable the test is skipped.
void DropTargetsTest::externalClassicSampleMatchesPython() {
  const QString sample=qEnvironmentVariable("HDFSTUDIO_CLASSIC_SAMPLE");
  if(sample.isEmpty() || !QFileInfo(sample).isFile()) QSKIP("HDFSTUDIO_CLASSIC_SAMPLE is not set");
  MainWindow window;
  auto *tree=window.findChild<FileTreeWidget*>();
  auto *mdi=window.findChild<QMdiArea*>();
  close_modal_dialogs_later();
  QCOMPARE(window.read_file(sample),0);
  QTreeWidgetItem *root=tree->topLevelItem(0);
  auto child_named=[root](const QString &name) -> QTreeWidgetItem* {
    for(int i=0;i<root->childCount();i++) if(root->child(i)->text(0)==name) return root->child(i);
    return nullptr;
  };
  const QDir logs(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../../../logs/S4-UI-ClassicNetcdf"));
  QVERIFY(QDir().mkpath(logs.absolutePath()));

  tree->setCurrentItem(child_named("ttr"));
  QElapsedTimer timer; timer.start();
  QVERIFY(QMetaObject::invokeMethod(tree,"add_grid"));
  const qint64 openMilliseconds=timer.elapsed();
  QWidget *grid=mdi->subWindowList().last()->widget();
  QAbstractItemModel *model=grid->findChild<QTableView*>("gridTable")->model();
  const auto combos=grid->findChildren<QComboBox*>();
  QCOMPARE(combos.size(),2);
  combos.at(0)->setCurrentIndex(7);
  combos.at(1)->setCurrentIndex(42);
  QFile plane(logs.absoluteFilePath("grid-ttr-t7-lev42.tsv"));
  QVERIFY(plane.open(QIODevice::WriteOnly|QIODevice::Truncate));
  for(int row=0;row<model->rowCount();row++) {
    QStringList cells;
    for(int column=0;column<model->columnCount();column++) cells << model->data(model->index(row,column)).toString();
    plane.write((cells.join('\t')+"\n").toUtf8());
  }
  plane.close();
  QFile meta(logs.absoluteFilePath("grid-open.txt"));
  QVERIFY(meta.open(QIODevice::WriteOnly|QIODevice::Truncate));
  meta.write(QString("grid_open_ms=%1\nrows=%2\ncolumns=%3\n").arg(openMilliseconds).arg(model->rowCount()).arg(model->columnCount()).toUtf8());
  meta.close();
  mdi->closeAllSubWindows();

  tree->setCurrentItem(child_named("time"));
  emit tree->itemDoubleClicked(child_named("time"),0);
  QAbstractItemModel *timeModel=mdi->subWindowList().last()->widget()->findChild<QTableView*>("gridTable")->model();
  QTRY_COMPARE_WITH_TIMEOUT(timeModel->columnCount(),3,60000);
  QFile times(logs.absoluteFilePath("grid-time.tsv"));
  QVERIFY(times.open(QIODevice::WriteOnly|QIODevice::Truncate));
  for(int row=0;row<timeModel->rowCount();row++)
    times.write(QString("%1\t%2\t%3\n").arg(timeModel->data(timeModel->index(row,0)).toString(),
        timeModel->data(timeModel->index(row,1)).toString(),timeModel->data(timeModel->index(row,2)).toString()).toUtf8());
}

// The third option: one table with the decoded column then the raw column, so a cell and the number
// it came from can be read in the same row. Date columns follow the doubled value columns.
void DropTargetsTest::bothGridsShowDecodedAndRawSideBySide() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString file=dir.filePath("both_grids.nc");
  {
    const QByteArray path=file.toLocal8Bit();
    int ncid=-1,latDim=-1,lonDim=-1,precipVar=-1;
    QCOMPARE(nc_create(path.constData(),NC_NETCDF4|NC_CLASSIC_MODEL|NC_CLOBBER,&ncid),NC_NOERR);
    QCOMPARE(nc_def_dim(ncid,"lat",2,&latDim),NC_NOERR);
    QCOMPARE(nc_def_dim(ncid,"lon",3,&lonDim),NC_NOERR);
    const int dims[2]={latDim,lonDim};
    QCOMPARE(nc_def_var(ncid,"precipitation",NC_INT,2,dims,&precipVar),NC_NOERR);
    const double scale=0.0001, offset=0.0;
    QCOMPARE(nc_put_att_double(ncid,precipVar,"scale_factor",NC_DOUBLE,1,&scale),NC_NOERR);
    QCOMPARE(nc_put_att_double(ncid,precipVar,"add_offset",NC_DOUBLE,1,&offset),NC_NOERR);
    QCOMPARE(nc_enddef(ncid),NC_NOERR);
    const int stored[6]={0,10000,123800,250,99990000,-5000};
    QCOMPARE(nc_put_var_int(ncid,precipVar,stored),NC_NOERR);
    QCOMPARE(nc_close(ncid),NC_NOERR);
  }

  MainWindow window;
  auto *tree=window.findChild<FileTreeWidget*>();
  auto *mdi=window.findChild<QMdiArea*>();
  QVERIFY(tree);QVERIFY(mdi);
  close_modal_dialogs_later();
  QCOMPARE(window.read_file(file),0);
  QTreeWidgetItem *root=tree->topLevelItem(0);
  QVERIFY(root);
  QTreeWidgetItem *precip=nullptr;
  for(int i=0;i<root->childCount();i++)
    if(root->child(i)->text(0)=="precipitation") precip=root->child(i);
  QVERIFY(precip);

  tree->setCurrentItem(precip);
  QVERIFY(QMetaObject::invokeMethod(tree,"add_both_grids"));
  QCOMPARE(mdi->subWindowList().size(),1);
  QMdiSubWindow *sub=mdi->subWindowList().last();
  QAbstractItemModel *model=sub->widget()->findChild<QTableView*>("gridTable")->model();
  // 3 lon columns become 6: decoded, raw, decoded, raw, decoded, raw
  QCOMPARE(model->columnCount(),6);
  QCOMPARE(model->headerData(0,Qt::Horizontal).toString(),QString("1\ndecoded"));
  QCOMPARE(model->headerData(1,Qt::Horizontal).toString(),QString("1\nraw"));
  QCOMPARE(model->headerData(4,Qt::Horizontal).toString(),QString("3\ndecoded"));
  QCOMPARE(model->headerData(5,Qt::Horizontal).toString(),QString("3\nraw"));
  QVERIFY(sub->windowTitle().contains("Both Grids (decoded and raw)"));
  // the same cell twice: 99990000 stored next to its decoded 9999, and -5000 next to -0.5
  QCOMPARE(model->data(model->index(1,2)).toString(),QString("9999"));
  QCOMPARE(model->data(model->index(1,3)).toString(),QString("99990000"));
  QCOMPARE(model->data(model->index(1,4)).toString(),QString("-0.5"));
  QCOMPARE(model->data(model->index(1,5)).toString(),QString("-5000"));
  // an unpacked value and its raw pair are identical
  QCOMPARE(model->data(model->index(0,0)).toString(),QString("0"));
  QCOMPARE(model->data(model->index(0,1)).toString(),QString("0"));
  mdi->closeAllSubWindows();
  window.close_all_files();
}

// Histogram of one layer, sampled at random: the default keeps one value in twenty, the sample is
// exactly round(finite / N) values drawn from the whole plane, the same seed keeps the same values,
// NaN is left out, and the bars are built from decoded values.
void DropTargetsTest::histogramSamplesOneInTwentyAndBinsDecodedValues() {
  // 0 .. 9999, so every bin has values and a lattice sample would give exactly 50 per bin.
  std::vector<double> values;
  for(int i = 0; i < 10000; ++i) values.push_back(double(i));
  const NcHistogram sampled = nc_histogram(values, 0.05, 10, 1);
  QVERIFY2(sampled.valid, qPrintable(sampled.error));
  QCOMPARE(sampled.total, size_t(10000));
  QCOMPARE(sampled.missing, size_t(0));
  QCOMPARE(sampled.every_nth, 20);
  QCOMPARE(sampled.sampled, size_t(500));        // exactly a twentieth of 10000, not "about"
  QCOMPARE(sampled.bins, 10);
  QCOMPARE(std::accumulate(sampled.counts.begin(), sampled.counts.end(), size_t(0)), sampled.sampled);
  // the values are drawn from the whole plane, so the axis covers (nearly) all of it
  QVERIFY(sampled.minimum >= 0.0 && sampled.minimum < 1000.0);
  QVERIFY(sampled.maximum <= 9999.0 && sampled.maximum > 9000.0);
  QCOMPARE(sampled.edges.front(), sampled.minimum);
  QCOMPARE(sampled.edges.back(), sampled.maximum);
  // a uniform plane has values in every bin...
  for(size_t count : sampled.counts) QVERIFY(count > 0);
  // ...and a random draw is not the regular grid that would give exactly 50 per bin
  QVERIFY(!std::all_of(sampled.counts.begin(), sampled.counts.end(),
                       [](size_t count) { return count == 50; }));

  // the same seed is repeatable, another seed is another sample of the same size
  const NcHistogram again = nc_histogram(values, 0.05, 10, 1);
  QCOMPARE(again.sampled, sampled.sampled);
  QCOMPARE(again.counts, sampled.counts);
  QCOMPARE(again.minimum, sampled.minimum);
  const NcHistogram other = nc_histogram(values, 0.05, 10, 2);
  QCOMPARE(other.sampled, size_t(500));
  QVERIFY(other.counts != sampled.counts);

  // keeping everything uses every value; NaN is missing, not a bin
  std::vector<double> with_missing = values;
  with_missing.push_back(NAN);
  with_missing.push_back(NAN);
  const NcHistogram all = nc_histogram(with_missing, 1.0, 4, 1);
  QCOMPARE(all.total, size_t(10000));
  QCOMPARE(all.missing, size_t(2));
  QCOMPARE(all.sampled, size_t(10000));
  QCOMPARE(all.minimum, 0.0);
  QCOMPARE(all.maximum, 9999.0);          // 1 in 1 sees the exact extremes
  QCOMPARE(all.edges.back(), 9999.0);
  QVERIFY(qFuzzyCompare(all.mean + 1.0, 4999.5 + 1.0));
  // the maximum value lands in the last bin, not outside it
  QCOMPARE(all.counts.front(), size_t(2500));
  QCOMPARE(all.counts.back(), size_t(2500));

  // a plane smaller than the step still keeps one value (a twentieth of 3 rounds to nothing)
  const std::vector<double> three{1.0, 2.0, 3.0};
  const NcHistogram tiny = nc_histogram(three, 0.05, 4, 1);
  QVERIFY(tiny.valid);
  QCOMPARE(tiny.total, size_t(3));
  QCOMPARE(tiny.sampled, size_t(1));
  QCOMPARE(tiny.sampled, std::accumulate(tiny.counts.begin(), tiny.counts.end(), size_t(0)));

  // a constant plane still draws one bar, and a bad fraction is refused
  const NcHistogram constant = nc_histogram(std::vector<double>(100, 42.0), 1.0, 8, 1);
  QVERIFY(constant.valid);
  QCOMPARE(constant.bins, 1);
  QCOMPARE(constant.counts.at(0), size_t(100));
  QVERIFY(constant.edges.front() < 42.0 && constant.edges.back() > 42.0);
  QVERIFY(!nc_histogram(values, 0.0, 8, 1).valid);
  QVERIFY(!nc_histogram(values, 1.5, 8, 1).valid);
  QVERIFY(!nc_histogram(std::vector<double>(4, NAN), 0.05, 8, 1).valid);

  // decoded values: the packed fixture stores x*10000 and means x, so the histogram spans 0..9999
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString file=dir.filePath("histogram.nc");
  {
    const QByteArray path=file.toLocal8Bit();
    int ncid=-1,timeDim=-1,latDim=-1,lonDim=-1,var=-1;
    QCOMPARE(nc_create(path.constData(),NC_NETCDF4|NC_CLASSIC_MODEL|NC_CLOBBER,&ncid),NC_NOERR);
    QCOMPARE(nc_def_dim(ncid,"time",1,&timeDim),NC_NOERR);
    QCOMPARE(nc_def_dim(ncid,"lat",4,&latDim),NC_NOERR);
    QCOMPARE(nc_def_dim(ncid,"lon",5,&lonDim),NC_NOERR);
    const int dims[3]={timeDim,latDim,lonDim};
    QCOMPARE(nc_def_var(ncid,"precipitation",NC_INT,3,dims,&var),NC_NOERR);
    const double scale=0.0001, offset=0.0;
    QCOMPARE(nc_put_att_double(ncid,var,"scale_factor",NC_DOUBLE,1,&scale),NC_NOERR);
    QCOMPARE(nc_put_att_double(ncid,var,"add_offset",NC_DOUBLE,1,&offset),NC_NOERR);
    const int fillValue=-32767;
    QCOMPARE(nc_put_att_int(ncid,var,"_FillValue",NC_INT,1,&fillValue),NC_NOERR);
    QCOMPARE(nc_enddef(ncid),NC_NOERR);
    int stored[20];
    for(int i=0;i<20;++i) stored[i]= i==0 ? -32767 : (i-1)*10000;   // 0 .. 18 -> decoded 0..18
    QCOMPARE(nc_put_var_int(ncid,var,stored),NC_NOERR);
    QCOMPARE(nc_close(ncid),NC_NOERR);
  }

  const NcPlaneResult plane=nc_read_plane_decoded(file,"/precipitation",{1,4,5},{0});
  QVERIFY2(plane.error.isEmpty(),qPrintable(plane.error));
  QCOMPARE(plane.reader,QString("netCDF-C"));
  QCOMPARE(plane.plane.rows,size_t(4));
  QCOMPARE(plane.plane.cols,size_t(5));
  QCOMPARE(plane.plane.values.size(),size_t(20));
  QVERIFY(std::isnan(plane.plane.values[0]));           // _FillValue became NaN
  QCOMPARE(plane.plane.values[1],0.0);
  QCOMPARE(plane.plane.values[19],18.0);

  const NcHistogram packed=nc_histogram(plane.plane.values,1.0,6,1);
  QVERIFY(packed.valid);
  QCOMPARE(packed.total,size_t(19));                    // the missing value is not counted
  QCOMPARE(packed.missing,size_t(1));
  QCOMPARE(packed.minimum,0.0);
  QCOMPARE(packed.maximum,18.0);
  const QStringList lines=nc_histogram_lines(packed);
  QVERIFY2(lines.first().contains("19 finite, 1 missing"),qPrintable(lines.first()));
  QVERIFY2(lines.value(1).contains("range: [0, 18]"),qPrintable(lines.value(1)));

  // the window: default sampling is 1 in 20, the plane is read once and resampling is a setting
  HistogramView view(file.toLocal8Bit().toStdString(),"/precipitation",{1,4,5});
  view.resize(800,600);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));
  // the sampling field is found by its accessible name: the first spin box is the layer selector
  QSpinBox *every=nullptr;
  for(QSpinBox *spin:view.findChildren<QSpinBox*>())
    if(spin->accessibleName()=="Sample 1 in N") every=spin;
  QVERIFY(every);
  QCOMPARE(every->value(),20);
  QCOMPARE(QString::number(view.property("keepFraction").toDouble()),QString("0.05"));
  QCOMPARE(view.property("planeValues").toULongLong(),qulonglong(20));
  QCOMPARE(view.property("total").toULongLong(),qulonglong(19));
  QCOMPARE(view.property("missing").toULongLong(),qulonglong(1));
  auto *canvas=view.findChild<HistogramCanvas*>();
  QVERIFY(canvas);
  // one twentieth of the 19 finite values is one value, drawn at random from the plane
  QCOMPARE(canvas->histogram().sampled,size_t(1));
  QVERIFY(canvas->histogram().minimum>=0.0 && canvas->histogram().maximum<=18.0);
  // the step only re-samples the values already in memory: no second read of the file
  every->setValue(2);
  QCOMPARE(view.property("sampled").toULongLong(),qulonglong(10));   // round(19 / 2)
  QCOMPARE(view.property("planeValues").toULongLong(),qulonglong(20));
  // 1 uses every value of the plane
  every->setValue(1);
  QCOMPARE(view.property("sampled").toULongLong(),qulonglong(19));
  QCOMPARE(QString::number(view.property("keepFraction").toDouble()),QString("1"));
  // the bins field re-bins the same values
  QSpinBox *bins=nullptr;
  for(QSpinBox *spin:view.findChildren<QSpinBox*>())
    if(spin->accessibleName()=="Bins") bins=spin;
  QVERIFY(bins);
  QCOMPARE(bins->value(),32);
  bins->setValue(5);
  QCOMPARE(view.property("bins").toInt(),5);
  QCOMPARE(canvas->histogram().counts.size(),size_t(5));
  QCOMPARE(nc_histogram_lines(canvas->histogram()).size(),size_t(3+canvas->histogram().bins));
  QVERIFY(!canvas->grab().isNull());

  // hdf_cli hist is the same code path: same totals, same sample size, same lines
  const QString cli=QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("hdf_cli.exe");
  QVERIFY2(QFileInfo(cli).isFile(),qPrintable(cli));
  auto run_cli=[&cli](const QStringList &arguments) -> QString {
    QProcess process;
    process.start(cli,arguments);
    if(!process.waitForFinished(60000) || process.exitCode()!=0)
      return QString("exit %1: %2").arg(process.exitCode()).arg(QString::fromUtf8(process.readAllStandardError()));
    return QString::fromUtf8(process.readAllStandardOutput());
  };
  const QString full=run_cli({"hist",file,"/precipitation","1.0","6","0"});
  QVERIFY2(full.contains("19 finite, 1 missing"),qPrintable(full));
  QVERIFY2(full.contains("sampled 19 at random (1 in 1, seed 1)"),qPrintable(full));
  // the same lines, apart from the platform line ending of a console stream
  const QString expected=nc_histogram_lines(nc_histogram(plane.plane.values,1.0,6,1)).join('\n');
  QCOMPARE(QString(full).replace("\r\n","\n").trimmed(),expected.trimmed());
  const QString fifth=run_cli({"hist",file,"/precipitation","0.05","6","0"});
  QVERIFY2(fifth.contains("sampled 1 at random (1 in 20, seed 1)"),qPrintable(fifth));
  QVERIFY2(run_cli({"hist",file,"/precipitation","0.0","6","0"}).contains("must be greater than 0"),
           "a zero fraction must be refused");
}

void DropTargetsTest::rasterFlipAndResetRestoreOriginalView() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString file = dir.filePath("flip.nc");
  {
    const QByteArray path = file.toLocal8Bit();
    int ncid = -1, latDim = -1, lonDim = -1, var = -1;
    QCOMPARE(nc_create(path.constData(), NC_NETCDF4 | NC_CLASSIC_MODEL | NC_CLOBBER, &ncid), NC_NOERR);
    QCOMPARE(nc_def_dim(ncid, "lat", 2, &latDim), NC_NOERR);
    QCOMPARE(nc_def_dim(ncid, "lon", 3, &lonDim), NC_NOERR);
    const int dims[2] = {latDim, lonDim};
    QCOMPARE(nc_def_var(ncid, "z", NC_DOUBLE, 2, dims, &var), NC_NOERR);
    QCOMPARE(nc_enddef(ncid), NC_NOERR);
    const double values[6] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    QCOMPARE(nc_put_var_double(ncid, var, values), NC_NOERR);
    QCOMPARE(nc_close(ncid), NC_NOERR);
  }

  RasterView view(file.toLocal8Bit().toStdString(), "/z", {2, 3});
  view.resize(800, 600);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));
  QTest::qWait(50);

  auto *canvas = view.findChild<QGraphicsView*>();
  auto *flipV = view.findChild<QAction*>("flipVerticalAction");
  auto *flipH = view.findChild<QAction*>("flipHorizontalAction");
  auto *reset = view.findChild<QAction*>("resetViewAction");
  QVERIFY(canvas && flipV && flipH && reset);
  QVERIFY(flipV->isCheckable() && flipH->isCheckable());
  QVERIFY(!flipV->isChecked() && !flipH->isChecked());

  const double baseScaleX = canvas->transform().m11();
  const double baseScaleY = canvas->transform().m22();
  QVERIFY(baseScaleX > 0.0 && baseScaleY > 0.0);

  flipV->trigger();
  QVERIFY(flipV->isChecked());
  QVERIFY(canvas->transform().m22() < 0.0);
  QVERIFY(canvas->transform().m11() > 0.0);

  flipH->trigger();
  QVERIFY(flipH->isChecked());
  QVERIFY(canvas->transform().m11() < 0.0);
  QVERIFY(canvas->transform().m22() < 0.0);

  // Zoom must still work when m11 < 0.
  QAction *zoomIn = view.findChild<QAction*>("zoomInAction");
  QVERIFY(zoomIn);
  const double beforeZoom = std::abs(canvas->transform().m11());
  zoomIn->trigger();
  QVERIFY(qFuzzyCompare(std::abs(canvas->transform().m11()),beforeZoom*1.25));

  // Toggle Flip V back off by clicking again.
  flipV->trigger();
  QVERIFY(!flipV->isChecked());
  QVERIFY(canvas->transform().m22() > 0.0);
  QVERIFY(canvas->transform().m11() < 0.0);

  // Reset restores orientation, zoom, and button states unconditionally.
  flipV->trigger();
  QVERIFY(flipV->isChecked() && flipH->isChecked());
  reset->trigger();
  QVERIFY(!flipV->isChecked() && !flipH->isChecked());
  QVERIFY(canvas->transform().m11() > 0.0 && canvas->transform().m22() > 0.0);
  QVERIFY(qFuzzyCompare(canvas->transform().m11(), baseScaleX));
  QVERIFY(qFuzzyCompare(canvas->transform().m22(), baseScaleY));
}

void DropTargetsTest::rasterZoomButtonsWheelAndCyclesPreserveData() {
  QTemporaryDir directory;QVERIFY(directory.isValid());
  const QString filename=directory.filePath("zoom.nc");
  int file=-1,row=-1,col=-1,variable=-1;
  QCOMPARE(nc_create(nc_path_bytes(filename).constData(),NC_CLOBBER,&file),NC_NOERR);
  QCOMPARE(nc_def_dim(file,"y",80,&row),NC_NOERR);QCOMPARE(nc_def_dim(file,"x",120,&col),NC_NOERR);
  int dimensions[]={row,col};QCOMPARE(nc_def_var(file,"field",NC_DOUBLE,2,dimensions,&variable),NC_NOERR);
  QCOMPARE(nc_enddef(file),NC_NOERR);
  std::vector<double> values(80*120);std::iota(values.begin(),values.end(),0.);
  QCOMPARE(nc_put_var_double(file,variable,values.data()),NC_NOERR);QCOMPARE(nc_close(file),NC_NOERR);
  RasterView view(filename.toLocal8Bit().toStdString(),"/field",{80,120});
  view.resize(800,650);view.show();QVERIFY(QTest::qWaitForWindowExposed(&view));
  QTRY_COMPARE(view.property("planeReadCount").toULongLong(),qulonglong(1));
  auto *canvas=view.findChild<QGraphicsView*>();auto *bar=view.findChild<QToolBar*>("maskBar");
  auto *zoom_in=view.findChild<QAction*>("zoomInAction"),*zoom_out=view.findChild<QAction*>("zoomOutAction");
  auto *fit=view.findChild<QAction*>("fitImageAction"),*reset=view.findChild<QAction*>("resetViewAction");
  auto *flip_h=view.findChild<QAction*>("flipHorizontalAction"),*flip_v=view.findChild<QAction*>("flipVerticalAction");
  QVERIFY(canvas&&bar&&zoom_in&&zoom_out&&fit&&reset&&flip_h&&flip_v);
  auto *in_button=qobject_cast<QToolButton*>(bar->widgetForAction(zoom_in));
  auto *out_button=qobject_cast<QToolButton*>(bar->widgetForAction(zoom_out));
  QVERIFY(in_button&&out_button);
  for(int width:{640,800,1100}) {
    view.resize(width,650);QCoreApplication::processEvents();
    QVERIFY(in_button->isVisibleTo(&view));QVERIFY(out_button->isVisibleTo(&view));
    QVERIFY(bar->rect().contains(in_button->geometry()));QVERIFY(bar->rect().contains(out_button->geometry()));
    const double initial=std::abs(canvas->transform().m11());
    QTest::mouseClick(in_button,Qt::LeftButton);
    QVERIFY(qFuzzyCompare(std::abs(canvas->transform().m11()),initial*1.25));
    QTest::mouseClick(out_button,Qt::LeftButton);
    QVERIFY(qFuzzyCompare(std::abs(canvas->transform().m11()),initial));
  }
  const auto reads=view.property("planeReadCount");const auto rendered=view.property("renderedRevision");
  QGraphicsPixmapItem *image=nullptr;
  for(auto *item:canvas->scene()->items()) if(item->type()==QGraphicsPixmapItem::Type) image=static_cast<QGraphicsPixmapItem*>(item);
  QVERIFY(image);const auto original_pixels=image->pixmap().cacheKey();
  for(int orientation=0;orientation<4;++orientation) {
    reset->trigger();flip_h->setChecked(bool(orientation&1));flip_v->setChecked(bool(orientation&2));
    for(int step=0;step<8;++step) zoom_in->trigger();
    const double initial=std::abs(canvas->transform().m11());
    const QPointF center=canvas->viewport()->rect().center();
    const QPointF before=canvas->viewportTransform().inverted().map(center);
    QTest::mouseClick(in_button,Qt::LeftButton);
    const QPointF after=canvas->viewportTransform().inverted().map(QPointF(canvas->viewport()->rect().center()));
    QVERIFY2(QLineF(before,after).length()<=2./initial,"Button zoom moved the view center");
    QTest::mouseClick(out_button,Qt::LeftButton);
    for(int cycle=0;cycle<100;++cycle) {zoom_in->trigger();zoom_out->trigger();}
    QVERIFY(qFuzzyCompare(std::abs(canvas->transform().m11()),initial));
    QCOMPARE(canvas->transform().m11()<0,bool(orientation&1));QCOMPARE(canvas->transform().m22()<0,bool(orientation&2));
    const QPointF pointer(canvas->viewport()->width()*.37,canvas->viewport()->height()*.43);
    auto wheel=[canvas,pointer](QPoint angle,QPoint pixels=QPoint()) {
      QWheelEvent event(pointer,canvas->viewport()->mapToGlobal(pointer.toPoint()),pixels,angle,
                        Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
      QApplication::sendEvent(canvas->viewport(),&event);
    };
    const QPointF at_pointer=canvas->viewportTransform().inverted().map(pointer);
    wheel(QPoint(0,120));
    QVERIFY(qFuzzyCompare(std::abs(canvas->transform().m11()),initial*1.25));
    const QPointF after_pointer=canvas->viewportTransform().inverted().map(pointer);
    QVERIFY2(QLineF(at_pointer,after_pointer).length()<=2./initial,"Wheel zoom moved the point under the pointer");
    wheel(QPoint(0,-120));QVERIFY(qFuzzyCompare(std::abs(canvas->transform().m11()),initial));
    wheel(QPoint(120,0));wheel(QPoint());QVERIFY(qFuzzyCompare(std::abs(canvas->transform().m11()),initial));
    wheel(QPoint(),QPoint(0,60));
    QVERIFY(qFuzzyCompare(std::abs(canvas->transform().m11()),initial*std::sqrt(1.25)));
    wheel(QPoint(),QPoint(0,-60));QVERIFY(qFuzzyCompare(std::abs(canvas->transform().m11()),initial));
    for(int step=0;step<100;++step) zoom_in->trigger();
    QVERIFY(qFuzzyCompare(std::abs(canvas->transform().m11()),2048.));
    zoom_in->trigger();QVERIFY(qFuzzyCompare(std::abs(canvas->transform().m11()),2048.));
    for(int step=0;step<100;++step) zoom_out->trigger();
    QVERIFY(qFuzzyCompare(std::abs(canvas->transform().m11()),.002));
    zoom_out->trigger();QVERIFY(qFuzzyCompare(std::abs(canvas->transform().m11()),.002));
    fit->trigger();QVERIFY(std::abs(canvas->transform().m11())>.002);
    QCOMPARE(canvas->transform().m11()<0,bool(orientation&1));QCOMPARE(canvas->transform().m22()<0,bool(orientation&2));
  }
  reset->trigger();
  auto *profile=view.findChild<QAction*>("profileModeAction");profile->setChecked(true);
  for(const QPointF &point:{QPointF(30,25),QPointF(80,50)})
    QTest::mouseClick(canvas->viewport(),Qt::LeftButton,Qt::NoModifier,canvas->mapFromScene(point));
  view.findChild<QAction*>("showProfileAction")->trigger();
  auto *profile_window=view.findChild<ProfileView*>();QVERIFY(profile_window);
  const auto profile_mean=profile_window->property("mean");profile_window->hide();view.activateWindow();
  QList<QGraphicsItem*> overlay;
  for(auto *item:canvas->scene()->items()) if(item->data(0).toString()=="profileOverlay") overlay<<item;
  QVERIFY(overlay.size()>=3);
  QTest::mouseClick(in_button,Qt::LeftButton);QTest::mouseClick(out_button,Qt::LeftButton);
  for(auto *item:overlay) QVERIFY(canvas->scene()->items().contains(item));
  QCOMPARE(profile_window->property("mean"),profile_mean);QVERIFY(profile->isChecked());
  QCOMPARE(view.property("planeReadCount"),reads);QCOMPARE(view.property("renderedRevision"),rendered);
  QCOMPARE(image->pixmap().cacheKey(),original_pixels);
  reset->trigger();QVERIFY(!flip_h->isChecked()&&!flip_v->isChecked());
  const QString log=qEnvironmentVariable("HDFSTUDIO_TEST_LOG_DIR");
  if(!log.isEmpty()) QVERIFY(view.grab().save(QDir(log).filePath("zoom-"+qEnvironmentVariable("HDFSTUDIO_TEST_CONFIG")+".png")));
}

void DropTargetsTest::curveProfileSamplesPolylineAndSplineOnDecodedPlane() {
  // 1) Pure-function checks: polyline, Catmull-Rom spline, bilinear interpolation, NaN handling.
  QVERIFY(curve_sample_path({QPointF(0, 0)}, CurvePathMode::Polyline, 0.5).empty());
  const std::vector<QPointF> line = curve_sample_path({QPointF(0, 0), QPointF(4, 0)}, CurvePathMode::Polyline, 0.5);
  QCOMPARE(line.size(), size_t(9));
  QVERIFY(qFuzzyCompare(line.front().x(), 0.0) && qFuzzyCompare(line.back().x(), 4.0));

  const std::vector<QPointF> controls{QPointF(0, 0), QPointF(2, 3), QPointF(5, 1), QPointF(8, 4)};
  const std::vector<QPointF> spline = curve_sample_path(controls, CurvePathMode::SmoothSpline, 0.5);
  QVERIFY(spline.size() > 10);
  QVERIFY(std::hypot(spline.front().x() - controls.front().x(), spline.front().y() - controls.front().y()) < 1e-6);
  QVERIFY(std::hypot(spline.back().x() - controls.back().x(), spline.back().y() - controls.back().y()) < 1e-6);

  const std::vector<double> plane{1.0, 2.0, 3.0,
                                  4.0, 5.0, 6.0,
                                  7.0, 8.0, std::numeric_limits<double>::quiet_NaN()};
  const CurveProfileResult invalid = curve_extract_profile(plane, 3, 3, {QPointF(0, 0)});
  QVERIFY(!invalid.valid);

  const CurveProfileResult half = curve_extract_profile(plane, 3, 3, {QPointF(0.5, 0.5), QPointF(2.0, 2.0)},
                                                        ProfileInterpMode::Bilinear);
  QVERIFY(half.valid);
  QCOMPARE(half.finite_count, size_t(1));
  QCOMPARE(half.missing_count, size_t(1));
  // Mean of (1, 2, 4, 5) = 3.0
  QVERIFY(qFuzzyCompare(half.points[0].value, 3.0));
  QVERIFY(std::isnan(half.points[1].value));

  // 2) GUI check on RasterView -> Profile mode -> click points -> Show Profile -> Clear.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString file = dir.filePath("profile.nc");
  {
    const QByteArray path = file.toLocal8Bit();
    int ncid = -1, latDim = -1, lonDim = -1, var = -1;
    QCOMPARE(nc_create(path.constData(), NC_NETCDF4 | NC_CLASSIC_MODEL | NC_CLOBBER, &ncid), NC_NOERR);
    QCOMPARE(nc_def_dim(ncid, "lat", 20, &latDim), NC_NOERR);
    QCOMPARE(nc_def_dim(ncid, "lon", 30, &lonDim), NC_NOERR);
    const int dims[2] = {latDim, lonDim};
    QCOMPARE(nc_def_var(ncid, "temp", NC_DOUBLE, 2, dims, &var), NC_NOERR);
    QCOMPARE(nc_enddef(ncid), NC_NOERR);
    std::vector<double> values(20 * 30, 0.0);
    for(int r = 0; r < 20; ++r)
      for(int c = 0; c < 30; ++c)
        values[size_t(r * 30 + c)] = double(r * 10 + c);
    QCOMPARE(nc_put_var_double(ncid, var, values.data()), NC_NOERR);
    QCOMPARE(nc_close(ncid), NC_NOERR);
  }

  RasterView view(file.toLocal8Bit().toStdString(), "/temp", {20, 30});
  view.resize(800, 600);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));
  QTest::qWait(50);

  auto *canvas = view.findChild<QGraphicsView*>();
  auto *profileMode = view.findChild<QAction*>("profileModeAction");
  auto *showProfile = view.findChild<QAction*>("showProfileAction");
  auto *clearProfile = view.findChild<QAction*>("clearProfileAction");
  QVERIFY(canvas && profileMode && showProfile && clearProfile);
  QVERIFY(!showProfile->isEnabled() && !clearProfile->isEnabled());

  profileMode->setChecked(true);
  const QPoint p1 = canvas->mapFromScene(QPointF(2.0, 2.0));
  const QPoint p2 = canvas->mapFromScene(QPointF(15.0, 10.0));
  const QPoint p3 = canvas->mapFromScene(QPointF(25.0, 15.0));
  QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, p1);
  QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, p2);
  QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, p3);
  QVERIFY(showProfile->isEnabled() && clearProfile->isEnabled());

  showProfile->trigger();
  auto *profileWin = view.findChild<ProfileView*>("profileView");
  QVERIFY(profileWin && profileWin->isVisible());
  QVERIFY(profileWin->property("finiteCount").toULongLong() > 10);
  QVERIFY(profileWin->property("minimum").toDouble() >= 0.0);
  QVERIFY(profileWin->property("maximum").toDouble() <= 19 * 10 + 29);

  clearProfile->trigger();
  QVERIFY(!showProfile->isEnabled() && !clearProfile->isEnabled());

  // Fine-tuning interactions: right-drag moves a control point, double-click deletes it,
  // and clicking Profile a second time (toggling off) clears the curve.
  QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, p1);
  QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, p2);
  QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, p3);
  QVERIFY(showProfile->isEnabled());
  // Double-click on p2 deletes it, leaving 2 points (still >= 2).
  QTest::mouseDClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, p2);
  QVERIFY(showProfile->isEnabled());
  // Double-click on p1 deletes it, leaving 1 point (< 2).
  QTest::mouseDClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, p1);
  QVERIFY(!showProfile->isEnabled() && clearProfile->isEnabled());
  // Add back p2, right-drag p3 to a new position, toggle Profile off (hides overlay, keeps points),
  // toggle Profile back on (restores overlay), then Clear erases all points.
  QTest::mouseClick(canvas->viewport(), Qt::LeftButton, Qt::NoModifier, p2);
  QVERIFY(showProfile->isEnabled());
  QTest::mousePress(canvas->viewport(), Qt::RightButton, Qt::NoModifier, p3);
  QTest::mouseMove(canvas->viewport(), canvas->mapFromScene(QPointF(20.0, 12.0)));
  QTest::mouseRelease(canvas->viewport(), Qt::RightButton, Qt::NoModifier,
                      canvas->mapFromScene(QPointF(20.0, 12.0)));
  QVERIFY(showProfile->isEnabled());
  auto overlay_count = [&view]() {
    int count = 0;
    for(QGraphicsItem *item : view.findChild<QGraphicsView*>()->scene()->items())
      if(item->data(0).toString() == "profileOverlay") ++count;
    return count;
  };
  QVERIFY(overlay_count() > 0);
  profileMode->setChecked(false);
  QCOMPARE(overlay_count(), 0);
  QVERIFY(showProfile->isEnabled() && clearProfile->isEnabled());
  profileMode->setChecked(true);
  QVERIFY(overlay_count() > 0);
  clearProfile->trigger();
  QCOMPARE(overlay_count(), 0);
  QVERIFY(!showProfile->isEnabled() && !clearProfile->isEnabled());
}

// Optional reconciliation on a real packed NetCDF-4 classic product (CMPA FAST style: int32 with
// scale_factor/add_offset). Set HDFSTUDIO_PACKED_SAMPLE to its path and HDFSTUDIO_PACKED_VARIABLE
// to the 2-D/3-D variable (default "precipitation"). The decoded and stored grids are dumped whole
// to logs/S4-UI-DecodedGrid/ and compared with netCDF4 by tests/reconcile_decoded_grid.py.
void DropTargetsTest::externalPackedSampleMatchesPython() {
  const QString sample=qEnvironmentVariable("HDFSTUDIO_PACKED_SAMPLE");
  if(sample.isEmpty() || !QFileInfo(sample).isFile())
    QSKIP("HDFSTUDIO_PACKED_SAMPLE is not set");
  const QString variable=qEnvironmentVariable("HDFSTUDIO_PACKED_VARIABLE").isEmpty()
      ? QString("precipitation") : qEnvironmentVariable("HDFSTUDIO_PACKED_VARIABLE");

  const QDir logs(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../../../logs/S4-UI-DecodedGrid"));
  QVERIFY(QDir().mkpath(logs.absolutePath()));
  MainWindow window;
  auto *tree=window.findChild<FileTreeWidget*>();
  auto *mdi=window.findChild<QMdiArea*>();
  QVERIFY(tree);QVERIFY(mdi);
  close_modal_dialogs_later();
  QCOMPARE(window.read_file(sample),0);
  QTreeWidgetItem *root=tree->topLevelItem(0);
  QVERIFY(root);
  QTreeWidgetItem *target=nullptr;
  for(int i=0;i<root->childCount();i++)
    if(root->child(i)->text(0)==variable) target=root->child(i);
  QVERIFY2(target,qPrintable(variable));

  auto dump=[](QWidget *grid,const QString &path) {
    QAbstractItemModel *model=grid->findChild<QTableView*>("gridTable")->model();
    QFile out(path);
    if(!out.open(QIODevice::WriteOnly|QIODevice::Truncate)) return false;
    for(int row=0;row<model->rowCount();row++) {
      QStringList cells;
      for(int column=0;column<model->columnCount();column++) cells << model->data(model->index(row,column)).toString();
      out.write((cells.join('\t')+"\n").toUtf8());
    }
    out.close();
    return true;
  };

  QStringList meta;
  struct ModeCase { const char *method; const char *name; const char *dump; };
  const ModeCase cases[]={{"add_grid","decoded","grid-decoded.tsv"},
                          {"add_stored_grid","stored","grid-stored.tsv"},
                          {"add_both_grids","both","grid-both.tsv"}};
  for(const ModeCase &testCase:cases) {
    tree->setCurrentItem(target);
    QElapsedTimer timer; timer.start();
    QVERIFY(QMetaObject::invokeMethod(tree,testCase.method));
    QWidget *grid=mdi->subWindowList().last()->widget();
    QAbstractItemModel *model=grid->findChild<QTableView*>("gridTable")->model();
    // first value of the first row: the cost of one cell shows in the 1.68 M cell benchmarks below
    const QString first=model->data(model->index(0,0)).toString();
    const qint64 milliseconds=timer.elapsed();
    QVERIFY(dump(grid,logs.absoluteFilePath(testCase.dump)));

    // whole-table read cost: every cell of the visible plane through the model
    QElapsedTimer readTimer; readTimer.start();
    for(int row=0;row<model->rowCount();row++)
      for(int column=0;column<model->columnCount();column++)
        model->data(model->index(row,column)).toString();
    const qint64 readMilliseconds=readTimer.elapsed();

    meta << QString("%1 rows=%2 columns=%3 first=\"%4\" open_ms=%5 all_cells_ms=%6")
                .arg(testCase.name).arg(model->rowCount()).arg(model->columnCount())
                .arg(first).arg(milliseconds).arg(readMilliseconds);
    mdi->closeAllSubWindows();
  }
  QFile metaFile(logs.absoluteFilePath("grid-open.txt"));
  QVERIFY(metaFile.open(QIODevice::WriteOnly|QIODevice::Truncate));
  metaFile.write((meta.join('\n')+"\n").toUtf8());
  metaFile.close();
  window.close_all_files();
}

void DropTargetsTest::regressionCliRejectsInvalidParameters_data() {
  QTest::addColumn<QStringList>("arguments");
  QTest::addColumn<int>("expected");
  QTest::newRow("zero-fraction") << QStringList{"hist","@file","/field","0","32"} << 2;
  QTest::newRow("negative-fraction") << QStringList{"hist","@file","/field","-0.1","32"} << 2;
  QTest::newRow("large-fraction") << QStringList{"hist","@file","/field","1.1","32"} << 2;
  QTest::newRow("zero-bins") << QStringList{"hist","@file","/field","1","0"} << 2;
  QTest::newRow("negative-bins") << QStringList{"hist","@file","/field","1","-3"} << 2;
  QTest::newRow("invalid-index") << QStringList{"csv","@file","/field","@out","9"} << 3;
  QTest::newRow("missing-variable") << QStringList{"props","@file","/absent"} << 3;
  QTest::newRow("missing-file") << QStringList{"probe","@missing"} << 3;
  QTest::newRow("corrupt-file") << QStringList{"info","@broken"} << 3;
  QTest::newRow("bad-mask") << QStringList{"mask","@file","/field","9","2","0"} << 2;
}
void DropTargetsTest::regressionCliRejectsInvalidParameters() {
  QFETCH(QStringList,arguments);QFETCH(int,expected);
  QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("valid.nc");
  int file=-1,y=-1,x=-1,var=-1;QCOMPARE(nc_create(nc_path_bytes(path).constData(),NC_CLOBBER,&file),NC_NOERR);
  nc_def_dim(file,"y",2,&y);nc_def_dim(file,"x",3,&x);int dims[]={y,x};nc_def_var(file,"field",NC_DOUBLE,2,dims,&var);nc_enddef(file);
  double data[]={0,1,2,3,4,5};nc_put_var_double(file,var,data);nc_close(file);
  QFile broken(dir.filePath("broken.nc"));QVERIFY(broken.open(QIODevice::WriteOnly));broken.write("not a netcdf file");broken.close();
  for(auto &arg:arguments) {if(arg=="@file")arg=path;else if(arg=="@out")arg=dir.filePath("out.csv");else if(arg=="@missing")arg=dir.filePath("missing.nc");else if(arg=="@broken")arg=broken.fileName();}
  QProcess child;child.start(QDir(QCoreApplication::applicationDirPath()).filePath("hdf_cli.exe"),arguments);
  QVERIFY(child.waitForFinished(30000));QCOMPARE(child.exitStatus(),QProcess::NormalExit);
  QCOMPARE(child.exitCode(),expected);
}
void DropTargetsTest::regressionBatchPreservesExistingOutputs() {
  QTemporaryDir dir;QVERIFY(dir.isValid());const auto filename=dir.filePath("preview.nc");
  int file=-1,y=-1,x=-1,var=-1;QCOMPARE(nc_create(nc_path_bytes(filename).constData(),NC_CLOBBER,&file),NC_NOERR);
  nc_def_dim(file,"y",2,&y);nc_def_dim(file,"x",3,&x);int dims[]={y,x};nc_def_var(file,"field",NC_DOUBLE,2,dims,&var);nc_enddef(file);double data[]={0,1,2,3,4,5};nc_put_var_double(file,var,data);nc_close(file);
  const auto target=dir.filePath("existing.png");QFile old(target);QVERIFY(old.open(QIODevice::WriteOnly));old.write("KEEP");old.close();
  QProcess child;child.start(QDir(QCoreApplication::applicationDirPath()).filePath("HDFStudio.exe"),{"--preview","field","--png",target,filename});
  QVERIFY(child.waitForFinished(30000));QCOMPARE(child.exitStatus(),QProcess::NormalExit);QVERIFY(child.exitCode()!=0);
  QVERIFY(old.open(QIODevice::ReadOnly));QCOMPARE(old.readAll(),QByteArray("KEEP"));old.close();
  QFile sidecar(target+".json");QVERIFY(sidecar.open(QIODevice::WriteOnly));sidecar.write("KEEP-JSON");sidecar.close();QVERIFY(old.remove());
  child.start(QDir(QCoreApplication::applicationDirPath()).filePath("HDFStudio.exe"),{"--preview","field","--png",target,filename});
  QVERIFY(child.waitForFinished(30000));QVERIFY(child.exitCode()!=0);QVERIFY(!QFileInfo::exists(target));
  QVERIFY(sidecar.open(QIODevice::ReadOnly));QCOMPARE(sidecar.readAll(),QByteArray("KEEP-JSON"));
}
void DropTargetsTest::regressionMappingDialogAndImportRecovery() {
  using namespace HdfColorMap;
  QStandardPaths::setTestModeEnabled(true);QString error;const auto maps=load_registry(error);QVERIFY(error.isEmpty());
  Mapping mapping;mapping.minimum=-2;mapping.maximum=6;mapping.reversed=true;
  ColorMapPicker picker(maps,5,mapping);picker.show();QVERIFY(QTest::qWaitForWindowExposed(&picker));
  auto *selector=picker.findChild<QComboBox*>("colorCardSelector");QVERIFY(selector);
  QLineEdit *search=nullptr;for(auto *edit:picker.findChildren<QLineEdit*>())if(edit->placeholderText()=="Search color cards")search=edit;
  QVERIFY(search);search->setText("thermal");auto *list=qobject_cast<QListView*>(selector->view());QVERIFY(list);
  for(int index=0;index<selector->count();++index)QCOMPARE(list->isRowHidden(index),!selector->itemText(index).contains("thermal",Qt::CaseInsensitive));
  search->clear();picker.findChild<QComboBox*>("colorNormalization")->setCurrentIndex(2);
  auto *period=picker.findChild<QLineEdit*>("colorPeriod");period->setText("0");
  QTimer::singleShot(50,&picker,[]{for(auto *widget:QApplication::topLevelWidgets())if(auto *box=qobject_cast<QMessageBox*>(widget))box->accept();});
  QTest::mouseClick(picker.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok),Qt::LeftButton);
  QVERIFY(picker.isVisible());period->setText("360");
  QTest::mouseClick(picker.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok),Qt::LeftButton);
  QCOMPARE(picker.result(),int(QDialog::Accepted));QCOMPARE(picker.selected_mapping().period,360.);QVERIFY(picker.selected_mapping().reversed);
  QTemporaryDir dir;QFile invalid(dir.filePath("bad.rgb"));QVERIFY(invalid.open(QIODevice::WriteOnly));invalid.write("ncolors=2\n0 0 0\n999 2 3\n");invalid.close();
  Palette output=maps[5];const auto id=output.id;QVERIFY(!import_rgb(invalid.fileName(),RgbScale::Byte,output).isEmpty());QCOMPARE(output.id,id);
}
void DropTargetsTest::regressionGuiExportsCurrentFullResolutionRaster() {
  QTemporaryDir dir;const auto filename=dir.filePath("export.nc");int file=-1,t=-1,y=-1,x=-1,var=-1;
  QCOMPARE(nc_create(nc_path_bytes(filename).constData(),NC_CLOBBER,&file),NC_NOERR);
  nc_def_dim(file,"t",2,&t);nc_def_dim(file,"y",300,&y);nc_def_dim(file,"x",300,&x);int dims[]={t,y,x};nc_def_var(file,"field",NC_DOUBLE,3,dims,&var);nc_enddef(file);
  std::vector<double> data(180000);std::iota(data.begin(),data.end(),0.);nc_put_var_double(file,var,data.data());nc_close(file);
  MainWindow window;window.resize(1200,850);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QCOMPARE(window.read_file(filename),0);
  auto *tree=window.findChild<FileTreeWidget*>();auto *mdi=window.findChild<QMdiArea*>();const auto targets=tree->findItems("field",Qt::MatchExactly|Qt::MatchRecursive);QCOMPARE(targets.size(),1);
  tree->setCurrentItem(targets.first());emit tree->itemDoubleClicked(targets.first(),0);
  QVERIFY(!mdi->subWindowList().isEmpty());
  auto *view=qobject_cast<RasterView*>(mdi->subWindowList().last()->widget());QVERIFY(view);
  view->set_layer_index(0,1);view->findChild<QCheckBox*>("reverseColorMap")->setChecked(true);
  auto *start=view->findChild<QLineEdit*>("maskStart"),*end=view->findChild<QLineEdit*>("maskEnd");start->setText("90000");end->setText("90099");QMetaObject::invokeMethod(end,"editingFinished");
  view->findChild<QAction*>("flipHorizontalAction")->setChecked(true);
  auto *canvas=view->findChild<QGraphicsView*>();QGraphicsPixmapItem *item=nullptr;for(auto *candidate:canvas->scene()->items())if(candidate->type()==QGraphicsPixmapItem::Type)item=static_cast<QGraphicsPixmapItem*>(candidate);QVERIFY(item);
  QTRY_COMPARE(item->pixmap().toImage().pixel(0,0),HdfColorMap::masked_color);
  const auto expected=item->pixmap().toImage().convertToFormat(QImage::Format_RGB32).mirrored(true,false);
  const auto output=dir.filePath("current.png");
  const bool old=QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);
  const auto restore=qScopeGuard([old]{QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,old);});
  QTimer answer;answer.setInterval(20);connect(&answer,&QTimer::timeout,&window,[&]{for(auto *widget:QApplication::topLevelWidgets())if(auto *dialog=qobject_cast<QFileDialog*>(widget)){dialog->selectFile(output);QMetaObject::invokeMethod(dialog,"accept");}});answer.start();
  QVERIFY(QMetaObject::invokeMethod(tree,"export_item_as_image"));answer.stop();
  const QImage actual(output);QCOMPARE(actual.size(),QSize(300,300));QCOMPARE(actual.convertToFormat(QImage::Format_RGB32),expected);
  window.close_all_files();
}
void DropTargetsTest::regressionAsyncLayersMasksAndRecovery() {
  using namespace HdfColorMap;QTemporaryDir dir;const auto filename=dir.filePath("mixed.nc");
  int file=-1,t=-1,y=-1,x=-1,var=-1;QCOMPARE(nc_create(nc_path_bytes(filename).constData(),NC_CLOBBER,&file),NC_NOERR);
  nc_def_dim(file,"t",2,&t);nc_def_dim(file,"y",300,&y);nc_def_dim(file,"x",300,&x);int dims[]={t,y,x};nc_def_var(file,"field",NC_DOUBLE,3,dims,&var);nc_enddef(file);
  std::vector<double> data(180000);std::iota(data.begin(),data.end(),0.);nc_put_var_double(file,var,data.data());nc_close(file);
  RasterView view(filename.toLocal8Bit().toStdString(),"/field",{2,300,300});view.resize(1100,750);view.show();QVERIFY(QTest::qWaitForWindowExposed(&view));
  auto *canvas=view.findChild<QGraphicsView*>();QGraphicsPixmapItem *item=nullptr;for(auto *candidate:canvas->scene()->items())if(candidate->type()==QGraphicsPixmapItem::Type)item=static_cast<QGraphicsPixmapItem*>(candidate);QVERIFY(item);
  QTRY_VERIFY(!item->pixmap().isNull());
  auto *palette=view.findChild<QComboBox*>("colorMapSelector");auto *reverse=view.findChild<QCheckBox*>("reverseColorMap");
  auto *start=view.findChild<QLineEdit*>("maskStart"),*end=view.findChild<QLineEdit*>("maskEnd");
  for(int i=0;i<30;++i){view.set_layer_index(0,i%2);palette->setCurrentIndex(i%18);reverse->setChecked(i%2);}
  view.set_layer_index(0,1);palette->setCurrentIndex(palette->findData("bids:viridis"));reverse->setChecked(true);
  start->setText("90000");end->setText("90099");QMetaObject::invokeMethod(end,"editingFinished");
  QTRY_COMPARE(item->pixmap().toImage().pixel(0,0),masked_color);
  QString error;const auto maps=load_registry(error);auto found=std::find_if(maps.begin(),maps.end(),[](const Palette &p){return p.id=="bids:viridis";});QVERIFY(found!=maps.end());
  Mapping settings;settings.minimum=90100;settings.maximum=179999;settings.reversed=true;
  QImage expected(300,300,QImage::Format_RGB32);for(int r=0;r<300;++r)for(int c=0;c<300;++c){const int index=r*300+c;expected.setPixel(c,r,index<100?masked_color:map_value(*found,settings,90000+index));}
  QTRY_VERIFY(item->pixmap().toImage().convertToFormat(QImage::Format_RGB32)==expected);
  QCOMPARE(view.layer_index(0),1);QCOMPARE(view.property("masked").toULongLong(),qulonglong(100));
  auto *bar=view.findChild<ColorBar*>();const auto bar_image=bar->grab().toImage().convertToFormat(QImage::Format_RGB32);
  const int bar_x=bar->width()/2;const double bar_fraction=double(bar_x-44)/(bar->width()-88);
  QCOMPARE(bar_image.pixel(qRound(bar_x*bar_image.devicePixelRatio()),qRound(32*bar_image.devicePixelRatio())),map_value(*found,settings,value_at(bar_fraction,settings)));
  const auto read_count=view.property("planeReadCount");
  const QPoint probe=canvas->mapFromScene(QPointF(.5,.5));
  QMouseEvent hover(QEvent::MouseMove,QPointF(probe),QPointF(canvas->viewport()->mapToGlobal(probe)),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
  QApplication::sendEvent(canvas->viewport(),&hover);QVERIFY(canvas->viewport()->toolTip().contains("Mask"));
  start->setText("invalid");QMetaObject::invokeMethod(start,"editingFinished");QVERIFY(item->pixmap().isNull());QVERIFY(!view.property("minimum").isValid());
  QVERIFY(canvas->viewport()->toolTip().isEmpty());QVERIFY(view.findChild<QLabel*>("rasterLegend")->toolTip().isEmpty());
  start->clear();end->clear();QMetaObject::invokeMethod(end,"editingFinished");
  QTRY_VERIFY(!item->pixmap().isNull());QTRY_COMPARE(item->pixmap().toImage().pixel(0,0),uint32_t(qRgb(253,231,37)));QCOMPARE(view.property("planeReadCount"),read_count);
  view.set_layer_index(0,999);QCOMPARE(view.layer_index(0),1);view.set_layer_index(0,-9);QCOMPARE(view.layer_index(0),0);
  for(int i=0;i<5;++i){auto *closing=new RasterView(filename.toLocal8Bit().toStdString(),"/field",{2,300,300});closing->show();QTest::qWait(5);delete closing;}QTest::qWait(100);
}
void DropTargetsTest::regressionFiveDimensionalSlicesAndTypes() {
  QTemporaryDir dir;const auto filename=dir.filePath("types.nc");int file=-1;QCOMPARE(nc_create(nc_path_bytes(filename).constData(),NC_NETCDF4|NC_CLOBBER,&file),NC_NOERR);
  int dims[5];for(int i=0;i<5;++i){const auto name=QString("d%1").arg(i).toLatin1();QCOMPARE(nc_def_dim(file,name.constData(),2,&dims[i]),NC_NOERR);}
  int var=-1,text=-1;nc_def_var(file,"signed64",NC_INT64,5,dims,&var);nc_def_var(file,"text",NC_STRING,2,dims+3,&text);nc_enddef(file);
  long long data[32];for(int i=0;i<32;++i)data[i]=-5000000000LL+i;nc_put_var_longlong(file,var,data);nc_close(file);
  NcReader reader(filename);NcPlane plane;QVERIFY(reader.read_plane("/signed64",{1,0,1},plane).isEmpty());QCOMPARE(plane.rows,size_t(2));QCOMPARE(plane.cols,size_t(2));for(int i=0;i<4;++i)QCOMPARE(plane.values[size_t(i)],double(data[20+i]));
  QVERIFY(!reader.read_plane("/signed64",{2,0,0},plane).isEmpty());QVERIFY(!reader.read_plane("/text",{},plane).isEmpty());
  QVERIFY(!reader.read_plane("/absent",{},plane).isEmpty());QVERIFY(reader.read_plane("/signed64",{0,0,0},plane).isEmpty());QCOMPARE(plane.values[0],-5000000000.);
}
void DropTargetsTest::regressionBudgetIsReleasedAfterClose() {
  QTemporaryDir dir;const auto filename=dir.filePath("budget.nc");int file=-1,y=-1,x=-1,var=-1;QCOMPARE(nc_create(nc_path_bytes(filename).constData(),NC_CLOBBER,&file),NC_NOERR);
  nc_def_dim(file,"y",100,&y);nc_def_dim(file,"x",100,&x);int dims[]={y,x};nc_def_var(file,"field",NC_DOUBLE,2,dims,&var);nc_enddef(file);std::vector<double> data(10000,1.);nc_put_var_double(file,var,data.data());nc_close(file);
  const auto previous=qgetenv("HDFSTUDIO_RASTER_BUDGET_MIB");qputenv("HDFSTUDIO_RASTER_BUDGET_MIB","1");const auto restore=qScopeGuard([previous]{if(previous.isNull())qunsetenv("HDFSTUDIO_RASTER_BUDGET_MIB");else qputenv("HDFSTUDIO_RASTER_BUDGET_MIB",previous);});
  auto *first=new RasterView(filename.toLocal8Bit().toStdString(),"/field",{100,100});first->show();QTRY_COMPARE(first->property("planeReadCount").toULongLong(),qulonglong(1));
  {RasterView blocked(filename.toLocal8Bit().toStdString(),"/field",{100,100});blocked.show();QTRY_VERIFY(blocked.findChild<QLabel*>("rasterLegend")->text().contains("memory budget"));QVERIFY(!blocked.property("planeReadCount").isValid());}
  delete first;RasterView recovered(filename.toLocal8Bit().toStdString(),"/field",{100,100});recovered.show();QTRY_COMPARE(recovered.property("planeReadCount").toULongLong(),qulonglong(1));
}

void DropTargetsTest::regressionCorruptFileDoesNotBreakOpenSession() {
  QTemporaryDir dir;const auto filename=dir.filePath("valid.nc");int file=-1,y=-1,x=-1,var=-1;
  QCOMPARE(nc_create(nc_path_bytes(filename).constData(),NC_CLOBBER,&file),NC_NOERR);nc_def_dim(file,"y",2,&y);nc_def_dim(file,"x",3,&x);int dims[]={y,x};nc_def_var(file,"field",NC_DOUBLE,2,dims,&var);nc_enddef(file);double data[]={0,1,2,3,4,5};nc_put_var_double(file,var,data);nc_close(file);
  QFile corrupt(dir.filePath("corrupt.nc"));QVERIFY(corrupt.open(QIODevice::WriteOnly));corrupt.write("not a netcdf file");corrupt.close();
  MainWindow window;window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));QCOMPARE(window.read_file(filename),0);
  auto *tree=window.findChild<FileTreeWidget*>();QCOMPARE(tree->topLevelItemCount(),1);
  // A dedicated watchdog only closes the expected error message, not unrelated dialogs.
  QTimer watchdog;watchdog.setInterval(20);connect(&watchdog,&QTimer::timeout,&window,[]{for(auto *w:QApplication::topLevelWidgets())if(auto *box=qobject_cast<QMessageBox*>(w))box->accept();});watchdog.start();
  QVERIFY(window.read_file(corrupt.fileName())!=0);watchdog.stop();QCOMPARE(tree->topLevelItemCount(),1);
  QCOMPARE(tree->findItems("field",Qt::MatchExactly|Qt::MatchRecursive).size(),1);
  window.close_all_files();QCOMPARE(tree->topLevelItemCount(),0);QCOMPARE(window.read_file(filename),0);QCOMPARE(tree->topLevelItemCount(),1);window.close_all_files();
  QVERIFY(QFile::remove(filename));
}

void DropTargetsTest::cleanup() {
  if(!QTest::currentTestFailed()) return;
  const QString directory=qEnvironmentVariable("HDFSTUDIO_TEST_LOG_DIR");if(directory.isEmpty())return;
  const QString name=QString::fromLatin1(QTest::currentTestFunction());
  if(auto *screen=QGuiApplication::primaryScreen())screen->grabWindow(0).save(QDir(directory).filePath(name+"-failure.png"));
  QFile details(QDir(directory).filePath(name+"-widgets.txt"));if(details.open(QIODevice::WriteOnly)){
    for(auto *widget:QApplication::topLevelWidgets())details.write(QString("%1 | %2 | visible=%3\n").arg(widget->metaObject()->className(),widget->windowTitle()).arg(widget->isVisible()).toUtf8());
  }
}

void DropTargetsTest::regressionVectorViewChangesLayerAndOmitsMissingPairs() {
  QTemporaryDir dir;const auto filename=dir.filePath("vectors.nc");int file=-1,t=-1,y=-1,x=-1,u=-1,v=-1;
  QCOMPARE(nc_create(nc_path_bytes(filename).constData(),NC_CLOBBER,&file),NC_NOERR);nc_def_dim(file,"t",2,&t);nc_def_dim(file,"y",24,&y);nc_def_dim(file,"x",24,&x);int dims[]={t,y,x};nc_def_var(file,"u",NC_DOUBLE,3,dims,&u);nc_def_var(file,"v",NC_DOUBLE,3,dims,&v);const double fill=-999;nc_put_att_double(file,u,"_FillValue",NC_DOUBLE,1,&fill);nc_put_att_double(file,v,"_FillValue",NC_DOUBLE,1,&fill);nc_enddef(file);
  std::vector<double> ud(1152),vd(1152);std::fill(ud.begin(),ud.begin()+576,4.);std::fill(ud.begin()+576,ud.end(),0.);std::fill(vd.begin(),vd.begin()+576,0.);std::fill(vd.begin()+576,vd.end(),4.);ud[0]=fill;nc_put_var_double(file,u,ud.data());nc_put_var_double(file,v,vd.data());nc_close(file);
  NcReader reader(filename);NcVectorPlane pair;QVERIFY(reader.read_vector_planes("/u","/v",{0},pair).isEmpty());QVERIFY(std::isnan(pair.u.values[0]));QCOMPARE(pair.u.values[1],4.);QCOMPARE(pair.v.values[1],0.);
  VectorView view(filename,"/u","/v",{2,24,24});view.resize(700,500);view.show();QVERIFY(QTest::qWaitForWindowExposed(&view));
  QLabel *canvas=nullptr;for(auto *label:view.findChildren<QLabel*>())if(!label->pixmap().isNull())canvas=label;QVERIFY(canvas);
  const auto first=canvas->pixmap().toImage().convertToFormat(QImage::Format_RGB32);QCOMPARE(first.size(),QSize(24,24));
  auto *axis=view.findChild<QSpinBox*>();QVERIFY(axis);axis->setValue(1);const auto second=canvas->pixmap().toImage().convertToFormat(QImage::Format_RGB32);QVERIFY(first!=second);
  axis->setValue(0);QCOMPARE(canvas->pixmap().toImage().convertToFormat(QImage::Format_RGB32),first);
}

void DropTargetsTest::regressionProfileCsvMatchesAnalyticField() {
  QTemporaryDir dir;auto values=std::make_shared<std::vector<double>>(100);for(int r=0;r<10;++r)for(int c=0;c<10;++c)(*values)[size_t(r*10+c)]=2.*r+3.*c+1.;
  ProfileView view("analytic",values,10,10,{QPointF(1,1),QPointF(7,7)},CurvePathMode::Polyline);view.show();QVERIFY(QTest::qWaitForWindowExposed(&view));
  const bool old=QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,true);const auto restore=qScopeGuard([old]{QApplication::setAttribute(Qt::AA_DontUseNativeDialogs,old);});
  const auto path=dir.filePath("profile.csv");QTimer answer;answer.setInterval(20);connect(&answer,&QTimer::timeout,&view,[&]{for(auto *widget:QApplication::topLevelWidgets())if(auto *dialog=qobject_cast<QFileDialog*>(widget)){dialog->selectFile(path);QMetaObject::invokeMethod(dialog,"accept");}});answer.start();view.findChild<QAction*>("exportProfileCsvAction")->trigger();answer.stop();
  QFile output(path);QVERIFY(output.open(QIODevice::ReadOnly));const auto rows=QString::fromUtf8(output.readAll()).trimmed().split('\n');QCOMPARE(rows.first().trimmed(),QString("index,distance,row,col,value"));QCOMPARE(rows.size()-1,int(view.property("pointCount").toULongLong()));
  double last_distance=-1;for(int index=1;index<rows.size();++index){const auto cells=rows[index].split(',');QCOMPARE(cells.size(),5);QCOMPARE(cells[0].toInt(),index-1);const double distance=cells[1].toDouble(),r=cells[2].toDouble(),c=cells[3].toDouble(),value=cells[4].toDouble();QVERIFY(distance>last_distance);last_distance=distance;QVERIFY(std::abs(value-(2*r+3*c+1))<1e-9);}
}

namespace {
bool write_texture_fixture(const QString &path, size_t rows = 12, size_t cols = 16) {
  int file = -1, time = -1, y = -1, x = -1, variable = -1;
  if(nc_create(nc_path_bytes(path).constData(), NC_CLOBBER, &file) != NC_NOERR) return false;
  const auto close = qScopeGuard([&] { nc_close(file); });
  nc_def_dim(file, "time", 3, &time); nc_def_dim(file, "y", rows, &y); nc_def_dim(file, "x", cols, &x);
  const int dimensions[] = {time, y, x};
  nc_def_var(file, "field", NC_DOUBLE, 3, dimensions, &variable);
  const double fill = -9999.; nc_put_att_double(file, variable, "_FillValue", NC_DOUBLE, 1, &fill);
  nc_enddef(file);
  std::vector<double> values(3 * rows * cols, fill);
  for(size_t row = 0; row < rows; ++row) for(size_t col = 0; col < cols; ++col) {
    values[row * cols + col] = col < cols / 2 ? 0 : double((row % 4) * 4 + col % 4);
    values[rows * cols + row * cols + col] = 5;
  }
  return nc_put_var_double(file, variable, values.data()) == NC_NOERR;
}
}

void DropTargetsTest::textureEntropyViewIsIndependent2DImage() {
  QTemporaryDir directory; QVERIFY(directory.isValid());
  const QString file = directory.filePath("texture.nc"); QVERIFY(write_texture_fixture(file));
  MainWindow window; window.resize(1450, 1000); window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
  QCOMPARE(window.read_file(file), 0);
  auto *tree = window.findChild<FileTreeWidget*>(); auto *mdi = window.findChild<QMdiArea*>();
  const auto nodes = tree->findItems("field", Qt::MatchExactly | Qt::MatchRecursive); QCOMPARE(nodes.size(), 1);
  tree->setCurrentItem(nodes.first());
  auto *action = window.findChild<QAction*>("actionTextureEntropyImage"); QVERIFY(action);
  QMenu *view_menu = nullptr;
  for(auto *menu : window.findChildren<QMenu*>()) if(menu->actions().contains(action)) view_menu = menu;
  QVERIFY(view_menu); QVERIFY(view_menu->actions().contains(window.findChild<QAction*>("actionImageView")));
  action->trigger();
  QCOMPARE(mdi->subWindowList().size(), 1); QVERIFY(window.findChildren<RasterView*>().empty());
  QVERIFY(!window.findChild<ComparePanel*>());
  auto *view = qobject_cast<TextureEntropyView*>(mdi->subWindowList().first()->widget()); QVERIFY(view);
  QVERIFY(view->entropy_plane().valid); QVERIFY(!view->source_view());
  QCOMPARE(view->image_for_export().size(), QSize(16, 12));
  auto *canvas = view->findChild<QGraphicsView*>("textureEntropyCanvas"); QVERIFY(canvas);
  QCOMPARE(canvas->sceneRect(), QRectF(0, 0, 16, 12));
  const auto &entropy = view->entropy_plane();
  QCOMPARE(entropy.block_entropy[0], 0.); QCOMPARE(entropy.block_entropy[3], 4.);
  QString error; const auto palettes = HdfColorMap::load_registry(error);
  const auto palette = std::find_if(palettes.begin(), palettes.end(), [](const HdfColorMap::Palette &value) { return value.id == "bids:viridis"; });
  QVERIFY(palette != palettes.end()); HdfColorMap::Mapping mapping; mapping.minimum = 0; mapping.maximum = 4;
  const auto image = view->image_for_export().convertToFormat(QImage::Format_RGB32);
  for(size_t row = 0; row < entropy.rows; ++row) for(size_t col = 0; col < entropy.cols; ++col)
    QCOMPARE(image.pixel(int(col), int(row)), HdfColorMap::map_value(*palette, mapping, entropy.values[row * entropy.cols + col]));
  auto *bar = view->findChild<ColorBar*>(); QVERIFY(bar); QVERIFY(bar->toolTip().contains("bits"));
  QVERIFY(view->findChild<QLabel*>("textureEntropySummary")->text().contains("12 x 16 pixels"));
  const auto computations = view->property("textureComputeCount"), reads = view->property("planeReadCount");
  const auto before = canvas->transform(); view->findChild<QAction*>("textureZoomInAction")->trigger();
  QVERIFY(std::abs(canvas->transform().m11() / before.m11() - 1.25) < 1e-12);
  QCOMPARE(view->property("textureComputeCount"), computations); QCOMPARE(view->property("planeReadCount"), reads);
  QCOMPARE(view->image_for_export().convertToFormat(QImage::Format_RGB32), image);
  const auto png = directory.filePath("texture.png"), csv = directory.filePath("texture.csv");
  QVERIFY(view->export_png(png).isEmpty()); QVERIFY(view->export_csv(csv).isEmpty());
  QCOMPARE(QImage(png).convertToFormat(QImage::Format_RGB32), image);
  QFile exported(csv); QVERIFY(exported.open(QIODevice::ReadOnly)); const auto csv_text = exported.readAll(); exported.close();
  QVERIFY(csv_text.startsWith("row,col,entropy_bits,block_row,block_col,block_entropy_bits\n"));
  QCOMPARE(csv_text.count('\n'), 193);
  QProcess cli; const auto cli_csv = directory.filePath("cli.csv");
  cli.start(QDir(QCoreApplication::applicationDirPath()).filePath("hdf_cli.exe"), {"texture-entropy", file, "field", cli_csv, "0"});
  QVERIFY(cli.waitForFinished(30000)); QCOMPARE(cli.exitCode(), 0);
  QFile cli_output(cli_csv); QVERIFY(cli_output.open(QIODevice::ReadOnly)); QCOMPARE(cli_output.readAll(), csv_text);
  auto *next = window.findChild<QAction*>("actionLayerNext"); QVERIFY(next); next->trigger();
  QCOMPARE(view->layer_index(0), 1); for(double value : view->entropy_plane().values) QCOMPARE(value, 0.);
  next->trigger(); QCOMPARE(view->layer_index(0), 2); QVERIFY(!view->entropy_plane().valid); QVERIFY(view->image_for_export().isNull());
  QVERIFY(!view->findChild<QAction*>("textureExportPngAction")->isEnabled());
  view->set_layer_index(0, 0); QVERIFY(view->entropy_plane().valid);
  auto *start = view->findChild<QLineEdit*>("textureMaskStart"), *end = view->findChild<QLineEdit*>("textureMaskEnd");
  start->setText("0"); end->setText("0"); view->findChild<QPushButton*>("textureApplyMask")->click();
  QVERIFY(view->entropy_plane().valid); QVERIFY(std::isnan(view->entropy_plane().values[0]));
  QCOMPARE(view->image_for_export().pixel(0,0), HdfColorMap::missing_color);
  start->setText("invalid"); view->findChild<QPushButton*>("textureApplyMask")->click();
  QVERIFY(view->image_for_export().isNull()); start->clear(); end->clear(); view->findChild<QPushButton*>("textureApplyMask")->click();
  QVERIFY(view->entropy_plane().valid);
  bool context_found = false;
  QTimer::singleShot(20, &window, [&] {
    for(auto *widget : QApplication::topLevelWidgets()) if(auto *menu = qobject_cast<QMenu*>(widget)) {
      for(auto *entry : menu->actions()) if(entry->text() == "Texture Entropy Image") context_found = true;
      if(menu->isVisible()) menu->close();
    }
  });
  const auto rect = tree->visualItemRect(nodes.first());
  QVERIFY(QMetaObject::invokeMethod(tree, "show_context_menu", Q_ARG(QPoint, rect.center())));
  QVERIFY(context_found);
  window.close_all_files(); QCOMPARE(mdi->subWindowList().size(), 0);
}

void DropTargetsTest::textureEntropySnapshotFollowsMaskAndSourceLifetime() {
  QTemporaryDir directory; const auto filename = directory.filePath("source.nc"); QVERIFY(write_texture_fixture(filename));
  MainWindow window; window.resize(1450, 1000); window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
  QCOMPARE(window.read_file(filename), 0);
  auto *tree = window.findChild<FileTreeWidget*>(); auto *mdi = window.findChild<QMdiArea*>();
  const auto nodes = tree->findItems("field", Qt::MatchExactly | Qt::MatchRecursive); QCOMPARE(nodes.size(), 1);
  tree->setCurrentItem(nodes.first()); window.findChild<QAction*>("actionImageView")->trigger();
  auto *source = qobject_cast<RasterView*>(mdi->subWindowList().first()->widget()); QVERIFY(source);
  QTRY_VERIFY(source->plane_snapshot().values);
  window.findChild<QAction*>("actionTextureEntropyImage")->trigger();
  QCOMPARE(mdi->subWindowList().size(), 2);
  auto *view = qobject_cast<TextureEntropyView*>(mdi->subWindowList().last()->widget()); QVERIFY(view);
  QCOMPARE(view->source_view(), source); QVERIFY(view->entropy_plane().valid);
  QVERIFY(!view->property("planeReadCount").isValid());
  const auto computed = view->property("textureComputeCount");
  source->findChild<QAction*>("zoomInAction")->trigger();
  source->findChild<QCheckBox*>("reverseColorMap")->setChecked(true);
  QCOMPARE(view->property("textureComputeCount"), computed);
  auto *mask_start = source->findChild<QLineEdit*>("maskStart"), *mask_end = source->findChild<QLineEdit*>("maskEnd");
  mask_start->setText("0"); mask_end->setText("0"); QMetaObject::invokeMethod(mask_end, "editingFinished");
  QTRY_VERIFY(std::isnan(view->entropy_plane().values[0])); QCOMPARE(view->image_for_export().pixel(0,0), HdfColorMap::missing_color);
  QCOMPARE(view->property("sourceRevision").toULongLong(), source->plane_snapshot().revision);
  mask_start->clear(); mask_end->clear(); QMetaObject::invokeMethod(mask_end,"editingFinished");
  window.step_layer(LayerStep::Next); QCOMPARE(source->layer_index(0), 1);
  for(double value : view->entropy_plane().values) QCOMPARE(value, 0.);
  source->set_sync_out_of_range(true, "Outside test sequence"); QVERIFY(!view->entropy_plane().valid);
  QVERIFY(view->image_for_export().isNull()); source->set_sync_out_of_range(false, {}); QVERIFY(view->entropy_plane().valid);
  source->set_layer_index(0, 0); QVERIFY(view->entropy_plane().valid);
  mdi->subWindowList().first()->close(); QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
  QVERIFY(!view->source_view()); QVERIFY(view->image_for_export().isNull());
  QVERIFY(view->findChild<QLabel*>("textureEntropySummary")->text().contains("closed"));
  QVERIFY(!window.findChild<QAction*>("actionLayerNext")->isEnabled());
  QVERIFY(!window.findChild<QAction*>("actionLayerPlay")->isEnabled());
  window.close_all_files(); QCOMPARE(mdi->subWindowList().size(), 0);

  // Save both real forecast entropy images and raw values for independent PNG/CSV reconciliation.
  const QString output = qEnvironmentVariable("HDFSTUDIO_TEST_LOG_DIR");
  for(const auto &key : {"HDFSTUDIO_FORECAST_ANCHOR", "HDFSTUDIO_FORECAST_TARGET"}) {
    const auto path = qEnvironmentVariable(key); if(path.isEmpty() || output.isEmpty()) continue;
    NcReader reader(path); NcPlane plane;
    const QString variable = qEnvironmentVariable("HDFSTUDIO_FORECAST_VARIABLE", "TMP_2maboveground");
    QVERIFY(reader.read_plane(variable.toUtf8().toStdString(), {0}, plane).isEmpty());
    TextureEntropyView real(path.toLocal8Bit().toStdString(), variable.toUtf8().toStdString(), {1,hsize_t(plane.rows),hsize_t(plane.cols)});
    QVERIFY(real.entropy_plane().valid);
    const QString prefix = QDir(output).filePath(qEnvironmentVariable("HDFSTUDIO_TEST_CONFIG") + "-" + QString::fromLatin1(key));
    QVERIFY(real.export_png(prefix + ".png").isEmpty()); QVERIFY(real.export_csv(prefix + ".csv").isEmpty());
  }
}

void DropTargetsTest::textureEntropyBudgetAndInvalidRecovery() {
  QTemporaryDir directory; const auto filename = directory.filePath("budget.nc"); QVERIFY(write_texture_fixture(filename, 100, 100));
  const auto previous = qgetenv("HDFSTUDIO_RASTER_BUDGET_MIB"); qputenv("HDFSTUDIO_RASTER_BUDGET_MIB", "1");
  const auto restore = qScopeGuard([previous] { if(previous.isNull()) qunsetenv("HDFSTUDIO_RASTER_BUDGET_MIB"); else qputenv("HDFSTUDIO_RASTER_BUDGET_MIB", previous); });
  {
    TextureEntropyView first(filename.toLocal8Bit().toStdString(), "field", {3,100,100}); QVERIFY(first.entropy_plane().valid);
    TextureEntropyView blocked(filename.toLocal8Bit().toStdString(), "field", {3,100,100}); QVERIFY(blocked.image_for_export().isNull());
    QVERIFY(blocked.findChild<QLabel*>("textureEntropySummary")->text().contains("memory budget"));
    QVERIFY(!blocked.property("planeReadCount").isValid());
  }
  { TextureEntropyView recovered(filename.toLocal8Bit().toStdString(), "field", {3,100,100}); QVERIFY(recovered.entropy_plane().valid); }
  {
    TextureEntropyView empty(filename.toLocal8Bit().toStdString(), "field", {0,100,100}); QVERIFY(!empty.entropy_plane().valid);
    QCOMPARE(empty.layer_count(0), 0); LayerPlayback playback; QVERIFY(!playback.start(&empty, 0));
    TextureEntropyView one(filename.toLocal8Bit().toStdString(), "field", {1,100,100}); QVERIFY(one.entropy_plane().valid);
    QVERIFY(!playback.start(&one, 0));
  }
  QVERIFY(reserve_raster(10000));
  {
    TextureEntropyView animated(filename.toLocal8Bit().toStdString(), "field", {2,100,100});
    LayerPlayback playback; QVERIFY(playback.start(&animated,0));
    QTRY_COMPARE(animated.layer_index(0),1); QVERIFY(!playback.is_playing());
    for(double value : animated.entropy_plane().values) QCOMPARE(value,0.);
  }
  // The host is destroyed with both views open: source destruction emits derived layer_changed.
  {
    qunsetenv("HDFSTUDIO_RASTER_BUDGET_MIB");
    MainWindow host; host.resize(1200,850); host.show();
    QCOMPARE(host.read_file(filename),0);
    auto *tree=host.findChild<FileTreeWidget*>();
    const auto fields=tree->findItems("field",Qt::MatchExactly|Qt::MatchRecursive); QCOMPARE(fields.size(),1);
    tree->setCurrentItem(fields.first());host.findChild<QAction*>("actionImageView")->trigger();
    QTRY_VERIFY(!host.findChildren<RasterView*>().empty() && host.findChild<RasterView*>()->plane_snapshot().values);
    host.findChild<QAction*>("actionTextureEntropyImage")->trigger();
    QVERIFY(host.findChild<TextureEntropyView*>()->entropy_plane().valid);
    // No close_all_files(): exercise actual main-window destruction and outgoing callback cleanup.
  }
}

void DropTargetsTest::textureEntropyBlocksAndInterpolation() {
  const auto constant = nc_texture_entropy(std::vector<double>(81, 42), 9, 9);
  QVERIFY(constant.valid); QCOMPARE(constant.block_rows, size_t(3)); QCOMPARE(constant.block_cols, size_t(3));
  for(double value : constant.values) QCOMPARE(value, 0.);
  std::vector<double> values(32);
  for(size_t row = 0; row < 4; ++row) for(size_t col = 0; col < 8; ++col)
    values[row * 8 + col] = col < 4 ? 0 : double(row * 4 + col - 4);
  const auto texture = nc_texture_entropy(values, 4, 8);
  QVERIFY(texture.valid); QCOMPARE(texture.block_entropy, (std::vector<double>{0,4}));
  const double expected[] = {0,0,.5,1.5,2.5,3.5,4,4};
  for(size_t row = 0; row < 4; ++row) for(size_t col = 0; col < 8; ++col)
    QCOMPARE(texture.values[row * 8 + col], expected[col]);
  std::vector<double> binary(16); for(size_t index = 0; index < 16; ++index) binary[index] = double(index % 2);
  QCOMPARE(nc_texture_entropy(binary, 4, 4).block_entropy[0], 1.);
  auto rearranged = binary; std::sort(rearranged.begin(), rearranged.end());
  QCOMPARE(nc_texture_entropy(binary,4,4).values, nc_texture_entropy(rearranged,4,4).values);
  values[0] = NAN; values[1] = INFINITY;
  const auto gaps = nc_texture_entropy(values, 4, 8);
  QCOMPARE(gaps.excluded_count, size_t(2)); QVERIFY(std::isnan(gaps.values[0])); QVERIFY(std::isnan(gaps.values[1]));
  for(size_t row = 0; row < 4; ++row) for(size_t col = 4; col < 8; ++col) values[row * 8 + col] = NAN;
  const auto empty_block = nc_texture_entropy(values,4,8);
  QVERIFY(std::isnan(empty_block.block_entropy[1])); QCOMPARE(empty_block.values[3], 0.);
  std::vector<double> edge_values(25); std::iota(edge_values.begin(), edge_values.end(), 0.);
  const auto edges = nc_texture_entropy(edge_values,5,5);
  QCOMPARE(edges.block_entropy, (std::vector<double>{4,2,2,0})); QCOMPARE(edges.values.back(),0.);
  QCOMPARE(edges.values[2 * 5 + 2],3.2); // Actual centers 1.5 and 4, rather than padded blocks.
  for(double &value : binary) value = value ? 1e308 : -1e308;
  QCOMPARE(nc_texture_entropy(binary,4,4).block_entropy[0],1.);
  for(size_t index = 0; index < 16; ++index) binary[index] = 1e308 + double(index) * 2e292;
  QCOMPARE(nc_texture_entropy(binary,4,4).block_entropy[0],4.);
  QVERIFY(!nc_texture_entropy({},0,4).valid); QVERIFY(!nc_texture_entropy({1},4,4).valid);
  QVERIFY(!nc_texture_entropy({1},std::numeric_limits<size_t>::max(),2).valid);
  QVERIFY(!nc_texture_entropy(std::vector<double>(16,NAN),4,4).valid);
  QCOMPARE(nc_texture_entropy({3},1,1).values[0],0.);
  // An asymmetric 2D pattern catches transposed rows/columns and one-axis-only interpolation.
  std::vector<double> pattern(64, 0);
  for(size_t row = 0; row < 8; ++row) for(size_t col = 0; col < 8; ++col) {
    if(row < 4 && col >= 4) pattern[row * 8 + col] = double((col - 4) % 2);
    if(row >= 4 && col < 4) pattern[row * 8 + col] = double(row - 4);
    if(row >= 4 && col >= 4) pattern[row * 8 + col] = double((row - 4) * 4 + col - 4);
  }
  const auto two_dimensional = nc_texture_entropy(pattern,8,8);
  QCOMPARE(two_dimensional.block_entropy,(std::vector<double>{0,1,2,4}));
  QCOMPARE(two_dimensional.values[2*8+2],.390625);
  QCOMPARE(two_dimensional.values[6*8+6],4.);
}

void DropTargetsTest::imageMetricsEntropyAccuracy() {
  const auto constant = nc_gray_entropy(std::vector<double>(100,42));
  QVERIFY(constant.valid); QCOMPARE(constant.entropy_bits,0.0);
  std::vector<double> uniform(256); std::iota(uniform.begin(),uniform.end(),0.0);
  const auto entropy = nc_gray_entropy(uniform);
  QVERIFY(entropy.valid); QVERIFY(std::abs(entropy.entropy_bits-8) < 1e-12);
  QCOMPARE(entropy.normalized_entropy,1.0);
  const auto missing = nc_gray_entropy({0,1,NAN,INFINITY,-INFINITY},2);
  QCOMPARE(missing.valid_count,size_t(2)); QCOMPARE(missing.missing_count,size_t(3));
  QCOMPARE(missing.entropy_bits,1.0);
  QVERIFY(!nc_gray_entropy({NAN}).valid); QVERIFY(!nc_gray_entropy({}).valid);
  QVERIFY(!nc_gray_entropy(uniform,1).valid); QVERIFY(!nc_gray_entropy(uniform,65537).valid);
  QVERIFY(!nc_gray_entropy(uniform,256,true,1,1).valid);
  QCOMPARE(nc_gray_entropy({-20,-10,10,20},2,true,-1,1).entropy_bits,1.0);
  QCOMPARE(nc_gray_entropy({-1e308,1e308},2).entropy_bits,1.0);
  const double step=std::nextafter(1e12,INFINITY)-1e12;
  for(size_t i=0;i<uniform.size();++i)uniform[i]=1e12+double(i)*step;
  QCOMPARE(nc_gray_entropy(uniform).entropy_bits,8.0);
}

void DropTargetsTest::imageMetricsEntropyAndPairAccuracy() {
  std::vector<double> a={0,1,2,3}, b={3,5,7,9};
  auto pair=nc_pair_metrics(a,2,2,b,2,2);
  QVERIFY(pair.valid && pair.r_valid); QVERIFY(std::abs(pair.pearson_r-1)<1e-12);
  b={9,7,5,3};pair=nc_pair_metrics(a,2,2,b,2,2);
  QVERIFY(std::abs(pair.pearson_r+1)<1e-12);
  b={-1,3,0,4};pair=nc_pair_metrics(a,2,2,b,2,2);
  QCOMPARE(pair.bias,0.0);QVERIFY(std::abs(pair.mae-1.5)<1e-12);QVERIFY(std::abs(pair.rmse-std::sqrt(2.5))<1e-12);
  pair=nc_pair_metrics({1,NAN,3,INFINITY},2,2,{2,8,NAN,4},2,2);
  QVERIFY(pair.valid);QVERIFY(!pair.r_valid);QVERIFY(std::isnan(pair.pearson_r));
  QCOMPARE(pair.valid_pairs,size_t(1));QCOMPARE(pair.excluded_pairs,size_t(3));QCOMPARE(pair.rmse,1.0);
  pair=nc_pair_metrics({2,2},1,2,{3,4},1,2);QVERIFY(pair.valid);QVERIFY(!pair.r_valid);
  QVERIFY(!nc_pair_metrics(a,2,2,a,1,4).valid);
  QVERIFY(!nc_pair_metrics(a,2,3,a,2,3).valid);
  QVERIFY(!nc_pair_metrics({NAN},1,1,{0},1,1).valid);
  pair=nc_pair_metrics({1e12,1e12+1,1e12+2},1,3,{1e12+2,1e12+4,1e12+6},1,3);
  QVERIFY(pair.r_valid);QVERIFY(std::abs(pair.pearson_r-1)<1e-12);
  pair=nc_pair_metrics({-1e308,1e308},1,2,{-1e308,1e308},1,2);
  QVERIFY(pair.r_valid);QVERIFY(std::abs(pair.pearson_r-1)<1e-12);QCOMPARE(pair.rmse,0.0);
  pair=nc_pair_metrics({1e308,1e-100,2e-100,3e-100},1,4,{1e308,2e-100,3e-100,4e-100},1,4);
  QVERIFY(pair.valid);QVERIFY(std::abs(pair.rmse/(std::sqrt(.75)*1e-100)-1)<1e-12);
  QVERIFY(std::abs(pair.mae/7.5e-101-1)<1e-12);QVERIFY(std::abs(pair.bias/7.5e-101-1)<1e-12);
}

void DropTargetsTest::histogramOrRasterShowsGrayEntropy() {
  QTemporaryDir dir;const auto filename=dir.filePath("entropy.nc");
  int file=-1,t=-1,y=-1,x=-1,var=-1;
  QCOMPARE(nc_create(nc_path_bytes(filename).constData(),NC_CLOBBER,&file),NC_NOERR);
  nc_def_dim(file,"t",3,&t);nc_def_dim(file,"y",16,&y);nc_def_dim(file,"x",16,&x);
  int dimensions[]={t,y,x};nc_def_var(file,"field",NC_DOUBLE,3,dimensions,&var);nc_enddef(file);
  std::vector<double> data(768,NAN);
  std::iota(data.begin(),data.begin()+256,0.0);std::fill(data.begin()+256,data.begin()+512,42.0);
  QCOMPARE(nc_put_var_double(file,var,data.data()),NC_NOERR);nc_close(file);
  HistogramView histogram(filename.toLocal8Bit().toStdString(),"/field",{3,16,16});
  auto *summary=histogram.findChild<QLabel*>("histogramSummary");QVERIFY(summary);
  QVERIFY(summary->text().contains("8.0000 bits"));QCOMPARE(histogram.property("planeReadCount").toULongLong(),qulonglong(1));
  for(auto *spin:histogram.findChildren<QSpinBox*>()) {
    if(spin->accessibleName()=="Bins") spin->setValue(80);
    if(spin->accessibleName()=="Sample 1 in N") spin->setValue(1);
  }
  QVERIFY(summary->text().contains("8.0000 bits"));QCOMPARE(histogram.property("planeReadCount").toULongLong(),qulonglong(1));
  histogram.set_layer_index(0,1);QVERIFY(summary->text().contains("0.0000 bits"));
  histogram.set_layer_index(0,2);QVERIFY(summary->text().contains("Whole-plane histogram entropy (256 bins): N/A"));
  QCOMPARE(histogram.property("planeReadCount").toULongLong(),qulonglong(3));
  RasterView raster(filename.toLocal8Bit().toStdString(),"/field",{3,16,16});raster.resize(900,600);raster.show();
  QTRY_VERIFY(raster.plane_snapshot().values);
  const auto original=raster.plane_snapshot();QCOMPARE(original.values->size(),size_t(256));QCOMPARE(original.rows,size_t(16));
  QCOMPARE(raster.property("entropyBits").toDouble(),8.0);
  auto *palette=raster.findChild<QComboBox*>("paletteSelector");
  if(!palette) for(auto *combo:raster.findChildren<QComboBox*>()) if(combo->count()>=18) palette=combo;
  QVERIFY(palette);palette->setCurrentIndex((palette->currentIndex()+1)%palette->count());
  QCOMPARE(raster.plane_snapshot().values,original.values);QCOMPARE(raster.plane_snapshot().revision,original.revision);
  QCOMPARE(raster.property("planeReadCount").toULongLong(),qulonglong(1));
  raster.set_layer_index(0,1);QCOMPARE(raster.property("entropyBits").toDouble(),0.0);
  QVERIFY(raster.plane_snapshot().revision>original.revision);QCOMPARE((*original.values)[255],255.0);
  raster.set_layer_index(0,2);QVERIFY(!raster.property("entropyBits").isValid());
  QVERIFY(raster.findChild<QLabel*>("rasterEntropySummary")->text().contains("N/A"));
  QCOMPARE(raster.property("planeReadCount").toULongLong(),qulonglong(3));
}

void DropTargetsTest::workspaceSyncAnchorLagAndOutOfRange() {
  QTemporaryDir dir;const auto filename=dir.filePath("sync.nc");
  int file=-1,t=-1,y=-1,x=-1,var=-1;
  QCOMPARE(nc_create(nc_path_bytes(filename).constData(),NC_CLOBBER,&file),NC_NOERR);
  nc_def_dim(file,"t",10,&t);nc_def_dim(file,"y",16,&y);nc_def_dim(file,"x",16,&x);
  int dimensions[]={t,y,x};nc_def_var(file,"field",NC_DOUBLE,3,dimensions,&var);nc_enddef(file);
  std::vector<double> data(2560);std::iota(data.begin(),data.end(),0.0);
  QCOMPARE(nc_put_var_double(file,var,data.data()),NC_NOERR);nc_close(file);
  auto *a=new RasterView(filename.toLocal8Bit().toStdString(),"/field",{10,16,16});
  auto *b=new RasterView(filename.toLocal8Bit().toStdString(),"/field",{10,16,16});
  auto *c=new RasterView(filename.toLocal8Bit().toStdString(),"/field",{10,16,16});
  auto cleanup=qScopeGuard([&]{delete a;delete b;delete c;});
  for(auto *view:{a,b,c}) {view->resize(800,500);view->show();}
  QTRY_VERIFY(a->plane_snapshot().values && b->plane_snapshot().values && c->plane_snapshot().values);
  WorkspaceSyncController sync;
  QSignalSpy metrics(&sync,&WorkspaceSyncController::metrics_updated);
  sync.set_anchor(a);SyncBinding binding;binding.view=b;binding.lag_offset=2;sync.add_or_update_follower(binding);
  binding.view=c;binding.lag_offset=-1;sync.add_or_update_follower(binding);
  a->set_layer_index(0,3);QCOMPARE(b->layer_index(0),5);QCOMPARE(c->layer_index(0),2);
  QCOMPARE((*b->plane_snapshot().values)[0],1280.0);
  const auto previous=b->plane_snapshot();
  a->set_layer_index(0,8);QVERIFY(b->plane_snapshot().out_of_range);QVERIFY(!b->plane_snapshot().values);
  QCOMPARE(b->layer_index(0),5);QVERIFY(b->image_for_export().isNull());
  QVERIFY(b->findChild<QGraphicsView*>()->scene()->items().size()>0);
  a->set_layer_index(0,5);QCOMPARE(b->layer_index(0),7);QVERIFY(!b->plane_snapshot().out_of_range);
  QVERIFY(b->plane_snapshot().values);QCOMPARE((*b->plane_snapshot().values)[0],1792.0);
  QCOMPARE((*previous.values)[0],1280.0);
  a->set_layer_index(0,0);QVERIFY(c->plane_snapshot().out_of_range);QCOMPARE(b->layer_index(0),2);
  binding.view=b;binding.lag_offset=0;sync.add_or_update_follower(binding);QCOMPARE(b->layer_index(0),0);
  a->findChild<QAction*>("zoomInAction")->trigger();
  a->findChild<QAction*>("flipHorizontalAction")->setChecked(true);
  QCOMPARE(b->viewport_state().transform,a->viewport_state().transform);QVERIFY(b->viewport_state().flip_h);
  auto state=a->viewport_state();state.transform=QTransform::fromScale(-100,100);state.h_ratio=.7;state.v_ratio=.2;
  a->apply_viewport_state(state);
  QTRY_COMPARE(b->viewport_state().transform,a->viewport_state().transform);
  QVERIFY(std::abs(b->viewport_state().h_ratio-a->viewport_state().h_ratio)<.001);
  QVERIFY(std::abs(b->viewport_state().v_ratio-a->viewport_state().v_ratio)<.001);
  // Target actions never broadcast back into the anchor or other followers.
  const auto anchor_transform=a->viewport_state().transform;b->findChild<QAction*>("zoomInAction")->trigger();
  QCOMPARE(a->viewport_state().transform,anchor_transform);
  auto *selector=a->findChild<QComboBox*>("colorMapSelector");
  const auto target_palette=b->color_state().palette.id;selector->setCurrentIndex((selector->currentIndex()+1)%selector->count());
  QCOMPARE(b->color_state().palette.id,target_palette);
  binding.sync_colormap=true;sync.add_or_update_follower(binding);
  QCOMPARE(b->color_state().palette.id,a->color_state().palette.id);
  QCOMPARE(b->color_state().mapping.minimum,a->color_state().mapping.minimum);
  QCOMPARE(b->color_state().mapping.maximum,a->color_state().mapping.maximum);
  auto tiny_colors=a->color_state();tiny_colors.mapping.minimum=1e-12;tiny_colors.mapping.maximum=2e-12;
  a->apply_color_state(tiny_colors);
  QCOMPARE(b->color_state().mapping.minimum,1e-12);QCOMPARE(b->color_state().mapping.maximum,2e-12);
  binding.lag_offset=2;sync.add_or_update_follower(binding);
  for(int i=0;i<30;++i) a->set_layer_index(0,i%10);
  QVERIFY(metrics.count()<600);
  delete c;c=nullptr;QCOMPARE(sync.followers().size(),size_t(1));
  delete a;a=nullptr;QVERIFY(!sync.anchor());QVERIFY(sync.followers().empty());
  QVERIFY(!b->plane_snapshot().out_of_range);QVERIFY(b->plane_snapshot().values);
}

void DropTargetsTest::imageMetricsResidualAccuracy() {
  const auto residual=nc_pair_residual({1,2,NAN,4},2,2,{3,1,8,4},2,2);
  QVERIFY(residual.valid);QCOMPARE(residual.rows,size_t(2));QCOMPARE(residual.cols,size_t(2));
  QCOMPARE(residual.valid_pairs,size_t(3));QCOMPARE(residual.excluded_pairs,size_t(1));
  QCOMPARE(residual.values[0],2.0);QCOMPARE(residual.values[1],-1.0);QVERIFY(std::isnan(residual.values[2]));
  QCOMPARE(residual.values[3],0.0);QCOMPARE(residual.absolute_max,2.0);
  const auto zero=nc_pair_residual({3,4},1,2,{3,4},1,2);
  QVERIFY(zero.valid);QCOMPARE(zero.absolute_max,0.0);
  QVERIFY(!nc_pair_residual({1,2},1,2,{1,2},2,1).valid);
  QVERIFY(!nc_pair_residual({NAN},1,1,{0},1,1).valid);
  QVERIFY(!nc_pair_residual({-1e308},1,1,{1e308},1,1).valid);
}

void DropTargetsTest::comparePanelComputesLivePairMetricsAndExportsCsv() {
  QTemporaryDir dir;const auto filename=dir.filePath("compare.nc");
  int file=-1,t=-1,y=-1,x=-1,a_var=-1,b_var=-1,small_var=-1;
  QCOMPARE(nc_create(nc_path_bytes(filename).constData(),NC_CLOBBER,&file),NC_NOERR);
  nc_def_dim(file,"t",10,&t);nc_def_dim(file,"y",2,&y);nc_def_dim(file,"x",3,&x);
  int dimensions[]={t,y,x};nc_def_var(file,"a",NC_DOUBLE,3,dimensions,&a_var);nc_def_var(file,"b",NC_DOUBLE,3,dimensions,&b_var);
  int small_dimensions[]={t,x};nc_def_var(file,"small",NC_DOUBLE,2,small_dimensions,&small_var);nc_enddef(file);
  std::vector<double> av(60),bv(60);
  for(int k=0;k<10;++k)for(int i=0;i<6;++i){av[size_t(k*6+i)]=k*10+i;bv[size_t(k*6+i)]=i==5?NAN:2*(k*10+i)+3;}
  nc_put_var_double(file,a_var,av.data());nc_put_var_double(file,b_var,bv.data());nc_close(file);
  MainWindow window;window.resize(1500,900);window.show();
  auto *mdi=window.findChild<QMdiArea*>();QVERIFY(mdi);
  auto *a=new RasterView(filename.toLocal8Bit().toStdString(),"/a",{10,2,3});
  auto *b=new RasterView(filename.toLocal8Bit().toStdString(),"/b",{10,2,3});
  auto *c=new RasterView(filename.toLocal8Bit().toStdString(),"/a",{10,2,3});
  for(auto *view:{a,b,c}){auto *sub=mdi->addSubWindow(view);sub->setAttribute(Qt::WA_DeleteOnClose);sub->show();}
  QTRY_VERIFY(a->plane_snapshot().values && b->plane_snapshot().values && c->plane_snapshot().values);
  auto *action=window.findChild<QAction*>("actionCompareMetrics");QVERIFY(action);action->trigger();
  auto *panel=window.findChild<ComparePanel*>();QVERIFY(panel);QVERIFY(panel->isVisible());
  auto *text=panel->findChild<QPlainTextEdit*>("compareMetricsText");
  QVERIFY(!text->toPlainText().contains("Entropy", Qt::CaseInsensitive));
  QVERIFY(!panel->comparison_csv().contains("entropy"));
  auto *lag=panel->findChild<QSpinBox*>("compareLagSpin");
  auto *layer=panel->findChild<QSpinBox*>("compareAnchorLayerSpin");
  auto *target=panel->findChild<QComboBox*>("compareTargetCombo");
  auto *sync=window.findChild<WorkspaceSyncController*>();QVERIFY(sync);
  QTRY_VERIFY(text->toPlainText().contains("Pearson r: 1"));
  lag->setValue(2);layer->setValue(3);
  QTRY_COMPARE(b->layer_index(0),5);QTRY_VERIFY(text->toPlainText().contains("Anchor[3] <-> Target[5] (lag +2"));
  QVERIFY(text->toPlainText().contains("Valid pairs: 5"));QVERIFY(text->toPlainText().contains("Excluded pairs: 1"));
  QVERIFY(text->toPlainText().contains("Bias (Target - Anchor): 75"));
  const double expected_rmse=std::sqrt((73.*73+74.*74+75.*75+76.*76+77.*77)/5);
  QVERIFY(text->toPlainText().contains("RMSE: "+QString::number(expected_rmse,'g',15)));
  const auto a_reads=a->property("planeReadCount"),b_reads=b->property("planeReadCount");
  panel->findChild<QPushButton*>("compareResidualButton")->click();
  auto *residual=panel->findChild<ResidualView*>();QVERIFY(residual);
  QTRY_VERIFY(residual->residual().valid);QCOMPARE(residual->residual().values[0],73.0);QVERIFY(std::isnan(residual->residual().values[5]));
  QVERIFY(!residual->image_for_export().isNull());QCOMPARE(residual->image_for_export().size(),QSize(3,2));
  panel->findChild<QPushButton*>("compareCopyButton")->click();QCOMPARE(QApplication::clipboard()->text(),text->toPlainText());
  const auto output=dir.filePath("comparison.csv");QVERIFY(panel->export_csv(output).isEmpty());
  QFile csv(output);QVERIFY(csv.open(QIODevice::ReadOnly));QCOMPARE(QString::fromUtf8(csv.readAll()),panel->comparison_csv());
  QVERIFY(panel->comparison_csv().contains("anchor_revision"));QVERIFY(panel->comparison_csv().contains("\"75\""));
  QCOMPARE(a->property("planeReadCount"),a_reads);QCOMPARE(b->property("planeReadCount"),b_reads);
  const auto computations=panel->property("metricsComputeCount");
  a->findChild<QAction*>("zoomInAction")->trigger();QCoreApplication::processEvents();
  QCOMPARE(panel->property("metricsComputeCount"),computations);
  lag->setValue(10000);QTRY_VERIFY(text->toPlainText().contains("Status: Out of range"));
  QVERIFY(text->toPlainText().contains("RMSE: N/A"));QVERIFY(!residual->residual().valid);QVERIFY(residual->image_for_export().isNull());
  lag->setValue(2);QTRY_VERIFY(residual->residual().valid);QCOMPARE(residual->residual().values[0],73.0);
  panel->findChild<QSlider*>("compareAnchorLayerSlider")->setValue(4);QTRY_COMPARE(b->layer_index(0),6);
  QTRY_COMPARE(residual->residual().values[0],83.0);
  // Switching selected target preserves the earlier follower and its independent lag.
  target->setCurrentIndex(3);lag->setValue(-1);QTRY_COMPARE(c->layer_index(0),3);QCOMPARE(sync->followers().size(),size_t(2));
  target->setCurrentIndex(2);QCOMPARE(lag->value(),2);
  layer->setValue(5);QTRY_COMPARE(b->layer_index(0),7);QTRY_COMPARE(c->layer_index(0),4);
  auto *mask_start=b->findChild<QLineEdit*>("maskStart");auto *mask_end=b->findChild<QLineEdit*>("maskEnd");
  QVERIFY(mask_start && mask_end);mask_start->setText("143");mask_end->setText("143");QMetaObject::invokeMethod(mask_end,"editingFinished",Qt::DirectConnection);
  QTRY_VERIFY(text->toPlainText().contains("Valid pairs: 4"));QTRY_VERIFY(std::isnan(residual->residual().values[0]));
  // Remove one follower, then anchor destruction clears all bindings and stale output.
  mdi->subWindowList()[2]->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
  QTRY_COMPARE(sync->followers().size(),size_t(1));
  mdi->subWindowList()[0]->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
  QTRY_VERIFY(!sync->anchor());QTRY_VERIFY(text->toPlainText().contains("Select two different image views"));
  QVERIFY(sync->followers().empty());QVERIFY(!residual->residual().valid);
  // A 2-D target with a different shape must invalidate metrics and the derived map.
  auto *small=new RasterView(filename.toLocal8Bit().toStdString(),"/small",{10,3});
  auto *small_sub=mdi->addSubWindow(small);small_sub->setAttribute(Qt::WA_DeleteOnClose);small_sub->show();
  QTRY_VERIFY(small->plane_snapshot().values);
  panel->set_views({b,small});
  panel->findChild<QComboBox*>("compareAnchorCombo")->setCurrentIndex(1);
  target->setCurrentIndex(2);panel->findChild<QCheckBox*>("compareSyncLayer")->setChecked(false);
  QTRY_VERIFY(text->toPlainText().contains("Plane shapes do not match"));
  QVERIFY(text->toPlainText().contains("RMSE: N/A"));QVERIFY(residual->image_for_export().isNull());
  QVERIFY(!panel->findChild<QPushButton*>("compareResidualButton")->isEnabled());
}

void DropTargetsTest::forecastPairResidualMatchesEveryDecodedPixel() {
  const auto a_path=qEnvironmentVariable("HDFSTUDIO_FORECAST_ANCHOR"),b_path=qEnvironmentVariable("HDFSTUDIO_FORECAST_TARGET");
  if(a_path.isEmpty()||b_path.isEmpty())QSKIP("HDFSTUDIO_FORECAST_ANCHOR/TARGET not configured");
  const auto variable=qEnvironmentVariable("HDFSTUDIO_FORECAST_VARIABLE","TMP_2maboveground");
  const auto log=qEnvironmentVariable("HDFSTUDIO_TEST_LOG_DIR");QVERIFY(!log.isEmpty());
  const auto config=qEnvironmentVariable("HDFSTUDIO_TEST_CONFIG");QVERIFY(!config.isEmpty());
  RasterView a(a_path.toLocal8Bit().toStdString(),variable.toStdString(),{480,190,384});
  RasterView b(b_path.toLocal8Bit().toStdString(),variable.toStdString(),{180,190,384});
  a.resize(800,550);b.resize(800,550);a.show();b.show();
  QTRY_VERIFY(a.plane_snapshot().values && b.plane_snapshot().values);
  WorkspaceSyncController sync;sync.set_anchor(&a);SyncBinding binding;binding.view=&b;binding.lag_offset=-1;sync.add_or_update_follower(binding);
  ResidualView view;view.show();
  for(int index:{1,91,180}) {
    a.set_layer_index(0,index);QCOMPARE(b.layer_index(0),index-1);QVERIFY(!b.plane_snapshot().out_of_range);
    const auto ap=a.plane_snapshot(),bp=b.plane_snapshot();
    QCOMPARE(ap.units,QString("K"));QCOMPARE(bp.units,QString("K"));
    view.update_planes(ap,bp,QString("Anchor[%1] Target[%2] lag -1").arg(index).arg(index-1));
    QVERIFY(view.residual().valid);QCOMPARE(view.residual().values.size(),size_t(190*384));
    for(size_t i=0;i<view.residual().values.size();++i) {
      if(std::isfinite((*ap.values)[i])&&std::isfinite((*bp.values)[i]))QCOMPARE(view.residual().values[i],(*bp.values)[i]-(*ap.values)[i]);
      else QVERIFY(std::isnan(view.residual().values[i]));
    }
    const QString stem=QDir(log).filePath(QString("forecast-%1-%2").arg(config).arg(index));
    QFile csv(stem+".csv");QVERIFY(csv.open(QIODevice::WriteOnly));
    QTextStream output(&csv);output.setRealNumberPrecision(17);
    for(size_t row=0;row<190;++row) {
      for(size_t col=0;col<384;++col){if(col)output<<',';output<<view.residual().values[row*384+col];}output<<'\n';
    }
    output.flush();csv.close();QVERIFY(view.image_for_export().save(stem+".png"));
  }
  a.set_layer_index(0,181);QVERIFY(b.plane_snapshot().out_of_range);
  view.update_planes(a.plane_snapshot(),b.plane_snapshot(),"out of range");QVERIFY(view.image_for_export().isNull());
}

void DropTargetsTest::linkedLayerToolbarNavigatesAnchorFromEitherImage() {
  QTemporaryDir dir;const auto filename=dir.filePath("navigation.nc");
  int file=-1,t=-1,y=-1,x=-1,var=-1;
  QCOMPARE(nc_create(nc_path_bytes(filename).constData(),NC_CLOBBER,&file),NC_NOERR);
  nc_def_dim(file,"time",8,&t);nc_def_dim(file,"y",2,&y);nc_def_dim(file,"x",3,&x);
  int dims[]={t,y,x};nc_def_var(file,"field",NC_DOUBLE,3,dims,&var);nc_enddef(file);
  std::vector<double> values(48);for(size_t i=0;i<values.size();++i)values[i]=double(i);
  nc_put_var_double(file,var,values.data());nc_close(file);
  MainWindow window;window.resize(1400,850);window.show();
  auto *mdi=window.findChild<QMdiArea*>();auto *bar=window.findChild<QToolBar*>("mainToolBar");
  QVERIFY(mdi);QVERIFY(bar);QVERIFY(QTest::qWaitForWindowExposed(&window));
  auto *a=new RasterView(filename.toLocal8Bit().toStdString(),"/field",{8,2,3});
  auto *b=new RasterView(filename.toLocal8Bit().toStdString(),"/field",{8,2,3});
  auto *c=new RasterView(filename.toLocal8Bit().toStdString(),"/field",{8,2,3});
  auto *d=new RasterView(filename.toLocal8Bit().toStdString(),"/field",{8,2,3});
  QList<QMdiSubWindow*> subs;
  for(auto *view:{a,b,c,d}){auto *sub=mdi->addSubWindow(view);sub->setAttribute(Qt::WA_DeleteOnClose);sub->show();subs.append(sub);}
  auto *sync=window.findChild<WorkspaceSyncController*>();QVERIFY(sync);sync->set_anchor(a);
  SyncBinding binding;binding.view=b;binding.lag_offset=2;sync->add_or_update_follower(binding);
  binding.view=c;binding.lag_offset=-1;sync->add_or_update_follower(binding);
  mdi->setActiveSubWindow(subs[1]);
  auto *index=window.findChild<QComboBox*>("layerIndexCombo");QVERIFY(index);
  auto click=[&](const char *name){auto *action=window.findChild<QAction*>(name);QVERIFY(action);QVERIFY(action->isEnabled());QTest::mouseClick(bar->widgetForAction(action),Qt::LeftButton);};
  QTRY_VERIFY(window.current_layer_view()==a);
  click("actionLayerNext");QCOMPARE(a->layer_index(0),1);QCOMPARE(b->layer_index(0),3);QCOMPARE(c->layer_index(0),0);QCOMPARE(d->layer_index(0),0);
  QCOMPARE(index->currentIndex(),1);
  click("actionLayerPrevious");QCOMPARE(a->layer_index(0),0);QCOMPARE(b->layer_index(0),2);QVERIFY(c->plane_snapshot().out_of_range);
  click("actionLayerLast");QCOMPARE(a->layer_index(0),7);QVERIFY(b->plane_snapshot().out_of_range);QCOMPARE(c->layer_index(0),6);
  QCOMPARE(index->currentIndex(),7);QVERIFY(window.findChild<QAction*>("actionLayerPrevious")->isEnabled());
  index->setCurrentIndex(4);emit index->activated(4);QCOMPARE(a->layer_index(0),4);QCOMPARE(b->layer_index(0),6);QVERIFY(!b->plane_snapshot().out_of_range);
  QTimer::singleShot(100,[]{if(auto *dialog=qobject_cast<QInputDialog*>(QApplication::activeModalWidget())){dialog->setIntValue(3);dialog->accept();}});
  window.findChild<QAction*>("actionLayerGoTo")->trigger();QCOMPARE(a->layer_index(0),2);QCOMPARE(b->layer_index(0),4);QCOMPARE(c->layer_index(0),1);
  mdi->setActiveSubWindow(subs[0]);click("actionLayerNext");QCOMPARE(a->layer_index(0),3);QCOMPARE(b->layer_index(0),5);
  mdi->setActiveSubWindow(subs[1]);binding.view=b;binding.lag_offset=2;binding.sync_layer=false;sync->add_or_update_follower(binding);
  QTRY_VERIFY(window.current_layer_view()==b);click("actionLayerNext");QCOMPARE(b->layer_index(0),6);QCOMPARE(a->layer_index(0),3);QCOMPARE(c->layer_index(0),2);
  mdi->setActiveSubWindow(subs[3]);QTRY_VERIFY(window.current_layer_view()==d);click("actionLayerNext");QCOMPARE(d->layer_index(0),1);QCOMPARE(a->layer_index(0),3);
  mdi->setActiveSubWindow(subs[2]);QTRY_VERIFY(window.current_layer_view()==a);
  subs[0]->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
  QTRY_VERIFY(!sync->anchor());QTRY_VERIFY(window.current_layer_view()!=a);
}

void DropTargetsTest::layerPlaybackSingleAndLinkedPlanes() {
  QTemporaryDir dir;const auto filename=dir.filePath("playback.nc");write_classic_fixture(filename,NC_CLOBBER);
  MainWindow window;window.resize(1500,900);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
  auto *mdi=window.findChild<QMdiArea*>();auto *bar=window.findChild<QToolBar*>("mainToolBar");
  auto *play=window.findChild<QAction*>("actionLayerPlay");auto *player=window.findChild<LayerPlayback*>();
  auto *interval=window.findChild<QSpinBox*>("playbackIntervalSpin");QVERIFY(play);QVERIFY(player);QVERIFY(interval);
  QVERIFY(!play->isEnabled());QCOMPARE(interval->value(),500);
  auto *a=new RasterView(filename.toLocal8Bit().toStdString(),"/ttr",{3,4,5,6});
  auto *a_sub=mdi->addSubWindow(a);a_sub->setAttribute(Qt::WA_DeleteOnClose);a_sub->show();
  QTRY_VERIFY(a->plane_snapshot().values);QTRY_VERIFY(play->isEnabled());interval->setValue(10000);
  auto click=[&]{QTest::mouseClick(bar->widgetForAction(play),Qt::LeftButton);};
  click();QVERIFY(player->is_playing());QCOMPARE(play->text(),QString("Pause"));
  QCOMPARE(player->target(),a);player->advance_frame();QCOMPARE(a->layer_index(0),1);
  click();QVERIFY(!player->is_playing());QCOMPARE(play->text(),QString("Play"));
  player->advance_frame();QCOMPARE(a->layer_index(0),1);
  click();player->advance_frame();QCOMPARE(a->layer_index(0),2);QVERIFY(!player->is_playing());
  click();QCOMPARE(a->layer_index(0),0);QVERIFY(player->is_playing());click();
  interval->setValue(100);click();QTRY_VERIFY(a->layer_index(0)>0);if(player->is_playing())click();
  const int paused=a->layer_index(0);QTest::qWait(220);QCOMPARE(a->layer_index(0),paused);
  // A not-ready render defers a frame instead of accumulating queued steps.
  interval->setValue(10000);a->set_layer_index(0,0);player->set_ready_check([]{return false;});
  click();player->advance_frame();QCOMPARE(a->layer_index(0),0);click();player->set_ready_check({});
  auto *b=new RasterView(filename.toLocal8Bit().toStdString(),"/ttr",{3,4,5,6});
  auto *b_sub=mdi->addSubWindow(b);b_sub->setAttribute(Qt::WA_DeleteOnClose);b_sub->show();
  window.findChild<QAction*>("actionCompareMetrics")->trigger();auto *panel=window.findChild<ComparePanel*>();QVERIFY(panel);
  auto *lag=panel->findChild<QSpinBox*>("compareLagSpin");lag->setValue(1);
  auto *text=panel->findChild<QPlainTextEdit*>("compareMetricsText");
  QTRY_COMPARE(b->layer_index(0),1);panel->findChild<QPushButton*>("compareResidualButton")->click();
  auto *residual=panel->findChild<ResidualView*>();QVERIFY(residual);QTRY_VERIFY(residual->residual().valid);
  QCOMPARE(residual->residual().values[0],10000.0);
  mdi->setActiveSubWindow(b_sub);click();QCOMPARE(player->target(),a);
  player->advance_frame();QCOMPARE(a->layer_index(0),1);QCOMPARE(b->layer_index(0),2);
  QTRY_VERIFY(text->toPlainText().contains("Anchor[1,0] <-> Target[2,0]"));
  QCOMPARE(residual->residual().values[0],10000.0);
  mdi->setActiveSubWindow(a_sub);QCoreApplication::processEvents();QVERIFY(player->is_playing());
  mdi->setActiveSubWindow(b_sub);QCoreApplication::processEvents();QVERIFY(player->is_playing());
  player->advance_frame();QCOMPARE(a->layer_index(0),2);QVERIFY(b->plane_snapshot().out_of_range);QVERIFY(!player->is_playing());
  QTRY_VERIFY(text->toPlainText().contains("RMSE: N/A"));QVERIFY(residual->image_for_export().isNull());
  click();QCOMPARE(a->layer_index(0),0);QCOMPARE(b->layer_index(0),1);QTRY_VERIFY(residual->residual().valid);click();
  // Interval bounds are enforced even when called without the spin box.
  player->set_interval_ms(1);QCOMPARE(player->interval_ms(),100);
  player->set_interval_ms(20000);QCOMPARE(player->interval_ms(),10000);
}

void DropTargetsTest::layerPlaybackStopsOnContextChanges() {
  QTemporaryDir dir;const auto filename=dir.filePath("playback-context.nc");write_classic_fixture(filename,NC_CLOBBER);
  MainWindow window;window.resize(1400,850);window.show();
  auto *mdi=window.findChild<QMdiArea*>();auto *play=window.findChild<QAction*>("actionLayerPlay");
  auto *player=window.findChild<LayerPlayback*>();auto *interval=window.findChild<QSpinBox*>("playbackIntervalSpin");interval->setValue(10000);
  auto make_view=[&](const char *name,std::vector<hsize_t> shape){auto *view=new RasterView(filename.toLocal8Bit().toStdString(),name,shape);auto *sub=mdi->addSubWindow(view);sub->setAttribute(Qt::WA_DeleteOnClose);sub->show();return sub;};
  auto *a_sub=make_view("/ttr",{3,4,5,6});auto *b_sub=make_view("/ttr",{3,4,5,6});auto *flat_sub=make_view("/count",{5,6});
  auto *a=qobject_cast<RasterView*>(a_sub->widget());auto *b=qobject_cast<RasterView*>(b_sub->widget());
  mdi->setActiveSubWindow(a_sub);QTRY_VERIFY(play->isEnabled());play->trigger();QVERIFY(player->is_playing());
  mdi->setActiveSubWindow(b_sub);QTRY_VERIFY(!player->is_playing());
  play->trigger();QVERIFY(player->is_playing());
  auto *axis=window.findChild<QComboBox*>("layerAxisCombo");axis->setCurrentIndex(1);emit axis->activated(1);QVERIFY(!player->is_playing());
  play->trigger();QVERIFY(player->is_playing());QCOMPARE(player->axis(),1);
  window.findChild<QAction*>("actionLayerNext")->trigger();QVERIFY(!player->is_playing());QCOMPARE(b->layer_index(1),1);
  play->trigger();QVERIFY(player->is_playing());mdi->setActiveSubWindow(flat_sub);QTRY_VERIFY(!player->is_playing());QVERIFY(!play->isEnabled());
  auto *sync=window.findChild<WorkspaceSyncController*>();sync->set_anchor(a);SyncBinding binding;binding.view=b;sync->add_or_update_follower(binding);
  mdi->setActiveSubWindow(b_sub);play->trigger();QVERIFY(player->is_playing());
  binding.lag_offset=1;sync->add_or_update_follower(binding);QVERIFY(!player->is_playing());
  play->trigger();QVERIFY(player->is_playing());a_sub->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
  QTRY_VERIFY(!player->is_playing());QVERIFY(!sync->anchor());
  // The playback controller also survives destruction without a MainWindow host.
  auto *orphan=new RasterView(filename.toLocal8Bit().toStdString(),"/ttr",{3,4,5,6});
  LayerPlayback isolated;isolated.set_interval_ms(10000);QVERIFY(isolated.start(orphan,0));
  delete orphan;QVERIFY(!isolated.is_playing());QVERIFY(!isolated.target());isolated.advance_frame();
  QVERIFY(!isolated.start(nullptr,0));QVERIFY(!isolated.start(flat_sub->widget(),0));
  mdi->setActiveSubWindow(b_sub);QTRY_VERIFY(play->isEnabled());play->trigger();QVERIFY(player->is_playing());
  window.close();QVERIFY(!player->is_playing());
}

void DropTargetsTest::layerPlaybackFrameCountBoundaries() {
  QTemporaryDir dir;const auto filename=dir.filePath("frame-count.nc");
  int file=-1,time=-1,y=-1,x=-1;
  QCOMPARE(nc_create(nc_path_bytes(filename).constData(),NC_CLOBBER,&file),NC_NOERR);
  QCOMPARE(nc_def_dim(file,"time",NC_UNLIMITED,&time),NC_NOERR);
  QCOMPARE(nc_def_dim(file,"y",2,&y),NC_NOERR);QCOMPARE(nc_def_dim(file,"x",3,&x),NC_NOERR);
  int dims[]={time,y,x},variable=-1;QCOMPARE(nc_def_var(file,"field",NC_DOUBLE,3,dims,&variable),NC_NOERR);
  QCOMPARE(nc_enddef(file),NC_NOERR);QCOMPARE(nc_close(file),NC_NOERR);
  MainWindow window;window.resize(1400,850);window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
  auto *mdi=window.findChild<QMdiArea*>();auto *player=window.findChild<LayerPlayback*>();
  auto *play=window.findChild<QAction*>("actionLayerPlay");auto *bar=window.findChild<QToolBar*>("mainToolBar");
  QVERIFY(mdi);QVERIFY(player);QVERIFY(play);QVERIFY(bar);QVERIFY(!play->isEnabled());
  QVERIFY(!QImage(":/images/play_green.png").isNull());QVERIFY(!QImage(":/images/pause_green.png").isNull());
  const QImage icon=QImage(":/images/play_green.png");QCOMPARE(icon.size(),QSize(32,32));
  QCOMPARE(icon.pixelColor(12,16),QColor(26,158,64,255));QCOMPARE(icon.pixelColor(0,0).alpha(),0);
  auto make_view=[&](const QString &path,int count){
    auto *view=new RasterView(path.toLocal8Bit().toStdString(),"/field",{hsize_t(count),2,3});
    auto *sub=mdi->addSubWindow(view);sub->setAttribute(Qt::WA_DeleteOnClose);sub->show();return sub;
  };
  auto *empty=make_view(filename,0);auto *empty_view=qobject_cast<RasterView*>(empty->widget());
  QCOMPARE(empty_view->layer_count(0),0);QTRY_VERIFY(!play->isEnabled());
  QCOMPARE(window.findChild<QLabel*>("layerLabel")->text(),QString("No frames on this axis"));
  QVERIFY(!window.findChild<QComboBox*>("layerIndexCombo")->isEnabled());
  auto *compare_action=window.findChild<QAction*>("actionCompareMetrics");QVERIFY(compare_action);
  auto *compare_button=bar->widgetForAction(compare_action);QVERIFY(compare_button);QVERIFY(compare_button->isVisible());
  QVERIFY(!player->start(empty_view,0));
  QTest::mouseClick(bar->widgetForAction(play),Qt::LeftButton);QVERIFY(!player->is_playing());QVERIFY(!play->isChecked());
  // Separate immutable files make the 0/1/2-frame cases independent of file caching.
  auto create_file=[&](const QString &path,int count){
    int handle=-1,td=-1,yd=-1,xd=-1,id=-1;
    QCOMPARE(nc_create(nc_path_bytes(path).constData(),NC_CLOBBER,&handle),NC_NOERR);
    nc_def_dim(handle,"time",count,&td);nc_def_dim(handle,"y",2,&yd);nc_def_dim(handle,"x",3,&xd);
    int shape[]={td,yd,xd};nc_def_var(handle,"field",NC_DOUBLE,3,shape,&id);nc_enddef(handle);
    std::vector<double> values(size_t(count)*6);for(size_t i=0;i<values.size();++i)values[i]=double(i);
    nc_put_var_double(handle,id,values.data());nc_close(handle);
  };
  const QString single_path=dir.filePath("one.nc"),two_path=dir.filePath("two.nc");
  create_file(single_path,1);create_file(two_path,2);
  auto *single=make_view(single_path,1);auto *single_view=qobject_cast<RasterView*>(single->widget());
  QCOMPARE(single_view->layer_count(0),1);QTRY_VERIFY(!play->isEnabled());QVERIFY(!player->start(single_view,0));
  auto *two=make_view(two_path,2);auto *two_view=qobject_cast<RasterView*>(two->widget());
  QCOMPARE(two_view->layer_count(0),2);QTRY_VERIFY(play->isEnabled());player->set_interval_ms(10000);
  play->trigger();QVERIFY(player->is_playing());player->advance_frame();QCOMPARE(two_view->layer_index(0),1);QVERIFY(!player->is_playing());
  play->trigger();QVERIFY(player->is_playing());QCOMPARE(two_view->layer_index(0),0);
  mdi->setActiveSubWindow(single);QTRY_VERIFY(!player->is_playing());QVERIFY(!play->isEnabled());
  // A linked Target uses the Anchor's frame count, even when the Target has more frames.
  auto *sync=window.findChild<WorkspaceSyncController*>();sync->set_anchor(single_view);
  SyncBinding binding;binding.view=two_view;sync->add_or_update_follower(binding);
  mdi->setActiveSubWindow(two);QTRY_VERIFY(window.current_layer_view()==single_view);QVERIFY(!play->isEnabled());
  sync->clear();sync->set_anchor(two_view);binding.view=single_view;sync->add_or_update_follower(binding);
  mdi->setActiveSubWindow(single);QTRY_VERIFY(window.current_layer_view()==two_view);QVERIFY(play->isEnabled());
  // Switching from a multi-frame axis to a singleton axis stops and disables playback.
  sync->clear();const auto classic_path=dir.filePath("axes.nc");write_classic_fixture(classic_path,NC_CLOBBER);
  auto *multi=new RasterView(classic_path.toLocal8Bit().toStdString(),"/ttr",{3,1,5,6});
  auto *multi_sub=mdi->addSubWindow(multi);multi_sub->show();QTRY_VERIFY(play->isEnabled());
  play->trigger();QVERIFY(player->is_playing());
  auto *axis=window.findChild<QComboBox*>("layerAxisCombo");axis->setCurrentIndex(1);emit axis->activated(1);
  QVERIFY(!player->is_playing());QVERIFY(!play->isEnabled());QVERIFY(!play->isChecked());
  // The empty-axis panel must not create an invalid negative spin-box range.
  mdi->setActiveSubWindow(empty);QCoreApplication::processEvents();QTest::mouseClick(compare_button,Qt::LeftButton);
  auto *panel=window.findChild<ComparePanel*>();QVERIFY(panel);
  QTRY_VERIFY(!panel->findChild<QSpinBox*>("compareAnchorLayerSpin")->isEnabled());
  QCOMPARE(panel->findChild<QSpinBox*>("compareAnchorLayerSpin")->minimum(),0);
  QCOMPARE(panel->findChild<QSpinBox*>("compareAnchorLayerSpin")->maximum(),0);
}

QTEST_MAIN(DropTargetsTest)
#include "drop_targets_test.moc"
