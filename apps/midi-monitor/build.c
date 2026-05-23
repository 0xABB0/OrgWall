#include "build.h"

bool project(Mel_Build_Target *t) {
    mel_build_set_name(t, "midi-monitor");
    mel_build_set_kind(t, MEL_TARGET_APPLICATION);
    mel_build_add_source_root(t, "apps/midi-monitor/src");
    mel_build_add_dependency(t, "melody");

    mel_build_set_config(t, "ROOTPROJECT_NAME", "MidiMonitor");
    mel_build_set_config(t, "NAMESPACE", "orgwall.midimonitor");
    mel_build_set_config(t, "APPLICATION_ID", "orgwall.midimonitor");
    mel_build_set_config(t, "BUNDLE_ID", "orgwall.midimonitor");
    return true;
}
