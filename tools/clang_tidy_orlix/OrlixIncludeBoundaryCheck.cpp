#include "OrlixIncludeBoundaryCheck.h"

#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"

#include <regex>
#include <string>
#include <vector>

namespace clang::tidy::orlix {

namespace {

bool pathHasComponent(llvm::StringRef Path, llvm::StringRef Needle) {
  return Path.contains(Needle);
}

bool fileNameMatchesLinuxRename(llvm::StringRef FileName) {
  llvm::StringRef BaseName = FileName;
  size_t Slash = FileName.rfind('/');
  if (Slash != llvm::StringRef::npos) {
    BaseName = FileName.drop_front(Slash + 1);
  }
  if (FileName.starts_with("linux/") || FileName.starts_with("asm/") ||
      FileName.starts_with("asm-generic/")) {
    return false;
  }
  if (!BaseName.contains("_")) {
    return false;
  }
  static const std::regex Pattern(
      R"((^|/)(?:(?:orlix|ix|kernel|linux|private|internal|bridge|adapter|shim|compat)_)?(?:fdset|fd_set|timespec|timeval|timezone|itimerspec|itimerval|sockaddr|socklen|socket|pollfd|poll|select|termios|winsize|sigset|siginfo|sigaction|stat|statfs|rusage|tms|iovec|msghdr|mmsghdr|ucred)(?:_[A-Za-z0-9]+)*(?:_compat|_bridge|_adapter|_shim|_private|_internal|_owner)?\.h$)");
  return std::regex_search(FileName.str(), Pattern);
}

bool isForbiddenStdHeader(llvm::StringRef FileName) {
  return FileName.starts_with("std") && FileName.ends_with(".h");
}

class IncludeBoundaryPPCallbacks : public PPCallbacks {
public:
  IncludeBoundaryPPCallbacks(OrlixIncludeBoundaryCheck &Check,
                             llvm::StringRef MainFilePath)
      : Check(Check), MainFilePath(MainFilePath.str()) {}

  void InclusionDirective(SourceLocation HashLoc, const Token &, StringRef FileName,
                          bool IsAngled, CharSourceRange, OptionalFileEntryRef,
                          StringRef, StringRef, const Module *, bool,
                          SrcMgr::CharacteristicKind) override {
    std::string IncludeText =
        std::string(IsAngled ? "<" : "\"") + FileName.str() +
        std::string(IsAngled ? ">" : "\"");

    if (FileName.starts_with("Foundation/") || FileName.starts_with("UIKit/") ||
        FileName.starts_with("CoreFoundation/") ||
        FileName.starts_with("CoreServices/") ||
        FileName.starts_with("CoreGraphics/") ||
        FileName.starts_with("TargetConditionals") ||
        FileName.starts_with("dispatch") || FileName.starts_with("os/")) {
      Check.diag(HashLoc,
                 "host framework imports are forbidden in Linux-owner code");
    }

    static const std::vector<std::string> ForbiddenHeaders = {
        "pthread.h",            "sys/sysctl.h",   "mach/",
        "objc/",                "dispatch/",      "os/log.h",
        "TargetConditionals.h", "Foundation/",    "UIKit/",
        "CoreFoundation/",      "sys/socket.h",   "sys/types.h",
        "sys/uio.h",            "sys/ioctl.h",    "sys/select.h",
        "sys/resource.h",       "sys/statvfs.h",  "sys/wait.h"};
    static const std::vector<std::string> ForbiddenAngledOnlyHeaders = {
        "poll.h", "termios.h", "signal.h"};

    for (const auto &Header : ForbiddenHeaders) {
      if (FileName == Header || FileName.starts_with(Header)) {
        Check.diag(HashLoc,
                   "forbidden host header is included from Linux-owner code");
      }
    }
    if (IsAngled) {
      for (const auto &Header : ForbiddenAngledOnlyHeaders) {
        if (FileName == Header) {
          Check.diag(HashLoc,
                     "forbidden host header is included from Linux-owner code");
        }
      }
    }

    if (isForbiddenStdHeader(FileName)) {
      Check.diag(HashLoc,
                 "host toolchain std*.h headers are forbidden in Linux-owner code; use vendored Linux truth from third_party/linux/6.12/arm64 instead");
    }

    if (FileName.starts_with("OrlixHostAdapter/") ||
        IncludeText.find("OrlixHostAdapter/") != std::string::npos) {
      Check.diag(HashLoc,
                 "Linux-owner code must not include OrlixHostAdapter headers");
    }

    if (FileName.starts_with("OrlixMLibC/") ||
        FileName.starts_with("orlixmlibc/") ||
        IncludeText.find("OrlixMLibC/") != std::string::npos) {
      Check.diag(HashLoc,
                 "Linux-owner code must not include OrlixMLibC headers");
    }

    if (FileName.contains("internal/ios")) {
      if (Check.isKernelPublicHeaderPath(MainFilePath)) {
        Check.diag(HashLoc,
                   "public headers in OrlixKernel/include must not depend on internal/ios");
      } else {
        Check.diag(HashLoc,
                   "Linux-owner code must not include internal/ios mediation headers");
      }
    }

    if (FileName.contains("_compat.h") || FileName.contains("_bridge.h") ||
        FileName.contains("_adapter.h") || FileName.contains("_shim.h") ||
        FileName.contains("_private.h") || FileName.contains("_internal.h") ||
        fileNameMatchesLinuxRename(FileName)) {
      Check.diag(HashLoc,
                 "repo-local renamed Linux-concept headers are forbidden in Linux-owner code; use Linux names instead");
    }
  }

private:
  OrlixIncludeBoundaryCheck &Check;
  std::string MainFilePath;
};

} // namespace

OrlixIncludeBoundaryCheck::OrlixIncludeBoundaryCheck(llvm::StringRef Name,
                                                       ClangTidyContext *Context)
    : ClangTidyCheck(Name, Context) {}

bool OrlixIncludeBoundaryCheck::isLinuxOwnerPath(llvm::StringRef Path) const {
  return pathHasComponent(Path, "OrlixKernel/fs/") ||
         pathHasComponent(Path, "OrlixKernel/kernel/") ||
         pathHasComponent(Path, "OrlixKernel/runtime/") ||
         pathHasComponent(Path, "OrlixKernel/include/") ||
         pathHasComponent(Path, "OrlixKernel/internal/");
}

bool OrlixIncludeBoundaryCheck::isKernelPublicHeaderPath(
    llvm::StringRef Path) const {
  return pathHasComponent(Path, "OrlixKernel/include/");
}

void OrlixIncludeBoundaryCheck::registerPPCallbacks(const SourceManager &SM,
                                                     Preprocessor *PP,
                                                     Preprocessor *) {
  StringRef Path = getCurrentMainFile();
  if (!isLinuxOwnerPath(Path))
    return;
  PP->addPPCallbacks(
      std::make_unique<IncludeBoundaryPPCallbacks>(*this, Path));
}

} // namespace clang::tidy::orlix
