/*
 * Copyright (C) 1998-2011 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation version 2.1 and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the Lesser GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA.
 *
 */

/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of VMware Inc. nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission of VMware Inc.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * The contents of this file are subject to the terms of the Common
 * Development and Distribution License (the "License") version 1.0
 * and no later version.  You may not use this file except in
 * compliance with the License.
 *
 * You can obtain a copy of the License at
 *         http://www.opensource.org/licenses/cddl1.php
 *
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 */

/*
 * vm_basic_types.h --
 *
 *    basic data types.
 */


#ifndef _VM_BASIC_TYPES_H_
#define	_VM_BASIC_TYPES_H_

/* STRICT ANSI means the Xserver build and X defines Bool differently. */
#if !defined(_XTYPEDEF_BOOL) && \
	(!defined(__STRICT_ANSI__) || defined(__FreeBSD__) ||	\
	defined(__MINGW32__))
#define	_XTYPEDEF_BOOL
typedef char Bool;
#endif

#ifndef FALSE
#define	FALSE	0
#endif

#ifndef TRUE
#define	TRUE	1
#endif

#define	IsBool(x)	(((x) & ~1) == 0)
#define	IsBool2(x, y)	((((x) | (y)) & ~1) == 0)

/*
 * Macros __i386__ and __ia64 are intrinsically defined by GCC
 */
#if defined _MSC_VER && defined _M_X64
#define	__x86_64__
#elif defined _MSC_VER && defined _M_IX86
#define	__i386__
#endif

#ifdef __i386__
#define	VM_I386
#endif

#ifdef __x86_64__
#define	VM_X86_64
#define	VM_I386
#define	vm_x86_64 (1)
#else
#define	vm_x86_64 (0)
#endif


#if defined(HAVE_STDINT_H)

/*
 * TODO: This is a C99 standard header.  We should be able to test for
 * #if __STDC_VERSION__ >= 199901L, but that breaks the Netware build
 * (which doesn't have stdint.h).
 */

#include <stdint.h>

typedef uint64_t	uint64;
typedef int64_t		int64;
typedef uint32_t	uint32;
typedef int32_t		int32;
typedef uint16_t	uint16;
typedef int16_t		int16;
typedef uint8_t		uint8;
typedef int8_t		int8;

/*
 * Note: C does not specify whether char is signed or unsigned, and
 * both gcc and msvc implement processor-specific signedness.  With
 * three types:
 * typeof(char) != typeof(signed char) != typeof(unsigned char)
 *
 * Be careful here, because gcc (4.0.1 and others) likes to warn about
 * conversions between signed char * and char *.
 */

#else /* !HAVE_STDINT_H */

#ifdef _MSC_VER

typedef unsigned __int64 uint64;
typedef signed __int64 int64;

#elif __GNUC__
/* The Xserver source compiles with -ansi -pendantic */
#if !defined(__STRICT_ANSI__) || defined(__FreeBSD__)
#if defined(VM_X86_64)
typedef unsigned long uint64;
typedef long int64;
#else
typedef unsigned long long uint64;
typedef long long int64;
#endif
#endif
#else
typedef uint64_t uint64;
typedef int64_t int64;
#endif /* _MSC_VER */

typedef unsigned int	uint32;
typedef unsigned short	uint16;
typedef unsigned char	uint8;

typedef int		int32;
typedef short		int16;
typedef signed char	int8;

#endif /* HAVE_STDINT_H */

/*
 * FreeBSD (for the tools build) unconditionally defines these in
 * sys/inttypes.h so don't redefine them if this file has already
 * been included. [greg]
 *
 * This applies to Solaris as well.
 */

/*
 * Before trying to do the includes based on OS defines, see if we can use
 * feature-based defines to get as much functionality as possible
 */

#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#ifdef HAVE_SYS_INTTYPES_H
#include <sys/inttypes.h>
#endif
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef __FreeBSD__
#include <sys/param.h> /* For __FreeBSD_version */
#endif

#if !defined(USING_AUTOCONF)
#if defined(__FreeBSD__) || defined(sun)
#ifdef KLD_MODULE
#include <sys/types.h>
#else
#if __FreeBSD_version >= 500043
#if !defined(VMKERNEL)
#include <inttypes.h>
#endif
#include <sys/types.h>
#else
#include <sys/inttypes.h>
#endif
#endif
#elif defined __APPLE__
#if KERNEL
#include <sys/unistd.h>
#include <sys/types.h> /* mostly for size_t */
#include <stdint.h>
#else
#include <unistd.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdint.h>
#endif
#elif defined __ANDROID__
#include <stdint.h>
#else
#if !defined(__intptr_t_defined) && !defined(intptr_t)
#ifdef VM_I386
#define	__intptr_t_defined
#ifdef VM_X86_64
typedef int64	intptr_t;
#else
typedef int32	intptr_t;
#endif
#elif defined(__arm__)
typedef int32	intptr_t;
#endif
#endif

#ifndef _STDINT_H
#ifdef VM_I386
#ifdef VM_X86_64
typedef uint64	uintptr_t;
#else
typedef uint32	uintptr_t;
#endif
#elif defined(__arm__)
typedef uint32	uintptr_t;
#endif
#endif
#endif
#endif

/*
 * Use CONST3264/CONST3264U if you want a constant to be
 * treated as a 32-bit number on 32-bit compiles and
 * a 64-bit number on 64-bit compiles. Useful in the case
 * of shifts, like (CONST3264U(1) << x), where x could be
 * more than 31 on a 64-bit compile.
 */

#ifdef VM_X86_64
#define	CONST3264(a) CONST64(a)
#define	CONST3264U(a) CONST64U(a)
#else
#define	CONST3264(a) (a)
#define	CONST3264U(a) (a)
#endif

typedef uint8 *TCA;  /* Pointer into TC (usually). */

/*
 * Type big enough to hold an integer between 0..100
 */
typedef uint8 Percent;
#define	AsPercent(v)	((Percent)(v))
#define	CHOOSE_PERCENT  AsPercent(101)


typedef uintptr_t VA;
typedef uintptr_t VPN;

typedef uint64_t    PA;
typedef uint32_t    PPN;

typedef uint64    PhysMemOff;
typedef uint64    PhysMemSize;

/* The Xserver source compiles with -ansi -pendantic */
#ifndef __STRICT_ANSI__
typedef uint64    BA;
#endif
typedef uint32    BPN;
typedef uint32    PageNum;
typedef unsigned	MemHandle;
typedef unsigned int  IoHandle;
typedef int32	World_ID;

#ifndef EXTERN
#define	EXTERN	extern
#endif
#define	CONST	const


#ifndef INLINE
#ifdef _MSC_VER
#define	INLINE	__inline
#else
#define	INLINE	inline
#endif
#endif


/*
 * At present, we effectively require a compiler that is at least
 * gcc-3.3 (circa 2003).  Enforce this here, various things below
 * this line depend upon it.
 *
 * In practice, most things presently compile with gcc-4.1 or gcc-4.4.
 * The various linux kernel modules may use older (gcc-3.3) compilers.
 */
#if defined __GNUC__ && (__GNUC__ < 3 || (__GNUC__ == 3 && __GNUC_MINOR__ < 3))
#error "gcc version is to old to compile assembly, need gcc-3.3 or better"
#endif


/*
 * Consider the following reasons functions are inlined:
 *
 *  1) inlined for performance reasons
 *  2) inlined because it's a single-use function
 *
 * Functions which meet only condition 2 should be marked with this
 * inline macro; It is not critical to be inlined (but there is a
 * code-space & runtime savings by doing so), so when other callers
 * are added the inline-ness should be removed.
 */

#if defined __GNUC__
/*
 * Starting at version 3.3, gcc does not always inline functions marked
 * 'inline' (it depends on their size and other factors). To force gcc
 * to inline a function, one must use the __always_inline__ attribute.
 * This attribute should be used sparingly and with care.  It is usually
 * preferable to let gcc make its own inlining decisions
 */
#define	INLINE_ALWAYS INLINE __attribute__((__always_inline__))
#else
#define	INLINE_ALWAYS INLINE
#endif
#define	INLINE_SINGLE_CALLER INLINE_ALWAYS

/*
 * Used when a hard guaranteed of no inlining is needed. Very few
 * instances need this since the absence of INLINE is a good hint
 * that gcc will not do inlining.
 */

#if defined(__GNUC__) && (defined(VMM) || defined(VMKERNEL))
#define	ABSOLUTELY_NOINLINE __attribute__((__noinline__))
#endif

/*
 * Branch prediction hints:
 *     LIKELY(exp)   - Expression exp is likely TRUE.
 *     UNLIKELY(exp) - Expression exp is likely FALSE.
 *   Usage example:
 *        if (LIKELY(excCode == EXC_NONE)) {
 *               or
 *        if (UNLIKELY(REAL_MODE(vc))) {
 *
 * We know how to predict branches on gcc3 and later (hopefully),
 * all others we don't so we do nothing.
 */

#if defined __GNUC__
/*
 * gcc3 uses __builtin_expect() to inform the compiler of an expected value.
 * We use this to inform the static branch predictor. The '!!' in LIKELY
 * will convert any !=0 to a 1.
 */
#define	LIKELY(_exp)	__builtin_expect(!!(_exp), 1)
#define	UNLIKELY(_exp)	__builtin_expect((_exp), 0)
#else
#define	LIKELY(_exp)	(_exp)
#define	UNLIKELY(_exp)	(_exp)
#endif

/*
 * UNUSED_PARAM should surround the parameter name and type declaration,
 * e.g. "int MyFunction(int var1, UNUSED_PARAM(int var2))"
 *
 */

#ifndef UNUSED_PARAM
#if defined(__GNUC__)
#define	UNUSED_PARAM(_parm) _parm  __attribute__((__unused__))
#else
#define	UNUSED_PARAM(_parm) _parm
#endif
#endif

#ifndef UNUSED_VARIABLE
// XXX is there a better way?
#define	UNUSED_VARIABLE(_var) (void)_var
#endif

#ifndef _SIZE_T
#ifdef VM_I386
#define	_SIZE_T
#ifdef VM_X86_64
	typedef uint64 size_t;
#else
	typedef uint32 size_t;
#endif
#elif defined(__arm__)
#define	_SIZE_T
	typedef uint32 size_t;
#endif
#endif

#if !defined(FROBOS) && !defined(_SSIZE_T) && !defined(_SSIZE_T_) && \
	!defined(ssize_t) && !defined(__ssize_t_defined) && \
	!defined(_SSIZE_T_DECLARED) && !defined(_SSIZE_T_DEFINED_)
#ifdef VM_I386
#define	_SSIZE_T
#define	__ssize_t_defined
#define	_SSIZE_T_DECLARED
#ifdef	VM_X86_64
	typedef int64 ssize_t;
#else
	typedef int32 ssize_t;
#endif
#elif defined(__arm__)
#define	_SSIZE_T
#define	__ssize_t_defined
#define	_SSIZE_T_DECLARED
	typedef int32 ssize_t;
#endif

#endif

/*
 * Format modifier for printing pid_t.  On sun the pid_t is a ulong, but on
 * Linux it's an int.
 * Use this like this: printf("The pid is %"FMTPID".\n", pid);
 */
#ifdef sun
#ifdef VM_X86_64
#define	FMTPID "d"
#else
#define	FMTPID "lu"
#endif
#else
#define	FMTPID "d"
#endif

/*
 * Format modifier for printing uid_t.  On Solaris 10 and earlier, uid_t
 * is a ulong, but on other platforms it's an unsigned int.
 * Use this like this: printf("The uid is %"FMTUID".\n", uid);
 */
#if defined(sun) && !defined(SOL11)
#ifdef VM_X86_64
#define	FMTUID "u"
#else
#define	FMTUID "lu"
#endif
#else
#define	FMTUID "u"
#endif

/*
 * Format modifier for printing mode_t.  On sun the mode_t is a ulong, but on
 * Linux it's an int.
 * Use this like this: printf("The mode is %"FMTMODE".\n", mode);
 */
#ifdef sun
#ifdef VM_X86_64
#define	FMTMODE "o"
#else
#define	FMTMODE "lo"
#endif
#else
#define	FMTMODE "o"
#endif

#endif /* _VM_BASIC_TYPES_H_ */
