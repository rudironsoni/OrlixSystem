#include "OrlixTestPolicyCheck.h"

#include "clang/AST/Decl.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/Linkage.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"

#include <regex>
#include <string>
#include <vector>

namespace clang::tidy::orlix {

using namespace clang::ast_matchers;

namespace {

bool isObjectiveCKernelTest(llvm::StringRef Path) {
  return Path.ends_with(".m") || Path.ends_with(".mm");
}

bool isKernelHarnessSupportPath(llvm::StringRef Path) {
  return Path.contains("OrlixKernelTests/kunit/kunit.c") ||
         Path.contains("OrlixKernelTests/PTYSessionIoctlShim.c");
}

struct RegexRule {
  const char *Pattern;
  const char *Message;
};

SourceLocation translateLocation(const SourceManager &SM, FileID FID,
                                 unsigned Line, unsigned Column) {
  return SM.translateLineCol(FID, Line, Column);
}

void scanLines(ClangTidyCheck &Check, const SourceManager &SM, StringRef Buffer,
               const std::vector<RegexRule> &Rules, llvm::StringRef Path) {
  FileID FID = SM.getMainFileID();
  const bool IsCompileSmoke = Path.ends_with("CompileSmoke.c");
  size_t Start = 0;
  unsigned LineNo = 1;
  while (Start <= Buffer.size()) {
    size_t End = Buffer.find('\n', Start);
    if (End == StringRef::npos)
      End = Buffer.size();
    std::string Line(Buffer.slice(Start, End).str());
    for (const auto &Rule : Rules) {
      if (IsCompileSmoke &&
          std::string(Rule.Message) ==
              "local fallback definitions for Linux ABI constants are forbidden in LinuxKernel tests; use vendored Linux headers instead") {
        continue;
      }
      if (std::string(Rule.Message) ==
              "MAX_PATH ownership belongs to OrlixKernel/fs/path.h; duplicate repo-local definitions are forbidden in LinuxKernel tests" &&
          Path.ends_with("OrlixKernel/fs/path.h")) {
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
}

class TestPolicyPPCallbacks : public PPCallbacks {
public:
  TestPolicyPPCallbacks(OrlixTestPolicyCheck &Check, const SourceManager &SM,
                        llvm::StringRef Path, bool KernelTests)
      : Check(Check), Path(Path.str()), KernelTests(KernelTests), SM(SM) {}

  void InclusionDirective(SourceLocation HashLoc, const Token &,
                          StringRef FileName, bool IsAngled,
                          CharSourceRange, OptionalFileEntryRef,
                          StringRef, StringRef, const Module *, bool,
                          SrcMgr::CharacteristicKind) override {
    if (KernelTests) {
      if (SM.isWrittenInMainFile(HashLoc)) {
        static const std::vector<std::string> ForbiddenKernelTestHeaders = {
            "string.h",      "strings.h",      "stdatomic.h",
            "sys/socket.h",  "sys/types.h",    "sys/uio.h",
            "sys/ioctl.h",   "sys/select.h",   "sys/resource.h",
            "sys/statvfs.h", "sys/wait.h",     "poll.h",
            "termios.h",     "signal.h"};

        for (const auto &Header : ForbiddenKernelTestHeaders) {
          if (!isKernelHarnessSupportPath(Path) &&
              (FileName == Header || FileName.starts_with(Header))) {
            Check.diag(HashLoc,
                       "host libc or POSIX headers are forbidden in LinuxKernel tests; use vendored Linux headers or fix the owning lint environment instead");
          }
        }

        if (!isKernelHarnessSupportPath(Path) &&
            FileName == "asm/stat.h") {
          Check.diag(HashLoc,
                     "full asm/stat.h is forbidden in LinuxKernel tests that prove userspace ABI; use vendored UAPI stat surfaces instead");
        }
      }

      if (FileName.starts_with("linux/") || FileName.starts_with("asm/") ||
          FileName.starts_with("asm-generic/") ||
          FileName.starts_with("uapi/")) {
        if (isObjectiveCKernelTest(Path)) {
          Check.diag(HashLoc,
                     "Objective-C LinuxKernel tests must stay harness-only and must not include vendored Linux headers");
        }
      }

      if (isObjectiveCKernelTest(Path) &&
          (FileName == "kernel/task.h" || FileName == "kernel/signal.h" ||
           FileName == "kernel/wait.h" || FileName == "kernel/futex.h" ||
           FileName == "fs/fdtable.h" || FileName == "fs/vfs.h" ||
           FileName == "fs/path.h" || FileName == "fs/pty.h")) {
        Check.diag(HashLoc,
                   "Objective-C LinuxKernel tests must stay harness-only and must not include kernel-private owner headers");
      }

      if (FileName.contains("internal/ios") ||
          FileName.starts_with("OrlixHostAdapter/") ||
          FileName.contains("backing_io.h") ||
          FileName.contains("backing_io_decls.h")) {
        Check.diag(HashLoc,
                   "LinuxKernel tests must not include host mediation headers");
      }

      if (SM.isWrittenInMainFile(HashLoc)) {
        if (FileName == "linux/fs.h") {
          SawLinuxFs = true;
          if (SawRepoFsOwner) {
            Check.diag(HashLoc,
                       "mixing full linux/fs.h with repo fs owner headers in one LinuxKernel test is forbidden; use the Linux ABI owner actually needed instead of combining incompatible VFS models");
          }
        } else if (FileName == "fs/vfs.h" || FileName == "fs/fdtable.h" ||
                   FileName == "fs/path.h" || FileName == "fs/pty.h") {
          SawRepoFsOwner = true;
          if (SawLinuxFs) {
            Check.diag(HashLoc,
                       "mixing repo fs owner headers with full linux/fs.h in one LinuxKernel test is forbidden; use the Linux ABI owner actually needed instead of combining incompatible VFS models");
          }
        } else if (FileName == "linux/signal.h") {
          SawLinuxSignal = true;
          if (SawRepoSignalOwner) {
            Check.diag(HashLoc,
                       "mixing full linux/signal.h with repo kernel signal owners in one LinuxKernel test is forbidden; keep Linux ABI constants separate from repo signal runtime ownership");
          }
        } else if (FileName == "kernel/signal.h") {
          SawRepoSignalOwner = true;
          if (SawLinuxSignal) {
            Check.diag(HashLoc,
                       "mixing repo kernel signal owners with full linux/signal.h in one LinuxKernel test is forbidden; keep Linux ABI constants separate from repo signal runtime ownership");
          }
        } else if (FileName == "linux/sched.h") {
          SawLinuxSched = true;
          if (SawRepoTaskOwner) {
            Check.diag(HashLoc,
                       "mixing full linux/sched.h with repo task owners in one LinuxKernel test is forbidden; keep Linux task ABI/constants separate from repo task runtime ownership");
          }
        } else if (FileName == "kernel/task.h") {
          SawRepoTaskOwner = true;
          if (SawLinuxSched) {
            Check.diag(HashLoc,
                       "mixing repo task owners with full linux/sched.h in one LinuxKernel test is forbidden; keep Linux task ABI/constants separate from repo task runtime ownership");
          }
        }
      }
    }
  }

private:
  OrlixTestPolicyCheck &Check;
  std::string Path;
  bool KernelTests;
  const SourceManager &SM;
  bool SawLinuxFs = false;
  bool SawRepoFsOwner = false;
  bool SawLinuxSignal = false;
  bool SawRepoSignalOwner = false;
  bool SawLinuxSched = false;
  bool SawRepoTaskOwner = false;
};

const std::vector<RegexRule> KernelTestRules = {
    {R"(\bHostTestSupport\b|\bOrlixHostAdapterTests\b)",
     "LinuxKernel tests must not reference HostBridge support"},
    {R"(\bS_ISDIR\s*\(|\bS_ISLNK\s*\(|\bS_ISREG\s*\(|\bS_ISCHR\s*\()",
     "Darwin S_IS* macros must not be used as Linux proof in tests"},
    {R"(^\s*#\s*define\s+[A-Za-z_][A-Za-z0-9_]*\s+__builtin_[A-Za-z0-9_]+\b)",
     "compiler builtin alias macros are forbidden in LinuxKernel tests; fix the owning Linux header surface or lint environment instead"},
    {R"(\b__builtin_(strlen|memcmp|memcpy|memset|strcmp|strchr|strrchr)\s*\()",
     "direct compiler builtin string or memory calls are forbidden in LinuxKernel tests; use Linux-owned headers or fix the owning header surface instead"},
    {R"(\b__builtin_va_(start|arg|end|copy)\s*\(|\b__builtin_va_list\b)",
     "direct compiler builtin varargs use is forbidden in LinuxKernel tests; use vendored linux/stdarg.h instead"},
    {R"(^\s*#\s*define\s+MAX_PATH\b)",
     "MAX_PATH ownership belongs to OrlixKernel/fs/path.h; duplicate repo-local definitions are forbidden in LinuxKernel tests"},
    {R"(^\s*#\s*include\s*<asm/unistd\.h>)",
     "direct asm/unistd.h intake is forbidden in LinuxKernel tests; use vendored uapi/asm/unistd.h for syscall ABI numbers instead"},
    {R"(^\s*#\s*undef\s+(TASK_[A-Z0-9_]+|SIG[A-Z0-9_]+|W[A-Z0-9_]+|AF_[A-Z0-9_]+|SOCK_[A-Z0-9_]+|SOL_[A-Z0-9_]+|CLONE_[A-Z0-9_]+|RLIM_[A-Z0-9_]+)\b)",
     "undef escapes around vendored Linux names are forbidden in LinuxKernel tests; fix the ownership or lint environment instead"},
    {R"(^\s*#\s*ifndef\s+(?![A-Z0-9_]*_H\b)(S_[A-Z0-9_]+|AF_[A-Z0-9_]+|SOCK_[A-Z0-9_]+|SOL_[A-Z0-9_]+|SIG[A-Z0-9_]+|CLONE_[A-Z0-9_]+|EPOLL[A-Z0-9_]*|O_[A-Z0-9_]+|F_[A-Z0-9_]+|AT_[A-Z0-9_]+|RLIM_[A-Z0-9_]+)\b)",
     "local fallback definitions for Linux ABI constants are forbidden in LinuxKernel tests; use vendored Linux headers instead"},
    {R"(syscall_dispatch_impl\s*\(\s*__NR_socketpair\s*,\s*[0-9]+\s*,\s*[0-9]+\s*,)",
     "raw numeric Linux socket ABI arguments are forbidden in LinuxKernel tests; consume the vendored Linux header owner for these constants instead"},
    {R"(\bextern\s+int\s+(ioctl|open|close|snprintf)\s*\()",
     "host syscall forward declarations are forbidden in test support"},
    {R"(\bsnprintf\s*\()",
     "snprintf is forbidden in test support"},
    {R"(\b[A-Za-z_][A-Za-z0-9_>]*->signal->(actions|blocked|shared_pending|queue|altstack)\b)",
     "direct signal private-state field access is forbidden in LinuxKernel tests outside the owning signal subsystem tests; add or use a signal-owned API instead"},
    {R"(\b[A-Za-z_][A-Za-z0-9_>]*->mm->signal_frame_[A-Za-z0-9_]+\b)",
     "direct signal frame bookkeeping access is forbidden in LinuxKernel tests; add or use an owner API instead"},
};

const std::vector<RegexRule> KernelSignalSubsystemRules = {
    {R"(\b[A-Za-z_][A-Za-z0-9_>]*->mm->signal_frame_[A-Za-z0-9_]+\b)",
     "direct signal frame bookkeeping access is forbidden in LinuxKernel tests; add or use an owner API instead"},
};

const std::vector<RegexRule> HostAdapterRules = {
    {R"(__attribute__\s*\(\(\s*visibility\s*\(\s*"default"\s*\)\s*\)\))",
     "OrlixHostAdapter serves OrlixKernel only and must not export public/default-visible libc-style entry points"},
};

bool shouldSkipLocation(SourceLocation Loc, const SourceManager &SM) {
  return Loc.isInvalid() || SM.isInSystemHeader(Loc);
}

bool isForbiddenHostAdapterWrapperName(llvm::StringRef Name) {
  return llvm::StringSwitch<bool>(Name)
      .Cases({"getuid", "geteuid", "getgid", "getegid"}, true)
      .Cases({"setuid", "setgid", "seteuid", "setegid"}, true)
      .Cases({"setresuid", "setresgid", "setreuid", "setregid"}, true)
      .Cases({"getresuid", "getresgid", "setfsuid", "setfsgid"}, true)
      .Cases({"getgroups", "setgroups", "prctl", "capget"}, true)
      .Cases({"capset", "getrandom", "getentropy", "futex"}, true)
      .Cases({"set_robust_list", "get_robust_list", "time", "gettimeofday"},
             true)
      .Cases({"settimeofday", "clock_gettime", "clock_getres",
              "clock_settime"},
             true)
      .Cases({"sleep", "usleep", "nanosleep", "setitimer"}, true)
      .Cases({"getitimer", "alarm", "uname", "gethostname"}, true)
      .Cases({"sethostname", "getdomainname", "setdomainname", "sigaction"},
             true)
      .Cases({"signal", "kill", "sigprocmask", "sigpending"}, true)
      .Cases({"sigsuspend", "raise", "pause", "killpg"}, true)
      .Cases({"getrlimit", "setrlimit", "getrlimit64", "setrlimit64"}, true)
      .Cases({"times", "getrusage", "prlimit", "waitpid"}, true)
      .Cases({"wait4", "wait", "wait3", "waitid"}, true)
      .Cases({"open", "openat", "creat", "close"}, true)
      .Cases({"pipe", "pipe2", "stat", "fstat"}, true)
      .Cases({"lstat", "access", "faccessat", "fstatat"}, true)
      .Cases({"newfstatat", "statx", "poll", "select"}, true)
      .Cases({"execve", "execv", "execvp", "fexecve"}, true)
      .Cases({"ioctl", "tcgetpgrp", "tcsetpgrp", "tcgetsid"}, true)
      .Cases({"isatty", "dup", "dup2", "dup3"}, true)
      .Cases({"flock", "fcntl", "getdents", "getdents64"}, true)
      .Cases({"setxattr", "lsetxattr", "fsetxattr", "getxattr"}, true)
      .Cases({"lgetxattr", "fgetxattr", "removexattr", "lremovexattr"}, true)
      .Cases({"fremovexattr", "listxattr", "llistxattr", "flistxattr"}, true)
      .Cases({"chmod", "fchmod", "fchmodat", "chown"}, true)
      .Cases({"fchown", "lchown", "fchownat", "umask"}, true)
      .Cases({"truncate", "ftruncate", "epoll_create", "epoll_create1"}, true)
      .Cases({"epoll_ctl", "epoll_wait", "epoll_pwait", "mount"}, true)
      .Cases({"umount", "umount2", "mount_setattr", "open_tree"}, true)
      .Cases({"move_mount", "pivot_root", "sync", "fsync"}, true)
      .Cases({"fdatasync", "syncfs", "statfs", "fstatfs"}, true)
      .Cases({"posix_fadvise", "posix_fallocate", "read", "write"}, true)
      .Cases({"lseek", "pread", "pwrite", "chdir"}, true)
      .Cases({"fchdir", "getcwd", "mkdir", "mkdirat"}, true)
      .Cases({"rmdir", "unlink", "unlinkat", "link"}, true)
      .Cases({"linkat", "symlink", "symlinkat", "readlink"}, true)
      .Cases({"readlinkat", "rename", "renameat", "renameat2"}, true)
      .Cases({"chroot", "readv", "writev", "preadv"}, true)
      .Cases({"pwritev", "preadv2", "pwritev2"}, true)
      .Default(false);
}

} // namespace

OrlixTestPolicyCheck::OrlixTestPolicyCheck(llvm::StringRef Name,
                                             ClangTidyContext *Context)
    : ClangTidyCheck(Name, Context) {}

void OrlixTestPolicyCheck::registerMatchers(MatchFinder *Finder) {
  Finder->addMatcher(functionDecl(isDefinition(), unless(isImplicit())).bind("func"),
                     this);
}

bool OrlixTestPolicyCheck::isHostAdapterPath(llvm::StringRef Path) const {
  return Path.contains("OrlixHostAdapter/");
}

bool OrlixTestPolicyCheck::isKernelTestPath(llvm::StringRef Path) const {
  return Path.contains("OrlixKernelTests/");
}

bool OrlixTestPolicyCheck::isHostTestPath(llvm::StringRef Path) const {
  return Path.contains("OrlixHostAdapterTests/");
}

bool OrlixTestPolicyCheck::isAnyPolicyPath(llvm::StringRef Path) const {
  return isHostAdapterPath(Path) || isKernelTestPath(Path) ||
         isHostTestPath(Path);
}

void OrlixTestPolicyCheck::check(const MatchFinder::MatchResult &Result) {
  const auto *Func = Result.Nodes.getNodeAs<FunctionDecl>("func");
  if (!Func || !Result.SourceManager)
    return;

  const SourceManager &SM = *Result.SourceManager;
  SourceLocation Loc = Func->getLocation();
  if (shouldSkipLocation(Loc, SM))
    return;

  auto Entry = SM.getFileEntryRefForID(SM.getFileID(Loc));
  if (!Entry)
    return;
  llvm::StringRef Path = Entry->getName();
  if (!isHostAdapterPath(Path))
    return;

  if (!Func->isExternallyVisible())
    return;

  llvm::StringRef Name = Func->getName();
  if (!isForbiddenHostAdapterWrapperName(Name))
    return;

  diag(Loc,
       "OrlixHostAdapter serves OrlixKernel only and must not define public "
       "libc/syscall-facing function '%0'; move the public wrapper to "
       "the userspace libc layer and keep only private kernel seam entry points here")
      << Name;
}

void OrlixTestPolicyCheck::registerPPCallbacks(const SourceManager &SM,
                                                Preprocessor *PP,
                                                Preprocessor *) {
  auto Entry = SM.getFileEntryRefForID(SM.getMainFileID());
  if (!Entry)
    return;
  StringRef Path = Entry->getName();
  if (!isAnyPolicyPath(Path))
    return;
  CurrentSM = &SM;
  PP->addPPCallbacks(
      std::make_unique<TestPolicyPPCallbacks>(*this, SM, Path, isKernelTestPath(Path)));
}

void OrlixTestPolicyCheck::onEndOfTranslationUnit() { scanMainFile(); }

void OrlixTestPolicyCheck::scanMainFile() {
  if (!CurrentSM)
    return;
  const SourceManager &SM = *CurrentSM;
  FileID FID = SM.getMainFileID();
  auto Entry = SM.getFileEntryRefForID(FID);
  if (!Entry)
    return;
  StringRef Path = Entry->getName();
  if (!isAnyPolicyPath(Path))
    return;

  StringRef Buffer = SM.getBufferData(FID);
  if (isHostAdapterPath(Path)) {
    scanLines(*this, SM, Buffer, HostAdapterRules, Path);
  }
  if (isKernelTestPath(Path)) {
    if (Path.contains("OrlixKernelTests/kernel/signal/")) {
      scanLines(*this, SM, Buffer, KernelSignalSubsystemRules, Path);
    } else {
      scanLines(*this, SM, Buffer, KernelTestRules, Path);
    }
  }
}

} // namespace clang::tidy::orlix
