package com.epichearts.remaster;

import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.VibrationEffect;
import android.os.Vibrator;

import org.libsdl.app.SDLActivity;

/* SDL3 activity: everything (SDL, RmlUi, recompiled game) is linked into libmain.so */
public class EpicActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }

    /* Save export (src/ui/saveio.cpp) goes through SDL's file dialog, SDLActivity.showFileDialog: ACTION_CREATE_DOCUMENT
       with a wildcard type (".ehsave" has no MIME type on Android) and no file name. The suggested name waits in the SDL
       hint EH_DOCUMENT_NAME; give SAF that name and a concrete type (octet-stream keeps the name as typed). */
    /* vibration request from src/ui/input.cpp (SDL_SendAndroidMessage): param = ms | amplitude << 16 */
    @Override
    protected boolean onUnhandledMessage(int command, Object param) {
        if (command == 0x8001 && param instanceof Integer) {
            int p = (Integer) param;
            vibrate(p & 0xffff, (p >> 16) & 0xff);
            return true;
        }
        return super.onUnhandledMessage(command, param);
    }

    /* vibration (src/ui/input.cpp): ms 0 cancels; amplitude 1..255 where the device supports it (API 26+) */
    public void vibrate(int ms, int amplitude) {
        Vibrator v = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
        if (v == null || !v.hasVibrator()) return;
        if (ms <= 0) { v.cancel(); return; }
        if (Build.VERSION.SDK_INT >= 26) {
            int a = Math.max(1, Math.min(255, amplitude));
            v.vibrate(VibrationEffect.createOneShot(ms, v.hasAmplitudeControl() ? a : VibrationEffect.DEFAULT_AMPLITUDE));
        } else {
            v.vibrate(ms);
        }
    }

    @Override
    public void startActivityForResult(Intent intent, int requestCode) {
        if (Intent.ACTION_CREATE_DOCUMENT.equals(intent.getAction())) {
            String name = nativeGetHint("EH_DOCUMENT_NAME");
            if (name != null && !name.isEmpty()) {
                intent.setType("application/octet-stream");
                intent.putExtra(Intent.EXTRA_TITLE, name);
            }
        }
        super.startActivityForResult(intent, requestCode);
    }
}
