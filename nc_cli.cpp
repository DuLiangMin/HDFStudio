// Console CSV exporter: hdf_cli csv file.nc /PRECT out.csv 0
#include "nc_reader.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <cmath>

int main(int argc,char **argv) {
  QCoreApplication app(argc,argv); auto args=app.arguments(); QTextStream err(stderr);
  if(args.size()<5 || args[1]!="csv") { err<<"Usage: hdf_cli csv file.nc /variable new.csv [index ...]\n"; return 2; }
  std::vector<size_t> indices;
  for(int i=5;i<args.size();++i) { bool ok; auto index=args[i].toULongLong(&ok); if(!ok) return 2; indices.push_back(index); }
  NcReader reader(args[2]); NcPlane plane;
  auto error=reader.read_plane(args[3].toStdString(),indices,plane);
  if(!error.isEmpty()) { err<<error<<'\n'; return 3; }
  QFile file(args[4]); if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)) { err<<file.errorString()<<'\n'; return 4; }
  QTextStream out(&file); out.setRealNumberPrecision(17);
  for(size_t r=0;r<plane.rows;++r) {
    for(size_t c=0;c<plane.cols;++c) { if(c) out<<','; double v=plane.values[r*plane.cols+c]; if(std::isfinite(v)) out<<v; }
    out<<'\n';
  }
  out.flush(); if(out.status()!=QTextStream::Ok || !file.flush()) {file.remove(); return 5;} return 0;
}
