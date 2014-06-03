// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_COLOR_CHOOSER_AURA_H_
#define CHROME_BROWSER_UI_VIEWS_COLOR_CHOOSER_AURA_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "content/public/browser/color_chooser.h"
#include "ui/views/color_chooser/color_chooser_listener.h"

namespace content {
class WebContents;
}

namespace views {
class ColorChooserView;
class Widget;
}

// TODO(mukai): rename this as -Ash and move to c/b/ui/ash after Linux-aura
// switches to its native color chooser.
class ColorChooserAura : public content::ColorChooser,
                         public views::ColorChooserListener {
 public:
  static ColorChooserAura* Open(content::WebContents* web_contents,
                                SkColor initial_color);

 private:
  static ColorChooserAura* current_color_chooser_;

  ColorChooserAura(content::WebContents* web_contents, SkColor initial_color);

  // content::ColorChooser overrides:
  virtual void End() OVERRIDE;
  virtual void SetSelectedColor(SkColor color) OVERRIDE;

  // views::ColorChooserListener overrides:
  virtual void OnColorChosen(SkColor color) OVERRIDE;
  virtual void OnColorChooserDialogClosed() OVERRIDE;

  void DidEndColorChooser();

  // The actual view of the color chooser.  No ownership because its parent
  // view will take care of its lifetime.
  views::ColorChooserView* view_;

  // The widget for the color chooser.  No ownership because it's released
  // automatically when closed.
  views::Widget* widget_;

  // The web contents invoking the color chooser.  No ownership because it will
  // outlive this class.
  content::WebContents* web_contents_;

  DISALLOW_COPY_AND_ASSIGN(ColorChooserAura);
};

#endif  // CHROME_BROWSER_UI_VIEWS_COLOR_CHOOSER_AURA_H_
