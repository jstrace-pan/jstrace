// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/installable/installable_logging.h"

#include "base/macros.h"
#include "base/strings/stringprintf.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"

namespace {

const std::string& GetMessagePrefix() {
  CR_DEFINE_STATIC_LOCAL(std::string, message_prefix,
                         ("Site cannot be installed: "));
  return message_prefix;
}

// Error message strings corresponding to the InstallableErrorCode enum.
static const char kRendererExitingMessage[] =
    "the page is in the process of being closed";
static const char kRendererCancelledMessage[] =
    "the page has requested the banner prompt be cancelled";
static const char kUserNavigatedMessage[] =
    "the page was navigated before the banner could be shown";
static const char kNotInMainFrameMessage[] =
    "the page is not loaded in the main frame";
static const char kNotFromSecureOriginMessage[] =
    "the page is not served from a secure origin";
static const char kNoManifestMessage[] =
    "the page has no manifest <link> URL";
static const char kManifestEmptyMessage[] =
    "the manifest could not be fetched, is empty, or could not be parsed";
static const char kStartUrlNotValidMessage[] =
    "the start URL in manifest is not valid";
static const char kManifestMissingNameOrShortNameMessage[] =
    "one of manifest name or short name must be specified";
static const char kManifestDisplayNotSupportedMessage[] =
    "the manifest display property must be set to 'standalone' or 'fullscreen'";
static const char kManifestMissingSuitableIconMessage[] =
    "the manifest does not contain a suitable icon - PNG format of at least "
    "%spx is required, and the sizes attribute must be set";
static const char kNoMatchingServiceWorkerMessage[] =
    "no matching service worker detected. You may need to reload the page, or "
    "check that the service worker for the current page also controls the "
    "start URL from the manifest";
static const char kNoAcceptableIconMessage[] =
    "a %spx square icon is required, but no supplied icon meets this "
    "requirement";
static const char kCannotDownloadIconMessage[] =
    "could not download the specified icon";
static const char kNoIconAvailableMessage[] =
    "no icon available to display";
static const char kPlatformNotSupportedOnAndroidMessage[] =
    "the specified application platform is not supported on Android";
static const char kNoIdSpecifiedMessage[] =
    "no Play store ID provided";
static const char kIdsDoNotMatchMessage[] =
    "a Play Store app URL and Play Store ID were specified in the manifest, "
    "but they do not match";

}  // namespace

void LogErrorToConsole(content::WebContents* web_contents,
                       InstallableErrorCode code,
                       const std::string& param) {
  if (!web_contents)
    return;

  content::ConsoleMessageLevel severity = content::CONSOLE_MESSAGE_LEVEL_ERROR;
  const char* pattern = nullptr;
  switch (code) {
    case NO_ERROR_DETECTED:
    case MAX_ERROR_CODE:
      return;
    case RENDERER_EXITING:
      pattern = kRendererExitingMessage;
      break;
    case RENDERER_CANCELLED:
      pattern = kRendererCancelledMessage;
      severity = content::CONSOLE_MESSAGE_LEVEL_LOG;
      break;
    case USER_NAVIGATED:
      pattern = kUserNavigatedMessage;
      severity = content::CONSOLE_MESSAGE_LEVEL_WARNING;
      break;
    case NOT_IN_MAIN_FRAME:
      pattern = kNotInMainFrameMessage;
      break;
    case NOT_FROM_SECURE_ORIGIN:
      pattern = kNotFromSecureOriginMessage;
      break;
    case NO_MANIFEST:
      pattern = kNoManifestMessage;
      break;
    case MANIFEST_EMPTY:
      pattern = kManifestEmptyMessage;
      break;
    case START_URL_NOT_VALID:
      pattern = kStartUrlNotValidMessage;
      break;
    case MANIFEST_MISSING_NAME_OR_SHORT_NAME:
      pattern = kManifestMissingNameOrShortNameMessage;
      break;
    case MANIFEST_DISPLAY_NOT_SUPPORTED:
      pattern = kManifestDisplayNotSupportedMessage;
      break;
    case MANIFEST_MISSING_SUITABLE_ICON:
      pattern = kManifestMissingSuitableIconMessage;
      break;
    case NO_MATCHING_SERVICE_WORKER:
      pattern = kNoMatchingServiceWorkerMessage;
      break;
    case NO_ACCEPTABLE_ICON:
      pattern = kNoAcceptableIconMessage;
      break;
    case CANNOT_DOWNLOAD_ICON:
      pattern = kCannotDownloadIconMessage;
      break;
    case NO_ICON_AVAILABLE:
      pattern = kNoIconAvailableMessage;
      break;
    case PLATFORM_NOT_SUPPORTED_ON_ANDROID:
      pattern = kPlatformNotSupportedOnAndroidMessage;
      severity = content::CONSOLE_MESSAGE_LEVEL_WARNING;
      break;
    case NO_ID_SPECIFIED:
      pattern = kNoIdSpecifiedMessage;
      break;
    case IDS_DO_NOT_MATCH:
      pattern = kIdsDoNotMatchMessage;
      break;
  }

  if (!pattern)
    return;
  std::string message = param.empty() ?
      pattern : base::StringPrintf(pattern, param.c_str());
  web_contents->GetMainFrame()->AddMessageToConsole(
      severity, GetMessagePrefix() + message);
}
