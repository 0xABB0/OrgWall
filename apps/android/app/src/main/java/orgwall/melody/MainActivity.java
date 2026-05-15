package orgwall.melody;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.FrameLayout;
import android.widget.TextView;

public final class MainActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        TextView hello = new TextView(this);
        hello.setText(R.string.hello_world);
        hello.setTextColor(Color.rgb(245, 241, 232));
        hello.setTextSize(28.0f);
        hello.setGravity(Gravity.CENTER);

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.rgb(21, 31, 42));
        root.addView(
                hello,
                new FrameLayout.LayoutParams(
                        FrameLayout.LayoutParams.MATCH_PARENT,
                        FrameLayout.LayoutParams.MATCH_PARENT));

        setContentView(root);
    }
}
