// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/common/accelerators/exit_warning_handler.h"

#include "ash/common/shell_delegate.h"
#include "ash/common/shell_window_ids.h"
#include "ash/common/wm_root_window_controller.h"
#include "ash/common/wm_shell.h"
#include "ash/common/wm_window.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "grit/ash_strings.h"
#include "ui/accessibility/ax_view_state.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace ash {
namespace {

const int64_t kTimeOutMilliseconds = 2000;
// Color of the text of the warning message.
const SkColor kTextColor = SK_ColorWHITE;
// Color of the window background.
const SkColor kWindowBackgroundColor = SkColorSetARGB(0xC0, 0x0, 0x0, 0x0);
// Radius of the rounded corners of the window.
const int kWindowCornerRadius = 2;
const int kHorizontalMarginAroundText = 100;
const int kVerticalMarginAroundText = 100;

class ExitWarningWidgetDelegateView : public views::WidgetDelegateView {
 public:
  ExitWarningWidgetDelegateView() : text_width_(0), width_(0), height_(0) {
#ifdef OS_CHROMEOS
    text_ = l10n_util::GetStringUTF16(IDS_ASH_SIGN_OUT_WARNING_POPUP_TEXT);
    accessible_name_ = l10n_util::GetStringUTF16(
        IDS_ASH_SIGN_OUT_WARNING_POPUP_TEXT_ACCESSIBLE);
#else
    text_ = l10n_util::GetStringUTF16(IDS_ASH_EXIT_WARNING_POPUP_TEXT);
    accessible_name_ =
        l10n_util::GetStringUTF16(IDS_ASH_EXIT_WARNING_POPUP_TEXT_ACCESSIBLE);
#endif
    ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
    const gfx::FontList& font_list =
        rb.GetFontList(ui::ResourceBundle::LargeFont);
    text_width_ = gfx::GetStringWidth(text_, font_list);
    width_ = text_width_ + kHorizontalMarginAroundText;
    height_ = font_list.GetHeight() + kVerticalMarginAroundText;
    views::Label* label = new views::Label();
    label->SetText(text_);
    label->SetHorizontalAlignment(gfx::ALIGN_CENTER);
    label->SetFontList(font_list);
    label->SetEnabledColor(kTextColor);
    label->SetDisabledColor(kTextColor);
    label->SetAutoColorReadabilityEnabled(false);
    label->SetSubpixelRenderingEnabled(false);
    AddChildView(label);
    SetLayoutManager(new views::FillLayout);
  }

  gfx::Size GetPreferredSize() const override {
    return gfx::Size(width_, height_);
  }

  void OnPaint(gfx::Canvas* canvas) override {
    SkPaint paint;
    paint.setStyle(SkPaint::kFill_Style);
    paint.setColor(kWindowBackgroundColor);
    canvas->DrawRoundRect(GetLocalBounds(), kWindowCornerRadius, paint);
    views::WidgetDelegateView::OnPaint(canvas);
  }

  void GetAccessibleState(ui::AXViewState* state) override {
    state->name = accessible_name_;
    state->role = ui::AX_ROLE_ALERT;
  }

 private:
  base::string16 text_;
  base::string16 accessible_name_;
  int text_width_;
  int width_;
  int height_;

  DISALLOW_COPY_AND_ASSIGN(ExitWarningWidgetDelegateView);
};

}  // namespace

ExitWarningHandler::ExitWarningHandler()
    : state_(IDLE), stub_timer_for_test_(false) {}

ExitWarningHandler::~ExitWarningHandler() {
  // Note: If a timer is outstanding, it is stopped in its destructor.
  Hide();
}

void ExitWarningHandler::HandleAccelerator() {
  switch (state_) {
    case IDLE:
      state_ = WAIT_FOR_DOUBLE_PRESS;
      Show();
      StartTimer();
      WmShell::Get()->RecordUserMetricsAction(UMA_ACCEL_EXIT_FIRST_Q);
      break;
    case WAIT_FOR_DOUBLE_PRESS:
      state_ = EXITING;
      CancelTimer();
      Hide();
      WmShell::Get()->RecordUserMetricsAction(UMA_ACCEL_EXIT_SECOND_Q);
      WmShell::Get()->delegate()->Exit();
      break;
    case EXITING:
      break;
  }
}

void ExitWarningHandler::TimerAction() {
  Hide();
  if (state_ == WAIT_FOR_DOUBLE_PRESS)
    state_ = IDLE;
}

void ExitWarningHandler::StartTimer() {
  if (stub_timer_for_test_)
    return;
  timer_.Start(FROM_HERE,
               base::TimeDelta::FromMilliseconds(kTimeOutMilliseconds), this,
               &ExitWarningHandler::TimerAction);
}

void ExitWarningHandler::CancelTimer() {
  timer_.Stop();
}

void ExitWarningHandler::Show() {
  if (widget_)
    return;
  WmWindow* root_window = WmShell::Get()->GetRootWindowForNewWindows();
  ExitWarningWidgetDelegateView* delegate = new ExitWarningWidgetDelegateView;
  gfx::Size rs = root_window->GetBounds().size();
  gfx::Size ps = delegate->GetPreferredSize();
  gfx::Rect bounds((rs.width() - ps.width()) / 2,
                   (rs.height() - ps.height()) / 3, ps.width(), ps.height());
  views::Widget::InitParams params;
  params.type = views::Widget::InitParams::TYPE_POPUP;
  params.opacity = views::Widget::InitParams::TRANSLUCENT_WINDOW;
  params.ownership = views::Widget::InitParams::WIDGET_OWNS_NATIVE_WIDGET;
  params.accept_events = false;
  params.keep_on_top = true;
  params.remove_standard_frame = true;
  params.delegate = delegate;
  params.bounds = bounds;
  params.name = "ExitWarningWindow";
  widget_.reset(new views::Widget);
  root_window->GetRootWindowController()->ConfigureWidgetInitParamsForContainer(
      widget_.get(), kShellWindowId_SettingBubbleContainer, &params);
  widget_->Init(params);
  widget_->SetContentsView(delegate);
  widget_->Show();

  delegate->NotifyAccessibilityEvent(ui::AX_EVENT_ALERT, true);
}

void ExitWarningHandler::Hide() {
  widget_.reset();
}

}  // namespace ash
