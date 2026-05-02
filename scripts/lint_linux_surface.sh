#!/bin/sh
set -eu

OWNER_PATHS="fs kernel runtime include"

echo "=== Check 1: Objective-C files outside allowed paths ==="
OBJC_FILES=$(find fs kernel runtime include -type f \( -name '*.m' -o -name '*.mm' \) 2>/dev/null || true)
if [ -n "$OBJC_FILES" ]; then
    echo "FAIL: Objective-C files found in Linux-owner paths:"
    echo "$OBJC_FILES"
    exit 1
fi
echo "   ✓ No stray .m/.mm files in Linux-owner paths"

echo ""
echo "=== Check 2: Host framework imports in Linux-owner paths ==="
HOST_FRAMEWORKS=$(rg -n '^\s*#\s*(include|import)\s*<(Foundation|UIKit|CoreFoundation|CoreServices|CoreGraphics|TargetConditionals|dispatch|os)/' $OWNER_PATHS 2>/dev/null || true)
if [ -n "$HOST_FRAMEWORKS" ]; then
    echo "FAIL: Host framework imports found in Linux-owner paths:"
    echo "$HOST_FRAMEWORKS"
    exit 1
fi
echo "   ✓ No host framework imports in Linux-owner paths"

echo ""
echo "=== Check 3: Forbidden host headers in Linux-owner paths ==="
FORBIDDEN_HEADERS=$(rg -n '^\s*#\s*include\s*<(pthread\.h|dispatch/.*|mach/.*|os/log\.h|objc/.*|sys/sysctl\.h|TargetConditionals\.h|Foundation/.*|UIKit/.*|CoreFoundation/.*)>' $OWNER_PATHS 2>/dev/null || true)
if [ -n "$FORBIDDEN_HEADERS" ]; then
    echo "FAIL: Forbidden host headers in Linux-owner paths:"
    echo "$FORBIDDEN_HEADERS"
    exit 1
fi
echo "   ✓ No forbidden host headers"

echo ""
echo "=== Check 4: Forbidden host APIs/tokens in Linux-owner paths ==="
FORBIDDEN_TOKENS=$(rg -n -e '\b(dlsym|RTLD_NEXT|RTLD_DEFAULT|dlopen|pthread_[a-z_]+|objc_[a-z_]+|mach_[a-z_]+|os_log)\b' -e '\b__(APPLE|MACH)__\b' -e '\bTARGET_OS_[A-Z0-9_]+\b' $OWNER_PATHS 2>/dev/null || true)
if [ -n "$FORBIDDEN_TOKENS" ]; then
    echo "FAIL: Forbidden host APIs/tokens in Linux-owner paths:"
    echo "$FORBIDDEN_TOKENS"
    exit 1
fi
echo "   ✓ No forbidden host APIs/tokens in Linux-owner paths"

echo ""
echo "=== Check 5: Generic abstraction leakage in Linux-owner paths ==="
GENERIC_ABSTRACTIONS=$(rg -n -e '\b(kmutex|kcond|kthread|konce|ksig|kplatform|kbridge|ix_mutex|ix_cond|ix_thread|ix_platform|ix_bridge|platform_mutex|platform_thread|bridge_mutex|bridge_thread)_[a-z0-9_]*\b' $OWNER_PATHS 2>/dev/null || true)
if [ -n "$GENERIC_ABSTRACTIONS" ]; then
    echo "FAIL: Generic abstraction leakage in Linux-owner paths:"
    echo "$GENERIC_ABSTRACTIONS"
    echo "Use narrow subsystem-owned interfaces under internal/ios/** instead."
    exit 1
fi
echo "   ✓ No generic abstraction leakage in Linux-owner paths"

echo ""
echo "=== Check 6: Wrong-direction mediation boundaries ==="
PUBLIC_IOS=$(rg -n '^\s*#\s*include\s*"internal/ios/.+"' include 2>/dev/null || true)
if [ -n "$PUBLIC_IOS" ]; then
    echo "FAIL: Public headers in include/ must not depend on internal/ios/**:"
    echo "$PUBLIC_IOS"
    exit 1
fi
BROAD_IOS=$(rg -n '^\s*#\s*include\s*"internal/ios/.*/(bridge|platform|generic|common|helpers?|shim|host_api)[^/"]*\.h"' fs kernel runtime 2>/dev/null || true)
if [ -n "$BROAD_IOS" ]; then
    echo "FAIL: Linux-owner code includes broad mediation headers from internal/ios/**:"
    echo "$BROAD_IOS"
    exit 1
fi
echo "   ✓ No wrong-direction broad mediation includes"

echo ""
echo "=== Check 7: Wrong subsystem placement ==="
HOST_IMPL_IN_OWNER=$(rg -n '^\s*(static\s+)?[A-Za-z_][A-Za-z0-9_\s\*]*\s+host_[a-z0-9_]+_impl\s*\([^)]*\)\s*\{' fs kernel runtime include 2>/dev/null || true)
if [ -n "$HOST_IMPL_IN_OWNER" ]; then
    echo "FAIL: host_*_impl function definitions found in Linux-owner paths:"
    echo "$HOST_IMPL_IN_OWNER"
    exit 1
fi
CROSS_SUBSYSTEM=$(rg -n '^\s*#\s*include\s*"internal/ios/fs/.*"' kernel runtime 2>/dev/null || true)
if [ -n "$CROSS_SUBSYSTEM" ]; then
    echo "FAIL: kernel/runtime must not include fs-owned internal/ios mediation headers:"
    echo "$CROSS_SUBSYSTEM"
    exit 1
fi
echo "   ✓ Subsystem placement boundaries hold"

echo ""
echo "=== Check 8: Forbidden logging/debug output in product code ==="
FORBIDDEN_LOGGING=$(rg -n -e '\b(printf|fprintf|vprintf|vfprintf|puts|fputs|putc|putchar|perror|NSLog)\s*\(' -e '\bos_log\s*\(' fs kernel runtime include internal/ios 2>/dev/null || true)
if [ -n "$FORBIDDEN_LOGGING" ]; then
    echo "FAIL: Forbidden logging/debug output in product code:"
    echo "$FORBIDDEN_LOGGING"
    exit 1
fi
echo "   ✓ No forbidden logging/debug output in product code"

echo ""
echo "=== Check 9: ABI/UAPI drift indicators ==="
HANDDEFINED_ABI=$(rg -n '^\s*#define\s+(FUTEX_|AT_|SA_|SIG[A-Z0-9_]+|O_[A-Z0-9_]+|F_[A-Z0-9_]+|RENAME_[A-Z0-9_]+)' fs kernel runtime include 2>/dev/null | rg -v -e 'I[X]_' -e 'TES[T]_' -e '_IMPL' || true)
if [ -n "$HANDDEFINED_ABI" ]; then
    echo "FAIL: Hand-defined Linux ABI constants found in Linux-owner paths:"
    echo "$HANDDEFINED_ABI"
    exit 1
fi
BRANDED_UAPI=$(rg -n -e '__attribute__\(\(visibility\("default"\)\)\)\s+.*\b(ixland_|ios_|darwin_)[A-Za-z0-9_]*\s*\(' fs kernel runtime include 2>/dev/null || true)
if [ -n "$BRANDED_UAPI" ]; then
    echo "FAIL: Branded public ABI/UAPI indicators found:"
    echo "$BRANDED_UAPI"
    exit 1
fi
echo "   ✓ No ABI/UAPI drift indicators"

echo ""
echo "=== Check 10: Test target naming ==="
# Verify no old IXLandSystemTests target in project.yml
OLD_TARGET=$(grep -E '^\s+IXLandSystemTests:' project.yml || true)
if [ -n "$OLD_TARGET" ]; then
    echo "FAIL: Old IXLandSystemTests target still exists in project.yml:"
    echo "$OLD_TARGET"
    exit 1
fi
# Verify no IXLandSystemIOSBridgeTests
WRONG_TARGET=$(grep -E '^\s+IXLandSystemIOSBridgeTests:' project.yml || true)
if [ -n "$WRONG_TARGET" ]; then
    echo "FAIL: Wrong target name IXLandSystemIOSBridgeTests in project.yml:"
    echo "$WRONG_TARGET"
    exit 1
fi
# Verify correct targets exist
KERNEL_TESTS=$(grep -E '^\s+IXLandSystemLinuxKernelTests:' project.yml || true)
if [ -z "$KERNEL_TESTS" ]; then
    echo "FAIL: IXLandSystemLinuxKernelTests target missing from project.yml"
    exit 1
fi
BRIDGE_TESTS=$(grep -E '^\s+IXLandSystemHostBridgeTests:' project.yml || true)
if [ -z "$BRIDGE_TESTS" ]; then
    echo "FAIL: IXLandSystemHostBridgeTests target missing from project.yml"
    exit 1
fi
echo "   ✓ Test targets correctly named"

echo ""
echo "=== Check 11: New broad mediation headers under internal/ios ==="
BROAD_HEADERS=$(find internal/ios -type f -name '*.h' 2>/dev/null | rg '/(bridge|platform|generic|common|helpers?|shim|host_api)[^/]*\.h$' || true)
if [ -n "$BROAD_HEADERS" ]; then
    echo "FAIL: Broad mediation headers found under internal/ios/** (must be narrow and subsystem-owned):"
    echo "$BROAD_HEADERS"
    exit 1
fi
echo "   ✓ No new broad mediation headers under internal/ios"

echo ""
echo "=== Check 12: Test ABI contamination - branded wrapper macros ==="
BRANDED_ALIASES=$(rg -n -e '\bI[X]_AT_[A-Z0-9_]+\b' -e '\bI[X]_F_[A-Z0-9_]+\b' IXLandSystemLinuxKernelTests IXLandSystemHostBridgeTests 2>/dev/null || true)
if [ -n "$BRANDED_ALIASES" ]; then
    echo "FAIL: Branded wrapper macros found in tests:"
    echo "$BRANDED_ALIASES"
    exit 1
fi
echo "   ✓ No branded wrapper macros in tests"

echo ""
echo "=== Check 13: Test ABI contamination - Objective-C Linux UAPI headers ==="
TEST_OBJC_LINUX_HEADERS=$(rg -n '^[[:space:]]*#(include|import)[[:space:]]*<(linux|asm)/' IXLandSystemLinuxKernelTests IXLandSystemHostBridgeTests --glob '*.m' 2>/dev/null || true)
if [ -n "$TEST_OBJC_LINUX_HEADERS" ]; then
    echo "FAIL: Objective-C tests include Linux UAPI headers:"
    echo "$TEST_OBJC_LINUX_HEADERS"
    exit 1
fi
echo "   ✓ No Objective-C Linux UAPI headers in tests"

echo ""
echo "=== Check 14: Test ABI contamination - linux_* accessor soup ==="
TEST_LINUX_ACCESSORS=$(rg -n -e '\blinux_(s_ifmt|s_is[a-z0-9_]+|at_[a-z0-9_]+|f_[a-z0-9_]+)\b' IXLandSystemHostBridgeTests 2>/dev/null || true)
if [ -n "$TEST_LINUX_ACCESSORS" ]; then
    echo "FAIL: linux_* accessor wrappers found in Objective-C tests:"
    echo "$TEST_LINUX_ACCESSORS"
    exit 1
fi
echo "   ✓ No linux_* accessor soup in Objective-C tests"

echo ""
echo "=== Check 15: Test ABI contamination - raw Linux constants ==="
TEST_RAW_LINUX_CONSTANTS=$(rg -n -e '0x[0-9A-Fa-f]+' IXLandSystemLinuxKernelTests IXLandSystemHostBridgeTests --glob '*.m' 2>/dev/null | rg -v 'INVALID_FLAG_TEST_VALUE|RENAME_NOREPLACE|RENAME_EXCHANGE|RENAME_WHITEOUT|memset\(|/\*|//' || true)
if [ -n "$TEST_RAW_LINUX_CONSTANTS" ]; then
    echo "FAIL: Suspicious raw numeric constants found in Objective-C tests:"
    echo "$TEST_RAW_LINUX_CONSTANTS"
    exit 1
fi
echo "   ✓ No raw Linux ABI constants in tests"

echo ""
echo "=== Check 16: Test ABI contamination - LinuxKernel host mediation includes ==="
TEST_LINUXKERNEL_HOST_INCLUDES=$(rg -n -e '^[[:space:]]*#(include|import)[[:space:]]*"internal/ios/' -e '^[[:space:]]*#(include|import)[[:space:]]*".*backing_io(_decls)?\.h"' IXLandSystemLinuxKernelTests --glob '*.[mhc]' 2>/dev/null || true)
if [ -n "$TEST_LINUXKERNEL_HOST_INCLUDES" ]; then
    echo "FAIL: LinuxKernel tests include host mediation headers:"
    echo "$TEST_LINUXKERNEL_HOST_INCLUDES"
    exit 1
fi
echo "   ✓ No host mediation includes in LinuxKernel tests"


echo ""
echo "=== Check 15: Test ABI contamination - TEST_* raw constants ==="
TEST_CONSTANTS=$(rg -n '^\s*#define\s+TEST_(AT_|RENAME_|F_|FD_)' IXLandSystemLinuxKernelTests/*.m IXLandSystemLinuxKernelTests/*.c IXLandSystemHostBridgeTests/*.m IXLandSystemHostBridgeTests/*.c 2>/dev/null || true)
if [ -n "$TEST_CONSTANTS" ]; then
    echo "FAIL: TEST_* raw constants found in test files:"
    echo "$TEST_CONSTANTS"
    echo "Use semantic test helpers instead of #define TEST_* constants."
    exit 1
fi
echo "   ✓ No TEST_* raw constants in tests"

echo ""
echo "=== Check 15b: Test ABI contamination - at constant helpers ==="
UAPI_AT_HELPERS=$(rg -n '\bi[x]land_test_uapi[_]at_' IXLandSystemLinuxKernelTests/*.m IXLandSystemLinuxKernelTests/*.c IXLandSystemHostBridgeTests/*.m IXLandSystemHostBridgeTests/*.c 2>/dev/null || true)
if [ -n "$UAPI_AT_HELPERS" ]; then
    echo "FAIL: at constant helper soup found in test files:"
    echo "$UAPI_AT_HELPERS"
    echo "Move Linux UAPI constant usage to C contract files with canonical Linux names."
    exit 1
fi
echo "   ✓ No at constant helper soup in tests"

echo ""
echo "=== Check 15c: Test ABI contamination - fcntl constant helpers ==="
UAPI_F_HELPERS=$(rg -n '\bi[x]land_test_uapi[_]f_' IXLandSystemLinuxKernelTests/*.m IXLandSystemLinuxKernelTests/*.c IXLandSystemHostBridgeTests/*.m IXLandSystemHostBridgeTests/*.c 2>/dev/null || true)
if [ -n "$UAPI_F_HELPERS" ]; then
    echo "FAIL: fcntl constant helper soup found in test files:"
    echo "$UAPI_F_HELPERS"
    echo "Move Linux UAPI constant usage to C contract files with canonical Linux names."
    exit 1
fi
echo "   ✓ No fcntl constant helper soup in tests"

echo ""
echo "=== Check 16: Bridge bag usage in Linux-facing tests ==="
BRIDGE_BAG=$(rg -n 'internal/ios/fs/backing_io\.h|internal/ios/fs/backing_io_decls\.h' IXLandSystemLinuxKernelTests/*.m 2>/dev/null || true)
if [ -n "$BRIDGE_BAG" ]; then
    echo "FAIL: Broad bridge bag headers found in Linux-facing tests:"
    echo "$BRIDGE_BAG"
    echo "Use narrow forward declarations instead of broad bridge bags."
    exit 1
fi
echo "   ✓ No broad bridge bag usage in Linux-facing tests"

echo ""
echo "=== Check 17: Broken host syscall errno rewriting ==="
BROKEN_HOST_ERRNO=$(rg -n 'errno[[:space:]]*=[[:space:]]*-ret|errno[[:space:]]*=[[:space:]]*\(int\)-ret' internal/ios/fs/path_host.c internal/ios/fs/backing_io.m 2>/dev/null || true)
if [ -n "$BROKEN_HOST_ERRNO" ]; then
    echo "FAIL: Broken host syscall errno rewriting found:"
    echo "$BROKEN_HOST_ERRNO"
    exit 1
fi
echo "   ✓ No broken host syscall errno rewriting"

echo ""
echo "=== Check 17a: execve transition wiring regressions ==="
EXECVE_BODY=$(python3 - <<'PY'
from pathlib import Path
text = Path('fs/exec.c').read_text()
start = text.index('int execve(')
end = text.index('int execv(')
print(text[start:end])
PY
)
EXECVE_REGRESSIONS=$(printf '%s' "$EXECVE_BODY" | rg -n 'task->comm|task->exe|vfork_exec_notify\s*\(|exec_close_cloexec\s*\(' 2>/dev/null || true)
if [ -n "$EXECVE_REGRESSIONS" ]; then
    echo "FAIL: Duplicated exec transition logic found in fs/exec.c execve():"
    echo "$EXECVE_REGRESSIONS"
    exit 1
fi
echo "   ✓ execve() routes through task_exec_transition_impl"

echo ""
echo "=== Check 17b: Test Linux UAPI contamination aliases ==="
TEST_UAPI_CONTAMINATION=$(rg -n 'include/ixland/linux_uapi_constants[.]h|\bI[X]_(AT_|F_)|\bTES[T]_(AT_|F_)|\bi[x]land_test_uapi[_](at_|f_)' IXLandSystemLinuxKernelTests IXLandSystemHostBridgeTests 2>/dev/null || true)
if [ -n "$TEST_UAPI_CONTAMINATION" ]; then
    echo "FAIL: Test Linux UAPI contamination aliases found:"
    echo "$TEST_UAPI_CONTAMINATION"
    exit 1
fi
echo "   ✓ No test Linux UAPI contamination aliases"

echo ""
echo "=== Check 17c: Unified host_fstat_impl contract ==="
HOST_FSTAT_CONTRACT=$(rg -n 'host_fstat_impl\s*\([^)]*struct stat\s*\*' internal/ios/fs 2>/dev/null || true)
if [ -n "$HOST_FSTAT_CONTRACT" ]; then
    echo "FAIL: host_fstat_impl declared with host struct stat:"
    echo "$HOST_FSTAT_CONTRACT"
    exit 1
fi
echo "   ✓ host_fstat_impl uses linux_stat contract"

echo ""
echo "=== Check 18: Darwin S_IS* used as Linux proof ==="
DARWIN_STAT=$(rg -n '\bS_ISDIR\s*\(|\bS_ISLNK\s*\(|\bS_ISREG\s*\(|\bS_ISCHR\s*\(' IXLandSystemLinuxKernelTests/*.m IXLandSystemHostBridgeTests/*.m 2>/dev/null | rg -v 'LinuxUAPITestSupport' || true)
if [ -n "$DARWIN_STAT" ]; then
    echo "FAIL: Darwin S_IS* macros used as Linux proof in tests:"
    echo "$DARWIN_STAT"
    echo "Use mode_is_* helpers instead."
    exit 1
fi
echo "   ✓ No Darwin S_IS* misuse in tests"

echo ""
echo "=== Check 18: project.yml - Linux UAPI include path contamination ==="
# Verify IXLandSystemLinuxKernelTests does not have global Linux UAPI include paths
KERNEL_GLOBAL_UAPI=$(grep -A10 'IXLandSystemLinuxKernelTests:' project.yml | grep -E 'LINUX_UAPI_INCLUDE_ROOT\s*}' || true)
if [ -n "$KERNEL_GLOBAL_UAPI" ]; then
    echo "FAIL: IXLandSystemLinuxKernelTests has global Linux UAPI include paths:"
    echo "$KERNEL_GLOBAL_UAPI"
    exit 1
fi
# Verify IXLandSystemHostBridgeTests does not have global Linux UAPI include paths
BRIDGE_GLOBAL_UAPI=$(grep -A10 'IXLandSystemHostBridgeTests:' project.yml | grep -E 'LINUX_UAPI_INCLUDE_ROOT\s*}' || true)
if [ -n "$BRIDGE_GLOBAL_UAPI" ]; then
    echo "FAIL: IXLandSystemHostBridgeTests has global Linux UAPI include paths:"
    echo "$BRIDGE_GLOBAL_UAPI"
    exit 1
fi
# Verify LinuxUAPITestSupport.c has per-source compilerFlags
LINUX_UAPI_FLAGS=$(grep -A5 'path: IXLandSystemLinuxKernelTests/LinuxUAPITestSupport.c' project.yml | grep 'LINUX_UAPI_INCLUDE_ROOT' || true)
if [ -z "$LINUX_UAPI_FLAGS" ]; then
    echo "FAIL: LinuxUAPITestSupport.c missing per-source Linux UAPI compilerFlags"
    exit 1
fi
echo "   ✓ Linux UAPI include paths scoped to approved C test support files"

echo ""
echo "=== Check 19: Linux UAPI headers included from .m test files ==="
UAPI_IN_M=$(grep -l '#include\s*<linux/' IXLandSystemLinuxKernelTests/*.m IXLandSystemHostBridgeTests/*.m 2>/dev/null || true)
UAPI_IN_M_ASM=$(grep -l '#include\s*<asm/' IXLandSystemLinuxKernelTests/*.m IXLandSystemHostBridgeTests/*.m 2>/dev/null || true)
if [ -n "$UAPI_IN_M" ] || [ -n "$UAPI_IN_M_ASM" ]; then
    echo "FAIL: Linux UAPI headers included from Objective-C test files:"
    echo "$UAPI_IN_M"
    echo "$UAPI_IN_M_ASM"
    echo "Linux UAPI headers must only be included by approved C support files."
    exit 1
fi
echo "   ✓ No Linux UAPI headers in Objective-C test files"

echo ""
echo "=== Check 20: Host syscall declarations in test support ==="
HOST_SYSCALL_DECLS=$(rg -n 'extern\s+int\s+(ioctl|open|close|snprintf)\s*\(' IXLandSystemLinuxKernelTests/*.c IXLandSystemLinuxKernelTests/*.m IXLandSystemHostBridgeTests/*.c IXLandSystemHostBridgeTests/*.m 2>/dev/null || true)
if [ -n "$HOST_SYSCALL_DECLS" ]; then
    echo "FAIL: Host syscall forward declarations in test support files:"
    echo "$HOST_SYSCALL_DECLS"
    echo "Use proper host headers or narrow test-only helpers."
    exit 1
fi
echo "   ✓ No host syscall forward declarations"

echo ""
echo "=== Check 21: snprintf in test support ==="
SNPRINTF_USAGE=$(rg -n 'snprintf' IXLandSystemLinuxKernelTests/*.c IXLandSystemLinuxKernelTests/*.m IXLandSystemHostBridgeTests/*.c IXLandSystemHostBridgeTests/*.m 2>/dev/null || true)
if [ -n "$SNPRINTF_USAGE" ]; then
    echo "FAIL: snprintf usage found in test support files:"
    echo "$SNPRINTF_USAGE"
    echo "snprintf is forbidden; use project-approved path helpers."
    exit 1
fi
echo "   ✓ No snprintf in test support"

echo ""
echo "=== Check 22: internal/ios includes from Linux kernel tests ==="
LINUX_TEST_IOS=$(rg -n 'internal/ios' IXLandSystemLinuxKernelTests/*.m IXLandSystemLinuxKernelTests/*.c 2>/dev/null || true)
if [ -n "$LINUX_TEST_IOS" ]; then
    echo "FAIL: internal/ios includes from Linux kernel tests:"
    echo "$LINUX_TEST_IOS"
    exit 1
fi
echo "   ✓ No internal/ios includes from Linux kernel tests"

echo ""
echo "=== Check 23: Test ABI contamination - branded wrapper functions ==="
BRANDED_WRAPPERS=$(rg -n '\bi[x]land_test_[A-Za-z0-9_]*\b' IXLandSystemLinuxKernelTests IXLandSystemHostBridgeTests 2>/dev/null || true)
if [ -n "$BRANDED_WRAPPERS" ]; then
    echo "FAIL: Branded wrapper functions found in test files:"
    echo "$BRANDED_WRAPPERS"
    echo "Remove branded test-helper vocabulary; use Linux C contracts or HostBridge fixtures with plain host names."
    exit 1
fi
echo "   ✓ No branded test-helper vocabulary in tests"

echo ""
echo "=== Check 24: Test ABI contamination - Linux-named accessor soup ==="
LINUX_ACCESSORS=$(rg -n '^\s*int\s+linux_\w+\s*\(' IXLandSystemLinuxKernelTests/*.m IXLandSystemLinuxKernelTests/*.c IXLandSystemHostBridgeTests/*.m IXLandSystemHostBridgeTests/*.c 2>/dev/null || true)
if [ -n "$LINUX_ACCESSORS" ]; then
    echo "FAIL: Linux-named accessor soup found in Objective-C test files:"
    echo "$LINUX_ACCESSORS"
    echo "Remove fake accessor soup; use direct target-correct includes/calls."
    exit 1
fi
echo "   ✓ No Linux-named accessor soup in tests"

echo ""
echo "=== Check 25: Test ABI contamination - test-prefixed raw constants ==="
TEST_CONSTANTS=$(rg -n '^\s*#define\s+TES[T]_' IXLandSystemLinuxKernelTests/*.m IXLandSystemLinuxKernelTests/*.c IXLandSystemHostBridgeTests/*.m IXLandSystemHostBridgeTests/*.c 2>/dev/null || true)
if [ -n "$TEST_CONSTANTS" ]; then
    echo "FAIL: Test-prefixed raw constants found in test files:"
    echo "$TEST_CONSTANTS"
    echo "Remove fake test-prefixed constants; use semantic helpers or direct UAPI."
    exit 1
fi
echo "   ✓ No test-prefixed raw constants in tests"

echo ""
echo "=== Check 26: Bridge bag usage in Linux-facing tests ==="
BRIDGE_BAG_LINUX=$(rg -n 'backing_io\.h|backing_io_decls\.h' IXLandSystemLinuxKernelTests/*.m IXLandSystemLinuxKernelTests/*.c 2>/dev/null || true)
if [ -n "$BRIDGE_BAG_LINUX" ]; then
    echo "FAIL: Broad bridge bag headers found in Linux-facing tests:"
    echo "$BRIDGE_BAG_LINUX"
    echo "Use narrow subsystem-owned interfaces under internal/ios/** instead."
    exit 1
fi
echo "   ✓ No broad bridge bag usage in Linux-facing tests"

echo ""
echo "=== Check 27: Test gutted with 'omitted for brevity' ==="
GUTTED_TESTS=$(rg -n 'omitted for brevity\|Additional owner-only tests continue here' IXLandSystemLinuxKernelTests/*.m IXLandSystemLinuxKernelTests/*.c IXLandSystemHostBridgeTests/*.m IXLandSystemHostBridgeTests/*.c 2>/dev/null || true)
if [ -n "$GUTTED_TESTS" ]; then
    echo "FAIL: Gutted tests found:"
    echo "$GUTTED_TESTS"
    echo "Restore test coverage; do not leave stubs."
    exit 1
fi
echo "   ✓ No gutted tests"

echo ""
echo "=== Check 28: Signal/wait alias drift ==="
SIG_WAIT_ALIAS_DRIFT=$(rg -n '\b(I[X]_SIG|I[X]_W|TES[T]_SIG|TES[T]_W|linux[_]sig|linux[_]wait)[A-Za-z0-9_]*\b' fs kernel IXLandSystemLinuxKernelTests IXLandSystemHostBridgeTests 2>/dev/null || true)
if [ -n "$SIG_WAIT_ALIAS_DRIFT" ]; then
    echo "FAIL: Signal/wait alias drift found:"
    echo "$SIG_WAIT_ALIAS_DRIFT"
    exit 1
fi
ALL_CAPS_SIGNAL_ALIAS=$(rg -n '\bLINU[X]_SIG[A-Za-z0-9_]*\b' kernel/wait.c kernel/signal.c kernel/signal.h 2>/dev/null || true)
if [ -n "$ALL_CAPS_SIGNAL_ALIAS" ]; then
    echo "FAIL: All-caps Linux signal alias vocabulary found in signal/wait owner files:"
    echo "$ALL_CAPS_SIGNAL_ALIAS"
    exit 1
fi
TERMINAL_SIGNAL_ALIAS_DRIFT=$(rg -n '\bPT[Y]_SIG[A-Za-z0-9_]*\b' fs/pty.c 2>/dev/null || true)
if [ -n "$TERMINAL_SIGNAL_ALIAS_DRIFT" ]; then
    echo "FAIL: PTY signal alias vocabulary found in fs/pty.c:"
    echo "$TERMINAL_SIGNAL_ALIAS_DRIFT"
    exit 1
fi
echo "   ✓ No signal/wait alias drift"

echo ""
echo "=== Check 29: Test Linux constant alias drift ==="
TEST_LINUX_CONSTANT_ALIAS=$(rg -n '\b(I[X]_F|I[X]_AT|I[X]_TC|I[X]_TIOC|I[X]_SIG|I[X]_W|TES[T]_SIG|TES[T]_W)[A-Za-z0-9_]*\b|include/ixland/linux_(uapi|abi)_constants[.]h' IXLandSystemLinuxKernelTests IXLandSystemHostBridgeTests 2>/dev/null || true)
if [ -n "$TEST_LINUX_CONSTANT_ALIAS" ]; then
    echo "FAIL: Test Linux constant alias drift found:"
    echo "$TEST_LINUX_CONSTANT_ALIAS"
    exit 1
fi
echo "   ✓ No test Linux constant alias drift"

echo ""
echo "=== Check 30: LinuxKernel tests do not use HostBridge support ==="
LINUX_TEST_HOST_SUPPORT=$(rg -n 'HostTestSupport|IXLandSystemHostBridgeTests' IXLandSystemLinuxKernelTests 2>/dev/null || true)
if [ -n "$LINUX_TEST_HOST_SUPPORT" ]; then
    echo "FAIL: LinuxKernel tests reference HostBridge support:"
    echo "$LINUX_TEST_HOST_SUPPORT"
    exit 1
fi
echo "   ✓ LinuxKernel tests do not use HostBridge support"

echo ""
echo "=== Check 31: internal/ios errno mediation is not rewritten from negative returns ==="
BROKEN_HOST_ERRNO_ALL=$(rg -n 'errno[[:space:]]*=[[:space:]]*(\(int\)[[:space:]]*)?-ret' internal/ios/fs 2>/dev/null || true)
if [ -n "$BROKEN_HOST_ERRNO_ALL" ]; then
    echo "FAIL: Broken host errno rewriting found under internal/ios/fs:"
    echo "$BROKEN_HOST_ERRNO_ALL"
    exit 1
fi
echo "   ✓ No internal/ios/fs errno = -ret rewriting"

echo ""
echo "=== Check 32: XcodeGen Linux UAPI source segregation ==="
XCODEGEN_UAPI_DRIFT=$(python3 - <<'PY'
from pathlib import Path
import re
import sys

text = Path("project.yml").read_text()
lines = text.splitlines()
errors = []

targets = {}
current_target = None
for line in lines:
    m = re.match(r"^  ([A-Za-z0-9_-]+):$", line)
    if m:
        current_target = m.group(1)
        targets[current_target] = []
        continue
    if current_target:
        targets[current_target].append(line)

for target in ("IXLandSystemLinuxKernelTests", "IXLandSystemHostBridgeTests"):
    block = "\n".join(targets.get(target, []))
    settings = block.split("sources:", 1)[0]
    if "LINUX_UAPI_INCLUDE_ROOT" in settings:
        errors.append(f"{target} has global Linux UAPI include paths")

host_block = "\n".join(targets.get("IXLandSystemHostBridgeTests", []))
if "LINUX_UAPI_INCLUDE_ROOT" in host_block:
    errors.append("IXLandSystemHostBridgeTests references Linux UAPI include root")

source_entries = {}
current_path = None
for i, line in enumerate(lines):
    path = None
    m_obj = re.match(r"^\s*-\s+path:\s+(.+)$", line)
    m_plain = re.match(r"^\s*-\s+([^:\[][^#]*\.(?:c|m|h))\s*$", line)
    if m_obj:
        path = m_obj.group(1).strip()
    elif m_plain:
        path = m_plain.group(1).strip()
    if path:
        current_path = path
        source_entries.setdefault(path, {"flags": False})
        continue
    if current_path and "compilerFlags:" in line:
        source_entries[current_path]["flags"] = True

for path, entry in source_entries.items():
    if path.endswith(".m") and entry["flags"]:
        errors.append(f"{path} is Objective-C but has per-source compilerFlags")
    if path.startswith("IXLandSystemLinuxKernelTests/") and "internal/ios" in path:
        errors.append(f"LinuxKernel test source references internal/ios: {path}")

for source in list(Path("IXLandSystemLinuxKernelTests").glob("*.c")) + list(Path("IXLandSystemHostBridgeTests").glob("*.c")):
    body = source.read_text(errors="ignore")
    includes_uapi = re.search(r"^\s*#\s*include\s*<(linux|asm|asm-generic)/", body, re.M)
    if includes_uapi:
        entry = source_entries.get(str(source))
        if not entry or not entry["flags"]:
            errors.append(f"{source} includes Linux UAPI without per-source compilerFlags")

print("\n".join(errors))
PY
)
if [ -n "$XCODEGEN_UAPI_DRIFT" ]; then
    echo "FAIL: XcodeGen Linux UAPI segregation drift found:"
    echo "$XCODEGEN_UAPI_DRIFT"
    exit 1
fi
echo "   ✓ XcodeGen Linux UAPI source segregation holds"

echo ""
echo "=== Check 33: virtual pipe is not host-pipe owned ==="
HOST_PIPE_OWNER=$(rg -n '\bhost_pipe\b|\bdarwin_pipe\b|Darwin pipe' fs internal/ios 2>/dev/null || true)
if [ -n "$HOST_PIPE_OWNER" ]; then
    echo "FAIL: Host pipe ownership vocabulary found:"
    echo "$HOST_PIPE_OWNER"
    exit 1
fi
echo "   ✓ No host pipe ownership vocabulary"

echo ""
echo "=== Check 34: wait queue owner header is host-mediation clean ==="
WAIT_QUEUE_IOS_INCLUDE=$(rg -n 'internal/ios' kernel/wait_queue.h 2>/dev/null || true)
if [ -n "$WAIT_QUEUE_IOS_INCLUDE" ]; then
    echo "FAIL: kernel/wait_queue.h includes host mediation:"
    echo "$WAIT_QUEUE_IOS_INCLUDE"
    exit 1
fi
echo "   ✓ wait queue header does not expose host mediation"

echo ""
echo "=== Check 35: virtual readiness is not host event owned ==="
HOST_EVENT_OWNER=$(rg -n 'host_epoll|kqueue|dispatch_source|host_pipe|Darwin.*pipe' fs kernel internal/ios 2>/dev/null || true)
if [ -n "$HOST_EVENT_OWNER" ]; then
    echo "FAIL: Host event ownership vocabulary found:"
    echo "$HOST_EVENT_OWNER"
    exit 1
fi
echo "   ✓ No host event ownership vocabulary"

echo ""
echo "=== Check 36: Linux ABI supplement path mirrors UAPI layout ==="
BAD_LINUX_ABI_PATH=$(find third_party/linux-abi -path '*/generated/*' -print 2>/dev/null || true)
if [ -n "$BAD_LINUX_ABI_PATH" ]; then
    echo "FAIL: Linux ABI supplement must not use generated path segments:"
    echo "$BAD_LINUX_ABI_PATH"
    exit 1
fi
if [ -d third_party/linux-abi ] && [ ! -d third_party/linux-abi/6.12/arm64/include ]; then
    echo "FAIL: Linux ABI supplement must mirror third_party/linux-uapi/6.12/arm64/include"
    exit 1
fi
echo "   ✓ Linux ABI supplement path mirrors UAPI layout"

echo ""
echo "=== All checks passed ==="
