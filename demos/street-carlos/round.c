#include "round.h"
#include "fighter.h"
#include "combat.h"
#include "stage.h"
#include <string.h>
#include <stdio.h>

void round_init_opt(Round_Ctx* r, Round_Init_Opt opt)
{
    memset(r, 0, sizeof(*r));
    r->p1 = opt.p1;
    r->p2 = opt.p2;
    r->rounds_to_win = opt.rounds_to_win > 0 ? opt.rounds_to_win : 2;
    r->ko_pause_ticks = opt.ko_pause_ticks > 0 ? opt.ko_pause_ticks : 180;
    r->stage_left = opt.stage_left > 0 ? opt.stage_left : STAGE_LEFT;
    r->stage_right = opt.stage_right > 0 ? opt.stage_right : STAGE_RIGHT;
    r->roundno = 1;
    r->state = ROUND_PRE_INTRO;
}

static void enter_state(Round_Ctx* r, u8 state)
{
    r->state = state;
    r->timer = 0;

    switch (state)
    {
        case ROUND_PRE_INTRO:
            r->p1->cns_state.roundstate = 0;
            r->p2->cns_state.roundstate = 0;
            break;

        case ROUND_INTRO:
            r->p1->cns_state.roundstate = 1;
            r->p2->cns_state.roundstate = 1;
            break;

        case ROUND_FIGHT:
            r->p1->cns_state.roundstate = 2;
            r->p2->cns_state.roundstate = 2;
            r->p1->cns_state.ctrl = true;
            r->p2->cns_state.ctrl = true;
            printf("ROUND %d - FIGHT!\n", r->roundno);
            break;

        case ROUND_KO:
        {
            r->p1->cns_state.roundstate = 3;
            r->p2->cns_state.roundstate = 3;

            Fighter* loser = (r->p1->cns_state.life <= 0) ? r->p1 : r->p2;
            Fighter* winner = (loser == r->p1) ? r->p2 : r->p1;
            r->ko_fighter = loser;
            r->winner = winner;

            loser->cns_state.alive = false;

            winner->cns_state.state_changed = true;
            winner->cns_state.pending_state = 180;
            winner->cns_state.pending_ctrl = 0;

            if (winner == r->p1)
                r->p1_wins++;
            else
                r->p2_wins++;

            printf("KO! %s wins (score: %d-%d)\n",
                winner == r->p1 ? "P1" : "P2", r->p1_wins, r->p2_wins);
            break;
        }

        case ROUND_POST:
        {
            r->p1->cns_state.roundstate = 4;
            r->p2->cns_state.roundstate = 4;
            break;
        }
    }
}

void round_tick(Round_Ctx* r, f32 dt)
{
    r->timer++;

    switch (r->state)
    {
        case ROUND_PRE_INTRO:
        {
            fighter_tick(r->p1, dt, r->stage_left, r->stage_right);
            fighter_tick(r->p2, dt, r->stage_left, r->stage_right);
            fighter_apply_combat_state(r->p1);
            fighter_apply_combat_state(r->p2);

            bool p1_past_5900 = (r->p1->cns_state.stateno != 5900);
            bool p2_past_5900 = (r->p2->cns_state.stateno != 5900);
            if (p1_past_5900 && p2_past_5900)
                enter_state(r, ROUND_INTRO);
            break;
        }

        case ROUND_INTRO:
        {
            fighter_tick(r->p1, dt, r->stage_left, r->stage_right);
            fighter_tick(r->p2, dt, r->stage_left, r->stage_right);
            fighter_apply_combat_state(r->p1);
            fighter_apply_combat_state(r->p2);

            bool p1_settled = r->p1->cns_state.time > 0;
            bool p2_settled = r->p2->cns_state.time > 0;
            if (!p1_settled || !p2_settled)
                break;

            bool p1_intro = (r->p1->cns_state.assert_flags & MUGEN_ASSERT_INTRO) != 0;
            bool p2_intro = (r->p2->cns_state.assert_flags & MUGEN_ASSERT_INTRO) != 0;
            if (!p1_intro && !p2_intro)
                enter_state(r, ROUND_FIGHT);
            break;
        }

        case ROUND_FIGHT:
        {
            fighter_tick(r->p1, dt, r->stage_left, r->stage_right);
            fighter_tick(r->p2, dt, r->stage_left, r->stage_right);
            combat_resolve(r->p1, r->p2);
            fighter_apply_combat_state(r->p1);
            fighter_apply_combat_state(r->p2);

            if (r->p1->cns_state.life <= 0 || r->p2->cns_state.life <= 0)
                enter_state(r, ROUND_KO);
            break;
        }

        case ROUND_KO:
        {
            fighter_tick(r->p1, dt, r->stage_left, r->stage_right);
            fighter_tick(r->p2, dt, r->stage_left, r->stage_right);
            fighter_apply_combat_state(r->p1);
            fighter_apply_combat_state(r->p2);

            if (r->timer >= r->ko_pause_ticks)
                enter_state(r, ROUND_POST);
            break;
        }

        case ROUND_POST:
        {
            bool match_over = (r->p1_wins >= r->rounds_to_win ||
                               r->p2_wins >= r->rounds_to_win);
            if (match_over)
            {
                printf("MATCH OVER! %s wins the match!\n",
                    r->p1_wins >= r->rounds_to_win ? "P1" : "P2");
                break;
            }

            if (r->timer >= 60)
            {
                round_reset(r);
            }
            break;
        }
    }
}

void round_reset(Round_Ctx* r)
{
    r->roundno++;
    r->ko_fighter = NULL;
    r->winner = NULL;

    fighter_round_reset(r->p1);
    fighter_round_reset(r->p2);

    r->p1->cns_state.roundno = r->roundno;
    r->p2->cns_state.roundno = r->roundno;

    enter_state(r, ROUND_PRE_INTRO);

    printf("ROUND %d START\n", r->roundno);
}
