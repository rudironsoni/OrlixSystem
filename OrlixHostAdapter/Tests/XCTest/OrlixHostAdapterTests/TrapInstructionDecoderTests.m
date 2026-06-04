#import <XCTest/XCTest.h>

#include "OrlixHostAdapter/runtime/trap_decode.h"

@interface TrapInstructionDecoderTests : XCTestCase
@end

@implementation TrapInstructionDecoderTests

- (void)testFindsTpidrBaseAcrossMlibcThreadKeyWindow
{
    uint32_t instructions[15] = {0};
    unsigned int baseRegister = 0;

    instructions[0] = 0xd53bd056U;  /* mrs x22, TPIDR_EL0 */
    instructions[14] = 0xf85f02c9U; /* ldur x9, [x22, #-0x10] */

    XCTAssertTrue(orlix_host_user_trap_memory_base_register(instructions[14],
                                                            &baseRegister));
    XCTAssertEqual(baseRegister, 22U);
    XCTAssertTrue(orlix_host_user_trap_recent_tpidr_write_matches_base(
        instructions,
        sizeof(instructions) / sizeof(instructions[0]),
        14,
        baseRegister));
}

- (void)testRebasesMlibcTcbPointerDerivedFromHostTls
{
    unsigned long correctedBase = 0;
    unsigned long hostTls = 0x1005;
    unsigned long activeUserTls = 0x6000002bff80;
    unsigned long badTcbBase = hostTls - 0x78;

    XCTAssertTrue(orlix_host_user_trap_rebase_register_from_host_tls(hostTls,
                                                                     activeUserTls,
                                                                     badTcbBase,
                                                                     0x600000000000,
                                                                     0x700000000000,
                                                                     &correctedBase));
    XCTAssertEqual(correctedBase, activeUserTls - 0x78);
}

- (void)testRebasesMlibcTcbPointerWithLiveSignalHostTls
{
    unsigned long correctedBase = 0;
    unsigned long installedHostTls = 0x1007;
    unsigned long liveSignalHostTls = 0x1005;
    unsigned long activeUserTls = 0x6000002bff80;
    unsigned long badTcbBase = liveSignalHostTls - 0x78;
    unsigned long hostTls = orlix_host_user_trap_host_tls_reference(
        installedHostTls,
        liveSignalHostTls,
        0x600000000000,
        0x700000000000);

    XCTAssertEqual(hostTls, liveSignalHostTls);
    XCTAssertTrue(orlix_host_user_trap_rebase_register_from_host_tls(hostTls,
                                                                     activeUserTls,
                                                                     badTcbBase,
                                                                     0x600000000000,
                                                                     0x700000000000,
                                                                     &correctedBase));
    XCTAssertEqual(correctedBase, activeUserTls - 0x78);
}

- (void)testTpidrSearchStopsWhenBaseRegisterIsRewritten
{
    uint32_t instructions[3] = {0};

    instructions[0] = 0xd53bd048U; /* mrs x8, TPIDR_EL0 */
    instructions[1] = 0xd101e108U; /* sub x8, x8, #0x78 */
    instructions[2] = 0xb9401909U; /* ldr w9, [x8, #0x18] */

    XCTAssertFalse(orlix_host_user_trap_recent_tpidr_write_matches_base(
        instructions,
        sizeof(instructions) / sizeof(instructions[0]),
        2,
        8));
}

- (void)testRejectsMisalignedHostedUserTls
{
    XCTAssertTrue(orlix_host_user_trap_valid_user_tls(0x600000000000,
                                                      0x700000000000,
                                                      0x6000002bff80));
    XCTAssertFalse(orlix_host_user_trap_valid_user_tls(0x600000000000,
                                                       0x700000000000,
                                                       0x6000002bff7e));
}

- (void)testRejectsTlsRepairWithoutActiveUserTls
{
    XCTAssertFalse(orlix_host_user_trap_can_repair_user_tls(0x600000000000,
                                                            0x700000000000,
                                                            0));
    XCTAssertFalse(orlix_host_user_trap_can_repair_user_tls(0x600000000000,
                                                            0x700000000000,
                                                            0x1005));
    XCTAssertTrue(orlix_host_user_trap_can_repair_user_tls(0x600000000000,
                                                           0x700000000000,
                                                           0x6000002bff80));
}

@end
