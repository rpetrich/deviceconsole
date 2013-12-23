/*	$NetBSD: forward.c,v 1.13 1998/09/18 01:42:54 cjs Exp $	*/

/*-
 * Copyright (c) 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Edward Sze-Tyan Wang.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 */

#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)forward.c	8.1 (Berkeley) 6/6/93";
#endif
__RCSID("$NetBSD: forward.c,v 1.13 1998/09/18 01:42:54 cjs Exp $");
#endif /* not lint */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>

#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>

#include "extern.h"

char *fname;

/*
 * forward -- display the file, from an offset, forward.
 *
 * There are eight separate cases for this -- regular and non-regular
 * files, by bytes or lines and from the beginning or end of the file.
 *
 * FBYTES	byte offset from the beginning of the file
 *	REG	seek
 *	NOREG	read, counting bytes
 *
 * FLINES	line offset from the beginning of the file
 *	REG	read, counting lines
 *	NOREG	read, counting lines
 *
 * RBYTES	byte offset from the end of the file
 *	REG	seek
 *	NOREG	cyclically read characters into a wrap-around buffer
 *
 * RLINES
 *	REG	mmap the file and step back until reach the correct offset.
 *	NOREG	cyclically read lines into a wrap-around array of buffers
 */
void log_tail(FILE *fp)
{
	int ch;
	struct timeval second;
    

    while (getc(fp) != EOF);
        if (ferror(fp)) {
            return;
        }
    
	
    
	for (;;) {
        char *buffer = NULL;
        int size = 0;
		while ((ch = getc(fp)) != EOF)  {
            buffer = realloc(buffer, size + 1);
            
            if(ch == '\n'){
                buffer = realloc(buffer, size + 2);
                buffer[size] = ch;
                size++;
                buffer[size] = '\0';
                simulator_write_callback(buffer, size);
                bzero(buffer, size);
                free(buffer);
                buffer = NULL;
                size = 0;
            }else{
                buffer[size] = ch;
                size++;
            }
		}
		if (ferror(fp)) {
			return;
		}
		/*
		 * We pause for one second after displaying any data that has
		 * accumulated since we read the file.  Since sleep(3) takes
		 * eight system calls, use select() instead.
		 */
		second.tv_sec = 1;
		second.tv_usec = 0;
		if (select(0, NULL, NULL, NULL, &second) == -1)
			err(1, "select: %s", strerror(errno));
		clearerr(fp);
    }
}
