#ifndef IXLAND_TIDY_TEST_POLICY_CHECK_H
#define IXLAND_TIDY_TEST_POLICY_CHECK_H

#include "clang-tidy/ClangTidyCheck.h"

namespace clang::tidy::ixland {

class IXLandTestPolicyCheck : public ClangTidyCheck {
public:
  IXLandTestPolicyCheck(llvm::StringRef Name, ClangTidyContext *Context);

  void registerPPCallbacks(const SourceManager &SM, Preprocessor *PP,
                           Preprocessor *ModuleExpanderPP) override;
  void onEndOfTranslationUnit() override;

private:
  bool isKernelTestPath(llvm::StringRef Path) const;
  bool isHostTestPath(llvm::StringRef Path) const;
  bool isAnyTestPath(llvm::StringRef Path) const;
  void scanMainFile();
  const SourceManager *CurrentSM = nullptr;
};

} // namespace clang::tidy::ixland

#endif
