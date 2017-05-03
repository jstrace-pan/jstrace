// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/renderer/pepper/pepper_platform_video_capture.h"

#include "base/bind.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "content/renderer/media/video_capture_impl_manager.h"
#include "content/renderer/pepper/pepper_media_device_manager.h"
#include "content/renderer/pepper/pepper_video_capture_host.h"
#include "content/renderer/render_frame_impl.h"
#include "content/renderer/render_thread_impl.h"
#include "media/base/bind_to_current_loop.h"
#include "media/base/video_frame.h"
#include "url/gurl.h"

namespace content {

PepperPlatformVideoCapture::PepperPlatformVideoCapture(
    int render_frame_id,
    const std::string& device_id,
    const GURL& document_url,
    PepperVideoCaptureHost* handler)
    : render_frame_id_(render_frame_id),
      device_id_(device_id),
      session_id_(0),
      handler_(handler),
      pending_open_device_(false),
      pending_open_device_id_(-1),
      weak_factory_(this) {
  // We need to open the device and obtain the label and session ID before
  // initializing.
  PepperMediaDeviceManager* const device_manager = GetMediaDeviceManager();
  if (device_manager) {
    pending_open_device_id_ = device_manager->OpenDevice(
        PP_DEVICETYPE_DEV_VIDEOCAPTURE,
        device_id,
        document_url,
        base::Bind(&PepperPlatformVideoCapture::OnDeviceOpened,
                   weak_factory_.GetWeakPtr()));
    pending_open_device_ = true;
  }
}

void PepperPlatformVideoCapture::StartCapture(
    const media::VideoCaptureParams& params) {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (!stop_capture_cb_.is_null())
    return;
  VideoCaptureImplManager* manager =
      RenderThreadImpl::current()->video_capture_impl_manager();
  stop_capture_cb_ =
      manager->StartCapture(session_id_,
                            params,
                            media::BindToCurrentLoop(base::Bind(
                                &PepperPlatformVideoCapture::OnStateUpdate,
                                weak_factory_.GetWeakPtr())),
                            media::BindToCurrentLoop(base::Bind(
                                &PepperPlatformVideoCapture::OnFrameReady,
                                weak_factory_.GetWeakPtr())));
}

void PepperPlatformVideoCapture::StopCapture() {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (stop_capture_cb_.is_null())
    return;
  stop_capture_cb_.Run();
  stop_capture_cb_.Reset();
}

void PepperPlatformVideoCapture::DetachEventHandler() {
  handler_ = NULL;
  StopCapture();
  if (!release_device_cb_.is_null()) {
    release_device_cb_.Run();
    release_device_cb_.Reset();
  }
  if (!label_.empty()) {
    PepperMediaDeviceManager* const device_manager = GetMediaDeviceManager();
    if (device_manager)
      device_manager->CloseDevice(label_);
    label_.clear();
  }
  if (pending_open_device_) {
    PepperMediaDeviceManager* const device_manager = GetMediaDeviceManager();
    if (device_manager)
      device_manager->CancelOpenDevice(pending_open_device_id_);
    pending_open_device_ = false;
    pending_open_device_id_ = -1;
  }
}

PepperPlatformVideoCapture::~PepperPlatformVideoCapture() {
  DCHECK(stop_capture_cb_.is_null());
  DCHECK(release_device_cb_.is_null());
  DCHECK(label_.empty());
  DCHECK(!pending_open_device_);
}

void PepperPlatformVideoCapture::OnDeviceOpened(int request_id,
                                                bool succeeded,
                                                const std::string& label) {
  pending_open_device_ = false;
  pending_open_device_id_ = -1;

  PepperMediaDeviceManager* const device_manager = GetMediaDeviceManager();
  succeeded = succeeded && device_manager;
  if (succeeded) {
    label_ = label;
    session_id_ = device_manager->GetSessionID(
        PP_DEVICETYPE_DEV_VIDEOCAPTURE, label);
    VideoCaptureImplManager* manager =
        RenderThreadImpl::current()->video_capture_impl_manager();
    release_device_cb_ = manager->UseDevice(session_id_);
  }

  if (handler_)
    handler_->OnInitialized(succeeded);
}

void PepperPlatformVideoCapture::OnStateUpdate(VideoCaptureState state) {
  if (!handler_)
    return;
  switch (state) {
    case VIDEO_CAPTURE_STATE_STARTED:
      handler_->OnStarted();
      break;
    case VIDEO_CAPTURE_STATE_STOPPED:
      handler_->OnStopped();
      break;
    case VIDEO_CAPTURE_STATE_PAUSED:
      handler_->OnPaused();
      break;
    case VIDEO_CAPTURE_STATE_ERROR:
      handler_->OnError();
      break;
    default:
      NOTREACHED() << "Unexpected state: " << state << ".";
  }
}

void PepperPlatformVideoCapture::OnFrameReady(
    const scoped_refptr<media::VideoFrame>& frame,
    base::TimeTicks estimated_capture_time) {
  if (handler_ && !stop_capture_cb_.is_null())
    handler_->OnFrameReady(frame);
}

PepperMediaDeviceManager* PepperPlatformVideoCapture::GetMediaDeviceManager() {
  RenderFrameImpl* const render_frame =
      RenderFrameImpl::FromRoutingID(render_frame_id_);
  return render_frame ?
      PepperMediaDeviceManager::GetForRenderFrame(render_frame).get() : NULL;
}

}  // namespace content
