// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ios/net/nsurlrequest_util.h"

#include "base/mac/scoped_nsobject.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

// Tests that FormatUrlRequestForLogging outputs the string in the form:
// request:<url> request.mainDocURL:<mainDocumentURL>.
TEST(NSURLRequestUtilTest, FormatUrlRequestForLogging) {
  base::scoped_nsobject<NSMutableURLRequest> request(
      [[NSMutableURLRequest alloc] init]);
  request.get().URL = [NSURL URLWithString:@"http://www.google.com"];
  request.get().mainDocumentURL =
      [NSURL URLWithString:@"http://www.google1.com"];
  std::string actualString, expectedString;

  actualString = net::FormatUrlRequestForLogging(request);
  expectedString = "request: http://www.google.com"
                   " request.mainDocURL: http://www.google1.com";
  EXPECT_EQ(expectedString, actualString);

  request.get().URL = nil;
  request.get().mainDocumentURL =
      [NSURL URLWithString:@"http://www.google1.com"];
  actualString = net::FormatUrlRequestForLogging(request);
  expectedString = "request: [nil] request.mainDocURL: http://www.google1.com";
  EXPECT_EQ(expectedString, actualString);

  request.get().URL = [NSURL URLWithString:@"http://www.google.com"];
  request.get().mainDocumentURL = nil;
  actualString = net::FormatUrlRequestForLogging(request);
  expectedString = "request: http://www.google.com request.mainDocURL: [nil]";
  EXPECT_EQ(expectedString, actualString);

  request.get().URL = nil;
  request.get().mainDocumentURL = nil;
  actualString = net::FormatUrlRequestForLogging(request);
  expectedString = "request: [nil] request.mainDocURL: [nil]";
  EXPECT_EQ(expectedString, actualString);
}

}  // namespace
