/* 
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#include <stddef.h>
#include <string.h>
#include <asm/ptrace.h>
#include <asm/sigcontext.h>
#include "sysdep/ptrace.h"
#include "kern_util.h"
#include "frame_user.h"

int sc_size(void *data)
{
	struct arch_frame_data *arch = data;

	return(sizeof(struct sigcontext) + arch->fpstate_size);
}

int copy_sc_to_user(void *to_ptr, void *from_ptr, void *data)
{
	struct arch_frame_data *arch = data;
	struct sigcontext *to = to_ptr, *from = from_ptr;
	struct _fpstate *to_fp, *from_fp;
	int err;

	to_fp = (struct _fpstate *)((unsigned long) to + sizeof(*to));
	from_fp = from->fpstate;
	err = copy_to_user_proc(to, from, sizeof(*to));
	if(from_fp != NULL){
		err |= copy_to_user_proc(&to->fpstate, &to_fp,
					 sizeof(to->fpstate));
		err |= copy_to_user_proc(to_fp, from_fp, arch->fpstate_size);
	}
	return(err);
}

int copy_sc_from_user(void *to_ptr, void *from_ptr, void *data)
{
	struct arch_frame_data *arch = data;
	struct sigcontext *to = to_ptr, *from = from_ptr;
	struct _fpstate *to_fp, *from_fp;
	unsigned long sigs;
	int err;

	to_fp = to->fpstate;
	from_fp = from->fpstate;
	sigs = to->oldmask;
	err = copy_from_user_proc(to, from, sizeof(*to));
	to->oldmask = sigs;
	if(to_fp != NULL){
		err |= copy_from_user_proc(&to->fpstate, &to_fp,
					   sizeof(to->fpstate));
		err |= copy_from_user_proc(to_fp, from_fp, arch->fpstate_size);
	}
	return(err);
}

void sc_to_sc(void *to_ptr, void *from_ptr)
{
	struct sigcontext *to = to_ptr, *from = from_ptr;
	int size = sizeof(*to) + signal_frame_sc.arch.fpstate_size;

	memcpy(to, from, size);
	if(from->fpstate != NULL) to->fpstate = (struct _fpstate *) (to + 1);
}

unsigned long *sc_sigmask(void *sc_ptr)
{
	struct sigcontext *sc = sc_ptr;

	return(&sc->oldmask);
}

int sc_get_fpregs(unsigned long buf, void *sc_ptr)
{
	struct sigcontext *sc = sc_ptr;
	struct _fpstate *from = sc->fpstate, *to = (struct _fpstate *) buf;
	int err = 0;

	if(from == NULL){
		err |= clear_user_proc(&to->cw, sizeof(to->cw));
		err |= clear_user_proc(&to->sw, sizeof(to->sw));
		err |= clear_user_proc(&to->tag, sizeof(to->tag));
		err |= clear_user_proc(&to->ipoff, sizeof(to->ipoff));
		err |= clear_user_proc(&to->cssel, sizeof(to->cssel));
		err |= clear_user_proc(&to->dataoff, sizeof(to->dataoff));
		err |= clear_user_proc(&to->datasel, sizeof(to->datasel));
		err |= clear_user_proc(&to->_st, sizeof(to->_st));
	}
	else {
		err |= copy_to_user_proc(&to->cw, &from->cw, sizeof(to->cw));
		err |= copy_to_user_proc(&to->sw, &from->sw, sizeof(to->sw));
		err |= copy_to_user_proc(&to->tag, &from->tag, 
					 sizeof(to->tag));
		err |= copy_to_user_proc(&to->ipoff, &from->ipoff, 
					 sizeof(to->ipoff));
		err |= copy_to_user_proc(&to->cssel,& from->cssel, 
					 sizeof(to->cssel));
		err |= copy_to_user_proc(&to->dataoff, &from->dataoff, 
				    sizeof(to->dataoff));
		err |= copy_to_user_proc(&to->datasel, &from->datasel, 
				    sizeof(to->datasel));
		err |= copy_to_user_proc(to->_st, from->_st, sizeof(to->_st));
	}
	return(err);
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-file-style: "linux"
 * End:
 */
