// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "chrome/browser/ui/passwords/manage_passwords_bubble_model.h"
#include "chrome/browser/ui/passwords/manage_passwords_icon_view.h"
#include "chrome/browser/ui/passwords/manage_passwords_ui_controller_mock.h"
#include "chrome/browser/ui/passwords/password_dialog_controller.h"
#include "chrome/browser/ui/passwords/password_dialog_prompts.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_profile.h"
#include "components/autofill/core/common/password_form.h"
#include "components/password_manager/core/browser/password_bubble_experiment.h"
#include "components/password_manager/core/browser/password_form_manager.h"
#include "components/password_manager/core/browser/password_manager.h"
#include "components/password_manager/core/browser/statistics_table.h"
#include "components/password_manager/core/browser/stub_form_saver.h"
#include "components/password_manager/core/browser/stub_password_manager_client.h"
#include "components/password_manager/core/browser/stub_password_manager_driver.h"
#include "components/password_manager/core/common/password_manager_ui.h"
#include "components/prefs/pref_service.h"
#include "components/variations/variations_associated_data.h"
#include "content/public/browser/navigation_details.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::DoAll;
using ::testing::ElementsAre;
using ::testing::Pointee;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::_;

namespace {

// Number of dismissals that for sure supresses the bubble.
const int kGreatDissmisalCount = 10;

class DialogPromptMock : public AccountChooserPrompt,
                         public AutoSigninFirstRunPrompt {
 public:
  DialogPromptMock() = default;

  MOCK_METHOD0(ShowAccountChooser, void());
  MOCK_METHOD0(ShowAutoSigninPrompt, void());
  MOCK_METHOD0(ControllerGone, void());

 private:
  DISALLOW_COPY_AND_ASSIGN(DialogPromptMock);
};

class TestManagePasswordsIconView : public ManagePasswordsIconView {
 public:
  TestManagePasswordsIconView() = default;

  void SetState(password_manager::ui::State state) override {
    state_ = state;
  }
  password_manager::ui::State state() { return state_; }

 private:
  password_manager::ui::State state_;

  DISALLOW_COPY_AND_ASSIGN(TestManagePasswordsIconView);
};

// This sublass is used to disable some code paths which are not essential for
// testing.
class TestManagePasswordsUIController : public ManagePasswordsUIController {
 public:
  TestManagePasswordsUIController(
      content::WebContents* contents,
      password_manager::PasswordManagerClient* client);
  ~TestManagePasswordsUIController() override;

  bool opened_bubble() const { return opened_bubble_; }

  MOCK_METHOD1(CreateAccountChooser,
               AccountChooserPrompt*(PasswordDialogController*));
  MOCK_METHOD1(CreateAutoSigninPrompt,
               AutoSigninFirstRunPrompt*(PasswordDialogController*));
  MOCK_METHOD0(OnUpdateBubbleAndIconVisibility, void());
  using ManagePasswordsUIController::DidNavigateMainFrame;

 private:
  void UpdateBubbleAndIconVisibility() override;
  void SavePasswordInternal() override {}
  void UpdatePasswordInternal(
      const autofill::PasswordForm& password_form) override {}
  void NeverSavePasswordInternal() override;

  bool opened_bubble_;
};

TestManagePasswordsUIController::TestManagePasswordsUIController(
    content::WebContents* contents,
    password_manager::PasswordManagerClient* client)
    : ManagePasswordsUIController(contents) {
  // Do not silently replace an existing ManagePasswordsUIController because it
  // unregisters itself in WebContentsDestroyed().
  EXPECT_FALSE(contents->GetUserData(UserDataKey()));
  contents->SetUserData(UserDataKey(), this);
  set_client(client);
}

TestManagePasswordsUIController::~TestManagePasswordsUIController() {
}

void TestManagePasswordsUIController::UpdateBubbleAndIconVisibility() {
  opened_bubble_ = IsAutomaticallyOpeningBubble();
  ManagePasswordsUIController::UpdateBubbleAndIconVisibility();
  OnUpdateBubbleAndIconVisibility();
  if (opened_bubble_)
    OnBubbleShown();
}

void TestManagePasswordsUIController::NeverSavePasswordInternal() {
  autofill::PasswordForm blacklisted;
  blacklisted.origin = this->GetOrigin();
  blacklisted.signon_realm = blacklisted.origin.spec();
  blacklisted.blacklisted_by_user = true;
  password_manager::PasswordStoreChange change(
      password_manager::PasswordStoreChange::ADD, blacklisted);
  password_manager::PasswordStoreChangeList list(1, change);
  OnLoginsChanged(list);
}

void CreateSmartBubbleFieldTrial() {
  using password_bubble_experiment::kSmartBubbleExperimentName;
  using password_bubble_experiment::kSmartBubbleThresholdParam;
  std::map<std::string, std::string> params;
  params[kSmartBubbleThresholdParam] =
      base::IntToString(kGreatDissmisalCount / 2);
  variations::AssociateVariationParams(kSmartBubbleExperimentName, "A", params);
  ASSERT_TRUE(
      base::FieldTrialList::CreateFieldTrial(kSmartBubbleExperimentName, "A"));
}

}  // namespace

class ManagePasswordsUIControllerTest : public ChromeRenderViewHostTestHarness {
 public:
  ManagePasswordsUIControllerTest()
      : password_manager_(&client_), field_trial_list_(nullptr) {}

  void SetUp() override;

  autofill::PasswordForm& test_local_form() { return test_local_form_; }
  autofill::PasswordForm& test_federated_form() { return test_federated_form_; }
  DialogPromptMock& dialog_prompt() { return dialog_prompt_; }

  TestManagePasswordsUIController* controller() {
    return static_cast<TestManagePasswordsUIController*>(
        ManagePasswordsUIController::FromWebContents(web_contents()));
  }

  void ExpectIconStateIs(password_manager::ui::State state);
  void ExpectIconAndControllerStateIs(password_manager::ui::State state);

  std::unique_ptr<password_manager::PasswordFormManager>
  CreateFormManagerWithBestMatches(
      const autofill::PasswordForm& observed_form,
      ScopedVector<autofill::PasswordForm> best_matches);

  std::unique_ptr<password_manager::PasswordFormManager> CreateFormManager();

  // Tests that the state is not changed when the password is autofilled.
  void TestNotChangingStateOnAutofill(
      password_manager::ui::State state);

  MOCK_METHOD1(CredentialCallback, void(const autofill::PasswordForm*));

 private:
  password_manager::StubPasswordManagerClient client_;
  password_manager::StubPasswordManagerDriver driver_;
  password_manager::PasswordManager password_manager_;

  autofill::PasswordForm test_local_form_;
  autofill::PasswordForm test_federated_form_;
  base::FieldTrialList field_trial_list_;
  DialogPromptMock dialog_prompt_;
};

void ManagePasswordsUIControllerTest::SetUp() {
  ChromeRenderViewHostTestHarness::SetUp();

  // Create the test UIController here so that it's bound to
  // |test_web_contents_|, and will be retrieved correctly via
  // ManagePasswordsUIController::FromWebContents in |controller()|.
  new TestManagePasswordsUIController(web_contents(), &client_);

  test_local_form_.origin = GURL("http://example.com/login");
  test_local_form_.username_value = base::ASCIIToUTF16("username");
  test_local_form_.password_value = base::ASCIIToUTF16("12345");

  test_federated_form_.origin = GURL("http://example.com/login");
  test_federated_form_.username_value = base::ASCIIToUTF16("username");
  test_federated_form_.federation_origin =
      url::Origin(GURL("https://federation.test/"));

  // We need to be on a "webby" URL for most tests.
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  content::WebContentsTester::For(web_contents())
  ->NavigateAndCommit(GURL("http://example.com"));
}

void ManagePasswordsUIControllerTest::ExpectIconStateIs(
    password_manager::ui::State state) {
  TestManagePasswordsIconView view;
  controller()->UpdateIconAndBubbleState(&view);
  EXPECT_EQ(state, view.state());
}

void ManagePasswordsUIControllerTest::ExpectIconAndControllerStateIs(
    password_manager::ui::State state) {
  ExpectIconStateIs(state);
  EXPECT_EQ(state, controller()->GetState());
}

std::unique_ptr<password_manager::PasswordFormManager>
ManagePasswordsUIControllerTest::CreateFormManagerWithBestMatches(
    const autofill::PasswordForm& observed_form,
    ScopedVector<autofill::PasswordForm> best_matches) {
  std::unique_ptr<password_manager::PasswordFormManager> test_form_manager(
      new password_manager::PasswordFormManager(
          &password_manager_, &client_, driver_.AsWeakPtr(), observed_form,
          base::WrapUnique(new password_manager::StubFormSaver)));
  test_form_manager->SimulateFetchMatchingLoginsFromPasswordStore();
  test_form_manager->OnGetPasswordStoreResults(std::move(best_matches));
  return test_form_manager;
}

std::unique_ptr<password_manager::PasswordFormManager>
ManagePasswordsUIControllerTest::CreateFormManager() {
  ScopedVector<autofill::PasswordForm> stored_forms;
  stored_forms.push_back(new autofill::PasswordForm(test_local_form()));
  return CreateFormManagerWithBestMatches(test_local_form(),
                                          std::move(stored_forms));
}

void ManagePasswordsUIControllerTest::TestNotChangingStateOnAutofill(
    password_manager::ui::State state) {
  DCHECK(state == password_manager::ui::PENDING_PASSWORD_STATE ||
         state == password_manager::ui::PENDING_PASSWORD_UPDATE_STATE ||
         state == password_manager::ui::CONFIRMATION_STATE);

  // Set the bubble state to |state|.
  std::unique_ptr<password_manager::PasswordFormManager> test_form_manager(
      CreateFormManager());
  test_form_manager->ProvisionallySave(
      test_local_form(),
      password_manager::PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  if (state == password_manager::ui::PENDING_PASSWORD_STATE)
    controller()->OnPasswordSubmitted(std::move(test_form_manager));
  else if (state == password_manager::ui::PENDING_PASSWORD_UPDATE_STATE)
    controller()->OnUpdatePasswordSubmitted(std::move(test_form_manager));
  else // password_manager::ui::CONFIRMATION_STATE
    controller()->OnAutomaticPasswordSave(std::move(test_form_manager));
  ASSERT_EQ(state, controller()->GetState());

  // Autofill happens.
  std::unique_ptr<autofill::PasswordForm> test_form(
      new autofill::PasswordForm(test_local_form()));
  autofill::PasswordFormMap map;
  map.insert(
      std::make_pair(test_local_form().username_value, std::move(test_form)));
  controller()->OnPasswordAutofilled(map, map.begin()->second->origin, nullptr);

  // State shouldn't changed.
  EXPECT_EQ(state, controller()->GetState());
  ExpectIconStateIs(state);
}

TEST_F(ManagePasswordsUIControllerTest, DefaultState) {
  EXPECT_EQ(password_manager::ui::INACTIVE_STATE, controller()->GetState());
  EXPECT_EQ(GURL::EmptyGURL(), controller()->GetOrigin());

  ExpectIconStateIs(password_manager::ui::INACTIVE_STATE);
}

TEST_F(ManagePasswordsUIControllerTest, PasswordAutofilled) {
  std::unique_ptr<autofill::PasswordForm> test_form(
      new autofill::PasswordForm(test_local_form()));
  autofill::PasswordForm* test_form_ptr = test_form.get();
  base::string16 kTestUsername = test_form->username_value;
  autofill::PasswordFormMap map;
  map.insert(std::make_pair(kTestUsername, std::move(test_form)));
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  controller()->OnPasswordAutofilled(map, map.begin()->second->origin, nullptr);

  EXPECT_EQ(password_manager::ui::MANAGE_STATE, controller()->GetState());
  EXPECT_EQ(test_form_ptr->origin, controller()->GetOrigin());
  ASSERT_EQ(1u, controller()->GetCurrentForms().size());
  EXPECT_EQ(kTestUsername, controller()->GetCurrentForms()[0]->username_value);

  // Controller should store a separate copy of the form as it doesn't own it.
  EXPECT_NE(test_form_ptr, controller()->GetCurrentForms()[0]);

  ExpectIconStateIs(password_manager::ui::MANAGE_STATE);
}

TEST_F(ManagePasswordsUIControllerTest, PasswordSubmitted) {
  std::unique_ptr<password_manager::PasswordFormManager> test_form_manager(
      CreateFormManager());
  test_form_manager->ProvisionallySave(
      test_local_form(),
      password_manager::PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  controller()->OnPasswordSubmitted(std::move(test_form_manager));
  EXPECT_EQ(password_manager::ui::PENDING_PASSWORD_STATE,
            controller()->GetState());
  EXPECT_TRUE(controller()->opened_bubble());
  EXPECT_EQ(test_local_form().origin, controller()->GetOrigin());

  ExpectIconStateIs(password_manager::ui::PENDING_PASSWORD_STATE);
}

TEST_F(ManagePasswordsUIControllerTest, BlacklistedFormPasswordSubmitted) {
  autofill::PasswordForm blacklisted;
  blacklisted.origin = test_local_form().origin;
  blacklisted.signon_realm = blacklisted.origin.spec();
  blacklisted.blacklisted_by_user = true;
  ScopedVector<autofill::PasswordForm> stored_forms;
  stored_forms.push_back(new autofill::PasswordForm(blacklisted));
  std::unique_ptr<password_manager::PasswordFormManager> test_form_manager =
      CreateFormManagerWithBestMatches(test_local_form(),
                                       std::move(stored_forms));
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  controller()->OnPasswordSubmitted(std::move(test_form_manager));
  EXPECT_EQ(password_manager::ui::PENDING_PASSWORD_STATE,
            controller()->GetState());
  EXPECT_FALSE(controller()->opened_bubble());

  ExpectIconStateIs(password_manager::ui::PENDING_PASSWORD_STATE);
}

TEST_F(ManagePasswordsUIControllerTest, PasswordSubmittedBubbleSuppressed) {
  CreateSmartBubbleFieldTrial();
  std::unique_ptr<password_manager::PasswordFormManager> test_form_manager(
      CreateFormManager());
  password_manager::InteractionsStats stats;
  stats.origin_domain = test_local_form().origin.GetOrigin();
  stats.username_value = test_local_form().username_value;
  stats.dismissal_count = kGreatDissmisalCount;
  auto interactions(base::WrapUnique(
      new std::vector<std::unique_ptr<password_manager::InteractionsStats>>));
  interactions->push_back(
      base::WrapUnique(new password_manager::InteractionsStats(stats)));
  test_form_manager->OnGetSiteStatistics(std::move(interactions));
  test_form_manager->ProvisionallySave(
      test_local_form(),
      password_manager::PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  controller()->OnPasswordSubmitted(std::move(test_form_manager));
  EXPECT_EQ(password_manager::ui::PENDING_PASSWORD_STATE,
            controller()->GetState());
  EXPECT_FALSE(controller()->opened_bubble());
  ASSERT_TRUE(controller()->GetCurrentInteractionStats());
  EXPECT_EQ(stats, *controller()->GetCurrentInteractionStats());

  ExpectIconStateIs(password_manager::ui::PENDING_PASSWORD_STATE);
  variations::testing::ClearAllVariationParams();
}

TEST_F(ManagePasswordsUIControllerTest, PasswordSubmittedBubbleNotSuppressed) {
  CreateSmartBubbleFieldTrial();
  std::unique_ptr<password_manager::PasswordFormManager> test_form_manager(
      CreateFormManager());
  password_manager::InteractionsStats stats;
  stats.origin_domain = test_local_form().origin.GetOrigin();
  stats.username_value = base::ASCIIToUTF16("not my username");
  stats.dismissal_count = kGreatDissmisalCount;
  auto interactions(base::WrapUnique(
      new std::vector<std::unique_ptr<password_manager::InteractionsStats>>));
  interactions->push_back(
      base::WrapUnique(new password_manager::InteractionsStats(stats)));
  test_form_manager->OnGetSiteStatistics(std::move(interactions));
  test_form_manager->ProvisionallySave(
      test_local_form(),
      password_manager::PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  controller()->OnPasswordSubmitted(std::move(test_form_manager));
  EXPECT_EQ(password_manager::ui::PENDING_PASSWORD_STATE,
            controller()->GetState());
  EXPECT_TRUE(controller()->opened_bubble());
  EXPECT_FALSE(controller()->GetCurrentInteractionStats());

  ExpectIconStateIs(password_manager::ui::PENDING_PASSWORD_STATE);
  variations::testing::ClearAllVariationParams();
}

TEST_F(ManagePasswordsUIControllerTest, PasswordSaved) {
  std::unique_ptr<password_manager::PasswordFormManager> test_form_manager(
      CreateFormManager());
  test_form_manager->ProvisionallySave(
      test_local_form(),
      password_manager::PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  controller()->OnPasswordSubmitted(std::move(test_form_manager));

  controller()->SavePassword();
  ExpectIconStateIs(password_manager::ui::MANAGE_STATE);
}

TEST_F(ManagePasswordsUIControllerTest, PasswordBlacklisted) {
  std::unique_ptr<password_manager::PasswordFormManager> test_form_manager(
      CreateFormManager());
  test_form_manager->ProvisionallySave(
      test_local_form(),
      password_manager::PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  controller()->OnPasswordSubmitted(std::move(test_form_manager));

  controller()->NeverSavePassword();
  ExpectIconStateIs(password_manager::ui::PENDING_PASSWORD_STATE);
}

TEST_F(ManagePasswordsUIControllerTest, NormalNavigations) {
  std::unique_ptr<password_manager::PasswordFormManager> test_form_manager(
      CreateFormManager());
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  controller()->OnPasswordSubmitted(std::move(test_form_manager));
  ExpectIconStateIs(password_manager::ui::PENDING_PASSWORD_STATE);

  // Fake-navigate. We expect the bubble's state to persist so a user reasonably
  // has been able to interact with the bubble. This happens on
  // `accounts.google.com`, for instance.
  controller()->DidNavigateMainFrame(content::LoadCommittedDetails(),
                                     content::FrameNavigateParams());
  EXPECT_EQ(password_manager::ui::PENDING_PASSWORD_STATE,
            controller()->GetState());
  ExpectIconStateIs(password_manager::ui::PENDING_PASSWORD_STATE);
}

TEST_F(ManagePasswordsUIControllerTest, NormalNavigationsClosedBubble) {
  std::unique_ptr<password_manager::PasswordFormManager> test_form_manager(
      CreateFormManager());
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  controller()->OnPasswordSubmitted(std::move(test_form_manager));
  controller()->SavePassword();
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  controller()->OnBubbleHidden();
  ExpectIconStateIs(password_manager::ui::MANAGE_STATE);

  // Fake-navigate. There is no bubble, reset the state.
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  controller()->DidNavigateMainFrame(content::LoadCommittedDetails(),
                                     content::FrameNavigateParams());
  EXPECT_EQ(password_manager::ui::INACTIVE_STATE, controller()->GetState());
  ExpectIconStateIs(password_manager::ui::INACTIVE_STATE);
}

TEST_F(ManagePasswordsUIControllerTest, PasswordSubmittedToNonWebbyURL) {
  // Navigate to a non-webby URL, then see what happens!
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  content::WebContentsTester::For(web_contents())
      ->NavigateAndCommit(GURL("chrome://sign-in"));

  std::unique_ptr<password_manager::PasswordFormManager> test_form_manager(
      CreateFormManager());
  test_form_manager->ProvisionallySave(
      test_local_form(),
      password_manager::PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  controller()->OnPasswordSubmitted(std::move(test_form_manager));
  EXPECT_EQ(password_manager::ui::INACTIVE_STATE, controller()->GetState());
  EXPECT_EQ(GURL::EmptyGURL(), controller()->GetOrigin());

  ExpectIconStateIs(password_manager::ui::INACTIVE_STATE);
}

TEST_F(ManagePasswordsUIControllerTest, BlacklistedElsewhere) {
  base::string16 kTestUsername = base::ASCIIToUTF16("test_username");
  autofill::PasswordFormMap map;
  map.insert(std::make_pair(
      kTestUsername,
      base::WrapUnique(new autofill::PasswordForm(test_local_form()))));
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  controller()->OnPasswordAutofilled(map, map.begin()->second->origin, nullptr);

  test_local_form().blacklisted_by_user = true;
  password_manager::PasswordStoreChange change(
      password_manager::PasswordStoreChange::ADD, test_local_form());
  password_manager::PasswordStoreChangeList list(1, change);
  controller()->OnLoginsChanged(list);

  EXPECT_EQ(password_manager::ui::MANAGE_STATE, controller()->GetState());
  EXPECT_EQ(test_local_form().origin, controller()->GetOrigin());

  ExpectIconStateIs(password_manager::ui::MANAGE_STATE);
}

TEST_F(ManagePasswordsUIControllerTest, AutomaticPasswordSave) {
  std::unique_ptr<password_manager::PasswordFormManager> test_form_manager(
      CreateFormManager());
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  controller()->OnAutomaticPasswordSave(std::move(test_form_manager));
  EXPECT_EQ(password_manager::ui::CONFIRMATION_STATE, controller()->GetState());

  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  controller()->OnBubbleHidden();
  ExpectIconStateIs(password_manager::ui::MANAGE_STATE);
}

TEST_F(ManagePasswordsUIControllerTest, ChooseCredentialLocal) {
  ScopedVector<autofill::PasswordForm> local_credentials;
  local_credentials.push_back(new autofill::PasswordForm(test_local_form()));
  ScopedVector<autofill::PasswordForm> federated_credentials;
  GURL origin("http://example.com");
  PasswordDialogController* dialog_controller = nullptr;
  EXPECT_CALL(*controller(), CreateAccountChooser(_)).WillOnce(
      DoAll(SaveArg<0>(&dialog_controller), Return(&dialog_prompt())));
  EXPECT_CALL(dialog_prompt(), ShowAccountChooser());
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  EXPECT_TRUE(controller()->OnChooseCredentials(
      std::move(local_credentials), std::move(federated_credentials), origin,
      base::Bind(&ManagePasswordsUIControllerTest::CredentialCallback,
                 base::Unretained(this))));
  EXPECT_EQ(password_manager::ui::CREDENTIAL_REQUEST_STATE,
            controller()->GetState());
  EXPECT_EQ(origin, controller()->GetOrigin());
  EXPECT_THAT(controller()->GetCurrentForms(),
              ElementsAre(Pointee(test_local_form())));
  ASSERT_THAT(dialog_controller->GetLocalForms(),
              ElementsAre(Pointee(test_local_form())));
  EXPECT_THAT(dialog_controller->GetFederationsForms(), testing::IsEmpty());
  ExpectIconStateIs(password_manager::ui::INACTIVE_STATE);

  EXPECT_CALL(dialog_prompt(), ControllerGone());
  EXPECT_CALL(*this, CredentialCallback(Pointee(test_local_form())));
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  dialog_controller->OnChooseCredentials(
      *dialog_controller->GetLocalForms()[0],
      password_manager::CredentialType::CREDENTIAL_TYPE_PASSWORD);
  EXPECT_EQ(password_manager::ui::MANAGE_STATE, controller()->GetState());
}

TEST_F(ManagePasswordsUIControllerTest, ChooseCredentialLocalButFederated) {
  ScopedVector<autofill::PasswordForm> local_credentials;
  local_credentials.push_back(
      new autofill::PasswordForm(test_federated_form()));
  ScopedVector<autofill::PasswordForm> federated_credentials;
  GURL origin("http://example.com");
  PasswordDialogController* dialog_controller = nullptr;
  EXPECT_CALL(*controller(), CreateAccountChooser(_)).WillOnce(
      DoAll(SaveArg<0>(&dialog_controller), Return(&dialog_prompt())));
  EXPECT_CALL(dialog_prompt(), ShowAccountChooser());
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  EXPECT_TRUE(controller()->OnChooseCredentials(
      std::move(local_credentials), std::move(federated_credentials), origin,
      base::Bind(&ManagePasswordsUIControllerTest::CredentialCallback,
                 base::Unretained(this))));
  EXPECT_EQ(password_manager::ui::CREDENTIAL_REQUEST_STATE,
            controller()->GetState());
  EXPECT_EQ(origin, controller()->GetOrigin());
  EXPECT_THAT(controller()->GetCurrentForms(),
              ElementsAre(Pointee(test_federated_form())));
  ASSERT_THAT(dialog_controller->GetLocalForms(),
              ElementsAre(Pointee(test_federated_form())));
  EXPECT_THAT(dialog_controller->GetFederationsForms(), testing::IsEmpty());
  ExpectIconStateIs(password_manager::ui::INACTIVE_STATE);

  EXPECT_CALL(dialog_prompt(), ControllerGone());
  EXPECT_CALL(*this, CredentialCallback(Pointee(test_federated_form())));
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  dialog_controller->OnChooseCredentials(
      *dialog_controller->GetLocalForms()[0],
      password_manager::CredentialType::CREDENTIAL_TYPE_PASSWORD);
  EXPECT_EQ(password_manager::ui::MANAGE_STATE, controller()->GetState());
}

TEST_F(ManagePasswordsUIControllerTest, ChooseCredentialCancel) {
  ScopedVector<autofill::PasswordForm> local_credentials;
  local_credentials.push_back(new autofill::PasswordForm(test_local_form()));
  ScopedVector<autofill::PasswordForm> federated_credentials;
  GURL origin("http://example.com");
  PasswordDialogController* dialog_controller = nullptr;
  EXPECT_CALL(*controller(), CreateAccountChooser(_)).WillOnce(
      DoAll(SaveArg<0>(&dialog_controller), Return(&dialog_prompt())));
  EXPECT_CALL(dialog_prompt(), ShowAccountChooser());
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  EXPECT_TRUE(controller()->OnChooseCredentials(
      std::move(local_credentials), std::move(federated_credentials), origin,
      base::Bind(&ManagePasswordsUIControllerTest::CredentialCallback,
                 base::Unretained(this))));
  EXPECT_EQ(password_manager::ui::CREDENTIAL_REQUEST_STATE,
            controller()->GetState());
  EXPECT_EQ(origin, controller()->GetOrigin());

  EXPECT_CALL(dialog_prompt(), ControllerGone()).Times(0);
  EXPECT_CALL(*this, CredentialCallback(nullptr));
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  dialog_controller->OnCloseDialog();
  EXPECT_EQ(password_manager::ui::MANAGE_STATE, controller()->GetState());
}

TEST_F(ManagePasswordsUIControllerTest, AutoSignin) {
  ScopedVector<autofill::PasswordForm> local_credentials;
  local_credentials.push_back(new autofill::PasswordForm(test_local_form()));
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  controller()->OnAutoSignin(std::move(local_credentials),
                             test_local_form().origin);
  EXPECT_EQ(password_manager::ui::AUTO_SIGNIN_STATE, controller()->GetState());
  EXPECT_EQ(test_local_form().origin, controller()->GetOrigin());
  ASSERT_FALSE(controller()->GetCurrentForms().empty());
  EXPECT_EQ(test_local_form(), *controller()->GetCurrentForms()[0]);
  ExpectIconStateIs(password_manager::ui::AUTO_SIGNIN_STATE);
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  controller()->OnBubbleHidden();
  ExpectIconAndControllerStateIs(password_manager::ui::MANAGE_STATE);
}

TEST_F(ManagePasswordsUIControllerTest, AutoSigninFirstRun) {
  EXPECT_CALL(*controller(), CreateAutoSigninPrompt(_)).WillOnce(
      Return(&dialog_prompt()));
  EXPECT_CALL(dialog_prompt(), ShowAutoSigninPrompt());
  controller()->OnPromptEnableAutoSignin();

  EXPECT_EQ(password_manager::ui::INACTIVE_STATE, controller()->GetState());
  EXPECT_CALL(dialog_prompt(), ControllerGone());
}

TEST_F(ManagePasswordsUIControllerTest, AutoSigninFirstRunAfterAutofill) {
  // Setup the managed state first.
  std::unique_ptr<autofill::PasswordForm> test_form(
      new autofill::PasswordForm(test_local_form()));
  autofill::PasswordForm* test_form_ptr = test_form.get();
  const base::string16 kTestUsername = test_form->username_value;
  autofill::PasswordFormMap map;
  map.insert(std::make_pair(kTestUsername, std::move(test_form)));
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  controller()->OnPasswordAutofilled(map, test_form_ptr->origin, nullptr);
  EXPECT_EQ(password_manager::ui::MANAGE_STATE, controller()->GetState());

  // Pop up the autosignin promo. The state should stay intact.
  EXPECT_CALL(*controller(), CreateAutoSigninPrompt(_)).WillOnce(
      Return(&dialog_prompt()));
  EXPECT_CALL(dialog_prompt(), ShowAutoSigninPrompt());
  controller()->OnPromptEnableAutoSignin();

  EXPECT_EQ(password_manager::ui::MANAGE_STATE, controller()->GetState());
  EXPECT_EQ(test_form_ptr->origin, controller()->GetOrigin());
  EXPECT_THAT(controller()->GetCurrentForms(),
              ElementsAre(Pointee(*test_form_ptr)));
  EXPECT_CALL(dialog_prompt(), ControllerGone());
}

TEST_F(ManagePasswordsUIControllerTest, AutoSigninFirstRunAfterNavigation) {
  // Pop up the autosignin promo.
  EXPECT_CALL(*controller(), CreateAutoSigninPrompt(_)).WillOnce(
      Return(&dialog_prompt()));
  EXPECT_CALL(dialog_prompt(), ShowAutoSigninPrompt());
  controller()->OnPromptEnableAutoSignin();

  // The dialog should survive any navigation.
  EXPECT_CALL(dialog_prompt(), ControllerGone()).Times(0);
  content::FrameNavigateParams params;
  params.transition = ui::PAGE_TRANSITION_LINK;
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  controller()->DidNavigateMainFrame(content::LoadCommittedDetails(), params);
  ASSERT_TRUE(testing::Mock::VerifyAndClearExpectations(&dialog_prompt()));
  EXPECT_CALL(dialog_prompt(), ControllerGone());
}

TEST_F(ManagePasswordsUIControllerTest, AutofillDuringAutoSignin) {
  ScopedVector<autofill::PasswordForm> local_credentials;
  local_credentials.push_back(new autofill::PasswordForm(test_local_form()));
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  controller()->OnAutoSignin(std::move(local_credentials),
                             test_local_form().origin);
  ExpectIconAndControllerStateIs(password_manager::ui::AUTO_SIGNIN_STATE);
  std::unique_ptr<autofill::PasswordForm> test_form(
      new autofill::PasswordForm(test_local_form()));
  autofill::PasswordFormMap map;
  base::string16 kTestUsername = test_form->username_value;
  map.insert(std::make_pair(kTestUsername, std::move(test_form)));
  controller()->OnPasswordAutofilled(map, map.begin()->second->origin, nullptr);

  ExpectIconAndControllerStateIs(password_manager::ui::AUTO_SIGNIN_STATE);
}

TEST_F(ManagePasswordsUIControllerTest, InactiveOnPSLMatched) {
  base::string16 kTestUsername = base::ASCIIToUTF16("test_username");
  autofill::PasswordFormMap map;
  std::unique_ptr<autofill::PasswordForm> psl_matched_test_form(
      new autofill::PasswordForm(test_local_form()));
  psl_matched_test_form->is_public_suffix_match = true;
  map.insert(std::make_pair(kTestUsername, std::move(psl_matched_test_form)));
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  controller()->OnPasswordAutofilled(map, map.begin()->second->origin, nullptr);

  EXPECT_EQ(password_manager::ui::INACTIVE_STATE, controller()->GetState());
}

TEST_F(ManagePasswordsUIControllerTest, UpdatePasswordSubmitted) {
  std::unique_ptr<password_manager::PasswordFormManager> test_form_manager(
      CreateFormManager());
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  controller()->OnUpdatePasswordSubmitted(std::move(test_form_manager));
  EXPECT_EQ(password_manager::ui::PENDING_PASSWORD_UPDATE_STATE,
            controller()->GetState());

  ExpectIconStateIs(password_manager::ui::PENDING_PASSWORD_UPDATE_STATE);
}

TEST_F(ManagePasswordsUIControllerTest, PasswordUpdated) {
  std::unique_ptr<password_manager::PasswordFormManager> test_form_manager(
      CreateFormManager());
  test_form_manager->ProvisionallySave(
      test_local_form(),
      password_manager::PasswordFormManager::IGNORE_OTHER_POSSIBLE_USERNAMES);
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  controller()->OnUpdatePasswordSubmitted(std::move(test_form_manager));

  ExpectIconStateIs(password_manager::ui::PENDING_PASSWORD_UPDATE_STATE);
  EXPECT_CALL(*controller(), OnUpdateBubbleAndIconVisibility());
  controller()->UpdatePassword(autofill::PasswordForm());
  ExpectIconStateIs(password_manager::ui::MANAGE_STATE);
}

TEST_F(ManagePasswordsUIControllerTest, SavePendingStatePasswordAutofilled) {
  TestNotChangingStateOnAutofill(password_manager::ui::PENDING_PASSWORD_STATE);
}

TEST_F(ManagePasswordsUIControllerTest, UpdatePendingStatePasswordAutofilled) {
  TestNotChangingStateOnAutofill(
      password_manager::ui::PENDING_PASSWORD_UPDATE_STATE);
}

TEST_F(ManagePasswordsUIControllerTest, ConfirmationStatePasswordAutofilled) {
  TestNotChangingStateOnAutofill(password_manager::ui::CONFIRMATION_STATE);
}
