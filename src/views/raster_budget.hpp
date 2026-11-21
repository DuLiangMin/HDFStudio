// Shared process-wide conservative allowance for source and derived raster buffers.
#pragma once
#include <QtGlobal>
#include <memory>
std::shared_ptr<void> reserve_raster(quint64 pixels);
