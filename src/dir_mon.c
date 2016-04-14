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
 *     Update #: 325
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
#include <signal.h>
#include <sys/inotify.h>
#include <sys/epoll.h>

#include "dir_mon.h"

static size_t fds_count = 0, ext_count = 0;
static size_t fds_pre_alloc = _FDS_PREALLOC;
static int ifd, *fds = NULL;
static char **dirs = NULL, **exts = NULL;
static size_t events = 0;

static void sigint_handler(__attribute__((unused)) int sig) {
    fprintf(stdout, "\n\tSeen events: %zu\n\n", events);
    exit(EXIT_SUCCESS);
}

static int is_disabled_ext(const char *path, int len) {
    size_t i;

    for (i = 0; i < ext_count; i++)
        if (strcasecmp((char *)(path + len - strlen(exts[i])), exts[i]) == 0) return 1;

    return 0;
}

static void handle_notify(int wfd, int *fd, char **names) {
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    const struct inotify_event *event;
    size_t i, evt_len = 0;
    ssize_t len;
    char *ptr, *fpath = NULL;

    while (1) {
        len = read(wfd, buf, sizeof buf);
        if (len == -1 && errno != EAGAIN) err(EXIT_FAILURE, "read()");

        if (len <= 0) break;
        for (ptr = buf; ptr < buf + len; ptr += sizeof(struct inotify_event) + evt_len) {
            event = (const struct inotify_event *)ptr;
            evt_len = event->len;

            events++;

            if (event->mask & IN_ATTRIB) printf("IN_ATTRIB: ");
            if (event->mask & IN_CREATE) printf("IN_CREATE: ");
            if (event->mask & IN_MOVED_TO) printf("IN_MOVED_TO: ");
            if (event->mask & IN_DELETE) printf("IN_DELETE: ");
            if (event->mask & IN_DELETE_SELF) printf("IN_DELETE_SELF: ");

            for (i = 1; i < fds_count; i++) {
                if (fd[i] == event->wd) {
                    printf("%s/", names[i]);
                    if (event->mask & IN_DELETE_SELF) inotify_rm_watch(ifd, fds[i]);
                    break;
                }
            }

            if (event->len) printf("%s", event->name);

            if (event->mask & IN_ISDIR)
                printf(" [directory]\n");
            else {
                printf(" [file]\n");
                if (exts != NULL && (event->mask & (IN_CREATE | IN_MOVED_TO)) &&
                    is_disabled_ext(event->name, strlen(event->name))) {
                    names[i] != NULL ? asprintf(&fpath, "%s/%s", names[i], event->name)
                        : asprintf(&fpath, "%s", event->name);
                    if (unlink(fpath) < 0) warn("unlink() %s", fpath);
                    free(fpath);
                }
            }
        }
    }
}

static int nftw_cb(const char *fpath, __attribute__((unused)) const struct stat *sb, int tflag,
                   __attribute__((unused)) struct FTW *ftwbuf) {
    if (tflag == FTW_D) {
        fds[fds_count] =
            inotify_add_watch(ifd, fpath, IN_CREATE | IN_ATTRIB | IN_DELETE_SELF | IN_DELETE | IN_MOVED_TO);
        if (fds[fds_count] < 0)
            err(EXIT_FAILURE, "inotify_add_watch(), %zu fds already added", fds_count);
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
         "walk\n\t--allocs=N - preallocate N memory blocks for a FD/Dirname at start\n"
         "\t--exts=extentions - comma sepatared list of disabled extentions\n",
         name);
}

int main(int argc, char *argv[]) {
    int opt, efd, poll_ret, ctl;
    int indexptr = 0;
    char *base_dir = NULL, *ptr, *ext = NULL, *bfr;
    struct epoll_event ev, revents;
    struct option opts[] = {{"base", required_argument, NULL, 'd'},
                            {"allocs", required_argument, NULL, 'a'},
                            {"exts", required_argument, NULL, 'e'},
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
        case 'e':
            ptr = optarg;
            while ((ext = strtok_r(ptr, _COMMA, &bfr)) != NULL) {
                ext_count++;
                ptr = NULL;
                if ((exts = (char **)realloc(exts, sizeof(char *) * ext_count)) == NULL)
                    err(EXIT_FAILURE, "realloc()");
                if ((exts[ext_count - 1] = (char *)malloc(sizeof(char) * (strlen(ext) + 1))) ==
                    NULL)
                    err(EXIT_FAILURE, "malloc()");
                strcpy(exts[ext_count - 1], ext);
                fprintf(stderr, "Add disabled extention: %s\n", exts[ext_count - 1]);
            }
            if (ext != NULL) free(ext);
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

    fprintf(stderr, "Watch for a %zu directories.\n", fds_count);

    ev.data.fd = ifd;
    ev.events = EPOLLIN;

    if ((ctl = epoll_ctl(efd, EPOLL_CTL_ADD, ifd, &ev)) == -1)
        err(EXIT_FAILURE, "epoll_ctl() error");

    signal(SIGINT, sigint_handler);

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
