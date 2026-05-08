#include "IXLandAbstractionLeakageCheck.h"

#include "clang/Lex/Preprocessor.h"

#include <regex>
#include <string>
#include <vector>

namespace clang::tidy::ixland {

namespace {

struct RegexRule {
  const char *Pattern;
  const char *Message;
};

bool pathHasComponent(llvm::StringRef Path, llvm::StringRef Needle) {
  return Path.contains(Needle);
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
};

} // namespace

IXLandAbstractionLeakageCheck::IXLandAbstractionLeakageCheck(
    llvm::StringRef Name, ClangTidyContext *Context)
    : ClangTidyCheck(Name, Context) {}

bool IXLandAbstractionLeakageCheck::isLinuxOwnerPath(llvm::StringRef Path) const {
  return pathHasComponent(Path, "IXLandKernel/fs/") ||
         pathHasComponent(Path, "IXLandKernel/kernel/") ||
         pathHasComponent(Path, "IXLandKernel/runtime/") ||
         pathHasComponent(Path, "IXLandKernel/include/");
}

void IXLandAbstractionLeakageCheck::registerPPCallbacks(const SourceManager &SM,
                                                        Preprocessor *,
                                                        Preprocessor *) {
  StringRef Path = getCurrentMainFile();
  if (!isLinuxOwnerPath(Path))
    return;
  CurrentSM = &SM;
}

void IXLandAbstractionLeakageCheck::onEndOfTranslationUnit() {
  scanMainFile();
}

void IXLandAbstractionLeakageCheck::scanMainFile() {
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

  StringRef Buffer = SM.getBufferData(FID);
  scanLines(*this, SM, Buffer, AbstractionLeakageRules);
}

} // namespace clang::tidy::ixland
