#include "build.h"

bool project(Mel_Build_Target *t) {
    mel_build_set_name(t, "display-probe");
    mel_build_set_kind(t, MEL_TARGET_APPLICATION);
    mel_build_add_source_root(t, "apps/display-probe/src");
    mel_build_add_dependency(t, "melody");

    mel_build_set_config(t, "ROOTPROJECT_NAME", "DisplayProbe");
    mel_build_set_config(t, "APP_LABEL", "Display Probe");
    mel_build_set_config(t, "NAMESPACE", "orgwall.displayprobe");
    mel_build_set_config(t, "APPLICATION_ID", "orgwall.displayprobe");
    mel_build_set_config(t, "BUNDLE_ID", "orgwall.displayprobe");
    mel_build_set_config(t, "VERSION_NAME", "1.0.0");
    return true;
}
