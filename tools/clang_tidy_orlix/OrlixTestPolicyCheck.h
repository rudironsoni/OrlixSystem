#ifndef ORLIX_TIDY_TEST_POLICY_CHECK_H
#define ORLIX_TIDY_TEST_POLICY_CHECK_H

#include "clang-tidy/ClangTidyCheck.h"

namespace clang::tidy::orlix {

class OrlixTestPolicyCheck : public ClangTidyCheck {
public:
  OrlixTestPolicyCheck(llvm::StringRef Name, ClangTidyContext *Context);

  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void registerPPCallbacks(const SourceManager &SM, Preprocessor *PP,
                           Preprocessor *ModuleExpanderPP) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;
  void onEndOfTranslationUnit() override;

private:
  bool isHostAdapterPath(llvm::StringRef Path) const;
  bool isKernelTestPath(llvm::StringRef Path) const;
  bool isHostTestPath(llvm::StringRef Path) const;
  bool isAnyPolicyPath(llvm::StringRef Path) const;
  void scanMainFile();
  const SourceManager *CurrentSM = nullptr;
};

} // namespace clang::tidy::orlix

#endif
