#ifndef IXLAND_TIDY_LOGGING_POLICY_CHECK_H
#define IXLAND_TIDY_LOGGING_POLICY_CHECK_H

#include "clang-tidy/ClangTidyCheck.h"

namespace clang::tidy::ixland {

class IXLandLoggingPolicyCheck : public ClangTidyCheck {
public:
  IXLandLoggingPolicyCheck(llvm::StringRef Name, ClangTidyContext *Context);

  void registerMatchers(ast_matchers::MatchFinder *Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult &Result) override;

private:
  bool isLinuxOwnerPath(llvm::StringRef Path) const;
};

} // namespace clang::tidy::ixland

#endif
