/* race.c — общая часть задания 3: гонка на общем счётчике и три способа её
 * устранения (мьютекс, atomic_int, локальные счётчики).
 *
 * Использование: race <none|mutex|atomic|local|all> [запусков]
 *   none   — 4 потока по 10^6 раз делают counter++ без синхронизации
 *   mutex  — инкремент под pthread_mutex_t
 *   atomic — atomic_fetch_add над atomic_int
 *   local  — каждый поток считает в свою локальную переменную, в конце
 *            одно сложение под мьютексом
 *   all    — все четыре режима подряд с замером времени               */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

#define NTHREADS 4
#define NITER    1000000

/* volatile — только чтобы компилятор с -O2 не свернул цикл в одно сложение;
 * атомарности volatile НЕ даёт, гонка остаётся. */
static volatile long counter_plain;
static long          counter_mutex;
static atomic_int    counter_atomic;
static long          counter_local;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

static void *thr_none(void *arg)
{
    (void)arg;
    for (int i = 0; i < NITER; i++)
        counter_plain++;               /* load — add — store: не атомарно */
    return NULL;
}

static void *thr_mutex(void *arg)
{
    (void)arg;
    for (int i = 0; i < NITER; i++) {
        pthread_mutex_lock(&mtx);
        counter_mutex++;
        pthread_mutex_unlock(&mtx);
    }
    return NULL;
}

static void *thr_atomic(void *arg)
{
    (void)arg;
    for (int i = 0; i < NITER; i++)
        atomic_fetch_add_explicit(&counter_atomic, 1, memory_order_relaxed);
    return NULL;
}

static void *thr_local(void *arg)
{
    (void)arg;
    long local = 0;
    for (int i = 0; i < NITER; i++)
        local++;
    pthread_mutex_lock(&mtx);          /* одна синхронизация на поток */
    counter_local += local;
    pthread_mutex_unlock(&mtx);
    return NULL;
}

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

/* Запустить NTHREADS потоков с функцией fn, вернуть время в мс */
static double run_threads(void *(*fn)(void *))
{
    pthread_t th[NTHREADS];
    double t0 = now();
    for (int i = 0; i < NTHREADS; i++) {
        int rc = pthread_create(&th[i], NULL, fn, NULL);
        if (rc) { fprintf(stderr, "pthread_create: %s\n", strerror(rc)); exit(1); }
    }
    for (int i = 0; i < NTHREADS; i++) {
        int rc = pthread_join(th[i], NULL);
        if (rc) { fprintf(stderr, "pthread_join: %s\n", strerror(rc)); exit(1); }
    }
    return (now() - t0) * 1000.0;
}

static void run_mode(const char *mode, int runs)
{
    const long expected = (long)NTHREADS * NITER;
    double total_ms = 0;
    printf("Режим %-6s (ожидается %ld):\n", mode, expected);
    for (int r = 1; r <= runs; r++) {
        long result;
        double ms;
        if (!strcmp(mode, "none"))        { counter_plain = 0;  ms = run_threads(thr_none);   result = counter_plain; }
        else if (!strcmp(mode, "mutex"))  { counter_mutex = 0;  ms = run_threads(thr_mutex);  result = counter_mutex; }
        else if (!strcmp(mode, "atomic")) { atomic_store(&counter_atomic, 0); ms = run_threads(thr_atomic); result = atomic_load(&counter_atomic); }
        else if (!strcmp(mode, "local"))  { counter_local = 0;  ms = run_threads(thr_local);  result = counter_local; }
        else { fprintf(stderr, "неизвестный режим %s\n", mode); exit(1); }
        total_ms += ms;
        printf("  запуск %d: счётчик = %8ld  потеряно %8ld (%5.1f%%)  время %8.2f мс\n",
               r, result, expected - result, 100.0 * (expected - result) / expected, ms);
    }
    printf("  среднее время: %.2f мс\n", total_ms / runs);
}

int main(int argc, char *argv[])
{
    const char *mode = argc > 1 ? argv[1] : "all";
    int runs = argc > 2 ? atoi(argv[2]) : 5;
    if (runs < 1) runs = 1;
    if (!strcmp(mode, "all")) {
        const char *modes[] = { "none", "mutex", "atomic", "local" };
        for (int i = 0; i < 4; i++) run_mode(modes[i], runs);
    } else {
        run_mode(mode, runs);
    }
    return 0;
}
