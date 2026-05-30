#include "harness.h"
#include "countdown.cont.h"

int main(void)
{
    for (i32 from = -3; from <= 16; from++)
    {
        g_case = "countdown";

        int expected = from > 0 ? from : 0;

        Mel_Cont_Frame_countdown f = {0};
        f.from                     = from;
        int got                    = 0;
        while (countdown__resume(&f)) got++;

        check_eq_i64("suspends", expected, got);
        check_eq_i64("done", false, countdown__resume(&f));
    }
    return harness_report("countdown_diff");
}
