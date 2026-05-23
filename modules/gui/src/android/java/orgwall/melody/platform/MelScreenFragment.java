package orgwall.melody.platform;

import android.app.Fragment;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;

@SuppressWarnings("deprecation")
public class MelScreenFragment extends Fragment {

    private View view;

    public static MelScreenFragment forView(View v) {
        MelScreenFragment f = new MelScreenFragment();
        f.view = v;
        return f;
    }

    public View view() { return view; }

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle saved) {
        if (view != null && view.getParent() instanceof ViewGroup) {
            ((ViewGroup) view.getParent()).removeView(view);
        }
        return view;
    }
}
