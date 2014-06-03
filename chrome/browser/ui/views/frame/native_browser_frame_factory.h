// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_NATIVE_BROWSER_FRAME_FACTORY_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_NATIVE_BROWSER_FRAME_FACTORY_H_

#include "base/basictypes.h"
#include "chrome/browser/ui/host_desktop.h"

class BrowserFrame;
class BrowserView;
class NativeBrowserFrame;

// Factory for creating a NativeBrowserFrame.
class NativeBrowserFrameFactory {
 public:
  // Construct a platform-specific implementation of this interface.
  static NativeBrowserFrame* CreateNativeBrowserFrame(
      BrowserFrame* browser_frame,
      BrowserView* browser_view);

  // Sets the factory. Takes ownership of |new_factory|, deleting existing
  // factory. Use NULL to go back to default factory.
  static void Set(NativeBrowserFrameFactory* new_factory);

  // Returns HOST_DESKTOP_TYPE_ASH on Windows when configured to allow browser
  // windows only in Metro mode, otherwise |desktop_type|.
  static chrome::HostDesktopType AdjustHostDesktopType(
      chrome::HostDesktopType desktop_type);

  virtual NativeBrowserFrame* Create(BrowserFrame* browser_frame,
                                     BrowserView* browser_view);

 protected:
  NativeBrowserFrameFactory() {}
  virtual ~NativeBrowserFrameFactory() {}

 private:
  // For Chrome running on desktop platforms, returns true if the factory should
  // create an ash browser frame for the provided |browser_view|.
  static bool ShouldCreateForAshDesktop(BrowserView* browser_view);

  DISALLOW_COPY_AND_ASSIGN(NativeBrowserFrameFactory);
};

#endif  // CHROME_BROWSER_UI_VIEWS_FRAME_NATIVE_BROWSER_FRAME_FACTORY_H_
