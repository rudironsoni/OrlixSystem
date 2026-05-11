#include "OrlixAbiSurfaceCheck.h"

#include "clang/AST/Decl.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/Linkage.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"

namespace clang::tidy::orlix {

using namespace clang::ast_matchers;

namespace {

constexpr llvm::StringLiteral BrandedPublicPattern =
    "(^|::)(orlix_|ios_|darwin_)[A-Za-z0-9_]*$";

bool pathHasComponent(llvm::StringRef Path, llvm::StringRef Needle) {
  return Path.contains(Needle);
}

bool shouldSkipLocation(SourceLocation Loc, const SourceManager &SM) {
  return Loc.isInvalid() || SM.isInSystemHeader(Loc);
}

class AbiSurfacePPCallbacks : public PPCallbacks {
public:
  AbiSurfacePPCallbacks(OrlixAbiSurfaceCheck &Check,
                        const SourceManager &SM)
      : Check(Check), SM(SM) {}

  void MacroDefined(const Token &MacroNameTok,
                    const MacroDirective *MD) override {
    IdentifierInfo *II = MacroNameTok.getIdentifierInfo();
    if (!II)
      return;

    llvm::StringRef Name = II->getName();
    if (!(Name.starts_with("FUTEX_") || Name.starts_with("AT_") ||
          Name.starts_with("SA_") || Name.starts_with("SIG") ||
          Name.starts_with("O_") || Name.starts_with("F_") ||
          Name.starts_with("RENAME_"))) {
      return;
    }

    if (Name == "SIGSTKSZ" || Name == "SIG_ATOMIC_MIN" ||
        Name == "SIG_ATOMIC_MAX") {
      return;
    }

    SourceLocation Loc = MacroNameTok.getLocation();
    if (Loc.isInvalid())
      return;
    if (!SM.isWrittenInMainFile(Loc) || SM.isInSystemHeader(Loc))
      return;

    if (Name.ends_with("_H") || Name.starts_with("SIGNAL_"))
      return;

    Check.diag(Loc,
               "hand-defined Linux ABI constant '%0' is forbidden in "
               "Linux-owner code")
        << Name;
  }

private:
  OrlixAbiSurfaceCheck &Check;
  const SourceManager &SM;
};

} // namespace

OrlixAbiSurfaceCheck::OrlixAbiSurfaceCheck(llvm::StringRef Name,
                                             ClangTidyContext *Context)
    : ClangTidyCheck(Name, Context) {}

bool OrlixAbiSurfaceCheck::isLinuxOwnerPath(llvm::StringRef Path) const {
  return pathHasComponent(Path, "OrlixKernel/fs/") ||
         pathHasComponent(Path, "OrlixKernel/kernel/") ||
         pathHasComponent(Path, "OrlixKernel/runtime/") ||
         pathHasComponent(Path, "OrlixKernel/include/") ||
         pathHasComponent(Path, "OrlixKernel/internal/");
}

void OrlixAbiSurfaceCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(
      functionDecl(unless(isImplicit()), matchesName(BrandedPublicPattern))
          .bind("func"),
      this);
}

void OrlixAbiSurfaceCheck::registerPPCallbacks(const SourceManager &SM,
                                                Preprocessor *PP,
                                                Preprocessor *) {
  llvm::StringRef Path = getCurrentMainFile();
  if (!isLinuxOwnerPath(Path))
    return;
  PP->addPPCallbacks(std::make_unique<AbiSurfacePPCallbacks>(*this, SM));
}

void OrlixAbiSurfaceCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *Func = Result.Nodes.getNodeAs<FunctionDecl>("func");
  if (!Func || !Result.SourceManager)
    return;

  const SourceManager &SM = *Result.SourceManager;
  SourceLocation Loc = Func->getLocation();
  if (shouldSkipLocation(Loc, SM))
    return;

  auto Entry = SM.getFileEntryRefForID(SM.getFileID(Loc));
  if (!Entry || !isLinuxOwnerPath(Entry->getName()))
    return;

  if (!Func->isExternallyVisible())
    return;

  if (Func->getVisibility() != DefaultVisibility)
    return;

  diag(Loc, "branded public ABI/UAPI symbol '%0' is forbidden")
      << Func->getName();
}

} // namespace clang::tidy::orlix
