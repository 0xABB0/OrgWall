#pragma once

#include "core.types.h"
#include "progress.h"

typedef struct Mel_Stage Mel_Stage;
typedef struct Mel_Loading_Stage Mel_Loading_Stage;

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

void mel_loading_stage_init_opt(Mel_Loading_Stage* stage, Mel_Loading_Stage_Opt opt);
#define mel_loading_stage_init(stage, ...) mel_loading_stage_init_opt((stage), (Mel_Loading_Stage_Opt){__VA_ARGS__})

void mel_loading_stage_shutdown(Mel_Loading_Stage* stage);
