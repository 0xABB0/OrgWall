#include "build.h"

bool project(Mel_Build_Target *t) {
    mel_build_set_name(t, "display-gui");
    mel_build_set_kind(t, MEL_TARGET_APPLICATION);
    mel_build_add_source_root(t, "apps/display-gui/src");
    mel_build_add_dependency(t, "melody");

    mel_build_set_config(t, "ROOTPROJECT_NAME", "DisplayGui");
    mel_build_set_config(t, "APP_LABEL", "Display GUI");
    mel_build_set_config(t, "NAMESPACE", "orgwall.displaygui");
    mel_build_set_config(t, "APPLICATION_ID", "orgwall.displaygui");
    mel_build_set_config(t, "BUNDLE_ID", "orgwall.displaygui");
    mel_build_set_config(t, "VERSION_NAME", "1.0.0");
    return true;
}
