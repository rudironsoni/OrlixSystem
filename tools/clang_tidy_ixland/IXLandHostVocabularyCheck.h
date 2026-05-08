#ifndef IXLAND_TIDY_HOST_VOCABULARY_CHECK_H
#define IXLAND_TIDY_HOST_VOCABULARY_CHECK_H

#include "clang-tidy/ClangTidyCheck.h"

namespace clang::tidy::ixland {

class IXLandHostVocabularyCheck : public ClangTidyCheck {
public:
  IXLandHostVocabularyCheck(llvm::StringRef Name, ClangTidyContext *Context);

  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;

private:
  bool isLinuxOwnerPath(llvm::StringRef Path) const;
};

} // namespace clang::tidy::ixland

#endif
