/*
 *              Round robin policy for multipath.
 *
 *
 * Version:	$Id: multipath_rr.c,v 1.1.2.2 2004/09/16 07:42:34 elueck Exp $
 *
 * Authors:	Einar Lueck <elueck@de.ibm.com><lkml@einar-lueck.de>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/igmp.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/mroute.h>
#include <linux/init.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <net/raw.h>
#include <linux/notifier.h>
#include <linux/if_arp.h>
#include <linux/netfilter_ipv4.h>
#include <net/ipip.h>
#include <net/checksum.h>
#include <net/ip_mp_alg.h>

#define RTprint(a...)	// printk(KERN_DEBUG a)

#define MULTIPATH_MAX_CANDIDATES 40

static struct rtable* last_used = NULL;

void __multipath_remove(struct rtable *rt)
{
	if (last_used == rt)
		last_used = NULL;
}

void __multipath_selectroute(const struct flowi *flp,
			     struct rtable *first, struct rtable **rp)
{
	struct rtable *nh, *result, *min_use_cand = NULL;
	int min_use = -1;

	/* if necessary and possible utilize the old alternative */
	if ( ( flp->flags & FLOWI_FLAG_MULTIPATHOLDROUTE ) != 0 &&
	     last_used != NULL ) {
		RTprint( KERN_CRIT"%s: holding route \n",
			 __FUNCTION__ );
		result = last_used;
		goto out;
	}

	/* 1. make sure all alt. nexthops have the same GC related data
	 * 2. determine the new candidate to be returned
	 */
	result = NULL;
	for (nh = rcu_dereference(first); nh;
 	     nh = rcu_dereference(nh->u.rt_next)) {
		if ((nh->u.dst.flags & DST_BALANCED) != 0 &&
		    multipath_comparekeys(&nh->fl, flp)) {
			nh->u.dst.lastuse = jiffies;

			if (min_use == -1 || nh->u.dst.__use < min_use) {
				min_use = nh->u.dst.__use;
				min_use_cand = nh;
			}
			RTprint( KERN_CRIT"%s: found balanced entry\n",
				 __FUNCTION__ );
		}
	}
	result = min_use_cand;
	if (!result)
		result = first;

out:
	last_used = result;
	result->u.dst.__use++;
	*rp = result;
}
