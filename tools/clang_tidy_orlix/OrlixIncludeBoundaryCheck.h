#ifndef ORLIX_TIDY_INCLUDE_BOUNDARY_CHECK_H
#define ORLIX_TIDY_INCLUDE_BOUNDARY_CHECK_H

#include "clang-tidy/ClangTidyCheck.h"

namespace clang::tidy::orlix {

class OrlixIncludeBoundaryCheck : public ClangTidyCheck {
public:
  OrlixIncludeBoundaryCheck(llvm::StringRef Name, ClangTidyContext *Context);

  void registerPPCallbacks(const SourceManager &SM, Preprocessor *PP,
                           Preprocessor *ModuleExpanderPP) override;
  bool isKernelPublicHeaderPath(llvm::StringRef Path) const;

private:
  bool isLinuxOwnerPath(llvm::StringRef Path) const;
  bool isHostKernelPath(llvm::StringRef Path) const;
};

} // namespace clang::tidy::orlix

#endif
