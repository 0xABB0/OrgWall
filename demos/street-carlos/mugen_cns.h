#pragma once

#include "core.types.h"
#include "string.str8.fwd.h"
#include "allocator.fwd.h"

typedef struct Mugen_Expr Mugen_Expr;
typedef struct Mugen_State_Controller Mugen_State_Controller;
typedef struct Mugen_Char_State Mugen_Char_State;
#ifndef COMMAND_LIST_DEFINED
typedef struct Command_List Command_List;
#endif

#define MUGEN_EXPR_LIT_INT     0
#define MUGEN_EXPR_LIT_FLOAT   1
#define MUGEN_EXPR_LIT_STRING  2
#define MUGEN_EXPR_UNARY       3
#define MUGEN_EXPR_BINARY      4
#define MUGEN_EXPR_FUNC        5
#define MUGEN_EXPR_QUERY       6
#define MUGEN_EXPR_VAR         7
#define MUGEN_EXPR_REDIRECT    8
#define MUGEN_EXPR_RANGE       9

#define MUGEN_REDIRECT_HELPER  0
#define MUGEN_REDIRECT_ROOT    1
#define MUGEN_REDIRECT_PARENT  2

#define MUGEN_OP_NEG   0
#define MUGEN_OP_NOT   1

#define MUGEN_OP_ADD   0
#define MUGEN_OP_SUB   1
#define MUGEN_OP_MUL   2
#define MUGEN_OP_DIV   3
#define MUGEN_OP_MOD   4
#define MUGEN_OP_EQ    5
#define MUGEN_OP_NEQ   6
#define MUGEN_OP_LT    7
#define MUGEN_OP_LE    8
#define MUGEN_OP_GT    9
#define MUGEN_OP_GE    10
#define MUGEN_OP_AND   11
#define MUGEN_OP_OR    12
#define MUGEN_OP_XOR   13
#define MUGEN_OP_BAND  14
#define MUGEN_OP_BOR   15
#define MUGEN_OP_POW   16

#define MUGEN_FUNC_CEIL    0
#define MUGEN_FUNC_FLOOR   1
#define MUGEN_FUNC_ABS     2
#define MUGEN_FUNC_IFELSE  3

#define MUGEN_QUERY_TIME           0
#define MUGEN_QUERY_ANIMTIME       1
#define MUGEN_QUERY_ANIMELEM       2
#define MUGEN_QUERY_ANIMELEMTIME   3
#define MUGEN_QUERY_STATENO        4
#define MUGEN_QUERY_PREVSTATENO    5
#define MUGEN_QUERY_STATETYPE      6
#define MUGEN_QUERY_MOVETYPE       7
#define MUGEN_QUERY_CTRL           8
#define MUGEN_QUERY_COMMAND        9
#define MUGEN_QUERY_VEL_X          10
#define MUGEN_QUERY_VEL_Y          11
#define MUGEN_QUERY_POS_X          12
#define MUGEN_QUERY_POS_Y          13
#define MUGEN_QUERY_MOVECONTACT    14
#define MUGEN_QUERY_MOVEHIT        15
#define MUGEN_QUERY_MOVEGUARDED    16
#define MUGEN_QUERY_HITCOUNT       17
#define MUGEN_QUERY_LIFE           18
#define MUGEN_QUERY_LIFEMAX        19
#define MUGEN_QUERY_POWER          20
#define MUGEN_QUERY_POWERMAX       21
#define MUGEN_QUERY_FACING         22
#define MUGEN_QUERY_P2BODYDIST_X   23
#define MUGEN_QUERY_P2DIST_X       24
#define MUGEN_QUERY_P2DIST_Y       25
#define MUGEN_QUERY_NUMHELPER      26
#define MUGEN_QUERY_ALIVE          27
#define MUGEN_QUERY_RANDOM         28
#define MUGEN_QUERY_GAMETIME       29
#define MUGEN_QUERY_ROUNDSTATE     30
#define MUGEN_QUERY_ISHELPER       31
#define MUGEN_QUERY_CONST          32
#define MUGEN_QUERY_SELFANIMEXIST  33
#define MUGEN_QUERY_GETHITVAR      34
#define MUGEN_QUERY_NUMPROJID      35
#define MUGEN_QUERY_ANIM           36
#define MUGEN_QUERY_HITSHAKEOVER   37
#define MUGEN_QUERY_HITOVER        38
#define MUGEN_QUERY_HITFALL        39
#define MUGEN_QUERY_P2STATETYPE    40
#define MUGEN_QUERY_P2MOVETYPE     41
#define MUGEN_QUERY_FRONTEDGEDIST  42
#define MUGEN_QUERY_BACKEDGEDIST   43
#define MUGEN_QUERY_FRONTEDGEBODYDIST 44
#define MUGEN_QUERY_BACKEDGEBODYDIST  45
#define MUGEN_QUERY_ROUNDNO        46
#define MUGEN_QUERY_ROUNDSEXISTED  47
#define MUGEN_QUERY_INGUARDDIST    48
#define MUGEN_QUERY_CANRECOVER     49
#define MUGEN_QUERY_PALNO          50
#define MUGEN_QUERY_HITDEFATTR     51
#define MUGEN_QUERY_LOSE           52
#define MUGEN_QUERY_WIN            53
#define MUGEN_QUERY_MATCHOVER      54
#define MUGEN_QUERY_NUMTARGET      55
#define MUGEN_QUERY_MAX            64

typedef f32 (*Mugen_Query_Eval_Fn)(Mugen_Expr* arg, Mugen_Char_State* state);

typedef struct {
    const char* name;
    Mugen_Query_Eval_Fn eval;
} Mugen_Query_Reg;

void mugen_query_register(u8 id, const char* name, Mugen_Query_Eval_Fn eval);
u8 mugen_query_lookup_name(str8 name);
Mugen_Query_Reg* mugen_query_get_reg(u8 id);

typedef struct {
    const char* name;
    u8 id;
} Mugen_Query_Name_Entry;

u32 mugen_query_name_count(void);
const Mugen_Query_Name_Entry* mugen_query_name_table(void);

#define MUGEN_VAR_INT      0
#define MUGEN_VAR_FLOAT    1
#define MUGEN_VAR_SYSINT   2
#define MUGEN_VAR_SYSFLOAT 3

struct Mugen_Expr {
    u8 type;
    union {
        i32 lit_int;
        f32 lit_float;
        str8 lit_string;
        struct { u8 op; Mugen_Expr* operand; } unary;
        struct { u8 op; Mugen_Expr* lhs; Mugen_Expr* rhs; } binary;
        struct { u8 id; Mugen_Expr** args; u8 arg_count; } func;
        struct { u8 id; Mugen_Expr* arg; } query;
        struct { u8 var_type; Mugen_Expr* index; } var;
        struct { u8 target_type; Mugen_Expr* id; Mugen_Expr* sub_expr; } redirect;
        struct { Mugen_Expr* value; Mugen_Expr* lo; Mugen_Expr* hi; bool lo_inclusive; bool hi_inclusive; } range;
    };
};

#define MUGEN_SC_NULL          0
#define MUGEN_SC_VELSET        1
#define MUGEN_SC_VELADD        2
#define MUGEN_SC_VELMUL        3
#define MUGEN_SC_POSSET        4
#define MUGEN_SC_POSADD        5
#define MUGEN_SC_CHANGESTATE   6
#define MUGEN_SC_CHANGEANIM    7
#define MUGEN_SC_HITDEF        8
#define MUGEN_SC_PLAYSND       9
#define MUGEN_SC_GRAVITY       10
#define MUGEN_SC_CTRL          11
#define MUGEN_SC_VARSET        12
#define MUGEN_SC_VARADD        13
#define MUGEN_SC_NOTHITBY      14
#define MUGEN_SC_WIDTH         15
#define MUGEN_SC_TURN          16
#define MUGEN_SC_SUPERPAUSE    17
#define MUGEN_SC_AFTERIMAGE    18
#define MUGEN_SC_AFTERIMAGETIME 19
#define MUGEN_SC_POWERADD      20
#define MUGEN_SC_SPRPRIORITY   21
#define MUGEN_SC_VARRANDOM     22
#define MUGEN_SC_STATETYPESET  23
#define MUGEN_SC_HITVELSET     24
#define MUGEN_SC_DEFENCEMULSET 25
#define MUGEN_SC_SELFSTATE     26
#define MUGEN_SC_HITFALLVEL    27
#define MUGEN_SC_HITFALLDAMAGE 28
#define MUGEN_SC_POSFREEZE     29
#define MUGEN_SC_HITFALLSET    30
#define MUGEN_SC_FALLENVSHAKE  31
#define MUGEN_SC_LIFESET       32
#define MUGEN_SC_ASSERTSPECIAL 33
#define MUGEN_SC_VARRANGESET   34
#define MUGEN_SC_TARGETBIND    35
#define MUGEN_SC_TARGETSTATE   36
#define MUGEN_SC_TARGETLIFEADD 37
#define MUGEN_SC_TARGETFACING  38
#define MUGEN_SC_TARGETPOWERADD 39
#define MUGEN_SC_CHANGEANIM2   40
#define MUGEN_SC_HELPER        41
#define MUGEN_SC_DESTROYSELF   42
#define MUGEN_SC_MAX           64

typedef void (*Mugen_SC_Parse_Fn)(Mugen_State_Controller* sc, str8 key, str8 val, const struct Mel_Alloc* alloc);
typedef void (*Mugen_SC_Exec_Fn)(Mugen_State_Controller* sc, Mugen_Char_State* state);

typedef struct {
    const char* name;
    Mugen_SC_Parse_Fn parse_param;
    Mugen_SC_Exec_Fn exec;
} Mugen_SC_Reg;

void mugen_sc_register(u8 id, const char* name, Mugen_SC_Parse_Fn parse_param, Mugen_SC_Exec_Fn exec);
u8 mugen_sc_lookup_name(str8 name);
Mugen_SC_Reg* mugen_sc_get_reg(u8 id);

#define MUGEN_ANIMTYPE_LIGHT   0
#define MUGEN_ANIMTYPE_MEDIUM  1
#define MUGEN_ANIMTYPE_HARD    2
#define MUGEN_ANIMTYPE_BACK    3
#define MUGEN_ANIMTYPE_UP      4
#define MUGEN_ANIMTYPE_DIAGUP  5

#define MUGEN_GROUNDTYPE_HIGH  0
#define MUGEN_GROUNDTYPE_LOW   1
#define MUGEN_GROUNDTYPE_TRIP  2

#define MUGEN_ATTR_S   (1u << 0)
#define MUGEN_ATTR_C   (1u << 1)
#define MUGEN_ATTR_A   (1u << 2)
#define MUGEN_ATTR_NA  (1u << 3)
#define MUGEN_ATTR_SA  (1u << 4)
#define MUGEN_ATTR_HA  (1u << 5)
#define MUGEN_ATTR_NP  (1u << 6)
#define MUGEN_ATTR_SP  (1u << 7)
#define MUGEN_ATTR_HP  (1u << 8)
#define MUGEN_ATTR_NT  (1u << 9)
#define MUGEN_ATTR_ST  (1u << 10)
#define MUGEN_ATTR_HT  (1u << 11)

#define MUGEN_HF_H  (1u << 0)
#define MUGEN_HF_L  (1u << 1)
#define MUGEN_HF_M  (MUGEN_HF_H | MUGEN_HF_L)
#define MUGEN_HF_A  (1u << 2)
#define MUGEN_HF_D  (1u << 3)
#define MUGEN_HF_F  (1u << 4)
#define MUGEN_HF_P  (1u << 5)
#define MUGEN_HF_MNS (1u << 6)
#define MUGEN_HF_PLS (1u << 7)

#define MUGEN_STATETYPE_U 0xFE
#define MUGEN_PHYSICS_U   0xFE

#define MUGEN_PHYSICS_S  1
#define MUGEN_PHYSICS_C  2
#define MUGEN_PHYSICS_A  3
#define MUGEN_PHYSICS_N  4
#define MUGEN_PHYSICS_L  5

#define MUGEN_MOVETYPE_I  0
#define MUGEN_MOVETYPE_A  1
#define MUGEN_MOVETYPE_H  2
#define MUGEN_MOVETYPE_U  0xFE

#define MUGEN_ASSERT_NOWALK        (1u << 0)
#define MUGEN_ASSERT_NOAUTOTURN    (1u << 1)
#define MUGEN_ASSERT_NOSTANDGUARD  (1u << 2)
#define MUGEN_ASSERT_NOCROUCHGUARD (1u << 3)
#define MUGEN_ASSERT_NOAIRGUARD    (1u << 4)
#define MUGEN_ASSERT_NOJUGGLECHECK (1u << 5)
#define MUGEN_ASSERT_INTRO         (1u << 6)
#define MUGEN_ASSERT_NOCORNERPUSH  (1u << 7)
#define MUGEN_ASSERT_UNGUARDABLE  (1u << 8)

#define MUGEN_POSTYPE_P1    0
#define MUGEN_POSTYPE_LEFT  1
#define MUGEN_POSTYPE_RIGHT 2
#define MUGEN_POSTYPE_BACK  3
#define MUGEN_POSTYPE_FRONT 4

typedef struct {
    Mugen_Expr** conditions;
    u32 count;
} Mugen_Trigger_Group;

struct Mugen_State_Controller {
    u8 type;
    Mugen_Expr** triggerall;
    u32 triggerall_count;
    Mugen_Trigger_Group* trigger_groups;
    u32 trigger_group_count;
    bool ignorehitpause;
    i32 persistent;
    i32 persistent_counter;
    void* params;
};

typedef struct {
    i32 stateno;
    u8 statetype;
    u8 movetype;
    u8 physics;
    i32 anim;
    f32 velset_x, velset_y;
    bool has_velset;
    i32 ctrl;
    i32 juggle;
    i32 poweradd;
    i32 sprpriority;
    bool hitdefpersist;
    bool movehitpersist;
    bool hitcountpersist;
    Mugen_State_Controller* controllers;
    u32 controller_count;
} Mugen_Statedef;

typedef struct {
    i32 life;
    i32 attack;
    i32 defence;
    i32 fall_defence_up;
    i32 liedown_time;
    i32 airjuggle;
    i32 sparkno;
    i32 guard_sparkno;
    i32 power_max;

    f32 xscale, yscale;
    f32 ground_back, ground_front;
    f32 air_back, air_front;
    f32 height;
    f32 attack_dist;

    f32 walk_fwd_x;
    f32 walk_back_x;
    f32 run_fwd_x, run_fwd_y;
    f32 run_back_x, run_back_y;
    f32 jump_neu_x, jump_y;
    f32 jump_back_x;
    f32 jump_fwd_x;
    f32 runjump_fwd_x, runjump_y;
    f32 runjump_back_x;
    f32 airjump_neu_x, airjump_y;
    f32 airjump_fwd_x;
    f32 airjump_back_x;

    i32 airjump_num;
    i32 airjump_height;
    f32 yaccel;
    f32 stand_friction;
    f32 crouch_friction;
    f32 stand_friction_threshold;
    f32 crouch_friction_threshold;
    f32 down_bounce_offset_x, down_bounce_offset_y;
    f32 down_bounce_yaccel;
    f32 down_bounce_groundlevel;
    f32 down_friction_threshold;
} Mugen_Char_Constants;

typedef struct Mugen_Cns {
    Mugen_Statedef* statedefs;
    u32 statedef_count;
    Mugen_Char_Constants constants;
    bool has_constants;
} Mugen_Cns;

typedef struct {
    bool active;
    u32 attr;
    u32 hitflag;
    u32 guardflag;
    u8 ground_type;
    u8 animtype;
    f32 damage_hit;
    f32 damage_guard;
    i32 pausetime_p1;
    i32 pausetime_p2;
    i32 guard_pausetime_p1;
    i32 guard_pausetime_p2;
    f32 spark_x, spark_y;
    i32 hitsound_group, hitsound_index;
    i32 guardsound_group, guardsound_index;
    i32 ground_slidetime;
    i32 ground_hittime;
    f32 ground_vel_x, ground_vel_y;
    f32 guard_velocity;
    i32 guard_slidetime;
    i32 guard_hittime;
    i32 guard_ctrltime;
    f32 air_vel_x, air_vel_y;
    i32 air_hittime;
    bool air_fall;
    bool fall;
    bool fall_recover;
    i32 fall_recovertime;
    f32 fall_vel_x, fall_vel_y;
    i32 priority;
    i32 getpower_hit, getpower_guard;
    bool forcestand;
    bool hitonce;
    i32 numhits;
    i32 p1stateno;
    i32 p2stateno;
    bool p2getp1state;
    i32 juggle;
    f32 ground_cornerpush_veloff;
    f32 air_cornerpush_veloff;
    f32 guard_cornerpush_veloff;
    f32 yaccel;
    bool has_yaccel;
    i32 p1facing;
    i32 p2facing;
} Mugen_HitDef_Result;

typedef struct {
    u8 animtype;
    u8 air_animtype;
    u8 ground_animtype;
    u8 groundtype;
    u8 airtype;
    i32 damage;
    i32 hitcount;
    i32 guardcount;
    i32 hitshaketime;
    i32 hittime;
    i32 slidetime;
    i32 ctrltime;
    f32 xvel, yvel;
    f32 xaccel, yaccel;
    f32 xoff, yoff;
    bool isbound;
    bool guarded;
    bool fallflag;
    bool fall_recover;
    i32 fall_recovertime;
    f32 fall_xvel, fall_yvel;
    i32 fall_damage;
    bool fall_kill;
    i32 attr;
    i32 priority;
    bool forcestand;
} Mugen_GetHitVar;

typedef struct {
    Mugen_Char_State* state;
    i32 hitdef_id;
} Mugen_Target_Entry;

struct Mugen_Char_State {
    f32 pos_x, pos_y;
    f32 vel_x, vel_y;

    i32 stateno;
    i32 prevstateno;
    u8 statetype;
    u8 movetype;
    u8 physics;
    bool ctrl;
    i32 time;

    u32 anim;
    i32 animtime;
    i32 animelem;
    i32 animelemtime;

    f32 life, lifemax;
    f32 power, powermax;
    i32 mctime;
    i32 movehit;
    i32 moveguarded;
    i32 hitcount;
    i32 juggle_points_remaining;

    i32 var[60];
    f32 fvar[40];
    i32 sysvar[5];
    f32 sysfvar[5];

    f32 facing;
    bool alive;
    bool is_helper;

    f32 p2_pos_x, p2_pos_y;
    u8 p2_statetype;
    u8 p2_movetype;
    f32 p2_width;

    f32 stage_left, stage_right;
    f32 ground_front, ground_back;

    Command_List* commands;

    f32 gravity;
    f32 stand_friction;
    f32 crouch_friction;
    f32 stand_friction_threshold;
    f32 crouch_friction_threshold;
    f32 walk_fwd_x, walk_back_x;
    f32 run_fwd_x, run_fwd_y;
    f32 run_back_x, run_back_y;
    f32 jump_neu_x, jump_fwd_x, jump_back_x, jump_y;
    f32 runjump_fwd_x, runjump_back_x, runjump_y;
    f32 airjump_neu_x, airjump_fwd_x, airjump_back_x, airjump_y;
    f32 data_attack;
    f32 attack_dist;
    i32 palno;

    Mugen_HitDef_Result hitdef;
    bool hitdef_pending;

    Mugen_GetHitVar ghv;

    i32 hitpause_time;
    bool hitdef_active;
    bool pos_freeze;

    i32 nothitby_time;
    u32 nothitby_attr;

    u32 assert_flags;
    f32 defence_mul;
    u64 rng_state;
    f32 data_height;
    f32 data_defence;
    f32 data_liedown_time;
    f32 data_airjuggle;
    f32 data_sparkno;
    f32 data_guard_sparkno;
    f32 data_xscale;
    f32 data_yscale;
    f32 data_air_front;
    f32 data_air_back;
    f32 data_airjump_num;
    f32 data_airjump_height;
    f32 down_bounce_offset_x, down_bounce_offset_y;
    f32 down_bounce_yaccel;
    f32 down_bounce_groundlevel;
    f32 down_friction_threshold;

    Mugen_Target_Entry* targets;
    u32 target_count;
    u32 target_cap;
    Mugen_Char_State* bound_to;
    Mugen_Cns* self_cns;
    Mugen_Cns* state_owner_cns;
    bool use_owner_anim;

    i32 pending_state;
    i32 pending_ctrl;
    i32 pending_anim;
    bool state_changed;

    i32 current_juggle;

    i32 gametime;
    i32 roundstate;
    i32 roundno;
    i32 roundsexisted;
    bool win;
    bool lose;
    bool matchover;

    i32* anim_elem_start_ticks;
    u32 anim_elem_count;

    bool (*anim_exists)(void* ctx, u32 anim);
    void* anim_exists_ctx;

    i32 (*query_num_helper)(void* ctx, i32 id);
    Mugen_Char_State* (*query_helper_state)(void* ctx, i32 id);
    Mugen_Char_State* (*query_root_state)(void* ctx);
    void* helper_ctx;

    bool helper_spawn_pending;
    i32 helper_spawn_id;
    i32 helper_spawn_stateno;
    f32 helper_spawn_x, helper_spawn_y;
    u8 helper_spawn_postype;
    i32 helper_spawn_facing;

    bool destroy_self_pending;

    f32 cornerpush_vel;
    i32 fall_time;
    i32 sprpriority;
};

Mugen_Expr* mugen_expr_parse(str8 text, const Mel_Alloc* alloc);
f32 mugen_expr_eval(Mugen_Expr* expr, Mugen_Char_State* state);
bool mugen_expr_eval_bool(Mugen_Expr* expr, Mugen_Char_State* state);

bool mugen_cns_load(Mugen_Cns* out, str8 data, const Mel_Alloc* alloc);
void mugen_cns_merge(Mugen_Cns* dst, Mugen_Cns* src, const Mel_Alloc* alloc);
void mugen_cns_shutdown(Mugen_Cns* cns, const Mel_Alloc* alloc);
Mugen_Statedef* mugen_cns_get(Mugen_Cns* cns, i32 stateno);

void mugen_cns_enter_state(Mugen_Cns* cns, Mugen_Char_State* state, i32 stateno);
void mugen_cns_tick(Mugen_Cns* cns, Mugen_Char_State* state);
void mugen_cns_tick_statedef(Mugen_Statedef* def, Mugen_Char_State* state);

void mugen_targets_add(Mugen_Char_State* state, Mugen_Char_State* target, i32 hitdef_id);
void mugen_targets_clear(Mugen_Char_State* state);
void mugen_targets_free(Mugen_Char_State* state);
