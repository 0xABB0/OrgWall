#include "mugen_air.h"
#include "anim.clip.h"
#include "anim.registry.h"
#include "allocator.h"
#include "string.str8.h"
#include "math.easing.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    Mugen_Clsn_Box* boxes;
    u32 count;
    u32 capacity;
} Mugen__Clsn_List;

typedef struct {
    Mugen_Air_Frame* frames;
    u32 count;
    u32 capacity;
} Mugen__Frame_List;

typedef struct {
    Mugen_Air_Action* actions;
    u32 count;
    u32 capacity;
} Mugen__Action_List;

static void mugen__clsn_push(Mugen__Clsn_List* list, Mugen_Clsn_Box box, const Mel_Alloc* alloc)
{
    if (list->count >= list->capacity)
    {
        u32 new_cap = list->capacity == 0 ? 4 : list->capacity * 2;
        usize new_size = sizeof(Mugen_Clsn_Box) * new_cap;
        if (list->boxes == NULL)
            list->boxes = mel_alloc(alloc, new_size);
        else
            list->boxes = mel_realloc(alloc, list->boxes, new_size);
        list->capacity = new_cap;
    }
    list->boxes[list->count++] = box;
}

static void mugen__frame_push(Mugen__Frame_List* list, Mugen_Air_Frame frame, const Mel_Alloc* alloc)
{
    if (list->count >= list->capacity)
    {
        u32 new_cap = list->capacity == 0 ? 8 : list->capacity * 2;
        usize new_size = sizeof(Mugen_Air_Frame) * new_cap;
        if (list->frames == NULL)
            list->frames = mel_alloc(alloc, new_size);
        else
            list->frames = mel_realloc(alloc, list->frames, new_size);
        list->capacity = new_cap;
    }
    list->frames[list->count++] = frame;
}

static void mugen__action_push(Mugen__Action_List* list, Mugen_Air_Action action, const Mel_Alloc* alloc)
{
    if (list->count >= list->capacity)
    {
        u32 new_cap = list->capacity == 0 ? 16 : list->capacity * 2;
        usize new_size = sizeof(Mugen_Air_Action) * new_cap;
        if (list->actions == NULL)
            list->actions = mel_alloc(alloc, new_size);
        else
            list->actions = mel_realloc(alloc, list->actions, new_size);
        list->capacity = new_cap;
    }
    list->actions[list->count++] = action;
}

static Mugen_Clsn_Box* mugen__clone_boxes(const Mugen_Clsn_Box* src, u32 count, const Mel_Alloc* alloc)
{
    if (count == 0 || src == NULL) return NULL;
    Mugen_Clsn_Box* dst = mel_alloc(alloc, sizeof(Mugen_Clsn_Box) * count);
    memcpy(dst, src, sizeof(Mugen_Clsn_Box) * count);
    return dst;
}

static str8 mugen__next_line(str8* remaining)
{
    if (remaining->len <= 0) return STR8_EMPTY;

    size nl = str8_find(*remaining, S8("\n"));
    str8 line;
    if (nl < 0)
    {
        line = *remaining;
        *remaining = STR8_EMPTY;
    }
    else
    {
        line = str8_prefix(*remaining, nl);
        *remaining = str8_slice(*remaining, nl + 1, remaining->len - nl - 1);
    }

    if (line.len > 0 && line.data[line.len - 1] == '\r')
        line.len--;

    return line;
}

static str8 mugen__strip_comment(str8 line)
{
    size semi = str8_find(line, S8(";"));
    if (semi >= 0) line = str8_prefix(line, semi);
    return line;
}

static bool mugen__parse_i16(str8 s, i16* out)
{
    s = str8_trim(s);
    if (s.len == 0) return false;
    char buf[32];
    str8_to_buf(s, buf, sizeof(buf));
    *out = (i16)atoi(buf);
    return true;
}

static bool mugen__parse_i32(str8 s, i32* out)
{
    s = str8_trim(s);
    if (s.len == 0) return false;
    char buf[32];
    str8_to_buf(s, buf, sizeof(buf));
    *out = atoi(buf);
    return true;
}

static bool mugen__parse_u32(str8 s, u32* out)
{
    s = str8_trim(s);
    if (s.len == 0) return false;
    char buf[32];
    str8_to_buf(s, buf, sizeof(buf));
    *out = (u32)atoi(buf);
    return true;
}

static str8 mugen__next_csv(str8* remaining)
{
    if (remaining->len <= 0) return STR8_EMPTY;

    size comma = str8_find(*remaining, S8(","));
    str8 token;
    if (comma < 0)
    {
        token = str8_trim(*remaining);
        *remaining = STR8_EMPTY;
    }
    else
    {
        token = str8_trim(str8_prefix(*remaining, comma));
        *remaining = str8_slice(*remaining, comma + 1, remaining->len - comma - 1);
    }
    return token;
}

static bool mugen__parse_clsn_box(str8 line, Mugen_Clsn_Box* out)
{
    size eq = str8_find(line, S8("="));
    if (eq < 0) return false;

    str8 values = str8_trim(str8_slice(line, eq + 1, line.len - eq - 1));

    str8 rem = values;
    str8 sx1 = mugen__next_csv(&rem);
    str8 sy1 = mugen__next_csv(&rem);
    str8 sx2 = mugen__next_csv(&rem);
    str8 sy2 = mugen__next_csv(&rem);

    if (!mugen__parse_i16(sx1, &out->x1)) return false;
    if (!mugen__parse_i16(sy1, &out->y1)) return false;
    if (!mugen__parse_i16(sx2, &out->x2)) return false;
    if (!mugen__parse_i16(sy2, &out->y2)) return false;

    if (out->x1 > out->x2) { i16 t = out->x1; out->x1 = out->x2; out->x2 = t; }
    if (out->y1 > out->y2) { i16 t = out->y1; out->y1 = out->y2; out->y2 = t; }

    return true;
}

static bool mugen__parse_frame_line(str8 line, Mugen_Air_Frame* out)
{
    str8 rem = line;
    str8 sgroup  = mugen__next_csv(&rem);
    str8 snumber = mugen__next_csv(&rem);
    str8 sxoff   = mugen__next_csv(&rem);
    str8 syoff   = mugen__next_csv(&rem);
    str8 stime   = mugen__next_csv(&rem);

    i32 group, number, xoff, yoff, time_val;
    if (!mugen__parse_i32(sgroup, &group)) return false;
    if (!mugen__parse_i32(snumber, &number)) return false;
    if (!mugen__parse_i32(sxoff, &xoff)) return false;
    if (!mugen__parse_i32(syoff, &yoff)) return false;
    if (!mugen__parse_i32(stime, &time_val)) return false;

    out->group = (u16)(group < 0 ? 0 : group);
    out->number = (u16)number;
    out->x_offset = (i16)xoff;
    out->y_offset = (i16)yoff;
    out->time = (i16)time_val;
    out->flip_h = false;
    out->flip_v = false;

    if (rem.len > 0)
    {
        str8 flags = mugen__next_csv(&rem);
        flags = str8_trim(flags);
        for (size i = 0; i < flags.len; i++)
        {
            if (flags.data[i] == 'H' || flags.data[i] == 'h') out->flip_h = true;
            if (flags.data[i] == 'V' || flags.data[i] == 'v') out->flip_v = true;
        }
    }

    return true;
}

static bool mugen__is_digit_or_minus(u8 c)
{
    return (c >= '0' && c <= '9') || c == '-';
}

bool mugen_air_load(Mugen_Air* out, str8 data, const Mel_Alloc* alloc)
{
    assert(out != NULL);
    assert(alloc != NULL);

    *out = (Mugen_Air){0};

    Mugen__Action_List actions = {0};
    Mugen__Frame_List frames = {0};
    Mugen__Clsn_List pending_clsn1 = {0};
    Mugen__Clsn_List pending_clsn2 = {0};
    Mugen__Clsn_List default_clsn1 = {0};
    Mugen__Clsn_List default_clsn2 = {0};

    bool in_action = false;
    u32 action_number = 0;
    u32 loop_start = MUGEN_AIR_NO_LOOP;
    bool clsn1_pending = false;
    bool clsn2_pending = false;

    str8 remaining = data;

    while (remaining.len > 0)
    {
        str8 raw_line = mugen__next_line(&remaining);
        str8 line = str8_trim(mugen__strip_comment(raw_line));
        if (line.len == 0) continue;

        if (str8_starts_with(line, S8("[Begin Action ")))
        {
            if (in_action && frames.count > 0)
            {
                Mugen_Air_Action act = {
                    .action_number = action_number,
                    .frames = frames.frames,
                    .frame_count = frames.count,
                    .loop_start = loop_start,
                };
                mugen__action_push(&actions, act, alloc);
                frames = (Mugen__Frame_List){0};
            }

            str8 num_part = str8_slice(line, 14, line.len - 15);
            i32 n;
            mugen__parse_i32(num_part, &n);
            action_number = (u32)n;
            in_action = true;
            loop_start = MUGEN_AIR_NO_LOOP;
            default_clsn1.count = 0;
            default_clsn2.count = 0;
            pending_clsn1.count = 0;
            pending_clsn2.count = 0;
            clsn1_pending = false;
            clsn2_pending = false;
            continue;
        }

        if (!in_action) continue;

        str8 lower = line;
        bool is_loopstart = false;
        if (lower.len >= 9)
        {
            char buf[16] = {0};
            for (size i = 0; i < 9 && i < lower.len; i++)
            {
                char c = (char)lower.data[i];
                buf[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
            }
            is_loopstart = (memcmp(buf, "loopstart", 9) == 0);
        }

        if (is_loopstart)
        {
            loop_start = frames.count;
            continue;
        }

        if (str8_starts_with(line, S8("Clsn1Default:")) || str8_starts_with(line, S8("Clsn1default:")))
        {
            default_clsn1.count = 0;
            clsn1_pending = false;

            str8 after = str8_trim(str8_slice(line, 13, line.len - 13));
            i32 box_count;
            mugen__parse_i32(after, &box_count);

            for (i32 b = 0; b < box_count; b++)
            {
                if (remaining.len == 0) break;
                str8 box_line = str8_trim(mugen__strip_comment(mugen__next_line(&remaining)));
                if (box_line.len == 0) { b--; continue; }
                Mugen_Clsn_Box box;
                if (mugen__parse_clsn_box(box_line, &box))
                    mugen__clsn_push(&default_clsn1, box, alloc);
            }
            continue;
        }

        if (str8_starts_with(line, S8("Clsn2Default:")) || str8_starts_with(line, S8("Clsn2default:")))
        {
            default_clsn2.count = 0;
            clsn2_pending = false;

            str8 after = str8_trim(str8_slice(line, 13, line.len - 13));
            i32 box_count;
            mugen__parse_i32(after, &box_count);

            for (i32 b = 0; b < box_count; b++)
            {
                if (remaining.len == 0) break;
                str8 box_line = str8_trim(mugen__strip_comment(mugen__next_line(&remaining)));
                if (box_line.len == 0) { b--; continue; }
                Mugen_Clsn_Box box;
                if (mugen__parse_clsn_box(box_line, &box))
                    mugen__clsn_push(&default_clsn2, box, alloc);
            }
            continue;
        }

        if (str8_starts_with(line, S8("Clsn1:")) || str8_starts_with(line, S8("Clsn1 :")))
        {
            pending_clsn1.count = 0;
            clsn1_pending = true;

            size colon = str8_find(line, S8(":"));
            str8 after = str8_trim(str8_slice(line, colon + 1, line.len - colon - 1));
            i32 box_count;
            mugen__parse_i32(after, &box_count);

            for (i32 b = 0; b < box_count; b++)
            {
                if (remaining.len == 0) break;
                str8 box_line = str8_trim(mugen__strip_comment(mugen__next_line(&remaining)));
                if (box_line.len == 0) { b--; continue; }
                Mugen_Clsn_Box box;
                if (mugen__parse_clsn_box(box_line, &box))
                    mugen__clsn_push(&pending_clsn1, box, alloc);
            }
            continue;
        }

        if (str8_starts_with(line, S8("Clsn2:")) || str8_starts_with(line, S8("Clsn2 :")))
        {
            pending_clsn2.count = 0;
            clsn2_pending = true;

            size colon = str8_find(line, S8(":"));
            str8 after = str8_trim(str8_slice(line, colon + 1, line.len - colon - 1));
            i32 box_count;
            mugen__parse_i32(after, &box_count);

            for (i32 b = 0; b < box_count; b++)
            {
                if (remaining.len == 0) break;
                str8 box_line = str8_trim(mugen__strip_comment(mugen__next_line(&remaining)));
                if (box_line.len == 0) { b--; continue; }
                Mugen_Clsn_Box box;
                if (mugen__parse_clsn_box(box_line, &box))
                    mugen__clsn_push(&pending_clsn2, box, alloc);
            }
            continue;
        }

        if (line.len > 0 && mugen__is_digit_or_minus(line.data[0]))
        {
            Mugen_Air_Frame frame = {0};
            if (!mugen__parse_frame_line(line, &frame)) continue;

            if (clsn1_pending)
            {
                frame.clsn1 = mugen__clone_boxes(pending_clsn1.boxes, pending_clsn1.count, alloc);
                frame.clsn1_count = pending_clsn1.count;
                clsn1_pending = false;
            }
            else
            {
                frame.clsn1 = mugen__clone_boxes(default_clsn1.boxes, default_clsn1.count, alloc);
                frame.clsn1_count = default_clsn1.count;
            }

            if (clsn2_pending)
            {
                frame.clsn2 = mugen__clone_boxes(pending_clsn2.boxes, pending_clsn2.count, alloc);
                frame.clsn2_count = pending_clsn2.count;
                clsn2_pending = false;
            }
            else
            {
                frame.clsn2 = mugen__clone_boxes(default_clsn2.boxes, default_clsn2.count, alloc);
                frame.clsn2_count = default_clsn2.count;
            }

            mugen__frame_push(&frames, frame, alloc);
        }
    }

    if (in_action && frames.count > 0)
    {
        Mugen_Air_Action act = {
            .action_number = action_number,
            .frames = frames.frames,
            .frame_count = frames.count,
            .loop_start = loop_start,
        };
        mugen__action_push(&actions, act, alloc);
    }

    if (pending_clsn1.boxes) mel_dealloc(alloc, pending_clsn1.boxes);
    if (pending_clsn2.boxes) mel_dealloc(alloc, pending_clsn2.boxes);
    if (default_clsn1.boxes) mel_dealloc(alloc, default_clsn1.boxes);
    if (default_clsn2.boxes) mel_dealloc(alloc, default_clsn2.boxes);

    out->actions = actions.actions;
    out->action_count = actions.count;

    return actions.count > 0;
}

void mugen_air_shutdown(Mugen_Air* air, const Mel_Alloc* alloc)
{
    assert(air != NULL);

    for (u32 a = 0; a < air->action_count; a++)
    {
        Mugen_Air_Action* act = &air->actions[a];
        for (u32 f = 0; f < act->frame_count; f++)
        {
            if (act->frames[f].clsn1) mel_dealloc(alloc, act->frames[f].clsn1);
            if (act->frames[f].clsn2) mel_dealloc(alloc, act->frames[f].clsn2);
        }
        mel_dealloc(alloc, act->frames);
    }
    if (air->actions) mel_dealloc(alloc, air->actions);
    *air = (Mugen_Air){0};
}

Mugen_Air_Action* mugen_air_find_action(Mugen_Air* air, u32 action_number)
{
    for (u32 i = 0; i < air->action_count; i++)
    {
        if (air->actions[i].action_number == action_number)
            return &air->actions[i];
    }
    return NULL;
}

static Mugen_Clsn_Box mugen__bounding_box(const Mugen_Clsn_Box* boxes, u32 count)
{
    assert(count > 0);
    Mugen_Clsn_Box bb = boxes[0];
    for (u32 i = 1; i < count; i++)
    {
        if (boxes[i].x1 < bb.x1) bb.x1 = boxes[i].x1;
        if (boxes[i].y1 < bb.y1) bb.y1 = boxes[i].y1;
        if (boxes[i].x2 > bb.x2) bb.x2 = boxes[i].x2;
        if (boxes[i].y2 > bb.y2) bb.y2 = boxes[i].y2;
    }
    return bb;
}

Mel_Anim_Clip mugen_air_compile(const Mugen_Air_Action* action, const Mel_Alloc* alloc)
{
    assert(action != NULL);
    assert(action->frame_count > 0);
    assert(alloc != NULL);

    u32 fc = action->frame_count;
    u16 step_id = MEL_EASING_COUNT - 1;

    f32 total_duration = 0.0f;
    f32 loop_start_time = 0.0f;
    bool has_loop = action->loop_start != MUGEN_AIR_NO_LOOP;

    for (u32 i = 0; i < fc; i++)
    {
        i16 t = action->frames[i].time;
        f32 dur = (t <= 0) ? (1.0f / MUGEN_TICKS_PER_SECOND) : ((f32)t / MUGEN_TICKS_PER_SECOND);
        if (has_loop && i == action->loop_start)
            loop_start_time = total_duration;
        total_duration += dur;
    }

    bool has_hold_forever = (action->frames[fc - 1].time == -1);
    bool is_looping = has_hold_forever ? false : true;
    f32 loop_time = has_loop ? loop_start_time : 0.0f;

    u32 group_count = 3;
    Mel_Track_Group* groups = mel_alloc(alloc, sizeof(Mel_Track_Group) * group_count);
    memset(groups, 0, sizeof(Mel_Track_Group) * group_count);

    {
        Mel_Track_Group* grp = &groups[0];
        grp->type_hash = MEL_ANIM_TYPE_F32;
        grp->track_count = 1;
        grp->property_ids = mel_alloc(alloc, sizeof(u64));
        grp->property_ids[0] = mel_xxh3_64("frame", 5);
        grp->keyframe_counts = mel_alloc(alloc, sizeof(u32));
        grp->keyframe_counts[0] = fc;
        grp->data_offsets = mel_alloc(alloc, sizeof(u32));
        grp->data_offsets[0] = 0;
        grp->flat_times = mel_alloc(alloc, sizeof(f32) * fc);
        grp->flat_values = mel_alloc(alloc, sizeof(f32) * fc);
        grp->flat_easing_ids = mel_alloc(alloc, sizeof(u16) * fc);
        grp->flat_easing_params = NULL;

        f32 cumulative = 0.0f;
        for (u32 i = 0; i < fc; i++)
        {
            grp->flat_times[i] = cumulative;
            ((f32*)grp->flat_values)[i] = (f32)i;
            grp->flat_easing_ids[i] = step_id;

            i16 t = action->frames[i].time;
            cumulative += (t <= 0) ? (1.0f / MUGEN_TICKS_PER_SECOND) : ((f32)t / MUGEN_TICKS_PER_SECOND);
        }
    }

    {
        Mel_Track_Group* grp = &groups[1];
        grp->type_hash = MEL_ANIM_TYPE_VEC4;
        grp->track_count = 1;
        grp->property_ids = mel_alloc(alloc, sizeof(u64));
        grp->property_ids[0] = mel_xxh3_64("hitbox", 6);
        grp->keyframe_counts = mel_alloc(alloc, sizeof(u32));
        grp->keyframe_counts[0] = fc;
        grp->data_offsets = mel_alloc(alloc, sizeof(u32));
        grp->data_offsets[0] = 0;
        grp->flat_times = mel_alloc(alloc, sizeof(f32) * fc);
        grp->flat_values = mel_alloc(alloc, sizeof(f32) * fc * 4);
        grp->flat_easing_ids = mel_alloc(alloc, sizeof(u16) * fc);
        grp->flat_easing_params = NULL;

        f32 cumulative = 0.0f;
        for (u32 i = 0; i < fc; i++)
        {
            grp->flat_times[i] = cumulative;
            grp->flat_easing_ids[i] = step_id;

            f32* v = &((f32*)grp->flat_values)[i * 4];
            if (action->frames[i].clsn1_count > 0)
            {
                Mugen_Clsn_Box bb = mugen__bounding_box(action->frames[i].clsn1, action->frames[i].clsn1_count);
                v[0] = (f32)bb.x1;
                v[1] = (f32)bb.y1;
                v[2] = (f32)(bb.x2 - bb.x1);
                v[3] = (f32)(bb.y2 - bb.y1);
            }
            else
            {
                v[0] = 0; v[1] = 0; v[2] = 0; v[3] = 0;
            }

            i16 t = action->frames[i].time;
            cumulative += (t <= 0) ? (1.0f / MUGEN_TICKS_PER_SECOND) : ((f32)t / MUGEN_TICKS_PER_SECOND);
        }
    }

    {
        Mel_Track_Group* grp = &groups[2];
        grp->type_hash = MEL_ANIM_TYPE_VEC4;
        grp->track_count = 1;
        grp->property_ids = mel_alloc(alloc, sizeof(u64));
        grp->property_ids[0] = mel_xxh3_64("hurtbox", 7);
        grp->keyframe_counts = mel_alloc(alloc, sizeof(u32));
        grp->keyframe_counts[0] = fc;
        grp->data_offsets = mel_alloc(alloc, sizeof(u32));
        grp->data_offsets[0] = 0;
        grp->flat_times = mel_alloc(alloc, sizeof(f32) * fc);
        grp->flat_values = mel_alloc(alloc, sizeof(f32) * fc * 4);
        grp->flat_easing_ids = mel_alloc(alloc, sizeof(u16) * fc);
        grp->flat_easing_params = NULL;

        f32 cumulative = 0.0f;
        for (u32 i = 0; i < fc; i++)
        {
            grp->flat_times[i] = cumulative;
            grp->flat_easing_ids[i] = step_id;

            f32* v = &((f32*)grp->flat_values)[i * 4];
            if (action->frames[i].clsn2_count > 0)
            {
                Mugen_Clsn_Box bb = mugen__bounding_box(action->frames[i].clsn2, action->frames[i].clsn2_count);
                v[0] = (f32)bb.x1;
                v[1] = (f32)bb.y1;
                v[2] = (f32)(bb.x2 - bb.x1);
                v[3] = (f32)(bb.y2 - bb.y1);
            }
            else
            {
                v[0] = 0; v[1] = 0; v[2] = 0; v[3] = 0;
            }

            i16 t = action->frames[i].time;
            cumulative += (t <= 0) ? (1.0f / MUGEN_TICKS_PER_SECOND) : ((f32)t / MUGEN_TICKS_PER_SECOND);
        }
    }

    return (Mel_Anim_Clip){
        .name_hash = (u64)action->action_number,
        .duration = total_duration,
        .is_looping = is_looping,
        .loop_start_time = loop_time,
        .additive_space = MEL_ANIM_ADDITIVE_LOCAL,
        .groups = groups,
        .group_count = group_count,
        .event_groups = NULL,
        .event_group_count = 0,
    };
}
