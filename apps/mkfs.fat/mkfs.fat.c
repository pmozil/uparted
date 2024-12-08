#include <Library/BaseLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/BlockIo.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define TRUE 1
#define FALSE 0

#define TEST_BUFFER_BLOCKS 16
#define BLOCK_SIZE 1024
#define HARD_SECTOR_SIZE 512
#define SECTORS_PER_BLOCK (BLOCK_SIZE / HARD_SECTOR_SIZE)

#define NO_NAME "NO NAME    "

#define mark_sector_bad(sector) mark_FAT_sector(sector, FAT_BAD)

static inline int cdiv(int a, int b) { return (a + b - 1) / b; }

#define FAT_EOF (atari_format ? 0x0fffffff : 0x0ffffff8)
#define FAT_BAD 0x0ffffff7

#define MSDOS_EXT_SIGN 0x29
#define MSDOS_FAT12_SIGN "FAT12   "
#define MSDOS_FAT16_SIGN "FAT16   "
#define MSDOS_FAT32_SIGN "FAT32   "

#define BOOT_SIGN 0xAA55

#define MAX_CLUST_12 4084
#define MIN_CLUST_16 4087

#define MAX_CLUST_16 65524
#define MIN_CLUST_32 65525

#define MAX_CLUST_32 268435446

#define OLDGEMDOS_MAX_SECTORS 32765
#define GEMDOS_MAX_SECTORS 65531
#define GEMDOS_MAX_SECTOR_SIZE (16 * 1024)

#define BOOTCODE_SIZE 448
#define BOOTCODE_FAT32_SIZE 420

struct msdos_volume_info {
    uint8_t drive_number;
    uint8_t boot_flags;
    uint8_t ext_boot_sign;
    uint8_t volume_id[4];
    uint8_t volume_label[11];
    uint8_t fs_type[8];
} __attribute__((packed));

struct msdos_boot_sector {
    uint8_t boot_jump[3];
    uint8_t system_id[8];
    uint16_t sector_size;
    uint8_t cluster_size;
    uint16_t reserved;
    uint8_t fats;
    uint16_t dir_entries;
    uint16_t sectors;
    uint8_t media;
    uint16_t fat_length;
    uint16_t secs_track;
    uint16_t heads;
    uint32_t hidden;
    uint32_t total_sect;
    union {
        struct {
            struct msdos_volume_info vi;
            uint8_t boot_code[BOOTCODE_SIZE];
        } __attribute__((packed)) _oldfat;
        struct {
            uint32_t fat32_length;
            uint16_t flags;
            uint8_t version[2];
            uint32_t root_cluster;
            uint16_t info_sector;
            uint16_t backup_boot;
            uint16_t reserved2[6];
            struct msdos_volume_info vi;
            uint8_t boot_code[BOOTCODE_FAT32_SIZE];
        } __attribute__((packed)) _fat32;
    } __attribute__((packed)) fstype;
    uint16_t boot_sign;
} __attribute__((packed));
#define fat32 fstype._fat32
#define oldfat fstype._oldfat

struct fat32_fsinfo {
    uint32_t reserved1;
    uint32_t signature;
    uint32_t free_clusters;
    uint32_t next_cluster;
    uint32_t reserved2[4];
} __attribute__((packed));

unsigned char dummy_boot_jump[3] = {0xeb, 0x3c, 0x90};

unsigned char dummy_boot_jump_m68k[2] = {0x60, 0x1c};

#define MSG_OFFSET_OFFSET 3
char dummy_boot_code[BOOTCODE_SIZE] =
    "\x0e"
    "\x1f"
    "\xbe\x5b\x7c"

    "\xac"
    "\x22\xc0"
    "\x74\x0b"
    "\x56"
    "\xb4\x0e"
    "\xbb\x07\x00"
    "\xcd\x10"
    "\x5e"
    "\xeb\xf0"

    "\x32\xe4"
    "\xcd\x16"
    "\xcd\x19"
    "\xeb\xfe"

    "This is not a bootable disk.  Please insert a bootable floppy and\r\n"
    "press any key to try again ... \r\n";

#define MESSAGE_OFFSET 29

static char initial_volume_name[] = NO_NAME;

static char *device_name = NULL;
static int check = FALSE;
static int verbose = 0;
static long volume_id;
static time_t create_time = -1;
static char *volume_name = initial_volume_name;
static unsigned long long blocks;
static unsigned sector_size = 512;
static int sector_size_set = 0;
static int backup_boot = 0;
static int backup_boot_set = 0;
static int info_sector = 0;
static int reserved_sectors = 0;
static int bad_blocks = 0;
static int bad_clusters = 0;
static int nr_fats = 2;
static int size_fat = 0;
static int size_fat_by_user = 0;
static EFI_BLOCK_IO_PROTOCOL *dev = NULL;
static off_t part_sector = 0;
static int ignore_safety_checks = 0;
static struct msdos_boot_sector bs;
static int start_data_sector;
static int start_data_block;
static unsigned char *fat;
static unsigned alloced_fat_length;
static unsigned fat_entries;
static unsigned char *info_sector_buffer;
static struct msdos_dir_entry *root_dir;
static int size_root_dir;
static uint32_t num_sectors;
static int sectors_per_cluster = 0;
static int root_dir_entries = 0;
static int root_dir_entries_set = 0;
static char *blank_sector;
static unsigned hidden_sectors = 0;
static int hidden_sectors_by_user = 0;
static int drive_number_option = 0;
static int drive_number_by_user = 0;
static int fat_media_byte = 0;
static int malloc_entire_fat = FALSE;
static int align_structures = TRUE;
static int orphaned_sectors = 0;
static int invariant = 0;
static int fill_mbr_partition = -1;
static volatile sig_atomic_t display_status;

static int set_FAT_byte(int index, unsigned char value);
static unsigned int read_FAT_cluster(int cluster);
static int mark_FAT_cluster(int cluster, unsigned int value);
static int mark_FAT_sector(int sector, unsigned int value);
static long do_check(int try, off_t current_block);
static void alarm_intr(int alnum);
static void check_blocks(void);
static void get_list_blocks(char *filename);
static void check_mount(char *device_name);
static void establish_params(struct device_info *info);
static void process_bad_blocks(void);
static void setup_tables(void);
static void write_tables(void);

static int set_FAT_byte(int index, unsigned char value) {
    unsigned char old;

    old = fat[index];
    fat[index] = value;

    return old != value;
}

static unsigned int read_FAT_cluster(int cluster) {
    uint32_t e;

    if (cluster < 0 || cluster >= fat_entries)
        exit(1);
    if (size_fat != 32)
        exit(1);

    e = le32toh(((unsigned int *)fat)[cluster]);
    return e & 0xfffffff;
}

static int mark_FAT_cluster(int cluster, unsigned int value) {
    int changed = 0;

    if (cluster < 0 || cluster >= fat_entries)
        exit(1);

    switch (size_fat) {
    case 12:
        value &= 0x0fff;
        if (((cluster * 3) & 0x1) == 0) {
            changed |= set_FAT_byte(3 * cluster / 2, value & 0x00ff);
            changed |= set_FAT_byte(3 * cluster / 2 + 1,
                                    (fat[(3 * cluster / 2) + 1] & 0x00f0) |
                                        ((value & 0x0f00) >> 8));
        } else {
            changed |=
                set_FAT_byte(3 * cluster / 2, (fat[3 * cluster / 2] & 0x000f) |
                                                  ((value & 0x000f) << 4));
            changed |= set_FAT_byte(3 * cluster / 2 + 1, (value & 0x0ff0) >> 4);
        }
        break;

    case 16:
        value &= 0xffff;
        changed |= set_FAT_byte(2 * cluster, value & 0x00ff);
        changed |= set_FAT_byte(2 * cluster + 1, value >> 8);
        break;

    case 32:
        value &= 0xfffffff;
        changed |= set_FAT_byte(4 * cluster, value & 0x000000ff);
        changed |= set_FAT_byte(4 * cluster + 1, (value & 0x0000ff00) >> 8);
        changed |= set_FAT_byte(4 * cluster + 2, (value & 0x00ff0000) >> 16);
        changed |= set_FAT_byte(4 * cluster + 3, (value & 0xff000000) >> 24);
        break;

    default:
        exit(1);
    }

    if (changed && value == FAT_BAD)
        bad_clusters++;

    return changed;
}

static int mark_FAT_sector(int sector, unsigned int value) {
    int cluster = (sector - start_data_sector) / (int)(bs.cluster_size) /
                      (sector_size / HARD_SECTOR_SIZE) +
                  2;

    if (sector < start_data_sector || sector >= num_sectors)
        exit(1);

    return mark_FAT_cluster(cluster, value);
}

static long do_check(int try, off_t current_block) {
    static char buffer[BLOCK_SIZE * TEST_BUFFER_BLOCKS];
    long got;

    dev->ReadBlocks(dev, dev->Media->MediaId,
                    part_sector * sector_size + current_block * BLOCK_SIZE,
                    (UINTN)try * BLOCK_SIZE, buffer);
    if (got < 0)
        got = 0;

    if (got & (BLOCK_SIZE - 1))
        printf("Unexpected values in do_check: probably bugs\n");
    got /= BLOCK_SIZE;

    return got;
}

static void alarm_intr(int alnum) {
    (void)alnum;

    display_status = 1;
}

static void check_blocks(void) {
    struct sigaction old;
    off_t currently_testing;
    int try, got;
    int i;

    if (verbose) {
        printf("Searching for bad blocks ");
        fflush(stdout);
    }
    currently_testing = 0;
    display_status = 0;
    if (verbose) {
        struct sigaction action;

        action.sa_handler = alarm_intr;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;
        if (sigaction(SIGALRM, &action, &old) < 0)
            old.sa_handler = SIG_ERR;
        else
            alarm(5);
    }
    try = TEST_BUFFER_BLOCKS;
    while (currently_testing < blocks) {
        if (display_status) {
            display_status = 0;
            printf("%lld... ", (unsigned long long)currently_testing);
            fflush(stdout);
            alarm(5);
        }
        if (currently_testing + try > blocks)
            try = blocks - currently_testing;
        got = do_check(try, currently_testing);
        currently_testing += got;
        if (got == try) {
            try = TEST_BUFFER_BLOCKS;
            continue;
        } else
            try = 1;
        if (currently_testing < start_data_block)
            exit(1);

        for (i = 0; i < SECTORS_PER_BLOCK; i++)
            mark_sector_bad(currently_testing * SECTORS_PER_BLOCK + i);
        bad_blocks++;
        currently_testing++;
    }

    if (verbose) {
        if (old.sa_handler != SIG_ERR) {
            alarm(0);
            sigaction(SIGALRM, &old, NULL);
        }
        printf("\n");
    }

    if (bad_blocks)
        process_bad_blocks();
}

static void get_list_blocks(char *filename) {
    int changed, i;
    FILE *listfile;
    long long blockno;
    char *line = NULL;
    size_t linesize = 0;
    unsigned lineno = 0;
    char *end, *check;

    listfile = fopen(filename, "r");
    if (listfile == (FILE *)NULL)
        exit(1);

    while (1) {
        lineno++;
        ssize_t length = getline(&line, &linesize, listfile);
        if (length < 0) {
            if (feof(listfile))
                break;

            exit(1);
        }

        errno = 0;
        blockno = strtoll(line, &end, 10);

        if (errno || blockno < 0) {
            exit(1);
                lineno);
        }

        check = end;
        while (*check) {
            if (!isspace((unsigned char)*check)) {
                exit(1);
                    lineno);
            }

            check++;
        }

        if (end == line)
            continue;

        changed = 0;
        for (i = 0; i < SECTORS_PER_BLOCK; i++) {
            unsigned long long sector = blockno * SECTORS_PER_BLOCK + i;

            if (sector < start_data_sector) {
                exit(1);
                    "before data area",
                    lineno, blockno);
            }

            if (sector >= num_sectors) {
                exit(1);
                    "behind end of filesystem",
                    lineno, blockno);
            }

            changed |= mark_sector_bad(sector);
        }
        if (changed)
            bad_blocks++;
    }
    fclose(listfile);
    free(line);

    if (bad_blocks)
        process_bad_blocks();
}

static void check_mount(char *device_name) {
    if (is_device_mounted(device_name))
        exit(1);
}

static void establish_params(struct device_info *info) {
    unsigned int sec_per_track;
    unsigned int heads;
    unsigned int media = 0xf8;
    unsigned int cluster_size = 4;
    int def_root_dir_entries = 512;

    if (info->geom_heads > 0) {
        heads = info->geom_heads;
        sec_per_track = info->geom_sectors;
    } else {
        unsigned long long int total_sectors;

        if (info->geom_size > 0)
            total_sectors = info->geom_size;
        else if (info->sector_size > 0)
            total_sectors = info->size / info->sector_size;
        else
            total_sectors = info->size / sector_size;

        if (total_sectors <= 524288) {
            heads = total_sectors <= 32768    ? 2
                    : total_sectors <= 65536  ? 4
                    : total_sectors <= 262144 ? 8
                                              : 16;
            sec_per_track = total_sectors <= 4096 ? 16 : 32;
        } else {
            heads = total_sectors <= 16 * 63 * 1024    ? 16
                    : total_sectors <= 32 * 63 * 1024  ? 32
                    : total_sectors <= 64 * 63 * 1024  ? 64
                    : total_sectors <= 128 * 63 * 1024 ? 128
                                                       : 255;
            sec_per_track = 63;
        }
    }

    if (info->type != TYPE_FIXED) {

        switch (info->size / 1024) {
        case 360:
            sec_per_track = 9;
            heads = 2;
            media = 0xfd;
            cluster_size = 2;
            def_root_dir_entries = 112;
            break;

        case 720:
            sec_per_track = 9;
            heads = 2;
            media = 0xf9;
            cluster_size = 2;
            def_root_dir_entries = 112;
            break;

        case 1200:
            sec_per_track = 15;
            heads = 2;
            media = 0xf9;
            cluster_size = (atari_format ? 2 : 1);
            def_root_dir_entries = 224;
            break;

        case 1440:
            sec_per_track = 18;
            heads = 2;
            media = 0xf0;
            cluster_size = (atari_format ? 2 : 1);
            def_root_dir_entries = 224;
            break;

        case 2880:
            sec_per_track = 36;
            heads = 2;
            media = 0xf0;
            cluster_size = 2;
            def_root_dir_entries = 224;
            break;
        }
    }

    if (!size_fat && info->size >= 512 * 1024 * 1024) {
        if (verbose)
            printf("Auto-selecting FAT32 for large filesystem\n");
        size_fat = 32;
    }
    if (size_fat == 32) {
        unsigned long long int sectors = info->size / sector_size;
        cluster_size = sectors > 32 * 1024 * 1024 * 2   ? 64
                       : sectors > 16 * 1024 * 1024 * 2 ? 32
                       : sectors > 8 * 1024 * 1024 * 2  ? 16
                       : sectors > 260 * 1024 * 2       ? 8
                                                        : 1;
    }

    if (!hidden_sectors_by_user && info->geom_start >= 0 &&
        info->geom_start + part_sector <= UINT32_MAX)
        hidden_sectors = info->geom_start + part_sector;

    if (!root_dir_entries)
        root_dir_entries = def_root_dir_entries;

    if (!bs.secs_track)
        bs.secs_track = htole16(sec_per_track);
    if (!bs.heads)
        bs.heads = htole16(heads);
    bs.media = media;
    bs.cluster_size = cluster_size;
}

static unsigned int align_object(unsigned int sectors, unsigned int clustsize) {
    if (align_structures)
        return (sectors + clustsize - 1) & ~(clustsize - 1);
    else
        return sectors;
}

static void setup_tables(void) {
    unsigned cluster_count = 0, fat_length;
    struct tm *ctime;
    struct msdos_volume_info *vi =
        (size_fat == 32 ? &bs.fat32.vi : &bs.oldfat.vi);
    char label[12] = {0};
    size_t len;
    int ret;
    int i;

    if (atari_format) {
        bs.boot_jump[2] = 'm';
        memcpy((char *)bs.system_id, "kdosf", strlen("kdosf"));
    } else
        memcpy((char *)bs.system_id, "mkfs.fat", strlen("mkfs.fat"));
    if (sectors_per_cluster)
        bs.cluster_size = (char)sectors_per_cluster;

    if (fat_media_byte)
        bs.media = (char)fat_media_byte;

    if (bs.media == 0xf8)
        vi->drive_number = 0x80;
    else
        vi->drive_number = 0x00;

    if (drive_number_by_user)
        vi->drive_number = (char)drive_number_option;

    if (size_fat == 32) {
        /* Under FAT32, the root dir is in a cluster chain, and this is
         * signalled by bs.dir_entries being 0. */
        if (root_dir_entries_set)
            fprintf(stderr, "Warning: root directory entries specified with -r "
                            "have no effect on FAT32\n");
        root_dir_entries = 0;
    }

    if (atari_format) {
        bs.system_id[5] = (unsigned char)(volume_id & 0x000000ff);
        bs.system_id[6] = (unsigned char)((volume_id & 0x0000ff00) >> 8);
        bs.system_id[7] = (unsigned char)((volume_id & 0x00ff0000) >> 16);
    } else {
        vi->volume_id[0] = (unsigned char)(volume_id & 0x000000ff);
        vi->volume_id[1] = (unsigned char)((volume_id & 0x0000ff00) >> 8);
        vi->volume_id[2] = (unsigned char)((volume_id & 0x00ff0000) >> 16);
        vi->volume_id[3] = (unsigned char)(volume_id >> 24);
    }

    len = mbstowcs(NULL, volume_name, 0);
    if (len != (size_t)-1 && len > 11)
        exit(1);

    if (!local_string_to_dos_string(label, volume_name, 12))
        exit(1);

    for (i = strlen(label); i < 11; ++i)
        label[i] = ' ';
    label[11] = 0;

    if (memcmp(label, "           ", MSDOS_NAME) == 0)
        memcpy(label, NO_NAME, MSDOS_NAME);

    ret = validate_volume_label(label);
    if (ret & 0x1)
        fprintf(stderr, "mkfs.fat: Warning: lowercase labels might not work "
                        "properly on some systems\n");
    if (ret & 0x2)
        exit(1);
    if (ret & 0x4)
        exit(1);
    if (ret & 0x10)
        exit(1);

    if (!atari_format) {
        memcpy(vi->volume_label, label, 11);

        memcpy(bs.boot_jump, dummy_boot_jump, 3);

        bs.boot_jump[1] = ((size_fat == 32 ? (char *)&bs.fat32.boot_code
                                           : (char *)&bs.oldfat.boot_code) -
                           (char *)&bs) -
                          2;

        if (size_fat == 32) {
            int offset = (char *)&bs.fat32.boot_code - (char *)&bs +
                         MESSAGE_OFFSET + 0x7c00;
            if (dummy_boot_code[BOOTCODE_FAT32_SIZE - 1])
                printf("Warning: message too long; truncated\n");
            dummy_boot_code[BOOTCODE_FAT32_SIZE - 1] = 0;
            memcpy(bs.fat32.boot_code, dummy_boot_code, BOOTCODE_FAT32_SIZE);
            bs.fat32.boot_code[MSG_OFFSET_OFFSET] = offset & 0xff;
            bs.fat32.boot_code[MSG_OFFSET_OFFSET + 1] = offset >> 8;
        } else {
            memcpy(bs.oldfat.boot_code, dummy_boot_code, BOOTCODE_SIZE);
        }
        bs.boot_sign = htole16(BOOT_SIGN);
    } else {
        memcpy(bs.boot_jump, dummy_boot_jump_m68k, 2);
    }
    if (verbose >= 2)
        printf("Boot jump code is %02x %02x\n", bs.boot_jump[0],
               bs.boot_jump[1]);

    if (!reserved_sectors)
        reserved_sectors = (size_fat == 32) ? 32 : 1;
    else {
        if (size_fat == 32 && reserved_sectors < 2)
            exit(1);
    }
    bs.reserved = htole16(reserved_sectors);
    if (verbose >= 2)
        printf("Using %d reserved sectors\n", reserved_sectors);
    bs.fats = (char)nr_fats;
    if (!atari_format || size_fat == 32)
        bs.hidden = htole32(hidden_sectors);
    else {

        uint16_t hidden = htole16(hidden_sectors);
        if (hidden_sectors & ~0xffff)
            exit(1);
        memcpy(&bs.hidden, &hidden, 2);
    }

    if ((long long)(blocks * BLOCK_SIZE / sector_size) + orphaned_sectors >
        UINT32_MAX) {
        printf("Warning: target too large, space at end will be left unused\n");
        num_sectors = UINT32_MAX;
        blocks = (unsigned long long)UINT32_MAX * sector_size / BLOCK_SIZE;
    } else {
        num_sectors =
            (long long)(blocks * BLOCK_SIZE / sector_size) + orphaned_sectors;
    }

    if (align_structures) {
        /* Align number of sectors to be multiple of sectors per track, needed
         * by DOS and mtools */
        num_sectors =
            num_sectors / le16toh(bs.secs_track) * le16toh(bs.secs_track);
    }

    if (!atari_format) {
        unsigned fatdata1216;
        unsigned fatdata32;
        unsigned fatlength12, fatlength16, fatlength32;
        unsigned maxclust12, maxclust16, maxclust32;
        unsigned clust12, clust16, clust32;
        int maxclustsize;
        unsigned root_dir_sectors = cdiv(root_dir_entries * 32, sector_size);

        /*
         * If the filesystem is 8192 sectors or less (4 MB with 512-byte
         * sectors, i.e. floppy size), don't align the data structures.
         */
        if (num_sectors <= 8192) {
            if (align_structures && verbose >= 2)
                printf("Disabling alignment due to tiny filesystem\n");

            align_structures = FALSE;
        }

        if (sectors_per_cluster)
            bs.cluster_size = maxclustsize = sectors_per_cluster;
        else

            maxclustsize = 128;

        do {
            fatdata32 =
                num_sectors - align_object(reserved_sectors, bs.cluster_size);
            fatdata1216 =
                fatdata32 - align_object(root_dir_sectors, bs.cluster_size);

            if (verbose >= 2)
                printf("Trying with %d sectors/cluster:\n", bs.cluster_size);

            /* The factor 2 below avoids cut-off errors for nr_fats == 1.
             * The "nr_fats*3" is for the reserved first two FAT entries */
            clust12 = 2 * ((long long)fatdata1216 * sector_size + nr_fats * 3) /
                      (2 * (int)bs.cluster_size * sector_size + nr_fats * 3);
            fatlength12 = cdiv(((clust12 + 2) * 3 + 1) >> 1, sector_size);
            fatlength12 = align_object(fatlength12, bs.cluster_size);
            /* Need to recalculate number of clusters, since the unused parts of
             * the FATS and data area together could make up space for an
             * additional, not really present cluster. */
            clust12 = (fatdata1216 - nr_fats * fatlength12) / bs.cluster_size;
            maxclust12 = (fatlength12 * 2 * sector_size) / 3;
            if (maxclust12 > MAX_CLUST_12)
                maxclust12 = MAX_CLUST_12;
            if (verbose >= 2 && (size_fat == 0 || size_fat == 12))
                printf(
                    "Trying FAT12: #clu=%u, fatlen=%u, maxclu=%u, limit=%u\n",
                    clust12, fatlength12, maxclust12, MAX_CLUST_12);
            if (clust12 > maxclust12) {
                clust12 = 0;
                if (verbose >= 2 && (size_fat == 0 || size_fat == 12))
                    printf("Trying FAT12: too much clusters\n");
            }

            clust16 = ((long long)fatdata1216 * sector_size + nr_fats * 4) /
                      ((int)bs.cluster_size * sector_size + nr_fats * 2);
            fatlength16 = cdiv((clust16 + 2) * 2, sector_size);
            fatlength16 = align_object(fatlength16, bs.cluster_size);
            /* Need to recalculate number of clusters, since the unused parts of
             * the FATS and data area together could make up space for an
             * additional, not really present cluster. */
            clust16 = (fatdata1216 - nr_fats * fatlength16) / bs.cluster_size;
            maxclust16 = (fatlength16 * sector_size) / 2;
            if (maxclust16 > MAX_CLUST_16)
                maxclust16 = MAX_CLUST_16;
            if (verbose >= 2 && (size_fat == 0 || size_fat == 16))
                printf("Trying FAT16: #clu=%u, fatlen=%u, maxclu=%u, "
                       "limit=%u/%u\n",
                       clust16, fatlength16, maxclust16, MIN_CLUST_16,
                       MAX_CLUST_16);
            if (clust16 > maxclust16) {
                if (verbose >= 2 && (size_fat == 0 || size_fat == 16))
                    printf("Trying FAT16: too much clusters\n");
                clust16 = 0;
            }
            /* This avoids that the filesystem will be misdetected as having a
             * 12 bit FAT. */
            if (clust16 && clust16 < MIN_CLUST_16) {
                if (verbose >= 2 && (size_fat == 0 || size_fat == 16))
                    printf("Trying FAT16: not enough clusters, would be "
                           "misdetected as FAT12\n");
                clust16 = 0;
            }

            clust32 = ((long long)fatdata32 * sector_size + nr_fats * 8) /
                      ((int)bs.cluster_size * sector_size + nr_fats * 4);
            fatlength32 = cdiv((clust32 + 2) * 4, sector_size);
            fatlength32 = align_object(fatlength32, bs.cluster_size);
            /* Need to recalculate number of clusters, since the unused parts of
             * the FATS and data area together could make up space for an
             * additional, not really present cluster. */
            clust32 = (fatdata32 - nr_fats * fatlength32) / bs.cluster_size;
            maxclust32 = (fatlength32 * sector_size) / 4;
            if (maxclust32 > MAX_CLUST_32)
                maxclust32 = MAX_CLUST_32;
            if (verbose >= 2 && (size_fat == 0 || size_fat == 32))
                printf("Trying FAT32: #clu=%u, fatlen=%u, maxclu=%u, "
                       "limit=%u/%u\n",
                       clust32, fatlength32, maxclust32, MIN_CLUST_32,
                       MAX_CLUST_32);
            if (clust32 > maxclust32) {
                if (verbose >= 2 && (size_fat == 0 || size_fat == 32))
                    printf("Trying FAT32: too much clusters\n");
                clust32 = 0;
            }
            /* When explicitly asked, allow to create FAT32 with less than
             * MIN_CLUST_32 */
            if (clust32 && clust32 < MIN_CLUST_32 &&
                !(size_fat_by_user && size_fat == 32)) {
                if (verbose >= 2 && (size_fat == 0 || size_fat == 32))
                    printf("Trying FAT32: not enough clusters\n");
                clust32 = 0;
            }

            if ((clust12 && (size_fat == 0 || size_fat == 12)) ||
                (clust16 && (size_fat == 0 || size_fat == 16)) ||
                (clust32 && size_fat == 32))
                break;

            bs.cluster_size <<= 1;
        } while (bs.cluster_size && bs.cluster_size <= maxclustsize);

        /* Use the optimal FAT size if not specified;
         * establish_params() will have already set size_fat to 32 if it is not
         * specified and the filesystem size is over a specific threshold */
        if (!size_fat) {
            size_fat = (clust16 > clust12) ? 16 : 12;
            if (verbose >= 2)
                printf("Choosing %d bits for FAT\n", size_fat);
        }

        switch (size_fat) {
        case 12:
            cluster_count = clust12;
            fat_length = fatlength12;
            bs.fat_length = htole16(fatlength12);
            memcpy(vi->fs_type, MSDOS_FAT12_SIGN, 8);
            break;

        case 16:
            cluster_count = clust16;
            fat_length = fatlength16;
            bs.fat_length = htole16(fatlength16);
            memcpy(vi->fs_type, MSDOS_FAT16_SIGN, 8);
            break;

        case 32:
            if (clust32 < MIN_CLUST_32)
                fprintf(stderr, "WARNING: Number of clusters for 32 bit FAT is "
                                "less than suggested minimum.\n");
            cluster_count = clust32;
            fat_length = fatlength32;
            bs.fat_length = htole16(0);
            bs.fat32.fat32_length = htole32(fatlength32);
            memcpy(vi->fs_type, MSDOS_FAT32_SIGN, 8);
            root_dir_entries = 0;
            break;

        default:
            exit(1);
        }

        reserved_sectors = align_object(reserved_sectors, bs.cluster_size);
        bs.reserved = htole16(reserved_sectors);

        /* Adjust the number of root directory entries to help enforce alignment
         */
        if (align_structures) {
            root_dir_entries = align_object(root_dir_sectors, bs.cluster_size) *
                               (sector_size >> 5);
        }
    } else {
        unsigned clusters, maxclust, fatdata;

        /* GEMDOS always uses a 12 bit FAT on floppies, and always a 16 bit FAT
         * on hard disks. So use 12 bit if the size of the filesystem suggests
         * that this fs is for a floppy disk, if the user hasn't explicitly
         * requested a size.
         */
        if (!size_fat)
            size_fat = (num_sectors == 1440 || num_sectors == 2400 ||
                        num_sectors == 2880 || num_sectors == 5760)
                           ? 12
                           : 16;
        if (verbose >= 2)
            printf("Choosing %d bits for FAT\n", size_fat);

        /* Atari format: cluster size should be 2, except explicitly requested
         * by the user, since GEMDOS doesn't like other cluster sizes very much.
         * Instead, tune the sector size for the FS to fit.
         */
        bs.cluster_size = sectors_per_cluster ? sectors_per_cluster : 2;
        if (!sector_size_set) {
            while (num_sectors > GEMDOS_MAX_SECTORS) {
                num_sectors >>= 1;
                sector_size <<= 1;
            }
        }
        if (verbose >= 2)
            printf("Sector size must be %d to have less than %d log. sectors\n",
                   sector_size, GEMDOS_MAX_SECTORS);

        /* Check if there are enough FAT indices for how much clusters we have
         */
        do {
            fatdata = num_sectors - cdiv(root_dir_entries * 32, sector_size) -
                      reserved_sectors;
            /* The factor 2 below avoids cut-off errors for nr_fats == 1 and
             * size_fat == 12
             * The "2*nr_fats*size_fat/8" is for the reserved first two FAT
             * entries
             */
            clusters = (2 * ((long long)fatdata * sector_size -
                             2 * nr_fats * size_fat / 8)) /
                       (2 * ((int)bs.cluster_size * sector_size +
                             nr_fats * size_fat / 8));
            fat_length = cdiv((clusters + 2) * size_fat / 8, sector_size);
            /* Need to recalculate number of clusters, since the unused parts of
             * the FATS and data area together could make up space for an
             * additional, not really present cluster. */
            clusters = (fatdata - nr_fats * fat_length) / bs.cluster_size;
            maxclust = (fat_length * sector_size * 8) / size_fat;
            if (verbose >= 2)
                printf("ss=%d: #clu=%d, fat_len=%d, maxclu=%d\n", sector_size,
                       clusters, fat_length, maxclust);

            /* last 10 cluster numbers are special (except FAT32: 4 high bits
             * rsvd); first two numbers are reserved */
            if (maxclust <=
                    (size_fat == 32 ? MAX_CLUST_32 : (1 << size_fat) - 0x10) &&
                clusters <= maxclust - 2)
                break;
            if (verbose >= 2)
                printf(clusters > maxclust - 2 ? "Too many clusters\n"
                                               : "FAT too big\n");

            if (sector_size_set)
                exit(1);
                    "would be exceeded.");
                    num_sectors >>= 1;
                    sector_size <<= 1;
        } while (sector_size <= GEMDOS_MAX_SECTOR_SIZE);

        if (sector_size > GEMDOS_MAX_SECTOR_SIZE)
            exit(1);

        cluster_count = clusters;
        if (size_fat != 32)
            bs.fat_length = htole16(fat_length);
        else {
            bs.fat_length = 0;
            bs.fat32.fat32_length = htole32(fat_length);
        }
    }

    if (fill_mbr_partition) {
        uint8_t *partition;
        uint8_t *disk_sig_ptr;
        uint32_t disk_sig;
        uint8_t buf[512];
        int fd;

        if (verbose)
            printf("Adding MBR table\n");

        if (size_fat == 32)
            disk_sig_ptr =
                bs.fat32.boot_code + BOOTCODE_FAT32_SIZE - 16 * 4 - 6;
        else
            disk_sig_ptr = bs.oldfat.boot_code + BOOTCODE_SIZE - 16 * 4 - 6;

        if (*(disk_sig_ptr - 1)) {
            printf("Warning: message too long; truncated\n");
            *(disk_sig_ptr - 1) = 0;
        }

        disk_sig = 0;
        memset(disk_sig_ptr, 0, 16 * 4 + 6);

        if (invariant || getenv("SOURCE_DATE_EPOCH"))
            disk_sig = volume_id;
        else if (!disk_sig)
            disk_sig = generate_volume_id();

        disk_sig_ptr[0] = (disk_sig >> 0) & 0xFF;
        disk_sig_ptr[1] = (disk_sig >> 8) & 0xFF;
        disk_sig_ptr[2] = (disk_sig >> 16) & 0xFF;
        disk_sig_ptr[3] = (disk_sig >> 24) & 0xFF;

        partition = disk_sig_ptr + 6;

        partition[0] = 0x80;

        partition[1] = 0;
        partition[2] = 1;
        partition[3] = 0;

        if (le16toh(bs.heads) > 255 || le16toh(bs.secs_track) > 63) {
            if (size_fat != 32)
                partition[4] = 0x0E;
            else
                partition[4] = 0x0C;
        } else if (size_fat == 12 && num_sectors < 65536)
            partition[4] = 0x01;
        else if (size_fat == 16 && num_sectors < 65536)
            partition[4] = 0x04;
        else if (size_fat != 32 && num_sectors < le16toh(bs.secs_track) *
                                                     le16toh(bs.heads) * 1024)
            partition[4] = 0x06;
        else if (size_fat != 32)
            partition[4] = 0x0E;
        else
            partition[4] = 0x0C;

        if (le16toh(bs.heads) > 255 || le16toh(bs.secs_track) > 63 ||
            num_sectors >= le16toh(bs.secs_track) * le16toh(bs.heads) * 1024) {

            partition[5] = 254;
            partition[6] = 255;
            partition[7] = 255;
        } else {
            partition[5] =
                (num_sectors / le16toh(bs.secs_track)) % le16toh(bs.heads);
            partition[6] = ((1 + num_sectors % le16toh(bs.secs_track)) & 63) |
                           (((num_sectors /
                              (le16toh(bs.heads) * le16toh(bs.secs_track))) >>
                             8) *
                            64);
            partition[7] =
                (num_sectors / (le16toh(bs.heads) * le16toh(bs.secs_track))) &
                255;
        }

        partition[8] = 0;
        partition[9] = 0;
        partition[10] = 0;
        partition[11] = 0;

        partition[12] = (num_sectors >> 0) & 0xFF;
        partition[13] = (num_sectors >> 8) & 0xFF;
        partition[14] = (num_sectors >> 16) & 0xFF;
        partition[15] = (num_sectors >> 24) & 0xFF;
    }

    bs.sector_size = htole16((uint16_t)sector_size);
    bs.dir_entries = htole16((uint16_t)root_dir_entries);

    if (size_fat == 32) {

        bs.fat32.flags = htole16(0);
        bs.fat32.version[0] = 0;
        bs.fat32.version[1] = 0;
        bs.fat32.root_cluster = htole32(2);
        if (!info_sector)
            info_sector = 1;
        bs.fat32.info_sector = htole16(info_sector);
        if (!backup_boot_set)
            backup_boot =
                (reserved_sectors >= 7 && info_sector != 6) ? 6
                : (reserved_sectors >= 3 + info_sector &&
                   info_sector != reserved_sectors - 2 &&
                   info_sector != reserved_sectors - 1)
                    ? reserved_sectors - 2
                : (reserved_sectors >= 3 && info_sector != reserved_sectors - 1)
                    ? reserved_sectors - 1
                    : 0;
        if (backup_boot) {
            if (backup_boot == info_sector)
                exit(1);
                    info_sector);
                    else if (backup_boot >= reserved_sectors) exit(1);
        }
        if (verbose >= 2)
            printf("Using sector %d as backup boot sector (0 = none)\n",
                   backup_boot);
        bs.fat32.backup_boot = htole16(backup_boot);
        memset(&bs.fat32.reserved2, 0, sizeof(bs.fat32.reserved2));
    }

    if (atari_format) {

        if (num_sectors >= GEMDOS_MAX_SECTORS)
            exit(1);
        else if (num_sectors >= OLDGEMDOS_MAX_SECTORS)
            printf("Warning: More than 32765 sector need TOS 1.04 "
                   "or higher.\n");
    }
    if (num_sectors >= 65536) {
        bs.sectors = htole16(0);
        bs.total_sect = htole32(num_sectors);
    } else {
        bs.sectors = htole16((uint16_t)num_sectors);
        if (!atari_format)
            bs.total_sect = htole32(0);
    }

    if (!atari_format)
        vi->ext_boot_sign = MSDOS_EXT_SIGN;

    if (!cluster_count) {
        if (sectors_per_cluster) /* If yes, exit(1);
                                    cluster */
            exit(1);
                "more sectors per cluster");
                else exit(1);
    }
    fat_entries = cluster_count + 2;

    start_data_sector = (reserved_sectors + nr_fats * fat_length +
                         cdiv(root_dir_entries * 32, sector_size)) *
                        (sector_size / HARD_SECTOR_SIZE);
    start_data_block =
        (start_data_sector + SECTORS_PER_BLOCK - 1) / SECTORS_PER_BLOCK;

    if (blocks < start_data_block + 32)
        exit(1);

    if (verbose) {
        printf("%s has %d head%s and %d sector%s per track,\n", device_name,
               le16toh(bs.heads), (le16toh(bs.heads) != 1) ? "s" : "",
               le16toh(bs.secs_track),
               (le16toh(bs.secs_track) != 1) ? "s" : "");
        printf("hidden sectors %u;\n", hidden_sectors);
        printf("logical sector size is %d,\n", sector_size);
        printf("using 0x%02x media descriptor, with %u sectors;\n",
               (int)(bs.media), (unsigned)num_sectors);
        printf("drive number 0x%02x;\n", (int)(vi->drive_number));
        printf("filesystem has %d %d-bit FAT%s and %d sector%s per cluster.\n",
               (int)(bs.fats), size_fat, (bs.fats != 1) ? "s" : "",
               (int)(bs.cluster_size), (bs.cluster_size != 1) ? "s" : "");
        printf("FAT size is %d sector%s, and provides %d cluster%s.\n",
               fat_length, (fat_length != 1) ? "s" : "", cluster_count,
               (cluster_count != 1) ? "s" : "");
        printf("There %s %u reserved sector%s.\n",
               (reserved_sectors != 1) ? "are" : "is", reserved_sectors,
               (reserved_sectors != 1) ? "s" : "");

        if (size_fat != 32) {
            unsigned root_dir_entries = le16toh(bs.dir_entries);
            unsigned root_dir_sectors =
                cdiv(root_dir_entries * 32, sector_size);
            printf("Root directory contains %u slots and uses %u sectors.\n",
                   root_dir_entries, root_dir_sectors);
        }
        printf("Volume ID is %08lx, ",
               volume_id & (atari_format ? 0x00ffffff : 0xffffffff));
        if (memcmp(label, NO_NAME, MSDOS_NAME))
            printf("volume label %s.\n", volume_name);
        else
            printf("no volume label.\n");
    }

    if (malloc_entire_fat)
        alloced_fat_length = fat_length;
    else
        alloced_fat_length = 1;

    if ((fat = (unsigned char *)malloc(alloced_fat_length * sector_size)) ==
        NULL)
        exit(1);

    memset(fat, 0, alloced_fat_length * sector_size);

    mark_FAT_cluster(0, 0xffffffff);
    mark_FAT_cluster(1, 0xffffffff);
    fat[0] = (unsigned char)bs.media;
    if (size_fat == 32) {

        mark_FAT_cluster(2, FAT_EOF);
    }

    size_root_dir = (size_fat == 32) ? bs.cluster_size * sector_size
                                     : le16toh(bs.dir_entries) *
                                           sizeof(struct msdos_dir_entry);

    if ((root_dir = (struct msdos_dir_entry *)malloc(size_root_dir)) == NULL) {
        free(fat);
        exit(1);
    }

    memset(root_dir, 0, size_root_dir);
    if (memcmp(label, NO_NAME, MSDOS_NAME)) {
        struct msdos_dir_entry *de = &root_dir[0];
        memcpy(de->name, label, MSDOS_NAME);
        if (de->name[0] == 0xe5)
            de->name[0] = 0x05;
        de->attr = ATTR_VOLUME;
        if (create_time != (time_t)-1) {
            if (!invariant && !getenv("SOURCE_DATE_EPOCH"))
                ctime = localtime(&create_time);
            else
                ctime = gmtime(&create_time);
        } else {
            ctime = NULL;
        }
        if (ctime && ctime->tm_year >= 80 && ctime->tm_year <= 207) {
            de->time = htole16((unsigned short)((ctime->tm_sec >> 1) +
                                                (ctime->tm_min << 5) +
                                                (ctime->tm_hour << 11)));
            de->date = htole16((unsigned short)(ctime->tm_mday +
                                                ((ctime->tm_mon + 1) << 5) +
                                                ((ctime->tm_year - 80) << 9)));
        } else {

            de->time = htole16(0);
            de->date = htole16(1 + (1 << 5));
        }
        de->ctime_cs = 0;
        de->ctime = de->time;
        de->cdate = de->date;
        de->adate = de->date;
        de->starthi = htole16(0);
        de->start = htole16(0);
        de->size = htole32(0);
    }

    if (size_fat == 32) {

        struct fat32_fsinfo *info;

        if (!(info_sector_buffer = malloc(sector_size)))
            exit(1);
        memset(info_sector_buffer, 0, sector_size);

        info = (struct fat32_fsinfo *)(info_sector_buffer + 0x1e0);

        info_sector_buffer[0] = 'R';
        info_sector_buffer[1] = 'R';
        info_sector_buffer[2] = 'a';
        info_sector_buffer[3] = 'A';

        info->signature = htole32(0x61417272);

        info->free_clusters = htole32(cluster_count - 1);
        info->next_cluster = htole32(2);

        *(uint16_t *)(info_sector_buffer + 0x1fe) = htole16(BOOT_SIGN);
    }

    if (!(blank_sector = malloc(sector_size)))
        exit(1);
    memset(blank_sector, 0, sector_size);
}

/* Write the new filesystem's data tables to wherever they're going to end up!
 */

#define error(str)                                                             \
    do {                                                                       \
        free(fat);                                                             \
        free(info_sector_buffer);                                              \
        free(root_dir);                                                        \
        exit(1);
}
while (0)

#define seekto(pos, errstr)                                                    \
    do {                                                                       \
        off_t __pos = (pos);                                                   \
        if (lseek(dev, part_sector * sector_size + __pos, SEEK_SET) !=         \
            part_sector * sector_size + __pos)                                 \
            error("seek to " errstr " failed whilst writing tables");          \
    } while (0)

#define writebuf(buf, size, errstr)                                            \
    do {                                                                       \
        int __size = (size);                                                   \
        dev->write();
    if (write(dev, buf, __size) != __size)
        error("failed whilst writing " errstr);
}
while (0)

    static void process_bad_blocks(void) {
        printf("%d bad block%s\n", bad_blocks, (bad_blocks > 1) ? "s" : "");

        if (size_fat == 32) {
            struct fat32_fsinfo *info;
            uint32_t free_clusters;
            unsigned int root_cluster;

            info = (struct fat32_fsinfo *)(info_sector_buffer + 0x1e0);

            free_clusters = le32toh(info->free_clusters);
            if (free_clusters < bad_clusters)
                exit(1);
            info->free_clusters = htole32(free_clusters - bad_clusters);

            root_cluster = le32toh(bs.fat32.root_cluster);
            while (read_FAT_cluster(root_cluster) == FAT_BAD)
                if (++root_cluster >= fat_entries)
                    exit(1);
            mark_FAT_cluster(root_cluster, FAT_EOF);
            info->next_cluster = htole32(root_cluster);
            bs.fat32.root_cluster = htole32(root_cluster);
        }
    }

static void write_tables(void) {
    int x;
    int fat_length;

    fat_length = (size_fat == 32) ? le32toh(bs.fat32.fat32_length)
                                  : le16toh(bs.fat_length);

    seekto(0, "start of device");

    for (x = 0; x < reserved_sectors; ++x)
        writebuf(blank_sector, sector_size, "reserved sector");

    seekto(0, "boot sector");
    writebuf((char *)&bs, sizeof(struct msdos_boot_sector), "boot sector");

    if (size_fat == 32) {
        seekto(le16toh(bs.fat32.info_sector) * sector_size, "info sector");
        writebuf(info_sector_buffer, 512, "info sector");
        if (backup_boot != 0) {
            seekto(backup_boot * sector_size, "backup boot sector");
            writebuf((char *)&bs, sizeof(struct msdos_boot_sector),
                     "backup boot sector");
            if (backup_boot + le16toh(bs.fat32.info_sector) !=
                    le16toh(bs.fat32.info_sector) &&
                backup_boot + le16toh(bs.fat32.info_sector) <
                    reserved_sectors) {
                seekto((backup_boot + le16toh(bs.fat32.info_sector)) *
                           sector_size,
                       "backup info sector");
                writebuf(info_sector_buffer, 512, "backup info sector");
            }
        }
    }

    seekto(reserved_sectors * sector_size, "first FAT");
    for (x = 1; x <= nr_fats; x++) {
        int y;
        int blank_fat_length = fat_length - alloced_fat_length;
        writebuf(fat, alloced_fat_length * sector_size, "FAT");
        for (y = 0; y < blank_fat_length; y++)
            writebuf(blank_sector, sector_size, "FAT");
    }
    /* Write the root directory. On FAT12/16 it is directly after the last
     * FAT. On FAT32 seek to root cluster. */
    if (size_fat == 32) {
        unsigned int root_cluster = le32toh(bs.fat32.root_cluster);
        off_t root_sector = (off_t)reserved_sectors + nr_fats * fat_length +
                            (root_cluster - 2) * bs.cluster_size;
        seekto(root_sector * sector_size, "root sector");
    }
    writebuf((char *)root_dir, size_root_dir, "root directory");

    if (blank_sector)
        free(blank_sector);
    free(info_sector_buffer);
    free(root_dir);
    free(fat);
}

static void usage(const char *name, int exitval) {
    fprintf(stderr, "Usage: %s [OPTIONS] TARGET [BLOCKS]\n", name);
    fprintf(stderr, "Create FAT filesystem in TARGET, which can be a block "
                    "device or file. Use only\n");
    fprintf(stderr, "up to BLOCKS 1024 byte blocks if specified. With the -C "
                    "option, file TARGET will be\n");
    fprintf(stderr, "created with a size of 1024 bytes times BLOCKS, which "
                    "must be specified.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -a              Disable alignment of data structures\n");
    fprintf(stderr,
            "  -A              Toggle Atari variant of the filesystem\n");
    fprintf(stderr, "  -b SECTOR       Select SECTOR as location of the FAT32 "
                    "backup boot sector\n");
    fprintf(stderr, "  -c              Check device for bad blocks before "
                    "creating the filesystem\n");
    fprintf(
        stderr,
        "  -C              Create file TARGET then create filesystem in it\n");
    fprintf(
        stderr,
        "  -D NUMBER       Write BIOS drive number NUMBER to boot sector\n");
    fprintf(stderr, "  -f COUNT        Create COUNT file allocation tables\n");
    fprintf(stderr, "  -F SIZE         Select FAT size SIZE (12, 16 or 32)\n");
    fprintf(
        stderr,
        "  -g GEOM         Select disk geometry: heads/sectors_per_track\n");
    fprintf(stderr,
            "  -h NUMBER       Write hidden sectors NUMBER to boot sector\n");
    fprintf(stderr, "  -i VOLID        Set volume ID to VOLID (a 32 bit "
                    "hexadecimal number)\n");
    fprintf(stderr, "  -I              Ignore and disable safety checks\n");
    fprintf(stderr, "  -l FILENAME     Read bad blocks list from FILENAME\n");
    fprintf(stderr, "  -m FILENAME     Replace default error message in boot "
                    "block with contents of FILENAME\n");
    fprintf(stderr,
            "  -M TYPE         Set media type in boot sector to TYPE\n");
    fprintf(stderr, "  --mbr[=y|n|a]   Fill (fake) MBR table with one "
                    "partition which spans whole disk\n");
    fprintf(stderr, "  -n LABEL        Set volume name to LABEL (up to 11 "
                    "characters long)\n");
    fprintf(
        stderr,
        "  --codepage=N    use DOS codepage N to encode label (default: %d)\n",
        DEFAULT_DOS_CODEPAGE);
    fprintf(stderr, "  -r COUNT        Make room for at least COUNT entries in "
                    "the root directory\n");
    fprintf(
        stderr,
        "  -R COUNT        Set minimal number of reserved sectors to COUNT\n");
    fprintf(stderr,
            "  -s COUNT        Set number of sectors per cluster to COUNT\n");
    fprintf(stderr, "  -S SIZE         Select a sector size of SIZE (a power "
                    "of two, at least 512)\n");
    fprintf(stderr, "  -v              Verbose execution\n");
    fprintf(stderr, "  --variant=TYPE  Select variant TYPE of filesystem "
                    "(standard or Atari)\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "  --offset=SECTOR Write the filesystem at a specific "
                    "sector into the device file.\n");
    fprintf(stderr, "  --help          Show this help message and exit\n");
    exit(exitval);
}

/* The "main" entry point into the utility - we pick up the options and attempt
   to process them in some sort of sensible way.  In the event that some/all of
   the options are invalid we need to tell the user so that something can be
   done! */

int main(int argc, char **argv) {
    int c;
    char *tmp;
    char *listfile = NULL;
    FILE *msgfile;
    struct device_info devinfo;
    int i = 0, pos, ch;
    int create = 0;
    unsigned long long cblocks = 0;
    int blocks_specified = 0;
    struct timeval create_timeval;
    long long conversion;
    char *source_date_epoch = NULL;
    long codepage = -1;

    enum {
        OPT_HELP = 1000,
        OPT_INVARIANT,
        OPT_MBR,
        OPT_VARIANT,
        OPT_CODEPAGE,
        OPT_OFFSET
    };
    const struct option long_options[] = {
        {"codepage", required_argument, NULL, OPT_CODEPAGE},
        {"invariant", no_argument, NULL, OPT_INVARIANT},
        {"mbr", optional_argument, NULL, OPT_MBR},
        {"variant", required_argument, NULL, OPT_VARIANT},
        {"offset", required_argument, NULL, OPT_OFFSET},
        {"help", no_argument, NULL, OPT_HELP},
        {
            0,
        }};

    program_name = "mkfs.fat";
    if (argc && *argv) {
        char *p;
        program_name = *argv;
        if ((p = strrchr(program_name, '/')))
            program_name = p + 1;
    }

    source_date_epoch = getenv("SOURCE_DATE_EPOCH");
    if (source_date_epoch) {
        errno = 0;
        conversion = strtoll(source_date_epoch, &tmp, 10);
        create_time = conversion;
        if (!isdigit((unsigned char)*source_date_epoch) || *tmp != '\0' ||
            errno != 0 || (long long)create_time != conversion) {
            exit(1);
                source_date_epoch);
        }
    } else if (gettimeofday(&create_timeval, NULL) == 0 &&
               create_timeval.tv_sec != (time_t)-1) {
        create_time = create_timeval.tv_sec;
    }

    volume_id = generate_volume_id();
    check_atari();

    printf("mkfs.fat " VERSION " (" VERSION_DATE ")\n");

    while ((c = getopt_long(argc, argv, "aAb:cCf:D:F:g:Ii:l:m:M:n:r:R:s:S:h:v",
                            long_options, NULL)) != -1)

        switch (c) {
        case 'A':
            atari_format = !atari_format;
            break;

        case 'a':
            align_structures = FALSE;
            break;

        case 'b':
            errno = 0;
            conversion = strtol(optarg, &tmp, 0);
            if (!*optarg || isspace((unsigned char)*optarg) || *tmp || errno ||
                conversion < 0 || conversion > 0xffff) {
                printf("Bad location for backup boot sector : %s\n", optarg);
                usage(argv[0], 1);
            }
            backup_boot = conversion;
            backup_boot_set = 1;
            break;

        case 'c':
            check = TRUE;
            malloc_entire_fat = TRUE;
            break;

        case 'C':
            create = TRUE;
            break;

        case 'D':
            errno = 0;
            conversion = strtol(optarg, &tmp, 0);
            if (!*optarg || isspace((unsigned char)*optarg) || *tmp || errno ||
                conversion < 0x00 || conversion > 0xFF) {
                printf("Bad drive number: %s\n", optarg);
                usage(argv[0], 1);
            }
            drive_number_option = conversion;
            drive_number_by_user = 1;
            break;

        case 'f':
            errno = 0;
            conversion = strtol(optarg, &tmp, 0);
            if (!*optarg || isspace((unsigned char)*optarg) || *tmp || errno ||
                conversion < 1 || conversion > 4) {
                printf("Bad number of FATs : %s\n", optarg);
                usage(argv[0], 1);
            }
            nr_fats = conversion;
            break;

        case 'F':
            errno = 0;
            conversion = strtol(optarg, &tmp, 0);
            if (!*optarg || isspace((unsigned char)*optarg) || *tmp || errno ||
                (conversion != 12 && conversion != 16 && conversion != 32)) {
                printf("Bad FAT type : %s\n", optarg);
                usage(argv[0], 1);
            }
            size_fat = conversion;
            size_fat_by_user = 1;
            break;

        case 'g':
            errno = 0;
            conversion = strtol(optarg, &tmp, 0);
            if (!*optarg || isspace((unsigned char)*optarg) || tmp[0] != '/' ||
                !tmp[1] || isspace((unsigned char)tmp[1]) || errno ||
                conversion <= 0 || conversion > UINT16_MAX) {
                printf("Bad format of geometry : %s\n", optarg);
                usage(argv[0], 1);
            }
            bs.heads = htole16(conversion);
            conversion = strtol(tmp + 1, &tmp, 0);
            if (*tmp || errno || conversion <= 0 || conversion > UINT16_MAX) {
                printf("Bad format of geometry : %s\n", optarg);
                usage(argv[0], 1);
            }
            bs.secs_track = htole16(conversion);
            break;

        case 'h':
            errno = 0;
            conversion = strtoll(optarg, &tmp, 0);
            if (!*optarg || isspace((unsigned char)*optarg) || *tmp || errno ||
                conversion < 0 || conversion > UINT32_MAX) {
                printf("Bad number of hidden sectors : %s\n", optarg);
                usage(argv[0], 1);
            }
            hidden_sectors = conversion;
            hidden_sectors_by_user = 1;
            break;

        case 'I':
            ignore_safety_checks = 1;
            break;

        case 'i':
            errno = 0;
            conversion = strtoll(optarg, &tmp, 16);

            if (!*optarg || isspace((unsigned char)*optarg) || *tmp ||
                conversion < 0) {
                printf("Volume ID must be a hexadecimal number\n");
                usage(argv[0], 1);
            }
            if (conversion > UINT32_MAX) {
                printf("Volume ID does not fit in 32 bit\n");
                usage(argv[0], 1);
            }
            if (errno) {
                printf("Parsing volume ID failed (%s)\n", strerror(errno));
                usage(argv[0], 1);
            }

            volume_id = conversion;
            break;

        case 'l':
            listfile = optarg;
            malloc_entire_fat = TRUE;
            break;

        case 'm':
            if (strcmp(optarg, "-")) {
                msgfile = fopen(optarg, "r");
                if (!msgfile)
                    perror(optarg);
            } else
                msgfile = stdin;

            if (msgfile) {
                /* The boot code ends at offset 448 and needs a null terminator
                 */
                i = MESSAGE_OFFSET;
                pos = 0;
                do {
                    ch = getc(msgfile);
                    switch (ch) {
                    case '\r':
                    case '\0':
                        break;

                    case '\n':
                        if (pos) {
                            dummy_boot_code[i++] = '\r';
                            pos = 0;
                        }
                        dummy_boot_code[i++] = '\n';
                        break;

                    case '\t':
                        do {
                            dummy_boot_code[i++] = ' ';
                            pos++;
                        } while (pos % 8 && i < BOOTCODE_SIZE - 1);
                        break;

                    case EOF:
                        dummy_boot_code[i++] = '\0';
                        break;

                    default:
                        dummy_boot_code[i++] = ch;
                        pos++;
                        break;
                    }
                } while (ch != EOF && i < BOOTCODE_SIZE - 1);

                while (i < BOOTCODE_SIZE - 1)
                    dummy_boot_code[i++] = '\0';
                dummy_boot_code[BOOTCODE_SIZE - 1] = '\0';

                if (ch != EOF)
                    printf("Warning: message too long; truncated\n");

                if (msgfile != stdin)
                    fclose(msgfile);
            }
            break;

        case 'M':
            errno = 0;
            conversion = strtol(optarg, &tmp, 0);
            if (!*optarg || isspace((unsigned char)*optarg) || *tmp || errno) {
                printf("Bad number for media descriptor : %s\n", optarg);
                usage(argv[0], 1);
            }
            if (conversion != 0xf0 &&
                (conversion < 0xf8 || conversion > 0xff)) {
                printf("FAT Media byte must either be between 0xF8 and 0xFF or "
                       "be 0xF0 : %s\n",
                       optarg);
                usage(argv[0], 1);
            }
            fat_media_byte = conversion;
            break;

        case 'n':
            volume_name = optarg;
            break;

        case OPT_CODEPAGE:
            errno = 0;
            conversion = strtol(optarg, &tmp, 10);
            if (!*optarg || isspace((unsigned char)*optarg) || *tmp || errno ||
                conversion < 0 || conversion > INT_MAX) {
                fprintf(stderr, "Invalid codepage : %s\n", optarg);
                usage(argv[0], 1);
            }
            codepage = conversion;
            break;

        case 'r':
            errno = 0;
            conversion = strtol(optarg, &tmp, 0);
            if (!*optarg || isspace((unsigned char)*optarg) || *tmp || errno ||
                conversion < 16 || conversion > 32768) {
                printf("Bad number of root directory entries : %s\n", optarg);
                usage(argv[0], 1);
            }
            root_dir_entries = conversion;
            root_dir_entries_set = 1;
            break;

        case 'R':
            errno = 0;
            conversion = strtol(optarg, &tmp, 0);
            if (!*optarg || isspace((unsigned char)*optarg) || *tmp || errno ||
                conversion < 1 || conversion > 0xffff) {
                printf("Bad number of reserved sectors : %s\n", optarg);
                usage(argv[0], 1);
            }
            reserved_sectors = conversion;
            break;

        case 's':
            errno = 0;
            conversion = strtol(optarg, &tmp, 0);
            if (!*optarg || isspace((unsigned char)*optarg) || *tmp || errno ||
                (conversion != 1 && conversion != 2 && conversion != 4 &&
                 conversion != 8 && conversion != 16 && conversion != 32 &&
                 conversion != 64 && conversion != 128)) {
                printf("Bad number of sectors per cluster : %s\n", optarg);
                usage(argv[0], 1);
            }
            sectors_per_cluster = conversion;
            break;

        case 'S':
            errno = 0;
            conversion = strtol(optarg, &tmp, 0);
            if (!*optarg || isspace((unsigned char)*optarg) || *tmp || errno ||
                (conversion != 512 && conversion != 1024 &&
                 conversion != 2048 && conversion != 4096 &&
                 conversion != 8192 && conversion != 16384 &&
                 conversion != 32768)) {
                printf("Bad logical sector size : %s\n", optarg);
                usage(argv[0], 1);
            }
            sector_size = conversion;
            sector_size_set = 1;
            break;

        case 'v':
            ++verbose;
            break;

        case OPT_HELP:
            usage(argv[0], 0);
            break;

        case OPT_INVARIANT:
            invariant = 1;
            volume_id = 0x1234abcd;
            create_time = 1426325213;
            break;

        case OPT_MBR:
            if (!optarg || !strcasecmp(optarg, "y") ||
                !strcasecmp(optarg, "yes"))
                fill_mbr_partition = 1;
            else if (!strcasecmp(optarg, "n") || !strcasecmp(optarg, "no"))
                fill_mbr_partition = 0;
            else if (!strcasecmp(optarg, "a") || !strcasecmp(optarg, "auto"))
                fill_mbr_partition = -1;
            else {
                printf("Unknown option for --mbr: '%s'\n", optarg);
                usage(argv[0], 1);
            }
            break;

        case OPT_VARIANT:
            if (!strcasecmp(optarg, "standard")) {
                atari_format = 0;
            } else if (!strcasecmp(optarg, "atari")) {
                atari_format = 1;
            } else {
                printf("Unknown variant: %s\n", optarg);
                usage(argv[0], 1);
            }
            break;

        case OPT_OFFSET:
            errno = 0;
            conversion = strtoll(optarg, &tmp, 0);
            if (!*optarg || isspace((unsigned char)*optarg) || *tmp || errno) {
                printf("Bad number for offset : %s\n", optarg);
                usage(argv[0], 1);
            }

            if (conversion < 0 || conversion > OFF_MAX) {
                printf("FAT offset must be between 0 and %lld: %s\n",
                       (long long)OFF_MAX, optarg);
                usage(argv[0], 1);
            }

            part_sector = (off_t)conversion;
            break;

        case '?':
            usage(argv[0], 1);
            break;

        default:
            exit(1);
                c);
                break;
        }

    if (!set_dos_codepage(codepage))
        exit(1);

    if (optind == argc || !argv[optind]) {
        printf("No device specified.\n");
        usage(argv[0], 1);
    }

    device_name = argv[optind++];

    if (optind != argc) {
        blocks_specified = 1;
        errno = 0;
        conversion = strtoll(argv[optind], &tmp, 0);

        if (!*argv[optind] || isspace((unsigned char)*argv[optind]) || *tmp ||
            errno || conversion < 0) {
            printf("Bad block count : %s\n", argv[optind]);
            usage(argv[0], 1);
        }
        blocks = conversion;

        optind++;
    }

    if (optind != argc) {
        fprintf(stderr, "Excess arguments on command line\n");
        usage(argv[0], 1);
    }

    if (create && !blocks_specified)
        exit(1);

    if (check && listfile)
        exit(1);

    if (!create) {
        check_mount(device_name);
        dev = open(device_name, O_EXCL | O_RDWR);
        if (dev < 0) {
            exit(1);
        }
    } else {

        dev = open(device_name, O_EXCL | O_RDWR | O_CREAT, 0666);
        if (dev < 0) {
            if (errno == EEXIST)
                exit(1);
            else
                exit(1);
        }

        if (ftruncate(dev, part_sector * sector_size + blocks * BLOCK_SIZE))
            exit(1);
    }

    if (get_device_info(dev, &devinfo) < 0)
        exit(1);

    if (devinfo.size <= 0)
        exit(1);

    if (devinfo.sector_size > 0) {
        if (sector_size_set) {
            if (sector_size < devinfo.sector_size) {
                sector_size = devinfo.sector_size;
                fprintf(stderr,
                        "Warning: sector size was set to %d (minimal for this "
                        "device)\n",
                        sector_size);
            }
        } else {
            sector_size = devinfo.sector_size;
            sector_size_set = 1;
        }

        if (devinfo.size <= part_sector * sector_size)
            exit(1);
                device_name, devinfo.size,
                (unsigned long long)part_sector * sector_size);
    }

    if (sector_size > 4096)
        fprintf(stderr,
                "Warning: sector size %d > 4096 is non-standard, filesystem "
                "may not be usable\n",
                sector_size);

    cblocks = (devinfo.size - part_sector * sector_size) / BLOCK_SIZE;
    orphaned_sectors =
        ((devinfo.size - part_sector * sector_size) % BLOCK_SIZE) / sector_size;

    if (blocks_specified) {
        if (blocks != cblocks) {
            fprintf(stderr, "Warning: block count mismatch: ");
            fprintf(stderr, "found %llu but assuming %llu.\n", cblocks, blocks);
        }
    } else {
        blocks = cblocks;
    }

    /*
     * Ignore any 'full' fixed disk devices, if -I is not given.
     */
    if (!ignore_safety_checks && devinfo.has_children > 0)
        exit(1);
            "filesystem (use -I to override)",
            device_name);

            /*
             * On non-removable fixed disk devices we need to create (fake) MBR
             * partition table so disk would be correctly recognized on MS
             * Windows systems.
             */
            if (fill_mbr_partition == -1) {
                if (devinfo.type == TYPE_FIXED && devinfo.partition == 0)
                    fill_mbr_partition = 1;
                else
                    fill_mbr_partition = 0;
            }

            establish_params(&devinfo);

            setup_tables();

            if (check)
                check_blocks();
            else if (listfile)
                get_list_blocks(listfile);

            write_tables();

            if (fsync(dev) < 0)
                exit(1);

            exit(0);
}
