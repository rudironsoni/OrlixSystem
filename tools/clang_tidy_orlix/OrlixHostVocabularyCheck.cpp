#include "OrlixHostVocabularyCheck.h"

#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/TypeLoc.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/StringRef.h"

namespace clang::tidy::orlix {

using namespace clang::ast_matchers;

namespace {

bool pathHasComponent(llvm::StringRef Path, llvm::StringRef Needle) {
  return Path.contains(Needle);
}

bool isVendoredLinuxDecl(const NamedDecl &Decl, const SourceManager &SM) {
  SourceLocation Loc = Decl.getLocation();
  if (Loc.isInvalid())
    return false;
  auto Entry = SM.getFileEntryRefForID(SM.getFileID(Loc));
  return Entry && pathHasComponent(Entry->getName(), "OrlixKernel/vendor/linux/");
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

bool matchesLinuxConceptRename(llvm::StringRef Name) {
  static constexpr llvm::StringLiteral Prefixes[] = {
      "orlix_", "ix_",      "kernel_", "linux_", "private_",
      "internal_", "bridge_", "adapter_", "shim_", "compat_"};
  static constexpr llvm::StringLiteral Suffixes[] = {
      "_compat", "_bridge", "_adapter", "_shim",
      "_private", "_internal", "_owner"};
  static constexpr llvm::StringLiteral Concepts[] = {
      "fdset",    "fd_set",   "fdbits",   "timeval",  "timespec",
      "itimerval","itimerspec","socket",  "sockaddr", "socklen",
      "pollfd",   "poll",     "select",   "termios",  "winsize",
      "sigset",   "siginfo",  "sigaction","timezone", "stat",
      "statfs",   "rusage",   "tms",      "iovec",    "msghdr",
      "mmsghdr",  "dirent",   "ucred"};

  auto HasConceptPrefix = [](llvm::StringRef Value) {
    for (llvm::StringLiteral Concept : Concepts) {
      if (Value.starts_with(Concept))
        return true;
    }
    return false;
  };

  auto HasConceptSuffix = [](llvm::StringRef Value) {
    for (llvm::StringLiteral Concept : Concepts) {
      if (Value.ends_with(Concept))
        return true;
    }
    return false;
  };

  for (llvm::StringLiteral Prefix : Prefixes) {
    if (Name.starts_with(Prefix) && HasConceptPrefix(Name.drop_front(Prefix.size())))
      return true;
  }
  for (llvm::StringLiteral Suffix : Suffixes) {
    if (Name.ends_with(Suffix) && HasConceptSuffix(Name.drop_back(Suffix.size())))
      return true;
  }
  return false;
}

bool matchesHostVocabulary(llvm::StringRef Name) {
  return Name.starts_with("host_") || Name.ends_with("_host") ||
         Name.ends_with("_bridge");
}

} // namespace

OrlixHostVocabularyCheck::OrlixHostVocabularyCheck(
    llvm::StringRef Name, ClangTidyContext *Context)
    : ClangTidyCheck(Name, Context) {}

bool OrlixHostVocabularyCheck::isLinuxOwnerPath(llvm::StringRef Path) const {
  return pathHasComponent(Path, "OrlixKernel/fs/") ||
         pathHasComponent(Path, "OrlixKernel/kernel/") ||
         pathHasComponent(Path, "OrlixKernel/runtime/") ||
         pathHasComponent(Path, "OrlixKernel/include/") ||
         pathHasComponent(Path, "OrlixKernel/internal/");
}

void OrlixHostVocabularyCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(
      namedDecl(unless(isImplicit())).bind("decl"),
      this);

  Finder->addMatcher(
      callExpr(callee(functionDecl().bind("callee")))
          .bind("call"),
      this);

  Finder->addMatcher(
      declRefExpr(to(namedDecl().bind("refdecl")))
          .bind("declref"),
      this);

  Finder->addMatcher(
      memberExpr(member(namedDecl().bind("memberdecl")))
          .bind("memberexpr"),
      this);

  Finder->addMatcher(
      typeLoc(loc(qualType(hasDeclaration(namedDecl().bind("typedecl")))))
          .bind("typeloc"),
      this);
}

void OrlixHostVocabularyCheck::check(const MatchFinder::MatchResult &Result) {
  if (!Result.SourceManager)
    return;
  const SourceManager &SM = *Result.SourceManager;

  if (const auto *Decl = Result.Nodes.getNodeAs<NamedDecl>("decl")) {
    if (!matchesHostVocabulary(Decl->getName()) &&
        !matchesLinuxConceptRename(Decl->getName()))
      return;
    SourceLocation Loc = Decl->getLocation();
    if (shouldSkipLocation(Loc, SM))
      return;
    auto Entry = SM.getFileEntryRefForID(SM.getFileID(Loc));
    if (!Entry || !isLinuxOwnerPath(Entry->getName()))
      return;
    if (matchesHostVocabulary(Decl->getName())) {
      diag(Loc,
           "host-branded %0 '%1' is forbidden in Linux-owner code; use a "
           "kernel-owned subsystem name instead")
          << kindLabel(*Decl) << Decl->getName();
    } else if (matchesLinuxConceptRename(Decl->getName())) {
      diag(Loc,
           "repo-local renamed Linux concept %0 '%1' is forbidden in Linux-owner code; use the Linux name instead")
          << kindLabel(*Decl) << Decl->getName();
    }
    return;
  }

  if (const auto *Call = Result.Nodes.getNodeAs<CallExpr>("call")) {
    const auto *Callee = Result.Nodes.getNodeAs<FunctionDecl>("callee");
    if (!Callee ||
        (!matchesHostVocabulary(Callee->getName()) &&
         !matchesLinuxConceptRename(Callee->getName())))
      return;
    if (isVendoredLinuxDecl(*Callee, SM))
      return;
    SourceLocation Loc = Call->getExprLoc();
    if (shouldSkipLocation(Loc, SM))
      return;
    auto Entry = SM.getFileEntryRefForID(SM.getFileID(Loc));
    if (!Entry || !isLinuxOwnerPath(Entry->getName()))
      return;
    if (matchesHostVocabulary(Callee->getName())) {
      diag(Loc,
           "call to host-branded function '%0' is forbidden in Linux-owner code")
          << Callee->getName();
    } else if (matchesLinuxConceptRename(Callee->getName())) {
      diag(Loc,
           "call to repo-local renamed Linux concept function '%0' is forbidden in Linux-owner code")
          << Callee->getName();
    }
    return;
  }

  if (const auto *Ref = Result.Nodes.getNodeAs<DeclRefExpr>("declref")) {
    const auto *Decl = Result.Nodes.getNodeAs<NamedDecl>("refdecl");
    if (!Decl ||
        (!matchesHostVocabulary(Decl->getName()) &&
         !matchesLinuxConceptRename(Decl->getName())))
      return;
    if (isVendoredLinuxDecl(*Decl, SM))
      return;
    SourceLocation Loc = Ref->getLocation();
    if (shouldSkipLocation(Loc, SM))
      return;
    auto Entry = SM.getFileEntryRefForID(SM.getFileID(Loc));
    if (!Entry || !isLinuxOwnerPath(Entry->getName()))
      return;
    if (matchesHostVocabulary(Decl->getName())) {
      diag(Loc,
           "reference to host-branded %0 '%1' is forbidden in Linux-owner code")
          << kindLabel(*Decl) << Decl->getName();
    } else if (matchesLinuxConceptRename(Decl->getName())) {
      diag(Loc,
           "reference to repo-local renamed Linux concept %0 '%1' is forbidden in Linux-owner code")
          << kindLabel(*Decl) << Decl->getName();
    }
    return;
  }

  if (const auto *Member = Result.Nodes.getNodeAs<MemberExpr>("memberexpr")) {
    const auto *Decl = Result.Nodes.getNodeAs<NamedDecl>("memberdecl");
    if (!Decl ||
        (!matchesHostVocabulary(Decl->getName()) &&
         !matchesLinuxConceptRename(Decl->getName())))
      return;
    if (isVendoredLinuxDecl(*Decl, SM))
      return;
    SourceLocation Loc = Member->getMemberLoc();
    if (shouldSkipLocation(Loc, SM))
      return;
    auto Entry = SM.getFileEntryRefForID(SM.getFileID(Loc));
    if (!Entry || !isLinuxOwnerPath(Entry->getName()))
      return;
    if (matchesHostVocabulary(Decl->getName())) {
      diag(Loc,
           "member access to host-branded %0 '%1' is forbidden in Linux-owner code")
          << kindLabel(*Decl) << Decl->getName();
    } else if (matchesLinuxConceptRename(Decl->getName())) {
      diag(Loc,
           "member access to repo-local renamed Linux concept %0 '%1' is forbidden in Linux-owner code")
          << kindLabel(*Decl) << Decl->getName();
    }
    return;
  }

  if (const auto *TypeLocNode = Result.Nodes.getNodeAs<TypeLoc>("typeloc")) {
    const auto *Decl = Result.Nodes.getNodeAs<NamedDecl>("typedecl");
    if (!Decl ||
        (!matchesHostVocabulary(Decl->getName()) &&
         !matchesLinuxConceptRename(Decl->getName())))
      return;
    if (isVendoredLinuxDecl(*Decl, SM))
      return;
    SourceLocation Loc = TypeLocNode->getBeginLoc();
    if (shouldSkipLocation(Loc, SM))
      return;
    auto Entry = SM.getFileEntryRefForID(SM.getFileID(Loc));
    if (!Entry || !isLinuxOwnerPath(Entry->getName()))
      return;
    if (matchesHostVocabulary(Decl->getName())) {
      diag(Loc,
           "use of host-branded %0 '%1' is forbidden in Linux-owner code")
          << kindLabel(*Decl) << Decl->getName();
    } else if (matchesLinuxConceptRename(Decl->getName())) {
      diag(Loc,
           "use of repo-local renamed Linux concept %0 '%1' is forbidden in Linux-owner code")
          << kindLabel(*Decl) << Decl->getName();
    }
  }
}

} // namespace clang::tidy::orlix
