/* eval.c — рекурсивный нисходящий разбор арифметического выражения
 *   expr   := term   { ('+'|'-') term }
 *   term   := factor { ('*'|'/'|'%') factor }
 *   factor := ['-'|'+'] ( number | '(' expr ')' )                    */
#include "eval.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>

struct parser { const char *p; int err; char *msg; int msglen; };

static void skip(struct parser *ps) { while (isspace((unsigned char)*ps->p)) ps->p++; }

static void fail(struct parser *ps, int code, const char *what)
{
    if (!ps->err) {
        ps->err = code;
        snprintf(ps->msg, (size_t)ps->msglen, "%s", what);
    }
}

static double expr(struct parser *ps);

static double factor(struct parser *ps)
{
    skip(ps);
    if (*ps->p == '-') { ps->p++; return -factor(ps); }
    if (*ps->p == '+') { ps->p++; return  factor(ps); }
    if (*ps->p == '(') {
        ps->p++;
        double v = expr(ps);
        skip(ps);
        if (*ps->p != ')') { fail(ps, 1, "ожидалась ')'"); return 0; }
        ps->p++;
        return v;
    }
    char *end;
    double v = strtod(ps->p, &end);
    if (end == ps->p) { fail(ps, 1, "ожидалось число"); return 0; }
    ps->p = end;
    return v;
}

static double term(struct parser *ps)
{
    double v = factor(ps);
    for (;;) {
        skip(ps);
        char op = *ps->p;
        if (op != '*' && op != '/' && op != '%') return v;
        ps->p++;
        double r = factor(ps);
        if (op == '*') v *= r;
        else if (r == 0) { fail(ps, 2, "деление на ноль"); return 0; }
        else if (op == '/') v /= r;
        else v = fmod(v, r);
    }
}

static double expr(struct parser *ps)
{
    double v = term(ps);
    for (;;) {
        skip(ps);
        char op = *ps->p;
        if (op != '+' && op != '-') return v;
        ps->p++;
        double r = term(ps);
        v = (op == '+') ? v + r : v - r;
    }
}

int eval_expr(const char *s, double *result, char *err, int errlen)
{
    struct parser ps = { s, 0, err, errlen };
    err[0] = '\0';
    double v = expr(&ps);
    skip(&ps);
    if (!ps.err && *ps.p != '\0') fail(&ps, 1, "лишние символы в конце");
    if (ps.err) return ps.err;
    *result = v;
    return 0;
}
