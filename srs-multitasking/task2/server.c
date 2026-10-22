/* server.c — сервер-калькулятор на именованных каналах (вариант 8, задание 2).
 *
 * Сервер создаёт общий FIFO /tmp/calc_server.fifo и читает из него запросы
 * struct request. На каждый запрос порождается рабочий процесс (fork),
 * который вычисляет выражение и отвечает клиенту в его личный FIFO
 * /tmp/calc_client.<pid>.fifo. Код завершения рабочего = статус ответа.
 *
 * Сбор завершившихся потомков: обработчик SIGCHLD пишет байт в «self-pipe»,
 * главный цикл ждёт poll() сразу на FIFO и на self-pipe и вызывает
 * waitpid(-1, WNOHANG) в цикле — зомби не накапливаются, printf в
 * обработчике сигнала не используется.
 * По SIGINT/SIGTERM сервер перестаёт принимать запросы, дожидается всех
 * рабочих, удаляет FIFO и завершается.
 *
 * Использование: server [-d мс]   (-d — искусственная задержка рабочего,
 *                                  чтобы увидеть процессы в pstree)       */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include "common.h"
#include "eval.h"

static int sig_pipe[2];                    /* self-pipe */
static volatile sig_atomic_t stop_flag = 0;
static int active_workers = 0;
static long served = 0;

static void die(const char *what) { perror(what); exit(EXIT_FAILURE); }

/* обработчики только async-signal-safe: write() и запись флага */
static void on_sigchld(int sig) { (void)sig; int e = errno; (void)!write(sig_pipe[1], "c", 1); errno = e; }
static void on_stop(int sig)    { (void)sig; int e = errno; stop_flag = 1; (void)!write(sig_pipe[1], "s", 1); errno = e; }

static void ts(char *buf, size_t n)
{
    struct timespec t; clock_gettime(CLOCK_REALTIME, &t);
    struct tm tm; localtime_r(&t.tv_sec, &tm);
    snprintf(buf, n, "%02d:%02d:%02d.%03d", tm.tm_hour % 24, tm.tm_min % 60, tm.tm_sec % 61, (int)(t.tv_nsec / 1000000));
}
#define LOG(...) do { char _t[48]; ts(_t, sizeof _t); printf("%s [server %d] ", _t, (int)getpid()); \
                      printf(__VA_ARGS__); putchar('\n'); fflush(stdout); } while (0)

/* Собрать всех завершившихся рабочих, вывести PID и код завершения */
static void reap_children(void)
{
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        active_workers--;
        if (WIFEXITED(status))
            LOG("рабочий %d завершён, код %d", (int)pid, WEXITSTATUS(status));
        else if (WIFSIGNALED(status))
            LOG("рабочий %d убит сигналом %d (%s)", (int)pid, WTERMSIG(status), strsignal(WTERMSIG(status)));
    }
    if (pid == -1 && errno != ECHILD) perror("waitpid");
}

/* Код рабочего процесса */
static void worker(const struct request *rq, int delay_ms)
{
    if (prctl(PR_SET_NAME, "calc_worker") == -1) perror("prctl");
    struct sigaction sa = { .sa_handler = SIG_DFL };
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGCHLD, &sa, NULL) == -1) perror("sigaction");
    if (sigaction(SIGINT, &sa, NULL) == -1)  perror("sigaction");
    if (sigaction(SIGTERM, &sa, NULL) == -1) perror("sigaction");

    if (delay_ms > 0) {
        struct timespec d = { delay_ms / 1000, (delay_ms % 1000) * 1000000L };
        while (nanosleep(&d, &d) == -1 && errno == EINTR) {}
    }
    struct response rs = { .seq = rq->seq, .worker_pid = getpid() };
    rs.status = eval_expr(rq->expr, &rs.value, rs.msg, sizeof rs.msg);

    char path[64];
    snprintf(path, sizeof path, CLIENT_FIFO_FMT, (int)rq->client_pid);
    alarm(5);                         /* если клиент исчез — не висеть вечно в open() */
    int fd = open(path, O_WRONLY);
    if (fd == -1) { perror(path); _exit(3); }
    alarm(0);
    ssize_t w = write(fd, &rs, sizeof rs);
    if (w != (ssize_t)sizeof rs) { perror("write ответа"); _exit(3); }
    if (close(fd) == -1) perror("close");
    _exit(rs.status);                 /* код завершения = статус вычисления */
}

int main(int argc, char *argv[])
{
    int delay_ms = 0, opt;
    while ((opt = getopt(argc, argv, "d:")) != -1) {
        if (opt == 'd') delay_ms = atoi(optarg);
        else { fprintf(stderr, "использование: %s [-d мс]\n", argv[0]); return 1; }
    }
    setvbuf(stdout, NULL, _IOLBF, 0);

    if (pipe2(sig_pipe, O_CLOEXEC | O_NONBLOCK) == -1) die("pipe2");

    struct sigaction sa = { 0 };
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_sigchld;
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) die("sigaction SIGCHLD");
    sa.sa_handler = on_stop;
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1)  die("sigaction SIGINT");
    if (sigaction(SIGTERM, &sa, NULL) == -1) die("sigaction SIGTERM");
    signal(SIGPIPE, SIG_IGN);          /* клиент закрыл FIFO — получим EPIPE, а не смерть */

    umask(0);
    if (mkfifo(SERVER_FIFO, 0666) == -1 && errno != EEXIST) die("mkfifo " SERVER_FIFO);

    /* Открываем FIFO на чтение и дополнительно на запись: пока у сервера есть
     * «свой» писатель, read() не вернёт EOF, когда очередной клиент отключится. */
    int rfd = open(SERVER_FIFO, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (rfd == -1) die("open " SERVER_FIFO " (r)");
    int dummy_w = open(SERVER_FIFO, O_WRONLY | O_CLOEXEC);
    if (dummy_w == -1) die("open " SERVER_FIFO " (w)");

    LOG("запущен, PPID %d, жду запросы в %s (задержка рабочего %d мс)", (int)getppid(), SERVER_FIFO, delay_ms);

    struct pollfd pfd[2] = { { rfd, POLLIN, 0 }, { sig_pipe[0], POLLIN, 0 } };
    while (!stop_flag) {
        int n = poll(pfd, 2, -1);
        if (n == -1) { if (errno == EINTR) continue; die("poll"); }

        if (pfd[1].revents & POLLIN) {          /* пришёл сигнал */
            char tmp[64];
            while (read(sig_pipe[0], tmp, sizeof tmp) > 0) {}
            reap_children();
        }
        if (pfd[0].revents & POLLIN) {
            struct request rq;
            ssize_t r;
            while ((r = read(rfd, &rq, sizeof rq)) == (ssize_t)sizeof rq) {
                rq.expr[EXPR_MAX - 1] = '\0';
                pid_t pid = fork();
                if (pid == -1) { perror("fork"); continue; }
                if (pid == 0) {
                    close(rfd); close(dummy_w); close(sig_pipe[0]); close(sig_pipe[1]);
                    worker(&rq, delay_ms);
                }
                active_workers++; served++;
                LOG("запрос #%d от клиента %d: \"%s\" -> рабочий %d", rq.seq, (int)rq.client_pid, rq.expr, (int)pid);
            }
            if (r == -1 && errno != EAGAIN) perror("read");
            else if (r > 0) fprintf(stderr, "неполный запрос (%zd байт) отброшен\n", r);
        }
    }

    LOG("получен сигнал остановки, ожидаю %d рабочих", active_workers);
    int status; pid_t pid;
    while ((pid = waitpid(-1, &status, 0)) > 0) {
        active_workers--;
        if (WIFEXITED(status)) LOG("рабочий %d завершён, код %d", (int)pid, WEXITSTATUS(status));
        else if (WIFSIGNALED(status)) LOG("рабочий %d убит сигналом %d", (int)pid, WTERMSIG(status));
    }
    if (pid == -1 && errno != ECHILD) perror("waitpid");
    if (close(rfd) == -1 || close(dummy_w) == -1) perror("close");
    if (unlink(SERVER_FIFO) == -1) perror("unlink");
    LOG("обслужено запросов: %ld, FIFO удалён, завершение с кодом 0", served);
    return 0;
}
