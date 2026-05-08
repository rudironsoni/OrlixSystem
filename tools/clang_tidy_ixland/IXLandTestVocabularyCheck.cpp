#include "IXLandTestVocabularyCheck.h"

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

const std::vector<RegexRule> KernelTestRules = {
    {R"(\bI[X]_(AT_|F_)[A-Z0-9_]+\b)",
     "branded wrapper macros are forbidden in tests"},
    {R"(\bI[X]_(F|AT|TC|TIOC|SIG|W)[A-Za-z0-9_]*\b)",
     "Linux constant alias drift is forbidden in tests"},
    {R"(\bTES[T]_(AT_|F_|SIG|W)[A-Za-z0-9_]*\b)",
     "test-prefixed raw constants are forbidden in tests"},
    {R"(\bi[x]land_test_[A-Za-z0-9_]*\b)",
     "branded test-helper vocabulary is forbidden in tests"},
    {R"(\bi[x]land_test_uapi[_](at_|f_))",
     "Linux UAPI helper soup is forbidden in tests"},
    {R"(include/ixland/linux_(uapi|abi)_constants\.h)",
     "Linux alias headers are forbidden in tests"},
    {R"(\blinux_(s_ifmt|s_is[a-z0-9_]+|at_[a-z0-9_]+|f_[a-z0-9_]+)\b)",
     "linux_* accessor soup is forbidden in tests"},
};

const std::vector<RegexRule> HostTestRules = {
    {R"(\bI[X]_(AT_|F_)[A-Z0-9_]+\b)",
     "branded wrapper macros are forbidden in tests"},
    {R"(\bi[x]land_test_[A-Za-z0-9_]*\b)",
     "branded test-helper vocabulary is forbidden in tests"},
};

} // namespace

IXLandTestVocabularyCheck::IXLandTestVocabularyCheck(llvm::StringRef Name,
                                                     ClangTidyContext *Context)
    : ClangTidyCheck(Name, Context) {}

bool IXLandTestVocabularyCheck::isKernelTestPath(llvm::StringRef Path) const {
  return Path.contains("IXLandKernelTests/");
}

bool IXLandTestVocabularyCheck::isHostTestPath(llvm::StringRef Path) const {
  return Path.contains("IXLandHostAdapterTests/");
}

bool IXLandTestVocabularyCheck::isAnyTestPath(llvm::StringRef Path) const {
  return isKernelTestPath(Path) || isHostTestPath(Path);
}

void IXLandTestVocabularyCheck::registerPPCallbacks(const SourceManager &SM,
                                                    Preprocessor *,
                                                    Preprocessor *) {
  auto Entry = SM.getFileEntryRefForID(SM.getMainFileID());
  if (!Entry)
    return;
  StringRef Path = Entry->getName();
  if (!isAnyTestPath(Path))
    return;
  CurrentSM = &SM;
}

void IXLandTestVocabularyCheck::onEndOfTranslationUnit() { scanMainFile(); }

void IXLandTestVocabularyCheck::scanMainFile() {
  if (!CurrentSM)
    return;
  const SourceManager &SM = *CurrentSM;
  FileID FID = SM.getMainFileID();
  auto Entry = SM.getFileEntryRefForID(FID);
  if (!Entry)
    return;
  StringRef Path = Entry->getName();
  if (!isAnyTestPath(Path))
    return;

  StringRef Buffer = SM.getBufferData(FID);
  if (isKernelTestPath(Path)) {
    scanLines(*this, SM, Buffer, KernelTestRules);
  } else {
    scanLines(*this, SM, Buffer, HostTestRules);
  }
}

} // namespace clang::tidy::ixland
