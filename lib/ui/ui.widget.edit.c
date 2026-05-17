#include "ui.widget.edit.h"

#include <string.h>
#include <SDL3/SDL.h>

#include "font.atlas.h"
#include "math.vec2.h"
#include "str8.h"
#include "collection.slotmap.h"
#include "math.scalar.h"
#include <SDL3/SDL_keyboard.h>

static void wedit_notify_change(Mel_WEdit* edit)
{
    if (edit->on_change)
        edit->on_change(edit, edit->user_data);
}

static void wedit_draw(Mel_Widget* w, void* ctx)
{
    (void)w;
    (void)ctx;
}

static bool wedit_mouse_down(Mel_Widget* w, Mel_Vec2 pos, i32 button)
{
    MEL_UNUSED(pos);
    return button == 1;
}

static bool wedit_push_char(Mel_WEdit* edit, char c)
{
    if (edit->text_len + 1 >= sizeof(edit->text))
        return false;
    edit->text[edit->text_len++] = c;
    edit->text[edit->text_len] = '\0';
    wedit_notify_change(edit);
    return true;
}

static bool wedit_key_down(Mel_Widget* w, const SDL_KeyboardEvent* event)
{
    Mel_WEdit* edit = (Mel_WEdit*)w;
    if (!(w->state & MEL_WIDGET_STATE_FOCUSED))
        return false;

    if (event->key == SDLK_BACKSPACE)
    {
        if (edit->text_len > 0)
        {
            edit->text_len--;
            edit->text[edit->text_len] = '\0';
            wedit_notify_change(edit);
        }
        return true;
    }

    if (event->key == SDLK_RETURN || event->key == SDLK_RETURN2 || event->key == SDLK_KP_ENTER)
    {
        if (edit->on_confirm)
            edit->on_confirm(edit, edit->user_data);
        return true;
    }

    if (event->key == SDLK_ESCAPE)
        return true;

    if (event->mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI | SDL_KMOD_ALT))
        return false;

    SDL_Keycode key = SDL_GetKeyFromScancode(event->scancode, event->mod, true);
    if (key >= 32 && key < 127)
        return wedit_push_char(edit, (char)key);

    return false;
}

void mel_wedit_init(Mel_WEdit* edit)
{
    assert(edit != nullptr);
    mel_widget_init(&edit->base);
    edit->base.draw = wedit_draw;
    edit->base.on_mouse_down = wedit_mouse_down;
    edit->base.on_key_down = wedit_key_down;
    edit->bg_color = mel_vec4(0.08f, 0.08f, 0.10f, 0.95f);
    edit->border_color = mel_vec4(0.30f, 0.30f, 0.34f, 1.0f);
    edit->focus_border_color = mel_vec4(0.95f, 0.35f, 0.35f, 1.0f);
    edit->text_color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    edit->placeholder_color = mel_vec4(0.55f, 0.55f, 0.60f, 1.0f);
    edit->font = MEL_FONT_ATLAS_HANDLE_NULL;
}

void mel_wedit_set_text(Mel_WEdit* edit, str8 text)
{
    assert(edit != nullptr);
    usize len = (usize)text.len < sizeof(edit->text) - 1 ? (usize)text.len : sizeof(edit->text) - 1;
    memcpy(edit->text, text.data, len);
    edit->text[len] = '\0';
    edit->text_len = (u32)len;
}

void mel_wedit_set_placeholder(Mel_WEdit* edit, str8 text)
{
    assert(edit != nullptr);
    usize len = (usize)text.len < sizeof(edit->placeholder) - 1 ? (usize)text.len : sizeof(edit->placeholder) - 1;
    memcpy(edit->placeholder, text.data, len);
    edit->placeholder[len] = '\0';
    edit->placeholder_len = (u32)len;
}

str8 mel_wedit_get_text(Mel_WEdit* edit)
{
    assert(edit != nullptr);
    return str8_from_parts((u8*)edit->text, edit->text_len);
}
