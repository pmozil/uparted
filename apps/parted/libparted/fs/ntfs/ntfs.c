/*
    libparted - a library for manipulating disk partitions
    Copyright (C) 2000, 2007, 2009-2014, 2019-2023 Free Software Foundation,
    Inc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>

#include <parted/endian.h>
#include <parted/parted.h>

#if ENABLE_NLS
#include <libintl.h>
#define _(String) dgettext(PACKAGE, String)
#else
#define _(String) (String)
#endif /* ENABLE_NLS */

#include <unistd.h>

#define NTFS_SIGNATURE "NTFS"

PedGeometry *ntfs_probe(PedGeometry *geom) {
    uint8_t *buf = malloc(geom->dev->sector_size);
    PedGeometry *newg = NULL;

    if (!ped_geometry_read(geom, buf, 0, 1))
        return 0;

    if (strncmp(NTFS_SIGNATURE, ((char *)buf + 3), strlen(NTFS_SIGNATURE)) ==
        0) {
        uint64_t length;
        memcpy(&length, buf + 0x28, sizeof(uint64_t));
        newg = ped_geometry_new(geom->dev, geom->start, length);
    }
    return newg;
}

static PedFileSystemOps ntfs_ops = {
    probe : ntfs_probe,
};

static PedFileSystemType ntfs_type = {
    next : NULL,
    ops : &ntfs_ops,
    name : "ntfs",
};

void ped_file_system_ntfs_init() { ped_file_system_type_register(&ntfs_type); }

void ped_file_system_ntfs_done() {
    ped_file_system_type_unregister(&ntfs_type);
}
