#ifndef _ASM_IA64_TLB_H
#define _ASM_IA64_TLB_H
/*
 * Copyright (C) 2002 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * This file was derived from asm-generic/tlb.h.
 */
/*
 * Removing a translation from a page table (including TLB-shootdown) is a four-step
 * procedure:
 *
 *	(1) Flush (virtual) caches --- ensures virtual memory is coherent with kernel memory
 *	    (this is a no-op on ia64).
 *	(2) Clear the relevant portions of the page-table
 *	(3) Flush the TLBs --- ensures that stale content is gone from CPU TLBs
 *	(4) Release the pages that were freed up in step (2).
 *
 * Note that the ordering of these steps is crucial to avoid races on MP machines.
 *
 * The Linux kernel defines several platform-specific hooks for TLB-shootdown.  When
 * unmapping a portion of the virtual address space, these hooks are called according to
 * the following template:
 *
 *	tlb <- tlb_gather_mmu(mm);		// start unmap for address space MM
 *	{
 *	  for each vma that needs a shootdown do {
 *	    tlb_start_vma(tlb, vma);
 *	      for each page-table-entry PTE that needs to be removed do {
 *		tlb_remove_tlb_entry(tlb, pte, address);
 *		if (pte refers to a normal page) {
 *		  tlb_remove_page(tlb, page);
 *		}
 *	      }
 *	    tlb_end_vma(tlb, vma);
 *	  }
 *	}
 *	tlb_finish_mmu(tlb, start, end);	// finish unmap for address space MM
 */
#include <linux/config.h>
#include <linux/mm.h>

#include <asm/processor.h>
#include <asm/tlbflush.h>

#ifdef CONFIG_SMP
# define FREE_PTE_NR		2048
# define tlb_fast_mode(tlb)	((tlb)->nr == ~0UL)
#else
# define FREE_PTE_NR		0
# define tlb_fast_mode(tlb)	(1)
#endif

typedef struct {
	struct mm_struct	*mm;
	unsigned long		nr;	/* == ~0UL => fast mode */
	unsigned long		freed;	/* number of pages freed */
	unsigned long		start_addr;
	unsigned long		end_addr;
	struct page 		*pages[FREE_PTE_NR];
} mmu_gather_t;

/* Users of the generic TLB shootdown code must declare this storage space. */
extern mmu_gather_t	mmu_gathers[NR_CPUS];

/*
 * Flush the TLB for address range START to END and, if not in fast mode, release the
 * freed pages that where gathered up to this point.
 */
static inline void
ia64_tlb_flush_mmu (mmu_gather_t *tlb, unsigned long start, unsigned long end)
{
	unsigned long nr;

	if (unlikely (end - start >= 1024*1024*1024*1024UL
		      || rgn_index(start) != rgn_index(end - 1)))
	{
		/*
		 * If we flush more than a tera-byte or across regions, we're probably
		 * better off just flushing the entire TLB(s).  This should be very rare
		 * and is not worth optimizing for.
		 */
		flush_tlb_all();
	} else {
		/*
		 * XXX fix me: flush_tlb_range() should take an mm pointer instead of a
		 * vma pointer.
		 */
		struct vm_area_struct vma;

		vma.vm_mm = tlb->mm;
		/* flush the address range from the tlb: */
		flush_tlb_range(&vma, start, end);
		/* now flush the virt. page-table area mapping the address range: */
		flush_tlb_range(&vma, ia64_thash(start), ia64_thash(end));
	}

	/* lastly, release the freed pages */
	nr = tlb->nr;
	if (!tlb_fast_mode(tlb)) {
		unsigned long i;
		tlb->nr = 0;
		tlb->start_addr = ~0UL;
		for (i = 0; i < nr; ++i)
			free_page_and_swap_cache(tlb->pages[i]);
	}
}

/*
 * Return a pointer to an initialized mmu_gather_t.
 */
static inline mmu_gather_t *
tlb_gather_mmu (struct mm_struct *mm)
{
	mmu_gather_t *tlb = &mmu_gathers[smp_processor_id()];

	tlb->mm = mm;
	tlb->freed = 0;
	tlb->start_addr = ~0UL;

	/* Use fast mode if only one CPU is online */
	tlb->nr = smp_num_cpus > 1 ? 0UL : ~0UL;
	return tlb;
}

/*
 * Called at the end of the shootdown operation to free up any resources that were
 * collected.  The page table lock is still held at this point.
 */
static inline void
tlb_finish_mmu (mmu_gather_t *tlb, unsigned long start, unsigned long end)
{
	unsigned long freed = tlb->freed;
	struct mm_struct *mm = tlb->mm;
	unsigned long rss = mm->rss;

	if (rss < freed)
		freed = rss;
	mm->rss = rss - freed;
	/*
	 * Note: tlb->nr may be 0 at this point, so we can't rely on tlb->start_addr and
	 * tlb->end_addr.
	 */
	ia64_tlb_flush_mmu(tlb, start, end);

	/* keep the page table cache within bounds */
	check_pgt_cache();
}

/*
 * Remove TLB entry for PTE mapped at virtual address ADDRESS.  This is called for any
 * PTE, not just those pointing to (normal) physical memory.
 */
static inline void
tlb_remove_tlb_entry (mmu_gather_t *tlb, pte_t pte, unsigned long address)
{
	if (tlb->start_addr == ~0UL)
		tlb->start_addr = address;
	tlb->end_addr = address + PAGE_SIZE;
}

/*
 * Logically, this routine frees PAGE.  On MP machines, the actual freeing of the page
 * must be delayed until after the TLB has been flushed (see comments at the beginning of
 * this file).
 */
static inline void
tlb_remove_page (mmu_gather_t *tlb, struct page *page)
{
	if (tlb_fast_mode(tlb)) {
		free_page_and_swap_cache(page);
		return;
	}
	tlb->pages[tlb->nr++] = page;
	if (tlb->nr >= FREE_PTE_NR)
		ia64_tlb_flush_mmu(tlb, tlb->start_addr, tlb->end_addr);
}

#define tlb_start_vma(tlb, vma)			do { } while (0)
#define tlb_end_vma(tlb, vma)			do { } while (0)

#endif /* _ASM_IA64_TLB_H */
