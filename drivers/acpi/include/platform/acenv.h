/******************************************************************************
 *
 * Name: acenv.h - Generation environment specific items
 *       $Revision: 86 $
 *
 *****************************************************************************/

/*
 *  Copyright (C) 2000 - 2002, R. Byron Moore
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __ACENV_H__
#define __ACENV_H__


/*
 * Configuration for ACPI tools and utilities
 */

#ifdef _ACPI_DUMP_APP
#define ACPI_DEBUG
#define ACPI_APPLICATION
#define ENABLE_DEBUGGER
#define ACPI_USE_SYSTEM_CLIBRARY
#define PARSER_ONLY
#endif

#ifdef _ACPI_EXEC_APP
#undef DEBUGGER_THREADING
#define DEBUGGER_THREADING      DEBUGGER_SINGLE_THREADED
#define ACPI_DEBUG
#define ACPI_APPLICATION
#define ENABLE_DEBUGGER
#define ACPI_USE_SYSTEM_CLIBRARY
#endif

#ifdef _ACPI_ASL_COMPILER
#define ACPI_DEBUG
#define ACPI_APPLICATION
#define ENABLE_DEBUGGER
#define ACPI_USE_SYSTEM_CLIBRARY
#endif

/*
 * Memory allocation tracking.  Used only if
 * 1) This is the debug version
 * 2) This is NOT a 16-bit version of the code (not enough real-mode memory)
 */
#ifdef ACPI_DEBUG
#ifndef _IA16
#define ACPI_DBG_TRACK_ALLOCATIONS
#endif
#endif

/*
 * Environment configuration.  The purpose of this file is to interface to the
 * local generation environment.
 *
 * 1) ACPI_USE_SYSTEM_CLIBRARY - Define this if linking to an actual C library.
 *      Otherwise, local versions of string/memory functions will be used.
 * 2) ACPI_USE_STANDARD_HEADERS - Define this if linking to a C library and
 *      the standard header files may be used.
 *
 * The ACPI subsystem only uses low level C library functions that do not call
 * operating system services and may therefore be inlined in the code.
 *
 * It may be necessary to tailor these include files to the target
 * generation environment.
 *
 *
 * Functions and constants used from each header:
 *
 * string.h:    memcpy
 *              memset
 *              strcat
 *              strcmp
 *              strcpy
 *              strlen
 *              strncmp
 *              strncat
 *              strncpy
 *
 * stdlib.h:    strtoul
 *
 * stdarg.h:    va_list
 *              va_arg
 *              va_start
 *              va_end
 *
 */

/*! [Begin] no source code translation */

#if defined(_LINUX)
#include "aclinux.h"

#elif defined(_AED_EFI)
#include "acefi.h"

#elif defined(MSDOS)
#include "acdos16.h"

#elif defined(WIN32)
#include "acwin.h"

#elif defined(WIN64)
#include "acwin64.h"

#elif defined(__FreeBSD__)
#include "acfreebsd.h"

#elif defined(MODESTO)
#include "acmodesto.h"

#elif defined(NETWARE)
#include "acnetware.h"

#else

/* All other environments */

#define ACPI_USE_STANDARD_HEADERS

/* Name of host operating system (returned by the _OS_ namespace object) */

#define ACPI_OS_NAME         "Intel ACPI/CA Core Subsystem"

/* This macro is used to tag functions as "printf-like" because
 * some compilers can catch printf format string problems. MSVC
 * doesn't, so this is proprocessed away.
 */
#define ACPI_PRINTF_LIKE_FUNC

#endif

/*! [End] no source code translation !*/

/******************************************************************************
 *
 * C library configuration
 *
 *****************************************************************************/

#ifdef ACPI_USE_SYSTEM_CLIBRARY
/*
 * Use the standard C library headers.
 * We want to keep these to a minimum.
 *
 */

#ifdef ACPI_USE_STANDARD_HEADERS
/*
 * Use the standard headers from the standard locations
 */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#endif /* ACPI_USE_STANDARD_HEADERS */

/*
 * We will be linking to the standard Clib functions
 */

#define ACPI_STRSTR(s1,s2)      strstr((s1), (s2))
#define ACPI_STRUPR(s)          acpi_ut_strupr ((s))
#define ACPI_STRLEN(s)          (u32) strlen((s))
#define ACPI_STRCPY(d,s)        strcpy((d), (s))
#define ACPI_STRNCPY(d,s,n)     strncpy((d), (s), (NATIVE_INT)(n))
#define ACPI_STRNCMP(d,s,n)     strncmp((d), (s), (NATIVE_INT)(n))
#define ACPI_STRCMP(d,s)        strcmp((d), (s))
#define ACPI_STRCAT(d,s)        strcat((d), (s))
#define ACPI_STRNCAT(d,s,n)     strncat((d), (s), (NATIVE_INT)(n))
#define ACPI_STRTOUL(d,s,n)     strtoul((d), (s), (NATIVE_INT)(n))
#define ACPI_MEMCPY(d,s,n)      (void) memcpy((d), (s), (NATIVE_INT)(n))
#define ACPI_MEMSET(d,s,n)      (void) memset((d), (s), (NATIVE_INT)(n))
#define ACPI_TOUPPER            toupper
#define ACPI_TOLOWER            tolower
#define ACPI_IS_XDIGIT          isxdigit

/******************************************************************************
 *
 * Not using native C library, use local implementations
 *
 *****************************************************************************/
#else

/*
 * Use local definitions of C library macros and functions
 * NOTE: The function implementations may not be as efficient
 * as an inline or assembly code implementation provided by a
 * native C library.
 */

#ifndef va_arg

#ifndef _VALIST
#define _VALIST
typedef char *va_list;
#endif /* _VALIST */

/*
 * Storage alignment properties
 */

#define  _AUPBND                (sizeof (NATIVE_INT) - 1)
#define  _ADNBND                (sizeof (NATIVE_INT) - 1)

/*
 * Variable argument list macro definitions
 */

#define _bnd(X, bnd)            (((sizeof (X)) + (bnd)) & (~(bnd)))
#define va_arg(ap, T)           (*(T *)(((ap) += (_bnd (T, _AUPBND))) - (_bnd (T,_ADNBND))))
#define va_end(ap)              (void) 0
#define va_start(ap, A)         (void) ((ap) = (((char *) &(A)) + (_bnd (A,_AUPBND))))

#endif /* va_arg */


#define ACPI_STRSTR(s1,s2)      acpi_ut_strstr ((s1), (s2))
#define ACPI_STRUPR(s)          acpi_ut_strupr ((s))
#define ACPI_STRLEN(s)          acpi_ut_strlen ((s))
#define ACPI_STRCPY(d,s)        acpi_ut_strcpy ((d), (s))
#define ACPI_STRNCPY(d,s,n)     acpi_ut_strncpy ((d), (s), (n))
#define ACPI_STRNCMP(d,s,n)     acpi_ut_strncmp ((d), (s), (n))
#define ACPI_STRCMP(d,s)        acpi_ut_strcmp ((d), (s))
#define ACPI_STRCAT(d,s)        acpi_ut_strcat ((d), (s))
#define ACPI_STRNCAT(d,s,n)     acpi_ut_strncat ((d), (s), (n))
#define ACPI_STRTOUL(d,s,n)     acpi_ut_strtoul ((d), (s),(n))
#define ACPI_MEMCPY(d,s,n)      (void) acpi_ut_memcpy ((d), (s), (n))
#define ACPI_MEMSET(d,v,n)      (void) acpi_ut_memset ((d), (v), (n))
#define ACPI_TOUPPER            acpi_ut_to_upper
#define ACPI_TOLOWER            acpi_ut_to_lower

#endif /* ACPI_USE_SYSTEM_CLIBRARY */


/******************************************************************************
 *
 * Assembly code macros
 *
 *****************************************************************************/

/*
 * Handle platform- and compiler-specific assembly language differences.
 * These should already have been defined by the platform includes above.
 *
 * Notes:
 * 1) Interrupt 3 is used to break into a debugger
 * 2) Interrupts are turned off during ACPI register setup
 */

/* Unrecognized compiler, use defaults */

#ifndef ACPI_ASM_MACROS

/*
 * Calling conventions:
 *
 * ACPI_SYSTEM_XFACE        - Interfaces to host OS (handlers, threads)
 * ACPI_EXTERNAL_XFACE      - External ACPI interfaces
 * ACPI_INTERNAL_XFACE      - Internal ACPI interfaces
 * ACPI_INTERNAL_VAR_XFACE  - Internal variable-parameter list interfaces
 */
#define ACPI_SYSTEM_XFACE
#define ACPI_EXTERNAL_XFACE
#define ACPI_INTERNAL_XFACE
#define ACPI_INTERNAL_VAR_XFACE

#define ACPI_ASM_MACROS
#define BREAKPOINT3
#define ACPI_DISABLE_IRQS()
#define ACPI_ENABLE_IRQS()
#define ACPI_ACQUIRE_GLOBAL_LOCK(Glptr, acq)
#define ACPI_RELEASE_GLOBAL_LOCK(Glptr, acq)

#endif /* ACPI_ASM_MACROS */


#ifdef ACPI_APPLICATION

/* Don't want software interrupts within a ring3 application */

#undef BREAKPOINT3
#define BREAKPOINT3
#endif


/******************************************************************************
 *
 * Compiler-specific information is contained in the compiler-specific
 * headers.
 *
 *****************************************************************************/
#endif /* __ACENV_H__ */
