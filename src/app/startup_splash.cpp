//Copyright (C) 2016 Pedro Vicente
//GNU General Public License (GPL) Version 3 described in the LICENSE file

#include "startup_splash.hpp"
#include <QtWidgets>
#include <QFontDatabase>

// Builds the startup waiting box. A plain frameless widget is used instead of QSplashScreen:
// QSplashScreen::event() handles QEvent::Show by spinning in waitForWidgetMapped() until the
// native window is mapped, capped at 1000 ms (10 ms Sleep per round). That mapped check never
// succeeded from inside the handler on this deployment, so every show() burned the whole cap and
// the box was closed before it ever painted. Measured: constructed 165 ms, show() returned
// 1183 ms; in the same process a plain top-level widget mapped in 135 ms, so the second was the
// wait itself, not window creation. Kept out of the main() test guard so tests can inspect it.
// Qt's own docs also advise Qt::WindowStaysOnTopHint, which WA_ShowWithoutActivating needs here.
QWidget *create_startup_splash()
{
  QWidget *splash = new QWidget(nullptr, Qt::SplashScreen | Qt::FramelessWindowHint |
                                         Qt::WindowStaysOnTopHint);
  splash->setObjectName("startupSplash");
  splash->setAttribute(Qt::WA_ShowWithoutActivating);
  auto *splash_layout = new QVBoxLayout(splash);
  splash_layout->setContentsMargins(32, 22, 32, 22);
  splash_layout->setSpacing(8);
  auto *splash_title = new QLabel(QStringLiteral("HDFStudio"), splash);
  QFont splash_font = splash_title->font();
  splash_font.setPointSize(splash_font.pointSize() + 9);
  splash_font.setBold(true);
  splash_title->setFont(splash_font);
  auto *splash_line = new QLabel(QStringLiteral("Unleash the Power of Your NetCDF and GRIB Data"), splash);
  auto tagline_font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
  tagline_font.setPointSize(tagline_font.pointSize() + 1);
  splash_line->setFont(tagline_font);
  splash_layout->addWidget(splash_title);
  splash_layout->addWidget(splash_line);
  splash->setStyleSheet(QStringLiteral(
    "#startupSplash { background: #fafafa; border: 2px solid #3d99a4; }"
    "QLabel { color: #2a4752; }"));
  splash->adjustSize();
  if(QScreen *screen = QGuiApplication::primaryScreen()) {
    const QRect area = screen->availableGeometry();
    splash->move(area.center() - QPoint(splash->width() / 2, splash->height() / 2));
  }
  return splash;
}
