// Layer navigation shared by the views and the main window's Layer toolbar/menu.
// A variable with more than two dimensions is shown one 2-D plane at a time; the leading axes
// (for example time and lev of ttr(time, lev, lat, lon)) select the plane. The old HDF Explorer
// (2007/2008/2022) had main-toolbar buttons that stepped the active view:
//   32776 up = next layer, 32777 down = previous, 32778 double up = last, 32779 double down = first,
//   32831 = "go to layer" dialog, plus the status text "Layer %d of %d".
// HDFStudio keeps that behaviour; indices are 0-based inside, shown 1-based to the user.
#pragma once
#include <QString>

enum class LayerStep { Next, Previous, Last, First };

// Index after a step, clamped like the old handlers: Next stops at the last layer,
// Previous stops at 0. Returns 0 when count <= 0.
inline int layer_step_target(int current, int count, LayerStep step)
{
  if(count <= 0) return 0;
  int target = current;
  if(step == LayerStep::Next) target = current + 1;
  if(step == LayerStep::Previous) target = current - 1;
  if(step == LayerStep::Last) target = count - 1;
  if(step == LayerStep::First) target = 0;
  if(target < 0) target = 0;
  if(target > count - 1) target = count - 1;
  return target;
}

// A requested index kept inside [0, count-1]; 0 when count <= 0.
inline int layer_clamp(int index, int count)
{
  if(count <= 0 || index < 0) return 0;
  if(index > count - 1) return count - 1;
  return index;
}

// Implemented by every view that shows one plane of a higher-rank variable (grid, image view).
// A view announces its own selector changes through a Qt signal named layer_changed().
class LayerNavigation
{
public:
  virtual ~LayerNavigation() = default;
  virtual int layer_axis_count() const = 0;           // leading axes; 0 when the variable has rank <= 2
  virtual QString layer_axis_name(int axis) const = 0; // dimension name, or "axis N" when unknown
  virtual int layer_count(int axis) const = 0;         // length of that axis
  virtual int layer_index(int axis) const = 0;         // current 0-based index
  virtual void set_layer_index(int axis, int index) = 0; // clamps, re-reads the plane, emits layer_changed()
  int navigation_axis = 0;                              // axis stepped by the main toolbar, kept per view
};
