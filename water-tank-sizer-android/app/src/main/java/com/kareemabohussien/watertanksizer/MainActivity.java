package com.kareemabohussien.watertanksizer;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Picture;
import android.graphics.RectF;
import android.graphics.pdf.PdfDocument;
import android.net.Uri;
import android.os.Bundle;\nimport android.os.CancellationSignal;\nimport android.os.ParcelFileDescriptor;
import android.print.PrintAttributes;
import android.print.PrintDocumentAdapter;\nimport android.print.PrintDocumentInfo;\nimport android.print.PageRange;
import android.print.PrintManager;
import android.webkit.JavascriptInterface;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Toast;

import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

public class MainActivity extends Activity {
    private static final int REQUEST_FILE_CHOOSER = 2001;
    private static final int REQUEST_SAVE_FILE = 2002;
    private static final int REQUEST_SAVE_PDF = 2003;

    private WebView webView;
    private ValueCallback<Uri[]> filePathCallback;
    private String pendingExportContent;
    private boolean waitingForPrintReturn = false;
    private boolean printUiPaused = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Required so capturePicture() can render the complete long report, not only the viewport.
        WebView.enableSlowWholeDocumentDraw();

        webView = new WebView(this);
        setContentView(webView);

        WebSettings settings = webView.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setDatabaseEnabled(true);
        settings.setAllowFileAccess(true);
        settings.setAllowContentAccess(true);
        settings.setBuiltInZoomControls(false);
        settings.setDisplayZoomControls(false);
        settings.setSupportZoom(false);

        webView.setWebViewClient(new WebViewClient());
        webView.setWebChromeClient(new WebChromeClient() {
            @Override
            public boolean onShowFileChooser(WebView webView, ValueCallback<Uri[]> callback, FileChooserParams params) {
                if (filePathCallback != null) filePathCallback.onReceiveValue(null);
                filePathCallback = callback;
                try {
                    startActivityForResult(params.createIntent(), REQUEST_FILE_CHOOSER);
                    return true;
                } catch (Exception e) {
                    filePathCallback = null;
                    Toast.makeText(MainActivity.this, "Unable to open file picker", Toast.LENGTH_SHORT).show();
                    return false;
                }
            }
        });

        webView.addJavascriptInterface(new AndroidBridge(), "Android");
        webView.loadUrl("file:///android_asset/index.html");
    }

    private class AndroidBridge {
        @JavascriptInterface
        public void saveTextFile(String content, String suggestedName) {
            pendingExportContent = content;
            runOnUiThread(() -> {
                Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("application/json");
                intent.putExtra(Intent.EXTRA_TITLE,
                        (suggestedName == null || suggestedName.trim().isEmpty())
                                ? "Water-Tank-Project.json"
                                : suggestedName);
                startActivityForResult(intent, REQUEST_SAVE_FILE);
            });
        }

        @JavascriptInterface
        public void printPage() {
            runOnUiThread(() -> {
                webView.evaluateJavascript("prepareReportMode(true);", ignored ->
                        webView.postDelayed(() -> {
                            try {
                                Toast.makeText(MainActivity.this, "Opening print dialog…", Toast.LENGTH_SHORT).show();
                                PrintManager pm = (PrintManager) getSystemService(Context.PRINT_SERVICE);
                                PrintDocumentAdapter adapter =
                                        webView.createPrintDocumentAdapter("Water Tank Sizing - Kareem abo Hussien");
                                PrintAttributes attrs = new PrintAttributes.Builder()
                                        .setMediaSize(PrintAttributes.MediaSize.ISO_A4.asLandscape())
                                        .setColorMode(PrintAttributes.COLOR_MODE_COLOR)
                                        .build();
                                waitingForPrintReturn = true;
                                printUiPaused = false;
                                pm.print("Water Tank Sizing - Kareem abo Hussien", adapter, attrs);
                            } catch (Exception e) {
                                restoreReportMode();
                                Toast.makeText(MainActivity.this,
                                        "Print service unavailable. Use Save PDF instead.",
                                        Toast.LENGTH_LONG).show();
                            }
                        }, 350));
            });
        }

        @JavascriptInterface
        public void savePdf() {
            runOnUiThread(() -> {
                webView.evaluateJavascript("prepareReportMode(true);", ignored ->
                        webView.postDelayed(() -> {
                            Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                            intent.addCategory(Intent.CATEGORY_OPENABLE);
                            intent.setType("application/pdf");
                            intent.putExtra(Intent.EXTRA_TITLE, "Water-Tank-Sizing-Kareem-abo-Hussien.pdf");
                            startActivityForResult(intent, REQUEST_SAVE_PDF);
                        }, 300));
            });
        }
    }

    private void writePdf(Uri uri) {
        webView.postDelayed(() -> {
            ParcelFileDescriptor destination = null;
            try {
                destination = getContentResolver().openFileDescriptor(uri, "w");
                if (destination == null) {
                    throw new IllegalStateException("Cannot open selected PDF location");
                }

                final ParcelFileDescriptor finalDestination = destination;
                final PrintDocumentAdapter adapter =
                        webView.createPrintDocumentAdapter("Water Tank Sizing - Kareem abo Hussien");

                final PrintAttributes attrs = new PrintAttributes.Builder()
                        .setMediaSize(PrintAttributes.MediaSize.ISO_A4.asLandscape())
                        .setResolution(new PrintAttributes.Resolution("v8pdf", "V8 PDF", 300, 300))
                        .setMinMargins(PrintAttributes.Margins.NO_MARGINS)
                        .setColorMode(PrintAttributes.COLOR_MODE_COLOR)
                        .build();

                final CancellationSignal cancellationSignal = new CancellationSignal();

                adapter.onLayout(
                        null,
                        attrs,
                        cancellationSignal,
                        new PrintDocumentAdapter.LayoutResultCallback() {
                            @Override
                            public void onLayoutFinished(PrintDocumentInfo info, boolean changed) {
                                adapter.onWrite(
                                        new PageRange[]{PageRange.ALL_PAGES},
                                        finalDestination,
                                        cancellationSignal,
                                        new PrintDocumentAdapter.WriteResultCallback() {
                                            @Override
                                            public void onWriteFinished(PageRange[] pages) {
                                                try {
                                                    finalDestination.close();
                                                } catch (Exception ignored) {
                                                }
                                                Toast.makeText(
                                                        MainActivity.this,
                                                        "A4 landscape PDF saved successfully",
                                                        Toast.LENGTH_LONG
                                                ).show();
                                                restoreReportMode();
                                            }

                                            @Override
                                            public void onWriteFailed(CharSequence error) {
                                                try {
                                                    finalDestination.close();
                                                } catch (Exception ignored) {
                                                }
                                                Toast.makeText(
                                                        MainActivity.this,
                                                        "PDF save failed" + (error == null ? "" : ": " + error),
                                                        Toast.LENGTH_LONG
                                                ).show();
                                                restoreReportMode();
                                            }

                                            @Override
                                            public void onWriteCancelled() {
                                                try {
                                                    finalDestination.close();
                                                } catch (Exception ignored) {
                                                }
                                                restoreReportMode();
                                            }
                                        }
                                );
                            }

                            @Override
                            public void onLayoutFailed(CharSequence error) {
                                try {
                                    finalDestination.close();
                                } catch (Exception ignored) {
                                }
                                Toast.makeText(
                                        MainActivity.this,
                                        "PDF layout failed" + (error == null ? "" : ": " + error),
                                        Toast.LENGTH_LONG
                                ).show();
                                restoreReportMode();
                            }

                            @Override
                            public void onLayoutCancelled() {
                                try {
                                    finalDestination.close();
                                } catch (Exception ignored) {
                                }
                                restoreReportMode();
                            }
                        },
                        null
                );
            } catch (Exception e) {
                if (destination != null) {
                    try {
                        destination.close();
                    } catch (Exception ignored) {
                    }
                }
                Toast.makeText(
                        MainActivity.this,
                        "PDF save failed: " + e.getMessage(),
                        Toast.LENGTH_LONG
                ).show();
                restoreReportMode();
            }
        }, 500);
    }

    private void restoreReportMode() {
        if (webView != null) {
            webView.evaluateJavascript("prepareReportMode(false);", null);
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (waitingForPrintReturn) printUiPaused = true;
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (waitingForPrintReturn && printUiPaused) {
            waitingForPrintReturn = false;
            printUiPaused = false;
            restoreReportMode();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);

        if (requestCode == REQUEST_FILE_CHOOSER) {
            if (filePathCallback != null) {
                filePathCallback.onReceiveValue(WebChromeClient.FileChooserParams.parseResult(resultCode, data));
                filePathCallback = null;
            }
            return;
        }

        if (requestCode == REQUEST_SAVE_FILE) {
            if (resultCode == RESULT_OK && data != null && data.getData() != null && pendingExportContent != null) {
                try (OutputStream out = getContentResolver().openOutputStream(data.getData())) {
                    if (out != null) {
                        out.write(pendingExportContent.getBytes(StandardCharsets.UTF_8));
                        out.flush();
                        Toast.makeText(this, "Project exported", Toast.LENGTH_SHORT).show();
                    }
                } catch (Exception e) {
                    Toast.makeText(this, "Export failed", Toast.LENGTH_LONG).show();
                }
            }
            pendingExportContent = null;
            return;
        }

        if (requestCode == REQUEST_SAVE_PDF) {
            if (resultCode == RESULT_OK && data != null && data.getData() != null) {
                writePdf(data.getData());
            } else {
                restoreReportMode();
            }
        }
    }

    @Override
    public void onBackPressed() {
        if (webView != null && webView.canGoBack()) webView.goBack();
        else super.onBackPressed();
    }

    @Override
    protected void onDestroy() {
        if (webView != null) {
            webView.removeJavascriptInterface("Android");
            webView.destroy();
        }
        super.onDestroy();
    }
}
