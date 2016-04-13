/* inotify_ex.c ---
 *
 * Filename: inotify_ex.c
 * Description:
 * Author: Andrey Andruschenko
 * Maintainer:
 * Created: Ср апр 13 14:46:15 2016 (+0300)
 * Version:
 * Package-Requires: ()
 * Last-Updated:
 *           By:
 *     Update #: 108
 * URL:
 * Doc URL:
 * Keywords:
 * Compatibility:
 *
 */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <ftw.h>
#include <getopt.h>
#include <errno.h>
#include <err.h>
#include <sys/inotify.h>
#include <sys/epoll.h>

#include "dir_mon.h"

static size_t fds_count = 0;
static size_t fds_pre_alloc = _FDS_PREALLOC;
static int ifd, *fds = NULL;
static char **dirs = NULL;

static void handle_notify(int wfd, int *fd, char **names) {
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    const struct inotify_event *event;
    size_t i;
    ssize_t len;
    char *ptr;

    while (1) {
        len = read(wfd, buf, sizeof buf);
        if (len == -1 && errno != EAGAIN) err(EXIT_FAILURE, "read()");

        if (len <= 0) break;
        for (ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + event->len) {
            event = (const struct inotify_event *)ptr;

            if (event->mask & IN_ATTRIB) printf("IN_ATTRIB: ");
            if (event->mask & IN_CREATE) printf("IN_CREATE: ");
            if (event->mask & IN_DELETE) printf("IN_DELETE: ");
            if (event->mask & IN_DELETE_SELF) printf("IN_DELETE_SELF: ");

            for (i = 1; i < fds_count; ++i) {
                if (fd[i] == event->wd) {
                    printf("%s/", names[i]);
                    if (event->mask & IN_DELETE_SELF) inotify_rm_watch(ifd, fds[i]);
                    break;
                }
            }

            if (event->len) printf("%s", event->name);

            /* Print type of filesystem object */

            if (event->mask & IN_ISDIR)
                printf(" [directory]\n");
            else
                printf(" [file]\n");
        }
    }
}

static int nftw_cb(const char *fpath, __attribute__((unused)) const struct stat *sb, int tflag,
                   __attribute__((unused)) struct FTW *ftwbuf) {
    if (tflag == FTW_D) {
        fds[fds_count] =
            inotify_add_watch(ifd, fpath, IN_CREATE | IN_ATTRIB | IN_DELETE_SELF | IN_DELETE);
        if (fds[fds_count] < 0)
            err(EXIT_FAILURE, "inotify_add_watch(), %" PRIu64 " fds already added", fds_count);
        if ((dirs[fds_count] = (char *)malloc(sizeof(char) * (strlen(fpath) + 1))) == NULL)
            err(EXIT_FAILURE, "malloc()");
        strcpy(dirs[fds_count], fpath);

        fds_count++;
        fprintf(stdout, "Add [%s]\n", fpath);

        if (fds_count == fds_pre_alloc) {
            fds_pre_alloc += 32;
            if ((fds = (int *)realloc(fds, sizeof(int) * fds_pre_alloc)) == NULL)
                err(EXIT_FAILURE, "realloc()");
            if ((dirs = (char **)realloc(dirs, sizeof(char *) * fds_pre_alloc)) == NULL)
                err(EXIT_FAILURE, "realloc()");
        }
    }
    return 0;
}

static int usage(const char *name) {
    errx(EXIT_SUCCESS,
         "\nUsage:\n\t%s <options>\nOptions:\n\t--base=directory - first directory from start to "
         "walk\n\t--allocs=N - preallocate N memory blocks for a FD/Dirname at start\n",
         name);
}

int main(int argc, char *argv[]) {
    int opt, efd, poll_ret, ctl;
    int indexptr = 0;
    char *base_dir = NULL;
    struct epoll_event ev, revents;
    struct option opts[] = {{"base", required_argument, NULL, 'd'},
                            {"allocs", required_argument, NULL, 'a'},
                            {0, 0, 0, 0}};

    while ((opt = getopt_long_only(argc, argv, "d:?", opts, &indexptr)) != -1) {
        switch (opt) {
        case 'd':
            if ((base_dir = (char *)malloc(sizeof(char) * (strlen(optarg) + 1))) == NULL)
                err(EXIT_FAILURE, "malloc()");
            strcpy(base_dir, optarg);
            break;
        case 'a':
            fds_pre_alloc = atoll(optarg);
            break;
        default:
            usage(argv[0]);
            break;
        }
    }

    if (base_dir == NULL)
        if ((base_dir = get_current_dir_name()) == NULL)
            err(EXIT_FAILURE, "get_current_dir_name()");

    if ((fds = (int *)calloc(_FDS_PREALLOC, sizeof(int))) == NULL) err(EXIT_FAILURE, "calloc()");
    if ((dirs = (char **)calloc(_FDS_PREALLOC, sizeof(char *))) == NULL)
        err(EXIT_FAILURE, "calloc()");

    if ((efd = epoll_create(1)) == -1) err(EXIT_FAILURE, "epoll_create() error");
    if ((ifd = inotify_init1(IN_NONBLOCK)) < 0) err(EXIT_FAILURE, "inotify_init()");

    if (nftw(base_dir, nftw_cb, _NFTW_MAX_FDS_, FTW_PHYS | FTW_MOUNT) == -1)
        err(EXIT_FAILURE, "nftw()");

    fprintf(stderr, "Watch for a %" PRIu64 " directories.\n", fds_count);

    ev.data.fd = ifd;
    ev.events = EPOLLIN;

    if ((ctl = epoll_ctl(efd, EPOLL_CTL_ADD, ifd, &ev)) == -1)
        err(EXIT_FAILURE, "epoll_ctl() error");

    while (1) {
        poll_ret = epoll_wait(efd, &revents, 1, -1);

        if (poll_ret < 0) {
            if (errno == EINTR) break;
            err(EXIT_FAILURE, "epoll() error.");
        } else if (poll_ret == 0)
            continue;
        handle_notify(ifd, fds, dirs);
    }

    return EXIT_SUCCESS;
}
/* inotify_ex.c ends here */
