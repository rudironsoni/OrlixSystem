#include "IXLandIncludeBoundaryCheck.h"

#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"

#include <string>
#include <vector>

namespace clang::tidy::ixland {

namespace {

bool pathHasComponent(llvm::StringRef Path, llvm::StringRef Needle) {
  return Path.contains(Needle);
}

class IncludeBoundaryPPCallbacks : public PPCallbacks {
public:
  IncludeBoundaryPPCallbacks(IXLandIncludeBoundaryCheck &Check,
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
        "pthread.h",            "sys/sysctl.h", "mach/",
        "objc/",                "dispatch/",    "os/log.h",
        "TargetConditionals.h", "Foundation/",  "UIKit/",
        "CoreFoundation/"};

    for (const auto &Header : ForbiddenHeaders) {
      if (FileName == Header || FileName.starts_with(Header)) {
        Check.diag(HashLoc,
                   "forbidden host header is included from Linux-owner code");
      }
    }

    if (FileName.starts_with("IXLandHostAdapter/") ||
        IncludeText.find("IXLandHostAdapter/") != std::string::npos) {
      Check.diag(HashLoc,
                 "Linux-owner code must not include IXLandHostAdapter headers");
    }

    if (FileName.starts_with("IXLandMLibC/") ||
        FileName.starts_with("ixlandmlibc/") ||
        IncludeText.find("IXLandMLibC/") != std::string::npos) {
      Check.diag(HashLoc,
                 "Linux-owner code must not include IXLandMLibC headers");
    }

    if (FileName.contains("internal/ios")) {
      if (Check.isKernelPublicHeaderPath(MainFilePath)) {
        Check.diag(HashLoc,
                   "public headers in IXLandKernel/include must not depend on internal/ios");
      } else {
        Check.diag(HashLoc,
                   "Linux-owner code must not include internal/ios mediation headers");
      }
    }
  }

private:
  IXLandIncludeBoundaryCheck &Check;
  std::string MainFilePath;
};

} // namespace

IXLandIncludeBoundaryCheck::IXLandIncludeBoundaryCheck(llvm::StringRef Name,
                                                       ClangTidyContext *Context)
    : ClangTidyCheck(Name, Context) {}

bool IXLandIncludeBoundaryCheck::isLinuxOwnerPath(llvm::StringRef Path) const {
  return pathHasComponent(Path, "IXLandKernel/fs/") ||
         pathHasComponent(Path, "IXLandKernel/kernel/") ||
         pathHasComponent(Path, "IXLandKernel/runtime/") ||
         pathHasComponent(Path, "IXLandKernel/include/");
}

bool IXLandIncludeBoundaryCheck::isKernelPublicHeaderPath(
    llvm::StringRef Path) const {
  return pathHasComponent(Path, "IXLandKernel/include/");
}

void IXLandIncludeBoundaryCheck::registerPPCallbacks(const SourceManager &SM,
                                                     Preprocessor *PP,
                                                     Preprocessor *) {
  StringRef Path = getCurrentMainFile();
  if (!isLinuxOwnerPath(Path))
    return;
  PP->addPPCallbacks(
      std::make_unique<IncludeBoundaryPPCallbacks>(*this, Path));
}

} // namespace clang::tidy::ixland
