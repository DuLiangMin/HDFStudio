// HDF/NetCDF grid creation boundary. The caller retains ownership of ItemData.
#pragma once
class QWidget;
class ItemData;

// How a grid shows the values of a packed variable (scale_factor / add_offset):
//   GridDecoded - the real values (stored x scale + offset), what the image view and legend show
//   GridStored  - the numbers exactly as written in the file
//   GridBoth    - both side by side, decoded column then stored column
enum GridValueMode
{
  GridDecoded = 0,
  GridStored  = 1,
  GridBoth    = 2
};

// Requires a loaded variable/attribute buffer; parent/MDI owns the returned widget.
// Runtime layer navigation is available through the existing LayerNavigation interface.
QWidget *create_legacy_grid(QWidget *parent, ItemData *item_data, GridValueMode mode);
