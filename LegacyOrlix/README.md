# LegacyOrlix

This directory quarantines code and tests from the old local Orlix kernel prototype.

Contents here are migration reference only. They are not target implementation paths, product build inputs, milestone proof, or active XCTest/KUnit/kselftest ownership.

Useful behavior may be migrated only by ownership into upstream Linux, `OrlixKernel/Sources/ports/orlix/overlay/arch/orlix`, `OrlixKernel/Sources/ports/orlix/overlay/drivers/orlix`, boot code, `OrlixHostAdapter` seams, or `OrlixMLibC`. Behavior that upstream Linux owns should be deleted rather than reimplemented locally.

Do not add new work here except to preserve or annotate legacy reference material while migrating it out.
