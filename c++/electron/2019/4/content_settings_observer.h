// Copyright (c) 2016 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ATOM_RENDERER_CONTENT_SETTINGS_OBSERVER_H_
#define ATOM_RENDERER_CONTENT_SETTINGS_OBSERVER_H_

#include "base/compiler_specific.h"
#include "content/public/renderer/render_frame_observer.h"
#include "third_party/blink/public/platform/web_content_settings_client.h"

namespace atom {

class ContentSettingsObserver : public content::RenderFrameObserver,
                                public blink::WebContentSettingsClient {
 public:
  explicit ContentSettingsObserver(content::RenderFrame* render_frame);
  ~ContentSettingsObserver() override;

  // blink::WebContentSettingsClient implementation.
  bool AllowDatabase() override;
  bool AllowStorage(bool local) override;
  bool AllowIndexedDB(const blink::WebSecurityOrigin& security_origin) override;

 private:
  // content::RenderFrameObserver implementation.
  void OnDestruct() override;

  DISALLOW_COPY_AND_ASSIGN(ContentSettingsObserver);
};

}  // namespace atom

#endif  // ATOM_RENDERER_CONTENT_SETTINGS_OBSERVER_H_
