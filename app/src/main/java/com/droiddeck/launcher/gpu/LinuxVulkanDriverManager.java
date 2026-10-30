package com.droiddeck.launcher.gpu;

import android.content.Context;
import android.net.Uri;
import android.util.Log;

import com.droiddeck.launcher.R;
import com.droiddeck.launcher.core.FileUtils;

import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public class LinuxVulkanDriverManager {
    private static final String TAG = "LinuxVulkanDriver";
    public static final String DIR_NAME = "linux_vulkan_drivers";
    public static final String LIB_NAME = "libvulkan_freedreno.so";
    public static final String ICD_NAME = "icd.json";
    public static final String META_NAME = "meta.json";
    /**
     * A driver's GL part (Zink and Mesa's GLX/EGL from the same Mesa as the Vulkan driver): the
     * zip's gl/ folder, kept as is. Its gl.json marks it; its env file says what the session sets
     * when the driver is selected, so GL changes ship in the driver, not in the app.
     */
    public static final String GL_DIR = "gl";
    public static final String GL_MARKER = "gl.json";
    public static final String GL_ENV = "env";
    /**
     * The driver's own env file at the zip root: one NAME=value per line, read when the driver is
     * selected. It is what lets a driver carry its settings, so a knob that used to live in the
     * APK or in the user's droiddeck-env changes by shipping a driver. Read before the GL part's
     * env file, so the GL part can override a line here.
     */
    public static final String ENV_NAME = "env";
    /** Used when a zip's own ICD manifest states no api_version. */
    private static final String API_DEFAULT = "1.1.274";
    /** The zip's own ICD manifests, whose api_version the generated one carries over. */
    private static final List<String> ICD_NAMES = Collections.unmodifiableList(Arrays.asList(
            "freedreno_icd.aarch64.json", "panfrost_icd.aarch64.json", "panvk_icd.aarch64.json",
            "lvp_icd.aarch64.json", ICD_NAME));
    /**
     * Driver libraries accepted as a Linux runtime driver, by the name they ship under: Turnip
     * (Adreno), PanVK (Mali, built as libvulkan_panfrost or libvulkan_panvk) and lavapipe. The
     * library is stored as {@link #LIB_NAME} whatever it is; the ICD points at it by absolute path,
     * so the file name does not matter to the Vulkan loader.
     * Ported from SPLATPLAYS/DroidDeck (51bd995).
     */
    private static final List<String> LIB_PREFIXES = Collections.unmodifiableList(Arrays.asList(
            "libvulkan_freedreno", "libvulkan_panvk", "libvulkan_panfrost", "libvulkan_lvp", "libvulkan_virtio"));

    public static boolean isRuntimeDriverName(String name) {
        if (name == null || !name.endsWith(".so")) return false;
        for (String prefix : LIB_PREFIXES) if (name.startsWith(prefix)) return true;
        return false;
    }

    /** The api_version of an ICD manifest's JSON text, or null. */
    static String apiVersionOf(String manifestJson) {
        try {
            JSONObject icd = new JSONObject(manifestJson).optJSONObject("ICD");
            String v = icd == null ? null : icd.optString("api_version", "");
            return v == null || v.isEmpty() ? null : v;
        } catch (Exception e) {
            return null;
        }
    }

    private final Context context;
    private final File rootDir;

    public LinuxVulkanDriverManager(Context context) {
        this.context = context;
        this.rootDir = new File(context.getFilesDir(), DIR_NAME);
        if (!rootDir.exists()) rootDir.mkdirs();
    }

    public File getDriverDir(String id) {
        return new File(rootDir, id);
    }

    public boolean isInstalled(String id) {
        if (id == null || id.isEmpty() || id.contains("/") || id.contains("..")) return false;
        File dir = getDriverDir(id);
        return new File(dir, LIB_NAME).isFile() && new File(dir, ICD_NAME).isFile();
    }

    static boolean isSafeFileName(String name) {
        return name != null && !name.equals(".") && !name.equals("..") && name.matches("[A-Za-z0-9._+-]+");
    }

    /** The driver's GL part, or null when it has none. */
    public File getGlDir(String id) {
        if (!isInstalled(id)) return null;
        File gl = new File(getDriverDir(id), GL_DIR);
        return new File(gl, GL_MARKER).isFile() ? gl : null;
    }

    /** What the session adds to its environment for the driver's GL part (empty without one). */
    public List<String> glEnvironment(String id, List<String> current) {
        File gl = getGlDir(id);
        if (gl == null) return new ArrayList<>();
        File env = new File(gl, GL_ENV);
        String text = env.isFile() ? FileUtils.readString(env) : null;
        return parseGlEnv(text, gl.getAbsolutePath(), current);
    }

    /**
     * What the session adds to its environment for the driver itself, from the env file at the
     * driver's root (empty without one). Read before {@link #glEnvironment}, so a line in the GL
     * part's own env file still wins.
     */
    public List<String> driverEnvironment(String id, List<String> current) {
        if (!isInstalled(id)) return new ArrayList<>();
        File env = new File(getDriverDir(id), ENV_NAME);
        String text = env.isFile() ? FileUtils.readString(env) : null;
        return parseGlEnv(text, getDriverDir(id).getAbsolutePath(), current);
    }

    /**
     * Make the driver's own settings the final word in {@code guest}. The driver is the only part
     * that knows what it was built around, so a knob it states must not be overridable by the app,
     * the user's droiddeck-env, or the agent: any earlier entry of the same name is dropped, and
     * the driver's entries are appended. {@code /usr/bin/env} keeps the last value of a repeated
     * name, so appending alone would already win -- dropping the earlier entry keeps the
     * environment unambiguous rather than leaving two lines that disagree.
     *
     * <p>The driver's two env files are read in order: the root {@code env}, then {@code gl/env}
     * (Zink and Mesa's GLX/EGL from the same build), so a line in the GL part wins over the same
     * line in the root file. A {@code NAME+=value} line in the GL part appends to what the root
     * file produced, so both survive instead of the later one replacing the earlier.
     *
     * @return the entries added, for the session log.
     */
    public List<String> applyDriverEnvironment(String id, List<String> guest) {
        List<String> root = driverEnvironment(id, guest);
        List<String> withRoot = new ArrayList<>(guest);
        withRoot.addAll(root);
        List<String> gl = glEnvironment(id, withRoot);
        List<String> all = new ArrayList<>(root.size() + gl.size());
        all.addAll(root);
        all.addAll(gl);
        return applyEnvironment(all, guest);
    }

    static List<String> applyEnvironment(List<String> entries, List<String> guest) {
        if (entries.isEmpty()) return entries;
        for (String entry : entries) {
            int eq = entry.indexOf('=');
            if (eq <= 0) continue;
            String name = entry.substring(0, eq);
            guest.removeIf(existing -> existing != null && existing.startsWith(name + "="));
        }
        guest.addAll(entries);
        return entries;
    }

    /**
     * A GL part's env file: one KEY=VALUE per line ("#" starts a comment), KEY+=VALUE to append
     * to the variable with ":" (LD_LIBRARY_PATH), "@GL@" for the GL part's folder. Returns
     * KEY=VALUE entries to add after {@code current} (later entries win).
     */
    static List<String> parseGlEnv(String text, String glPath, List<String> current) {
        ArrayList<String> out = new ArrayList<>();
        if (text == null) return out;
        for (String raw : text.split("\n")) {
            String line = raw.trim();
            if (line.isEmpty() || line.startsWith("#")) continue;
            int eq = line.indexOf('=');
            if (eq <= 0) continue;
            boolean append = line.charAt(eq - 1) == '+';
            String key = line.substring(0, append ? eq - 1 : eq);
            if (!key.matches("[A-Za-z_][A-Za-z0-9_]*")) continue;
            String value = line.substring(eq + 1).replace("@GL@", glPath);
            if (append) {
                String before = lastValue(out, key);
                if (before == null) before = lastValue(current, key);
                if (before != null && !before.isEmpty()) value = before + ":" + value;
            }
            out.add(key + "=" + value);
        }
        return out;
    }

    private static String lastValue(List<String> env, String key) {
        if (env == null) return null;
        String prefix = key + "=";
        for (int i = env.size() - 1; i >= 0; i--) {
            String e = env.get(i);
            if (e != null && e.startsWith(prefix)) return e.substring(prefix.length());
        }
        return null;
    }

    public String getIcdPath(String id) {
        return isInstalled(id) ? new File(getDriverDir(id), ICD_NAME).getAbsolutePath() : null;
    }

    public List<String> enumerateInstalledDrivers() {
        ArrayList<String> ids = new ArrayList<>();
        File[] dirs = rootDir.listFiles();
        if (dirs == null) return ids;
        for (File d : dirs) if (d.isDirectory() && isInstalled(d.getName())) ids.add(d.getName());
        Collections.sort(ids);
        return ids;
    }

    private JSONObject readMeta(String id) {
        try {
            File meta = new File(getDriverDir(id), META_NAME);
            if (!meta.isFile()) return null;
            return new JSONObject(FileUtils.readString(meta));
        } catch (Exception e) {
            return null;
        }
    }

    public String getDriverName(String id) {
        JSONObject m = readMeta(id);
        String name = m != null ? m.optString("name", "") : "";
        return name.isEmpty() ? id : name;
    }

    public String getDriverVersion(String id) {
        JSONObject m = readMeta(id);
        return m != null ? m.optString("driverVersion", "") : "";
    }

    public String getMinGlibc(String id) {
        JSONObject m = readMeta(id);
        return m != null ? m.optString("minGlibc", "") : "";
    }

    public void removeDriver(String id) {
        if (id == null || id.isEmpty() || id.contains("/") || id.contains("..")) return;
        Log.d(TAG, "removing imported Linux Vulkan driver " + id);
        FileUtils.delete(getDriverDir(id));
    }

    public String installDriver(Uri zipUri, String displayName) throws IOException {
        File tmpDir = newStagingDir();
        boolean keep = false;
        try {
            String soName = null;
            String apiVersion = null;
            JSONObject zipMeta = null;
            try (InputStream is = context.getContentResolver().openInputStream(zipUri);
                 ZipInputStream zis = new ZipInputStream(is)) {
                if (is == null) throw new IOException("cannot open " + zipUri);
                ZipEntry entry;
                while ((entry = zis.getNextEntry()) != null) {
                    if (entry.isDirectory()) continue;
                    String base = new File(entry.getName()).getName();
                    if (base.isEmpty()) continue;
                    String path = entry.getName().replace('\\', '/');
                    if (path.startsWith(GL_DIR + "/")) {
                        // Flat folder of plain file names only.
                        if (path.equals(GL_DIR + "/" + base) && isSafeFileName(base)) {
                            File gl = new File(tmpDir, GL_DIR);
                            if (!gl.isDirectory() && !gl.mkdirs()) throw new IOException("cannot create " + gl);
                            Files.copy(zis, new File(gl, base).toPath(), StandardCopyOption.REPLACE_EXISTING);
                        } else {
                            Log.w(TAG, "skipping " + path + " in the driver's GL part");
                        }
                        continue;
                    }
                    if (isRuntimeDriverName(base)) {
                        if (soName != null) Log.w(TAG, "zip has several Vulkan driver libraries; using the first (" + soName + ")");
                        else {
                            Files.copy(zis, new File(tmpDir, LIB_NAME).toPath(), StandardCopyOption.REPLACE_EXISTING);
                            soName = base;
                        }
                    } else if (ICD_NAMES.contains(base)) {
                        // Only its api_version is kept: Turnip declares 1.1, PanVK 1.4, and a
                        // generated manifest that understates it makes the loader skip the driver.
                        String v = apiVersionOf(new String(readAll(zis), StandardCharsets.UTF_8));
                        if (v != null) apiVersion = v;
                    } else if (base.equals(META_NAME)) {
                        try {
                            zipMeta = new JSONObject(new String(readAll(zis), StandardCharsets.UTF_8));
                        } catch (Exception e) {
                            Log.w(TAG, "meta.json unreadable, ignoring: " + e.getMessage());
                        }
                    } else if (path.equals(ENV_NAME) && isSafeFileName(base)) {
                        // The driver's own environment. Kept verbatim: it is read back at session
                        // start (driverEnvironment) and is how a driver carries its settings.
                        Files.copy(zis, new File(tmpDir, ENV_NAME).toPath(), StandardCopyOption.REPLACE_EXISTING);
                    }
                }
            }
            String id = adopt(tmpDir, soName, zipMeta, displayName, apiVersion);
            keep = true;
            return id;
        } finally {
            if (!keep) FileUtils.delete(tmpDir);
        }
    }

    File newStagingDir() throws IOException {
        File tmpDir = new File(rootDir, ".tmp-" + System.nanoTime());
        FileUtils.delete(tmpDir);
        if (!tmpDir.mkdirs()) throw new IOException("cannot create " + tmpDir);
        return tmpDir;
    }

    String adopt(File tmpDir, String soName, JSONObject zipMeta, String displayName) throws IOException {
        return adopt(tmpDir, soName, zipMeta, displayName, null);
    }

    String adopt(File tmpDir, String soName, JSONObject zipMeta, String displayName, String apiVersion)
            throws IOException {
        try {
            // No manifest in the zip: PanVK is a Vulkan 1.4 driver, and an understated version
            // makes the loader pass it over.
            if (apiVersion == null && soName != null && soName.startsWith("libvulkan_pan")) apiVersion = "1.4.303";
            if (soName == null) {
                throw new IllegalArgumentException(context.getString(R.string.lvd_no_library));
            }
            File so = new File(tmpDir, LIB_NAME);
            if (!isAarch64Elf(so)) {
                throw new IllegalArgumentException(context.getString(R.string.lvd_not_aarch64, soName));
            }
            if (!containsAscii(so, "libc.so.6")) {
                throw new IllegalArgumentException(context.getString(R.string.lvd_not_glibc, soName));
            }
            String kind = zipMeta != null ? zipMeta.optString("kind", "") : "";
            if (!kind.isEmpty() && !"linux-vulkan-icd".equals(kind)) {
                Log.w(TAG, "meta.json says kind=" + kind + ", importing anyway (the binary is glibc)");
            }

            String name = zipMeta != null ? zipMeta.optString("name", "") : "";
            String version = zipMeta != null ? zipMeta.optString("driverVersion", "") : "";
            String minGlibc = zipMeta != null ? zipMeta.optString("minGlibc", "") : "";
            if (name.isEmpty()) {
                name = displayName != null ? displayName : "linux-driver";
                if (name.toLowerCase().endsWith(".zip")) name = name.substring(0, name.length() - 4);
            }
            String id = uniqueId(sanitizeId(name));
            File dir = getDriverDir(id);

            JSONObject icd = new JSONObject();
            icd.put("file_format_version", "1.0.0");
            JSONObject icdBody = new JSONObject();
            icdBody.put("library_path", new File(dir, LIB_NAME).getAbsolutePath());
            icdBody.put("api_version", apiVersion != null ? apiVersion : API_DEFAULT);
            icd.put("ICD", icdBody);
            if (!FileUtils.writeString(new File(tmpDir, ICD_NAME), icd.toString(2))) throw new IOException("cannot write icd.json");

            JSONObject meta = new JSONObject();
            meta.put("schemaVersion", 1);
            meta.put("kind", "linux-vulkan-icd");
            meta.put("name", name);
            meta.put("driverVersion", version);
            meta.put("libc", "glibc");
            meta.put("minGlibc", minGlibc);
            meta.put("sourceLibraryName", soName);
            String backend = zipMeta != null ? zipMeta.optString("backend", "") : "";
            if (!backend.isEmpty()) meta.put("backend", backend);
            meta.put("apiVersion", apiVersion != null ? apiVersion : API_DEFAULT);
            meta.put("importedAt", System.currentTimeMillis());
            if (!FileUtils.writeString(new File(tmpDir, META_NAME), meta.toString(2))) throw new IOException("cannot write meta.json");

            if (!tmpDir.renameTo(dir)) throw new IOException("cannot move into " + dir);
            Log.i(TAG, "imported Linux Vulkan driver " + id + " (" + soName + ", minGlibc=" + minGlibc + ") -> " + dir);
            return id;
        } catch (org.json.JSONException e) {
            throw new IOException("manifest write failed: " + e.getMessage());
        }
    }

    private String uniqueId(String base) {
        String id = base;
        int n = 2;
        while (getDriverDir(id).exists()) id = base + "-" + (n++);
        return id;
    }

    static String sanitizeId(String name) {
        String s = name.trim().replaceAll("[^A-Za-z0-9._-]+", "_").replaceAll("^[._]+", "");
        if (s.length() > 64) s = s.substring(0, 64);
        return s.isEmpty() ? "linux-driver" : s;
    }

    static boolean isAarch64Elf(File f) {
        try (FileInputStream in = new FileInputStream(f)) {
            byte[] h = new byte[20];
            int n = 0;
            while (n < h.length) {
                int r = in.read(h, n, h.length - n);
                if (r < 0) break;
                n += r;
            }
            if (n < 20) return false;
            if (h[0] != 0x7f || h[1] != 'E' || h[2] != 'L' || h[3] != 'F') return false;
            if (h[4] != 2 || h[5] != 1) return false;
            int machine = (h[18] & 0xff) | ((h[19] & 0xff) << 8);
            return machine == 0xB7;
        } catch (IOException e) {
            return false;
        }
    }

    private static byte[] readAll(InputStream in) throws IOException {
        java.io.ByteArrayOutputStream bos = new java.io.ByteArrayOutputStream();
        byte[] buf = new byte[8192];
        int r;
        while ((r = in.read(buf)) > 0) bos.write(buf, 0, r);
        return bos.toByteArray();
    }

    static boolean containsAscii(File f, String needle) {
        byte[] nb = needle.getBytes(StandardCharsets.US_ASCII);
        try (InputStream in = new FileInputStream(f)) {
            byte[] buf = new byte[1 << 16];
            int carry = 0;
            int r;
            while ((r = in.read(buf, carry, buf.length - carry)) > 0) {
                int len = carry + r;
                for (int i = 0; i + nb.length <= len; i++) {
                    int j = 0;
                    while (j < nb.length && buf[i + j] == nb[j]) j++;
                    if (j == nb.length) return true;
                }
                carry = Math.min(nb.length - 1, len);
                System.arraycopy(buf, len - carry, buf, 0, carry);
            }
        } catch (IOException e) {
            return false;
        }
        return false;
    }
}
