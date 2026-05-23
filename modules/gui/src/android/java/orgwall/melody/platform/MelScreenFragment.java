package orgwall.melody.platform;

import android.app.Fragment;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;

@SuppressWarnings("deprecation")
public class MelScreenFragment extends Fragment {

    private static final String ARG_HANDLE = "mel.handle";

    public static MelScreenFragment forHandle(long handle) {
        MelScreenFragment f = new MelScreenFragment();
        Bundle args = new Bundle();
        args.putLong(ARG_HANDLE, handle);
        f.setArguments(args);
        return f;
    }

    public long handle() {
        Bundle args = getArguments();
        return args != null ? args.getLong(ARG_HANDLE) : 0L;
    }

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle saved) {
        View v = MelGui.viewFor(handle());
        if (v != null && v.getParent() instanceof ViewGroup) {
            ((ViewGroup) v.getParent()).removeView(v);
        }
        return v;
    }
}
