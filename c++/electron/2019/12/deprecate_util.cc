// Copyright (c) 2019 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/common/deprecate_util.h"

#include "gin/dictionary.h"
#include "shell/common/gin_converters/callback_converter.h"

namespace electron {

void EmitDeprecationWarning(node::Environment* env,
                            const std::string& warning_msg,
                            const std::string& warning_type) {
  gin::Dictionary process(env->isolate(), env->process_object());

  base::RepeatingCallback<void(base::StringPiece, base::StringPiece,
                               base::StringPiece)>
      emit_warning;
  process.Get("emitWarning", &emit_warning);

  emit_warning.Run(warning_msg, warning_type, "");
}

}  // namespace electron
