// Copyright (c) 2016 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef ATOM_RENDERER_API_ATOM_API_RENDERER_IPC_H_
#define ATOM_RENDERER_API_ATOM_API_RENDERER_IPC_H_

#include <string>

#include "base/values.h"
#include "native_mate/arguments.h"

namespace atom {

namespace api {

void Send(mate::Arguments* args,
          const std::string& channel,
          const base::ListValue& arguments);

base::ListValue SendSync(mate::Arguments* args,
                         const std::string& channel,
                         const base::ListValue& arguments);

void SendTo(mate::Arguments* args,
            bool internal,
            bool send_to_all,
            int32_t web_contents_id,
            const std::string& channel,
            const base::ListValue& arguments);

}  // namespace api

}  // namespace atom

#endif  // ATOM_RENDERER_API_ATOM_API_RENDERER_IPC_H_
