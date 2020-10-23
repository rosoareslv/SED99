// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/app/atom_main.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <vector>

#if defined(OS_WIN)
#include <windows.h>  // windows.h must be included first

#include <atlbase.h>  // ensures that ATL statics like `_AtlWinModule` are initialized (it's an issue in static debug build)
#include <shellapi.h>
#include <shellscalingapi.h>
#include <tchar.h>

#include "base/environment.h"
#include "base/process/launch.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/windows_version.h"
#include "content/public/app/sandbox_helper_win.h"
#include "sandbox/win/src/sandbox_types.h"
#include "shell/app/atom_main_delegate.h"
#include "shell/app/command_line_args.h"
#include "shell/common/crash_reporter/win/crash_service_main.h"
#elif defined(OS_LINUX)  // defined(OS_WIN)
#include <unistd.h>
#include <cstdio>
#include "content/public/app/content_main.h"
#include "shell/app/atom_main_delegate.h"  // NOLINT
#else                                      // defined(OS_LINUX)
#include <mach-o/dyld.h>
#include <unistd.h>
#include <cstdio>
#include "shell/app/atom_library_main.h"
#endif  // defined(OS_MACOSX)

#include "base/at_exit.h"
#include "base/i18n/icu_util.h"
#include "electron/buildflags/buildflags.h"
#include "shell/app/node_main.h"
#include "shell/common/atom_command_line.h"
#include "shell/common/atom_constants.h"

#if defined(HELPER_EXECUTABLE) && !defined(MAS_BUILD)
#include "sandbox/mac/seatbelt_exec.h"  // nogncheck
#endif

namespace {

ALLOW_UNUSED_TYPE bool IsEnvSet(const char* name) {
#if defined(OS_WIN)
  size_t required_size;
  getenv_s(&required_size, nullptr, 0, name);
  return required_size != 0;
#else
  char* indicator = getenv(name);
  return indicator && indicator[0] != '\0';
#endif
}

#if defined(OS_POSIX)
void FixStdioStreams() {
  // libuv may mark stdin/stdout/stderr as close-on-exec, which interferes
  // with chromium's subprocess spawning. As a workaround, we detect if these
  // streams are closed on startup, and reopen them as /dev/null if necessary.
  // Otherwise, an unrelated file descriptor will be assigned as stdout/stderr
  // which may cause various errors when attempting to write to them.
  //
  // For details see https://github.com/libuv/libuv/issues/2062
  struct stat st;
  if (fstat(STDIN_FILENO, &st) < 0 && errno == EBADF)
    ignore_result(freopen("/dev/null", "r", stdin));
  if (fstat(STDOUT_FILENO, &st) < 0 && errno == EBADF)
    ignore_result(freopen("/dev/null", "w", stdout));
  if (fstat(STDERR_FILENO, &st) < 0 && errno == EBADF)
    ignore_result(freopen("/dev/null", "w", stderr));
}
#endif

}  // namespace

#if defined(OS_WIN)

namespace crash_reporter {
extern const char kCrashpadProcess[];
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, wchar_t* cmd, int) {
  struct Arguments {
    int argc = 0;
    wchar_t** argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);

    ~Arguments() { LocalFree(argv); }
  } arguments;

  if (!arguments.argv)
    return -1;

#ifdef _DEBUG
  // Don't display assert dialog boxes in CI test runs
  static const char* kCI = "CI";
  if (IsEnvSet(kCI)) {
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG | _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);

    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_DEBUG | _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);

    _set_error_mode(_OUT_TO_STDERR);
  }
#endif

#if BUILDFLAG(ENABLE_RUN_AS_NODE)
  bool run_as_node = IsEnvSet(electron::kRunAsNode);
#else
  bool run_as_node = false;
#endif

  // Make sure the output is printed to console.
  if (run_as_node || !IsEnvSet("ELECTRON_NO_ATTACH_CONSOLE"))
    base::RouteStdioToConsole(false);

  std::vector<char*> argv(arguments.argc);
  std::transform(arguments.argv, arguments.argv + arguments.argc, argv.begin(),
                 [](auto& a) { return _strdup(base::WideToUTF8(a).c_str()); });
#if BUILDFLAG(ENABLE_RUN_AS_NODE)
  if (run_as_node) {
    base::AtExitManager atexit_manager;
    base::i18n::InitializeICU();
    auto ret = electron::NodeMain(argv.size(), argv.data());
    std::for_each(argv.begin(), argv.end(), free);
    return ret;
  }
#endif

  base::CommandLine::Init(argv.size(), argv.data());
  const base::CommandLine& cmd_line = *base::CommandLine::ForCurrentProcess();
  if (cmd_line.GetSwitchValueASCII("type") ==
      crash_reporter::kCrashpadProcess) {
    return crash_service::Main(&argv);
  }

  if (!electron::CheckCommandLineArguments(arguments.argc, arguments.argv))
    return -1;

  sandbox::SandboxInterfaceInfo sandbox_info = {0};
  content::InitializeSandboxInfo(&sandbox_info);
  electron::AtomMainDelegate delegate;

  content::ContentMainParams params(&delegate);
  params.instance = instance;
  params.sandbox_info = &sandbox_info;
  electron::AtomCommandLine::Init(arguments.argc, arguments.argv);
  return content::ContentMain(params);
}

#elif defined(OS_LINUX)  // defined(OS_WIN)

int main(int argc, char* argv[]) {
  FixStdioStreams();

#if BUILDFLAG(ENABLE_RUN_AS_NODE)
  if (IsEnvSet(electron::kRunAsNode)) {
    base::i18n::InitializeICU();
    base::AtExitManager atexit_manager;
    return electron::NodeMain(argc, argv);
  }
#endif

  electron::AtomMainDelegate delegate;
  content::ContentMainParams params(&delegate);
  params.argc = argc;
  params.argv = const_cast<const char**>(argv);
  electron::AtomCommandLine::Init(argc, argv);
  return content::ContentMain(params);
}

#else  // defined(OS_LINUX)

int main(int argc, char* argv[]) {
  FixStdioStreams();

#if BUILDFLAG(ENABLE_RUN_AS_NODE)
  if (IsEnvSet(electron::kRunAsNode)) {
    return AtomInitializeICUandStartNode(argc, argv);
  }
#endif

#if defined(HELPER_EXECUTABLE) && !defined(MAS_BUILD)
  uint32_t exec_path_size = 0;
  int rv = _NSGetExecutablePath(NULL, &exec_path_size);
  if (rv != -1) {
    fprintf(stderr, "_NSGetExecutablePath: get length failed\n");
    abort();
  }

  std::unique_ptr<char[]> exec_path(new char[exec_path_size]);
  rv = _NSGetExecutablePath(exec_path.get(), &exec_path_size);
  if (rv != 0) {
    fprintf(stderr, "_NSGetExecutablePath: get path failed\n");
    abort();
  }
  sandbox::SeatbeltExecServer::CreateFromArgumentsResult seatbelt =
      sandbox::SeatbeltExecServer::CreateFromArguments(exec_path.get(), argc,
                                                       argv);
  if (seatbelt.sandbox_required) {
    if (!seatbelt.server) {
      fprintf(stderr, "Failed to create seatbelt sandbox server.\n");
      abort();
    }
    if (!seatbelt.server->InitializeSandbox()) {
      fprintf(stderr, "Failed to initialize sandbox.\n");
      abort();
    }
  }
#endif  // defined(HELPER_EXECUTABLE) && !defined(MAS_BUILD)

  return AtomMain(argc, argv);
}

#endif  // defined(OS_MACOSX)
