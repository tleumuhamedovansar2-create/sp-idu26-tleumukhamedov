# СРС 1_1 «Многозадачность в Linux» — вариант 8

Дисциплина «Системное программирование», группа ИС-25.
Сборка: `gcc -Wall -Wextra -std=c11 -pthread`, в каждом каталоге свой Makefile (`make`, `make clean`).

| Каталог | Задание | Программы | Пример запуска |
|---|---|---|---|
| task1 | procinfo + утилита procfd | procinfo, procfd, fddemo | `./fddemo & ./procfd $!` |
| task2 | клиент-сервер на FIFO | server, client, clients | `./server -d 1500 & ./clients 5` |
| task3 | гонка + параллельная сортировка слиянием | race, race_tsan, msort, msort_tsan | `./race all 5`, `make bench` |
| task4 | межпроцессный мьютекс vs именованный семафор | shmcnt | `./shmcnt -m mutex -p 4`, `make bench` |

Подробности — в отчётах по каждому заданию.
