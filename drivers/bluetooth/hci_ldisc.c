/* 
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated

   Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES 
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN 
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF 
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS, 
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS 
   SOFTWARE IS DISCLAIMED.
*/

/*
 * Bluetooth HCI UART driver.
 *
 * $Id: hci_ldisc.c,v 1.5 2002/10/02 18:37:20 maxk Exp $    
 */
#define VERSION "2.1"

#include <linux/config.h>
#include <linux/module.h>

#include <linux/version.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/poll.h>

#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/signal.h>
#include <linux/ioctl.h>
#include <linux/skbuff.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include "hci_uart.h"

#ifndef CONFIG_BT_HCIUART_DEBUG
#undef  BT_DBG
#define BT_DBG( A... )
#undef  BT_DMP
#define BT_DMP( A... )
#endif

static struct hci_uart_proto *hup[HCI_UART_MAX_PROTO];

int hci_uart_register_proto(struct hci_uart_proto *p)
{
	if (p->id >= HCI_UART_MAX_PROTO)
		return -EINVAL;

	if (hup[p->id])
		return -EEXIST;

	hup[p->id] = p;
	return 0;
}

int hci_uart_unregister_proto(struct hci_uart_proto *p)
{
	if (p->id >= HCI_UART_MAX_PROTO)
		return -EINVAL;

	if (!hup[p->id])
		return -EINVAL;

	hup[p->id] = NULL;
	return 0;
}

static struct hci_uart_proto *hci_uart_get_proto(unsigned int id)
{
	if (id >= HCI_UART_MAX_PROTO)
		return NULL;
	return hup[id];
}

static inline void hci_uart_tx_complete(struct hci_uart *hu, int pkt_type)
{
	struct hci_dev *hdev = &hu->hdev;
	
	/* Update HCI stat counters */
	switch (pkt_type) {
	case HCI_COMMAND_PKT:
		hdev->stat.cmd_tx++;
		break;

	case HCI_ACLDATA_PKT:
		hdev->stat.acl_tx++;
		break;

	case HCI_SCODATA_PKT:
		hdev->stat.cmd_tx++;
		break;
	}
}

static inline struct sk_buff *hci_uart_dequeue(struct hci_uart *hu)
{
	struct sk_buff *skb = hu->tx_skb;
	if (!skb)
		skb = hu->proto->dequeue(hu);
	else
		hu->tx_skb = NULL;
	return skb;
}

int hci_uart_tx_wakeup(struct hci_uart *hu)
{
	struct tty_struct *tty = hu->tty;
	struct hci_dev *hdev = &hu->hdev;
	struct sk_buff *skb;
	
	if (test_and_set_bit(HCI_UART_SENDING, &hu->tx_state)) {
		set_bit(HCI_UART_TX_WAKEUP, &hu->tx_state);
		return 0;
	}

	BT_DBG("");

restart:
	clear_bit(HCI_UART_TX_WAKEUP, &hu->tx_state);

	while ((skb = hci_uart_dequeue(hu))) {
		int len;
	
		set_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
		len = tty->driver.write(tty, 0, skb->data, skb->len);
		hdev->stat.byte_tx += len;

		skb_pull(skb, len);
		if (skb->len) {
			hu->tx_skb = skb;
			break;
		}
	
		hci_uart_tx_complete(hu, skb->pkt_type);
		kfree_skb(skb);
	} 
	
	if (test_bit(HCI_UART_TX_WAKEUP, &hu->tx_state))
		goto restart;

	clear_bit(HCI_UART_SENDING, &hu->tx_state);
	return 0;
}

/* ------- Interface to HCI layer ------ */
/* Initialize device */
static int hci_uart_open(struct hci_dev *hdev)
{
	BT_DBG("%s %p", hdev->name, hdev);

	/* Nothing to do for UART driver */

	set_bit(HCI_RUNNING, &hdev->flags);
	return 0;
}

/* Reset device */
static int hci_uart_flush(struct hci_dev *hdev)
{
	struct hci_uart *hu  = (struct hci_uart *) hdev->driver_data;
	struct tty_struct *tty = hu->tty;

	BT_DBG("hdev %p tty %p", hdev, tty);

	if (hu->tx_skb) {
		kfree_skb(hu->tx_skb); hu->tx_skb = NULL;
	}

	/* Flush any pending characters in the driver and discipline. */
	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);

	if (tty->driver.flush_buffer)
		tty->driver.flush_buffer(tty);

	if (test_bit(HCI_UART_PROTO_SET, &hu->flags))
		hu->proto->flush(hu);

	return 0;
}

/* Close device */
static int hci_uart_close(struct hci_dev *hdev)
{
	BT_DBG("hdev %p", hdev);

	if (!test_and_clear_bit(HCI_RUNNING, &hdev->flags))
		return 0;

	hci_uart_flush(hdev);
	return 0;
}

/* Send frames from HCI layer */
static int hci_uart_send_frame(struct sk_buff *skb)
{
	struct hci_dev* hdev = (struct hci_dev *) skb->dev;
	struct tty_struct *tty;
	struct hci_uart *hu;

	if (!hdev) {
		BT_ERR("Frame for uknown device (hdev=NULL)");
		return -ENODEV;
	}

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return -EBUSY;

	hu = (struct hci_uart *) hdev->driver_data;
	tty = hu->tty;

	BT_DBG("%s: type %d len %d", hdev->name, skb->pkt_type, skb->len);

	hu->proto->enqueue(hu, skb);

	hci_uart_tx_wakeup(hu);
	return 0;
}

static void hci_uart_destruct(struct hci_dev *hdev)
{
	struct hci_uart *hu;

	if (!hdev) return;

	BT_DBG("%s", hdev->name);

	hu = (struct hci_uart *) hdev->driver_data;
	kfree(hu);

	MOD_DEC_USE_COUNT;
}

/* ------ LDISC part ------ */
/* hci_uart_tty_open
 * 
 *     Called when line discipline changed to HCI_UART.
 *
 * Arguments:
 *     tty    pointer to tty info structure
 * Return Value:    
 *     0 if success, otherwise error code
 */
static int hci_uart_tty_open(struct tty_struct *tty)
{
	struct hci_uart *hu = (void *) tty->disc_data;

	BT_DBG("tty %p", tty);

	if (hu)
		return -EEXIST;

	if (!(hu = kmalloc(sizeof(struct hci_uart), GFP_KERNEL))) {
		BT_ERR("Can't allocate controll structure");
		return -ENFILE;
	}
	memset(hu, 0, sizeof(struct hci_uart));

	tty->disc_data = hu;
	hu->tty = tty;

	spin_lock_init(&hu->rx_lock);

	/* Flush any pending characters in the driver and line discipline */
	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);

	if (tty->driver.flush_buffer)
		tty->driver.flush_buffer(tty);
	
	MOD_INC_USE_COUNT;
	return 0;
}

/* hci_uart_tty_close()
 *
 *    Called when the line discipline is changed to something
 *    else, the tty is closed, or the tty detects a hangup.
 */
static void hci_uart_tty_close(struct tty_struct *tty)
{
	struct hci_uart *hu = (void *)tty->disc_data;

	BT_DBG("tty %p", tty);

	/* Detach from the tty */
	tty->disc_data = NULL;

	if (hu) {
		struct hci_dev *hdev = &hu->hdev;
		hci_uart_close(hdev);

		if (test_and_clear_bit(HCI_UART_PROTO_SET, &hu->flags)) {
			hu->proto->close(hu);
			hci_unregister_dev(hdev);
		}

		MOD_DEC_USE_COUNT;
	}
}

/* hci_uart_tty_wakeup()
 *
 *    Callback for transmit wakeup. Called when low level
 *    device driver can accept more send data.
 *
 * Arguments:        tty    pointer to associated tty instance data
 * Return Value:    None
 */
static void hci_uart_tty_wakeup(struct tty_struct *tty)
{
	struct hci_uart *hu = (void *)tty->disc_data;

	BT_DBG("");

	if (!hu)
		return;

	clear_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);

	if (tty != hu->tty)
		return;

	if (test_bit(HCI_UART_PROTO_SET, &hu->flags))
		hci_uart_tx_wakeup(hu);
}

/* hci_uart_tty_room()
 * 
 *    Callback function from tty driver. Return the amount of 
 *    space left in the receiver's buffer to decide if remote
 *    transmitter is to be throttled.
 *
 * Arguments:        tty    pointer to associated tty instance data
 * Return Value:    number of bytes left in receive buffer
 */
static int hci_uart_tty_room (struct tty_struct *tty)
{
	return 65536;
}

/* hci_uart_tty_receive()
 * 
 *     Called by tty low level driver when receive data is
 *     available.
 *     
 * Arguments:  tty          pointer to tty isntance data
 *             data         pointer to received data
 *             flags        pointer to flags for data
 *             count        count of received data in bytes
 *     
 * Return Value:    None
 */
static void hci_uart_tty_receive(struct tty_struct *tty, const __u8 *data, char *flags, int count)
{
	struct hci_uart *hu = (void *)tty->disc_data;
	
	if (!hu || tty != hu->tty)
		return;

	if (!test_bit(HCI_UART_PROTO_SET, &hu->flags))
		return;
	
	spin_lock(&hu->rx_lock);
	hu->proto->recv(hu, (void *) data, count);
	hu->hdev.stat.byte_rx += count;
	spin_unlock(&hu->rx_lock);

	if (test_and_clear_bit(TTY_THROTTLED,&tty->flags) && tty->driver.unthrottle)
		tty->driver.unthrottle(tty);
}

static int hci_uart_register_dev(struct hci_uart *hu)
{
	struct hci_dev *hdev;

	BT_DBG("");

	/* Initialize and register HCI device */
	hdev = &hu->hdev;

	hdev->type = HCI_UART;
	hdev->driver_data = hu;

	hdev->open  = hci_uart_open;
	hdev->close = hci_uart_close;
	hdev->flush = hci_uart_flush;
	hdev->send  = hci_uart_send_frame;
	hdev->destruct = hci_uart_destruct;

	if (hci_register_dev(hdev) < 0) {
		BT_ERR("Can't register HCI device %s", hdev->name);
		return -ENODEV;
	}
	MOD_INC_USE_COUNT;
	return 0;
}

static int hci_uart_set_proto(struct hci_uart *hu, int id)
{
	struct hci_uart_proto *p;
	int err;	
	
	p = hci_uart_get_proto(id);
	if (!p)
		return -EPROTONOSUPPORT;

	err = p->open(hu);
	if (err)
		return err;

	hu->proto = p;

	err = hci_uart_register_dev(hu);
	if (err) {
		p->close(hu);
		return err;
	}
	return 0;
}

/* hci_uart_tty_ioctl()
 *
 *    Process IOCTL system call for the tty device.
 *
 * Arguments:
 *
 *    tty        pointer to tty instance data
 *    file       pointer to open file object for device
 *    cmd        IOCTL command code
 *    arg        argument for IOCTL call (cmd dependent)
 *
 * Return Value:    Command dependent
 */
static int hci_uart_tty_ioctl(struct tty_struct *tty, struct file * file,
                            unsigned int cmd, unsigned long arg)
{
	struct hci_uart *hu = (void *)tty->disc_data;
	int err = 0;

	BT_DBG("");

	/* Verify the status of the device */
	if (!hu)
		return -EBADF;

	switch (cmd) {
	case HCIUARTSETPROTO:
		if (!test_and_set_bit(HCI_UART_PROTO_SET, &hu->flags)) {
			err = hci_uart_set_proto(hu, arg);
			if (err) {
				clear_bit(HCI_UART_PROTO_SET, &hu->flags);
				return err;
			}
			tty->low_latency = 1;
		} else	
			return -EBUSY;

	case HCIUARTGETPROTO:
		if (test_bit(HCI_UART_PROTO_SET, &hu->flags))
			return hu->proto->id;
		return -EUNATCH;
		
	default:
		err = n_tty_ioctl(tty, file, cmd, arg);
		break;
	};

	return err;
}

/*
 * We don't provide read/write/poll interface for user space.
 */
static ssize_t hci_uart_tty_read(struct tty_struct *tty, struct file *file, unsigned char *buf, size_t nr)
{
	return 0;
}
static ssize_t hci_uart_tty_write(struct tty_struct *tty, struct file *file, const unsigned char *data, size_t count)
{
	return 0;
}
static unsigned int hci_uart_tty_poll(struct tty_struct *tty, struct file *filp, poll_table *wait)
{
	return 0;
}

#ifdef CONFIG_BT_HCIUART_H4
int h4_init(void);
int h4_deinit(void);
#endif
#ifdef CONFIG_BT_HCIUART_BCSP
int bcsp_init(void);
int bcsp_deinit(void);
#endif

int __init hci_uart_init(void)
{
	static struct tty_ldisc hci_uart_ldisc;
	int err;

	BT_INFO("Bluetooth HCI UART driver ver %s Copyright (C) 2000,2001 Qualcomm Inc",
		VERSION);
	BT_INFO("Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>");

	/* Register the tty discipline */

	memset(&hci_uart_ldisc, 0, sizeof (hci_uart_ldisc));
	hci_uart_ldisc.magic       = TTY_LDISC_MAGIC;
	hci_uart_ldisc.name        = "n_hci";
	hci_uart_ldisc.open        = hci_uart_tty_open;
	hci_uart_ldisc.close       = hci_uart_tty_close;
	hci_uart_ldisc.read        = hci_uart_tty_read;
	hci_uart_ldisc.write       = hci_uart_tty_write;
	hci_uart_ldisc.ioctl       = hci_uart_tty_ioctl;
	hci_uart_ldisc.poll        = hci_uart_tty_poll;
	hci_uart_ldisc.receive_room= hci_uart_tty_room;
	hci_uart_ldisc.receive_buf = hci_uart_tty_receive;
	hci_uart_ldisc.write_wakeup= hci_uart_tty_wakeup;

	if ((err = tty_register_ldisc(N_HCI, &hci_uart_ldisc))) {
		BT_ERR("Can't register HCI line discipline (%d)", err);
		return err;
	}

#ifdef CONFIG_BT_HCIUART_H4
	h4_init();
#endif
#ifdef CONFIG_BT_HCIUART_BCSP
	bcsp_init();
#endif
	
	return 0;
}

void hci_uart_cleanup(void)
{
	int err;

#ifdef CONFIG_BT_HCIUART_H4
	h4_deinit();
#endif
#ifdef CONFIG_BT_HCIUART_BCSP
	bcsp_deinit();
#endif

	/* Release tty registration of line discipline */
	if ((err = tty_register_ldisc(N_HCI, NULL)))
		BT_ERR("Can't unregister HCI line discipline (%d)", err);
}

module_init(hci_uart_init);
module_exit(hci_uart_cleanup);

MODULE_AUTHOR("Maxim Krasnyansky <maxk@qualcomm.com>");
MODULE_DESCRIPTION("Bluetooth HCI UART driver ver " VERSION);
MODULE_LICENSE("GPL");
