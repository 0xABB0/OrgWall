#pragma once

#include "core.types.h"

typedef u8 Socd_Mode;
#define SOCD_NEUTRAL     0
#define SOCD_UP_PRIORITY 1
#define SOCD_FIRST_WINS  2

typedef u8 Cmd_Key_Id;
#define CK_U   0
#define CK_D   1
#define CK_B   2
#define CK_F   3
#define CK_L   4
#define CK_R   5
#define CK_UB  6
#define CK_UF  7
#define CK_DB  8
#define CK_DF  9
#define CK_UL  10
#define CK_UR  11
#define CK_DL  12
#define CK_DR  13
#define CK_N   14
#define CK_a   15
#define CK_b   16
#define CK_c   17
#define CK_x   18
#define CK_y   19
#define CK_z   20
#define CK_s   21
#define CK_d   22
#define CK_w   23
#define CK_m   24

typedef struct {
    Cmd_Key_Id key;
    bool slash;
    bool tilde;
    bool dollar;
    i32 chargetime;
} Command_Key;

static inline bool cmd_key_is_direction(Command_Key k)  { return k.key <= CK_N; }
static inline bool cmd_key_is_button(Command_Key k)     { return k.key >= CK_a; }
static inline bool cmd_key_is_dir_press(Command_Key k)  { return !k.tilde && k.key <= CK_N; }
static inline bool cmd_key_is_dir_release(Command_Key k){ return k.tilde && k.key <= CK_N; }
static inline bool cmd_key_is_btn_press(Command_Key k)  { return !k.tilde && k.key >= CK_a; }
static inline bool cmd_key_is_btn_release(Command_Key k){ return k.tilde && k.key >= CK_a; }

typedef struct {
    i32 Ub, Db, Lb, Rb, Bb, Fb, Nb;
    i32 ab, bb, cb, xb, yb, zb, sb, db, wb, mb;
    i32 Up, Dp, Lp, Rp, Bp, Fp, Np;
    i32 ap, bp, cp, xp, yp, zp, sp, dp, wp, mp;
    Socd_Mode socd_mode;
    bool socd_first[4];
    bool facing_right;
} Input_Buffer;

void input_buffer_init(Input_Buffer* buf, bool facing_right);
void input_buffer_update(Input_Buffer* buf, bool U, bool D, bool L, bool R,
                         bool a, bool b, bool c, bool x, bool y, bool z,
                         bool s, bool d, bool w, bool m);
i32  input_buffer_state(Input_Buffer* buf, Command_Key key);
i32  input_buffer_state_charge(Input_Buffer* buf, Command_Key key);
