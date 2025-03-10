/* misc.c
 *
 * Copyright (C) 2014-2022 wolfSSL Inc.
 *
 * This file is part of wolfSSH.
 *
 * wolfSSH is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfSSH is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with wolfSSH.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * The misc module contains inline functions. This file is either included
 * into source files or built separately depending on the inline configure
 * option.
 */


#ifdef HAVE_CONFIG_H
    #include <config.h>
#endif

#ifdef WOLFSSL_USER_SETTINGS
#include <wolfssl/wolfcrypt/settings.h>
#else
#include <wolfssl/options.h>
#endif

#ifndef WOLFSSH_MISC_C
#define WOLFSSH_MISC_C


#include <wolfssh/misc.h>
#include <wolfssh/log.h>


#ifdef NO_INLINE
    #define STATIC
#else
    #define STATIC static
#endif


/* Check for if compiling misc.c when not needed. */
#if !defined(WOLFSSH_MISC_INCLUDED) && !defined(NO_INLINE)
    #define MISC_WARNING "misc.c does not need to be compiled when using inline (NO_INLINE not defined))"

    #ifndef _MSC_VER
        #warning MISC_WARNING
    #else
        #pragma message("warning: " MISC_WARNING)
    #endif

#else /* !WOLFSSL_MISC_INCLUDED && !NO_INLINE */


#ifndef min
STATIC INLINE word32 min(word32 a, word32 b)
{
    return a > b ? b : a;
}
#endif /* min */


/* convert opaque to 32 bit integer */
STATIC INLINE void ato32(const byte* c, word32* u32)
{
    *u32 = (c[0] << 24) | (c[1] << 16) | (c[2] << 8) | c[3];
}


/* convert 32 bit integer to opaque */
STATIC INLINE void c32toa(word32 u32, byte* c)
{
    c[0] = (u32 >> 24) & 0xff;
    c[1] = (u32 >> 16) & 0xff;
    c[2] = (u32 >>  8) & 0xff;
    c[3] =  u32 & 0xff;
}


/* Make sure compiler doesn't skip */
STATIC INLINE void ForceZero(const void* mem, word32 length)
{
    volatile byte* z = (volatile byte*)mem;

    while (length--) *z++ = 0;
}


/* check all length bytes for equality, return 0 on success */
STATIC INLINE int ConstantCompare(const byte* a, const byte* b,
                                  word32 length)
{
    word32 i;
    word32 compareSum = 0;

    for (i = 0; i < length; i++) {
        compareSum |= a[i] ^ b[i];
    }

    return compareSum;
}


#undef STATIC


#endif /* !WOLFSSL_MISC_INCLUDED && !NO_INLINE */


#endif /* WOLFSSH_MISC_C */
