// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>
#include <asm/boot.h>

static void orlix_boot_handoff_records_params(struct kunit *test)
{
	struct boot_params params = {
		.cmdline = "console=ttyS0 root=/dev/vda rootfstype=ext4 ro orlix.profile=appstore",
		.memory_base = 0x100000,
		.memory_size = 0x4000000,
		.dtb_base = "dtb",
		.dtb_size = 3,
		.root_device = "/dev/vda",
		.console_device = "ttyS0",
	};

	arch_boot_reset_handoff();
	KUNIT_EXPECT_EQ(test, 0, arch_boot_handoff_count());
	KUNIT_EXPECT_PTR_EQ(test, NULL, arch_boot_last_params());

	KUNIT_EXPECT_EQ(test, ORLIX_ARCH_BOOT_OK,
			arch_boot_prepare_entry(&params));

	KUNIT_EXPECT_EQ(test, 1, arch_boot_handoff_count());
	KUNIT_EXPECT_PTR_EQ(test, &params, arch_boot_last_params());
	KUNIT_EXPECT_PTR_EQ(test, &params, arch_boot_params());
}

static void orlix_boot_handoff_rejects_missing_dtb(struct kunit *test)
{
	struct boot_params params = {
		.cmdline = "console=ttyS0 root=/dev/vda rootfstype=ext4 ro orlix.profile=appstore",
		.root_device = "/dev/vda",
		.console_device = "ttyS0",
	};

	arch_boot_reset_handoff();

	KUNIT_EXPECT_EQ(test, ORLIX_ARCH_BOOT_INVALID_CONFIG,
			arch_boot_prepare_entry(&params));
	KUNIT_EXPECT_EQ(test, 0, arch_boot_handoff_count());
	KUNIT_EXPECT_PTR_EQ(test, NULL, arch_boot_last_params());
}

static struct kunit_case orlix_boot_test_cases[] = {
	KUNIT_CASE(orlix_boot_handoff_records_params),
	KUNIT_CASE(orlix_boot_handoff_rejects_missing_dtb),
	{}
};

static struct kunit_suite orlix_boot_test_suite = {
	.name = "orlix-boot",
	.test_cases = orlix_boot_test_cases,
};

kunit_test_suite(orlix_boot_test_suite);

MODULE_LICENSE("GPL");
