/* procinfo.c — сведения о текущем процессе (общая часть задания 1).
 * Только системные вызовы и /proc, без system() и внешних утилит. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/resource.h>

/* Найти в /proc/self/status строку, начинающуюся с key, и скопировать значение */
static int status_field(const char *key, char *out, size_t outsz)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) { perror("fopen /proc/self/status"); return -1; }
    char line[256];
    size_t klen = strlen(key);
    int found = -1;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, key, klen) == 0 && line[klen] == ':') {
            char *v = line + klen + 1;
            while (*v == ' ' || *v == '\t') v++;
            v[strcspn(v, "\n")] = '\0';
            snprintf(out, outsz, "%s", v);
            found = 0;
            break;
        }
    }
    fclose(f);
    return found;
}

int main(void)
{
    pid_t pid = getpid(), ppid = getppid();
    pid_t pgrp = getpgrp();
    pid_t sid = getsid(0);
    if (sid == -1) { perror("getsid"); return 1; }

    errno = 0;
    int nice_val = getpriority(PRIO_PROCESS, 0);   /* -1 — допустимое значение, проверяем errno */
    if (nice_val == -1 && errno != 0) { perror("getpriority"); return 1; }

    char name[64] = "?", state[64] = "?", vol[32] = "?", nonvol[32] = "?", thr[16] = "?";
    status_field("Name", name, sizeof name);
    status_field("State", state, sizeof state);
    status_field("Threads", thr, sizeof thr);
    status_field("voluntary_ctxt_switches", vol, sizeof vol);
    status_field("nonvoluntary_ctxt_switches", nonvol, sizeof nonvol);

    printf("=== procinfo: атрибуты текущего процесса ===\n");
    printf("Имя (comm)                 : %s\n", name);
    printf("PID                        : %d\n", (int)pid);
    printf("PPID                       : %d\n", (int)ppid);
    printf("UID / EUID                 : %d / %d\n", (int)getuid(), (int)geteuid());
    printf("GID / EGID                 : %d / %d\n", (int)getgid(), (int)getegid());
    printf("Группа процессов (PGID)    : %d\n", (int)pgrp);
    printf("Сессия (SID)               : %d\n", (int)sid);
    printf("Значение nice              : %d\n", nice_val);
    printf("Состояние                  : %s\n", state);
    printf("Число потоков              : %s\n", thr);
    printf("Добровольные перекл. конт. : %s\n", vol);
    printf("Принудительные перекл.     : %s\n", nonvol);
    return 0;
}
