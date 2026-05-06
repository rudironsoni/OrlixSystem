/*
 * IXLandSystemTests - CredentialTests.m
 *
 * Internal runtime semantic tests for credential subsystem.
 *
 * This is an INTERNAL RUNTIME SEMANTIC TEST.
 * It tests IXLandSystem private credential APIs via _impl() and do_* entry points.
 * It does NOT prove public Linux UAPI compatibility.
 */

#import <XCTest/XCTest.h>

/* Pull in private internal headers for credential subsystem testing */
#include "../kernel/cred_internal.h"
/* close_impl and other owner fd helpers are declared in fs/fdtable.h */
#include "fs/fdtable.h"

/* Declare internal _impl entry points for testing */
extern uid_t getuid_impl(void);
extern uid_t geteuid_impl(void);
extern gid_t getgid_impl(void);
extern gid_t getegid_impl(void);
extern int setuid_impl(uid_t uid);
extern int setgid_impl(gid_t gid);
extern int seteuid_impl(uid_t euid);
extern int setegid_impl(gid_t egid);
extern int setresuid_impl(uid_t ruid, uid_t euid, uid_t suid);
extern int setresgid_impl(gid_t rgid, gid_t egid, gid_t sgid);
extern int setreuid_impl(uid_t ruid, uid_t euid);
extern int setregid_impl(gid_t rgid, gid_t egid);
extern int getresuid_impl(uid_t *ruid, uid_t *euid, uid_t *suid);
extern int getresgid_impl(gid_t *rgid, gid_t *egid, gid_t *sgid);
extern uid_t setfsuid_impl(uid_t fsuid);
extern gid_t setfsgid_impl(gid_t fsgid);
extern int getgroups_impl(int size, gid_t list[]);
extern int setgroups_impl(int size, const gid_t *list);
extern void cred_reset_to_defaults(void);

@interface CredentialTests : XCTestCase
@end

@implementation CredentialTests

- (void)setUp {
    [super setUp];
    /* Ensure credential subsystem is initialized */
    cred_init();
    /* Reset credentials to IXLand defaults before each test */
    cred_reset_to_defaults();
    /* Clean up any lingering file descriptors using owner close_impl */
    for (int fd = 3; fd < 256; fd++) {
        close_impl(fd);
    }
}

- (void)tearDown {
    /* Clean up any open file descriptors using owner close_impl */
    for (int fd = 3; fd < 256; fd++) {
        close_impl(fd);
    }
    cred_reset_to_defaults();
    [super tearDown];
}

/* ============================================================================
 * BASIC VIRTUAL CREDENTIAL STATE TESTS
 * ============================================================================ */

- (void)testGetuidImplReturnsVirtualUid {
    /* After initialization, getuid() should return virtual IXLand UID */
    uint32_t uid = (uint32_t)getuid_impl();

    /* IXLand default is virtual root (0), NOT host mobile user (501) */
    XCTAssertEqual(uid, 0u, @"getuid_impl should return virtual IXLand UID (0), not 501");
}

- (void)testGeteuidImplReturnsVirtualEuid {
    /* Effective UID should match real UID by default */
    uint32_t euid = (uint32_t)geteuid_impl();

    XCTAssertEqual(euid, 0u, @"geteuid_impl should return virtual IXLand EUID (0)");
}

- (void)testGetgidImplReturnsVirtualGid {
    /* Same test for GID */
    uint32_t gid = (uint32_t)getgid_impl();

    XCTAssertEqual(gid, 0u, @"getgid_impl should return virtual IXLand GID (0), not 501");
}

- (void)testGetegidImplReturnsVirtualEgid {
    /* Effective GID */
    uint32_t egid = (uint32_t)getegid_impl();

    XCTAssertEqual(egid, 0u, @"getegid_impl should return virtual IXLand EGID (0)");
}

/* ============================================================================
 * VIRTUAL SETUID/SETGID SEMANTICS TESTS
 * ============================================================================ */

- (void)testSetuidImplVirtualRootCanSetuid {
    /* Virtual root (euid=0) should be able to setuid */
    int result = setuid_impl(1000);

    XCTAssertEqual(result, 0, @"setuid_impl should succeed for virtual root");

    uid_t uid = getuid_impl();
    uid_t euid = geteuid_impl();

    XCTAssertEqual(uid, 1000, @"UID should be set to 1000");
    XCTAssertEqual(euid, 1000, @"EUID should be set to 1000");
}

- (void)testSetgidImplVirtualRootCanSetgid {
    int result = setgid_impl(1000);

    XCTAssertEqual(result, 0, @"setgid_impl should succeed for virtual root");

    gid_t gid = getgid_impl();
    gid_t egid = getegid_impl();

    XCTAssertEqual(gid, 1000, @"GID should be set to 1000");
    XCTAssertEqual(egid, 1000, @"EGID should be set to 1000");
}

- (void)testSetuidNonRootCannotSetuidToArbitraryUid {
    /* Set up: change to a non-root user */
    int result = setuid_impl(1000);
    XCTAssertEqual(result, 0, @"setuid_impl should succeed for root");

    /* Now try to setuid to another arbitrary UID (not root) */
    result = setuid_impl(2000);

    /* Should fail with EPERM - non-root cannot setuid to arbitrary UIDs */
    XCTAssertEqual(result, -1, @"setuid_impl should fail for non-root setting arbitrary UID");
    XCTAssertEqual(errno, EPERM, @"errno should be EPERM");
}

- (void)testSetuidNonRootCanRevertToOwnUid {
    /* Set up: become UID 1000 */
    int result = setuid_impl(1000);
    XCTAssertEqual(result, 0, @"setuid_impl should succeed for root");

    /* Non-root user should be able to setuid to own UID */
    result = setuid_impl(1000);

    XCTAssertEqual(result, 0, @"setuid_impl should succeed for reverting to own UID");
}

- (void)testSeteuidImplVirtualRootChangesEffectiveUidOnly {
    XCTAssertEqual(setresuid_impl(1000, 0, 2000), 0, @"root should establish saved uid state");

    XCTAssertEqual(seteuid_impl(2000), 0, @"seteuid should select saved uid");
    XCTAssertEqual(getuid_impl(), 1000u, @"real uid should remain unchanged");
    XCTAssertEqual(geteuid_impl(), 2000u, @"effective uid should change");
}

- (void)testSetegidImplVirtualRootChangesEffectiveGidOnly {
    XCTAssertEqual(setresgid_impl(1000, 0, 2000), 0, @"root should establish saved gid state");

    XCTAssertEqual(setegid_impl(2000), 0, @"setegid should select saved gid");
    XCTAssertEqual(getgid_impl(), 1000u, @"real gid should remain unchanged");
    XCTAssertEqual(getegid_impl(), 2000u, @"effective gid should change");
}

- (void)testSetresuidImplUpdatesRealEffectiveAndSavedUid {
    XCTAssertEqual(setresuid_impl(1000, 1001, 1002), 0, @"root should set all uid slots");

    XCTAssertEqual(getuid_impl(), 1000u, @"real uid should update");
    XCTAssertEqual(geteuid_impl(), 1001u, @"effective uid should update");
}

- (void)testSetresgidImplUpdatesRealEffectiveAndSavedGid {
    XCTAssertEqual(setresgid_impl(2000, 2001, 2002), 0, @"root should set all gid slots");

    XCTAssertEqual(getgid_impl(), 2000u, @"real gid should update");
    XCTAssertEqual(getegid_impl(), 2001u, @"effective gid should update");
}

- (void)testGetresuidImplReportsRealEffectiveAndSavedUid {
    uid_t ruid = 0;
    uid_t euid = 0;
    uid_t suid = 0;

    XCTAssertEqual(setresuid_impl(1000, 1001, 1002), 0, @"root should set all uid slots");
    XCTAssertEqual(getresuid_impl(&ruid, &euid, &suid), 0, @"getresuid should copy all uid slots");
    XCTAssertEqual(ruid, 1000u);
    XCTAssertEqual(euid, 1001u);
    XCTAssertEqual(suid, 1002u);
}

- (void)testGetresgidImplReportsRealEffectiveAndSavedGid {
    gid_t rgid = 0;
    gid_t egid = 0;
    gid_t sgid = 0;

    XCTAssertEqual(setresgid_impl(2000, 2001, 2002), 0, @"root should set all gid slots");
    XCTAssertEqual(getresgid_impl(&rgid, &egid, &sgid), 0, @"getresgid should copy all gid slots");
    XCTAssertEqual(rgid, 2000u);
    XCTAssertEqual(egid, 2001u);
    XCTAssertEqual(sgid, 2002u);
}

- (void)testSetreuidAndSetregidImplUpdateRealAndEffectiveIds {
    XCTAssertEqual(setregid_impl(4000, 4001), 0, @"root should set real and effective gid");
    XCTAssertEqual(setreuid_impl(3000, 3001), 0, @"root should set real and effective uid");
    XCTAssertEqual(getuid_impl(), 3000u);
    XCTAssertEqual(geteuid_impl(), 3001u);
    XCTAssertEqual(getgid_impl(), 4000u);
    XCTAssertEqual(getegid_impl(), 4001u);
}

- (void)testSetfsuidAndSetfsgidReturnPreviousIds {
    XCTAssertEqual(setfsuid_impl(5000), 0u, @"setfsuid should return previous fsuid");
    XCTAssertEqual(setfsgid_impl(6000), 0u, @"setfsgid should return previous fsgid");
    XCTAssertEqual(setfsuid_impl(5001), 5000u, @"setfsuid should return previous fsuid");
    XCTAssertEqual(setfsgid_impl(6001), 6000u, @"setfsgid should return previous fsgid");
}

/* ============================================================================
 * VIRTUAL CREDENTIAL PERSISTENCE TESTS
 * ============================================================================ */

- (void)testCredentialStatePersistsAcrossCalls {
    /* Set specific credentials */
    setgid_impl(24);
    setuid_impl(42);

    /* Verify persistence */
    XCTAssertEqual(getuid_impl(), 42, @"UID should persist");
    XCTAssertEqual(getgid_impl(), 24, @"GID should persist");
}

- (void)testSetgroupsImplVirtualRootSetsSupplementaryGroups {
    gid_t groups[2] = {3000, 3001};
    gid_t observed[2] = {0, 0};

    XCTAssertEqual(setgroups_impl(2, groups), 0, @"virtual root should set supplementary groups");
    XCTAssertEqual(getgroups_impl(0, NULL), 2, @"getgroups size query should return group count");
    XCTAssertEqual(getgroups_impl(2, observed), 2, @"getgroups should copy supplementary groups");
    XCTAssertEqual(observed[0], 3000u, @"first supplementary group should match");
    XCTAssertEqual(observed[1], 3001u, @"second supplementary group should match");
}

- (void)testGetgroupsImplRejectsSmallBuffer {
    gid_t groups[2] = {3000, 3001};
    gid_t observed[1] = {0};

    XCTAssertEqual(setgroups_impl(2, groups), 0, @"virtual root should set supplementary groups");
    errno = 0;
    XCTAssertEqual(getgroups_impl(1, observed), -1, @"getgroups should reject undersized buffers");
    XCTAssertEqual(errno, EINVAL, @"getgroups undersized buffer should set EINVAL");
}

- (void)testSetgroupsImplNonRootFails {
    gid_t groups[1] = {3000};

    XCTAssertEqual(setuid_impl(1000), 0, @"root should first become non-root");
    errno = 0;
    XCTAssertEqual(setgroups_impl(1, groups), -1, @"non-root should not set supplementary groups");
    XCTAssertEqual(errno, EPERM, @"non-root setgroups should set EPERM");
}

/* ============================================================================
 * VIRTUAL CREDENTIAL ALLOCATION TESTS
 * ============================================================================ */

- (void)testAllocCredWithDefaults {
    struct cred *cred = alloc_cred();
    XCTAssertTrue(cred != NULL, @"alloc_cred should succeed");
    
    /* Skip assertions if allocation failed to avoid crash */
    if (cred == NULL) {
        return;
    }

    /* Should have IXLand defaults, not host identity */
    XCTAssertEqual(cred->uid, 0u, @"Default cred UID should be 0 (virtual root)");
    XCTAssertEqual(cred->gid, 0u, @"Default cred GID should be 0 (virtual root)");
    XCTAssertEqual(cred->euid, 0u, @"Default cred EUID should be 0 (virtual root)");
    XCTAssertEqual(cred->egid, 0u, @"Default cred EGID should be 0 (virtual root)");
    XCTAssertEqual(cred->suid, 0u, @"Default cred SUID should be 0");
    XCTAssertEqual(cred->sgid, 0u, @"Default cred SGID should be 0");
    XCTAssertEqual(cred->fsuid, 0u, @"Default cred FSUID should be 0");
    XCTAssertEqual(cred->fsgid, 0u, @"Default cred FSGID should be 0");
    XCTAssertEqual(cred->group_count, (size_t)0, @"Default cred should have no supplementary groups");
    XCTAssertFalse(cred->no_new_privs, @"Default cred should allow exec privilege transitions");
    XCTAssertEqual(cred->securebits, 0u, @"Default cred should have no securebits set");
    XCTAssertNotEqual(cred->cap_permitted, 0ULL, @"Default root cred should have virtual permitted capabilities");
    XCTAssertNotEqual(cred->cap_effective, 0ULL, @"Default root cred should have virtual effective capabilities");
    XCTAssertNotEqual(cred->cap_bounding, 0ULL, @"Default root cred should have a virtual bounding set");

    free_cred(cred);
}

- (void)testCredReferenceCounting {
    struct cred *cred = alloc_cred();
    XCTAssertEqual(cred->refs, 1, @"Initial refcount should be 1");

    get_cred(cred);
    XCTAssertEqual(cred->refs, 2, @"After get_cred, refcount should be 2");

    put_cred(cred);
    XCTAssertEqual(cred->refs, 1, @"After put_cred, refcount should be 1");

    put_cred(cred); /* Should not crash, might free */
}

/* ============================================================================
 * VIRTUAL SAVED UID/GID TESTS
 * ============================================================================ */

- (void)testRootSetuidUpdatesSavedUid {
    uid_t ruid = 0;
    uid_t euid = 0;
    uid_t suid = 0;

    XCTAssertEqual(setresuid_impl(1000, 0, 2000), 0, @"root should establish real and saved uid state");
    XCTAssertEqual(getresuid_impl(&ruid, &euid, &suid), 0, @"getresuid should report established uid state");
    XCTAssertEqual(ruid, 1000u, @"real uid should be set");
    XCTAssertEqual(euid, 0u, @"effective uid should remain root");
    XCTAssertEqual(suid, 2000u, @"saved uid should be set");

    XCTAssertEqual(seteuid_impl(1000), 0, @"seteuid should drop effective uid to the real uid");
    XCTAssertEqual(getresuid_impl(&ruid, &euid, &suid), 0, @"getresuid should report dropped effective uid");
    XCTAssertEqual(ruid, 1000u);
    XCTAssertEqual(euid, 1000u);
    XCTAssertEqual(suid, 2000u, @"saved uid should survive effective uid changes");

    XCTAssertEqual(setuid_impl(2000), 0, @"setuid should restore effective uid from the saved uid");
    XCTAssertEqual(getresuid_impl(&ruid, &euid, &suid), 0, @"getresuid should report restored saved uid");
    XCTAssertEqual(ruid, 1000u, @"real uid should remain unchanged when restoring saved uid");
    XCTAssertEqual(euid, 2000u, @"effective uid should be restored from saved uid");
    XCTAssertEqual(suid, 2000u, @"saved uid should remain available after restoration");
}

@end
