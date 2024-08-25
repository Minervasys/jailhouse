/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Jailhouse, a Linux-based partitioning hypervisor
 *
 * Configuration for RK3566 and RK3568 SoCs
 *
 * Copyright (c) 2024 Minerva Systems
 *
 * Authors:
 *  Alex Zuepke <alex.zuepke@minervasys.tech>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 *
 * Reservation via device tree: reg = <0x0 0x0a800000 0x0 0x05800000>
 *
 * This memory region is outside the regions reserved for and by the firmware.
 * The vendor BSP reserves the region 0x08400000..0x09400000 for the OPTEE,
 * with the DTB placed either before or after the OPTEE memory; check with
 * the "bdinfo", "bidram_dump" and "sysmem_dump" commands in U-Boot. Memory
 * from 0x0a800000 to 0x10000000 is a good default on most Rockchip boards.
 *
 * Of the reserved memory, we use the first 2 MB for the hypervisor
 * and the region from 0x0aa00000 on for SHMs and communication.
 * We place our virtual PCI bridge in the reserved I/O region at 0xf6c00000.
 *
 * The RK3568 doesn't provide an SMMU, therefore we cannot use cache coloring
 * for DMA memory, e.g. the memory of the root cell.
 *
 * The last used interrupt number in the TRM is 282 (SPI 250), so for Jailhouse
 * we use interrupts from 284 (SPI 252). Each vPCI controller uses 4 interrupts.
 * The root cell uses interrupts 284..287 (SPIs 252..255).
 * The first guest cell uses interrupts 288..291 (SPIs 256..259), and so on.
 * The GIC supports 352 interrupts (320 SPIs) overall.
 */

#include <jailhouse/types.h>
#include <jailhouse/cell-config.h>

struct {
	struct jailhouse_system header;
	__u64 cpus[1];
	struct jailhouse_memory mem_regions[4 + 4 + 1 + 6];
	struct jailhouse_irqchip irqchips[3];
	struct jailhouse_pci_device pci_devices[2];
} __attribute__((packed)) config = {
	.header = {
		.signature = JAILHOUSE_SYSTEM_SIGNATURE,
		.architecture = JAILHOUSE_ARM64,
		.revision = JAILHOUSE_CONFIG_REVISION,
		.flags = JAILHOUSE_SYS_VIRTUAL_DEBUG_CONSOLE,
		.hypervisor_memory = {
			.phys_start = 0x0a800000,
			.size = 0x00200000,
		},
		.debug_console = {
			/* uart2 */
			.address = 0xfe660000,
			.size = 0x00010000,
			.type = JAILHOUSE_CON_TYPE_8250,
			.flags = JAILHOUSE_CON_ACCESS_MMIO |
				 JAILHOUSE_CON_REGDIST_4,
		},
		.platform_info = {
			.pci_mmconfig_base = 0xf6c00000,
			.pci_mmconfig_end_bus = 0,
			.pci_is_virtual = 1,
			.pci_domain = 3,
			.arm = {
				.maintenance_irq = 25,
				.gic_version = 3,
				.gicd_base = 0xfd400000,
				.gicr_base = 0xfd460000,
			},
			.memguard = {
				.num_irqs = 32 + 320,
				.hv_timer = 26,
				.num_pmu_irq = 4,
				/* One PMU irq per CPU */
				.pmu_cpu_irq = {
					260, 261, 262, 263,
				},
			},
		},
		.root_cell = {
			.name = "rk3568",
			.num_pci_devices = ARRAY_SIZE(config.pci_devices),
			.cpu_set_size = sizeof(config.cpus),
			.num_memory_regions = ARRAY_SIZE(config.mem_regions),
			.num_irqchips = ARRAY_SIZE(config.irqchips),
			.vpci_irq_base = 284 - 32,
		},
	},

	.cpus = {
		0b1111,
	},

	.mem_regions = {
		/* 6 MB memory region from 0x0aa00000 to 0x0b000000 for communication */

		/* IVSHMEM shared memory regions for 00:00.0 (demo) */
		/* 4 regions for 2 peers */
		/* state table, read-only for all */ {
			.phys_start = 0x0aa00000,
			.virt_start = 0x0aa00000,
			.size = 0x10000,
			.flags = JAILHOUSE_MEM_READ,
		},
		/* shared region, read-write for all */ {
			.phys_start = 0x0aa10000,
			.virt_start = 0x0aa10000,
			.size = 0x10000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE,
		},
		/* peer 0 output region */ {
			.phys_start = 0x0aa20000,
			.virt_start = 0x0aa20000,
			.size = 0x10000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE,
		},
		/* peer 1 output region */ {
			.phys_start = 0x0aa30000,
			.virt_start = 0x0aa30000,
			.size = 0x10000,
			.flags = JAILHOUSE_MEM_READ,
		},

		/* IVSHMEM shared memory regions for 00:01.0 (networking) */
		JAILHOUSE_SHMEM_NET_REGIONS(0x0ab00000, 0), /* four regions, size 1MB */

		/* 80 MB memory region from 0x0b000000 to 0x10000000 for cells */ {
			.phys_start = 0x0b000000,
			.virt_start = 0x0b000000,
			.size = 0x05000000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE,
		},

		/* normal memory before the Jailhouse reserved memory region */ {
			.phys_start = 0x00000000,
			.virt_start = 0x00000000,
			.size = 0x0a800000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
			         JAILHOUSE_MEM_EXECUTE | JAILHOUSE_MEM_DMA,
		},
		/* normal memory after the Jailhouse reserved memory region */ {
			.phys_start = 0x10000000,
			.virt_start = 0x10000000,
			.size = 0xe0000000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
			         JAILHOUSE_MEM_EXECUTE | JAILHOUSE_MEM_DMA,
		},
		/* 256 MB I/O memory region, first part (PCI regions) */ {
			.phys_start = 0xf0000000,
			.virt_start = 0xf0000000,
			.size = 0x06c00000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
			         JAILHOUSE_MEM_IO | JAILHOUSE_MEM_DMA,
		},
		/* 256 MB I/O memory region, second part (rest) */ {
			.phys_start = 0xfc000000,
			.virt_start = 0xfc000000,
			.size = 0x04000000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
			         JAILHOUSE_MEM_IO | JAILHOUSE_MEM_DMA,
		},
		/* normal memory beyond 4 GB, at most 8 GB are supported */ {
			.phys_start = 0x100000000,
			.virt_start = 0x100000000,
			.size = 0x110000000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
			         JAILHOUSE_MEM_EXECUTE | JAILHOUSE_MEM_DMA,
		},
		/* PCIe memory region */ {
			.phys_start = 0x300000000,
			.virt_start = 0x300000000,
			.size = 0xc0c00000,
			.flags = JAILHOUSE_MEM_READ | JAILHOUSE_MEM_WRITE |
			         JAILHOUSE_MEM_IO | JAILHOUSE_MEM_DMA,
		},
	},

	.irqchips = {
		/* GIC */ {
			.address = 0xfd400000,
			.pin_base = 32,
			.pin_bitmap = {
				0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
			},
		},
		/* GIC */ {
			.address = 0xfd400000,
			.pin_base = 160,
			.pin_bitmap = {
				0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
			},
		},
		/* GIC */ {
			.address = 0xfd400000,
			.pin_base = 288,
			.pin_bitmap = {
				0xffffffff, 0xffffffff, 0, 0,
			},
		},
	},

	.pci_devices = {
		/* 00:00.0 (demo) */ {
			.type = JAILHOUSE_PCI_TYPE_IVSHMEM,
			.domain = 3,
			.bdf = 0 << 3,
			.bar_mask = JAILHOUSE_IVSHMEM_BAR_MASK_INTX,
			.shmem_regions_start = 0,
			.shmem_dev_id = 0,
			.shmem_peers = 2,
			.shmem_protocol = JAILHOUSE_SHMEM_PROTO_UNDEFINED,
		},
		/* 00:01.0 (networking) */ {
			.type = JAILHOUSE_PCI_TYPE_IVSHMEM,
			.domain = 3,
			.bdf = 1 << 3,
			.bar_mask = JAILHOUSE_IVSHMEM_BAR_MASK_INTX,
			.shmem_regions_start = 4,
			.shmem_dev_id = 0,
			.shmem_peers = 2,
			.shmem_protocol = JAILHOUSE_SHMEM_PROTO_VETH,
		},
	},
};
