/* dir_mon.h ---
 *
 * Filename: dir_mon.h
 * Description:
 * Author: Andrey Andruschenko
 * Maintainer:
 * Created: Ср апр 13 17:06:15 2016 (+0300)
 * Version:
 * Package-Requires: ()
 * Last-Updated:
 *           By:
 *     Update #: 12
 * URL:
 * Doc URL:
 * Keywords:
 * Compatibility:
 *
 */

/* Code: */
#ifndef __DIR_MON_H_

#define _NFTW_MAX_FDS_ 64
#define _FDS_PREALLOC 1024
#define _COMMA ","

typedef struct __pds_entry_ {
    bool used;   // entry is used
    int fd;      // entry FD
    int type;    // nftw typeflag FTW_D | FTW_F
    char *path;  // entry path
} __attribute__((packed)) pfds_t;

#endif

/* dir_mon.h ends here */
