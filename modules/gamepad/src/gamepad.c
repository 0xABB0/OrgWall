#include <gamepad/gamepad.h>
#include <gamepad/joystick.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.array/array.h>
#include <string/str8.h>
#include <log/log.h>

#include <string.h>

#include "joystick_backend.h"
#include "gamecontrollerdb.gen.h"

typedef struct
{
    Mel_Gamepad_Button   button;
    Mel_Gamepad_Binding  binding;
} Button_Map;

typedef struct
{
    Mel_Gamepad_Axis    axis;
    Mel_Gamepad_Binding binding;
} Axis_Map;

typedef struct
{
    Mel_Guid                guid;
    str8                    name;
    Mel_Gamepad_Face_Labels labels;
    Mel_Array(Button_Map)   buttons;
    Mel_Array(Axis_Map)     axes;
} Mapping;

struct Mel_Gamepad_Db
{
    const Mel_Alloc*  alloc;
    str8              platform_filter;
    char              platform_buf[16];
    Mel_Array(Mapping) mappings;
    char*             text_arena;
    usize             text_used;
    usize             text_cap;
};

static Mel_Gamepad_Db* g_active_db;

static const char* host_platform(void)
{
#if defined(__APPLE__)
    return "Mac OS X";
#elif defined(__ANDROID__)
    return "Android";
#elif defined(__linux__)
    return "Linux";
#elif defined(_WIN32)
    return "Windows";
#elif defined(__EMSCRIPTEN__)
    return "Web";
#else
    return "";
#endif
}

Mel_Gamepad_Db* mel_gamepad_db_create_opt(const Mel_Alloc* alloc, Mel_Gamepad_Db_Opt opt)
{
    const Mel_Alloc* a = alloc ? alloc : mel_alloc_heap();
    Mel_Gamepad_Db*  db = mel_alloc(a, sizeof *db);
    if (!db)
        return NULL;
    memset(db, 0, sizeof *db);
    db->alloc = a;
    if (opt.platform_filter.len > 0 && opt.platform_filter.len < sizeof db->platform_buf)
    {
        memcpy(db->platform_buf, opt.platform_filter.data, opt.platform_filter.len);
        db->platform_filter = (str8){ (u8*)db->platform_buf, (size)opt.platform_filter.len };
    }
    else
    {
        const char* hp = host_platform();
        usize       n = strlen(hp);
        if (n >= sizeof db->platform_buf)
            n = sizeof db->platform_buf - 1;
        memcpy(db->platform_buf, hp, n);
        db->platform_filter = (str8){ (u8*)db->platform_buf, (size)n };
    }
    mel_array_init(&db->mappings, a);
    return db;
}

void mel_gamepad_db_destroy(Mel_Gamepad_Db* db)
{
    if (!db)
        return;
    if (g_active_db == db)
        g_active_db = NULL;
    for (usize i = 0; i < db->mappings.count; i++)
    {
        mel_array_free(&db->mappings.items[i].buttons);
        mel_array_free(&db->mappings.items[i].axes);
    }
    mel_array_free(&db->mappings);
    if (db->text_arena)
        mel_dealloc(db->alloc, db->text_arena);
    mel_dealloc(db->alloc, db);
}

u32 mel_gamepad_db_count(const Mel_Gamepad_Db* db) { return db ? (u32)db->mappings.count : 0; }

static str8 intern(Mel_Gamepad_Db* db, str8 s)
{
    if (s.len == 0)
        return STR8_EMPTY;
    if (db->text_used + (usize)s.len + 1 > db->text_cap)
    {
        usize need = db->text_cap == 0 ? 4096 : db->text_cap * 2;
        while (db->text_used + (usize)s.len + 1 > need)
            need *= 2;
        char* grown = db->text_arena ? mel_realloc(db->alloc, db->text_arena, need) : mel_alloc(db->alloc, need);
        if (!grown)
            return STR8_EMPTY;
        db->text_arena = grown;
        db->text_cap = need;
    }
    char* dst = db->text_arena + db->text_used;
    memcpy(dst, s.data, (usize)s.len);
    dst[s.len] = '\0';
    db->text_used += (usize)s.len + 1;
    return (str8){ (u8*)dst, s.len };
}

static bool button_from_name(str8 name, Mel_Gamepad_Button* out)
{
    static const struct
    {
        const char*        s;
        Mel_Gamepad_Button b;
    } table[] = {
        { "a", MEL_GAMEPAD_BUTTON_SOUTH },
        { "b", MEL_GAMEPAD_BUTTON_EAST },
        { "x", MEL_GAMEPAD_BUTTON_WEST },
        { "y", MEL_GAMEPAD_BUTTON_NORTH },
        { "back", MEL_GAMEPAD_BUTTON_BACK },
        { "guide", MEL_GAMEPAD_BUTTON_GUIDE },
        { "start", MEL_GAMEPAD_BUTTON_START },
        { "leftstick", MEL_GAMEPAD_BUTTON_LEFT_STICK },
        { "rightstick", MEL_GAMEPAD_BUTTON_RIGHT_STICK },
        { "leftshoulder", MEL_GAMEPAD_BUTTON_LEFT_SHOULDER },
        { "rightshoulder", MEL_GAMEPAD_BUTTON_RIGHT_SHOULDER },
        { "dpup", MEL_GAMEPAD_BUTTON_DPAD_UP },
        { "dpdown", MEL_GAMEPAD_BUTTON_DPAD_DOWN },
        { "dpleft", MEL_GAMEPAD_BUTTON_DPAD_LEFT },
        { "dpright", MEL_GAMEPAD_BUTTON_DPAD_RIGHT },
        { "misc1", MEL_GAMEPAD_BUTTON_MISC1 },
        { "paddle1", MEL_GAMEPAD_BUTTON_RIGHT_PADDLE1 },
        { "paddle2", MEL_GAMEPAD_BUTTON_LEFT_PADDLE1 },
        { "paddle3", MEL_GAMEPAD_BUTTON_RIGHT_PADDLE2 },
        { "paddle4", MEL_GAMEPAD_BUTTON_LEFT_PADDLE2 },
        { "touchpad", MEL_GAMEPAD_BUTTON_TOUCHPAD },
    };
    for (usize i = 0; i < sizeof table / sizeof table[0]; i++)
        if (str8_ieq_cstr(name, table[i].s))
        {
            *out = table[i].b;
            return true;
        }
    return false;
}

static bool axis_from_name(str8 name, Mel_Gamepad_Axis* out)
{
    static const struct
    {
        const char*      s;
        Mel_Gamepad_Axis a;
    } table[] = {
        { "leftx", MEL_GAMEPAD_AXIS_LEFT_X },
        { "lefty", MEL_GAMEPAD_AXIS_LEFT_Y },
        { "rightx", MEL_GAMEPAD_AXIS_RIGHT_X },
        { "righty", MEL_GAMEPAD_AXIS_RIGHT_Y },
        { "lefttrigger", MEL_GAMEPAD_AXIS_LEFT_TRIGGER },
        { "righttrigger", MEL_GAMEPAD_AXIS_RIGHT_TRIGGER },
    };
    for (usize i = 0; i < sizeof table / sizeof table[0]; i++)
        if (str8_ieq_cstr(name, table[i].s))
        {
            *out = table[i].a;
            return true;
        }
    return false;
}

static u32 parse_u32(str8 s)
{
    u32 v = 0;
    for (size i = 0; i < s.len; i++)
    {
        if (s.data[i] < '0' || s.data[i] > '9')
            break;
        u32 digit = (u32)(s.data[i] - '0');
        if (v > (0xFFFFFFFFu - digit) / 10u)
            return 0xFFFFFFFFu;
        v = v * 10u + digit;
    }
    return v;
}

static bool parse_target(str8 src, Mel_Gamepad_Binding* out)
{
    *out = (Mel_Gamepad_Binding){ 0 };
    if (src.len == 0)
        return false;
    size off = 0;
    if (src.data[0] == '+')
    {
        out->axis_sign = 1;
        off = 1;
    }
    else if (src.data[0] == '-')
    {
        out->axis_sign = -1;
        off = 1;
    }
    if (off >= src.len)
        return false;
    u8 tag = src.data[off];
    str8 rest = str8_slice(src, off + 1, src.len - off - 1);
    if (rest.len > 0 && rest.data[rest.len - 1] == '~')
    {
        out->invert = true;
        rest.len -= 1;
    }
    if (tag == 'b')
    {
        out->kind = MEL_GAMEPAD_BIND_BUTTON;
        out->index = parse_u32(rest);
        return true;
    }
    if (tag == 'a')
    {
        out->kind = MEL_GAMEPAD_BIND_AXIS;
        out->index = parse_u32(rest);
        return true;
    }
    if (tag == 'h')
    {
        out->kind = MEL_GAMEPAD_BIND_HAT;
        size dot = str8_find(rest, S8("."));
        if (dot < 0)
            return false;
        out->index = parse_u32(str8_prefix(rest, dot));
        out->hat_mask = (u8)parse_u32(str8_slice(rest, dot + 1, rest.len - dot - 1));
        return true;
    }
    return false;
}

static str8 next_field(str8* line, u8 sep)
{
    size i = 0;
    while (i < line->len && line->data[i] != sep)
        i++;
    str8 field = str8_prefix(*line, i);
    if (i < line->len)
        i++;
    *line = str8_slice(*line, i, line->len - i);
    return field;
}

bool mel_gamepad_db_load_line(Mel_Gamepad_Db* db, str8 line)
{
    if (!db)
        return false;
    line = str8_trim(line);
    if (line.len == 0 || line.data[0] == '#')
        return false;

    str8 guid_field = next_field(&line, ',');
    str8 name_field = next_field(&line, ',');
    if (guid_field.len == 0 || name_field.len == 0)
        return false;

    Mel_Guid guid;
    if (!mel_guid_from_string(guid_field, &guid))
        return false;

    Mapping m;
    memset(&m, 0, sizeof m);
    m.guid = guid;
    m.labels = MEL_GAMEPAD_FACE_LABELS_AB;
    mel_array_init(&m.buttons, db->alloc);
    mel_array_init(&m.axes, db->alloc);

    bool platform_ok = (db->platform_filter.len == 0);

    while (line.len > 0)
    {
        str8 pair = next_field(&line, ',');
        if (pair.len == 0)
            continue;
        size eq = str8_find(pair, S8(":"));
        if (eq < 0)
            continue;
        str8 key = str8_prefix(pair, eq);
        str8 val = str8_slice(pair, eq + 1, pair.len - eq - 1);

        if (str8_ieq_cstr(key, "platform"))
        {
            if (db->platform_filter.len == 0 || str8_ieq(val, db->platform_filter))
                platform_ok = true;
            else
                platform_ok = false;
            continue;
        }
        if (str8_ieq_cstr(key, "type") || str8_ieq_cstr(key, "crc") || str8_ieq_cstr(key, "hint") || str8_ieq_cstr(key, "sdk") || str8_ieq_cstr(key, "hint"))
            continue;

        Mel_Gamepad_Button button;
        Mel_Gamepad_Axis   axis;
        Mel_Gamepad_Binding binding;
        if (button_from_name(key, &button))
        {
            if (parse_target(val, &binding))
            {
                Button_Map bm = { .button = button, .binding = binding };
                mel_array_push(&m.buttons, bm);
            }
        }
        else if (axis_from_name(key, &axis))
        {
            if (parse_target(val, &binding))
            {
                Axis_Map am = { .axis = axis, .binding = binding };
                mel_array_push(&m.axes, am);
            }
        }
    }

    if (!platform_ok)
    {
        mel_array_free(&m.buttons);
        mel_array_free(&m.axes);
        return false;
    }

    m.name = intern(db, name_field);
    if (str8_contains(name_field, S8("PS3")) || str8_contains(name_field, S8("PS4")) || str8_contains(name_field, S8("PS5")) || str8_contains(name_field, S8("DualShock")) || str8_contains(name_field, S8("DualSense")) || str8_contains(name_field, S8("Sony")))
        m.labels = MEL_GAMEPAD_FACE_LABELS_SONY;
    else if (str8_contains(name_field, S8("Nintendo")) || str8_contains(name_field, S8("Switch")) || str8_contains(name_field, S8("Joy-Con")))
        m.labels = MEL_GAMEPAD_FACE_LABELS_NINTENDO;

    mel_array_push(&db->mappings, m);
    return true;
}

u32 mel_gamepad_db_load_string(Mel_Gamepad_Db* db, str8 text)
{
    if (!db)
        return 0;
    u32  added = 0;
    size start = 0;
    for (size i = 0; i <= text.len; i++)
    {
        if (i == text.len || text.data[i] == '\n')
        {
            str8 line = str8_slice(text, start, i - start);
            if (mel_gamepad_db_load_line(db, line))
                added++;
            start = i + 1;
        }
    }
    return added;
}

u32 mel_gamepad_db_load_bundled(Mel_Gamepad_Db* db)
{
    if (!db)
        return 0;
    str8 text = { (u8*)mel_gamepad_db_bundled_text, (size)mel_gamepad_db_bundled_len };
    return mel_gamepad_db_load_string(db, text);
}

void mel_gamepad_set_db(Mel_Gamepad_Db* db) { g_active_db = db; }

static const Mapping* find_mapping(Mel_Gamepad_Db* db, Mel_Guid guid)
{
    if (!db)
        return NULL;
    for (usize i = 0; i < db->mappings.count; i++)
        if (mel_guid_equal(db->mappings.items[i].guid, guid))
            return &db->mappings.items[i];
    return NULL;
}

static const Mapping* mapping_for(Mel_Joystick j)
{
    const Mel_Joystick_Descriptor* d = mel_joystick__descriptor(j);
    if (!d)
        return NULL;
    return find_mapping(g_active_db, d->guid);
}

bool mel_gamepad_supported(Mel_Joystick j) { return mapping_for(j) != NULL; }

bool mel_gamepad_button_binding(Mel_Joystick j, Mel_Gamepad_Button button, Mel_Gamepad_Binding* out)
{
    const Mapping* m = mapping_for(j);
    if (!m)
        return false;
    for (usize i = 0; i < m->buttons.count; i++)
        if (m->buttons.items[i].button == button)
        {
            *out = m->buttons.items[i].binding;
            return true;
        }
    return false;
}

bool mel_gamepad_axis_binding(Mel_Joystick j, Mel_Gamepad_Axis axis, Mel_Gamepad_Binding* out)
{
    const Mapping* m = mapping_for(j);
    if (!m)
        return false;
    for (usize i = 0; i < m->axes.count; i++)
        if (m->axes.items[i].axis == axis)
        {
            *out = m->axes.items[i].binding;
            return true;
        }
    return false;
}

static bool eval_button(const Mel_Joystick_State* st, Mel_Gamepad_Binding b)
{
    switch (b.kind)
    {
    case MEL_GAMEPAD_BIND_BUTTON:
        return b.index < st->button_count && st->buttons[b.index] != 0;
    case MEL_GAMEPAD_BIND_HAT:
        return b.index < st->hat_count && (st->hats[b.index] & b.hat_mask) == b.hat_mask && b.hat_mask != 0;
    case MEL_GAMEPAD_BIND_AXIS:
        if (b.index >= st->axis_count)
            return false;
        return b.axis_sign < 0 ? st->axes[b.index] < -16384 : st->axes[b.index] > 16384;
    default:
        return false;
    }
}

static f32 eval_axis(const Mel_Joystick_State* st, Mel_Gamepad_Binding b)
{
    f32 v = 0.0f;
    switch (b.kind)
    {
    case MEL_GAMEPAD_BIND_AXIS:
        if (b.index < st->axis_count)
            v = (f32)st->axes[b.index] / 32767.0f;
        break;
    case MEL_GAMEPAD_BIND_BUTTON:
        if (b.index < st->button_count)
            v = st->buttons[b.index] ? 1.0f : 0.0f;
        break;
    case MEL_GAMEPAD_BIND_HAT:
        if (b.index < st->hat_count)
            v = (st->hats[b.index] & b.hat_mask) == b.hat_mask && b.hat_mask != 0 ? 1.0f : 0.0f;
        break;
    default:
        break;
    }
    if (b.invert)
        v = -v;
    if (b.axis_sign != 0)
        v = v < 0.0f ? 0.0f : v;
    if (v < -1.0f)
        v = -1.0f;
    if (v > 1.0f)
        v = 1.0f;
    return v;
}

Mel_Gamepad_Frame mel_gamepad_read(Mel_Joystick j)
{
    Mel_Gamepad_Frame frame;
    memset(&frame, 0, sizeof frame);
    const Mapping* m = mapping_for(j);
    if (!m)
        return frame;
    Mel_Joystick_State_Result sr = mel_joystick_poll(j);
    if (mel_joystick_failed(sr.status))
        return frame;
    const Mel_Joystick_State* st = &sr.value;
    for (usize i = 0; i < m->buttons.count; i++)
        frame.down[m->buttons.items[i].button] = eval_button(st, m->buttons.items[i].binding);
    for (usize i = 0; i < m->axes.count; i++)
        frame.axis[m->axes.items[i].axis] = eval_axis(st, m->axes.items[i].binding);
    frame.valid = true;
    return frame;
}

Mel_Gamepad_Face_Labels mel_gamepad_face_labels(Mel_Joystick j)
{
    const Mapping* m = mapping_for(j);
    return m ? m->labels : MEL_GAMEPAD_FACE_LABELS_AB;
}

str8 mel_gamepad_button_label(Mel_Gamepad_Button button, Mel_Gamepad_Face_Labels labels)
{
    switch (labels)
    {
    case MEL_GAMEPAD_FACE_LABELS_SONY:
        switch (button)
        {
        case MEL_GAMEPAD_BUTTON_SOUTH:
            return S8("Cross");
        case MEL_GAMEPAD_BUTTON_EAST:
            return S8("Circle");
        case MEL_GAMEPAD_BUTTON_WEST:
            return S8("Square");
        case MEL_GAMEPAD_BUTTON_NORTH:
            return S8("Triangle");
        default:
            break;
        }
        break;
    case MEL_GAMEPAD_FACE_LABELS_NINTENDO:
        switch (button)
        {
        case MEL_GAMEPAD_BUTTON_SOUTH:
            return S8("B");
        case MEL_GAMEPAD_BUTTON_EAST:
            return S8("A");
        case MEL_GAMEPAD_BUTTON_WEST:
            return S8("Y");
        case MEL_GAMEPAD_BUTTON_NORTH:
            return S8("X");
        default:
            break;
        }
        break;
    case MEL_GAMEPAD_FACE_LABELS_AB:
    default:
        switch (button)
        {
        case MEL_GAMEPAD_BUTTON_SOUTH:
            return S8("A");
        case MEL_GAMEPAD_BUTTON_EAST:
            return S8("B");
        case MEL_GAMEPAD_BUTTON_WEST:
            return S8("X");
        case MEL_GAMEPAD_BUTTON_NORTH:
            return S8("Y");
        default:
            break;
        }
        break;
    }
    return Mel_Gamepad_Button_to_string(button);
}
