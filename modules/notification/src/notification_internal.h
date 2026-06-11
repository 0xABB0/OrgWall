#pragma once

#include <notification/notification.h>
#include <notification/provider.h>
#include <collection/slotmap.h>

struct mel_notif_auth
{
    const char* name;
    bool        granted;
};

typedef struct
{
    Mel_Notif_Content content;
    Mel_Notif_Action* actions;
    Mel_Notif_Trigger trigger;
    bool              scheduled;
} Notif_Slot;

Notif_Slot*             mel_notif__slot(Mel_SlotMap_Handle h);
const Mel_Alloc*        mel_notif__alloc(void);
Mel_Notif_Provider_Desc* mel_notif__active_provider_desc(void);

void mel_notif__init_bare(const Mel_Alloc* alloc, Mel_Executor* exec);
