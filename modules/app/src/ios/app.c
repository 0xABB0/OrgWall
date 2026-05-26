// Shadows posix/app.c on iOS (same basename). The real entry is Objective-C
// (UIApplicationMain owns the main thread), so this C stub just forwards.
int mel_ios_app_main(int argc, char** argv);

int main(int argc, char** argv)
{
    return mel_ios_app_main(argc, argv);
}
