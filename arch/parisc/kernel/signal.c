/*
 *  linux/arch/parisc/kernel/signal.c: Architecture-specific signal
 *  handling support.
 *
 *  Copyright (C) 2000 David Huggins-Daines <dhd@debian.org>
 *  Copyright (C) 2000 Linuxcare, Inc.
 *
 *  Based on the ia64, i386, and alpha versions.
 *
 *  Like the IA-64, we are a recent enough port (we are *starting*
 *  with glibc2.2) that we do not need to support the old non-realtime
 *  Linux signals.  Therefore we don't.  HP/UX signals will go in
 *  arch/parisc/hpux/signal.c when we figure out how to do them.
 */

#include <linux/version.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <asm/ucontext.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>

#define DEBUG_SIG 0

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

extern long sys_wait4 (int, int *, int, struct rusage *);
int do_signal(sigset_t *oldset, struct pt_regs *regs, int in_syscall);

int copy_siginfo_to_user(siginfo_t *to, siginfo_t *from)
{
	if (!access_ok (VERIFY_WRITE, to, sizeof(siginfo_t)))
		return -EFAULT;
	if (from->si_code < 0)
		return __copy_to_user(to, from, sizeof(siginfo_t));
	else {
		int err;

		/*
		 * If you change siginfo_t structure, please be sure
		 * this code is fixed accordingly.  It should never
		 * copy any pad contained in the structure to avoid
		 * security leaks, but must copy the generic 3 ints
		 * plus the relevant union member.
		 */
		err = __put_user(from->si_signo, &to->si_signo);
		err |= __put_user(from->si_errno, &to->si_errno);
		err |= __put_user((short)from->si_code, &to->si_code);
		switch (from->si_code >> 16) {
		      case __SI_FAULT >> 16:
			/* FIXME: should we put the interruption code here? */
		      case __SI_POLL >> 16:
			err |= __put_user(from->si_addr, &to->si_addr);
			break;
		      case __SI_CHLD >> 16:
			err |= __put_user(from->si_utime, &to->si_utime);
			err |= __put_user(from->si_stime, &to->si_stime);
			err |= __put_user(from->si_status, &to->si_status);
		      default:
			err |= __put_user(from->si_uid, &to->si_uid);
			err |= __put_user(from->si_pid, &to->si_pid);
			break;
		      /* case __SI_RT: This is not generated by the kernel as of now.  */
		}
		return err;
	}
}

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */
#ifdef __LP64__
#include "sys32.h"
#endif

asmlinkage int
sys_rt_sigsuspend(sigset_t *unewset, size_t sigsetsize, struct pt_regs *regs)
{
	sigset_t saveset, newset;
#ifdef __LP64__
	/* XXX FIXME -- assumes 32-bit user app! */
	sigset_t32 newset32;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t32))
		return -EINVAL;

	if (copy_from_user(&newset32, (sigset_t32 *)unewset, sizeof(newset32)))
		return -EFAULT;

	newset.sig[0] = newset32.sig[0] | ((unsigned long)newset32.sig[1] << 32);
#else

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&newset, unewset, sizeof(newset)))
		return -EFAULT;
#endif
	sigdelsetmask(&newset, ~_BLOCKABLE);

	spin_lock_irq(&current->sigmask_lock);
	saveset = current->blocked;
	current->blocked = newset;
	recalc_sigpending();
	spin_unlock_irq(&current->sigmask_lock);

	regs->gr[28] = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&saveset, regs, 1))
			return -EINTR;
	}
}

/*
 * Do a signal return - restore sigcontext.
 */

struct rt_sigframe {
	unsigned int tramp[4];
	struct siginfo info;
	struct ucontext uc;
};

/* Trampoline for calling rt_sigreturn() */
#define INSN_LDI_R25_0	 0x34190000 /* ldi  0,%r25 (in_syscall=0) */
#define INSN_LDI_R25_1	 0x34190002 /* ldi  1,%r25 (in_syscall=1) */
#define INSN_LDI_R20	 0x3414015a /* ldi  __NR_rt_sigreturn,%r20 */
#define INSN_BLE_SR2_R0  0xe4008200 /* be,l 0x100(%sr2,%r0),%sr0,%r31 */
#define INSN_NOP	 0x80000240 /* nop */
/* For debugging */
#define INSN_DIE_HORRIBLY 0x68000ccc /* stw %r0,0x666(%sr0,%r0) */

/*
 * The 32-bit ABI wants at least 48 bytes for a function call frame:
 * 16 bytes for arg0-arg3, and 32 bytes for magic (the only part of
 * which Linux/parisc uses is sp-20 for the saved return pointer...)
 * Then, the stack pointer must be rounded to a cache line (64 bytes).
 */
#define PARISC_RT_SIGFRAME_SIZE					\
	(((sizeof(struct rt_sigframe) + 48) + 63) & -64)

static long
restore_sigcontext(struct sigcontext *sc, struct pt_regs *regs)
{
	long err = 0;

	err |= __copy_from_user(regs->gr, sc->sc_gr, sizeof(regs->gr));
	err |= __copy_from_user(regs->fr, sc->sc_fr, sizeof(regs->fr));
	err |= __copy_from_user(regs->iaoq, sc->sc_iaoq, sizeof(regs->iaoq));
	err |= __copy_from_user(regs->iasq, sc->sc_iasq, sizeof(regs->iasq));
	err |= __get_user(regs->sar, &sc->sc_sar);

#if DEBUG_SIG
	printk("restore_sigcontext: r28 is %ld\n", regs->gr[28]);
#endif
	return err;
}

void
sys_rt_sigreturn(struct pt_regs *regs, int in_syscall)
{
	struct rt_sigframe *frame;
	struct siginfo si;
	sigset_t set;
	unsigned long usp = regs->gr[30];

	/* Unwind the user stack to get the rt_sigframe structure. */
	frame = (struct rt_sigframe *)
		(usp - PARISC_RT_SIGFRAME_SIZE);
#if DEBUG_SIG
	printk("in sys_rt_sigreturn, frame is %p\n", frame);
#endif

	/* Verify that it's a good sigcontext before using it */
	if (!access_ok(VERIFY_READ, frame, sizeof(*frame)))
		goto give_sigsegv;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto give_sigsegv;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sigmask_lock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sigmask_lock);

	/* Good thing we saved the old gr[30], eh? */
	if (restore_sigcontext(&frame->uc.uc_mcontext, regs))
		goto give_sigsegv;

#if DEBUG_SIG
	printk("usp: %#08lx stack %p",
	       usp, &frame->uc.uc_stack);
#endif

	/* I don't know why everyone else assumes they can call this
           with a pointer to a stack_t on the kernel stack.  That
           makes no sense.  Anyway we'll do it like m68k, since we
           also are using segmentation in the same way as them. */
	if (do_sigaltstack(&frame->uc.uc_stack, NULL, usp) == -EFAULT)
		goto give_sigsegv;

	/* If we are on the syscall path IAOQ will not be restored, and
	 * if we are on the interrupt path we must not corrupt gr31.
	 */
	if (in_syscall)
		regs->gr[31] = regs->iaoq[0];
#if DEBUG_SIG
	printk("returning to %#lx\n", regs->iaoq[0]);
	printk("in sys_rt_sigreturn:\n");
	show_regs(regs);
#endif
	return;

give_sigsegv:
#if DEBUG_SIG
	printk("fuckup in sys_rt_sigreturn, sending SIGSEGV\n");
#endif
	si.si_signo = SIGSEGV;
	si.si_errno = 0;
	si.si_code = SI_KERNEL;
	si.si_pid = current->pid;
	si.si_uid = current->uid;
	si.si_addr = &frame->uc;
	force_sig_info(SIGSEGV, &si, current);
	return;
}

/*
 * Set up a signal frame.
 */

static inline void *
get_sigframe(struct k_sigaction *ka, unsigned long sp, size_t frame_size)
{
	if ((ka->sa.sa_flags & SA_ONSTACK) != 0 && ! on_sig_stack(sp))
		sp = current->sas_ss_sp + current->sas_ss_size;

	return (void *) sp; /* Stacks grow up.  Fun. */
}

static long
setup_sigcontext(struct sigcontext *sc, struct pt_regs *regs, int in_syscall)
		 
{
	unsigned long flags = 0;
	long err = 0;

	if (on_sig_stack((unsigned long) sc))
		flags |= PARISC_SC_FLAG_ONSTACK;
	if (in_syscall) {
		flags |= PARISC_SC_FLAG_IN_SYSCALL;
		/* regs->iaoq is undefined in the syscall return path */
		err |= __put_user(regs->gr[31], &sc->sc_iaoq[0]);
		err |= __put_user(regs->gr[31]+4, &sc->sc_iaoq[1]);
#if DEBUG_SIG
		printk("setup_sigcontext: iaoq %#lx/%#lx\n", regs->gr[31], regs->gr[31]);
#endif
	} else {
		err |= __copy_to_user(sc->sc_iaoq, regs->iaoq, sizeof(regs->iaoq));
		err |= __copy_to_user(sc->sc_iasq, regs->iasq, sizeof(regs->iasq));
#if DEBUG_SIG
		printk("setup_sigcontext: iaoq %#lx/%#lx\n", regs->iaoq[0], regs->iaoq[1]);
#endif
	}

	err |= __put_user(flags, &sc->sc_flags);
	err |= __copy_to_user(sc->sc_gr, regs->gr, sizeof(regs->gr));
	err |= __copy_to_user(sc->sc_fr, regs->fr, sizeof(regs->fr));
	err |= __put_user(regs->sar, &sc->sc_sar);
#if DEBUG_SIG
	printk("setup_sigcontext: r28 is %ld\n", regs->gr[28]);
#endif

	return err;
}

static long
setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
	       sigset_t *set, struct pt_regs *regs, int in_syscall)
{
	struct rt_sigframe *frame;
	unsigned long rp, usp, haddr;
	struct siginfo si;
	int err = 0;

	usp = regs->gr[30];
	/* access_ok is broken, so do a simplistic "are we stomping on
           kernel space" assertion. */
	if (usp > PAGE_OFFSET) {
		printk("setup_rt_frame: called on kernel space (usp=%#lx),  NOW YOU MUST DIE!!!\n",
		       usp);
		show_regs(regs);
		while(1);
	}
		
	frame = get_sigframe(ka, usp, sizeof(*frame));
	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

#if DEBUG_SIG
	printk("setup_rt_frame 1: frame %p info %p\n", frame, info);
#endif

	err |= __copy_to_user(&frame->info, info, sizeof(siginfo_t));
	err |= __put_user(current->sas_ss_sp, &frame->uc.uc_stack.ss_sp);
	err |= __put_user(current->sas_ss_size, &frame->uc.uc_stack.ss_size);
	err |= __put_user(sas_ss_flags(regs->gr[30]),
			  &frame->uc.uc_stack.ss_flags);
	err |= setup_sigcontext(&frame->uc.uc_mcontext, regs, in_syscall);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	if (err)
		goto give_sigsegv;

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	err |= __put_user(in_syscall ? INSN_LDI_R25_1 : INSN_LDI_R25_0,
			&frame->tramp[0]);
	err |= __put_user(INSN_LDI_R20, &frame->tramp[1]);
	err |= __put_user(INSN_BLE_SR2_R0, &frame->tramp[2]);
	err |= __put_user(INSN_NOP, &frame->tramp[3]);

#if DEBUG_SIG
	/* Assert that we're flushing in the correct space... */
	{
		int sid;
		asm ("mfsp %%sr3,%0" : "=r" (sid));
		printk("flushing 64 bytes at space %#x offset %p\n",
		       sid, frame->tramp);
	}
#endif

#if CACHE_FLUSHING_IS_NOT_BROKEN
	flush_icache_range((unsigned long) &frame->tramp[0],
			   (unsigned long) &frame->tramp[4]);
#else
	/* It should *always* be cache line-aligned, but the compiler
           sometimes screws up. */
	asm volatile("fdc 0(%%sr3,%0)\n\t"
		     "fdc %1(%%sr3,%0)\n\t"
		     "sync\n\t"
		     "fic 0(%%sr3,%0)\n\t"
		     "fic %1(%%sr3,%0)\n\t"
		     "sync\n\t"
		     : : "r" (frame->tramp), "r" (L1_CACHE_BYTES));
#endif
	rp = (unsigned long) frame->tramp;

	if (err)
		goto give_sigsegv;

#ifdef __LP64__
/* Much more has to happen with signals than this -- but it'll at least */
/* provide a pointer to some places which definitely need a look. */
#define HACK unsigned int
#else
#define HACK unsigned long
#endif
	haddr = (HACK) ka->sa.sa_handler;
	/* ARGH!  Fucking brain damage.  You don't want to know. */
	if (haddr & 2) {
		HACK *plabel;
		HACK ltp;

		plabel = (HACK *) (haddr & ~3);
		err |= __get_user(haddr, plabel);
		err |= __get_user(ltp, plabel + 1);
		if (err)
			goto give_sigsegv;
		regs->gr[19] = ltp;
	}

	/* The syscall return path will create IAOQ values from r31.
	 */
	if (in_syscall)
		regs->gr[31] = (HACK) haddr;
	else {
		regs->iaoq[0] = (HACK) haddr | 3;
		regs->iaoq[1] = regs->iaoq[0] + 4;
	}

	regs->gr[2]  = rp;                /* userland return pointer */
	regs->gr[26] = sig;               /* signal number */
	regs->gr[25] = (HACK) &frame->info; /* siginfo pointer */
	regs->gr[24] = (HACK) &frame->uc;   /* ucontext pointer */
#if DEBUG_SIG
	printk("making sigreturn frame: %#lx + %#lx = %#lx\n",
	       regs->gr[30], PARISC_RT_SIGFRAME_SIZE,
	       regs->gr[30] + PARISC_RT_SIGFRAME_SIZE);
#endif
	/* Raise the user stack pointer to make a proper call frame. */
	regs->gr[30] = ((HACK) frame + PARISC_RT_SIGFRAME_SIZE);

#if DEBUG_SIG
	printk("SIG deliver (%s:%d): frame=0x%p sp=%#lx iaoq=%#lx/%#lx rp=%#lx\n",
	       current->comm, current->pid, frame, regs->gr[30],
	       regs->iaoq[0], regs->iaoq[1], rp);
#endif

	return 1;

give_sigsegv:
#if DEBUG_SIG
	printk("fuckup in setup_rt_frame, sending SIGSEGV\n");
#endif
	if (sig == SIGSEGV)
		ka->sa.sa_handler = SIG_DFL;
	si.si_signo = SIGSEGV;
	si.si_errno = 0;
	si.si_code = SI_KERNEL;
	si.si_pid = current->pid;
	si.si_uid = current->uid;
	si.si_addr = frame;
	force_sig_info(SIGSEGV, &si, current);
	return 0;
}

/*
 * OK, we're invoking a handler.
 */	

static long
handle_signal(unsigned long sig, struct k_sigaction *ka,
	      siginfo_t *info, sigset_t *oldset,
	      struct pt_regs *regs, int in_syscall)
{
#if DEBUG_SIG
	printk("handle_signal(sig=%ld, ka=%p, info=%p, oldset=%p, regs=%p)\n",
	       sig, ka, info, oldset, regs);
#endif
	/* Set up the stack frame */
	if (!setup_rt_frame(sig, ka, info, oldset, regs, in_syscall))
		return 0;

	if (ka->sa.sa_flags & SA_ONESHOT)
		ka->sa.sa_handler = SIG_DFL;

	if (!(ka->sa.sa_flags & SA_NODEFER)) {
		spin_lock_irq(&current->sigmask_lock);
		sigorsets(&current->blocked,&current->blocked,&ka->sa.sa_mask);
		sigaddset(&current->blocked,sig);
		recalc_sigpending();
		spin_unlock_irq(&current->sigmask_lock);
	}
	return 1;
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 *
 * We need to be able to restore the syscall arguments (r21-r26) to
 * restart syscalls.  Thus, the syscall path should save them in the
 * pt_regs structure (it's okay to do so since they are caller-save
 * registers).  As noted below, the syscall number gets restored for
 * us due to the magic of delayed branching.
 */
asmlinkage int
do_signal(sigset_t *oldset, struct pt_regs *regs, int in_syscall)
{
	siginfo_t info;
	struct k_sigaction *ka;

#if DEBUG_SIG
	printk("do_signal(oldset=0x%p, regs=0x%p, sr7 %#lx, pending %d, in_syscall=%d\n",
	       oldset, regs, regs->sr[7], current->sigpending, in_syscall);
#endif
	/* Everyone else checks to see if they are in kernel mode at
	   this point and exits if that's the case.  I'm not sure why
	   we would be called in that case, but for some reason we
	   are. */

	if (!oldset)
		oldset = &current->blocked;

#if DEBUG_SIG
	printk("do_signal: oldset %08lx:%08lx\n", oldset->sig[0], oldset->sig[1]);
#endif

	for (;;) {
		unsigned long signr;

		spin_lock_irq(&current->sigmask_lock);
		signr = dequeue_signal(&current->blocked, &info);
		spin_unlock_irq(&current->sigmask_lock);
#if DEBUG_SIG
		printk("do_signal: signr=%ld, pid=%d\n", signr, current->pid);
#endif

		if (!signr)
			break;

		if ((current->ptrace & PT_PTRACED) && signr != SIGKILL) {
			/* Let the debugger run.  */
			current->exit_code = signr;
			set_current_state(TASK_STOPPED);
			notify_parent(current, SIGCHLD);
			schedule();

			/* We're back.  Did the debugger cancel the sig?  */
			if (!(signr = current->exit_code))
				continue;
			current->exit_code = 0;

			/* The debugger continued.  Ignore SIGSTOP.  */
			if (signr == SIGSTOP)
				continue;

			/* Update the siginfo structure.  Is this good?  */
			if (signr != info.si_signo) {
				info.si_signo = signr;
				info.si_errno = 0;
				info.si_code = SI_USER;
				info.si_pid = current->p_pptr->pid;
				info.si_uid = current->p_pptr->uid;
			}

			/* If the (new) signal is now blocked, requeue it.  */
			if (sigismember(&current->blocked, signr)) {
				send_sig_info(signr, &info, current);
				continue;
			}
		}

		ka = &current->sig->action[signr-1];
#if DEBUG_SIG
		printk("sa_handler is %lx\n", ka->sa.sa_handler);
#endif
		if ((unsigned long) ka->sa.sa_handler == (unsigned long) SIG_IGN) {
			if (signr != SIGCHLD)
				continue;
			while (sys_wait4(-1, NULL, WNOHANG, NULL) > 0)
				/* nothing */;
			continue;
		}

		if ((unsigned long) ka->sa.sa_handler == (unsigned long) SIG_DFL) {
			int exit_code = signr;

			/* Init gets no signals it doesn't want.  */
			if (current->pid == 1)
				continue;

			switch (signr) {
			case SIGCONT: case SIGCHLD: case SIGWINCH:
				continue;

			case SIGTSTP: case SIGTTIN: case SIGTTOU:
				if (is_orphaned_pgrp(current->pgrp))
					continue;
				/* FALLTHRU */

			case SIGSTOP:
				set_current_state(TASK_STOPPED);
				current->exit_code = signr;
				if (!(current->p_pptr->sig->action[SIGCHLD-1].sa.sa_flags & SA_NOCLDSTOP))
					notify_parent(current, SIGCHLD);
				schedule();
				continue;

			case SIGQUIT: case SIGILL: case SIGTRAP:
			case SIGABRT: case SIGFPE: case SIGSEGV:
			case SIGBUS: case SIGSYS: case SIGXCPU: case SIGXFSZ:
				if (signr == SIGQUIT) /* Userspace debugging */
					show_regs(regs);
				if (do_coredump(signr, regs))
					exit_code |= 0x80;
				/* FALLTHRU */

			default:
				sig_exit(signr, exit_code, &info);
				/* NOTREACHED */
			}
		}

		/* Restart a system call if necessary. */
		if (in_syscall) {
			/* Check the return code */
			switch (regs->gr[28]) {
			case -ERESTARTNOHAND:
#if DEBUG_SIG
				printk("ERESTARTNOHAND: returning -EINTR\n");
#endif
				regs->gr[28] = -EINTR;
				break;

			case -ERESTARTSYS:
				if (!(ka->sa.sa_flags & SA_RESTART)) {
#if DEBUG_SIG
					printk("ERESTARTSYS: putting -EINTR\n");
#endif
					regs->gr[28] = -EINTR;
					break;
				}
			/* fallthrough */
			case -ERESTARTNOINTR:
				/* A syscall is just a branch, so all
                                   we have to do is fiddle the return
                                   pointer. */
				regs->gr[31] -= 8; /* delayed branching */
				/* Preserve original r28. */
				regs->gr[28] = regs->orig_r28;
				break;
			}
		}
		/* Whee!  Actually deliver the signal.  If the
		   delivery failed, we need to continue to iterate in
		   this loop so we can deliver the SIGSEGV... */
		if (handle_signal(signr, ka, &info, oldset, regs, in_syscall)) {
#if DEBUG_SIG
			printk("Exiting do_signal (success), regs->gr[28] = %ld\n", regs->gr[28]);
#endif
			return 1;
		}
	}

	/* Did we come from a system call? */
	if (in_syscall) {
		/* Restart the system call - no handlers present */
		if (regs->gr[28] == -ERESTARTNOHAND ||
		    regs->gr[28] == -ERESTARTSYS ||
		    regs->gr[28] == -ERESTARTNOINTR) {
			/* Hooray for delayed branching.  We don't
                           have to restore %r20 (the system call
                           number) because it gets loaded in the delay
                           slot of the branch external instruction. */
			regs->gr[31] -= 8;
			/* Preserve original r28. */
			regs->gr[28] = regs->orig_r28;
		}
	}
#if DEBUG_SIG
	printk("Exiting do_signal (not delivered), regs->gr[28] = %ld\n", regs->gr[28]);
#endif
	return 0;
}
