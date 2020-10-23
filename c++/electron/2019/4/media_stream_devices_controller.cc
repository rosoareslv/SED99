// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE-CHROMIUM file.

#include "atom/browser/media/media_stream_devices_controller.h"

#include <memory>
#include <utility>

#include "atom/browser/media/media_capture_devices_dispatcher.h"
#include "content/public/browser/desktop_media_id.h"
#include "content/public/browser/media_stream_request.h"

namespace atom {

namespace {

bool HasAnyAvailableDevice() {
  const blink::MediaStreamDevices& audio_devices =
      MediaCaptureDevicesDispatcher::GetInstance()->GetAudioCaptureDevices();
  const blink::MediaStreamDevices& video_devices =
      MediaCaptureDevicesDispatcher::GetInstance()->GetVideoCaptureDevices();

  return !audio_devices.empty() || !video_devices.empty();
}

}  // namespace

MediaStreamDevicesController::MediaStreamDevicesController(
    const content::MediaStreamRequest& request,
    content::MediaResponseCallback callback)
    : request_(request),
      callback_(std::move(callback)),
      // For MEDIA_OPEN_DEVICE requests (Pepper) we always request both webcam
      // and microphone to avoid popping two infobars.
      microphone_requested_(
          request.audio_type == blink::MEDIA_DEVICE_AUDIO_CAPTURE ||
          request.request_type == blink::MEDIA_OPEN_DEVICE_PEPPER_ONLY),
      webcam_requested_(
          request.video_type == blink::MEDIA_DEVICE_VIDEO_CAPTURE ||
          request.request_type == blink::MEDIA_OPEN_DEVICE_PEPPER_ONLY) {}

MediaStreamDevicesController::~MediaStreamDevicesController() {
  if (!callback_.is_null()) {
    std::move(callback_).Run(blink::MediaStreamDevices(),
                             blink::MEDIA_DEVICE_FAILED_DUE_TO_SHUTDOWN,
                             std::unique_ptr<content::MediaStreamUI>());
  }
}

bool MediaStreamDevicesController::TakeAction() {
  // Do special handling of desktop screen cast.
  if (request_.audio_type == blink::MEDIA_GUM_TAB_AUDIO_CAPTURE ||
      request_.video_type == blink::MEDIA_GUM_TAB_VIDEO_CAPTURE ||
      request_.audio_type == blink::MEDIA_GUM_DESKTOP_AUDIO_CAPTURE ||
      request_.video_type == blink::MEDIA_GUM_DESKTOP_VIDEO_CAPTURE) {
    HandleUserMediaRequest();
    return true;
  }

  // Deny the request if there is no device attached to the OS.
  if (!HasAnyAvailableDevice()) {
    Deny(blink::MEDIA_DEVICE_NO_HARDWARE);
    return true;
  }

  Accept();
  return true;
}

void MediaStreamDevicesController::Accept() {
  // Get the default devices for the request.
  blink::MediaStreamDevices devices;
  if (microphone_requested_ || webcam_requested_) {
    switch (request_.request_type) {
      case blink::MEDIA_OPEN_DEVICE_PEPPER_ONLY: {
        const blink::MediaStreamDevice* device = nullptr;
        // For open device request pick the desired device or fall back to the
        // first available of the given type.
        if (request_.audio_type == blink::MEDIA_DEVICE_AUDIO_CAPTURE) {
          device =
              MediaCaptureDevicesDispatcher::GetInstance()
                  ->GetRequestedAudioDevice(request_.requested_audio_device_id);
          // TODO(wjia): Confirm this is the intended behavior.
          if (!device) {
            device = MediaCaptureDevicesDispatcher::GetInstance()
                         ->GetFirstAvailableAudioDevice();
          }
        } else if (request_.video_type == blink::MEDIA_DEVICE_VIDEO_CAPTURE) {
          // Pepper API opens only one device at a time.
          device =
              MediaCaptureDevicesDispatcher::GetInstance()
                  ->GetRequestedVideoDevice(request_.requested_video_device_id);
          // TODO(wjia): Confirm this is the intended behavior.
          if (!device) {
            device = MediaCaptureDevicesDispatcher::GetInstance()
                         ->GetFirstAvailableVideoDevice();
          }
        }
        if (device)
          devices.push_back(*device);
        break;
      }
      case blink::MEDIA_GENERATE_STREAM: {
        bool needs_audio_device = microphone_requested_;
        bool needs_video_device = webcam_requested_;

        // Get the exact audio or video device if an id is specified.
        if (!request_.requested_audio_device_id.empty()) {
          const blink::MediaStreamDevice* audio_device =
              MediaCaptureDevicesDispatcher::GetInstance()
                  ->GetRequestedAudioDevice(request_.requested_audio_device_id);
          if (audio_device) {
            devices.push_back(*audio_device);
            needs_audio_device = false;
          }
        }
        if (!request_.requested_video_device_id.empty()) {
          const blink::MediaStreamDevice* video_device =
              MediaCaptureDevicesDispatcher::GetInstance()
                  ->GetRequestedVideoDevice(request_.requested_video_device_id);
          if (video_device) {
            devices.push_back(*video_device);
            needs_video_device = false;
          }
        }

        // If either or both audio and video devices were requested but not
        // specified by id, get the default devices.
        if (needs_audio_device || needs_video_device) {
          MediaCaptureDevicesDispatcher::GetInstance()->GetDefaultDevices(
              needs_audio_device, needs_video_device, &devices);
        }
        break;
      }
      case blink::MEDIA_DEVICE_ACCESS: {
        // Get the default devices for the request.
        MediaCaptureDevicesDispatcher::GetInstance()->GetDefaultDevices(
            microphone_requested_, webcam_requested_, &devices);
        break;
      }
      case blink::MEDIA_DEVICE_UPDATE: {
        NOTREACHED();
        break;
      }
    }
  }

  std::move(callback_).Run(devices, blink::MEDIA_DEVICE_OK,
                           std::unique_ptr<content::MediaStreamUI>());
}

void MediaStreamDevicesController::Deny(
    blink::MediaStreamRequestResult result) {
  std::move(callback_).Run(blink::MediaStreamDevices(), result,
                           std::unique_ptr<content::MediaStreamUI>());
}

void MediaStreamDevicesController::HandleUserMediaRequest() {
  blink::MediaStreamDevices devices;

  if (request_.audio_type == blink::MEDIA_GUM_TAB_AUDIO_CAPTURE) {
    devices.push_back(
        blink::MediaStreamDevice(blink::MEDIA_GUM_TAB_AUDIO_CAPTURE, "", ""));
  }
  if (request_.video_type == blink::MEDIA_GUM_TAB_VIDEO_CAPTURE) {
    devices.push_back(
        blink::MediaStreamDevice(blink::MEDIA_GUM_TAB_VIDEO_CAPTURE, "", ""));
  }
  if (request_.audio_type == blink::MEDIA_GUM_DESKTOP_AUDIO_CAPTURE) {
    devices.push_back(blink::MediaStreamDevice(
        blink::MEDIA_GUM_DESKTOP_AUDIO_CAPTURE, "loopback", "System Audio"));
  }
  if (request_.video_type == blink::MEDIA_GUM_DESKTOP_VIDEO_CAPTURE) {
    content::DesktopMediaID screen_id;
    // If the device id wasn't specified then this is a screen capture request
    // (i.e. chooseDesktopMedia() API wasn't used to generate device id).
    if (request_.requested_video_device_id.empty()) {
      screen_id = content::DesktopMediaID(content::DesktopMediaID::TYPE_SCREEN,
                                          -1 /* kFullDesktopScreenId */);
    } else {
      screen_id =
          content::DesktopMediaID::Parse(request_.requested_video_device_id);
    }

    devices.push_back(
        blink::MediaStreamDevice(blink::MEDIA_GUM_DESKTOP_VIDEO_CAPTURE,
                                 screen_id.ToString(), "Screen"));
  }

  std::move(callback_).Run(devices,
                           devices.empty() ? blink::MEDIA_DEVICE_NO_HARDWARE
                                           : blink::MEDIA_DEVICE_OK,
                           std::unique_ptr<content::MediaStreamUI>());
}

}  // namespace atom
