#include "OrlixIncludeBoundaryCheck.h"

#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"

#include <filesystem>
#include <regex>
#include <string>
#include <unordered_map>
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

bool isRuntimeUserspaceAbiSurface(llvm::StringRef MainFilePath) {
  return MainFilePath.contains("/OrlixKernel/runtime/");
}

bool isHeaderPath(llvm::StringRef MainFilePath) {
  return MainFilePath.ends_with(".h") || MainFilePath.ends_with(".hpp") ||
         MainFilePath.ends_with(".hh");
}

std::string repoRootFromMainFile(llvm::StringRef MainFilePath) {
  size_t KernelPos = MainFilePath.find("/OrlixKernel/");
  if (KernelPos != llvm::StringRef::npos) {
    return MainFilePath.substr(0, KernelPos).str();
  }

  size_t HostPos = MainFilePath.find("/OrlixHostAdapter/");
  if (HostPos != llvm::StringRef::npos) {
    return MainFilePath.substr(0, HostPos).str();
  }

  return {};
}

std::string siblingKernelHeaderForUapi(llvm::StringRef FileName) {
  if (!FileName.starts_with("uapi/")) {
    return {};
  }
  return FileName.drop_front(5).str();
}

bool vendoredKernelHeaderExists(llvm::StringRef MainFilePath,
                                llvm::StringRef CandidateHeader) {
  static std::unordered_map<std::string, bool> Cache;

  if (CandidateHeader.empty()) {
    return false;
  }

  std::string RepoRoot = repoRootFromMainFile(MainFilePath);
  if (RepoRoot.empty()) {
    return false;
  }

  std::string CacheKey = RepoRoot + "|" + CandidateHeader.str();
  auto It = Cache.find(CacheKey);
  if (It != Cache.end()) {
    return It->second;
  }

  std::filesystem::path VendorRoot =
      std::filesystem::path(RepoRoot) / "OrlixKernel" / "vendor" / "linux";
  bool Found = false;

  std::error_code EC;
  if (std::filesystem::exists(VendorRoot, EC)) {
    for (const auto &VersionDir :
         std::filesystem::directory_iterator(VendorRoot, EC)) {
      if (EC || !VersionDir.is_directory()) {
        continue;
      }
      for (const auto &ArchDir :
           std::filesystem::directory_iterator(VersionDir.path(), EC)) {
        if (EC || !ArchDir.is_directory()) {
          continue;
        }
        std::filesystem::path CandidatePath =
            ArchDir.path() / "kheaders" / "include" / CandidateHeader.str();
        if (std::filesystem::exists(CandidatePath, EC)) {
          Found = true;
          break;
        }
      }
      if (Found) {
        break;
      }
    }
  }

  Cache.emplace(CacheKey, Found);
  return Found;
}

class IncludeBoundaryPPCallbacks : public PPCallbacks {
public:
  IncludeBoundaryPPCallbacks(OrlixIncludeBoundaryCheck &Check,
                             const SourceManager &SM,
                             llvm::StringRef MainFilePath, bool LinuxOwner,
                             bool HostKernel)
      : Check(Check), MainFilePath(MainFilePath.str()), LinuxOwner(LinuxOwner),
        HostKernel(HostKernel), SM(SM) {}

  void InclusionDirective(SourceLocation HashLoc, const Token &, StringRef FileName,
                          bool IsAngled, CharSourceRange, OptionalFileEntryRef,
                          StringRef, StringRef, const Module *, bool,
                          SrcMgr::CharacteristicKind) override {
    std::string IncludeText =
        std::string(IsAngled ? "<" : "\"") + FileName.str() +
        std::string(IsAngled ? ">" : "\"");
    bool MainFileInclude = SM.isWrittenInMainFile(HashLoc);

    if (LinuxOwner) {
      if (!MainFileInclude) {
        return;
      }

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
                   "host toolchain std*.h headers are forbidden in Linux-owner code; use vendored Linux truth from OrlixKernel/vendor/linux/<version>/<arch>/kheaders/include instead");
      }

      if (FileName.starts_with("__vendor/") || FileName.starts_with("support/uapi/")) {
        Check.diag(HashLoc,
                   "temporary vendored alias include surfaces are forbidden; include the real upstream Linux path instead");
      }

      if (Check.isKernelPublicHeaderPath(MainFilePath) &&
          isHeaderPath(MainFilePath) &&
          !isRuntimeUserspaceAbiSurface(MainFilePath) &&
          FileName.starts_with("uapi/") &&
          vendoredKernelHeaderExists(MainFilePath,
                                     siblingKernelHeaderForUapi(FileName))) {
        Check.diag(HashLoc,
                   "direct UAPI includes are forbidden in public Linux-owner headers; use full upstream Linux kernel headers there and keep UAPI consumption in implementation files or kernel-private subsystem headers that translate Linux userspace ABI");
      }

      if (FileName.starts_with("third_party/linux/") ||
          FileName.starts_with("OrlixKernel/vendor/linux/")) {
        Check.diag(HashLoc,
                   "provenance-heavy vendor paths are forbidden in source includes; use upstream Linux include names only");
      }

      if (FileName.starts_with("OrlixHostAdapter/") ||
          IncludeText.find("OrlixHostAdapter/") != std::string::npos) {
        Check.diag(HashLoc,
                   "Linux-owner code must not include OrlixHostAdapter headers");
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

    if (HostKernel && MainFileInclude) {
      bool DarwinHeader = FileName == "pthread.h" || FileName == "signal.h" ||
                          FileName == "time.h" || FileName == "unistd.h" ||
                          FileName == "errno.h" || FileName == "string.h" ||
                          FileName == "stdlib.h" || FileName == "stdint.h" ||
                          FileName == "stddef.h" || FileName.starts_with("sys/") ||
                          FileName.starts_with("Foundation/") ||
                          FileName.starts_with("dispatch/") ||
                          FileName.starts_with("os/");
      bool DeepLinuxHeader = FileName.starts_with("linux/") ||
                             FileName.starts_with("asm/") ||
                             FileName.starts_with("asm-generic/");
      bool BroadKernelHeader =
          FileName == "kernel/signal.h" || FileName == "kernel/task.h" ||
          FileName == "kernel/wait.h" || FileName == "kernel/futex.h" ||
          FileName == "fs/fdtable.h" || FileName == "fs/vfs.h" ||
          FileName == "fs/path.h" || FileName == "fs/pty.h";

      if (BroadKernelHeader) {
        Check.diag(HashLoc,
                   "OrlixHostAdapter kernel implementation files must not include broad kernel owner headers; narrow the seam instead");
      }
      if (DarwinHeader) {
        SawDarwinHeader = true;
      }
      if (DeepLinuxHeader) {
        SawDeepLinuxHeader = true;
      }
      if (SawDarwinHeader && SawDeepLinuxHeader) {
        Check.diag(HashLoc,
                   "OrlixHostAdapter kernel implementation files must not mix Darwin SDK headers with deep Linux kheaders in one translation unit; move Linux struct semantics back into OrlixKernel and keep the host seam scalar or opaque");
      }
    }

  }

private:
  OrlixIncludeBoundaryCheck &Check;
  std::string MainFilePath;
  bool LinuxOwner;
  bool HostKernel;
  bool SawDarwinHeader = false;
  bool SawDeepLinuxHeader = false;
  const SourceManager &SM;
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

bool OrlixIncludeBoundaryCheck::isHostKernelPath(llvm::StringRef Path) const {
  return pathHasComponent(Path, "OrlixHostAdapter/kernel/sync.c") ||
         pathHasComponent(Path, "OrlixHostAdapter/kernel/clock.c");
}

bool OrlixIncludeBoundaryCheck::isKernelPublicHeaderPath(
    llvm::StringRef Path) const {
  return pathHasComponent(Path, "OrlixKernel/include/");
}

void OrlixIncludeBoundaryCheck::registerPPCallbacks(const SourceManager &SM,
                                                     Preprocessor *PP,
                                                     Preprocessor *) {
  StringRef Path = getCurrentMainFile();
  bool LinuxOwner = isLinuxOwnerPath(Path);
  bool HostKernel = isHostKernelPath(Path);
  if (!LinuxOwner && !HostKernel)
    return;
  PP->addPPCallbacks(
      std::make_unique<IncludeBoundaryPPCallbacks>(*this, SM, Path, LinuxOwner,
                                                   HostKernel));
}

} // namespace clang::tidy::orlix
