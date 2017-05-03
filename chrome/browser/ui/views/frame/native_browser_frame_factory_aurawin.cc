// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/frame/native_browser_frame_factory.h"

#include "chrome/browser/ui/views/frame/browser_frame_mus.h"
#include "chrome/browser/ui/views/frame/desktop_browser_frame_aura.h"
#include "services/shell/runner/common/client_util.h"

NativeBrowserFrame* NativeBrowserFrameFactory::Create(
    BrowserFrame* browser_frame,
    BrowserView* browser_view) {
  if (shell::ShellIsRemote())
    return new BrowserFrameMus(browser_frame, browser_view);
  return new DesktopBrowserFrameAura(browser_frame, browser_view);
}
