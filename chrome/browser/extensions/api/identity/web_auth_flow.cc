// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/api/identity/web_auth_flow.h"

#include <utility>

#include "base/base64.h"
#include "base/location.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/trace_event/trace_event.h"
#include "chrome/browser/extensions/component_loader.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/extensions/api/identity_private.h"
#include "chrome/common/extensions/extension_constants.h"
#include "components/guest_view/browser/guest_view_base.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/resource_request_details.h"
#include "content/public/browser/web_contents.h"
#include "crypto/random.h"
#include "extensions/browser/app_window/app_window.h"
#include "extensions/browser/event_router.h"
#include "extensions/browser/extension_system.h"
#include "grit/browser_resources.h"
#include "url/gurl.h"

using content::RenderViewHost;
using content::ResourceRedirectDetails;
using content::WebContents;
using content::WebContentsObserver;
using guest_view::GuestViewBase;

namespace extensions {

namespace identity_private = api::identity_private;

WebAuthFlow::WebAuthFlow(
    Delegate* delegate,
    Profile* profile,
    const GURL& provider_url,
    Mode mode)
    : delegate_(delegate),
      profile_(profile),
      provider_url_(provider_url),
      mode_(mode),
      embedded_window_created_(false) {
}

WebAuthFlow::~WebAuthFlow() {
  DCHECK(delegate_ == NULL);

  // Stop listening to notifications first since some of the code
  // below may generate notifications.
  registrar_.RemoveAll();
  WebContentsObserver::Observe(nullptr);

  if (!app_window_key_.empty()) {
    AppWindowRegistry::Get(profile_)->RemoveObserver(this);

    if (app_window_ && app_window_->web_contents())
      app_window_->web_contents()->Close();
  }
}

void WebAuthFlow::Start() {
  AppWindowRegistry::Get(profile_)->AddObserver(this);

  // Attach a random ID string to the window so we can recognize it
  // in OnAppWindowAdded.
  std::string random_bytes;
  crypto::RandBytes(base::WriteInto(&random_bytes, 33), 32);
  base::Base64Encode(random_bytes, &app_window_key_);

  // identityPrivate.onWebFlowRequest(app_window_key, provider_url_, mode_)
  std::unique_ptr<base::ListValue> args(new base::ListValue());
  args->AppendString(app_window_key_);
  args->AppendString(provider_url_.spec());
  if (mode_ == WebAuthFlow::INTERACTIVE)
    args->AppendString("interactive");
  else
    args->AppendString("silent");

  std::unique_ptr<Event> event(new Event(
      events::IDENTITY_PRIVATE_ON_WEB_FLOW_REQUEST,
      identity_private::OnWebFlowRequest::kEventName, std::move(args)));
  event->restrict_to_browser_context = profile_;
  ExtensionSystem* system = ExtensionSystem::Get(profile_);

  extensions::ComponentLoader* component_loader =
      system->extension_service()->component_loader();
  if (!component_loader->Exists(extension_misc::kIdentityApiUiAppId)) {
    component_loader->Add(
        IDR_IDENTITY_API_SCOPE_APPROVAL_MANIFEST,
        base::FilePath(FILE_PATH_LITERAL("identity_scope_approval_dialog")));
  }

  EventRouter::Get(profile_)->DispatchEventWithLazyListener(
      extension_misc::kIdentityApiUiAppId, std::move(event));
}

void WebAuthFlow::DetachDelegateAndDelete() {
  delegate_ = NULL;
  base::ThreadTaskRunnerHandle::Get()->DeleteSoon(FROM_HERE, this);
}

void WebAuthFlow::OnAppWindowAdded(AppWindow* app_window) {
  if (app_window->window_key() == app_window_key_ &&
      app_window->extension_id() == extension_misc::kIdentityApiUiAppId) {
    app_window_ = app_window;
    WebContentsObserver::Observe(app_window->web_contents());

    registrar_.Add(
        this,
        content::NOTIFICATION_WEB_CONTENTS_RENDER_VIEW_HOST_CREATED,
        content::NotificationService::AllBrowserContextsAndSources());
  }
}

void WebAuthFlow::OnAppWindowRemoved(AppWindow* app_window) {
  if (app_window->window_key() == app_window_key_ &&
      app_window->extension_id() == extension_misc::kIdentityApiUiAppId) {
    app_window_ = NULL;
    registrar_.RemoveAll();
    WebContentsObserver::Observe(nullptr);

    if (delegate_)
      delegate_->OnAuthFlowFailure(WebAuthFlow::WINDOW_CLOSED);
  }
}

void WebAuthFlow::BeforeUrlLoaded(const GURL& url) {
  if (delegate_ && embedded_window_created_)
    delegate_->OnAuthFlowURLChange(url);
}

void WebAuthFlow::AfterUrlLoaded() {
  if (delegate_ && embedded_window_created_ && mode_ == WebAuthFlow::SILENT)
    delegate_->OnAuthFlowFailure(WebAuthFlow::INTERACTION_REQUIRED);
}

void WebAuthFlow::Observe(int type,
                          const content::NotificationSource& source,
                          const content::NotificationDetails& details) {
  DCHECK_EQ(content::NOTIFICATION_WEB_CONTENTS_RENDER_VIEW_HOST_CREATED, type);
  DCHECK(app_window_);

  if (!delegate_ || embedded_window_created_)
    return;

  RenderViewHost* render_view(content::Details<RenderViewHost>(details).ptr());
  WebContents* web_contents = WebContents::FromRenderViewHost(render_view);
  GuestViewBase* guest = GuestViewBase::FromWebContents(web_contents);
  WebContents* owner = guest ? guest->owner_web_contents() : nullptr;
  if (!web_contents || owner != WebContentsObserver::web_contents())
    return;

  // Switch from watching the app window to the guest inside it.
  embedded_window_created_ = true;
  WebContentsObserver::Observe(web_contents);

  registrar_.RemoveAll();
}

void WebAuthFlow::RenderProcessGone(base::TerminationStatus status) {
  if (delegate_)
    delegate_->OnAuthFlowFailure(WebAuthFlow::WINDOW_CLOSED);
}

void WebAuthFlow::DidStartProvisionalLoadForFrame(
    content::RenderFrameHost* render_frame_host,
    const GURL& validated_url,
    bool is_error_page,
    bool is_iframe_srcdoc) {
  if (!render_frame_host->GetParent())
    BeforeUrlLoaded(validated_url);
}

void WebAuthFlow::DidFailProvisionalLoad(
    content::RenderFrameHost* render_frame_host,
    const GURL& validated_url,
    int error_code,
    const base::string16& error_description,
    bool was_ignored_by_handler) {
  TRACE_EVENT_ASYNC_STEP_PAST1("identity",
                               "WebAuthFlow",
                               this,
                               "DidFailProvisionalLoad",
                               "error_code",
                               error_code);
  if (delegate_)
    delegate_->OnAuthFlowFailure(LOAD_FAILED);
}

void WebAuthFlow::DidGetRedirectForResourceRequest(
    content::RenderFrameHost* render_frame_host,
    const content::ResourceRedirectDetails& details) {
  BeforeUrlLoaded(details.new_url);
}

void WebAuthFlow::TitleWasSet(content::NavigationEntry* entry,
                              bool explicit_set) {
  if (delegate_)
    delegate_->OnAuthFlowTitleChange(base::UTF16ToUTF8(entry->GetTitle()));
}

void WebAuthFlow::DidStopLoading() {
  AfterUrlLoaded();
}

void WebAuthFlow::DidNavigateMainFrame(
    const content::LoadCommittedDetails& details,
    const content::FrameNavigateParams& params) {
  if (delegate_ && details.http_status_code >= 400)
    delegate_->OnAuthFlowFailure(LOAD_FAILED);
}

}  // namespace extensions
