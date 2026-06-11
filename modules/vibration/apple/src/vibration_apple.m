#include <vibration/provider.h>
#include <log/log.h>

#import <CoreHaptics/CoreHaptics.h>
#import <Foundation/Foundation.h>

#define MEL_VIB_APPLE_STABLE_ID 0x6170706C68617000ULL
#define MEL_VIB_TRANSIENT_FLOOR_S 0.02f

static CHHapticEngine*          g_engine;
static id<CHHapticPatternPlayer> g_player;

static bool apple_supports(void)
{
    return CHHapticEngine.capabilitiesForHardware.supportsHaptics;
}

static bool apple_ensure_engine(void)
{
    if (g_engine)
        return true;
    if (!apple_supports())
        return false;
    NSError* err = nil;
    g_engine = [[CHHapticEngine alloc] initAndReturnError:&err];
    if (!g_engine)
    {
        mel_log_error("vibration", "CHHapticEngine init failed: %s", err ? err.localizedDescription.UTF8String : "unknown");
        return false;
    }
    if (![g_engine startAndReturnError:&err])
    {
        mel_log_error("vibration", "CHHapticEngine start failed: %s", err ? err.localizedDescription.UTF8String : "unknown");
        g_engine = nil;
        return false;
    }
    return true;
}

static u32 apple_enumerate(void* user, Mel_Vib_Raw* out, u32 cap)
{
    (void)user;
    if (cap == 0 || !apple_supports())
        return 0;
    out[0] = (Mel_Vib_Raw){
        .stable_id = MEL_VIB_APPLE_STABLE_ID,
        .name = S8("CoreHaptics"),
        .caps = {
            .present = true,
            .amplitude = true,
            .sharpness = true,
            .envelopes = true,
            .continuous = true,
            .can_pause = true,
            .pause_exact = false,
            .completion_exact = false,
            .actuator_count = 1,
        },
    };
    return 1;
}

static Mel_Vib_Status apple_submit(void* user, u64 stable_id, u64 token, const Mel_Vib_Lowered* lowered, Mel_Vib_Completion completion)
{
    (void)user;
    (void)stable_id;
    (void)token;
    (void)completion;
    if (!apple_ensure_engine())
        return MEL_VIB_ERROR;
    if (lowered->count == 0)
        return MEL_VIB_ERROR;

    NSMutableArray<CHHapticEvent*>* events = [NSMutableArray array];
    for (u32 i = 0; i < lowered->count; i++)
    {
        Mel_Vib_Event           e = lowered->events[i];
        CHHapticEventParameter* intensity = [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity value:e.intensity];
        CHHapticEventParameter* sharpness = [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticSharpness value:e.sharpness];
        bool                    continuous = e.duration > 0.0f;
        CHHapticEventType       type = continuous ? CHHapticEventTypeHapticContinuous : CHHapticEventTypeHapticTransient;
        CHHapticEvent*          ev = [[CHHapticEvent alloc] initWithEventType:type
                                                          parameters:@[ intensity, sharpness ]
                                                        relativeTime:e.at
                                                            duration:continuous ? e.duration : 0.0];
        [events addObject:ev];
    }

    NSError*         err = nil;
    CHHapticPattern* pattern = [[CHHapticPattern alloc] initWithEvents:events parameters:@[] error:&err];
    if (!pattern)
    {
        mel_log_error("vibration", "CHHapticPattern build failed: %s", err ? err.localizedDescription.UTF8String : "unknown");
        return MEL_VIB_ERROR;
    }
    id<CHHapticPatternPlayer> player = [g_engine createPlayerWithPattern:pattern error:&err];
    if (!player)
    {
        mel_log_error("vibration", "CHHaptic player create failed: %s", err ? err.localizedDescription.UTF8String : "unknown");
        return MEL_VIB_ERROR;
    }
    if (![player startAtTime:0 error:&err])
    {
        mel_log_error("vibration", "CHHaptic player start failed: %s", err ? err.localizedDescription.UTF8String : "unknown");
        return MEL_VIB_ERROR;
    }
    g_player = player;
    return MEL_VIB_OK;
}

static void apple_abort(void* user, u64 stable_id, u64 token)
{
    (void)user;
    (void)stable_id;
    (void)token;
    if (g_player)
    {
        [g_player stopAtTime:0 error:nil];
        g_player = nil;
    }
}

void mel_vib__register_host_providers(void)
{
    static const Mel_Vib_Provider_Desc desc = {
        .name = "apple-corehaptics",
        .enumerate = apple_enumerate,
        .submit = apple_submit,
        .abort = apple_abort,
    };
    mel_vib_provider_register(&desc);
}
