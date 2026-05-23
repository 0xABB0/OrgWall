#include "build.h"

bool project(Mel_Build_Target *t) {
    mel_build_set_name(t, "hello-world-gui");
    mel_build_set_kind(t, MEL_TARGET_APPLICATION);
    mel_build_add_source_root(t, "apps/hello-world-gui/src");
    mel_build_add_dependency(t, "melody");

    mel_build_set_config(t, "ROOTPROJECT_NAME", "HelloWorldGui");
    mel_build_set_config(t, "NAMESPACE", "orgwall.helloworld");
    mel_build_set_config(t, "APPLICATION_ID", "orgwall.helloworld");
    mel_build_set_config(t, "BUNDLE_ID", "orgwall.helloworld");
    return true;
}
