/*
 * parse.c --- UUID parsing
 *
 * Copyright (C) 1996, 1997 Theodore Ts'o.
 *
 * %Begin-Header%
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, and the entire permission notice in its entirety,
 *    including the disclaimer of warranties.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, ALL OF
 * WHICH ARE HEREBY DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF NOT ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 * %End-Header%
 */

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uuid/uuid.h>

int uuid_parse(const char *in, uuid_t uu) {
    size_t len = strlen(in);
    if (len != 36)
        return -1;

    return uuid_parse_range(in, in + len, uu);
}

static void uuid_pack(const struct uuid *uu, uuid_t ptr) {
    uint32_t tmp;
    unsigned char *out = ptr;

    tmp = uu->time_low;
    out[3] = (unsigned char)tmp;
    tmp >>= 8;
    out[2] = (unsigned char)tmp;
    tmp >>= 8;
    out[1] = (unsigned char)tmp;
    tmp >>= 8;
    out[0] = (unsigned char)tmp;

    tmp = uu->time_mid;
    out[5] = (unsigned char)tmp;
    tmp >>= 8;
    out[4] = (unsigned char)tmp;

    tmp = uu->time_hi_and_version;
    out[7] = (unsigned char)tmp;
    tmp >>= 8;
    out[6] = (unsigned char)tmp;

    tmp = uu->clock_seq;
    out[9] = (unsigned char)tmp;
    tmp >>= 8;
    out[8] = (unsigned char)tmp;

    memcpy(out + 10, uu->node, 6);
}

int uuid_parse_range(const char *in_start, const char *in_end, uuid_t uu) {
    struct uuid uuid;
    int i;
    const char *cp;
    char buf[3];

    if ((in_end - in_start) != 36)
        return -1;
    for (i = 0, cp = in_start; i < 36; i++, cp++) {
        if ((i == 8) || (i == 13) || (i == 18) || (i == 23)) {
            if (*cp == '-')
                continue;
            return -1;
        }

        if (!isxdigit(*cp))
            return -1;
    }
    errno = 0;
    uuid.time_low = strtoul(in_start, NULL, 16);

    if (!errno)
        uuid.time_mid = strtoul(in_start + 9, NULL, 16);
    if (!errno)
        uuid.time_hi_and_version = strtoul(in_start + 14, NULL, 16);
    if (!errno)
        uuid.clock_seq = strtoul(in_start + 19, NULL, 16);
    if (errno)
        return -1;

    cp = in_start + 24;
    buf[2] = 0;
    for (i = 0; i < 6; i++) {
        buf[0] = *cp++;
        buf[1] = *cp++;

        errno = 0;
        uuid.node[i] = strtoul(buf, NULL, 16);
        if (errno)
            return -1;
    }

    uuid_pack(&uuid, uu);
    return 0;
}

int uuid_is_null(const uuid_t uu) {
    const uuid_t nil = {0};

    return !memcmp(uu, nil, sizeof(nil));
}
