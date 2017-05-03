// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/common/shelf/shelf_constants.h"
#include "ash/common/shelf/wm_shelf.h"
#include "ash/common/system/toast/toast_manager.h"
#include "ash/common/wm/wm_screen_util.h"
#include "ash/common/wm_shell.h"
#include "ash/display/display_manager.h"
#include "ash/screen_util.h"
#include "ash/shell.h"
#include "ash/test/ash_test_base.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "ui/compositor/scoped_animation_duration_scale_mode.h"
#include "ui/views/widget/widget.h"

namespace ash {

class DummyEvent : public ui::Event {
 public:
  DummyEvent() : Event(ui::ET_UNKNOWN, base::TimeTicks(), 0) {}
  ~DummyEvent() override {}
};

class ToastManagerTest : public test::AshTestBase {
 public:
  ToastManagerTest() {}
  ~ToastManagerTest() override {}

 private:
  void SetUp() override {
    test::AshTestBase::SetUp();

    manager_ = WmShell::Get()->toast_manager();

    manager_->ResetSerialForTesting();
    EXPECT_EQ(0, GetToastSerial());
  }

 protected:
  ToastManager* manager() { return manager_; }

  int GetToastSerial() { return manager_->serial_for_testing(); }

  ToastOverlay* GetCurrentOverlay() {
    return manager_->GetCurrentOverlayForTesting();
  }

  views::Widget* GetCurrentWidget() {
    ToastOverlay* overlay = GetCurrentOverlay();
    return overlay ? overlay->widget_for_testing() : nullptr;
  }

  std::string GetCurrentText() {
    ToastOverlay* overlay = GetCurrentOverlay();
    return overlay ? overlay->text_ : std::string();
  }

  std::string GetCurrentDismissText() {
    ToastOverlay* overlay = GetCurrentOverlay();
    return overlay ? overlay->dismiss_text_ : std::string();
  }

  void ClickDismissButton() {
    ToastOverlay* overlay = GetCurrentOverlay();
    if (overlay)
      overlay->ClickDismissButtonForTesting(DummyEvent());
  }

  std::string ShowToast(const std::string& text, int32_t duration) {
    std::string id = "TOAST_ID_" + base::UintToString(serial_++);
    manager()->Show(ToastData(id, text, duration, ""));
    return id;
  }

  std::string ShowToastWithDismiss(const std::string& text,
                                   int32_t duration,
                                   const std::string& dismiss_text) {
    std::string id = "TOAST_ID_" + base::UintToString(serial_++);
    manager()->Show(ToastData(id, text, duration, dismiss_text));
    return id;
  }

  void CancelToast(const std::string& id) { manager()->Cancel(id); }

 private:
  ToastManager* manager_ = nullptr;
  unsigned int serial_ = 0;

  DISALLOW_COPY_AND_ASSIGN(ToastManagerTest);
};

TEST_F(ToastManagerTest, ShowAndCloseAutomatically) {
  ShowToast("DUMMY", 10);

  EXPECT_EQ(1, GetToastSerial());

  while (GetCurrentOverlay() != nullptr)
    base::RunLoop().RunUntilIdle();
}

TEST_F(ToastManagerTest, ShowAndCloseManually) {
  ShowToast("DUMMY", ToastData::kInfiniteDuration);

  EXPECT_EQ(1, GetToastSerial());

  EXPECT_FALSE(GetCurrentWidget()->GetLayer()->GetAnimator()->is_animating());

  ClickDismissButton();

  EXPECT_EQ(nullptr, GetCurrentOverlay());
}

TEST_F(ToastManagerTest, ShowAndCloseManuallyDuringAnimation) {
  ui::ScopedAnimationDurationScaleMode slow_animation_duration(
      ui::ScopedAnimationDurationScaleMode::SLOW_DURATION);

  ShowToast("DUMMY", ToastData::kInfiniteDuration);
  EXPECT_TRUE(GetCurrentWidget()->GetLayer()->GetAnimator()->is_animating());
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(1, GetToastSerial());
  EXPECT_TRUE(GetCurrentWidget()->GetLayer()->GetAnimator()->is_animating());

  // Try to close it during animation.
  ClickDismissButton();

  while (GetCurrentWidget()->GetLayer()->GetAnimator()->is_animating())
    base::RunLoop().RunUntilIdle();

  // Toast isn't closed.
  EXPECT_TRUE(GetCurrentOverlay() != nullptr);
}

TEST_F(ToastManagerTest, QueueMessage) {
  ShowToast("DUMMY1", 10);
  ShowToast("DUMMY2", 10);
  ShowToast("DUMMY3", 10);

  EXPECT_EQ(1, GetToastSerial());
  EXPECT_EQ("DUMMY1", GetCurrentText());

  while (GetToastSerial() != 2)
    base::RunLoop().RunUntilIdle();

  EXPECT_EQ("DUMMY2", GetCurrentText());

  while (GetToastSerial() != 3)
    base::RunLoop().RunUntilIdle();

  EXPECT_EQ("DUMMY3", GetCurrentText());
}

TEST_F(ToastManagerTest, PositionWithVisibleBottomShelf) {
  WmShelf* shelf = GetPrimaryShelf();
  EXPECT_EQ(SHELF_ALIGNMENT_BOTTOM, shelf->GetAlignment());
  EXPECT_EQ(SHELF_VISIBLE, shelf->GetVisibilityState());

  ShowToast("DUMMY", ToastData::kInfiniteDuration);
  EXPECT_EQ(1, GetToastSerial());

  gfx::Rect toast_bounds = GetCurrentWidget()->GetWindowBoundsInScreen();
  gfx::Rect root_bounds = wm::GetDisplayBoundsWithShelf(shelf->GetWindow());

  EXPECT_TRUE(toast_bounds.Intersects(shelf->GetUserWorkAreaBounds()));
  EXPECT_NEAR(root_bounds.CenterPoint().x(), toast_bounds.CenterPoint().x(), 1);

  if (SupportsHostWindowResize()) {
    // If host resize is not supported, ShelfLayoutManager::GetIdealBounds()
    // doesn't return correct value.
    gfx::Rect shelf_bounds = shelf->GetIdealBounds();
    EXPECT_FALSE(toast_bounds.Intersects(shelf_bounds));
    EXPECT_EQ(shelf_bounds.y() - 5, toast_bounds.bottom());
    EXPECT_EQ(root_bounds.bottom() - shelf_bounds.height() - 5,
              toast_bounds.bottom());
  }
}

TEST_F(ToastManagerTest, PositionWithAutoHiddenBottomShelf) {
  std::unique_ptr<aura::Window> window(
      CreateTestWindowInShellWithBounds(gfx::Rect(1, 2, 3, 4)));

  WmShelf* shelf = GetPrimaryShelf();
  EXPECT_EQ(SHELF_ALIGNMENT_BOTTOM, shelf->GetAlignment());
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_BEHAVIOR_ALWAYS);
  EXPECT_EQ(SHELF_AUTO_HIDE_HIDDEN, shelf->GetAutoHideState());

  ShowToast("DUMMY", ToastData::kInfiniteDuration);
  EXPECT_EQ(1, GetToastSerial());

  gfx::Rect toast_bounds = GetCurrentWidget()->GetWindowBoundsInScreen();
  gfx::Rect root_bounds = wm::GetDisplayBoundsWithShelf(shelf->GetWindow());

  EXPECT_TRUE(toast_bounds.Intersects(shelf->GetUserWorkAreaBounds()));
  EXPECT_NEAR(root_bounds.CenterPoint().x(), toast_bounds.CenterPoint().x(), 1);
  EXPECT_EQ(root_bounds.bottom() - kShelfAutoHideSize - 5,
            toast_bounds.bottom());
}

TEST_F(ToastManagerTest, PositionWithHiddenBottomShelf) {
  WmShelf* shelf = GetPrimaryShelf();
  EXPECT_EQ(SHELF_ALIGNMENT_BOTTOM, shelf->GetAlignment());
  shelf->SetAutoHideBehavior(SHELF_AUTO_HIDE_ALWAYS_HIDDEN);
  EXPECT_EQ(SHELF_HIDDEN, shelf->GetVisibilityState());

  ShowToast("DUMMY", ToastData::kInfiniteDuration);
  EXPECT_EQ(1, GetToastSerial());

  gfx::Rect toast_bounds = GetCurrentWidget()->GetWindowBoundsInScreen();
  gfx::Rect root_bounds = wm::GetDisplayBoundsWithShelf(shelf->GetWindow());

  EXPECT_TRUE(toast_bounds.Intersects(shelf->GetUserWorkAreaBounds()));
  EXPECT_NEAR(root_bounds.CenterPoint().x(), toast_bounds.CenterPoint().x(), 1);
  EXPECT_EQ(root_bounds.bottom() - 5, toast_bounds.bottom());
}

TEST_F(ToastManagerTest, PositionWithVisibleLeftShelf) {
  WmShelf* shelf = GetPrimaryShelf();
  EXPECT_EQ(SHELF_VISIBLE, shelf->GetVisibilityState());
  shelf->SetAlignment(SHELF_ALIGNMENT_LEFT);

  ShowToast("DUMMY", ToastData::kInfiniteDuration);
  EXPECT_EQ(1, GetToastSerial());

  gfx::Rect toast_bounds = GetCurrentWidget()->GetWindowBoundsInScreen();
  gfx::RectF precise_toast_bounds(toast_bounds);
  gfx::Rect root_bounds = wm::GetDisplayBoundsWithShelf(shelf->GetWindow());

  EXPECT_TRUE(toast_bounds.Intersects(shelf->GetUserWorkAreaBounds()));
  EXPECT_EQ(root_bounds.bottom() - 5, toast_bounds.bottom());

  if (SupportsHostWindowResize()) {
    // If host resize is not supported then calling WmShelf::GetIdealBounds()
    // doesn't return correct value.
    gfx::Rect shelf_bounds = shelf->GetIdealBounds();
    EXPECT_FALSE(toast_bounds.Intersects(shelf_bounds));
    EXPECT_EQ(round(shelf_bounds.right() +
                    (root_bounds.width() - shelf_bounds.width()) / 2.0),
              round(precise_toast_bounds.CenterPoint().x()));
  }
}

TEST_F(ToastManagerTest, PositionWithUnifiedDesktop) {
  if (!SupportsMultipleDisplays())
    return;

  DisplayManager* display_manager = Shell::GetInstance()->display_manager();
  display_manager->SetUnifiedDesktopEnabled(true);
  UpdateDisplay("1000x500,0+600-100x500");

  WmShelf* shelf = GetPrimaryShelf();
  EXPECT_EQ(SHELF_ALIGNMENT_BOTTOM, shelf->GetAlignment());
  EXPECT_EQ(SHELF_VISIBLE, shelf->GetVisibilityState());

  ShowToast("DUMMY", ToastData::kInfiniteDuration);
  EXPECT_EQ(1, GetToastSerial());

  gfx::Rect toast_bounds = GetCurrentWidget()->GetWindowBoundsInScreen();
  gfx::Rect root_bounds = wm::GetDisplayBoundsWithShelf(shelf->GetWindow());

  EXPECT_TRUE(toast_bounds.Intersects(shelf->GetUserWorkAreaBounds()));
  EXPECT_TRUE(root_bounds.Contains(toast_bounds));
  EXPECT_NEAR(root_bounds.CenterPoint().x(), toast_bounds.CenterPoint().x(), 1);

  if (SupportsHostWindowResize()) {
    // If host resize is not supported then calling WmShelf::GetIdealBounds()
    // doesn't return correct value.
    gfx::Rect shelf_bounds = shelf->GetIdealBounds();
    EXPECT_FALSE(toast_bounds.Intersects(shelf_bounds));
    EXPECT_EQ(shelf_bounds.y() - 5, toast_bounds.bottom());
    EXPECT_EQ(root_bounds.bottom() - shelf_bounds.height() - 5,
              toast_bounds.bottom());
  }
}

TEST_F(ToastManagerTest, CancelToast) {
  std::string id1 = ShowToast("TEXT1", ToastData::kInfiniteDuration);
  std::string id2 = ShowToast("TEXT2", ToastData::kInfiniteDuration);
  std::string id3 = ShowToast("TEXT3", ToastData::kInfiniteDuration);

  // Confirm that the first toast is shown.
  EXPECT_EQ("TEXT1", GetCurrentText());
  // Cancel the queued toast.
  CancelToast(id2);
  // Confirm that the shown toast is still visible.
  EXPECT_EQ("TEXT1", GetCurrentText());
  // Cancel the shown toast.
  CancelToast(id1);
  // Confirm that the next toast is visible.
  EXPECT_EQ("TEXT3", GetCurrentText());
  // Cancel the shown toast.
  CancelToast(id3);
  // Confirm that the shown toast disappears.
  EXPECT_FALSE(GetCurrentOverlay());
  // Confirm that only 1 toast is shown.
  EXPECT_EQ(2, GetToastSerial());
}

}  // namespace ash
