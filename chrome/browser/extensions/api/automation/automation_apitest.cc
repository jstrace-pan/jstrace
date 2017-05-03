// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/files/file_path.h"
#include "base/location.h"
#include "base/path_service.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "chrome/browser/extensions/api/automation_internal/automation_event_router.h"
#include "chrome/browser/extensions/chrome_extension_function.h"
#include "chrome/browser/extensions/extension_apitest.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/extensions/api/automation_internal.h"
#include "chrome/common/extensions/chrome_extension_messages.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/ax_event_notification_details.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "extensions/test/extension_test_message_listener.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/ax_node.h"
#include "ui/accessibility/ax_serializable_tree.h"
#include "ui/accessibility/ax_tree.h"
#include "ui/accessibility/ax_tree_serializer.h"
#include "ui/accessibility/tree_generator.h"

#if defined(OS_CHROMEOS)
#include "ash/common/accelerators/accelerator_controller.h"
#include "ash/common/wm_shell.h"
#include "chrome/browser/ui/aura/accessibility/automation_manager_aura.h"
#endif

namespace extensions {

namespace {
static const char kDomain[] = "a.com";
static const char kSitesDir[] = "automation/sites";
static const char kGotTree[] = "got_tree";
}  // anonymous namespace

class AutomationApiTest : public ExtensionApiTest {
 protected:
  GURL GetURLForPath(const std::string& host, const std::string& path) {
    std::string port = base::UintToString(embedded_test_server()->port());
    GURL::Replacements replacements;
    replacements.SetHostStr(host);
    replacements.SetPortStr(port);
    GURL url =
        embedded_test_server()->GetURL(path).ReplaceComponents(replacements);
    return url;
  }

  void StartEmbeddedTestServer() {
    base::FilePath test_data;
    ASSERT_TRUE(PathService::Get(chrome::DIR_TEST_DATA, &test_data));
    embedded_test_server()->ServeFilesFromDirectory(
        test_data.AppendASCII("extensions/api_test")
        .AppendASCII(kSitesDir));
    ASSERT_TRUE(ExtensionApiTest::StartEmbeddedTestServer());
    host_resolver()->AddRule("*", embedded_test_server()->base_url().host());
  }

 public:
  void SetUpInProcessBrowserTestFixture() override {
    ExtensionApiTest::SetUpInProcessBrowserTestFixture();
  }
};

IN_PROC_BROWSER_TEST_F(AutomationApiTest, TestRendererAccessibilityEnabled) {
  StartEmbeddedTestServer();
  const GURL url = GetURLForPath(kDomain, "/index.html");
  ui_test_utils::NavigateToURL(browser(), url);

  ASSERT_EQ(1, browser()->tab_strip_model()->count());
  content::WebContents* const tab =
      browser()->tab_strip_model()->GetWebContentsAt(0);
  ASSERT_FALSE(tab->IsFullAccessibilityModeForTesting());
  ASSERT_FALSE(tab->IsTreeOnlyAccessibilityModeForTesting());

  base::FilePath extension_path =
      test_data_dir_.AppendASCII("automation/tests/basic");
  ExtensionTestMessageListener got_tree(kGotTree, false /* no reply */);
  LoadExtension(extension_path);
  ASSERT_TRUE(got_tree.WaitUntilSatisfied());

  ASSERT_FALSE(tab->IsFullAccessibilityModeForTesting());
  ASSERT_TRUE(tab->IsTreeOnlyAccessibilityModeForTesting());
}

IN_PROC_BROWSER_TEST_F(AutomationApiTest, SanityCheck) {
  StartEmbeddedTestServer();
  ASSERT_TRUE(RunExtensionSubtest("automation/tests/tabs", "sanity_check.html"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(AutomationApiTest, GetTreeByTabId) {
  StartEmbeddedTestServer();
  ASSERT_TRUE(RunExtensionSubtest("automation/tests/tabs", "tab_id.html"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(AutomationApiTest, Events) {
  StartEmbeddedTestServer();
  ASSERT_TRUE(RunExtensionSubtest("automation/tests/tabs", "events.html"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(AutomationApiTest, Actions) {
  StartEmbeddedTestServer();
  ASSERT_TRUE(RunExtensionSubtest("automation/tests/tabs", "actions.html"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(AutomationApiTest, Location) {
  StartEmbeddedTestServer();
  ASSERT_TRUE(RunExtensionSubtest("automation/tests/tabs", "location.html"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(AutomationApiTest, BoundsForRange) {
  StartEmbeddedTestServer();
  ASSERT_TRUE(RunExtensionSubtest("automation/tests/tabs",
                                  "bounds_for_range.html"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(AutomationApiTest, TabsAutomationBooleanPermissions) {
  StartEmbeddedTestServer();
  ASSERT_TRUE(RunExtensionSubtest(
          "automation/tests/tabs_automation_boolean", "permissions.html"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(AutomationApiTest, TabsAutomationBooleanActions) {
  StartEmbeddedTestServer();
  ASSERT_TRUE(RunExtensionSubtest(
          "automation/tests/tabs_automation_boolean", "actions.html"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(AutomationApiTest, TabsAutomationHostsPermissions) {
  StartEmbeddedTestServer();
  ASSERT_TRUE(RunExtensionSubtest(
          "automation/tests/tabs_automation_hosts", "permissions.html"))
      << message_;
}

#if defined(USE_AURA)
IN_PROC_BROWSER_TEST_F(AutomationApiTest, Desktop) {
  ASSERT_TRUE(RunExtensionSubtest("automation/tests/desktop", "desktop.html"))
      << message_;
}

// TODO(crbug.com/615908): Flaky on CrOS sanitizers.
#if defined(OS_CHROMEOS)
#if defined(MEMORY_SANITIZER) || defined(ADDRESS_SANITIZER)
#define MAYBE_DesktopInitialFocus DISABLED_DesktopInitialFocus
#else
#define MAYBE_DesktopInitialFocus DesktopInitialFocus
#endif
IN_PROC_BROWSER_TEST_F(AutomationApiTest, MAYBE_DesktopInitialFocus) {
  ASSERT_TRUE(
      RunExtensionSubtest("automation/tests/desktop", "initial_focus.html"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(AutomationApiTest, DesktopFocusWeb) {
  ASSERT_TRUE(
      RunExtensionSubtest("automation/tests/desktop", "focus_web.html"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(AutomationApiTest, DesktopFocusIframe) {
  StartEmbeddedTestServer();
  ASSERT_TRUE(
      RunExtensionSubtest("automation/tests/desktop", "focus_iframe.html"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(AutomationApiTest, DesktopFocusViews) {
  AutomationManagerAura::GetInstance()->Enable(browser()->profile());
  // Trigger the shelf subtree to be computed.
  ash::WmShell::Get()->accelerator_controller()->PerformActionIfEnabled(
      ash::FOCUS_SHELF);

  ASSERT_TRUE(
      RunExtensionSubtest("automation/tests/desktop", "focus_views.html"))
      << message_;
}
#endif

IN_PROC_BROWSER_TEST_F(AutomationApiTest, DesktopNotRequested) {
  ASSERT_TRUE(RunExtensionSubtest("automation/tests/tabs",
                                  "desktop_not_requested.html")) << message_;
}

#if defined(OS_CHROMEOS)
IN_PROC_BROWSER_TEST_F(AutomationApiTest, DesktopActions) {
  AutomationManagerAura::GetInstance()->Enable(browser()->profile());
  // Trigger the shelf subtree to be computed.
  ash::WmShell::Get()->accelerator_controller()->PerformActionIfEnabled(
      ash::FOCUS_SHELF);

  ASSERT_TRUE(RunExtensionSubtest("automation/tests/desktop", "actions.html"))
      << message_;
}

// Flaky, see http://crbug.com/435449
IN_PROC_BROWSER_TEST_F(AutomationApiTest, DISABLED_DesktopLoadTabs) {
  ASSERT_TRUE(RunExtensionSubtest("automation/tests/desktop", "load_tabs.html"))
      << message_;
}
#endif  // defined(OS_CHROMEOS)
#else  // !defined(USE_AURA)
IN_PROC_BROWSER_TEST_F(AutomationApiTest, DesktopNotSupported) {
  ASSERT_TRUE(RunExtensionSubtest("automation/tests/desktop",
                                  "desktop_not_supported.html"))
      << message_;
}
#endif  // defined(USE_AURA)

IN_PROC_BROWSER_TEST_F(AutomationApiTest, CloseTab) {
  StartEmbeddedTestServer();
  ASSERT_TRUE(RunExtensionSubtest("automation/tests/tabs", "close_tab.html"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(AutomationApiTest, QuerySelector) {
  StartEmbeddedTestServer();
  ASSERT_TRUE(
      RunExtensionSubtest("automation/tests/tabs", "queryselector.html"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(AutomationApiTest, Find) {
  StartEmbeddedTestServer();
  ASSERT_TRUE(RunExtensionSubtest("automation/tests/tabs", "find.html"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(AutomationApiTest, Attributes) {
  StartEmbeddedTestServer();
  ASSERT_TRUE(RunExtensionSubtest("automation/tests/tabs", "attributes.html"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(AutomationApiTest, TreeChange) {
  StartEmbeddedTestServer();
  ASSERT_TRUE(RunExtensionSubtest("automation/tests/tabs", "tree_change.html"))
      << message_;
}

IN_PROC_BROWSER_TEST_F(AutomationApiTest, DocumentSelection) {
  StartEmbeddedTestServer();
  ASSERT_TRUE(
      RunExtensionSubtest("automation/tests/tabs", "document_selection.html"))
      << message_;
}

}  // namespace extensions
