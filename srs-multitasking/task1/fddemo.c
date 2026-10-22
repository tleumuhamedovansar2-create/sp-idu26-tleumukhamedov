/* fddemo.c — тестовый процесс с разными типами дескрипторов для procfd:
 * обычный файл (чтение и дозапись), канал, сокет, /dev/null, eventfd, каталог. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/eventfd.h>

#define CHECK(x) do { if ((x) == -1) { perror(#x); exit(1); } } while (0)

int main(void)
{
    int f1 = open("/etc/passwd", O_RDONLY);           CHECK(f1);
    char buf[100];
    CHECK(read(f1, buf, sizeof buf));                  /* сдвинем позицию на 100 */
    int f2 = open("demo.log", O_WRONLY | O_CREAT | O_APPEND, 0644); CHECK(f2);
    CHECK(write(f2, "hello\n", 6));
    int p[2];                                          CHECK(pipe2(p, O_CLOEXEC));
    int s = socket(AF_INET, SOCK_STREAM, 0);           CHECK(s);
    int nul = open("/dev/null", O_RDWR | O_NONBLOCK);  CHECK(nul);
    int ev = eventfd(0, 0);                            CHECK(ev);
    int dir = open("/tmp", O_RDONLY | O_DIRECTORY);    CHECK(dir);
    printf("fddemo: PID %d, открыто дескрипторов 3..%d; ожидаю 60 с\n", (int)getpid(), dir);
    fflush(stdout);
    sleep(60);
    return 0;
}
