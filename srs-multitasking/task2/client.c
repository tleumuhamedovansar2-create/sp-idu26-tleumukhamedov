/* client.c — клиент калькулятора (вариант 8, задание 2).
 * Создаёт личный FIFO /tmp/calc_client.<pid>.fifo, отправляет выражения
 * в общий FIFO сервера и читает ответы из личного.
 * Использование: client "выражение" ...   (без аргументов — читает stdin)
 * Код завершения: 0 — все выражения вычислены, 1 — были ошибки, 2 — сбой. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "common.h"

static char my_fifo[64];
static int  my_rfd = -1;   /* личный FIFO открыт один раз на всё время работы */

static void cleanup(void) { if (my_fifo[0]) unlink(my_fifo); }
static void on_signal(int sig) { unlink(my_fifo); signal(sig, SIG_DFL); raise(sig); }

static int ask(int srv_fd, int seq, const char *expr)
{
    struct request rq = { .client_pid = getpid(), .seq = seq };
    snprintf(rq.expr, sizeof rq.expr, "%s", expr);
    if (write(srv_fd, &rq, sizeof rq) != (ssize_t)sizeof rq) { perror("write запроса"); exit(2); }

    struct response rs;
    ssize_t r = read(my_rfd, &rs, sizeof rs);   /* блокируется до ответа рабочего */
    if (r != (ssize_t)sizeof rs) { fprintf(stderr, "ответ не получен (%zd)\n", r); exit(2); }
    if (rs.seq != seq) { fprintf(stderr, "ответ на чужой запрос (%d != %d)\n", rs.seq, seq); exit(2); }

    if (rs.status == ST_OK)
        printf("[client %d] %s = %.10g   (рабочий %d)\n", (int)getpid(), expr, rs.value, (int)rs.worker_pid);
    else
        printf("[client %d] %s : ОШИБКА: %s   (рабочий %d)\n", (int)getpid(), expr, rs.msg, (int)rs.worker_pid);
    fflush(stdout);
    return rs.status == ST_OK ? 0 : 1;
}

int main(int argc, char *argv[])
{
    snprintf(my_fifo, sizeof my_fifo, CLIENT_FIFO_FMT, (int)getpid());
    umask(0);
    if (mkfifo(my_fifo, 0622) == -1) { perror("mkfifo"); return 2; }
    if (atexit(cleanup) != 0) { fprintf(stderr, "atexit\n"); return 2; }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    /* Открываем личный FIFO на чтение (NONBLOCK — чтобы не ждать писателя) и сами
     * держим конец на запись: тогда read() не вернёт EOF в промежутке между
     * закрытием FIFO одним рабочим и открытием следующим. */
    my_rfd = open(my_fifo, O_RDONLY | O_NONBLOCK);
    if (my_rfd == -1) { perror("open личного FIFO (r)"); return 2; }
    int my_dummy_w = open(my_fifo, O_WRONLY);
    if (my_dummy_w == -1) { perror("open личного FIFO (w)"); return 2; }
    int rfl = fcntl(my_rfd, F_GETFL);
    if (rfl == -1 || fcntl(my_rfd, F_SETFL, rfl & ~O_NONBLOCK) == -1) { perror("fcntl"); return 2; }

    int srv = open(SERVER_FIFO, O_WRONLY | O_NONBLOCK);  /* NONBLOCK: если сервера нет — ENXIO/ENOENT */
    if (srv == -1) { fprintf(stderr, "[client %d] сервер недоступен: %s\n", (int)getpid(), strerror(errno)); return 2; }
    int fl = fcntl(srv, F_GETFL);
    if (fl == -1 || fcntl(srv, F_SETFL, fl & ~O_NONBLOCK) == -1) { perror("fcntl"); return 2; }

    int errors = 0, seq = 0;
    if (argc > 1) {
        for (int i = 1; i < argc; i++) errors += ask(srv, ++seq, argv[i]);
    } else {
        char line[EXPR_MAX];
        while (fgets(line, sizeof line, stdin)) {
            line[strcspn(line, "\n")] = '\0';
            if (line[0]) errors += ask(srv, ++seq, line);
        }
    }
    if (close(srv) == -1 || close(my_rfd) == -1 || close(my_dummy_w) == -1) perror("close");
    return errors ? 1 : 0;
}
