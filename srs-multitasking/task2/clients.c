/* clients.c — запуск N клиентов параллельно (fork + execv) и сбор их
 * кодов завершения через waitpid. Использование: clients N */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

static const char *exprs[][3] = {
    { "2+2*2",            "(1+2)*(3+4)",   NULL },
    { "100/7",            "2*(3+4",        NULL },   /* синтаксическая ошибка */
    { "-(5-8)*3.5",       "17%5",          NULL },
    { "1/0",              "10/4",          NULL },   /* деление на ноль */
    { "3.14159*2*2",      NULL,            NULL },
    { "((((1))))+2",      "9-3-2",         NULL },
    { "2*2*2*2*2*2*2*2",  NULL,            NULL },
    { "0.1+0.2",          NULL,            NULL },
};
#define NEXPR (int)(sizeof exprs / sizeof exprs[0])

int main(int argc, char *argv[])
{
    int n = argc > 1 ? atoi(argv[1]) : 4;
    if (n < 1 || n > 64) { fprintf(stderr, "N должно быть 1..64\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);
    printf("[clients %d] запускаю %d клиентов\n", (int)getpid(), n);

    pid_t *pids = calloc((size_t)n, sizeof *pids);
    if (!pids) { perror("calloc"); return 1; }
    for (int i = 0; i < n; i++) {
        pid_t pid = fork();
        if (pid == -1) { perror("fork"); break; }
        if (pid == 0) {
            const char **e = exprs[i % NEXPR];
            char *args[4] = { "client", (char *)e[0], (char *)e[1], NULL };
            execv("./client", args);
            perror("execv ./client");
            _exit(127);                   /* _exit: не сбрасывать буферы родителя */
        }
        pids[i] = pid;
    }
    int fails = 0;
    for (int i = 0; i < n && pids[i]; i++) {
        int st;
        pid_t p;
        while ((p = waitpid(pids[i], &st, 0)) == -1 && errno == EINTR) {}
        if (p == -1) { perror("waitpid"); continue; }
        if (WIFEXITED(st)) {
            printf("[clients %d] клиент %d завершён, код %d\n", (int)getpid(), (int)p, WEXITSTATUS(st));
            if (WEXITSTATUS(st)) fails++;
        } else if (WIFSIGNALED(st)) {
            printf("[clients %d] клиент %d убит сигналом %d\n", (int)getpid(), (int)p, WTERMSIG(st));
            fails++;
        }
    }
    free(pids);
    printf("[clients %d] все клиенты завершены, с ошибками: %d\n", (int)getpid(), fails);
    return 0;
}
