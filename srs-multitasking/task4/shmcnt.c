/* shmcnt.c — вариант 8, задание 4: межпроцессный мьютекс.
 *
 * N процессов увеличивают общий счётчик в разделяемой памяти POSIX.
 * Сравниваются способы защиты:
 *   mutex — pthread_mutex_t с атрибутом PTHREAD_PROCESS_SHARED (+ ROBUST),
 *           лежащий в той же разделяемой памяти;
 *   sem   — именованный семафор POSIX sem_open("/srs_cnt_sem"), начальное значение 1;
 *   none  — без защиты (демонстрация гонки между процессами).
 *
 * Схема: родитель создаёт объект shm_open("/srs_cnt_shm"), задаёт размер
 * ftruncate, отображает mmap(MAP_SHARED), инициализирует мьютекс и барьер
 * (оба PROCESS_SHARED), создаёт семафор и порождает N потомков. Каждый
 * потомок САМ открывает объекты по имени (shm_open/sem_open без O_CREAT) —
 * как независимый процесс, ждёт на барьере общего старта и выполняет ITER
 * инкрементов. Родитель засекает время от барьера до завершения всех
 * потомков (waitpid), проверяет счётчик и удаляет объекты (shm_unlink, sem_unlink).
 *
 * Использование:
 *   shmcnt [-m mutex|sem|none] [-p процессов] [-i итераций] [-c] [-h мс]
 *   shmcnt -b [-i итераций]    — таблица mutex/sem для 1, 2, 4, 8 процессов (CSV)
 *   -c  — процесс №0 аварийно завершается (abort), ДЕРЖА мьютекс:
 *         проверка робастного мьютекса (EOWNERDEAD → pthread_mutex_consistent)
 *   -h  — после работы каждый потомок «засыпает» на указанное время,
 *         чтобы увидеть процессы и объекты в /dev/shm                       */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/resource.h>

#define SHM_NAME "/srs_cnt_shm"
#define SEM_NAME "/srs_cnt_sem"
#define MAXPROC  64

enum mode { M_MUTEX, M_SEM, M_NONE };
static const char *mode_name[] = { "mutex", "sem", "none" };

struct shared {
    pthread_mutex_t   mtx;          /* PTHREAD_PROCESS_SHARED + ROBUST */
    pthread_barrier_t start;        /* общий старт N потомков и родителя */
    volatile long     counter;      /* защищаемый счётчик */
    long              recovered;    /* сколько раз восстанавливали мьютекс */
};

#define CHECK(call) do { if ((call) == -1) { perror(#call); exit(EXIT_FAILURE); } } while (0)
#define CHECK_PT(call) do { int _e = (call); if (_e) { fprintf(stderr, "%s: %s\n", #call, strerror(_e)); exit(EXIT_FAILURE); } } while (0)

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

/* Захват робастного мьютекса: если прежний владелец умер, держа его,
 * получаем EOWNERDEAD, помечаем состояние согласованным и продолжаем. */
static void robust_lock(struct shared *sh)
{
    int rc = pthread_mutex_lock(&sh->mtx);
    if (rc == EOWNERDEAD) {
        sh->recovered++;
        fprintf(stderr, "[worker %d] EOWNERDEAD: владелец мьютекса погиб, восстанавливаю\n", (int)getpid());
        CHECK_PT(pthread_mutex_consistent(&sh->mtx));
    } else if (rc) {
        fprintf(stderr, "pthread_mutex_lock: %s\n", strerror(rc));
        exit(EXIT_FAILURE);
    }
}

/* Код потомка: открыть объекты по имени и выполнить iters инкрементов */
static void worker(int idx, enum mode m, long iters, int crash, int hold_ms)
{
    int fd = shm_open(SHM_NAME, O_RDWR, 0);
    CHECK(fd);
    struct shared *sh = mmap(NULL, sizeof *sh, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (sh == MAP_FAILED) { perror("mmap"); _exit(EXIT_FAILURE); }
    CHECK(close(fd));                      /* отображение остаётся и после close */
    sem_t *sem = sem_open(SEM_NAME, 0);
    if (sem == SEM_FAILED) { perror("sem_open"); _exit(EXIT_FAILURE); }

    int rc = pthread_barrier_wait(&sh->start);
    if (rc && rc != PTHREAD_BARRIER_SERIAL_THREAD) { fprintf(stderr, "barrier: %s\n", strerror(rc)); _exit(1); }

    for (long i = 0; i < iters; i++) {
        switch (m) {
        case M_MUTEX:
            robust_lock(sh);
            sh->counter++;
            if (crash && idx == 0 && i == iters / 2) {
                fprintf(stderr, "[worker %d] имитирую сбой: abort() с захваченным мьютексом\n", (int)getpid());
                abort();
            }
            CHECK_PT(pthread_mutex_unlock(&sh->mtx));
            break;
        case M_SEM:
            while (sem_wait(sem) == -1) { if (errno != EINTR) { perror("sem_wait"); _exit(1); } }
            sh->counter++;
            CHECK(sem_post(sem));
            break;
        case M_NONE:
            sh->counter++;                 /* гонка: чтение-изменение-запись не атомарны */
            break;
        }
    }
    if (hold_ms > 0) usleep((useconds_t)hold_ms * 1000);
    CHECK(sem_close(sem));
    CHECK(munmap(sh, sizeof *sh));
    _exit(EXIT_SUCCESS);
}

struct result { long counter, expected, recovered; double secs; int failed; long vcsw, ivcsw; };

static struct result run(enum mode m, int nproc, long iters, int crash, int hold_ms, int verbose)
{
    /* 1. создать и инициализировать разделяемую память */
    shm_unlink(SHM_NAME);                  /* остатки от аварийного запуска */
    sem_unlink(SEM_NAME);
    int fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0600);
    CHECK(fd);
    CHECK(ftruncate(fd, sizeof(struct shared)));
    struct shared *sh = mmap(NULL, sizeof *sh, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (sh == MAP_FAILED) { perror("mmap"); exit(EXIT_FAILURE); }
    CHECK(close(fd));

    pthread_mutexattr_t ma;
    CHECK_PT(pthread_mutexattr_init(&ma));
    CHECK_PT(pthread_mutexattr_setpshared(&ma, PTHREAD_PROCESS_SHARED));
    CHECK_PT(pthread_mutexattr_setrobust(&ma, PTHREAD_MUTEX_ROBUST));
    CHECK_PT(pthread_mutex_init(&sh->mtx, &ma));
    CHECK_PT(pthread_mutexattr_destroy(&ma));

    pthread_barrierattr_t ba;
    CHECK_PT(pthread_barrierattr_init(&ba));
    CHECK_PT(pthread_barrierattr_setpshared(&ba, PTHREAD_PROCESS_SHARED));
    CHECK_PT(pthread_barrier_init(&sh->start, &ba, (unsigned)nproc + 1));
    CHECK_PT(pthread_barrierattr_destroy(&ba));
    sh->counter = 0;
    sh->recovered = 0;

    /* 2. именованный семафор со значением 1 (двоичный) */
    sem_t *sem = sem_open(SEM_NAME, O_CREAT | O_EXCL, 0600, 1);
    if (sem == SEM_FAILED) { perror("sem_open"); exit(EXIT_FAILURE); }

    /* 3. порождение процессов */
    pid_t pids[MAXPROC];
    struct rusage ru0, ru1;                 /* учёт переключений контекста потомков */
    CHECK(getrusage(RUSAGE_CHILDREN, &ru0));
    fflush(stdout);
    for (int i = 0; i < nproc; i++) {
        pids[i] = fork();
        CHECK(pids[i]);
        if (pids[i] == 0) worker(i, m, iters, crash, hold_ms);
    }
    int rc = pthread_barrier_wait(&sh->start);   /* старт всех одновременно */
    if (rc && rc != PTHREAD_BARRIER_SERIAL_THREAD) { fprintf(stderr, "barrier: %s\n", strerror(rc)); exit(1); }
    double t0 = now();

    /* 4. ожидание всех потомков */
    struct result r = { 0 };
    for (int i = 0; i < nproc; i++) {
        int st;
        pid_t p;
        while ((p = waitpid(pids[i], &st, 0)) == -1 && errno == EINTR) {}
        CHECK(p);
        if (WIFEXITED(st)) {
            if (verbose) printf("  процесс %d завершён, код %d\n", (int)p, WEXITSTATUS(st));
            if (WEXITSTATUS(st)) r.failed++;
        } else if (WIFSIGNALED(st)) {
            if (verbose) printf("  процесс %d убит сигналом %d (%s)\n", (int)p, WTERMSIG(st), strsignal(WTERMSIG(st)));
            r.failed++;
        }
    }
    CHECK(getrusage(RUSAGE_CHILDREN, &ru1));
    r.vcsw  = ru1.ru_nvcsw  - ru0.ru_nvcsw;
    r.ivcsw = ru1.ru_nivcsw - ru0.ru_nivcsw;
    r.secs = now() - t0 - (hold_ms > 0 ? hold_ms / 1000.0 : 0);
    r.counter = sh->counter;
    r.expected = (long)nproc * iters;
    r.recovered = sh->recovered;

    /* 5. освобождение ресурсов */
    CHECK_PT(pthread_barrier_destroy(&sh->start));
    CHECK_PT(pthread_mutex_destroy(&sh->mtx));
    CHECK(munmap(sh, sizeof *sh));
    CHECK(shm_unlink(SHM_NAME));
    CHECK(sem_close(sem));
    CHECK(sem_unlink(SEM_NAME));
    return r;
}

int main(int argc, char *argv[])
{
    enum mode m = M_MUTEX;
    int nproc = 4, crash = 0, bench = 0, hold_ms = 0, opt;
    long iters = 1000000;
    while ((opt = getopt(argc, argv, "m:p:i:ch:b")) != -1) {
        switch (opt) {
        case 'm':
            if (!strcmp(optarg, "mutex")) m = M_MUTEX;
            else if (!strcmp(optarg, "sem")) m = M_SEM;
            else if (!strcmp(optarg, "none")) m = M_NONE;
            else { fprintf(stderr, "режим: mutex|sem|none\n"); return 1; }
            break;
        case 'p': nproc = atoi(optarg); break;
        case 'i': iters = atol(optarg); break;
        case 'c': crash = 1; break;
        case 'h': hold_ms = atoi(optarg); break;
        case 'b': bench = 1; break;
        default:
            fprintf(stderr, "использование: %s [-m mutex|sem|none] [-p N] [-i ITER] [-c] [-h мс] | -b\n", argv[0]);
            return 1;
        }
    }
    if (nproc < 1 || nproc > MAXPROC || iters < 1) { fprintf(stderr, "неверные параметры\n"); return 1; }
    setvbuf(stdout, NULL, _IOLBF, 0);
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);

    if (!bench) {
        printf("[parent %d] режим %s, процессов %d, итераций на процесс %ld, ядер %ld\n",
               (int)getpid(), mode_name[m], nproc, iters, ncpu);
        struct result r = run(m, nproc, iters, crash, hold_ms, 1);
        printf("[parent %d] счётчик = %ld, ожидалось %ld, потеряно %ld (%.1f%%)\n", (int)getpid(),
               r.counter, r.expected, r.expected - r.counter, 100.0 * (r.expected - r.counter) / r.expected);
        printf("[parent %d] переключений контекста у потомков: добровольных %ld, принудительных %ld\n",
               (int)getpid(), r.vcsw, r.ivcsw);
        if (crash) printf("[parent %d] восстановлений мьютекса (EOWNERDEAD): %ld\n", (int)getpid(), r.recovered);
        printf("[parent %d] время %.3f с, %.1f нс на операцию; объекты %s и %s удалены\n", (int)getpid(),
               r.secs, r.secs * 1e9 / r.expected, SHM_NAME, SEM_NAME);
        return 0;
    }

    int counts[] = { 1, 2, 4, 8 };
    FILE *csv = fopen("shmcnt_bench.csv", "w");
    if (!csv) perror("fopen");
    else fprintf(csv, "procs,mutex_s,sem_s,mutex_ns_op,sem_ns_op,mutex_vcsw,sem_vcsw\n");
    printf("Итераций на процесс: %ld, ядер: %ld, время — лучшее из 3 запусков\n", iters, ncpu);
    printf("процессов  mutex, с  sem, с  mutex нс/оп  sem нс/оп  sem/mutex  перекл.mutex  перекл.sem  верно\n");
    for (int k = 0; k < 4; k++) {
        double best[2] = { 1e30, 1e30 };
        long csw[2] = { 0, 0 };              /* добровольные переключения в лучшем прогоне */
        int ok = 1;
        for (int rep = 0; rep < 3; rep++)
            for (int mm = 0; mm < 2; mm++) {
                struct result r = run((enum mode)mm, counts[k], iters, 0, 0, 0);
                if (r.counter != r.expected || r.failed) ok = 0;
                if (r.secs < best[mm]) { best[mm] = r.secs; csw[mm] = r.vcsw; }
            }
        double ops = (double)counts[k] * iters;
        printf("%9d %9.3f %7.3f %12.1f %10.1f %10.2f %13ld %11ld  %s\n", counts[k], best[0], best[1],
               best[0] * 1e9 / ops, best[1] * 1e9 / ops, best[1] / best[0], csw[0], csw[1], ok ? "да" : "НЕТ");
        if (csv) fprintf(csv, "%d,%.4f,%.4f,%.2f,%.2f,%ld,%ld\n", counts[k], best[0], best[1],
                         best[0] * 1e9 / ops, best[1] * 1e9 / ops, csw[0], csw[1]);
    }
    if (csv && fclose(csv) == EOF) perror("fclose");
    return 0;
}
