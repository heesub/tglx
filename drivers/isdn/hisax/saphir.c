/* $Id: saphir.c,v 1.8.6.2 2001/09/23 22:24:51 kai Exp $
 *
 * low level stuff for HST Saphir 1
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Thanks to    HST High Soft Tech GmbH
 *
 */

#include <linux/init.h>
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"

extern const char *CardType[];
static char *saphir_rev = "$Revision: 1.8.6.2 $";
static spinlock_t saphir_lock = SPIN_LOCK_UNLOCKED;

#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

#define ISAC_DATA	0
#define HSCX_DATA	1
#define ADDRESS_REG	2
#define IRQ_REG		3
#define SPARE_REG	4
#define RESET_REG	5

static inline u8
readreg(struct IsdnCardState *cs, unsigned int adr, u8 off)
{
	u8 ret;
	unsigned long flags;

	spin_lock_irqsave(&saphir_lock, flags);
	byteout(cs->hw.saphir.ale, off);
	ret = bytein(adr);
	spin_unlock_irqrestore(&saphir_lock, flags);
	return ret;
}

static inline void
writereg(struct IsdnCardState *cs, unsigned int adr, u8 off, u8 data)
{
	unsigned long flags;

	spin_lock_irqsave(&saphir_lock, flags);
	byteout(cs->hw.saphir.ale, off);
	byteout(adr, data);
	spin_unlock_irqrestore(&saphir_lock, flags);
}

static inline void
readfifo(struct IsdnCardState *cs, unsigned int adr, u8 off, u8 *data, int size)
{
	byteout(cs->hw.saphir.ale, off);
	insb(adr, data, size);
}

static inline void
writefifo(struct IsdnCardState *cs, unsigned int adr, u8 off, u8 *data, int size)
{
	byteout(cs->hw.saphir.ale, off);
	outsb(adr, data, size);
}

static u8
isac_read(struct IsdnCardState *cs, u8 offset)
{
	return readreg(cs, cs->hw.saphir.isac, offset);
}

static void
isac_write(struct IsdnCardState *cs, u8 offset, u8 value)
{
	writereg(cs, cs->hw.saphir.isac, offset, value);
}

static void
isac_read_fifo(struct IsdnCardState *cs, u8 * data, int size)
{
	readfifo(cs, cs->hw.saphir.isac, 0, data, size);
}

static void
isac_write_fifo(struct IsdnCardState *cs, u8 * data, int size)
{
	writefifo(cs, cs->hw.saphir.isac, 0, data, size);
}

static struct dc_hw_ops isac_ops = {
	.read_reg   = isac_read,
	.write_reg  = isac_write,
	.read_fifo  = isac_read_fifo,
	.write_fifo = isac_write_fifo,
};

static u8
hscx_read(struct IsdnCardState *cs, int hscx, u8 offset)
{
	return readreg(cs, cs->hw.saphir.hscx, offset + (hscx ? 0x40 : 0));
}

static void
hscx_write(struct IsdnCardState *cs, int hscx, u8 offset, u8 value)
{
	writereg(cs, cs->hw.saphir.hscx, offset + (hscx ? 0x40 : 0), value);
}

static void
hscx_read_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	readfifo(cs, cs->hw.saphir.hscx, hscx ? 0x40 : 0, data, size);
}

static void
hscx_write_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	writefifo(cs, cs->hw.saphir.hscx, hscx ? 0x40 : 0, data, size);
}

static struct bc_hw_ops hscx_ops = {
	.read_reg   = hscx_read,
	.write_reg  = hscx_write,
	.read_fifo  = hscx_read_fifo,
	.write_fifo = hscx_write_fifo,
};

static void
saphir_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u8 val;

	spin_lock(&cs->lock);
	val = hscx_read(cs, 1, HSCX_ISTA);
      Start_HSCX:
	if (val)
		hscx_int_main(cs, val);
	val = isac_read(cs, ISAC_ISTA);
      Start_ISAC:
	if (val)
		isac_interrupt(cs, val);
	val = hscx_read(cs, 1, HSCX_ISTA);
	if (val) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX IntStat after IntRoutine");
		goto Start_HSCX;
	}
	val = isac_read(cs, ISAC_ISTA);
	if (val) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ISAC IntStat after IntRoutine");
		goto Start_ISAC;
	}
	/* Watchdog */
	if (cs->hw.saphir.timer.function) 
		mod_timer(&cs->hw.saphir.timer, jiffies+1*HZ);
	else
		printk(KERN_WARNING "saphir: Spurious timer!\n");

	hscx_write(cs, 0, HSCX_MASK, 0xFF);
	hscx_write(cs, 1, HSCX_MASK, 0xFF);
	isac_write(cs, ISAC_MASK, 0xFF);
	isac_write(cs, ISAC_MASK, 0x0);
	hscx_write(cs, 0, HSCX_MASK, 0x0);
	hscx_write(cs, 1, HSCX_MASK, 0x0);
	spin_unlock(&cs->lock);
}

static void
SaphirWatchDog(struct IsdnCardState *cs)
{
        /* 5 sec WatchDog, so read at least every 4 sec */
	isac_read(cs, ISAC_RBCH);
	mod_timer(&cs->hw.saphir.timer, jiffies+1*HZ);
}

void
release_io_saphir(struct IsdnCardState *cs)
{
	byteout(cs->hw.saphir.cfg_reg + IRQ_REG, 0xff);
	del_timer_sync(&cs->hw.saphir.timer);
	cs->hw.saphir.timer.function = NULL;
	if (cs->hw.saphir.cfg_reg)
		release_region(cs->hw.saphir.cfg_reg, 6);
}

static int
saphir_reset(struct IsdnCardState *cs)
{
	u8 irq_val;

	switch(cs->irq) {
		case 5: irq_val = 0;
			break;
		case 3: irq_val = 1;
			break;
		case 11:
			irq_val = 2;
			break;
		case 12:
			irq_val = 3;
			break;
		case 15:
			irq_val = 4;
			break;
		default:
			printk(KERN_WARNING "HiSax: saphir wrong IRQ %d\n",
				cs->irq);
			return (1);
	}
	byteout(cs->hw.saphir.cfg_reg + IRQ_REG, irq_val);
	byteout(cs->hw.saphir.cfg_reg + RESET_REG, 1);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((30*HZ)/1000);	/* Timeout 30ms */
	byteout(cs->hw.saphir.cfg_reg + RESET_REG, 0);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout((30*HZ)/1000);	/* Timeout 30ms */
	byteout(cs->hw.saphir.cfg_reg + IRQ_REG, irq_val);
	byteout(cs->hw.saphir.cfg_reg + SPARE_REG, 0x02);
	return (0);
}

static int
saphir_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	switch (mt) {
		case CARD_RESET:
			saphir_reset(cs);
			return(0);
		case CARD_RELEASE:
			release_io_saphir(cs);
			return(0);
		case CARD_TEST:
			return(0);
	}
	return(0);
}

static struct card_ops saphir_ops = {
	.init     = inithscxisac,
	.irq_func = saphir_interrupt,
};

int __init
setup_saphir(struct IsdnCard *card)
{
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, saphir_rev);
	printk(KERN_INFO "HiSax: HST Saphir driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_HSTSAPHIR)
		return (0);

	/* IO-Ports */
	cs->hw.saphir.cfg_reg = card->para[1];
	cs->hw.saphir.isac = card->para[1] + ISAC_DATA;
	cs->hw.saphir.hscx = card->para[1] + HSCX_DATA;
	cs->hw.saphir.ale = card->para[1] + ADDRESS_REG;
	cs->irq = card->para[0];
	if (!request_region((cs->hw.saphir.cfg_reg), 6, "saphir")) {
		printk(KERN_WARNING
			"HiSax: %s config port %x-%x already in use\n",
			CardType[card->typ],
			cs->hw.saphir.cfg_reg,
			cs->hw.saphir.cfg_reg + 5);
		return (0);
	}

	printk(KERN_INFO
	       "HiSax: %s config irq:%d io:0x%X\n",
	       CardType[cs->typ], cs->irq,
	       cs->hw.saphir.cfg_reg);

	cs->hw.saphir.timer.function = (void *) SaphirWatchDog;
	cs->hw.saphir.timer.data = (long) cs;
	init_timer(&cs->hw.saphir.timer);
	cs->hw.saphir.timer.expires = jiffies + 4*HZ;
	add_timer(&cs->hw.saphir.timer);
	if (saphir_reset(cs)) {
		release_io_saphir(cs);
		return (0);
	}
	cs->dc_hw_ops = &isac_ops;
	cs->bc_hw_ops = &hscx_ops;
	cs->cardmsg = &saphir_card_msg;
	cs->card_ops = &saphir_ops;
	ISACVersion(cs, "saphir:");
	if (HscxVersion(cs, "saphir:")) {
		printk(KERN_WARNING
		    "saphir: wrong HSCX versions check IO address\n");
		release_io_saphir(cs);
		return (0);
	}
	return (1);
}
