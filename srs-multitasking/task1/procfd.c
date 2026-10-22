/* procfd.c — вариант 8: вывести все открытые дескрипторы процесса
 * и объекты, на которые они указывают (/proc/<pid>/fd + readlink).
 * Дополнительно из /proc/<pid>/fdinfo/<fd> выводятся флаги и позиция,
 * а из /proc/<pid>/limits — лимит открытых файлов.
 *
 * Использование: procfd [PID ...]   (без аргументов — собственный процесс) */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>

/* длина строки UTF-8 в символах (для выравнивания столбцов с кириллицей) */
static size_t u8len(const char *s)
{
    size_t n = 0;
    for (; *s; s++) if (((unsigned char)*s & 0xC0) != 0x80) n++;
    return n;
}

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);
}

/* Классификация цели ссылки */
static const char *fd_kind(const char *target, const struct stat *st, int have_st)
{
    if (strncmp(target, "pipe:", 5) == 0)        return "канал";
    if (strncmp(target, "socket:", 7) == 0)      return "сокет";
    if (strncmp(target, "anon_inode:", 11) == 0) return "anon_inode";
    if (have_st) {
        if (S_ISREG(st->st_mode))  return "файл";
        if (S_ISDIR(st->st_mode))  return "каталог";
        if (S_ISCHR(st->st_mode))  return "симв.устр.";
        if (S_ISBLK(st->st_mode))  return "блоч.устр.";
        if (S_ISFIFO(st->st_mode)) return "FIFO";
    }
    return "другое";
}

/* Расшифровка флагов открытия (поле flags в fdinfo — восьмеричное) */
static void decode_flags(unsigned long fl, char *out, size_t n)
{
    const char *acc = (fl & O_ACCMODE) == O_RDONLY ? "R" :
                      (fl & O_ACCMODE) == O_WRONLY ? "W" : "RW";
    snprintf(out, n, "%s%s%s%s", acc,
             (fl & O_APPEND)   ? ",APPEND"   : "",
             (fl & O_NONBLOCK) ? ",NONBLOCK" : "",
             (fl & O_CLOEXEC)  ? ",CLOEXEC"  : "");
}

static void read_fdinfo(pid_t pid, int fd, long long *pos, unsigned long *flags)
{
    char path[PATH_MAX], line[256];
    snprintf(path, sizeof path, "/proc/%d/fdinfo/%d", (int)pid, fd);
    *pos = -1; *flags = 0;
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        sscanf(line, "pos: %lld", pos);
        sscanf(line, "flags: %lo", flags);
    }
    fclose(f);
}

static void print_limit(pid_t pid)
{
    char path[64], line[256];
    snprintf(path, sizeof path, "/proc/%d/limits", (int)pid);
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f))
        if (strncmp(line, "Max open files", 14) == 0) {
            unsigned long soft, hard;
            if (sscanf(line + 14, "%lu %lu", &soft, &hard) == 2)
                printf("Лимит RLIMIT_NOFILE: мягкий %lu, жёсткий %lu\n", soft, hard);
        }
    fclose(f);
}

static void comm_of(pid_t pid, char *out, size_t n)
{
    char path[64];
    snprintf(path, sizeof path, "/proc/%d/comm", (int)pid);
    snprintf(out, n, "?");
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (fgets(out, (int)n, f)) out[strcspn(out, "\n")] = '\0';
    fclose(f);
}

static int show_pid(pid_t pid)
{
    char dirpath[64];
    snprintf(dirpath, sizeof dirpath, "/proc/%d/fd", (int)pid);
    DIR *d = opendir(dirpath);
    if (!d) {
        fprintf(stderr, "procfd: %s: %s\n", dirpath, strerror(errno));
        return -1;
    }
    /* собираем номера дескрипторов, чтобы вывести их по порядку */
    size_t cap = 64, cnt = 0;
    int *fds = malloc(cap * sizeof *fds);
    if (!fds) { perror("malloc"); closedir(d); return -1; }
    int self_dirfd = dirfd(d);
    struct dirent *e;
    errno = 0;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        int fd = atoi(e->d_name);
        if (pid == getpid() && fd == self_dirfd) continue; /* наш собственный DIR* */
        if (cnt == cap) {
            cap *= 2;
            int *tmp = realloc(fds, cap * sizeof *fds);
            if (!tmp) { perror("realloc"); free(fds); closedir(d); return -1; }
            fds = tmp;
        }
        fds[cnt++] = fd;
    }
    if (errno) perror("readdir");
    qsort(fds, cnt, sizeof *fds, cmp_int);

    char comm[64];
    comm_of(pid, comm, sizeof comm);
    printf("Процесс %d (%s): открыто дескрипторов: %zu\n", (int)pid, comm, cnt);
    printf("  FD  ТИП         РЕЖИМ               ПОЗИЦИЯ  -> ОБЪЕКТ\n");
    printf("----  ----------- ---------------- ----------  --------------------------\n");
    for (size_t i = 0; i < cnt; i++) {
        char lpath[PATH_MAX], target[PATH_MAX];
        snprintf(lpath, sizeof lpath, "%s/%d", dirpath, fds[i]);
        ssize_t len = readlink(lpath, target, sizeof target - 1);
        if (len == -1) {
            printf("%4d  readlink: %s\n", fds[i], strerror(errno));
            continue;
        }
        target[len] = '\0';
        struct stat st;
        int have_st = (stat(lpath, &st) == 0);   /* stat по ссылке — свойства объекта */
        long long pos; unsigned long fl; char fls[48];
        read_fdinfo(pid, fds[i], &pos, &fl);
        decode_flags(fl, fls, sizeof fls);
        const char *kind = fd_kind(target, &st, have_st);
        printf("%4d  %-*s %-16s %10lld  %s\n", fds[i],
               11 + (int)(strlen(kind) - u8len(kind)), kind, fls, pos, target);
    }
    print_limit(pid);
    free(fds);
    closedir(d);
    return 0;
}

int main(int argc, char *argv[])
{
    int rc = 0;
    if (argc == 1)
        return show_pid(getpid()) == 0 ? 0 : 1;
    for (int i = 1; i < argc; i++) {
        char *end;
        errno = 0;
        long v = strtol(argv[i], &end, 10);
        if (errno || *end || v <= 0) {
            fprintf(stderr, "procfd: неверный PID: %s\n", argv[i]);
            rc = 1; continue;
        }
        if (i > 1) putchar('\n');
        if (show_pid((pid_t)v) != 0) rc = 1;
    }
    return rc;
}
