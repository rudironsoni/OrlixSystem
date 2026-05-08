#ifndef IXLAND_TIDY_ABI_SURFACE_CHECK_H
#define IXLAND_TIDY_ABI_SURFACE_CHECK_H

#include "clang-tidy/ClangTidyCheck.h"

namespace clang::tidy::ixland {

class IXLandAbiSurfaceCheck : public ClangTidyCheck {
public:
  IXLandAbiSurfaceCheck(llvm::StringRef Name, ClangTidyContext *Context);

  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void registerPPCallbacks(const SourceManager &SM, Preprocessor *PP,
                           Preprocessor *ModuleExpanderPP) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;

private:
  bool isLinuxOwnerPath(llvm::StringRef Path) const;
};

} // namespace clang::tidy::ixland

#endif
