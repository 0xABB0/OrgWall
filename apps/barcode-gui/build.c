#include "build.h"

bool project(Mel_Build_Target *t) {
    mel_build_set_name(t, "barcode-gui");
    mel_build_set_kind(t, MEL_TARGET_APPLICATION);
    mel_build_add_source_root(t, "apps/barcode-gui/src");
    mel_build_add_dependency(t, "melody");
    mel_build_add_dependency(t, "barcode");

    mel_build_set_config(t, "ROOTPROJECT_NAME", "BarcodeGui");
    mel_build_set_config(t, "APP_LABEL", "Barcode Studio");
    mel_build_set_config(t, "NAMESPACE", "orgwall.barcodegui");
    mel_build_set_config(t, "APPLICATION_ID", "orgwall.barcodegui");
    mel_build_set_config(t, "BUNDLE_ID", "orgwall.barcodegui");
    mel_build_set_config(t, "VERSION_NAME", "1.0.0");
    return true;
}
