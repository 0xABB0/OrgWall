#include "harness.h"
#include "classify.gen.h"

static void oracle(i32 n, Trace* t, i32* ret)
{
    i32 seen = 0;
    if (n > 0)
    {
        seen = 1;
        trace_push(t, seen);
    }
    else
    {
        seen = -1;
        trace_push(t, seen);
    }
    trace_push(t, seen + n);
    *ret = seen;
}

int main(void)
{
    for (i32 n = -8; n <= 8; n++)
    {
        g_case = "classify";

        Trace expected = {0};
        i32   eret     = 0;
        oracle(n, &expected, &eret);

        Mel_Cont_Frame_classify f = {0};
        f.n                       = n;
        Trace got                 = {0};
        i32   y;
        while (classify__resume(&f, &y)) trace_push(&got, y);

        check_traces("seq", &expected, &got);
        check_eq_i64("ret", eret, f.__ret);

        trace_free(&expected);
        trace_free(&got);
    }
    return harness_report("classify_diff");
}
