#ifndef EVAL_H
#define EVAL_H
/* Вычислить выражение: числа, + - * / %, скобки, унарный минус.
 * Возвращает 0 — успех, 1 — синтаксическая ошибка, 2 — деление на ноль. */
int eval_expr(const char *s, double *result, char *err, int errlen);
#endif
