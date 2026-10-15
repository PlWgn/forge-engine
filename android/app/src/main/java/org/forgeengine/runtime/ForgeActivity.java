package org.forgeengine.runtime;

import android.app.NativeActivity;
import android.os.Bundle;
import java.io.*;
import java.util.zip.*;
import java.nio.file.Files;
import java.nio.file.LinkOption;

/** Installs only packaged read-only content. Saves live outside this directory. */
public final class ForgeActivity extends NativeActivity {
    private static void remove(File file) throws IOException {
        if (Files.isDirectory(file.toPath(), LinkOption.NOFOLLOW_LINKS)) {
            File[] children = file.listFiles();
            if (children == null) throw new IOException("Cannot list " + file);
            for (File child : children) remove(child);
        }
        Files.deleteIfExists(file.toPath());
    }
    @Override public void onCreate(Bundle state) {
        try {
            File content = new File(getFilesDir(), "forge-content");
            File pending = new File(getFilesDir(), "forge-content-pending");
            File previous = new File(getFilesDir(), "forge-content-previous");
            // Recover an interrupted installation before staging the installed APK's content.
            if (!content.exists() && previous.exists() && !previous.renameTo(content))
                throw new IOException("Cannot recover game content");
            remove(pending);
            if (!pending.mkdirs()) throw new IOException("Cannot stage game content");
            String root = pending.getCanonicalPath() + File.separator;
            byte[] buffer = new byte[65536];
            try (ZipInputStream zip = new ZipInputStream(getAssets().open("forge-content.zip"))) {
                ZipEntry entry;
                while ((entry = zip.getNextEntry()) != null) {
                    File target = new File(pending, entry.getName());
                    if (!target.getCanonicalPath().startsWith(root)) throw new IOException("Invalid content path");
                    if (entry.isDirectory()) { if (!target.isDirectory() && !target.mkdirs()) throw new IOException("Cannot create directory"); }
                    else {
                        if (!target.getParentFile().isDirectory() && !target.getParentFile().mkdirs()) throw new IOException("Cannot create parent");
                        try (OutputStream out = new FileOutputStream(target)) {
                            int count;
                            while ((count = zip.read(buffer)) != -1) out.write(buffer, 0, count);
                        }
                    }
                    zip.closeEntry();
                }
            }
            remove(previous);
            if (content.exists() && !content.renameTo(previous)) throw new IOException("Cannot retire game content");
            if (!pending.renameTo(content)) throw new IOException("Cannot install game content");
            remove(previous);
        } catch (IOException error) { throw new IllegalStateException("Forge content installation failed", error); }
        super.onCreate(state);
    }
}
