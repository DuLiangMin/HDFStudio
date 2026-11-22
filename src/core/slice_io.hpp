#pragma once
#include "slice_coordinates.hpp"
#include <QJsonObject>
#include <functional>
#include <QIODevice>

QJsonObject slice_stamp(const QString &file);
QJsonObject slice_metadata(const QString &file,const QString &variable,const SliceInfo &info,const NcSliceSpec &spec);
QJsonObject slice_axis_json(const SliceAxisDescriptor &axis);
SliceAxisDescriptor slice_axis_from_json(const QJsonObject &object);
QString slice_write_atomic(const QString &path,bool overwrite,const std::function<QString(QIODevice&)> &writer);
bool slice_same_file(const QString &left,const QString &right);
QString slice_write_csv(const QString &path,const NcPlane &plane,const std::vector<double> &original,
    const std::vector<double> &row_coordinates,const std::vector<double> &col_coordinates,
    const QJsonObject &metadata,bool overwrite=false);
int slice_command(const QStringList &arguments);
