package com.blinkzero.nostrings;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Context;
import android.media.midi.MidiDevice;
import android.media.midi.MidiDeviceInfo;
import android.media.midi.MidiManager;
import android.media.midi.MidiOutputPort;
import android.media.midi.MidiReceiver;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.view.WindowManager;
import android.webkit.JavascriptInterface;
import android.webkit.PermissionRequest;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import androidx.webkit.WebViewAssetLoader;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * No Strings for Android: the single-file web build (assets/index.html) in a WebView,
 * served from an https origin (WebViewAssetLoader) so the AudioWorklet and local
 * storage work, plus the phone's own MIDI devices forwarded into the page.
 *
 * MIDI arrives two ways and the page uses whichever it gets first:
 *  - Web MIDI inside the WebView (granted here through onPermissionRequest)
 *  - MidiManager: every USB / Bluetooth / virtual MIDI device the system knows,
 *    parsed into complete messages and pushed to window.nostringsNativeMidi.message()
 */
public class MainActivity extends Activity {
    private static final String ASSET_HOST = "appassets.androidplatform.net";

    private WebView web;
    private final Handler main = new Handler(Looper.getMainLooper());
    private MidiManager midi;
    private final Map<String, MidiDevice> openDevices = new HashMap<>();
    private final List<MidiOutputPort> openPorts = new ArrayList<>();
    private boolean pageReady = false;

    @SuppressLint("SetJavaScriptEnabled")
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        web = new WebView(this);
        setContentView(web);
        WebView.setWebContentsDebuggingEnabled(true);   // chrome://inspect from the desktop

        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setMediaPlaybackRequiresUserGesture(false);   // the page unlocks audio on the first touch anyway
        s.setAllowFileAccess(false);
        s.setLoadWithOverviewMode(true);
        s.setUseWideViewPort(true);
        s.setSupportZoom(false);
        s.setBuiltInZoomControls(false);
        s.setDisplayZoomControls(false);
        s.setCacheMode(WebSettings.LOAD_NO_CACHE);
        web.setBackgroundColor(0xFF1A110A);

        final WebViewAssetLoader loader = new WebViewAssetLoader.Builder()
                .setDomain(ASSET_HOST)
                .addPathHandler("/assets/", new WebViewAssetLoader.AssetsPathHandler(this))
                .build();
        web.setWebViewClient(new WebViewClient() {
            @Override
            public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
                return loader.shouldInterceptRequest(request.getUrl());
            }
        });
        web.setWebChromeClient(new WebChromeClient() {
            @Override
            public void onPermissionRequest(PermissionRequest request) {
                // Web MIDI (sysex) and anything else the page asks for: it's our own page
                request.grant(request.getResources());
            }
        });
        web.addJavascriptInterface(new Bridge(), "NoStringsAndroid");
        web.loadUrl("https://" + ASSET_HOST + "/assets/index.html");

        midi = (MidiManager) getSystemService(Context.MIDI_SERVICE);
        if (midi != null) {
            midi.registerDeviceCallback(new MidiManager.DeviceCallback() {
                @Override public void onDeviceAdded(MidiDeviceInfo info) { openDevice(info); }
                @Override public void onDeviceRemoved(MidiDeviceInfo info) { closeDevice(info); }
            }, main);
        }
    }

    /** Called by the page (window.NoStringsAndroid.midiReady) once the host is listening. */
    private class Bridge {
        @JavascriptInterface
        public void midiReady() {
            main.post(() -> {
                pageReady = true;
                if (midi == null) { sendDevices(); return; }
                for (MidiDeviceInfo info : midi.getDevices()) openDevice(info);
                sendDevices();
            });
        }
    }

    // ---------------------------------------------------------------- MIDI ---
    private static String key(MidiDeviceInfo info) { return String.valueOf(info.getId()); }

    private static String nameOf(MidiDeviceInfo info) {
        Bundle p = info.getProperties();
        String n = p.getString(MidiDeviceInfo.PROPERTY_NAME);
        if (n == null || n.isEmpty()) n = p.getString(MidiDeviceInfo.PROPERTY_PRODUCT);
        if (n == null || n.isEmpty()) n = "MIDI device " + info.getId();
        return n;
    }

    private void openDevice(final MidiDeviceInfo info) {
        if (midi == null || info.getOutputPortCount() == 0 || openDevices.containsKey(key(info))) return;
        midi.openDevice(info, device -> {
            if (device == null) return;
            main.post(() -> {
                openDevices.put(key(info), device);
                for (int i = 0; i < info.getOutputPortCount(); i++) {
                    MidiOutputPort port = device.openOutputPort(i);   // "output" = data coming out of the device, into us
                    if (port == null) continue;
                    port.connect(new Parser());
                    openPorts.add(port);
                }
                sendDevices();
            });
        }, main);
    }

    private void closeDevice(MidiDeviceInfo info) {
        MidiDevice d = openDevices.remove(key(info));
        if (d != null) { try { d.close(); } catch (IOException ignored) {} }
        sendDevices();
    }

    private void sendDevices() {
        if (!pageReady) return;
        StringBuilder sb = new StringBuilder("[");
        boolean first = true;
        if (midi != null) {
            for (MidiDeviceInfo info : midi.getDevices()) {
                if (info.getOutputPortCount() == 0) continue;
                if (!first) sb.append(',');
                first = false;
                sb.append('"').append(nameOf(info).replace("\\", "\\\\").replace("\"", "\\\"")).append('"');
            }
        }
        sb.append(']');
        js("window.nostringsNativeMidi && window.nostringsNativeMidi.devices(" + sb + ")");
    }

    private void js(String code) {
        main.post(() -> { if (web != null) web.evaluateJavascript(code, null); });
    }

    /** Splits the byte stream into complete channel messages (running status included). */
    private class Parser extends MidiReceiver {
        private int status = 0, needed = 0, have = 0;
        private final int[] data = new int[2];

        private int lengthFor(int st) {
            int hi = st & 0xF0;
            if (hi == 0xC0 || hi == 0xD0) return 1;
            if (hi >= 0x80 && hi <= 0xE0) return 2;
            return 0;   // system messages: ignored
        }

        @Override
        public void onSend(byte[] msg, int offset, int count, long timestamp) {
            for (int i = 0; i < count; i++) {
                int b = msg[offset + i] & 0xFF;
                if (b >= 0xF8) continue;                 // realtime: ignore
                if (b >= 0x80) {
                    status = b; needed = lengthFor(b); have = 0;
                    if (b >= 0xF0) status = 0;           // system common: ignore until the next status
                    continue;
                }
                if (status == 0 || needed == 0) continue;
                data[have++] = b;
                if (have == needed) {
                    final int st = status, d1 = data[0], d2 = needed > 1 ? data[1] : 0;
                    js("window.nostringsNativeMidi && window.nostringsNativeMidi.message([" + st + "," + d1 + "," + d2 + "])");
                    have = 0;                             // running status: keep `status`
                }
            }
        }
    }

    // ------------------------------------------------------------- lifecycle ---
    @Override
    protected void onResume() {
        super.onResume();
        web.onResume();
        web.setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
    }

    @Override
    protected void onPause() {
        web.onPause();
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        for (MidiOutputPort p : openPorts) { try { p.close(); } catch (IOException ignored) {} }
        for (MidiDevice d : openDevices.values()) { try { d.close(); } catch (IOException ignored) {} }
        openPorts.clear();
        openDevices.clear();
        super.onDestroy();
    }

    @Override
    public void onBackPressed() {
        moveTaskToBack(true);   // keep the instrument alive in the background instead of killing it
    }
}
