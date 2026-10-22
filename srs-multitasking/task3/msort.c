/* msort.c — вариант 8, задание 3: параллельная сортировка слиянием массива
 * из 10^7 элементов с ограничением глубины порождения потоков.
 *
 * Алгоритм: на уровне рекурсии depth < max_depth текущий поток создаёт
 * новый поток для левой половины, правую сортирует сам, затем pthread_join
 * и слияние. Глубже max_depth сортировка идёт последовательно. Число
 * листовых потоков = 2^max_depth, поэтому для T потоков max_depth = log2(T).
 * Разные потоки работают с непересекающимися частями массива и буфера,
 * поэтому никаких блокировок не требуется — только pthread_join.
 *
 * Использование:
 *   msort [-n N] [-t потоков] [-r повторов] [-s seed] [-b]
 *   -b — бенчмарк: прогон для 1, 2, 4, 8 потоков, вывод таблицы и CSV      */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#define INSERTION_CUTOFF 32
#define THREAD_STACK     (256 * 1024)     /* размер стека потока через pthread_attr */

static int *tmpbuf;                        /* общий буфер для слияния, размер N */
static pthread_attr_t attr;

struct task { int *a; size_t lo, hi; int depth, max_depth; };

static void insertion(int *a, size_t lo, size_t hi)
{
    for (size_t i = lo + 1; i < hi; i++) {
        int v = a[i];
        size_t j = i;
        while (j > lo && a[j - 1] > v) { a[j] = a[j - 1]; j--; }
        a[j] = v;
    }
}

/* слить отсортированные [lo,mid) и [mid,hi) через tmpbuf[lo..hi) */
static void merge(int *a, size_t lo, size_t mid, size_t hi)
{
    if (a[mid - 1] <= a[mid]) return;      /* уже упорядочено */
    size_t i = lo, j = mid, k = lo;
    while (i < mid && j < hi) tmpbuf[k++] = (a[i] <= a[j]) ? a[i++] : a[j++];
    while (i < mid) tmpbuf[k++] = a[i++];
    while (j < hi)  tmpbuf[k++] = a[j++];
    memcpy(a + lo, tmpbuf + lo, (hi - lo) * sizeof *a);
}

static void seq_sort(int *a, size_t lo, size_t hi)
{
    if (hi - lo <= INSERTION_CUTOFF) { insertion(a, lo, hi); return; }
    size_t mid = lo + (hi - lo) / 2;
    seq_sort(a, lo, mid);
    seq_sort(a, mid, hi);
    merge(a, lo, mid, hi);
}

static void *par_sort(void *arg)
{
    struct task *t = arg;
    if (t->depth >= t->max_depth || t->hi - t->lo < 2 * INSERTION_CUTOFF) {
        seq_sort(t->a, t->lo, t->hi);
        return NULL;
    }
    size_t mid = t->lo + (t->hi - t->lo) / 2;
    struct task left  = { t->a, t->lo, mid,   t->depth + 1, t->max_depth };
    struct task right = { t->a, mid,   t->hi, t->depth + 1, t->max_depth };
    pthread_t th;
    int rc = pthread_create(&th, &attr, par_sort, &left);
    if (rc) {                               /* не удалось — сортируем сами */
        fprintf(stderr, "pthread_create: %s, левая половина последовательно\n", strerror(rc));
        par_sort(&left);
    }
    par_sort(&right);                       /* правую половину — в текущем потоке */
    if (!rc && (rc = pthread_join(th, NULL)))
        fprintf(stderr, "pthread_join: %s\n", strerror(rc));
    merge(t->a, t->lo, mid, t->hi);
    return NULL;
}

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int log2i(int t) { int d = 0; while ((1 << (d + 1)) <= t) d++; return d; }

static int is_sorted(const int *a, size_t n)
{
    for (size_t i = 1; i < n; i++) if (a[i - 1] > a[i]) return 0;
    return 1;
}

static int cmp_int(const void *x, const void *y)
{
    int a = *(const int *)x, b = *(const int *)y;
    return (a > b) - (a < b);
}

/* сортирует копию src, возвращает лучшее время из reps прогонов (с) */
static double timed_sort(const int *src, int *work, size_t n, int threads, int reps)
{
    double best = 1e30;
    for (int r = 0; r < reps; r++) {
        memcpy(work, src, n * sizeof *work);
        struct task root = { work, 0, n, 0, log2i(threads) };
        double t0 = now();
        par_sort(&root);
        double dt = now() - t0;
        if (dt < best) best = dt;
        if (!is_sorted(work, n)) { fprintf(stderr, "ОШИБКА: массив не отсортирован!\n"); exit(2); }
    }
    return best;
}

int main(int argc, char *argv[])
{
    size_t n = 10000000;
    int threads = 4, reps = 3, bench = 0, opt;
    unsigned seed = 12345;
    while ((opt = getopt(argc, argv, "n:t:r:s:b")) != -1) {
        switch (opt) {
        case 'n': n = strtoull(optarg, NULL, 10); break;
        case 't': threads = atoi(optarg); break;
        case 'r': reps = atoi(optarg); break;
        case 's': seed = (unsigned)atoi(optarg); break;
        case 'b': bench = 1; break;
        default:
            fprintf(stderr, "использование: %s [-n N] [-t потоков] [-r повторов] [-s seed] [-b]\n", argv[0]);
            return 1;
        }
    }
    if (n < 2 || threads < 1 || reps < 1) { fprintf(stderr, "неверные параметры\n"); return 1; }

    int rc = pthread_attr_init(&attr);
    if (!rc) rc = pthread_attr_setstacksize(&attr, THREAD_STACK);
    if (rc) { fprintf(stderr, "pthread_attr: %s\n", strerror(rc)); return 1; }

    int *src = malloc(n * sizeof *src), *work = malloc(n * sizeof *work);
    tmpbuf = malloc(n * sizeof *tmpbuf);
    if (!src || !work || !tmpbuf) { perror("malloc"); return 1; }
    srand(seed);
    for (size_t i = 0; i < n; i++) src[i] = rand();

    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    printf("Сортировка слиянием: N = %zu, повторов = %d, ядер ЦП (онлайн) = %ld\n", n, reps, ncpu);

    if (!bench) {
        double t = timed_sort(src, work, n, threads, reps);
        printf("потоков: %d (глубина %d)  время: %.3f с  массив отсортирован: да\n",
               1 << log2i(threads), log2i(threads), t);
    } else {
        /* эталон: стандартный qsort */
        memcpy(work, src, n * sizeof *work);
        double q0 = now();
        qsort(work, n, sizeof *work, cmp_int);
        double tq = now() - q0;
        int counts[] = { 1, 2, 4, 8 };
        double t1 = 0;
        /* заголовок выровнен вручную: printf считает ширину в байтах, а не в символах UTF-8 */
        printf(" потоков  глубина   время, с  ускорение   эффективн.\n");
        FILE *csv = fopen("msort_bench.csv", "w");
        if (!csv) perror("fopen msort_bench.csv");
        else fprintf(csv, "threads,depth,time_s,speedup,efficiency\n");
        for (int k = 0; k < 4; k++) {
            double t = timed_sort(src, work, n, counts[k], reps);
            if (k == 0) t1 = t;
            printf("%8d %8d %10.3f %10.2f %11.0f%%\n", counts[k], log2i(counts[k]), t, t1 / t,
                   100.0 * t1 / t / counts[k]);
            if (csv) fprintf(csv, "%d,%d,%.4f,%.3f,%.3f\n", counts[k], log2i(counts[k]), t, t1 / t, t1 / t / counts[k]);
        }
        if (csv && fclose(csv) == EOF) perror("fclose");
        printf("для сравнения qsort (1 поток): %.3f с; все результаты проверены is_sorted()\n", tq);
    }
    pthread_attr_destroy(&attr);
    free(src); free(work); free(tmpbuf);
    return 0;
}
