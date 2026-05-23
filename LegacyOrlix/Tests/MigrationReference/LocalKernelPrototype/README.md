# Local Kernel Prototype Migration Reference

This tree quarantines the old local-prototype tests that previously lived in the top-level test target.

These files are not target architecture proof and must not be added to Xcode proof schemes. Assertions from this tree may be deleted, migrated to Linux KUnit, migrated to Linux kselftest, or moved into narrow host-adapter XCTest only after ownership is reviewed.

Current classification:

- `kunit/**`: keep temporarily as migration reference; migrate useful kernel-internal assertions to Linux KUnit under `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix/**/<owner>_test.c` or `OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix/**/<owner>_test.c`.
- `fs/**`: keep temporarily as migration reference; migrate Linux-visible filesystem behavior to Linux kselftest or delete where upstream Linux already owns it.
- `kernel/**`: keep temporarily as migration reference; migrate Linux-visible task, futex, signal, wait, and process behavior to Linux kselftest, or kernel-internal Orlix-only helpers to KUnit if such helpers remain.
- `*Contract.c`, `*Contract.h`, and `*Tests.m`: keep temporarily as migration reference; migrate Linux-visible behavior to Linux kselftest, delete upstream-owned duplicate contracts, and move only narrow iOS host mechanics into `OrlixHostAdapter/Tests/XCTest/OrlixHostAdapterTests/`.
- `LinuxUAPICompileSmoke.c`, `FutexUAPICompileSmoke.c`, and `KernelOwnerCompatCompileSmoke.c`: keep temporarily as migration reference; future UAPI checks belong to installed Linux `headers_install` output and OrlixMLibC proof lanes, not local-prototype XCTest.
