#include "IXLandTestPolicyCheck.h"

#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/PPCallbacks.h"
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

class TestPolicyPPCallbacks : public PPCallbacks {
public:
  TestPolicyPPCallbacks(IXLandTestPolicyCheck &Check, llvm::StringRef Path,
                        bool KernelTests)
      : Check(Check), Path(Path.str()), KernelTests(KernelTests) {}

  void InclusionDirective(SourceLocation HashLoc, const Token &,
                          StringRef FileName, bool IsAngled,
                          CharSourceRange, OptionalFileEntryRef,
                          StringRef, StringRef, const Module *, bool,
                          SrcMgr::CharacteristicKind) override {
    if (KernelTests) {
      if (FileName.starts_with("linux/") || FileName.starts_with("asm/")) {
        if (StringRef(Path).ends_with(".m") || StringRef(Path).ends_with(".mm")) {
          Check.diag(HashLoc,
                     "Objective-C LinuxKernel tests must not include Linux UAPI headers");
        }
      }

      if (FileName.contains("internal/ios") ||
          FileName.starts_with("IXLandHostAdapter/") ||
          FileName.contains("backing_io.h") ||
          FileName.contains("backing_io_decls.h")) {
        Check.diag(HashLoc,
                   "LinuxKernel tests must not include host mediation headers");
      }
    }
  }

private:
  IXLandTestPolicyCheck &Check;
  std::string Path;
  bool KernelTests;
};

const std::vector<RegexRule> KernelTestRules = {
    {R"(\bHostTestSupport\b|\bIXLandHostAdapterTests\b)",
     "LinuxKernel tests must not reference HostBridge support"},
    {R"(\bS_ISDIR\s*\(|\bS_ISLNK\s*\(|\bS_ISREG\s*\(|\bS_ISCHR\s*\()",
     "Darwin S_IS* macros must not be used as Linux proof in tests"},
    {R"(\bextern\s+int\s+(ioctl|open|close|snprintf)\s*\()",
     "host syscall forward declarations are forbidden in test support"},
    {R"(\bsnprintf\s*\()",
     "snprintf is forbidden in test support"},
};

} // namespace

IXLandTestPolicyCheck::IXLandTestPolicyCheck(llvm::StringRef Name,
                                             ClangTidyContext *Context)
    : ClangTidyCheck(Name, Context) {}

bool IXLandTestPolicyCheck::isKernelTestPath(llvm::StringRef Path) const {
  return Path.contains("IXLandKernelTests/");
}

bool IXLandTestPolicyCheck::isHostTestPath(llvm::StringRef Path) const {
  return Path.contains("IXLandHostAdapterTests/");
}

bool IXLandTestPolicyCheck::isAnyTestPath(llvm::StringRef Path) const {
  return isKernelTestPath(Path) || isHostTestPath(Path);
}

void IXLandTestPolicyCheck::registerPPCallbacks(const SourceManager &SM,
                                                Preprocessor *PP,
                                                Preprocessor *) {
  auto Entry = SM.getFileEntryRefForID(SM.getMainFileID());
  if (!Entry)
    return;
  StringRef Path = Entry->getName();
  if (!isAnyTestPath(Path))
    return;
  CurrentSM = &SM;
  PP->addPPCallbacks(
      std::make_unique<TestPolicyPPCallbacks>(*this, Path, isKernelTestPath(Path)));
}

void IXLandTestPolicyCheck::onEndOfTranslationUnit() { scanMainFile(); }

void IXLandTestPolicyCheck::scanMainFile() {
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
  }
}

} // namespace clang::tidy::ixland
