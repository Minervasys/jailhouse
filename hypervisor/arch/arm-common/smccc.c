/*
 * Jailhouse, a Linux-based partitioning hypervisor
 *
 * Copyright (C) 2018 Texas Instruments Incorporated - http://www.ti.com/
 *
 * Authors:
 *  Lokesh Vutla <lokeshvutla@ti.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include <jailhouse/control.h>
#include <jailhouse/printk.h>
#include <asm/psci.h>
#include <asm/traps.h>
#include <asm/smc.h>
#include <asm/smccc.h>

bool sdei_available;

static bool sdei_probed __attribute__((__unused__));

int smccc_discover(void)
{
	struct per_cpu *cpu_data = this_cpu_data();
	long ret;

	cpu_data->smccc_feat_workaround_1 = ARM_SMCCC_NOT_SUPPORTED;
	cpu_data->smccc_feat_workaround_2 = ARM_SMCCC_NOT_SUPPORTED;

#if defined(CONFIG_MACH_NXP_S32v2)
	/* FIXME: add proper support for SMC and PSCI into u-boot and
	 * allow this code to run properly.
	 */
	sdei_available = false;
	return 0;
#endif

	ret = smc(PSCI_0_2_FN_VERSION);

	/* We need >=PSCIv1.0 for SMCCC. Against the spec, U-Boot may also
	 * return a negative error code. */
	if (ret < 0 || PSCI_VERSION_MAJOR(ret) < 1)
		return sdei_available ? trace_error(-EIO) : 0;

	/* Check if PSCI supports SMCCC version call */
	ret = smc_arg1(PSCI_1_0_FN_FEATURES, SMCCC_VERSION);
	if (ret != ARM_SMCCC_SUCCESS)
		return sdei_available ? trace_error(-EIO) : 0;

#ifdef CONFIG_MACH_RK3588
	sdei_available = false;
#else
#ifdef __aarch64__
	/* Check if we have SDEI (ARMv8 only) */
	ret = smc(SDEI_VERSION);
	if (ret >= ARM_SMCCC_VERSION_1_0) {
		if (sdei_probed && !sdei_available)
			return trace_error(-EIO);
		sdei_available = true;
	}
	sdei_probed = true;
#endif
#endif

	/* We need to have at least SMCCC v1.1 */
	ret = smc(SMCCC_VERSION);
	if (ret < ARM_SMCCC_VERSION_1_1)
		return 0;

	/* check if SMCCC_ARCH_FEATURES is actually available */
	ret = smc_arg1(SMCCC_ARCH_FEATURES, SMCCC_ARCH_FEATURES);
	if (ret != ARM_SMCCC_SUCCESS)
		return 0;

	cpu_data->smccc_feat_workaround_1 =
		smc_arg1(SMCCC_ARCH_FEATURES, SMCCC_ARCH_WORKAROUND_1);

	cpu_data->smccc_feat_workaround_2 =
		smc_arg1(SMCCC_ARCH_FEATURES, SMCCC_ARCH_WORKAROUND_2);

	return 0;
}

static inline long handle_arch_features(u32 id)
{
	switch (id) {
	case SMCCC_ARCH_FEATURES:
		return ARM_SMCCC_SUCCESS;

	case SMCCC_ARCH_WORKAROUND_1:
		return this_cpu_data()->smccc_feat_workaround_1;
	case SMCCC_ARCH_WORKAROUND_2:
		return this_cpu_data()->smccc_feat_workaround_2;

	default:
		return ARM_SMCCC_NOT_SUPPORTED;
	}
}

static enum trap_return handle_arch(struct trap_context *ctx)
{
	u32 function_id = ctx->regs[0];
	unsigned long *ret = &ctx->regs[0];

	switch (function_id) {
	case SMCCC_VERSION:
		*ret = ARM_SMCCC_VERSION_1_1;
		break;

	case SMCCC_ARCH_FEATURES:
		*ret = handle_arch_features(ctx->regs[1]);
		break;

	case SMCCC_ARCH_WORKAROUND_2:
		if (this_cpu_data()->smccc_feat_workaround_2 < 0)
			return ARM_SMCCC_NOT_SUPPORTED;
		return smc_arg1(SMCCC_ARCH_WORKAROUND_2, ctx->regs[1]);

	default:
		panic_printk("Unhandled SMC arch trap %lx\n", *ret);
		return TRAP_UNHANDLED;
	}

	return TRAP_HANDLED;
}

enum trap_return handle_smc(struct trap_context *ctx)
{
	unsigned long *regs = ctx->regs;
	enum trap_return ret = TRAP_HANDLED;
	u32 *stats = this_cpu_public()->stats;

	switch (SMCCC_GET_OWNER(regs[0])) {
	case ARM_SMCCC_OWNER_ARCH:
		stats[JAILHOUSE_CPU_STAT_VMEXITS_SMCCC]++;
		ret = handle_arch(ctx);
		break;

	case ARM_SMCCC_OWNER_SIP:
		stats[JAILHOUSE_CPU_STAT_VMEXITS_SMCCC]++;
#if defined(__aarch64__) && defined(CONFIG_MACH_ZYNQMP_ZCU102)
		regs[0] = smc_arg4(regs[0], regs[1], regs[2], regs[3], regs[4]);
#else
		if (this_cell() == &root_cell)
			/*
			 *  S32G3 Hack: Passthru, we need it for SMIC.
			 *  Requires further investigation!
			 */
			regs[0] = smc_arg7(regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], regs[6], regs[7]);
		else
			regs[0] = ARM_SMCCC_NOT_SUPPORTED;
#endif
		break;

	case ARM_SMCCC_OWNER_STANDARD:
		regs[0] = psci_dispatch(ctx);
		break;

	default:
		ret = TRAP_UNHANDLED;
	}

	arch_skip_instruction(ctx);

	return ret;
}
