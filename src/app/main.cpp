//Copyright (C) 2016 Pedro Vicente
//GNU General Public License (GPL) Version 3 described in the LICENSE file

// Product-only entrypoint; QtTest supplies its own main function.
#include "hdf_explorer.hpp"
#include "startup_splash.hpp"
#include "raster_view.hpp"
#include "grib/grib_data.hpp"
#include <QtWidgets>

static const char app_name[] = "HDFStudio";

int main(int argc, char *argv[])
{
  Q_INIT_RESOURCE(hdf_explorer);
  QApplication app(argc, argv);
  QCoreApplication::setApplicationVersion("0.14.06");
  QCoreApplication::setApplicationName(app_name);
#if QT_VERSION >= 0x050000
  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOption({"preview", "Export a numeric dataset preview.", "variable"});
  parser.addOption({"png", "Preview output PNG.", "path"});
  parser.addOption({"layer", "First leading axis index (zero-based).", "index", "0"});
  parser.addPositionalArgument("file", "The file to open.");
  parser.process(app);
  const QStringList args = parser.positionalArguments();
#endif

#if QT_VERSION >= 0x050000
  // The preview batch mode stays silent; the interactive start shows the waiting box until the
  // main window is ready. QSplashScreen is deliberately avoided (see create_startup_splash()).
  QWidget *splash = nullptr;
  // The window is ready in about 200 ms now, so the splash is held for a short minimum time to
  // be readable instead of flashing for a few frames.
  QElapsedTimer splash_clock;
  const int minimum_splash_ms = 1000;
  if(!parser.isSet("preview")) {
    splash = create_startup_splash();
    splash->show();
    splash_clock.start();
    app.processEvents();
  }
#endif
  MainWindow window;
#if QT_VERSION >= 0x050000
  if(args.size())
  {
    QString file_name = args.at(0);
    if(window.read_file(file_name)!=0) return 2;
    if(parser.isSet("preview")) {
      // Batch export must finish raster rendering before saving/exiting. Interactive
      // views keep bounded asynchronous recoloring for larger planes.
      app.setProperty("synchronousRasterPreview",true);
      auto tree=window.findChild<FileTreeWidget*>();
      if(GribData::is_grib(file_name)) {
        if(!parser.isSet("png"))return 3;
        const auto info=GribData::describe(file_name,parser.value("preview"));
        if(!info.error.isEmpty()||!info.can_image)return 3;
        RasterView view(file_name,parser.value("preview"),info);
        bool valid=false;const int layer=parser.value("layer").toInt(&valid);
        if(!valid||layer<0||layer>(view.layer_axis_count()?view.layer_count(0)-1:0))return 5;
        if(view.layer_axis_count())view.set_layer_index(0,layer);
        const auto image=view.image_for_export();if(image.isNull())return 7;
        QFile png(parser.value("png"));if(!png.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 7;
        QFile json(parser.value("png")+".json");if(!json.open(QIODevice::WriteOnly|QIODevice::NewOnly)){png.remove();return 8;}
        QJsonObject report;for(auto key:{"minimum","maximum","reader","decodeProfile"})report[key]=QJsonValue::fromVariant(view.property(key));
        report["width"]=image.width();report["height"]=image.height();report["object"]=parser.value("preview");report["source_session_id"]=info.object.source_session_id;
        const auto bytes=QJsonDocument(report).toJson();
        if(!image.save(&png,"PNG")||!png.flush()||json.write(bytes)!=bytes.size()||!json.flush()){png.remove();json.remove();return 7;}
        return 0;
      }
      const auto entries=tree->findItems(parser.value("preview"),Qt::MatchExactly|Qt::MatchRecursive);
      if(entries.size()!=1 || !parser.isSet("png")) return 3;
      emit tree->itemDoubleClicked(entries.first(),0);
      app.processEvents();
      auto canvas=window.findChild<QGraphicsView*>();
      if(!canvas) return 4;
      auto widget=canvas->parentWidget();
      auto spin=widget->findChild<QSpinBox*>();
      bool ok=false; int layer=parser.value("layer").toInt(&ok);
      if(!ok || layer<0 || layer>(spin?spin->maximum():0)) return 5;
      if(spin) spin->setValue(layer);
      auto items=canvas->scene()->items();
      if(items.size()!=1) return 6;
      auto raster=qgraphicsitem_cast<QGraphicsPixmapItem*>(items.front());
      auto output=parser.value("png");
      if(!raster || raster->pixmap().isNull()) return 7;
      // Batch mode has no overwrite confirmation. Reserve both new outputs before writing either.
      QFile png(output);
      if(!png.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return 7;
      QFile json(output+".json");
      if(!json.open(QIODevice::WriteOnly | QIODevice::NewOnly)) { png.remove(); return 8; }
      if(!raster->pixmap().save(&png,"PNG") || !png.flush()) { png.remove(); json.remove(); return 7; }
      QJsonObject report;
      for(auto key:{"minimum","maximum","reader"}) report[key]=QJsonValue::fromVariant(widget->property(key));
      report["width"]=raster->pixmap().width(); report["height"]=raster->pixmap().height();
      const auto bytes=QJsonDocument(report).toJson();
      if(json.write(bytes)!=bytes.size() || !json.flush()) { png.remove(); json.remove(); return 9; }
      return 0;
    }
    // Interactive start: every file named on the command line is opened, not only the first.
    for(int i=1;i<args.size();++i) window.read_file(args.at(i));
  }
#endif
  // Hold the splash for the remaining minimum time before the main window covers it, so the
  // message is actually legible rather than a single frame.
  if(splash) {
    while(splash_clock.elapsed() < minimum_splash_ms)
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  }
  window.showMaximized();
  if(splash) {
    splash->close();
    splash->deleteLater();
    splash = nullptr;
  }
  return app.exec();
}
