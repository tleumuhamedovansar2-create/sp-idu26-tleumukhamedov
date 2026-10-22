/* common.h — общий протокол клиента и сервера (вариант 8, задание 2) */
#ifndef COMMON_H
#define COMMON_H
#include <sys/types.h>

#define SERVER_FIFO   "/tmp/calc_server.fifo"
#define CLIENT_FIFO_FMT "/tmp/calc_client.%d.fifo"
#define EXPR_MAX 200
#define MSG_MAX  120

/* Размер запроса < PIPE_BUF (4096), поэтому запись в общий FIFO атомарна:
 * запросы разных клиентов не перемешиваются. */
struct request {
    pid_t client_pid;          /* по нему сервер находит личный FIFO клиента */
    int   seq;                 /* номер запроса у клиента */
    char  expr[EXPR_MAX];      /* арифметическое выражение */
};

enum { ST_OK = 0, ST_SYNTAX = 1, ST_DIVZERO = 2 };

struct response {
    int    seq;
    int    status;             /* ST_OK / ST_SYNTAX / ST_DIVZERO */
    pid_t  worker_pid;         /* какой процесс сервера посчитал */
    double value;
    char   msg[MSG_MAX];
};

#endif
