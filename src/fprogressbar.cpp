/***********************************************************************
* fprogressbar.cpp - Widget FProgressbar                               *
*                                                                      *
* This file is part of the Final Cut widget toolkit                    *
*                                                                      *
* Copyright 2014-2019 Markus Gans                                      *
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

#include "final/fprogressbar.h"

namespace finalcut
{

//----------------------------------------------------------------------
// class FProgressbar
//----------------------------------------------------------------------

// constructors and destructor
//----------------------------------------------------------------------
FProgressbar::FProgressbar(FWidget* parent)
  : FWidget(parent)
{
  unsetFocusable();
  setShadow();
}

//----------------------------------------------------------------------
FProgressbar::~FProgressbar()  // destructor
{ }


// public methods of FProgressbar
//----------------------------------------------------------------------
void FProgressbar::setPercentage (std::size_t percentage_value)
{
  if ( percentage_value == NOT_SET )
    percentage = NOT_SET;
  else if ( percentage_value > 100 )
    percentage = 100;
  else if ( percentage_value <= percentage && percentage != NOT_SET )
    return;
  else
    percentage = percentage_value;

  if ( isShown() )
  {
    drawPercentage();
    drawBar();
  }

  updateTerminal();
}

//----------------------------------------------------------------------
void FProgressbar::setGeometry ( const FPoint& pos, const FSize& size
                               , bool adjust )
{
  // Set the progress bar geometry

  FWidget::setGeometry (pos, size, adjust);
  bar_length = size.getWidth();
}

//----------------------------------------------------------------------
bool FProgressbar::setShadow (bool enable)
{
  if ( enable
    && getEncoding() != fc::VT100
    && getEncoding() != fc::ASCII )
  {
    flags.shadow = true;
    setShadowSize(FSize(1, 1));
  }
  else
  {
    flags.shadow = false;
    setShadowSize(FSize(0, 0));
  }

  return enable;
}

//----------------------------------------------------------------------
void FProgressbar::hide()
{
  FWidget::hide();
  FSize shadow = hasShadow() ? FSize(1, 1) : FSize(0, 0);
  hideSize (getSize() + shadow);
  setPrintPos (FPoint(int(getWidth()) - 4, 0));
  print ("      ");  // hide percentage
}

//----------------------------------------------------------------------
void FProgressbar::reset()
{
  percentage = NOT_SET;

  if ( isShown() )
  {
    drawPercentage();
    drawBar();
  }

  updateTerminal();
}


// private methods of FProgressbar
//----------------------------------------------------------------------
void FProgressbar::draw()
{
  drawPercentage();
  drawBar();

  if ( flags.shadow )
    drawShadow ();

  flush_out();
}

//----------------------------------------------------------------------
void FProgressbar::drawPercentage()
{
  auto parent_widget = getParentWidget();

  if ( parent_widget )
    setColor ( parent_widget->getForegroundColor()
             , parent_widget->getBackgroundColor() );
  else
    setColor ( wc.dialog_fg, wc.dialog_bg );

  if ( isMonochron() )
    setReverse(true);

  setPrintPos (FPoint(int(getWidth()) - 3, 0));

  if ( percentage > 100 )
    print ("--- %");
  else
    printf ("%3d %%", percentage);

  if ( isMonochron() )
    setReverse(false);
}

//----------------------------------------------------------------------
void FProgressbar::drawBar()
{
  std::size_t i = 0;
  double length;
  setPrintPos (FPoint(1, 1));
  setColor ( wc.progressbar_bg
           , wc.progressbar_fg );

  if ( percentage == NOT_SET )
    length = double(-0/100);
  else
    length = double(bar_length * percentage) / 100;

  if ( isMonochron() )
    setReverse(false);

  for (; i < trunc(length); i++)
    print (' ');

  if ( isMonochron() )
    setReverse(true);

  if ( percentage > 0 && percentage <= 100 && trunc(length) < bar_length )
  {
    if ( round(length) > trunc(length) || getMaxColor() < 16 )
    {
      if ( isMonochron() )
      {
        setReverse(false);
        print (' ');
        setReverse(true);
      }
      else
        print (' ');
    }
    else
    {
      setColor (wc.progressbar_fg, wc.progressbar_bg);
      print (fc::LeftHalfBlock);  // ▌
    }

    i++;
  }

  setColor (wc.progressbar_fg, wc.progressbar_bg);

  if ( getMaxColor() < 16 )
  {
    for (; i < bar_length; i++)
      print (fc::MediumShade);  // ▒
  }
  else
  {
    for (; i < bar_length; i++)
      print (' ');
  }

  if ( isMonochron() )
    setReverse(false);

  updateTerminal();
  flush_out();
}

}  // namespace finalcut
