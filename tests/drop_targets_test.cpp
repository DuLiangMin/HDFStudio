// 26NOV19: Qt Test coverage for local-file drag/drop dispatch.
// Run: ctest --test-dir build/hdf_explorer -C Debug -R hdf_explorer_drop_tests --output-on-failure
// Input: daily_bcccsm_2024100100_PRECT_clip.nc at the workspace root; no output files.
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
#include "properties_inspector.hpp"
#include "text_export.hpp"
#include "nc_reader.hpp"
#include "plane_reader.hpp"
#include "histogram_view.hpp"
#include "mesh_view.hpp"
#include <QPlainTextEdit>

class DropTargetsTest : public QObject {
  Q_OBJECT

private slots:
  void acceptsExistingLocalFile();
  void rejectsMissingLocalFile();
  void opensNetcdfFromEachTarget();
  void continuousPalettesMapEndpointsAndReverse();
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
  void externalPackedSampleMatchesPython();
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
  const QString sample = QDir(QCoreApplication::applicationDirPath())
                           .absoluteFilePath("../../../../daily_bcccsm_2024100100_PRECT_clip.nc");
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

    auto roots = tree->findItems("daily_bcccsm_2024100100_PRECT_clip.nc",
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
  const QString sample=QDir(QCoreApplication::applicationDirPath())
      .absoluteFilePath("../../../../daily_bcccsm_2024100100_PRECT_clip.nc");
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
  const QString sample=QDir(QCoreApplication::applicationDirPath())
      .absoluteFilePath("../../../../daily_bcccsm_2024100100_PRECT_clip.nc");
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
  const QString sample=QDir(QCoreApplication::applicationDirPath())
      .absoluteFilePath("../../../../daily_bcccsm_2024100100_PRECT_clip.nc");
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
  const QString sample=QDir(QCoreApplication::applicationDirPath())
      .absoluteFilePath("../../../../no_stand_nc/tmp2m_f.01.1999010100.1999050100.1999010100.nc");
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
  const QStringList samples{
    "tmp2m_f.01.1999010100.1999050100.1999010100.nc",
    "tmp2m_f.01.1999010106.1999021506.1999010106.nc"};
  for(const QString &name : samples) {
    MainWindow dump_window;
    QCOMPARE(dump_window.read_file(QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath("../../../../no_stand_nc/"+name)),0);
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
  const QString sample=QDir(QCoreApplication::applicationDirPath())
      .absoluteFilePath("../../../../daily_bcccsm_2024100100_PRECT_clip.nc");
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
  QCOMPARE(windowMenu->actions().size(),3);   // Cascade, Tile, Close All Views

  for(const char *name:{"ttr","count","ttr"}) {
    tree->setCurrentItem(child_named(name));
    QVERIFY(QMetaObject::invokeMethod(tree,"add_grid"));
  }
  const QList<QMdiSubWindow*> views=mdi->subWindowList(QMdiArea::CreationOrder);
  QCOMPARE(views.size(),3);
  refresh();
  QVERIFY(cascade->isEnabled());
  QCOMPARE(windowMenu->actions().at(0),cascade);
  QVERIFY(windowMenu->actions().at(3)->isSeparator());
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
  QCOMPARE(windowMenu->actions().size(),3);
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
  QCOMPARE(decodedModel->headerData(0,Qt::Horizontal).toString(),QString("Value (decoded to real)"));
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
  QCOMPARE(storedModel->headerData(0,Qt::Horizontal).toString(),QString("Value (raw)"));
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
  QCOMPARE(model->headerData(0,Qt::Horizontal).toString(),QString("Value 1 (decoded)"));
  QCOMPARE(model->headerData(1,Qt::Horizontal).toString(),QString("Value 1 (raw)"));
  QCOMPARE(model->headerData(4,Qt::Horizontal).toString(),QString("Value 3 (decoded)"));
  QCOMPARE(model->headerData(5,Qt::Horizontal).toString(),QString("Value 3 (raw)"));
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

QTEST_MAIN(DropTargetsTest)
#include "drop_targets_test.moc"