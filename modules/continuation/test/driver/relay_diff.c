#include "harness.h"
#include "relay.gen.h"

static void oracle(i32 base, Trace* t, i32* ret)
{
    trace_push(t, base);
    trace_push(t, base + 1);
    trace_push(t, base + 2);
    trace_push(t, base + 100);
    *ret = base;
}

int main(void)
{
    for (i32 base = 0; base <= 5; base++)
    {
        g_case = "relay";

        Trace expected = {0};
        i32   eret     = 0;
        oracle(base, &expected, &eret);

        Mel_Cont_Frame_relay f = {0};
        f.base                 = base;
        Trace got              = {0};
        i32   y;
        while (relay__resume(&f, &y)) trace_push(&got, y);

        check_traces("seq", &expected, &got);
        check_eq_i64("ret", eret, f.__ret);

        trace_free(&expected);
        trace_free(&got);
    }
    return harness_report("relay_diff");
}
