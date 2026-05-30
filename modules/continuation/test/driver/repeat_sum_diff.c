#include "harness.h"
#include "repeat_sum.cont.h"

static void oracle(i32 n, Trace* t, i32* ret)
{
    i32 total = 0;
    i32 k     = 0;
    do
    {
        total += k;
        trace_push(t, total);
        k++;
    } while (k < n);
    *ret = total;
}

int main(void)
{
    for (i32 n = -2; n <= 12; n++)
    {
        g_case = "repeat_sum";

        Trace expected = {0};
        i32   eret     = 0;
        oracle(n, &expected, &eret);

        Mel_Cont_Frame_repeat_sum f = {0};
        f.n                         = n;
        Trace got                   = {0};
        i32   y;
        while (repeat_sum__resume(&f, &y)) trace_push(&got, y);

        check_traces("seq", &expected, &got);
        check_eq_i64("ret", eret, f.__ret);

        trace_free(&expected);
        trace_free(&got);
    }
    return harness_report("repeat_sum_diff");
}
