#include "mugen_char.h"
#include "mugen_sff.h"
#include "mugen_air.h"
#include "mugen_cmd.h"
#include "mugen_cns.h"
#include "anim.clip.h"
#include "anim.registry.h"
#include "collection.slotmap.h"
#include "gpu.texture.h"
#include "gpu.pipeline.h"
#include "texture.pool.h"
#include "sprite.pass.h"
#include "vfs.h"
#include "allocator.h"
#include "string.str8.h"
#include <SDL3/SDL.h>
#include <string.h>

bool mugen_char_load_opt(Mugen_Char* mc, Mugen_Char_Load_Opt opt)
{
    *mc = (Mugen_Char){0};

    if (!mugen_sff_load(&mc->sff, opt.vfs, opt.sff_path, opt.alloc))
        return false;

    usize air_size = 0;
    u8* air_data = mel_vfs_read_file_alloc(opt.vfs, opt.air_path, &air_size, opt.alloc);
    if (!air_data)
    {
        mugen_sff_shutdown(&mc->sff, opt.alloc);
        return false;
    }

    str8 air_str = str8_from_parts(air_data, (size)air_size);
    if (!mugen_air_load(&mc->air, air_str, opt.alloc))
    {
        mel_dealloc(opt.alloc, air_data);
        mugen_sff_shutdown(&mc->sff, opt.alloc);
        return false;
    }
    mel_dealloc(opt.alloc, air_data);

    usize cmd_size = 0;
    mc->cmd_file_data = mel_vfs_read_file_alloc(opt.vfs, opt.cmd_path, &cmd_size, opt.alloc);
    if (mc->cmd_file_data)
    {
        str8 cmd_str = str8_from_parts(mc->cmd_file_data, (size)cmd_size);
        mugen_cmd_load(&mc->cmd, cmd_str, opt.alloc);

        const char* needle = "[Statedef -1]";
        size needle_len = (size)strlen(needle);
        for (size i = 0; i + needle_len <= cmd_str.len; i++)
        {
            bool match = true;
            for (size j = 0; j < needle_len; j++)
            {
                u8 a = cmd_str.data[i + j];
                u8 b = (u8)needle[j];
                if (a >= 'A' && a <= 'Z') a += 32;
                if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) { match = false; break; }
            }
            if (match)
            {
                str8 cmd_cns_str = str8_from_parts(cmd_str.data + i, cmd_str.len - i);
                mugen_cns_load(&mc->cmd_cns, cmd_cns_str, opt.alloc);
                break;
            }
        }
    }

    usize cns_size = 0;
    mc->cns_file_data = mel_vfs_read_file_alloc(opt.vfs, opt.cns_path, &cns_size, opt.alloc);
    if (mc->cns_file_data)
    {
        str8 cns_str = str8_from_parts(mc->cns_file_data, (size)cns_size);
        mugen_cns_load(&mc->cns, cns_str, opt.alloc);
        mc->cns_loaded = true;
    }

    usize common_size = 0;
    mc->common_cns_file_data = mel_vfs_read_file_alloc(opt.vfs, opt.common_cns_path, &common_size, opt.alloc);
    if (mc->common_cns_file_data)
    {
        str8 common_str = str8_from_parts(mc->common_cns_file_data, (size)common_size);
        mugen_cns_load(&mc->common_cns, common_str, opt.alloc);
    }

    mel_anim_registry_init(opt.alloc);
    mel_slotmap_init(&mc->clip_pool, opt.alloc, .item_size = sizeof(Mel_Anim_Clip));

    mc->action_map = mel_alloc(opt.alloc, mc->air.action_count * sizeof(Fighter_Action_Map));
    mc->action_map_count = 0;

    for (u32 i = 0; i < mc->air.action_count; i++)
    {
        Mugen_Air_Action* action = &mc->air.actions[i];
        if (action->frame_count == 0) continue;

        Mel_Anim_Clip clip = mugen_air_compile(action, opt.alloc);
        Mel_Anim_Clip_Handle handle = mel_slotmap_insert(&mc->clip_pool, &clip);

        mc->action_map[mc->action_map_count] = (Fighter_Action_Map){
            .action_number = action->action_number,
            .clip = handle,
        };
        mc->action_map_count++;
    }

    mel_gpu_texture_init(&mc->tex, opt.dev,
        .pixels = mc->sff.atlas_pixels,
        .width = mc->sff.atlas_width,
        .height = mc->sff.atlas_height,
        .nearest_filter = true);
    mc->tex.descriptor = mel_gpu_pipeline_alloc_descriptor(&opt.sprite_pass->pipeline, opt.dev);
    mel_gpu_pipeline_write_texture(&opt.sprite_pass->pipeline, opt.dev,
        mc->tex.descriptor, mc->tex.image.view, mc->tex.sampler);
    mc->tex_handle = mel_texture_pool_register(opt.tex_pool, &mc->tex);

    mc->loaded = true;
    SDL_Log("MUGEN char loaded: %u actions compiled", mc->action_map_count);
    return true;
}

void mugen_char_shutdown(Mugen_Char* mc, Mel_Gpu_Device* dev, const Mel_Alloc* alloc)
{
    if (!mc->loaded) return;

    mel_gpu_texture_shutdown(&mc->tex, dev);

    for (u32 i = 0; i < mc->action_map_count; i++)
    {
        Mel_Anim_Clip* clip = (Mel_Anim_Clip*)mel_slotmap_get(&mc->clip_pool, mc->action_map[i].clip);
        if (clip) mel_anim_clip_destroy(clip, alloc);
    }
    mel_slotmap_free(&mc->clip_pool);

    if (mc->action_map)
        mel_dealloc(alloc, mc->action_map);

    mugen_cmd_shutdown(&mc->cmd, alloc);
    if (mc->cmd_file_data)
        mel_dealloc(alloc, mc->cmd_file_data);
    if (mc->cns_file_data)
        mel_dealloc(alloc, mc->cns_file_data);
    if (mc->common_cns_file_data)
        mel_dealloc(alloc, mc->common_cns_file_data);

    mugen_air_shutdown(&mc->air, alloc);
    mugen_sff_shutdown(&mc->sff, alloc);

    mc->loaded = false;
}
