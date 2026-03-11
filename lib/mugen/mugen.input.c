#include "mugen.input.h"
#include <string.h>
#include <limits.h>

static i32 max2(i32 a, i32 b) { return a > b ? a : b; }
static i32 max3(i32 a, i32 b, i32 c) { return max2(a, max2(b, c)); }
static i32 min2(i32 a, i32 b) { return a < b ? a : b; }
static i32 min3(i32 a, i32 b, i32 c) { return min2(a, min2(b, c)); }
static i32 absi(i32 x) { return x < 0 ? -x : x; }

static i32 ignore_recent(i32 counter)
{
    if (counter == 1) return INT_MIN;
    return counter;
}

static void update_counter(i32* counter, bool held)
{
    if (held && *counter <= 0)       *counter = 1;
    else if (held && *counter > 0)   *counter += 1;
    else if (!held && *counter > 0)  *counter = -1;
    else if (!held && *counter <= 0) *counter -= 1;
}

static void socd_resolve(bool* a, bool* b, bool* first_a, bool* first_b, Socd_Mode mode)
{
    if (!*a && !*b)
    {
        *first_a = false;
        *first_b = false;
        return;
    }

    if (*a && !*b) { *first_a = true; *first_b = false; }
    if (!*a && *b) { *first_a = false; *first_b = true; }

    if (*a && *b)
    {
        switch (mode)
        {
            case SOCD_NEUTRAL:
                *a = false;
                *b = false;
                break;
            case SOCD_UP_PRIORITY:
                *b = false;
                break;
            case SOCD_FIRST_WINS:
                if (*first_a && !*first_b) *b = false;
                else if (*first_b && !*first_a) *a = false;
                else { *a = false; *b = false; }
                break;
        }
    }
}

void input_buffer_init(Input_Buffer* buf, bool facing_right)
{
    memset(buf, 0, sizeof(*buf));
    buf->facing_right = facing_right;
}

void input_buffer_update(Input_Buffer* buf, bool U, bool D, bool L, bool R,
                         bool a, bool b, bool c, bool x, bool y, bool z,
                         bool s, bool d, bool w, bool m)
{
    socd_resolve(&U, &D, &buf->socd_first[0], &buf->socd_first[1], buf->socd_mode);

    if (buf->socd_mode == SOCD_UP_PRIORITY && U && D) D = false;
    socd_resolve(&L, &R, &buf->socd_first[2], &buf->socd_first[3], buf->socd_mode);

    bool F_raw, B_raw;
    if (buf->facing_right) { F_raw = R; B_raw = L; }
    else                   { F_raw = L; B_raw = R; }

    bool N = !(U || D || L || R);

    buf->Up = buf->Ub; buf->Dp = buf->Db;
    buf->Lp = buf->Lb; buf->Rp = buf->Rb;
    buf->Bp = buf->Bb; buf->Fp = buf->Fb;
    buf->Np = buf->Nb;
    buf->ap = buf->ab; buf->bp = buf->bb; buf->cp = buf->cb;
    buf->xp = buf->xb; buf->yp = buf->yb; buf->zp = buf->zb;
    buf->sp = buf->sb; buf->dp = buf->db; buf->wp = buf->wb; buf->mp = buf->mb;

    update_counter(&buf->Ub, U);
    update_counter(&buf->Db, D);
    update_counter(&buf->Lb, L);
    update_counter(&buf->Rb, R);
    update_counter(&buf->Bb, B_raw);
    update_counter(&buf->Fb, F_raw);
    update_counter(&buf->Nb, N);

    update_counter(&buf->ab, a);
    update_counter(&buf->bb, b);
    update_counter(&buf->cb, c);
    update_counter(&buf->xb, x);
    update_counter(&buf->yb, y);
    update_counter(&buf->zb, z);
    update_counter(&buf->sb, s);
    update_counter(&buf->db, d);
    update_counter(&buf->wb, w);
    update_counter(&buf->mb, m);
}

static i32* buf_counter(Input_Buffer* buf, Cmd_Key_Id key)
{
    switch (key)
    {
        case CK_U: return &buf->Ub;
        case CK_D: return &buf->Db;
        case CK_B: return &buf->Bb;
        case CK_F: return &buf->Fb;
        case CK_L: return &buf->Lb;
        case CK_R: return &buf->Rb;
        case CK_N: return &buf->Nb;
        case CK_a: return &buf->ab;
        case CK_b: return &buf->bb;
        case CK_c: return &buf->cb;
        case CK_x: return &buf->xb;
        case CK_y: return &buf->yb;
        case CK_z: return &buf->zb;
        case CK_s: return &buf->sb;
        case CK_d: return &buf->db;
        case CK_w: return &buf->wb;
        case CK_m: return &buf->mb;
        default:   return &buf->Nb;
    }
}

static i32* buf_prev(Input_Buffer* buf, Cmd_Key_Id key)
{
    switch (key)
    {
        case CK_U: return &buf->Up;
        case CK_D: return &buf->Dp;
        case CK_B: return &buf->Bp;
        case CK_F: return &buf->Fp;
        case CK_L: return &buf->Lp;
        case CK_R: return &buf->Rp;
        case CK_N: return &buf->Np;
        case CK_a: return &buf->ap;
        case CK_b: return &buf->bp;
        case CK_c: return &buf->cp;
        case CK_x: return &buf->xp;
        case CK_y: return &buf->yp;
        case CK_z: return &buf->zp;
        case CK_s: return &buf->sp;
        case CK_d: return &buf->dp;
        case CK_w: return &buf->wp;
        case CK_m: return &buf->mp;
        default:   return &buf->Np;
    }
}

static i32 state_dir_hold_no_dollar(Input_Buffer* buf, Cmd_Key_Id key)
{
    i32 conflict, intended;

    switch (key)
    {
        case CK_U: conflict = -max3(buf->Bb, buf->Db, buf->Fb); intended = buf->Ub; break;
        case CK_D: conflict = -max3(buf->Bb, buf->Ub, buf->Fb); intended = buf->Db; break;
        case CK_B: conflict = -max3(buf->Db, buf->Ub, buf->Fb); intended = buf->Bb; break;
        case CK_F: conflict = -max3(buf->Db, buf->Ub, buf->Bb); intended = buf->Fb; break;
        case CK_L: conflict = -max3(buf->Db, buf->Ub, buf->Rb); intended = buf->Lb; break;
        case CK_R: conflict = -max3(buf->Db, buf->Ub, buf->Lb); intended = buf->Rb; break;
        case CK_N: return buf->Nb;

        case CK_UB: conflict = -max2(buf->Db, buf->Fb); intended = min2(buf->Ub, buf->Bb); break;
        case CK_UF: conflict = -max2(buf->Db, buf->Bb); intended = min2(buf->Ub, buf->Fb); break;
        case CK_DB: conflict = -max2(buf->Ub, buf->Fb); intended = min2(buf->Db, buf->Bb); break;
        case CK_DF: conflict = -max2(buf->Ub, buf->Bb); intended = min2(buf->Db, buf->Fb); break;
        case CK_UL: conflict = -max2(buf->Db, buf->Rb); intended = min2(buf->Ub, buf->Lb); break;
        case CK_UR: conflict = -max2(buf->Db, buf->Lb); intended = min2(buf->Ub, buf->Rb); break;
        case CK_DL: conflict = -max2(buf->Ub, buf->Rb); intended = min2(buf->Db, buf->Lb); break;
        case CK_DR: conflict = -max2(buf->Ub, buf->Lb); intended = min2(buf->Db, buf->Rb); break;

        default: return 0;
    }

    return min2(conflict, intended);
}

static i32 state_dir_hold_dollar(Input_Buffer* buf, Cmd_Key_Id key)
{
    i32 all_min = min2(absi(buf->Ub), min2(absi(buf->Db), min2(absi(buf->Bb), absi(buf->Fb))));

    switch (key)
    {
        case CK_U: return buf->Ub > 0 ? all_min : 0;
        case CK_D: return buf->Db > 0 ? all_min : 0;
        case CK_B: return buf->Bb > 0 ? all_min : 0;
        case CK_F: return buf->Fb > 0 ? all_min : 0;
        case CK_L: return buf->Lb > 0 ? all_min : 0;
        case CK_R: return buf->Rb > 0 ? all_min : 0;

        case CK_UB: return (buf->Ub > 0 && buf->Bb > 0) ? all_min : 0;
        case CK_UF: return (buf->Ub > 0 && buf->Fb > 0) ? all_min : 0;
        case CK_DB: return (buf->Db > 0 && buf->Bb > 0) ? all_min : 0;
        case CK_DF: return (buf->Db > 0 && buf->Fb > 0) ? all_min : 0;
        case CK_UL: return (buf->Ub > 0 && buf->Lb > 0) ? all_min : 0;
        case CK_UR: return (buf->Ub > 0 && buf->Rb > 0) ? all_min : 0;
        case CK_DL: return (buf->Db > 0 && buf->Lb > 0) ? all_min : 0;
        case CK_DR: return (buf->Db > 0 && buf->Rb > 0) ? all_min : 0;

        case CK_N:
        {
            i32 r = all_min;
            r = min2(r, absi(buf->ab)); r = min2(r, absi(buf->bb)); r = min2(r, absi(buf->cb));
            r = min2(r, absi(buf->xb)); r = min2(r, absi(buf->yb)); r = min2(r, absi(buf->zb));
            r = min2(r, absi(buf->sb)); r = min2(r, absi(buf->db)); r = min2(r, absi(buf->wb));
            return r;
        }

        default: return 0;
    }
}

static i32 state_dir_release_no_dollar(Input_Buffer* buf, Cmd_Key_Id key)
{
    switch (key)
    {
        case CK_U: case CK_D: case CK_B: case CK_F:
        case CK_L: case CK_R:
        {
            i32 counter = *buf_counter(buf, key);
            i32 prev    = *buf_prev(buf, key);
            if (!(counter < 0 || prev > 0)) return 0;

            i32 conflict, intended;
            switch (key)
            {
                case CK_U: conflict = -max3(buf->Bb, buf->Db, buf->Fb); intended = buf->Ub; break;
                case CK_D: conflict = -max3(buf->Bb, buf->Ub, buf->Fb); intended = buf->Db; break;
                case CK_B: conflict = -max3(buf->Db, buf->Ub, buf->Fb); intended = buf->Bb; break;
                case CK_F: conflict = -max3(buf->Db, buf->Ub, buf->Bb); intended = buf->Fb; break;
                case CK_L: conflict = -max3(buf->Db, buf->Ub, buf->Rb); intended = buf->Lb; break;
                case CK_R: conflict = -max3(buf->Db, buf->Ub, buf->Lb); intended = buf->Rb; break;
                default: return 0;
            }
            return -min2(conflict, intended);
        }

        case CK_N: return -buf->Nb;

        case CK_UB: case CK_UF: case CK_DB: case CK_DF:
        case CK_UL: case CK_UR: case CK_DL: case CK_DR:
        {
            Cmd_Key_Id comp_a, comp_b, opp_a, opp_b;
            switch (key)
            {
                case CK_UB: comp_a = CK_U; comp_b = CK_B; opp_a = CK_D; opp_b = CK_F; break;
                case CK_UF: comp_a = CK_U; comp_b = CK_F; opp_a = CK_D; opp_b = CK_B; break;
                case CK_DB: comp_a = CK_D; comp_b = CK_B; opp_a = CK_U; opp_b = CK_F; break;
                case CK_DF: comp_a = CK_D; comp_b = CK_F; opp_a = CK_U; opp_b = CK_B; break;
                case CK_UL: comp_a = CK_U; comp_b = CK_L; opp_a = CK_D; opp_b = CK_R; break;
                case CK_UR: comp_a = CK_U; comp_b = CK_R; opp_a = CK_D; opp_b = CK_L; break;
                case CK_DL: comp_a = CK_D; comp_b = CK_L; opp_a = CK_U; opp_b = CK_R; break;
                case CK_DR: comp_a = CK_D; comp_b = CK_R; opp_a = CK_U; opp_b = CK_L; break;
                default: return 0;
            }

            i32 ca = *buf_counter(buf, comp_a), pa = *buf_prev(buf, comp_a);
            i32 cb = *buf_counter(buf, comp_b), pb = *buf_prev(buf, comp_b);

            if (!((ca < 0 || pa > 0) && (cb < 0 || pb > 0))) return 0;

            i32 conflict = -max2(*buf_counter(buf, opp_a), *buf_counter(buf, opp_b));
            i32 intended = min2(ca, cb);
            return -min2(conflict, intended);
        }

        default: return 0;
    }
}

static i32 state_dir_release_dollar(Input_Buffer* buf, Cmd_Key_Id key)
{
    switch (key)
    {
        case CK_N:
        {
            i32 all_min = min2(absi(buf->Ub), min2(absi(buf->Db), min2(absi(buf->Bb), absi(buf->Fb))));
            i32 r = all_min;
            r = min2(r, absi(buf->ab)); r = min2(r, absi(buf->bb)); r = min2(r, absi(buf->cb));
            r = min2(r, absi(buf->xb)); r = min2(r, absi(buf->yb)); r = min2(r, absi(buf->zb));
            r = min2(r, absi(buf->sb)); r = min2(r, absi(buf->db)); r = min2(r, absi(buf->wb));
            return r;
        }

        case CK_U:
        {
            if (!(buf->Ub < 0 || buf->Up > 0)) return 0;
            if (buf->Ub < 0) return -buf->Ub;
            return min3(absi(buf->Db), absi(buf->Bb), absi(buf->Fb));
        }
        case CK_D:
        {
            if (!(buf->Db < 0 || buf->Dp > 0)) return 0;
            if (buf->Db < 0) return -buf->Db;
            return min3(absi(buf->Ub), absi(buf->Bb), absi(buf->Fb));
        }
        case CK_B:
        {
            if (!(buf->Bb < 0 || buf->Bp > 0)) return 0;
            if (buf->Bb < 0) return -buf->Bb;
            return min3(absi(buf->Ub), absi(buf->Db), absi(buf->Fb));
        }
        case CK_F:
        {
            if (!(buf->Fb < 0 || buf->Fp > 0)) return 0;
            if (buf->Fb < 0) return -buf->Fb;
            return min3(absi(buf->Ub), absi(buf->Db), absi(buf->Bb));
        }
        case CK_L:
        {
            if (!(buf->Lb < 0 || buf->Lp > 0)) return 0;
            if (buf->Lb < 0) return -buf->Lb;
            return min3(absi(buf->Ub), absi(buf->Db), absi(buf->Rb));
        }
        case CK_R:
        {
            if (!(buf->Rb < 0 || buf->Rp > 0)) return 0;
            if (buf->Rb < 0) return -buf->Rb;
            return min3(absi(buf->Ub), absi(buf->Db), absi(buf->Lb));
        }

        case CK_UB: case CK_UF: case CK_DB: case CK_DF:
        case CK_UL: case CK_UR: case CK_DL: case CK_DR:
        {
            Cmd_Key_Id comp_a, comp_b, opp_a, opp_b;
            switch (key)
            {
                case CK_UB: comp_a = CK_U; comp_b = CK_B; opp_a = CK_D; opp_b = CK_F; break;
                case CK_UF: comp_a = CK_U; comp_b = CK_F; opp_a = CK_D; opp_b = CK_B; break;
                case CK_DB: comp_a = CK_D; comp_b = CK_B; opp_a = CK_U; opp_b = CK_F; break;
                case CK_DF: comp_a = CK_D; comp_b = CK_F; opp_a = CK_U; opp_b = CK_B; break;
                case CK_UL: comp_a = CK_U; comp_b = CK_L; opp_a = CK_D; opp_b = CK_R; break;
                case CK_UR: comp_a = CK_U; comp_b = CK_R; opp_a = CK_D; opp_b = CK_L; break;
                case CK_DL: comp_a = CK_D; comp_b = CK_L; opp_a = CK_U; opp_b = CK_R; break;
                case CK_DR: comp_a = CK_D; comp_b = CK_R; opp_a = CK_U; opp_b = CK_L; break;
                default: return 0;
            }

            i32 ca = *buf_counter(buf, comp_a), pa = *buf_prev(buf, comp_a);
            i32 cb = *buf_counter(buf, comp_b), pb = *buf_prev(buf, comp_b);

            if (!((ca < 0 || pa > 0) && (cb < 0 || pb > 0))) return 0;

            if (ca < 0 || cb < 0) return -min2(ca, cb);

            return min2(absi(*buf_counter(buf, opp_a)), absi(*buf_counter(buf, opp_b)));
        }

        default: return 0;
    }
}

i32 input_buffer_state(Input_Buffer* buf, Command_Key key)
{
    if (cmd_key_is_button(key))
    {
        i32 counter = *buf_counter(buf, key.key);
        i32 prev    = *buf_prev(buf, key.key);

        if (!key.tilde) return counter;

        if (counter < 0 || prev > 0) return -counter;
        return 0;
    }

    if (!key.tilde && !key.dollar) return state_dir_hold_no_dollar(buf, key.key);
    if (!key.tilde &&  key.dollar) return state_dir_hold_dollar(buf, key.key);
    if ( key.tilde && !key.dollar) return state_dir_release_no_dollar(buf, key.key);
    return state_dir_release_dollar(buf, key.key);
}

static i32 charge_dir_hold_dollar(Input_Buffer* buf, Cmd_Key_Id key)
{
    switch (key)
    {
        case CK_U: return buf->Ub;
        case CK_D: return buf->Db;
        case CK_B: return buf->Bb;
        case CK_F: return buf->Fb;
        case CK_L: return buf->Lb;
        case CK_R: return buf->Rb;
        case CK_N: return buf->Nb;

        case CK_UB: return min2(buf->Ub, buf->Bb);
        case CK_UF: return min2(buf->Ub, buf->Fb);
        case CK_DB: return min2(buf->Db, buf->Bb);
        case CK_DF: return min2(buf->Db, buf->Fb);
        case CK_UL: return min2(buf->Ub, buf->Lb);
        case CK_UR: return min2(buf->Ub, buf->Rb);
        case CK_DL: return min2(buf->Db, buf->Lb);
        case CK_DR: return min2(buf->Db, buf->Rb);

        default: return 0;
    }
}

static i32 charge_dir_release_dollar(Input_Buffer* buf, Cmd_Key_Id key)
{
    switch (key)
    {
        case CK_U: return buf->Up;
        case CK_D: return buf->Dp;
        case CK_B: return buf->Bp;
        case CK_F: return buf->Fp;
        case CK_L: return buf->Lp;
        case CK_R: return buf->Rp;
        case CK_N: return buf->Np;

        case CK_UB: return min2(buf->Up, buf->Bp);
        case CK_UF: return min2(buf->Up, buf->Fp);
        case CK_DB: return min2(buf->Dp, buf->Bp);
        case CK_DF: return min2(buf->Dp, buf->Fp);
        case CK_UL: return min2(buf->Up, buf->Lp);
        case CK_UR: return min2(buf->Up, buf->Rp);
        case CK_DL: return min2(buf->Dp, buf->Lp);
        case CK_DR: return min2(buf->Dp, buf->Rp);

        default: return 0;
    }
}

static i32 charge_dir_hold_no_dollar(Input_Buffer* buf, Cmd_Key_Id key)
{
    i32 conflict, strict;

    switch (key)
    {
        case CK_U: conflict = -max3(buf->Bb, buf->Db, buf->Fb); strict = min2(conflict, buf->Ub); break;
        case CK_D: conflict = -max3(buf->Bb, buf->Ub, buf->Fb); strict = min2(conflict, buf->Db); break;
        case CK_B: conflict = -max3(buf->Ub, buf->Db, buf->Fb); strict = min2(conflict, buf->Bb); break;
        case CK_F: conflict = -max3(buf->Ub, buf->Db, buf->Bb); strict = min2(conflict, buf->Fb); break;
        case CK_L: conflict = -max3(buf->Ub, buf->Db, buf->Rb); strict = min2(conflict, buf->Lb); break;
        case CK_R: conflict = -max3(buf->Ub, buf->Db, buf->Lb); strict = min2(conflict, buf->Rb); break;
        case CK_N: return buf->Nb;

        case CK_UB: conflict = -max2(buf->Db, buf->Fb); strict = min2(conflict, min2(buf->Ub, buf->Bb)); break;
        case CK_UF: conflict = -max2(buf->Db, buf->Bb); strict = min2(conflict, min2(buf->Ub, buf->Fb)); break;
        case CK_DB: conflict = -max2(buf->Ub, buf->Fb); strict = min2(conflict, min2(buf->Db, buf->Bb)); break;
        case CK_DF: conflict = -max2(buf->Ub, buf->Bb); strict = min2(conflict, min2(buf->Db, buf->Fb)); break;
        case CK_UL: conflict = -max2(buf->Db, buf->Rb); strict = min2(conflict, min2(buf->Ub, buf->Lb)); break;
        case CK_UR: conflict = -max2(buf->Db, buf->Lb); strict = min2(conflict, min2(buf->Ub, buf->Rb)); break;
        case CK_DL: conflict = -max2(buf->Ub, buf->Rb); strict = min2(conflict, min2(buf->Db, buf->Lb)); break;
        case CK_DR: conflict = -max2(buf->Ub, buf->Lb); strict = min2(conflict, min2(buf->Db, buf->Rb)); break;

        default: return 0;
    }

    return max2(0, strict);
}

static i32 charge_dir_release_no_dollar(Input_Buffer* buf, Cmd_Key_Id key)
{
    i32 conflict, strict;

    switch (key)
    {
        case CK_U:
            conflict = -max3(ignore_recent(buf->Bb), ignore_recent(buf->Db), ignore_recent(buf->Fb));
            strict = min2(conflict, buf->Up); break;
        case CK_D:
            conflict = -max3(ignore_recent(buf->Ub), ignore_recent(buf->Bb), ignore_recent(buf->Fb));
            strict = min2(conflict, buf->Dp); break;
        case CK_B:
            conflict = -max3(ignore_recent(buf->Ub), ignore_recent(buf->Db), ignore_recent(buf->Fb));
            strict = min2(conflict, buf->Bp); break;
        case CK_F:
            conflict = -max3(ignore_recent(buf->Ub), ignore_recent(buf->Db), ignore_recent(buf->Bb));
            strict = min2(conflict, buf->Fp); break;
        case CK_L:
            conflict = -max3(ignore_recent(buf->Ub), ignore_recent(buf->Db), ignore_recent(buf->Rb));
            strict = min2(conflict, buf->Lp); break;
        case CK_R:
            conflict = -max3(ignore_recent(buf->Ub), ignore_recent(buf->Db), ignore_recent(buf->Lb));
            strict = min2(conflict, buf->Rp); break;
        case CK_N: return buf->Np;

        case CK_UB:
            conflict = -max2(ignore_recent(buf->Db), ignore_recent(buf->Fb));
            strict = min2(conflict, min2(buf->Up, buf->Bp)); break;
        case CK_UF:
            conflict = -max2(ignore_recent(buf->Db), ignore_recent(buf->Bb));
            strict = min2(conflict, min2(buf->Up, buf->Fp)); break;
        case CK_DB:
            conflict = -max2(ignore_recent(buf->Ub), ignore_recent(buf->Fb));
            strict = min2(conflict, min2(buf->Dp, buf->Bp)); break;
        case CK_DF:
            conflict = -max2(ignore_recent(buf->Ub), ignore_recent(buf->Bb));
            strict = min2(conflict, min2(buf->Dp, buf->Fp)); break;
        case CK_UL:
            conflict = -max2(ignore_recent(buf->Db), ignore_recent(buf->Rb));
            strict = min2(conflict, min2(buf->Up, buf->Lp)); break;
        case CK_UR:
            conflict = -max2(ignore_recent(buf->Db), ignore_recent(buf->Lb));
            strict = min2(conflict, min2(buf->Up, buf->Rp)); break;
        case CK_DL:
            conflict = -max2(ignore_recent(buf->Ub), ignore_recent(buf->Rb));
            strict = min2(conflict, min2(buf->Dp, buf->Lp)); break;
        case CK_DR:
            conflict = -max2(ignore_recent(buf->Ub), ignore_recent(buf->Lb));
            strict = min2(conflict, min2(buf->Dp, buf->Rp)); break;

        default: return 0;
    }

    return max2(0, strict);
}

i32 input_buffer_state_charge(Input_Buffer* buf, Command_Key key)
{
    if (cmd_key_is_button(key))
    {
        if (!key.tilde) return *buf_counter(buf, key.key);
        return *buf_prev(buf, key.key);
    }

    if (!key.tilde &&  key.dollar) return charge_dir_hold_dollar(buf, key.key);
    if ( key.tilde &&  key.dollar) return charge_dir_release_dollar(buf, key.key);
    if (!key.tilde && !key.dollar) return charge_dir_hold_no_dollar(buf, key.key);
    return charge_dir_release_no_dollar(buf, key.key);
}
