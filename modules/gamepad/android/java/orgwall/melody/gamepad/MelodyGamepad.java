package orgwall.melody.gamepad;

import android.view.InputDevice;

public final class MelodyGamepad
{
    private MelodyGamepad() {}

    public static boolean isGamepad(InputDevice device)
    {
        if (device == null)
            return false;
        int sources = device.getSources();
        return (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD || (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }
}
