// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web/public/web_state/context_menu_params.h"

namespace web {

ContextMenuParams::ContextMenuParams()
    : referrer_policy(ReferrerPolicyDefault), location(CGPointZero) {}

ContextMenuParams::ContextMenuParams(const ContextMenuParams& other) = default;

ContextMenuParams::~ContextMenuParams() {}

}  // namespace web
