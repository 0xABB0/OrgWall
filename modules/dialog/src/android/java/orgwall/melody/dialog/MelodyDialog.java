package orgwall.melody.dialog;

import android.app.Activity;
import android.content.ClipData;
import android.content.Intent;
import android.net.Uri;
import android.provider.DocumentsContract;

import java.util.ArrayList;

public final class MelodyDialog
{
    public static final int REQUEST_BASE = 0x4d454c00;

    private MelodyDialog() {}

    private static native void nativeOnResult(long token, String[] paths, int chosenFilter, boolean cancelled);

    private static Activity sActivity;
    private static long      sPendingToken;

    public static void attach(Activity activity)
    {
        sActivity = activity;
    }

    public static void launch(long token, int request, String title, String[] mimeTypes, boolean multi)
    {
        if (sActivity == null)
        {
            nativeOnResult(token, new String[0], 0, true);
            return;
        }
        sPendingToken = token;

        Intent intent;
        if ((request & 0x8) != 0)
        {
            intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        }
        else if ((request & 0x4) != 0)
        {
            intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType(mimeTypes != null && mimeTypes.length > 0 ? mimeTypes[0] : "*/*");
        }
        else
        {
            intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            if (mimeTypes != null && mimeTypes.length > 0)
                intent.putExtra(Intent.EXTRA_MIME_TYPES, mimeTypes);
            if (multi)
                intent.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        }
        if (title != null)
            intent.putExtra(Intent.EXTRA_TITLE, title);

        try
        {
            sActivity.startActivityForResult(intent, REQUEST_BASE);
        }
        catch (Exception e)
        {
            nativeOnResult(token, new String[0], 0, true);
        }
    }

    public static boolean onActivityResult(int requestCode, int resultCode, Intent data)
    {
        if (requestCode != REQUEST_BASE)
            return false;
        long token = sPendingToken;
        sPendingToken = 0;
        if (resultCode != Activity.RESULT_OK || data == null)
        {
            nativeOnResult(token, new String[0], 0, true);
            return true;
        }
        ArrayList<String> uris = new ArrayList<>();
        ClipData          clip = data.getClipData();
        if (clip != null)
        {
            for (int i = 0; i < clip.getItemCount(); i++)
            {
                Uri u = clip.getItemAt(i).getUri();
                if (u != null)
                    uris.add(u.toString());
            }
        }
        else if (data.getData() != null)
        {
            uris.add(data.getData().toString());
        }
        nativeOnResult(token, uris.toArray(new String[0]), 0, uris.isEmpty());
        return true;
    }
}
