#include "harness.h"
#include "sum_to.cont.h"

static void oracle(i64 n, Trace* t, i64* ret)
{
    i64 acc = 0;
    for (i32 i = 0; i < n; i++)
    {
        acc += i;
        trace_push(t, acc);
    }
    *ret = acc;
}

int main(void)
{
    for (i64 n = 0; n <= 24; n++)
    {
        g_case = "sum_to";

        Trace expected = {0};
        i64   eret     = 0;
        oracle(n, &expected, &eret);

        Mel_Cont_Frame_sum_to f = {0};
        f.n                     = n;
        Trace got               = {0};
        i64   y;
        while (sum_to__resume(&f, &y)) trace_push(&got, y);

        check_traces("seq", &expected, &got);
        check_eq_i64("ret", eret, f.__ret);
        check_eq_i64("done", false, sum_to__resume(&f, &y));

        trace_free(&expected);
        trace_free(&got);
    }
    return harness_report("sum_to_diff");
}
