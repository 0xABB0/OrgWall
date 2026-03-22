#include "sponza.camera.h"

#include <math.h>

#include "core.app.h"
#include "window.h"
#include "math.mat4.h"

static Mel_Vec3 sponza_camera_forward(const Sponza_Camera* camera)
{
    f32 cp = cosf(camera->pitch);
    return mel_vec3_normalize(mel_vec3(
        cosf(camera->yaw) * cp,
        sinf(camera->pitch),
        sinf(camera->yaw) * cp));
}

void sponza_camera_init(Sponza_Camera* camera,
                        Mel_Vec3 target,
                        Mel_Vec3 extents)
{
    *camera = (Sponza_Camera){0};
    camera->target = target;
    camera->extents = extents;

    f32 far_span = extents.x;
    if (extents.y > far_span) far_span = extents.y;
    if (extents.z > far_span) far_span = extents.z;
    camera->far_plane = far_span * 6.0f;
    if (camera->far_plane < 100.0f)
        camera->far_plane = 100.0f;

    camera->position = mel_vec3(
        target.x,
        target.y + extents.y * 0.12f,
        target.z + extents.z * 0.55f);

    Mel_Vec3 initial_forward = mel_vec3_normalize(mel_vec3_sub(target, camera->position));
    camera->yaw = atan2f(initial_forward.z, initial_forward.x);
    camera->pitch = asinf(initial_forward.y);
}

void sponza_camera_update(Sponza_Camera* camera,
                          Mel_Swapchain_Handle swapchain,
                          Mel_Render_View_Handle view,
                          f32 dt)
{
    if (!mel_swapchain_handle_valid(swapchain))
        return;
    if (!mel_render_view_handle_valid(view))
        return;

    Mel_Swapchain_Entry* sc_entry = mel_swapchain_registry_get(swapchain);
    if (sc_entry == nullptr)
        return;
    if (sc_entry->swapchain.extent_width == 0 || sc_entry->swapchain.extent_height == 0)
        return;

    f32 aspect = (f32)sc_entry->swapchain.extent_width / (f32)sc_entry->swapchain.extent_height;
    const bool* keys = SDL_GetKeyboardState(nullptr);

    Mel_Vec3 up = mel_vec3(0, 1, 0);
    Mel_Vec3 forward = sponza_camera_forward(camera);
    Mel_Vec3 flat_forward = mel_vec3(forward.x, 0.0f, forward.z);
    if (mel_vec3_len_sq(flat_forward) > 0.000001f)
        flat_forward = mel_vec3_normalize(flat_forward);
    else
        flat_forward = mel_vec3(0, 0, -1);

    Mel_Vec3 right = mel_vec3_cross(flat_forward, up);
    if (mel_vec3_len_sq(right) > 0.000001f)
        right = mel_vec3_normalize(right);
    else
        right = mel_vec3(1, 0, 0);

    Mel_Vec3 move = mel_vec3(0, 0, 0);
    if (keys[SDL_SCANCODE_W]) move = mel_vec3_add(move, flat_forward);
    if (keys[SDL_SCANCODE_S]) move = mel_vec3_sub(move, flat_forward);
    if (keys[SDL_SCANCODE_D]) move = mel_vec3_add(move, right);
    if (keys[SDL_SCANCODE_A]) move = mel_vec3_sub(move, right);
    if (keys[SDL_SCANCODE_E] || keys[SDL_SCANCODE_SPACE]) move = mel_vec3_add(move, up);
    if (keys[SDL_SCANCODE_Q] || keys[SDL_SCANCODE_C] || keys[SDL_SCANCODE_LCTRL]) move = mel_vec3_sub(move, up);

    if (mel_vec3_len_sq(move) > 0.000001f)
    {
        move = mel_vec3_normalize(move);

        f32 speed = 3.5f;
        f32 span_xz = camera->extents.x > camera->extents.z ? camera->extents.x : camera->extents.z;
        if (span_xz > 1.0f)
            speed = span_xz * 0.18f;
        if (speed < 3.5f)
            speed = 3.5f;
        if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT])
            speed *= 3.0f;

        camera->position = mel_vec3_add(camera->position, mel_vec3_scale(move, speed * dt));
    }

    Mel_Vec3 target = mel_vec3_add(camera->position, forward);

    mel_render_view_set_camera(view, (Mel_Render_Camera){
        .view = mel_mat4_look_at(camera->position, target, up),
        .projection = mel_mat4_perspective(60.0f * (3.14159265f / 180.0f), aspect, 0.1f, camera->far_plane),
        .viewport = {{ 0, 0, 1, 1 }},
        .visibility_mask = 0xFFFFFFFF,
        .lod_bias = 1.0f,
        .view_count = 1,
    });
}

void sponza_camera_event(Sponza_Camera* camera,
                         SDL_Event* event,
                         SDL_Window* window)
{
    if (event->type == SDL_EVENT_MOUSE_MOTION && camera->mouse_captured)
    {
        const f32 sensitivity = 0.0025f;
        camera->yaw += event->motion.xrel * sensitivity;
        camera->pitch -= event->motion.yrel * sensitivity;

        const f32 pitch_limit = 1.55334306f;
        if (camera->pitch > pitch_limit) camera->pitch = pitch_limit;
        if (camera->pitch < -pitch_limit) camera->pitch = -pitch_limit;
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT && !camera->mouse_captured)
    {
        SDL_SetWindowRelativeMouseMode(window, true);
        camera->mouse_captured = true;
    }

    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE && !event->key.repeat)
    {
        if (camera->mouse_captured)
        {
            SDL_SetWindowRelativeMouseMode(window, false);
            camera->mouse_captured = false;
        }
        else
        {
            mel_quit();
        }
    }
}
