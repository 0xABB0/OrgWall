#pragma once

#include "core.types.h"
#include "allocator.fwd.h"
#include "string.str8.fwd.h"
#include "progress.h"

typedef struct Mel_Stage Mel_Stage;
typedef struct Mel_Loading_Stage Mel_Loading_Stage;
typedef struct Mel_Stage_Registry Mel_Stage_Registry;

typedef void (*Mel_Stage_Fn)(Mel_Stage* stage, void* user);

typedef struct {
    Mel_Stage_Fn on_start;
    Mel_Stage_Fn on_end;
    Mel_Stage_Fn on_tick;
    void*        user;
    bool         start_enabled;
} Mel_Stage_Opt;

struct Mel_Stage {
    Mel_Stage_Fn on_start;
    Mel_Stage_Fn on_end;
    Mel_Stage_Fn on_tick;
    void*        user;

    Mel_Stage*   next_active;
    Mel_Stage*   next_dirty;

    bool         attached;
    bool         enabled;
    bool         want_attached;
    bool         want_enabled;
    bool         queued;
};

typedef struct {
    Mel_Progress* progress;
    Mel_Stage*    next;
    f32           ready_at;
    bool          attach_next;
    bool          enable_next;
    bool          disable_self;
    bool          detach_self;
} Mel_Loading_Stage_Opt;

struct Mel_Loading_Stage {
    Mel_Stage     stage;
    Mel_Progress* progress;
    Mel_Stage*    next;
    f32           ready_at;
    bool          attach_next;
    bool          enable_next;
    bool          disable_self;
    bool          detach_self;
};

typedef struct {
    str8       name;
    Mel_Stage* stage;
    u32        tags;
} Mel_Stage_Reg_Opt;

typedef struct {
    str8       name;
    Mel_Stage* stage;
    u32        tags;
} Mel_Stage_Reg_Entry;

struct Mel_Stage_Registry {
    const Mel_Alloc*       alloc;
    Mel_Stage_Reg_Entry*   items;
    u32                    count;
    u32                    capacity;
};

void mel_stage_init_opt(Mel_Stage* stage, Mel_Stage_Opt opt);
#define mel_stage_init(stage, ...) mel_stage_init_opt((stage), (Mel_Stage_Opt){__VA_ARGS__})

void mel_stage_shutdown(Mel_Stage* stage);

void mel_stage_attach(Mel_Stage* stage);
void mel_stage_detach(Mel_Stage* stage);
void mel_stage_enable(Mel_Stage* stage);
void mel_stage_disable(Mel_Stage* stage);

bool mel_stage_is_attached(const Mel_Stage* stage);
bool mel_stage_is_enabled(const Mel_Stage* stage);

void mel_stage_tick(void);
void mel_stage_shutdown_all(void);

void mel_stage_registry_init(Mel_Stage_Registry* registry, const Mel_Alloc* alloc);
void mel_stage_registry_shutdown(Mel_Stage_Registry* registry);
bool mel_stage_registry_add_opt(Mel_Stage_Registry* registry, Mel_Stage_Reg_Opt opt);
#define mel_stage_registry_add(registry, ...) mel_stage_registry_add_opt((registry), (Mel_Stage_Reg_Opt){__VA_ARGS__})

Mel_Stage* mel_stage_registry_find(Mel_Stage_Registry* registry, str8 name);
bool mel_stage_registry_attach_named(Mel_Stage_Registry* registry, str8 name);
bool mel_stage_registry_detach_named(Mel_Stage_Registry* registry, str8 name);
bool mel_stage_registry_enable_named(Mel_Stage_Registry* registry, str8 name);
bool mel_stage_registry_disable_named(Mel_Stage_Registry* registry, str8 name);
u32 mel_stage_registry_detach_tagged(Mel_Stage_Registry* registry, u32 tags);
u32 mel_stage_registry_disable_tagged(Mel_Stage_Registry* registry, u32 tags);
bool mel_stage_registry_enable_exclusive(Mel_Stage_Registry* registry, str8 name, u32 within_tags);

void mel_loading_stage_init_opt(Mel_Loading_Stage* stage, Mel_Loading_Stage_Opt opt);
#define mel_loading_stage_init(stage, ...) mel_loading_stage_init_opt((stage), (Mel_Loading_Stage_Opt){__VA_ARGS__})

void mel_loading_stage_shutdown(Mel_Loading_Stage* stage);
