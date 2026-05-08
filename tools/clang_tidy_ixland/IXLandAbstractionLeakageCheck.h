#ifndef IXLAND_TIDY_ABSTRACTION_LEAKAGE_CHECK_H
#define IXLAND_TIDY_ABSTRACTION_LEAKAGE_CHECK_H

#include "clang-tidy/ClangTidyCheck.h"

namespace clang::tidy::ixland {

class IXLandAbstractionLeakageCheck : public ClangTidyCheck {
public:
  IXLandAbstractionLeakageCheck(llvm::StringRef Name,
                                ClangTidyContext *Context);

  void registerPPCallbacks(const SourceManager &SM, Preprocessor *PP,
                           Preprocessor *ModuleExpanderPP) override;
  void onEndOfTranslationUnit() override;

private:
  bool isLinuxOwnerPath(llvm::StringRef Path) const;
  void scanMainFile();

  const SourceManager *CurrentSM = nullptr;
};

} // namespace clang::tidy::ixland

#endif
