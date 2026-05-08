#include "IXLandHostVocabularyCheck.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/TypeLoc.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/StringRef.h"

namespace clang::tidy::ixland {

using namespace clang::ast_matchers;

namespace {

constexpr llvm::StringLiteral HostVocabularyPattern =
    "(^|::)(host_[A-Za-z0-9_]*|[A-Za-z0-9_]*_host|[A-Za-z0-9_]*_bridge)$";

bool pathHasComponent(llvm::StringRef Path, llvm::StringRef Needle) {
  return Path.contains(Needle);
}

llvm::StringRef kindLabel(const NamedDecl &Decl) {
  if (isa<FunctionDecl>(Decl))
    return "function";
  if (isa<RecordDecl>(Decl))
    return "record";
  if (isa<TypedefNameDecl>(Decl))
    return "typedef";
  if (isa<EnumConstantDecl>(Decl))
    return "enum constant";
  if (isa<FieldDecl>(Decl))
    return "field";
  if (isa<VarDecl>(Decl))
    return "variable";
  return "declaration";
}

bool shouldSkipLocation(SourceLocation Loc, const SourceManager &SM) {
  return Loc.isInvalid() || SM.isInSystemHeader(Loc);
}

} // namespace

IXLandHostVocabularyCheck::IXLandHostVocabularyCheck(
    llvm::StringRef Name, ClangTidyContext *Context)
    : ClangTidyCheck(Name, Context) {}

bool IXLandHostVocabularyCheck::isLinuxOwnerPath(llvm::StringRef Path) const {
  return pathHasComponent(Path, "IXLandKernel/fs/") ||
         pathHasComponent(Path, "IXLandKernel/kernel/") ||
         pathHasComponent(Path, "IXLandKernel/runtime/") ||
         pathHasComponent(Path, "IXLandKernel/include/");
}

void IXLandHostVocabularyCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(
      namedDecl(unless(isImplicit()), matchesName(HostVocabularyPattern))
          .bind("decl"),
      this);

  Finder->addMatcher(
      callExpr(callee(functionDecl(matchesName(HostVocabularyPattern))
                          .bind("callee")))
          .bind("call"),
      this);

  Finder->addMatcher(
      declRefExpr(to(namedDecl(matchesName(HostVocabularyPattern)).bind("refdecl")))
          .bind("declref"),
      this);

  Finder->addMatcher(
      memberExpr(member(namedDecl(matchesName(HostVocabularyPattern))
                            .bind("memberdecl")))
          .bind("memberexpr"),
      this);

  Finder->addMatcher(
      typeLoc(loc(qualType(hasDeclaration(namedDecl(matchesName(HostVocabularyPattern))
                                             .bind("typedecl")))))
          .bind("typeloc"),
      this);
}

void IXLandHostVocabularyCheck::check(const MatchFinder::MatchResult &Result) {
  if (!Result.SourceManager)
    return;
  const SourceManager &SM = *Result.SourceManager;

  if (const auto *Decl = Result.Nodes.getNodeAs<NamedDecl>("decl")) {
    SourceLocation Loc = Decl->getLocation();
    if (shouldSkipLocation(Loc, SM))
      return;
    auto Entry = SM.getFileEntryRefForID(SM.getFileID(Loc));
    if (!Entry || !isLinuxOwnerPath(Entry->getName()))
      return;
    diag(Loc,
         "host-branded %0 '%1' is forbidden in Linux-owner code; use a "
         "kernel-owned subsystem name instead")
        << kindLabel(*Decl) << Decl->getName();
    return;
  }

  if (const auto *Call = Result.Nodes.getNodeAs<CallExpr>("call")) {
    const auto *Callee = Result.Nodes.getNodeAs<FunctionDecl>("callee");
    if (!Callee)
      return;
    SourceLocation Loc = Call->getExprLoc();
    if (shouldSkipLocation(Loc, SM))
      return;
    auto Entry = SM.getFileEntryRefForID(SM.getFileID(Loc));
    if (!Entry || !isLinuxOwnerPath(Entry->getName()))
      return;
    diag(Loc,
         "call to host-branded function '%0' is forbidden in Linux-owner code")
        << Callee->getName();
    return;
  }

  if (const auto *Ref = Result.Nodes.getNodeAs<DeclRefExpr>("declref")) {
    const auto *Decl = Result.Nodes.getNodeAs<NamedDecl>("refdecl");
    if (!Decl)
      return;
    SourceLocation Loc = Ref->getLocation();
    if (shouldSkipLocation(Loc, SM))
      return;
    auto Entry = SM.getFileEntryRefForID(SM.getFileID(Loc));
    if (!Entry || !isLinuxOwnerPath(Entry->getName()))
      return;
    diag(Loc,
         "reference to host-branded %0 '%1' is forbidden in Linux-owner code")
        << kindLabel(*Decl) << Decl->getName();
    return;
  }

  if (const auto *Member = Result.Nodes.getNodeAs<MemberExpr>("memberexpr")) {
    const auto *Decl = Result.Nodes.getNodeAs<NamedDecl>("memberdecl");
    if (!Decl)
      return;
    SourceLocation Loc = Member->getMemberLoc();
    if (shouldSkipLocation(Loc, SM))
      return;
    auto Entry = SM.getFileEntryRefForID(SM.getFileID(Loc));
    if (!Entry || !isLinuxOwnerPath(Entry->getName()))
      return;
    diag(Loc,
         "member access to host-branded %0 '%1' is forbidden in Linux-owner code")
        << kindLabel(*Decl) << Decl->getName();
    return;
  }

  if (const auto *TypeLocNode = Result.Nodes.getNodeAs<TypeLoc>("typeloc")) {
    const auto *Decl = Result.Nodes.getNodeAs<NamedDecl>("typedecl");
    if (!Decl)
      return;
    SourceLocation Loc = TypeLocNode->getBeginLoc();
    if (shouldSkipLocation(Loc, SM))
      return;
    auto Entry = SM.getFileEntryRefForID(SM.getFileID(Loc));
    if (!Entry || !isLinuxOwnerPath(Entry->getName()))
      return;
    diag(Loc,
         "use of host-branded %0 '%1' is forbidden in Linux-owner code")
        << kindLabel(*Decl) << Decl->getName();
  }
}

} // namespace clang::tidy::ixland
