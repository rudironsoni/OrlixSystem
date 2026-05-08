#ifndef IXLAND_TIDY_INCLUDE_BOUNDARY_CHECK_H
#define IXLAND_TIDY_INCLUDE_BOUNDARY_CHECK_H

#include "clang-tidy/ClangTidyCheck.h"

namespace clang::tidy::ixland {

class IXLandIncludeBoundaryCheck : public ClangTidyCheck {
public:
  IXLandIncludeBoundaryCheck(llvm::StringRef Name, ClangTidyContext *Context);

  void registerPPCallbacks(const SourceManager &SM, Preprocessor *PP,
                           Preprocessor *ModuleExpanderPP) override;
  bool isKernelPublicHeaderPath(llvm::StringRef Path) const;

private:
  bool isLinuxOwnerPath(llvm::StringRef Path) const;
};

} // namespace clang::tidy::ixland

#endif
