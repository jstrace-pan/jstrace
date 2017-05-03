// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/task_manager/providers/web_contents/background_contents_tag.h"

#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/background/background_contents.h"
#include "chrome/browser/background/background_contents_service.h"
#include "chrome/browser/background/background_contents_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/task_manager/providers/web_contents/background_contents_task.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_set.h"

namespace task_manager {

BackgroundContentsTask* BackgroundContentsTag::CreateTask() const {
  // Try to lookup the application name from the parent extension (if any).
  Profile* profile = Profile::FromBrowserContext(
      web_contents()->GetBrowserContext());
  BackgroundContentsService* background_contents_service =
      BackgroundContentsServiceFactory::GetForProfile(profile);
  const base::string16& application_id =
      background_contents_service->GetParentApplicationId(background_contents_);
  const extensions::ExtensionSet& extensions_set =
      extensions::ExtensionRegistry::Get(profile)->enabled_extensions();
  const extensions::Extension* extension =
      extensions_set.GetByID(base::UTF16ToUTF8(application_id));
  base::string16 application_name;
  if (extension)
    application_name = base::UTF8ToUTF16(extension->name());

  return new BackgroundContentsTask(application_name, background_contents_);
}

BackgroundContentsTag::BackgroundContentsTag(
    content::WebContents* web_contents,
    BackgroundContents* background_contents)
    : WebContentsTag(web_contents),
      background_contents_(background_contents) {
  DCHECK(background_contents);
}

BackgroundContentsTag::~BackgroundContentsTag() {
}

}  // namespace task_manager
