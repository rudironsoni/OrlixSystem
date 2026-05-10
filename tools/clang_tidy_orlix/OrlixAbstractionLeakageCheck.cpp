#include "OrlixAbstractionLeakageCheck.h"

#include "clang/Lex/Preprocessor.h"

#include <regex>
#include <string>
#include <vector>

namespace clang::tidy::orlix {

namespace {

struct RegexRule {
  const char *Pattern;
  const char *Message;
};

bool pathHasComponent(llvm::StringRef Path, llvm::StringRef Needle) {
  return Path.contains(Needle);
}

bool pathMatchesCompatNaming(llvm::StringRef Path) {
  static const std::regex Pattern(
      R"((^|/)(?:(?:orlix|ix|kernel|linux|private|internal|bridge|adapter|shim|compat)_)?(?:fdset|fd_set|timespec|timeval|timezone|itimerspec|itimerval|sockaddr|socklen|socket|pollfd|poll|select|termios|winsize|sigset|siginfo|sigaction|stat|statfs|rusage|tms|iovec|msghdr|mmsghdr|ucred)(?:_[A-Za-z0-9]+)*(?:_compat|_bridge|_adapter|_shim|_private|_internal|_owner)?\.h$)");
  return std::regex_search(Path.str(), Pattern);
}

bool pathMatchesPrivateSplitHeader(llvm::StringRef Path) {
  static const std::regex Pattern(
      R"((^|/)[A-Za-z0-9]+_(?:handoff|contract|seam|facade|subset|slice|split)(?:_[A-Za-z0-9]+)?\.h$)");
  return std::regex_search(Path.str(), Pattern);
}

SourceLocation translateLocation(const SourceManager &SM, FileID FID,
                                 unsigned Line, unsigned Column) {
  return SM.translateLineCol(FID, Line, Column);
}

void scanLines(ClangTidyCheck &Check, const SourceManager &SM, StringRef Buffer,
               const std::vector<RegexRule> &Rules) {
  FileID FID = SM.getMainFileID();
  size_t Start = 0;
  unsigned LineNo = 1;
  while (Start <= Buffer.size()) {
    size_t End = Buffer.find('\n', Start);
    if (End == StringRef::npos)
      End = Buffer.size();
    std::string Line(Buffer.slice(Start, End).str());
    for (const auto &Rule : Rules) {
      std::smatch Match;
      if (std::regex_search(Line, Match, std::regex(Rule.Pattern))) {
        unsigned Column = static_cast<unsigned>(Match.position() + 1);
        Check.diag(translateLocation(SM, FID, LineNo, Column), Rule.Message);
      }
    }
    if (End == Buffer.size())
      break;
    Start = End + 1;
    ++LineNo;
  }
}

const std::vector<RegexRule> AbstractionLeakageRules = {
    {R"(\b(kmutex|kcond|kthread|konce|ksig|kplatform|kbridge|ix_mutex|ix_cond|ix_thread|ix_platform|ix_bridge|platform_mutex|platform_thread|bridge_mutex|bridge_thread)_[a-z0-9_]*\b)",
     "generic abstraction leakage found in Linux-owner code"},
    {R"(\b(orlix|ix|kernel|linux|private|internal|bridge|adapter|shim|compat)_(fd(set|bits?)|fd_set|time(val|spec)|timezone|itimer(val|spec)|socket|sock(addr|len)?|poll(fd)?|select|termios|winsize|sig(set|info|action)?|stat(fs)?|rusage|tms|iovec|msghdr|mmsghdr|dirent|ucred)[a-z0-9_]*\b)",
     "repo-local prefixed spellings for Linux-shaped concepts are forbidden in Linux-owner code; use Linux names instead"},
    {R"(\b(fd(set|bits?)|fd_set|time(val|spec)|timezone|itimer(val|spec)|socket|sock(addr|len)?|poll(fd)?|select|termios|winsize|sig(set|info|action)?|stat(fs)?|rusage|tms|iovec|msghdr|mmsghdr|dirent|ucred)_(compat|bridge|adapter|shim|private|internal|owner)\b)",
     "repo-local suffixed spellings for Linux-shaped concepts are forbidden in Linux-owner code; use Linux names instead"},
    {R"(\b[a-z0-9_]+_(compat|bridge|adapter|shim|private|internal|owner)(?:_[a-z0-9_]+)?\b)",
     "repo-local compat naming for Linux-shaped concepts is forbidden in Linux-owner code; use Linux names instead"},
    {R"(^\s*#\s*include\s*\"[^\"]*_(compat|bridge|adapter|shim|private|internal|owner)\.h\")",
     "repo-local renamed Linux-concept headers are forbidden in Linux-owner code; use Linux names instead"},
    {R"(^\s*#\s*include\s*\"[^\"]*_(handoff|contract|seam|facade|subset|slice|split)(?:_[A-Za-z0-9]+)?\.h\")",
     "repo-local private split headers are forbidden in Linux-owner code; keep the direct vendored Linux include path and fix the build or lint environment instead"},
};

} // namespace

OrlixAbstractionLeakageCheck::OrlixAbstractionLeakageCheck(
    llvm::StringRef Name, ClangTidyContext *Context)
    : ClangTidyCheck(Name, Context) {}

bool OrlixAbstractionLeakageCheck::isLinuxOwnerPath(llvm::StringRef Path) const {
  return pathHasComponent(Path, "OrlixKernel/fs/") ||
         pathHasComponent(Path, "OrlixKernel/kernel/") ||
         pathHasComponent(Path, "OrlixKernel/runtime/") ||
         pathHasComponent(Path, "OrlixKernel/include/") ||
         pathHasComponent(Path, "OrlixKernel/internal/");
}

void OrlixAbstractionLeakageCheck::registerPPCallbacks(const SourceManager &SM,
                                                        Preprocessor *,
                                                        Preprocessor *) {
  StringRef Path = getCurrentMainFile();
  if (!isLinuxOwnerPath(Path))
    return;
  CurrentSM = &SM;
}

void OrlixAbstractionLeakageCheck::onEndOfTranslationUnit() {
  scanMainFile();
}

void OrlixAbstractionLeakageCheck::scanMainFile() {
  if (!CurrentSM)
    return;
  const SourceManager &SM = *CurrentSM;
  FileID FID = SM.getMainFileID();
  auto Entry = SM.getFileEntryRefForID(FID);
  if (!Entry)
    return;
  StringRef Path = Entry->getName();
  if (!isLinuxOwnerPath(Path))
    return;

  if (pathMatchesCompatNaming(Path)) {
    diag(SM.getLocForStartOfFile(FID),
         "Linux-owner header naming is forbidden when it dresses Linux concepts with repo-local compat or helper suffixes; use Linux names instead");
  }
  if (pathMatchesPrivateSplitHeader(Path)) {
    diag(SM.getLocForStartOfFile(FID),
         "repo-local private split headers are forbidden in Linux-owner code; keep the direct vendored Linux include path and fix the build or lint environment instead");
  }

  StringRef Buffer = SM.getBufferData(FID);
  scanLines(*this, SM, Buffer, AbstractionLeakageRules);
}

} // namespace clang::tidy::orlix
