/***********************************************************************
* fwidget.cpp - Intermediate base class for all widget objects         *
*                                                                      *
* This file is part of the Final Cut widget toolkit                    *
*                                                                      *
* Copyright 2015-2019 Markus Gans                                      *
*                                                                      *
* The Final Cut is free software; you can redistribute it and/or       *
* modify it under the terms of the GNU Lesser General Public License   *
* as published by the Free Software Foundation; either version 3 of    *
* the License, or (at your option) any later version.                  *
*                                                                      *
* The Final Cut is distributed in the hope that it will be useful,     *
* but WITHOUT ANY WARRANTY; without even the implied warranty of       *
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        *
* GNU Lesser General Public License for more details.                  *
*                                                                      *
* You should have received a copy of the GNU Lesser General Public     *
* License along with this program.  If not, see                        *
* <http://www.gnu.org/licenses/>.                                      *
***********************************************************************/

#include <vector>

#include "final/fapplication.h"
#include "final/fevent.h"
#include "final/fmenubar.h"
#include "final/fstatusbar.h"
#include "final/fstring.h"
#include "final/ftermdata.h"
#include "final/fwidget.h"
#include "final/fwidgetcolors.h"

namespace finalcut
{

// global FWidget object
static FWidget* rootObject{nullptr};

// static class attributes
FStatusBar*          FWidget::statusbar{nullptr};
FMenuBar*            FWidget::menubar{nullptr};
FWidget*             FWidget::show_root_widget{nullptr};
FWidget*             FWidget::redraw_root_widget{nullptr};
FWidget::widgetList* FWidget::window_list{nullptr};
FWidget::widgetList* FWidget::dialog_list{nullptr};
FWidget::widgetList* FWidget::always_on_top_list{nullptr};
FWidget::widgetList* FWidget::close_widget{nullptr};
FWidgetColors        FWidget::wc{};
bool                 FWidget::init_desktop{false};
bool                 FWidget::hideable{false};
uInt                 FWidget::modal_dialog_counter{};

//----------------------------------------------------------------------
// class FWidget
//----------------------------------------------------------------------

// constructors and destructor
//----------------------------------------------------------------------
FWidget::FWidget (FWidget* parent, bool disable_alt_screen)
  : FVTerm(bool(! parent), disable_alt_screen)
  , FObject(parent)
{
  // init bit field with 0
  memset (&flags, 0, sizeof(flags));

  flags.active = true;          // Enable widget by default
  flags.visible = true;         // A widget is visible by default
  flags.focusable = true;       // A widget is focusable by default
  flags.visible_cursor = true;  // A widget has a visible cursor by default
  setWidgetProperty (true);     // This FObject is a widget

  if ( ! parent )
  {
    if  ( rootObject )
      throw std::runtime_error( "FWidget: No parent defined! "
                                "There should be only one root object" );
    rootObject = this;
    show_root_widget = nullptr;
    redraw_root_widget = nullptr;
    modal_dialog_counter = 0;
    statusbar = nullptr;
    init();
  }
  else
  {
    flags.visible_cursor = ! hideable;
    offset = parent->client_offset;
    double_flatline_mask.top.resize (getWidth(), false);
    double_flatline_mask.right.resize (getHeight(), false);
    double_flatline_mask.bottom.resize (getWidth(), false);
    double_flatline_mask.left.resize (getHeight(), false);
  }
}

//----------------------------------------------------------------------
FWidget::~FWidget()  // destructor
{
  processDestroy();
  delCallbacks();
  FApplication::removeQueuedEvent(this);

  // unset clicked widget
  if ( this == getClickedWidget() )
    setClickedWidget(0);

  // unset the local window widget focus
  if ( flags.focus )
  {
    if ( auto window = FWindow::getWindowWidget(this) )
      window->setWindowFocusWidget(0);
  }

  // unset the global widget focus
  if ( this == FWidget::getFocusWidget() )
    FWidget::setFocusWidget(0);

  // unset main widget
  if ( this == getMainWidget() )
  {
    setMainWidget(0);
    quit();
  }

  if ( accelerator_list )
  {
    delete accelerator_list;
    accelerator_list = nullptr;
  }

  // finish the program
  if ( rootObject == this )
    finish();
}


// public methods of FWidget
//----------------------------------------------------------------------
FWidget* FWidget::getRootWidget() const
{
  auto obj = const_cast<FWidget*>(this);
  auto p_obj = getParentWidget();

  while ( ! obj->isRootWidget() && p_obj )
  {
    obj = p_obj;
    p_obj = p_obj->getParentWidget();
  }

  return obj;
}

//----------------------------------------------------------------------
FWidget* FWidget::getParentWidget() const
{
  auto p_obj = getParent();

  if ( p_obj && p_obj->isWidget() )
    return static_cast<FWidget*>(p_obj);
  else
    return 0;
}

//----------------------------------------------------------------------
FWidget* FWidget::getFirstFocusableWidget (FObjectList list)
{
  if ( list.empty() )
    return 0;

  auto iter = list.begin();
  auto last = list.end();

  while ( iter != last )
  {
    if ( (*iter)->isWidget() )
    {
      auto child = static_cast<FWidget*>(*iter);

      if ( child->isEnabled() && child->acceptFocus() )
        return child;
    }

    ++iter;
  }

  return 0;
}

//----------------------------------------------------------------------
FWidget* FWidget::getLastFocusableWidget (FObjectList list)
{
  if ( list.empty() )
    return 0;

  auto first = list.begin();
  auto iter  = list.end();

  do
  {
    --iter;

    if ( ! (*iter)->isWidget() )
      continue;

    auto child = static_cast<FWidget*>(*iter);

    if ( child->isEnabled() && child->acceptFocus() )
      return child;
  }
  while ( iter != first );

  return 0;
}

//----------------------------------------------------------------------
FPoint FWidget::getPrintPos()
{
  const auto cur = getPrintCursor();
  return FPoint ( cur.getX() - offset.getX1() - getX() + 1
                , cur.getY() - offset.getY1() - getY() + 1 );
}

//----------------------------------------------------------------------
std::vector<bool>& FWidget::doubleFlatLine_ref (fc::sides side)
{
  assert ( side == fc::top
        || side == fc::right
        || side == fc::bottom
        || side == fc::left );

  switch ( side )
  {
    case fc::top:
      return double_flatline_mask.top;

    case fc::right:
      return double_flatline_mask.right;

    case fc::bottom:
      return double_flatline_mask.bottom;

    case fc::left:
      return double_flatline_mask.left;
  }

  return double_flatline_mask.left;
}

//----------------------------------------------------------------------
void FWidget::setMainWidget (FWidget* obj)
{
  main_widget = obj;
  auto app_object = FApplication::getApplicationObject();

  if ( obj && app_object && ! getFocusWidget() )
    app_object->focusFirstChild();
}

//----------------------------------------------------------------------
bool FWidget::setVisible (bool enable)
{
  return (flags.visible = enable);
}

//----------------------------------------------------------------------
bool FWidget::setEnable (bool enable)
{
  return (flags.active = enable);
}

//----------------------------------------------------------------------
bool FWidget::setFocus (bool enable)
{
  if ( ! isEnabled() )
    return false;

  if ( flags.focus == enable )
    return true;

  auto last_focus = FWidget::getFocusWidget();

  // set widget focus
  if ( enable && ! flags.focus )
  {
    if ( last_focus )
      last_focus->unsetFocus();

    FWidget::setFocusWidget(this);
  }

  auto window = FWindow::getWindowWidget(this);

  // set window focus
  if ( enable && window )
  {
    if ( ! window->isWindowActive() )
    {
      bool has_raised = window->raiseWindow();
      FWindow::setActiveWindow(window);

      if ( has_raised && window->isVisible() && window->isShown() )
        window->redraw();
    }

    window->setWindowFocusWidget(this);
  }

  return (flags.focus = enable);
}

//----------------------------------------------------------------------
void FWidget::setColor()
{
  // Changes colors to the widget default colors
  setColor (foreground_color, background_color);
}

//----------------------------------------------------------------------
void FWidget::setX (int x, bool adjust)
{
  if ( getX() == x && wsize.getX() == x )
    return;

  if ( ! isWindowWidget() && x < 1 )
    x = 1;

  wsize.setX(x);
  adjust_wsize.setX(x);

  if ( adjust )
    adjustSize();
}

//----------------------------------------------------------------------
void FWidget::setY (int y, bool adjust)
{
  if ( getY() == y && wsize.getY() == y )
    return;

  if ( ! isWindowWidget() && y < 1 )
    y = 1;

  wsize.setY(y);
  adjust_wsize.setY(y);

  if ( adjust )
    adjustSize();
}

//----------------------------------------------------------------------
void FWidget::setPos (const FPoint& p, bool adjust)
{
  FPoint pos(p);

  if ( getX() == pos.getX() && wsize.getX() == pos.getX()
    && getY() == pos.getY() && wsize.getY() == pos.getY() )
  {
    return;
  }

  if ( ! isWindowWidget() )
  {
    if ( pos.getX() < 1 )
      pos.setX(1);

    if ( pos.getY() < 1 )
      pos.setY(1);
  }

  wsize.setPos(pos);
  adjust_wsize.setPos(pos);

  if ( adjust )
    adjustSize();
}

//----------------------------------------------------------------------
void FWidget::setWidth (std::size_t width, bool adjust)
{
  width = std::min (width, size_hints.max_width);
  width = std::max (width, size_hints.min_width);

  if ( getWidth() == width && wsize.getWidth() == width  )
    return;

  if ( width < 1 )
    width = 1;

  wsize.setWidth(width);
  adjust_wsize.setWidth(width);

  if ( adjust )
    adjustSize();

  double_flatline_mask.top.resize (getWidth(), false);
  double_flatline_mask.bottom.resize (getWidth(), false);
}

//----------------------------------------------------------------------
void FWidget::setHeight (std::size_t height, bool adjust)
{
  height = std::min (height, size_hints.max_height);
  height = std::max (height, size_hints.min_height);

  if ( getHeight() == height && wsize.getHeight() == height )
    return;

  if ( height < 1 )
    height = 1;

  wsize.setHeight(height);
  adjust_wsize.setHeight(height);

  if ( adjust )
    adjustSize();

  double_flatline_mask.right.resize (getHeight(), false);
  double_flatline_mask.left.resize (getHeight(), false);
}

//----------------------------------------------------------------------
void FWidget::setSize (const FSize& size, bool adjust)
{
  std::size_t width = size.getWidth();
  std::size_t height = size.getHeight();
  width  = std::min (width,  size_hints.max_width);
  width  = std::max (width,  size_hints.min_width);
  height = std::min (height, size_hints.max_height);
  height = std::max (height, size_hints.min_height);

  if ( getWidth() == width && wsize.getWidth() == width
    && getHeight() == height && wsize.getHeight() == height )
    return;

  if ( width < 1 )
    width = 1;

  if ( height < 1 )
    height = 1;

  wsize.setWidth(width);
  wsize.setHeight(height);
  adjust_wsize.setWidth(width);
  adjust_wsize.setHeight(height);

  if ( adjust )
    adjustSize();

  double_flatline_mask.top.resize (getWidth(), false);
  double_flatline_mask.right.resize (getHeight(), false);
  double_flatline_mask.bottom.resize (getWidth(), false);
  double_flatline_mask.left.resize (getHeight(), false);
}

//----------------------------------------------------------------------
void FWidget::setTopPadding (int top, bool adjust)
{
  if ( padding.top == top )
    return;

  padding.top = top;

  if ( adjust )
  {
    if ( isRootWidget() )
    {
      auto r = rootObject;
      r->client_offset.setY1 (r->padding.top);
      adjustSizeGlobal();
    }
    else
      adjustSize();
  }
}

//----------------------------------------------------------------------
void FWidget::setLeftPadding (int left, bool adjust)
{
  if ( padding.left == left )
    return;

  padding.left = left;

  if ( adjust )
  {
    if ( isRootWidget() )
    {
      auto r = rootObject;
      r->client_offset.setX1 (r->padding.left);
      adjustSizeGlobal();
    }
    else
      adjustSize();
  }
}

//----------------------------------------------------------------------
void FWidget::setBottomPadding (int bottom, bool adjust)
{
  if ( padding.bottom == bottom )
    return;

  padding.bottom = bottom;

  if ( adjust )
  {
    if ( isRootWidget() )
    {
      auto r = rootObject;
      r->client_offset.setY2 (int(r->getHeight()) - 1 - r->padding.bottom);
      adjustSizeGlobal();
    }
    else
      adjustSize();
  }
}

//----------------------------------------------------------------------
void FWidget::setRightPadding (int right, bool adjust)
{
  if ( padding.right == right )
    return;

  padding.right = right;

  if ( adjust )
  {
    if ( isRootWidget() )
    {
      auto r = rootObject;
      r->client_offset.setX2  (int(r->getWidth()) - 1 - r->padding.right);
      adjustSizeGlobal();
    }
    else
      adjustSize();
  }
}

//----------------------------------------------------------------------
void FWidget::setParentOffset()
{
  auto p = getParentWidget();

  if ( p )
    offset = p->client_offset;
}

//----------------------------------------------------------------------
void FWidget::setTermOffset()
{
  auto r = getRootWidget();
  int w = int(r->getWidth());
  int h = int(r->getHeight());
  offset.setCoordinates (0, 0, w - 1, h - 1);
}

//----------------------------------------------------------------------
void FWidget::setTermOffsetWithPadding()
{
  auto r = getRootWidget();
  offset.setCoordinates ( r->getLeftPadding()
                        , r->getTopPadding()
                        , int(r->getWidth()) - 1 - r->getRightPadding()
                        , int(r->getHeight()) - 1 - r->getBottomPadding() );
}

//----------------------------------------------------------------------
void FWidget::setTermSize (const FSize& size)
{
  // Set xterm size to width x height

  if ( isXTerminal() )
  {
    rootObject->wsize.setRect(FPoint(1, 1), size);
    rootObject->adjust_wsize = rootObject->wsize;
    FTerm::setTermSize(size);  // width = columns / height = lines
    detectTermSize();
  }
}

//----------------------------------------------------------------------
void FWidget::setGeometry (const FPoint& p, const FSize& s, bool adjust)
{
  // Sets the geometry of the widget relative to its parent

  int x = p.getX();
  int y = p.getY();
  std::size_t w = s.getWidth();
  std::size_t h = s.getHeight();
  w = std::min (w, size_hints.max_width);
  w = std::max (w, size_hints.min_width);
  h = std::min (h, size_hints.max_height);
  h = std::max (h, size_hints.min_height);

  if ( getPos() == p && getWidth() == w && getHeight() == h )
    return;

  if ( ! isWindowWidget() )
  {
    ( x < 1 ) ? wsize.setX(1) : wsize.setX(x);
    ( y < 1 ) ? wsize.setY(1) : wsize.setY(y);
  }
  else
  {
    wsize.setX(x);
    wsize.setY(y);
  }

  ( w < 1 ) ? wsize.setWidth(1) : wsize.setWidth(w);
  ( h < 1 ) ? wsize.setHeight(1) : wsize.setHeight(h);

  adjust_wsize = wsize;
  int term_x = getTermX();
  int term_y = getTermY();

  client_offset.setCoordinates ( term_x - 1 + padding.left
                               , term_y - 1 + padding.top
                               , term_x - 2 + int(getWidth()) - padding.right
                               , term_y - 2 + int(getHeight()) - padding.bottom );

  double_flatline_mask.top.resize (getWidth(), false);
  double_flatline_mask.right.resize (getHeight(), false);
  double_flatline_mask.bottom.resize (getWidth(), false);
  double_flatline_mask.left.resize (getHeight(), false);

  if ( adjust )
    adjustSize();
}

//----------------------------------------------------------------------
bool FWidget::setCursorPos (const FPoint& pos)
{
  // sets the input cursor position

  widget_cursor_position.setPoint(pos);

  if ( ! flags.focus || isWindowWidget() )
    return false;

  if ( ! FWindow::getWindowWidget(this) )
    return false;

  auto area = getPrintArea();

  if ( area->widget )
  {
    int widget_offsetX = getTermX() - area->widget->getTermX();
    int widget_offsetY = getTermY() - area->widget->getTermY();

    if ( isChildPrintArea() )
    {
      widget_offsetX += (1 - area->widget->getLeftPadding());
      widget_offsetY += (1 - area->widget->getTopPadding());
    }

    setAreaCursor ( FPoint ( widget_offsetX + pos.getX()
                           , widget_offsetY + pos.getY() )
                  , flags.visible_cursor
                  , area );
    return true;
  }

  return false;
}

//----------------------------------------------------------------------
void FWidget::setPrintPos (const FPoint& pos)
{
  FPoint p{ offset.getX1() + getX() + pos.getX() - 1,
            offset.getY1() + getY() + pos.getY() - 1 };
  setPrintCursor(p);
}

//----------------------------------------------------------------------
void FWidget::setDoubleFlatLine (fc::sides side, bool bit)
{
  uLong length{};

  assert ( side == fc::top
        || side == fc::right
        || side == fc::bottom
        || side == fc::left );

  switch ( side )
  {
    case fc::top:
      length = double_flatline_mask.top.size();
      double_flatline_mask.top.assign(length, bit);
      break;

    case fc::right:
      length = double_flatline_mask.right.size();
      double_flatline_mask.right.assign(length, bit);
      break;

    case fc::bottom:
      length = double_flatline_mask.bottom.size();
      double_flatline_mask.bottom.assign(length, bit);
      break;

    case fc::left:
      length = double_flatline_mask.left.size();
      double_flatline_mask.left.assign(length, bit);
      break;
  }
}

//----------------------------------------------------------------------
void FWidget::setDoubleFlatLine (fc::sides side, int pos, bool bit)
{
  assert ( side == fc::top
        || side == fc::right
        || side == fc::bottom
        || side == fc::left );

  assert ( pos >= 1 );

  uLong length{};
  uLong index = uLong(pos - 1);

  switch ( side )
  {
    case fc::top:
      length = double_flatline_mask.top.size();

      if ( index < length )
        double_flatline_mask.top[index] = bit;

      break;

    case fc::right:
      length = double_flatline_mask.right.size();

      if ( index < length )
        double_flatline_mask.right[index] = bit;

      break;

    case fc::bottom:
      length = double_flatline_mask.bottom.size();

      if ( index < length )
        double_flatline_mask.bottom[index] = bit;

      break;

    case fc::left:
      length = double_flatline_mask.left.size();

      if ( index < length )
        double_flatline_mask.left[index] = bit;

      break;
  }
}

//----------------------------------------------------------------------
FWidget* FWidget::childWidgetAt (FWidget* p, const FPoint& pos)
{
  if ( p && p->hasChildren() )
  {
    auto iter = p->begin();
    auto last = p->end();

    while ( iter != last )
    {
      if ( ! (*iter)->isWidget() )
      {
        ++iter;
        continue;
      }

      auto widget = static_cast<FWidget*>(*iter);

      if ( widget->isEnabled()
        && widget->isShown()
        && ! widget->isWindowWidget()
        && widget->getTermGeometry().contains(pos) )
      {
        auto child = childWidgetAt(widget, pos);
        return ( child != 0 ) ? child : widget;
      }

      ++iter;
    }
  }

  return 0;
}

//----------------------------------------------------------------------
int FWidget::numOfFocusableChildren()
{
  if ( ! hasChildren() )
    return 0;

  int num{0};
  auto iter = FObject::begin();
  auto last = FObject::end();

  while ( iter != last )
  {
    if ( (*iter)->isWidget() )
    {
      auto widget = static_cast<FWidget*>(*iter);

      if ( widget->isShown()
        && widget->acceptFocus()
        && ! widget->isWindowWidget() )
        num++;
    }

    ++iter;
  }

  return num;
}

//----------------------------------------------------------------------
bool FWidget::close()
{
  FCloseEvent ev(fc::Close_Event);
  FApplication::sendEvent(this, &ev);

  if ( ev.isAccepted() )
  {
    if ( this == getMainWidget() )
      quit();
    else
    {
      hide();

      if ( ! flags.modal )
        close_widget->push_back(this);
    }
    return true;
  }
  else
    return false;
}

//----------------------------------------------------------------------
void FWidget::addCallback ( const FString& cb_signal
                          , FCallback cb_handler
                          , FDataPtr data )
{
  // add a (normal) function pointer as callback
  callback_data obj{ cb_signal, cb_handler, data };
  callback_objects.push_back(obj);
}

//----------------------------------------------------------------------
void FWidget::addCallback ( const FString& cb_signal
                          , FWidget* cb_instance
                          , FMemberCallback cb_handler
                          , FDataPtr data )
{
  // add a member function pointer as callback
  member_callback_data obj{ cb_signal, cb_instance, cb_handler, data };
  member_callback_objects.push_back(obj);
}

//----------------------------------------------------------------------
void FWidget::delCallback (FCallback cb_handler)
{
  // delete a cb_handler function pointer

  if ( callback_objects.empty() )
    return;

  auto iter = callback_objects.begin();

  while ( iter != callback_objects.end() )
  {
    if ( iter->cb_handler == cb_handler )
      iter = callback_objects.erase(iter);
    else
      ++iter;
  }
}

//----------------------------------------------------------------------
void FWidget::delCallback (FWidget* cb_instance)
{
  // delete all member function pointer from cb_instance

  if ( member_callback_objects.empty() )
    return;

  auto iter = member_callback_objects.begin();

  while ( iter != member_callback_objects.end() )
  {
    if ( iter->cb_instance == cb_instance )
      iter = member_callback_objects.erase(iter);
    else
      ++iter;
  }
}

//----------------------------------------------------------------------
void FWidget::delCallbacks()
{
  // delete all callbacks from this widget

  member_callback_objects.clear();  // member function pointer
  callback_objects.clear();         // function pointer
}

//----------------------------------------------------------------------
void FWidget::emitCallback (const FString& emit_signal)
{
  // member function pointer

  if ( ! member_callback_objects.empty() )
  {
    auto m_iter = member_callback_objects.begin();
    auto m_end = member_callback_objects.end();

    while ( m_iter != m_end )
    {
      if ( m_iter->cb_signal == emit_signal )
      {
        auto callback = m_iter->cb_handler;
        // call the member function pointer
        (m_iter->cb_instance->*callback) (this, m_iter->data);
      }

      ++m_iter;
    }
  }

  // function pointer
  if ( ! callback_objects.empty() )
  {
    auto iter = callback_objects.begin();
    auto last = callback_objects.end();

    while ( iter != last )
    {
      if ( iter->cb_signal == emit_signal )
      {
        auto callback = iter->cb_handler;
        // call the function pointer
        callback (this, iter->data);
      }

      ++iter;
    }
  }
}

//----------------------------------------------------------------------
void FWidget::addAccelerator (FKey key, FWidget* obj)
{
  auto widget = static_cast<FWidget*>(FWindow::getWindowWidget(obj));
  accelerator accel = { key, obj };

  if ( ! widget || widget == statusbar || widget == menubar )
    widget = getRootWidget();

  if ( widget && widget->accelerator_list )
    widget->accelerator_list->push_back(accel);
}

//----------------------------------------------------------------------
void FWidget::delAccelerator (FWidget* obj)
{
  auto widget = static_cast<FWidget*>(FWindow::getWindowWidget(this));

  if ( ! widget || widget == statusbar || widget == menubar )
    widget = getRootWidget();

  if ( widget
    && widget->accelerator_list
    && ! widget->accelerator_list->empty() )
  {
    auto iter = widget->accelerator_list->begin();

    while ( iter != widget->accelerator_list->end() )
    {
      if ( iter->object == obj )
        iter = widget->accelerator_list->erase(iter);
      else
        ++iter;
    }
  }
}

//----------------------------------------------------------------------
void FWidget::redraw()
{
  if ( ! redraw_root_widget )
    redraw_root_widget = this;

  if ( isRootWidget() )
  {
    startTerminalUpdate();
    // clean desktop
    setColor (wc.term_fg, wc.term_bg);
    clearArea (getVirtualDesktop());
  }
  else if ( ! isShown() )
    return;

  draw();

  if ( isRootWidget() )
    drawWindows();
  else
    drawChildren();

  if ( isRootWidget() )
    finishTerminalUpdate();

  if ( redraw_root_widget == this )
  {
    updateTerminal();
    flush_out();
    redraw_root_widget = nullptr;
  }
}

//----------------------------------------------------------------------
void FWidget::resize()
{
  if ( isRootWidget() )
  {
    FRect old_term_geometry (getTermGeometry());
    detectTermSize();
    FRect term_geometry (getTermGeometry());
    term_geometry.move (-1, -1);

    if ( old_term_geometry.getSize() == term_geometry.getSize() )
      return;

    resizeVTerm (term_geometry.getSize());
    resizeArea (term_geometry, getShadow(), getVirtualDesktop());
    adjustSizeGlobal();
  }
  else
    adjustSize();

  // resize the four double-flatline-masks
  double_flatline_mask.top.resize (getWidth(), false);
  double_flatline_mask.right.resize (getHeight(), false);
  double_flatline_mask.bottom.resize (getWidth(), false);
  double_flatline_mask.left.resize (getHeight(), false);
}

//----------------------------------------------------------------------
void FWidget::show()
{
  if ( ! isVisible() )
    return;

  if ( ! init_desktop )
  {
    // Sets the initial screen settings
    initScreenSettings();

    // draw the vdesktop
    auto r = getRootWidget();
    setColor(r->getForegroundColor(), r->getBackgroundColor());
    clearArea (getVirtualDesktop());
    init_desktop = true;
  }

  if ( ! show_root_widget )
  {
    startTerminalUpdate();
    show_root_widget = this;
  }

  draw();
  flags.hidden = false;
  flags.shown = true;

  if ( hasChildren() )
  {
    auto iter = FObject::begin();
    auto last = FObject::end();

    while ( iter != last )
    {
      if ( (*iter)->isWidget() )
      {
        auto widget = static_cast<FWidget*>(*iter);

        if ( ! widget->flags.hidden )
          widget->show();
      }

      ++iter;
    }
  }

  if ( show_root_widget && show_root_widget == this )
  {
    finishTerminalUpdate();
    updateTerminal();
    flush_out();
    show_root_widget = nullptr;
  }

  FShowEvent show_ev (fc::Show_Event);
  FApplication::sendEvent(this, &show_ev);
}

//----------------------------------------------------------------------
void FWidget::hide()
{
  flags.hidden = true;

  if ( isVisible() )
  {
    flags.shown = false;

    if ( ! isDialogWidget()
      && FWidget::getFocusWidget() == this
      && ! focusPrevChild() )
    {
      if ( FWidget::getFocusWidget() )
        FWidget::getFocusWidget()->unsetFocus();

      FWidget::setFocusWidget(getParentWidget());
    }

    FHideEvent hide_ev (fc::Hide_Event);
    FApplication::sendEvent(this, &hide_ev);
  }
}

//----------------------------------------------------------------------
bool FWidget::focusFirstChild()
{
  if ( ! hasChildren() )
    return false;

  auto iter = FObject::begin();
  auto last = FObject::end();

  while ( iter != last )
  {
    if ( ! (*iter)->isWidget() )
    {
      ++iter;
      continue;
    }

    auto widget = static_cast<FWidget*>(*iter);

    if ( widget->isEnabled()
      && widget->acceptFocus()
      && ! widget->isMenuWidget() )
    {
      widget->setFocus();

      if ( widget->numOfChildren() >= 1 )
      {
        if ( ! widget->focusFirstChild() && widget->isWindowWidget() )
        {
          ++iter;
          continue;
        }
      }

      return true;
    }

    ++iter;
  }
  return false;
}

//----------------------------------------------------------------------
bool FWidget::focusLastChild()
{
  if ( ! hasChildren() )
    return false;

  auto iter  = FObject::end();
  auto first = FObject::begin();

  do
  {
    --iter;

    if ( ! (*iter)->isWidget() )
      continue;

    auto widget = static_cast<FWidget*>(*iter);

    if ( widget->isEnabled()
      && widget->acceptFocus()
      && ! widget->isMenuWidget() )
    {
      widget->setFocus();

      if ( widget->numOfChildren() >= 1 )
      {
        if ( ! widget->focusLastChild() && widget->isWindowWidget() )
          continue;
      }

      return true;
    }
  }
  while ( iter != first );

  return false;
}

//----------------------------------------------------------------------
void FWidget::detectTermSize()
{
  auto r = rootObject;
  FTerm::detectTermSize();
  r->adjust_wsize.setRect (1, 1, getDesktopWidth(), getDesktopHeight());
  r->offset.setRect (0, 0, getDesktopWidth(), getDesktopHeight());
  r->client_offset.setCoordinates
  (
    r->padding.left,
    r->padding.top,
    int(getDesktopWidth()) - 1 - r->padding.right,
    int(getDesktopHeight()) - 1 - r->padding.bottom
  );
}

//----------------------------------------------------------------------
void FWidget::move (const FPoint& pos)
{
  wsize.move(pos);
  adjust_wsize.move(pos);
}

//----------------------------------------------------------------------
void FWidget::drawShadow()
{
  if ( isMonochron() && ! flags.trans_shadow )
    return;

  if ( (getEncoding() == fc::VT100 && ! flags.trans_shadow)
    || (getEncoding() == fc::ASCII && ! flags.trans_shadow) )
  {
    clearShadow();
    return;
  }

  int x1 = 1
    , x2 = int(getWidth())
    , y1 = 1
    , y2 = int(getHeight());

  if ( flags.trans_shadow )
  {
    // transparent shadow
    drawTransparentShadow (x1, y1, x2, y2);
  }
  else
  {
    // non-transparent shadow
    drawBlockShadow (x1, y1, x2, y2);
  }
}

//----------------------------------------------------------------------
void FWidget::clearShadow()
{
  if ( isMonochron() )
    return;

  int w = int(getWidth());
  int h = int(getHeight());

  if ( isWindowWidget() )
  {
    setColor (wc.shadow_fg, wc.shadow_bg);
    setInheritBackground();  // current background color will be ignored
  }
  else if ( auto p = getParentWidget() )
    setColor (wc.shadow_fg, p->getBackgroundColor());

  if ( w <= offset.getX2() )
  {
    for (std::size_t y{1}; y <= getHeight(); y++)
    {
      print() << FPoint(w + 1, int(y)) << ' ';  // clear █
    }
  }

  if ( h <= offset.getY2() )
  {
    print() << FPoint(2, h + 1);

    for (std::size_t i{1}; i <= getWidth(); i++)
      print (' ');  // clear ▀
  }

  if ( isWindowWidget() )
    unsetInheritBackground();
}

//----------------------------------------------------------------------
void FWidget::drawFlatBorder()
{
  if ( ! isNewFont() )
    return;

  int x1 = 1
    , x2 = int(getWidth()) + 1
    , y1 = 0
    , y2 = int(getHeight()) + 1;

  if ( auto p = getParentWidget() )
    setColor (wc.dialog_fg, p->getBackgroundColor());
  else
    setColor (wc.dialog_fg, wc.dialog_bg);

  for (std::size_t y{0}; y < getHeight(); y++)
  {
    print() << FPoint(x1 - 1, y1 + int(y) + 1);

    if ( double_flatline_mask.left[uLong(y)] )
      // left+right line (on left side)
      print (fc::NF_rev_border_line_right_and_left);
    else
      // right line (on left side)
      print (fc::NF_rev_border_line_right);
  }

  print() << FPoint(x2, y1 + 1);

  for (std::size_t y{0}; y < getHeight(); y++)
  {
    if ( double_flatline_mask.right[y] )
      // left+right line (on right side)
      print (fc::NF_rev_border_line_right_and_left);
    else
      // left line (on right side)
      print (fc::NF_border_line_left);

    print() << FPoint(x2, y1 + int(y) + 2);
  }

  print() << FPoint(x1, y1);

  for (std::size_t x{0}; x < getWidth(); x++)
  {
    if ( double_flatline_mask.top[x] )
      // top+bottom line (at top)
      print (fc::NF_border_line_up_and_down);
    else
      // bottom line (at top)
      print (fc::NF_border_line_bottom);
  }

  print() << FPoint(x1, y2);

  for (std::size_t x{0}; x < getWidth(); x++)
  {
    if ( double_flatline_mask.bottom[x] )
      // top+bottom line (at bottom)
      print (fc::NF_border_line_up_and_down);
    else
      // top line (at bottom)
      print (fc::NF_border_line_upper);
  }
}

//----------------------------------------------------------------------
void FWidget::clearFlatBorder()
{
  if ( ! isNewFont() )
    return;

  int x1 = 1
    , x2 = int(getWidth()) + 1
    , y1 = 0
    , y2 = int(getHeight()) + 1;

  if ( auto p = getParentWidget() )
    setColor (wc.dialog_fg, p->getBackgroundColor());
  else
    setColor (wc.dialog_fg, wc.dialog_bg);

  // clear on left side
  for (std::size_t y{0}; y < getHeight(); y++)
  {
    print() << FPoint(x1 - 1, y1 + int(y) + 1);

    if ( double_flatline_mask.left[y] )
      print (fc::NF_border_line_left);
    else
      print (' ');
  }

  // clear on right side
  for (std::size_t y{0}; y < getHeight(); y++)
  {
    print() << FPoint(x2, y1 + int(y) + 1);

    if ( double_flatline_mask.right[y] )
      print (fc::NF_rev_border_line_right);
    else
      print (' ');
  }

  // clear at top
  print() << FPoint(x1, y1);

  for (std::size_t x{0}; x < getWidth(); x++)
  {
    if ( double_flatline_mask.top[x] )
      print (fc::NF_border_line_upper);
    else
      print (' ');
  }

  // clear at bottom
  print() << FPoint(x1, y2);

  for (std::size_t x{0}; x < getWidth(); x++)
  {
    if ( double_flatline_mask.bottom[x] )
      print (fc::NF_border_line_bottom);
    else
      print (' ');
  }
}

//----------------------------------------------------------------------
void FWidget::quit()
{
  auto fapp = FApplication::getApplicationObject();
  fapp->exit(0);
}


// protected methods of FWidget
//----------------------------------------------------------------------
FVTerm::term_area* FWidget::getPrintArea()
{
  // returns the print area of this object

  if ( getCurrentPrintArea() )
    return getCurrentPrintArea();
  else
  {
    FWidget* obj{};
    FWidget* p_obj = this;

    do
    {
      obj = p_obj;
      p_obj = static_cast<FWidget*>(obj->getParent());
    }
    while ( ! obj->getVWin() && ! obj->getChildPrintArea() && p_obj );

    if ( obj->getVWin() )
    {
      setPrintArea (obj->getVWin());
      return getCurrentPrintArea();
    }
    else if ( obj->getChildPrintArea() )
    {
      setPrintArea (obj->getChildPrintArea());
      return getCurrentPrintArea();
    }
  }

  return getVirtualDesktop();
}

//----------------------------------------------------------------------
void FWidget::addPreprocessingHandler ( FVTerm* instance
                                      , FPreprocessingHandler handler )
{
  if ( ! getCurrentPrintArea() )
    FWidget::getPrintArea();

  FVTerm::addPreprocessingHandler (instance, handler);
}

//----------------------------------------------------------------------
void FWidget::delPreprocessingHandler (FVTerm* instance)
{
  if ( ! getCurrentPrintArea() )
    FWidget::getPrintArea();

  FVTerm::delPreprocessingHandler (instance);
}

//----------------------------------------------------------------------
bool FWidget::isChildPrintArea() const
{
  auto p_obj = static_cast<FWidget*>(getParent());

  if ( p_obj
    && p_obj->getChildPrintArea()
    && p_obj->getChildPrintArea() == getCurrentPrintArea() )
    return true;
  else
    return false;
}

//----------------------------------------------------------------------
void FWidget::setStatusBar (FStatusBar* sbar)
{
  if ( ! sbar || statusbar == sbar )
    return;

  if ( statusbar )
    delete statusbar;

  statusbar = sbar;
}

//----------------------------------------------------------------------
void FWidget::setMenuBar (FMenuBar* mbar)
{
  if ( ! mbar || menubar == mbar )
    return;

  if ( menubar )
    delete menubar;

  menubar = mbar;
}

//----------------------------------------------------------------------
void FWidget::adjustSize()
{
  if ( ! isRootWidget() )
  {
    auto p = getParentWidget();

    if ( isWindowWidget() )
    {
      if ( ignore_padding && ! isDialogWidget() )
        setTermOffset();
      else
        offset = rootObject->client_offset;
    }
    else if ( ignore_padding && p )
    {
      offset.setCoordinates ( p->getTermX() - 1
                            , p->getTermY() - 1
                            , p->getTermX() + int(p->getWidth()) - 2
                            , p->getTermY() + int(p->getHeight()) - 2 );
    }
    else if ( p )
      offset = p->client_offset;

    adjust_wsize = wsize;
  }

  // Move and shrink in case of lack of space
  if ( ! hasChildPrintArea() )
    insufficientSpaceAdjust();

  client_offset.setCoordinates
  (
    getTermX() - 1 + padding.left,
    getTermY() - 1 + padding.top,
    getTermX() - 2 + int(getWidth()) - padding.right,
    getTermY() - 2 + int(getHeight()) - padding.bottom
  );

  if ( hasChildren() )
  {
    auto iter = FObject::begin();
    auto last = FObject::end();

    while ( iter != last )
    {
      if ( (*iter)->isWidget() )
      {
        auto widget = static_cast<FWidget*>(*iter);

        if ( ! widget->isWindowWidget() )
          widget->adjustSize();
      }
      ++iter;
    }
  }
}

//----------------------------------------------------------------------
void FWidget::adjustSizeGlobal()
{
  if ( ! isRootWidget() )
  {
    getRootWidget()->adjustSizeGlobal();
    return;
  }

  if ( window_list && ! window_list->empty() )
  {
    for (auto&& window : *window_list)
      window->adjustSize();
  }
}

//----------------------------------------------------------------------
void FWidget::hideArea (const FSize& size)
{
  if ( size.isEmpty() )
    return;

  FColor fg{}, bg{};
  auto parent_widget = getParentWidget();

  if ( parent_widget )
  {
    fg = parent_widget->getForegroundColor();
    bg = parent_widget->getBackgroundColor();
  }
  else
  {
    fg = wc.dialog_fg;
    bg = wc.dialog_bg;
  }

  setColor (fg, bg);
  char* blank = createBlankArray(size.getWidth());

  if ( blank == 0 )
    return;

  for (int y{0}; y < int(size.getHeight()); y++)
  {
    print() << FPoint(1, 1 + y) << blank;
  }

  destroyBlankArray (blank);
  flush_out();
}

//----------------------------------------------------------------------
void FWidget::createWidgetAcceleratorList()
{
  if ( accelerator_list == 0 )
  {
    try
    {
      accelerator_list = new Accelerators();
    }
    catch (const std::bad_alloc& ex)
    {
      std::cerr << bad_alloc_str << ex.what() << std::endl;
      std::abort();
    }
  }
}

//----------------------------------------------------------------------
bool FWidget::focusNextChild()
{
  if ( isDialogWidget() || ! hasParent() )
    return false;

  auto parent = getParentWidget();

  if ( ! parent
    || ! parent->hasChildren()
    || parent->numOfFocusableChildren() <= 1 )
    return false;

  auto iter = parent->begin();
  auto last = parent->end();

  while ( iter != last )
  {
    if ( ! (*iter)->isWidget() )
    {
      ++iter;
      continue;
    }

    auto w = static_cast<FWidget*>(*iter);

    if ( w != this )
    {
      ++iter;
      continue;
    }

    FWidget* next{nullptr};
    auto next_element = iter;

    do
    {
      ++next_element;

      if ( next_element == parent->end() )
        next_element = parent->begin();

      if ( ! (*next_element)->isWidget() )
        continue;

      next = static_cast<FWidget*>(*next_element);
    } while ( ! next
           || ! next->isEnabled()
           || ! next->acceptFocus()
           || ! next->isShown()
           || next->isWindowWidget() );

    bool accpt = changeFocus (next, parent, fc::FocusNextWidget);

    if ( ! accpt )
      return false;

    break;  // The focus has been changed
  }

  return true;
}

//----------------------------------------------------------------------
bool FWidget::focusPrevChild()
{
  if ( isDialogWidget() || ! hasParent() )
    return false;

  auto parent = getParentWidget();

  if ( ! parent
    || ! parent->hasChildren()
    || parent->numOfFocusableChildren() <= 1 )
    return false;

  auto iter  = parent->end();
  auto first = parent->begin();

  do
  {
    --iter;

    if ( ! (*iter)->isWidget() )
      continue;

    auto w = static_cast<FWidget*>(*iter);

    if ( w != this )
      continue;

    FWidget* prev{nullptr};
    auto prev_element = iter;

    do
    {
      if ( ! (*prev_element)->isWidget() )
      {
        --prev_element;
        continue;
      }

      if ( prev_element == parent->begin() )
        prev_element = parent->end();

      --prev_element;
      prev = static_cast<FWidget*>(*prev_element);
    } while ( ! prev
           || ! prev->isEnabled()
           || ! prev->acceptFocus()
           || ! prev->isShown()
           || prev->isWindowWidget() );

    bool accpt = changeFocus (prev, parent, fc::FocusPreviousWidget);

    if ( ! accpt )
      return false;

    break;  // The focus has been changed
  }
  while ( iter != first );

  return true;
}

//----------------------------------------------------------------------
bool FWidget::event (FEvent* ev)
{
  switch ( uInt(ev->type()) )
  {
    case fc::KeyPress_Event:
      KeyPressEvent (static_cast<FKeyEvent*>(ev));
      break;

    case fc::KeyUp_Event:
      onKeyUp (static_cast<FKeyEvent*>(ev));
      break;

    case fc::KeyDown_Event:
      KeyDownEvent (static_cast<FKeyEvent*>(ev));
      break;

    case fc::MouseDown_Event:
      onMouseDown (static_cast<FMouseEvent*>(ev));
      break;

    case fc::MouseUp_Event:
      onMouseUp (static_cast<FMouseEvent*>(ev));
      break;

    case fc::MouseDoubleClick_Event:
      onMouseDoubleClick (static_cast<FMouseEvent*>(ev));
      break;

    case fc::MouseWheel_Event:
      onWheel (static_cast<FWheelEvent*>(ev));
      break;

    case fc::MouseMove_Event:
      onMouseMove (static_cast<FMouseEvent*>(ev));
      break;

    case fc::FocusIn_Event:
      onFocusIn (static_cast<FFocusEvent*>(ev));
      break;

    case fc::FocusOut_Event:
      onFocusOut (static_cast<FFocusEvent*>(ev));
      break;

    case fc::ChildFocusIn_Event:
      onChildFocusIn (static_cast<FFocusEvent*>(ev));
      break;

    case fc::ChildFocusOut_Event:
      onChildFocusOut (static_cast<FFocusEvent*>(ev));
      break;

    case fc::Accelerator_Event:
      onAccel (static_cast<FAccelEvent*>(ev));
      break;

    case fc::Resize_Event:
      onResize (static_cast<FResizeEvent*>(ev));
      break;

    case fc::Show_Event:
      onShow (static_cast<FShowEvent*>(ev));
      break;

    case fc::Hide_Event:
      onHide (static_cast<FHideEvent*>(ev));
      break;

    case fc::Close_Event:
      onClose (static_cast<FCloseEvent*>(ev));
      break;

    case fc::Timer_Event:
      onTimer (static_cast<FTimerEvent*>(ev));
      break;

    default:
      return false;
  }

  return true;
}

//----------------------------------------------------------------------
void FWidget::onKeyPress (FKeyEvent*)
{ }

//----------------------------------------------------------------------
void FWidget::onKeyUp (FKeyEvent*)
{ }

//----------------------------------------------------------------------
void FWidget::onKeyDown (FKeyEvent*)
{ }

//----------------------------------------------------------------------
void FWidget::onMouseDown (FMouseEvent*)
{ }

//----------------------------------------------------------------------
void FWidget::onMouseUp (FMouseEvent*)
{ }

//----------------------------------------------------------------------
void FWidget::onMouseDoubleClick (FMouseEvent*)
{ }

//----------------------------------------------------------------------
void FWidget::onWheel (FWheelEvent*)
{ }

//----------------------------------------------------------------------
void FWidget::onMouseMove (FMouseEvent*)
{ }

//----------------------------------------------------------------------
void FWidget::onFocusIn (FFocusEvent*)
{ }

//----------------------------------------------------------------------
void FWidget::onFocusOut (FFocusEvent*)
{ }

//----------------------------------------------------------------------
void FWidget::onChildFocusIn (FFocusEvent*)
{ }

//----------------------------------------------------------------------
void FWidget::onChildFocusOut (FFocusEvent*)
{ }

//----------------------------------------------------------------------
void FWidget::onAccel (FAccelEvent*)
{ }

//----------------------------------------------------------------------
void FWidget::onResize (FResizeEvent* ev)
{
  rootObject->resize();
  rootObject->redraw();
  ev->accept();
}

//----------------------------------------------------------------------
void FWidget::onShow (FShowEvent*)
{ }

//----------------------------------------------------------------------
void FWidget::onHide (FHideEvent*)
{ }

//----------------------------------------------------------------------
void FWidget::onClose (FCloseEvent* ev)
{
  ev->accept();
}


// private methods of FWidget
//----------------------------------------------------------------------
void FWidget::init()
{
  try
  {
    // Initialize widget lists
    window_list        = new widgetList();
    dialog_list        = new widgetList();
    always_on_top_list = new widgetList();
    close_widget       = new widgetList();
  }
  catch (const std::bad_alloc& ex)
  {
    std::cerr << bad_alloc_str << ex.what() << std::endl;
    return;
  }

  hideable = isCursorHideable();
  flags.visible_cursor = ! hideable;

  // Determine width and height of the terminal
  detectTermSize();
  wsize.setRect(1, 1, getDesktopWidth(), getDesktopHeight());
  adjust_wsize = wsize;
  offset.setRect(0, 0, getDesktopWidth(), getDesktopHeight());
  client_offset = offset;

  double_flatline_mask.top.resize (getWidth(), false);
  double_flatline_mask.right.resize (getHeight(), false);
  double_flatline_mask.bottom.resize (getWidth(), false);
  double_flatline_mask.left.resize (getHeight(), false);

  // Initialize default widget colors
  setColorTheme();

  // Default foreground and background color of the desktop/terminal
  foreground_color = wc.term_fg;
  background_color = wc.term_bg;
  init_desktop = false;

  // Create the root object accelerator list
  createWidgetAcceleratorList();
}

//----------------------------------------------------------------------
void FWidget::finish()
{
  if ( close_widget )
  {
    delete close_widget;
    close_widget = nullptr;
  }

  if ( dialog_list )
  {
    delete dialog_list;
    dialog_list = nullptr;
  }

  if ( always_on_top_list )
  {
    delete always_on_top_list;
    always_on_top_list = nullptr;
  }

  if ( window_list )
  {
    delete window_list;
    window_list = nullptr;
  }
}

//----------------------------------------------------------------------
inline void FWidget::insufficientSpaceAdjust()
{
  // Move and shrink widget if there is not enough space available

  if ( isWindowWidget() )
    return;

  // move left if not enough space
  while ( getTermX() + int(getWidth()) - padding.right > offset.getX2() + 2 )
  {
    adjust_wsize.x1_ref()--;
    adjust_wsize.x2_ref()--;

    if ( adjust_wsize.x1_ref() < 1 )
      adjust_wsize.x1_ref() = 1;
  }

  // move up if not enough space
  while ( getTermY() + int(getHeight()) - padding.bottom > offset.getY2() + 2 )
  {
    adjust_wsize.y1_ref()--;
    adjust_wsize.y2_ref()--;

    if ( adjust_wsize.y1_ref() < 1 )
      adjust_wsize.y1_ref() = 1;
  }

  // reduce the width if not enough space
  while ( offset.getX1() + int(getWidth()) - 1 > offset.getX2() )
    adjust_wsize.x2_ref()--;

  if ( getWidth() < size_hints.min_width )
    adjust_wsize.setWidth(size_hints.min_width);

  if ( getWidth() == 0 )
    adjust_wsize.setWidth(1);

  // reduce the height if not enough space
  while ( offset.getY1() + int(getHeight()) - 1 > offset.getY2() )
    adjust_wsize.y2_ref()--;

  if ( getHeight() < size_hints.min_height )
    adjust_wsize.setWidth(size_hints.min_height);

  if ( getHeight() == 0 )
    adjust_wsize.setHeight(1);
}

//----------------------------------------------------------------------
void FWidget::KeyPressEvent (FKeyEvent* kev)
{
  bool accpt_focus{false};

  if ( kev->key() == fc::Fkey_tab )
    accpt_focus = focusNextChild();
  else if ( kev->key() == fc::Fkey_btab )
    accpt_focus = focusPrevChild();

  if ( accpt_focus )
    return;

  FWidget* widget(this);

  while ( widget )
  {
    widget->onKeyPress(kev);

    if ( ! kev->isAccepted() )
    {
      if ( kev->key() == fc::Fkey_right
        || kev->key() == fc::Fkey_down )
        accpt_focus = focusNextChild();
      else if ( kev->key() == fc::Fkey_left
             || kev->key() == fc::Fkey_up )
        accpt_focus = focusPrevChild();

      if ( accpt_focus )
        return;
    }

    if ( kev->isAccepted() || widget->isRootWidget() )
      return;

    widget = widget->getParentWidget();
  }
}

//----------------------------------------------------------------------
void FWidget::KeyDownEvent (FKeyEvent* kev)
{
  FWidget* widget(this);

  while ( widget )
  {
    widget->onKeyDown(kev);

    if ( kev->isAccepted() || widget->isRootWidget() )
      break;

    widget = widget->getParentWidget();
  }
}

//----------------------------------------------------------------------
bool FWidget::changeFocus ( FWidget* follower, FWidget* parent
                          , fc::FocusTypes ft )
{
  FFocusEvent out (fc::FocusOut_Event);
  out.setFocusType(ft);
  FApplication::sendEvent(this, &out);

  FFocusEvent cfo (fc::ChildFocusOut_Event);
  cfo.setFocusType(ft);
  cfo.ignore();
  FApplication::sendEvent(parent, &cfo);

  if ( cfo.isAccepted() )
    out.ignore();

  if ( out.isAccepted() )
  {
    if ( follower == this )
      return false;

    follower->setFocus();
    FFocusEvent cfi (fc::ChildFocusIn_Event);
    FApplication::sendEvent(parent, &cfi);

    FFocusEvent in (fc::FocusIn_Event);
    in.setFocusType(ft);
    FApplication::sendEvent(follower, &in);

    if ( in.isAccepted() )
    {
      redraw();
      follower->redraw();
      updateTerminal();
      flush_out();
    }
  }

  return true;
}

//----------------------------------------------------------------------
void FWidget::draw()
{ }

//----------------------------------------------------------------------
void FWidget::drawWindows()
{
  // redraw windows
  charData default_char{};
  default_char.code         = ' ';
  default_char.fg_color     = fc::Black;
  default_char.bg_color     = fc::Black;
  default_char.attr.byte[0] = 0;
  default_char.attr.byte[1] = 0;

  if ( ! window_list || window_list->empty() )
    return;

  auto iter = window_list->begin();
  auto last  = window_list->end();

  while ( iter != last )
  {
    if ( (*iter)->isShown() )
    {
      auto win = (*iter)->getVWin();
      int w = win->width  + win->right_shadow;
      int h = win->height + win->bottom_shadow;
      std::fill_n (win->text, w * h, default_char);

      (*iter)->redraw();
    }

    ++iter;
  }
}

//----------------------------------------------------------------------
void FWidget::drawChildren()
{
  // draw child elements
  if ( ! hasChildren() )
    return;

  auto iter = FObject::begin();
  auto last = FObject::end();

  while ( iter != last )
  {
    if ( (*iter)->isWidget() )
    {
      auto widget = static_cast<FWidget*>(*iter);

      if ( widget->isShown() && ! widget->isWindowWidget() )
        widget->redraw();
    }

    ++iter;
  }
}

//----------------------------------------------------------------------
void FWidget::drawTransparentShadow (int x1, int y1, int x2, int y2)
{
  // transparent shadow

  setTransparent();
  print() << FPoint(x2 + 1, y1) << "  ";
  unsetTransparent();

  setColor (wc.shadow_bg, wc.shadow_fg);
  setTransShadow();

  for (std::size_t y{1}; y < getHeight(); y++)
  {
    print() << FPoint(x2 + 1, y1 + int(y)) << "  ";
  }

  unsetTransShadow();
  setTransparent();
  print() << FPoint(x1, y2 + 1) << "  ";
  unsetTransparent();

  setColor (wc.shadow_bg, wc.shadow_fg);
  setTransShadow();

  for (std::size_t x{2}; x <= getWidth() + 1; x++)
    print (' ');

  unsetTransShadow();

  if ( isMonochron() )
    setReverse(false);
}

//----------------------------------------------------------------------
void FWidget::drawBlockShadow (int x1, int y1, int x2, int y2)
{
  // non-transparent shadow
  wchar_t block;

  if ( ! hasShadowCharacter() )
    return;

  print() << FPoint(x2 + 1, y1);

  if ( isWindowWidget() )
  {
    setColor (wc.shadow_fg, wc.shadow_bg);
    setInheritBackground();  // current background color will be ignored
  }
  else if ( auto p = getParentWidget() )
    setColor (wc.shadow_fg, p->getBackgroundColor());

  block = fc::FullBlock;  // █
  print (fc::LowerHalfBlock);  // ▄

  if ( isWindowWidget() )
    unsetInheritBackground();

  for (std::size_t y{1}; y < getHeight(); y++)
  {
    print() << FPoint(x2 + 1, y1 + int(y)) << block;  // █
  }

  print() << FPoint(x1 + 1, y2 + 1);

  if ( isWindowWidget() )
    setInheritBackground();

  for (std::size_t x{1}; x <= getWidth(); x++)
    print (fc::UpperHalfBlock);  // ▀

  if ( isWindowWidget() )
    unsetInheritBackground();
}

//----------------------------------------------------------------------
void FWidget::setColorTheme()
{
  // Sets the default color theme

  if ( getMaxColor() < 16 )  // for 8 color mode
    wc.set8ColorTheme();
  else
    wc.set16ColorTheme();
}


// non-member functions
//----------------------------------------------------------------------
char* createBlankArray (std::size_t size)
{
  char* blank;

  if ( size == 0 )
    return 0;

  try
  {
    blank = new char[size + 1];
  }
  catch (const std::bad_alloc& ex)
  {
    std::cerr << bad_alloc_str << ex.what() << std::endl;
    return 0;
  }

  std::memset(blank, ' ', size);
  blank[size] = '\0';
  return blank;
}

//----------------------------------------------------------------------
void destroyBlankArray (char blank[])
{
  delete[] blank;
}

//----------------------------------------------------------------------
FKey getHotkey (const FString& text)
{
  if ( text.isEmpty() )
    return 0;

  std::size_t length = text.getLength();

  for (std::size_t i{0}; i < length; i++)
  {
    try
    {
      if ( i + 1 < length && text[i] == '&' )
        return FKey(text[++i]);
    }
    catch (const std::out_of_range&)
    {
      return 0;
    }
  }
  return 0;
}

//----------------------------------------------------------------------
std::size_t getHotkeyPos (wchar_t src[], wchar_t dest[], std::size_t length)
{
  // Find hotkey position in string
  // + generate a new string without the '&'-sign
  const wchar_t* txt = src;
  constexpr std::size_t NOT_SET = static_cast<std::size_t>(-1);
  std::size_t hotkeypos{NOT_SET};

  for (std::size_t i{0}; i < length; i++)
  {
    if ( i < length && txt[i] == L'&' && hotkeypos == NOT_SET )
    {
      hotkeypos = i;
      i++;
      src++;
    }

    *dest++ = *src++;
  }

  return hotkeypos;
}

//----------------------------------------------------------------------
inline void drawBox (FWidget* w, const FRect& r)
{
  // Use box-drawing characters to draw a border

  w->print() << r.getUpperLeftPos() << fc::BoxDrawingsDownAndRight;  // ┌

  for (int x = r.getX1() + 1; x < r.getX2(); x++)
    w->print (fc::BoxDrawingsHorizontal);     // ─

  w->print (fc::BoxDrawingsDownAndLeft);      // ┐

  for (int y = r.getY1() + 1; y < r.getY2(); y++)
  {
    w->print() << FPoint(r.getX1(), y)
               << fc::BoxDrawingsVertical     // │
               << FPoint(r.getX2(), y)
               << fc::BoxDrawingsVertical;    // │
  }

  w->print() << r.getLowerLeftPos() << fc::BoxDrawingsUpAndRight;  // └

  for (int x = r.getX1() + 1; x < r.getX2(); x++)
    w->print (fc::BoxDrawingsHorizontal);     // ─

  w->print (fc::BoxDrawingsUpAndLeft);        // ┘
}

//----------------------------------------------------------------------
inline void drawNewFontBox (FWidget* w, const FRect& r)
{
  // Use new graphical font characters to draw a border

  w->print() << r.getUpperLeftPos()
             << fc::NF_border_corner_middle_upper_left;  // ┌
  for (int x = r.getX1() + 1; x < r.getX2(); x++)
    w->print (fc::BoxDrawingsHorizontal);                // ─

  w->print (fc::NF_border_corner_middle_upper_right);    // ┐

  for (int y = r.getY1() + 1; y < r.getY2(); y++)
  {
    w->print() << FPoint(r.getX1(), y)
               << fc::NF_border_line_left        // border left ⎸
               << FPoint(r.getX2(), y)
               << fc::NF_rev_border_line_right;  // border right⎹
  }

  w->print() << r.getLowerLeftPos()
             << fc::NF_border_corner_middle_lower_left;  // └

  for (int x = r.getX1() + 1; x < r.getX2(); x++)
    w->print (fc::BoxDrawingsHorizontal);  // ─

  w->print (fc::NF_border_corner_middle_lower_right);  // ┘
}

//----------------------------------------------------------------------
void drawBorder (FWidget* w, FRect r)
{
  if ( r.x1_ref() > r.x2_ref() )
    std::swap (r.x1_ref(), r.x2_ref());

  if ( r.y1_ref() > r.y2_ref() )
    std::swap (r.y1_ref(), r.y2_ref());

  if ( r.x1_ref() < 1 )
    r.x1_ref() = 1;

  if ( r.y1_ref() < 1 )
    r.y1_ref() = 1;

  if ( r.x2_ref() > int(w->getWidth()) )
    r.x2_ref() = int(w->getWidth());

  if ( r.y2_ref() > int(w->getHeight()) )
    r.y2_ref() = int(w->getHeight());

  if ( w->isNewFont() )
    drawNewFontBox (w, r);
  else
    drawBox (w, r);
}

}  // namespace finalcut
