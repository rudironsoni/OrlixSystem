#include "OrlixSourcePolicyCheck.h"

#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/StringRef.h"

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

bool hasExtension(llvm::StringRef Path, llvm::StringRef Ext) {
  return Path.ends_with(Ext);
}

SourceLocation translateLocation(const SourceManager &SM, FileID FID,
                                 unsigned Line, unsigned Column) {
  return SM.translateLineCol(FID, Line, Column);
}

void scanLines(ClangTidyCheck &Check, const SourceManager &SM, StringRef Buffer,
               StringRef Path,
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
      if (Path.ends_with("OrlixKernel/fs/path.h") &&
          std::string(Rule.Message) ==
              "MAX_PATH ownership belongs to OrlixKernel/fs/path.h; duplicate repo-local definitions are forbidden") {
        continue;
      }
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

  (void)Path;
}

const std::vector<RegexRule> SourceRules = {
    {R"(\b(fdopendir|readdir|closedir|seekdir|telldir)\s*\()",
     "host directory iteration APIs are forbidden in Linux-owner code"},
    {R"(^\s*#\s*include\s*<dirent\.h>)",
     "host dirent headers are forbidden in Linux-owner code"},
    {R"(^\s*extern\s+int\s+errno\s*;)",
     "libc errno globals are forbidden in Linux-owner code; keep kernel-owner helpers on direct negative errno returns instead"},
    {R"(^\s*extern\s+.*\b(__error|malloc|calloc|realloc|free|abort|exit|atexit|getenv|setenv|unsetenv|qsort|bsearch)\s*\()",
     "libc forward declarations are forbidden in Linux-owner code; use Linux-owned kernel facilities or narrow host seams instead"},
    {R"(\b__error\s*\()",
     "libc errno accessors are forbidden in Linux-owner code; keep kernel-owner helpers on direct negative errno returns instead"},
    {R"(^\s*#\s*define\s+errno\b)",
     "libc errno macro rebinding is forbidden in Linux-owner code; keep kernel-owner helpers on direct negative errno returns instead"},
    {R"(\b(malloc|calloc|realloc|free|abort|exit|atexit|getenv|setenv|unsetenv|qsort|bsearch)\s*\()",
     "libc runtime calls are forbidden in Linux-owner code; use Linux-owned kernel facilities or narrow host seams instead"},
    {R"(^\s*#\s*define\s+[A-Za-z_][A-Za-z0-9_]*\s+__builtin_[A-Za-z0-9_]+\b)",
     "compiler builtin alias macros are forbidden in Linux-owner code; fix the owning Linux header surface or lint environment instead"},
    {R"(\b__builtin_(strlen|memcmp|memcpy|memset|strcmp|strchr|strrchr)\s*\()",
     "direct compiler builtin string or memory calls are forbidden in Linux-owner code; use Linux-owned headers or fix the owning header surface instead"},
    {R"(\b__builtin_va_(start|arg|end|copy)\s*\(|\b__builtin_va_list\b)",
     "direct compiler builtin varargs use is forbidden in Linux-owner code; use vendored linux/stdarg.h instead"},
    {R"(^\s*#\s*define\s+MAX_PATH\b)",
     "MAX_PATH ownership belongs to OrlixKernel/fs/path.h; duplicate repo-local definitions are forbidden"},
    {R"(^\s*#\s*include\s*<asm/unistd\.h>)",
     "direct asm/unistd.h intake is forbidden in Linux-owner code; use vendored uapi/asm/unistd.h for syscall ABI numbers instead"},
    {R"(^\s*#\s*undef\s+(TASK_[A-Z0-9_]+|SIG[A-Z0-9_]+|W[A-Z0-9_]+|AF_[A-Z0-9_]+|SOCK_[A-Z0-9_]+|SOL_[A-Z0-9_]+|CLONE_[A-Z0-9_]+|RLIM_[A-Z0-9_]+)\b)",
     "undef escapes around vendored Linux names are forbidden in Linux-owner code; fix the ownership or lint environment instead"},
    {R"(\b(dlsym|RTLD_NEXT|RTLD_DEFAULT|dlopen|pthread_[a-z_]+|objc_[a-z_]+|mach_[a-z_]+|os_log)\b)",
     "forbidden host APIs/tokens found in Linux-owner code"},
    {R"(\b(__APPLE__|__MACH__|TARGET_OS_[A-Z0-9_]+)\b)",
     "Darwin/iOS platform tokens are forbidden in Linux-owner code"},
};

const std::vector<RegexRule> NonPrivateOwnerHeaderRules = {
    {R"(^\s*(typedef\s+)?struct\s+[A-Za-z_][A-Za-z0-9_]*\s*\{)",
     "repo-private struct layout definitions are forbidden in non-private OrlixKernel owner headers; move the layout under OrlixKernel/private/** or keep the header API-only"},
};

const std::vector<RegexRule> NonSignalOwnerRules = {
    {R"(\b[A-Za-z_][A-Za-z0-9_>]*->signal->(actions|blocked|shared_pending|queue|altstack)\b)",
     "direct signal private-state field access is forbidden outside the signal owner; add or use a signal-owned API instead"},
};

const std::vector<RegexRule> NonSignalFrameOwnerRules = {
    {R"(\b[A-Za-z_][A-Za-z0-9_>]*->mm->signal_frame_[A-Za-z0-9_]+\b)",
     "direct signal frame bookkeeping access is forbidden outside the owning signal/task/mm surfaces; add or use an owner API instead"},
};

const std::vector<RegexRule> OwnerExternBypassRules = {
    {R"(\bextern\s+(int|long|ssize_t|int64_t)\s+(open_impl|openat_impl|creat_impl|flock_impl|dup_impl|dup2_impl|dup3_impl|fcntl_impl|stat_impl|fstat_impl|lstat_impl|access_impl|fstatat_impl|faccessat_impl|statx_impl|ioctl_impl|read_impl|write_impl|lseek_impl|pread_impl|pwrite_impl|copy_file_range_impl|fallocate_impl|sync_file_range_impl|splice_impl|vmsplice_impl|tee_impl|readv_impl|writev_impl|preadv_impl|pwritev_impl|preadv2_impl|pwritev2_impl|sendfile_impl)\s*\()",
     "raw extern declarations for repo-owned syscall surfaces are forbidden; include the owning OrlixKernel header instead"},
};

} // namespace

OrlixSourcePolicyCheck::OrlixSourcePolicyCheck(llvm::StringRef Name,
                                                 ClangTidyContext *Context)
    : ClangTidyCheck(Name, Context) {}

bool OrlixSourcePolicyCheck::isLinuxOwnerPath(llvm::StringRef Path) const {
  return pathHasComponent(Path, "OrlixKernel/fs/") ||
         pathHasComponent(Path, "OrlixKernel/kernel/") ||
         pathHasComponent(Path, "OrlixKernel/runtime/") ||
         pathHasComponent(Path, "OrlixKernel/include/") ||
         pathHasComponent(Path, "OrlixKernel/internal/");
}

void OrlixSourcePolicyCheck::registerPPCallbacks(
    const SourceManager &SM, Preprocessor *, Preprocessor *) {
  StringRef Path = getCurrentMainFile();
  if (!isLinuxOwnerPath(Path))
    return;
  CurrentSM = &SM;
}

void OrlixSourcePolicyCheck::onEndOfTranslationUnit() { scanMainFile(); }

void OrlixSourcePolicyCheck::scanMainFile() {
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

  if (hasExtension(Path, ".m") || hasExtension(Path, ".mm")) {
    diag(SM.getLocForStartOfFile(FID),
         "Objective-C files are forbidden in Linux-owner paths");
  }

  StringRef Buffer = SM.getBufferData(FID);
  scanLines(*this, SM, Buffer, Path, SourceRules);

  if (hasExtension(Path, ".h") &&
      (pathHasComponent(Path, "OrlixKernel/fs/") ||
       pathHasComponent(Path, "OrlixKernel/kernel/")) &&
      !pathHasComponent(Path, "OrlixKernel/private/")) {
    scanLines(*this, SM, Buffer, Path, NonPrivateOwnerHeaderRules);
  }

  if (!Path.ends_with("OrlixKernel/kernel/signal.c")) {
    scanLines(*this, SM, Buffer, Path, NonSignalOwnerRules);
  }

  if (!Path.ends_with("OrlixKernel/kernel/signal.c") &&
      !Path.ends_with("OrlixKernel/kernel/task.c") &&
      !Path.ends_with("OrlixKernel/kernel/mm.c") &&
      !Path.ends_with("OrlixKernel/private/kernel/task_state.h")) {
    scanLines(*this, SM, Buffer, Path, NonSignalFrameOwnerRules);
  }

  scanLines(*this, SM, Buffer, Path, OwnerExternBypassRules);
}

} // namespace clang::tidy::orlix
