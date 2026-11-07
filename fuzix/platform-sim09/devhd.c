/* SPDX-License-Identifier: GPL-2.0-only */
#include <kernel.h>
#include <kdata.h>
#include <printf.h>
#include <stdbool.h>
#include <tinydisk.h>
#include <device.h>

/*
 *	The raw disks of the simulator (-0 and -1 image files).  A DMA style
 *	command port at $E040: drive ($E041), 24 bit sector number ($E042-44,
 *	256 byte sectors), memory address ($E045-46), command ($E040: $81 read,
 *	$55 write); the result is left in $E040 (0 = good).  A Fuzix block is two
 *	sectors.
 *
 *	After the start-up the commands are asynchronous ($E049 = $81): the command
 *	returns at once ($E040 reads $FE until it is finished) and the CPU waits for the
 *	interrupt at the end of the command with the interrupts on (plt_idle: SYNC), so
 *	the clock and the console go on meanwhile.  The kernel does not sleep in a block
 *	driver (this Fuzix has no locking for that, and here the memory of the process is
 *	swapped to the disk to switch it), so no other process runs during a command.
 *	With the interrupts off (fork, the swapper: the interrupt stack is borrowed) the
 *	CPU polls.  A board without the asynchronous mode has the result at once.
 */
#define DISK_CMD	((volatile uint8_t *)0xE040)
#define DISK_DRIVE	((volatile uint8_t *)0xE041)
#define DISK_LSN	((volatile uint8_t *)0xE042)
#define DISK_ADDR	((volatile uint8_t *)0xE045)
#define DISK_CTL	((volatile uint8_t *)0xE049)

static uint8_t hd_async;	/* set when the start-up is done */

/* gcc6809 turns "store; read back; compare" into a test of the stored value, so the
   status is read in a function of its own */
static uint8_t __attribute__((noinline)) disk_status(void)
{
	return DISK_CMD[0];
}

/* (these two are in .common, at the top of the kernel data: the kernel text below $6C00, where the user space starts, is full) */
void __attribute__((section(".common"))) devhd_irq(void)
{
	if ((DISK_CTL[0] & 0xC0) == 0xC0)	/* a command has finished and the interrupt is on */
		DISK_CTL[0] = 0x01;		/* asynchronous, the interrupt off: the waiting CPU takes the result */
}

static int __attribute__((section(".common"))) sim_sector(uint8_t unit, bool is_read, uint32_t lsn, uint8_t *dptr)
{
	DISK_DRIVE[0] = unit;
	DISK_LSN[0] = (uint8_t)(lsn >> 16);
	DISK_LSN[1] = (uint8_t)(lsn >> 8);
	DISK_LSN[2] = (uint8_t)lsn;
	DISK_ADDR[0] = (uint8_t)((uint16_t)dptr >> 8);
	DISK_ADDR[1] = (uint8_t)(uint16_t)dptr;
	if (hd_async) {
		uint8_t st;
		irqflags_t irq = di();

		DISK_CTL[0] = 0x81;			/* asynchronous, interrupt at the end */
		DISK_CMD[0] = is_read ? 0x81 : 0x55;
		while ((st = disk_status()) == 0xFE) {
			if ((irq & 0x10) || inswap)
				continue;		/* the interrupts must stay off: poll */
			plt_idle();			/* wait for an interrupt, with the interrupts on */
			di();
		}
		irqrestore(irq);
		return st == 0;
	}
	DISK_CMD[0] = is_read ? 0x81 : 0x55;
	return disk_status() == 0;
}

static int sim_xfer(uint_fast8_t unit, bool is_read, uint32_t block, uint8_t *dptr)
{
	uint32_t lsn = block << 1;
	if (!sim_sector(unit, is_read, lsn, dptr))
		return 0;
	return sim_sector(unit, is_read, lsn + 1, dptr + 256);
}

__attribute__((section(".discard")))
void devhd_init(void)
{
	/* Both disks, with a partition table; a drive without an image is skipped quietly.
	   Page 2 (0200-02FF) is free at this point: it is the scratch sector of the probe. */
	uint8_t unit;
	for (unit = 0; unit < 2; unit++)
		if (sim_sector(unit, true, 0, (uint8_t *)0x0200))
			td_register(unit, sim_xfer, td_ioctl_none, 1);
	hd_async = 1;
}
