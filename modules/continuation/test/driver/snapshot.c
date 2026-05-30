#include "harness.h"
#include "sum_to.cont.h"

#include <string.h>

int main(void)
{
    for (int cut = 0; cut <= 6; cut++)
    {
        g_case = "snapshot";

        Mel_Cont_Frame_sum_to f = {0};
        f.n                     = 12;
        i64 y;
        for (int i = 0; i < cut; i++) sum_to__resume(&f, &y);

        Mel_Cont_Frame_sum_to copy;
        memcpy(&copy, &f, sizeof f);

        Trace a = {0};
        Trace b = {0};
        i64   ya;
        i64   yb;
        while (sum_to__resume(&f, &ya)) trace_push(&a, ya);
        while (sum_to__resume(&copy, &yb)) trace_push(&b, yb);

        check_traces("rewind", &a, &b);
        check_eq_i64("ret", f.__ret, copy.__ret);

        trace_free(&a);
        trace_free(&b);
    }
    return harness_report("snapshot");
}
