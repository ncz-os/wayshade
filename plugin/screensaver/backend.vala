// SPDX-License-Identifier: Apache-2.0
public class ScreensaverHack : Object {
    public string id { get; construct; }
    public string title { get; construct; }
    public string group_name { get; construct; }

    public ScreensaverHack (string id, string title, string group_name) {
        Object (id: id, title: title, group_name: group_name);
    }
}

// A style category of the chooser (category-list.tsv): id, label, one-line description.
public class ScreensaverCategory : Object {
    public string id { get; construct; }
    public string label { get; construct; }
    public string description { get; construct; }

    public ScreensaverCategory (string id, string label, string description) {
        Object (id: id, label: label, description: description);
    }
}

public class ScreensaverOption : Object {
    public string name { get; construct; }
    public string kind { get; construct; }
    public string default_value { get; construct; }
    public string min_value { get; construct; }
    public string max_value { get; construct; }
    public string[] choices { owned get; construct; }
    public string label { get; construct; }
    public string description { get; construct; }
    public string group_name { get; construct; }
    public bool advanced { get; construct; }
    public Gee.ArrayList<string> group_titles = new Gee.ArrayList<string> ();
    public Gee.HashMap<string, Gee.ArrayList<string>> group_choices = new Gee.HashMap<string, Gee.ArrayList<string>> ();
    public Gee.HashMap<string, string> choice_labels = new Gee.HashMap<string, string> ();

    public string display (string value) {
        return choice_labels.has_key (value) ? choice_labels[value] : value;
    }

    public ScreensaverOption (string name, string kind, string default_value, string min_value, string max_value,
                              string[] choices, string label, string description, string group_name,
                              bool advanced = false) {
        Object (name: name, kind: kind, default_value: default_value, min_value: min_value, max_value: max_value,
                choices: choices, label: label, description: description, group_name: group_name,
                advanced: advanced);
    }
}

// One option moved by a simple-mode slider: a curve through "position=value" stops
// (options/<hack>-simple.tsv). linear interpolates numbers between stops; step takes the
// value of the last stop at or below the slider.
public class SimpleAxisOption : Object {
    public string name { get; construct; }
    public string curve { get; construct; }
    public double[] positions;
    public string[] values;

    public SimpleAxisOption (string name, string curve) {
        Object (name: name, curve: curve);
    }

    public string value_at (double position) {
        double p = position.clamp (0, 100);
        if (curve == "step") {
            string v = values[0];
            for (int i = 0; i < positions.length; i++)
                if (positions[i] <= p)
                    v = values[i];
            return v;
        }
        for (int i = 1; i < positions.length; i++) {
            if (p <= positions[i]) {
                double span = positions[i] - positions[i - 1];
                double a = double.parse (values[i - 1]);
                double b = double.parse (values[i]);
                double v = span <= 0 ? b : a + (p - positions[i - 1]) / span * (b - a);
                return format_number (v);
            }
        }
        return values[values.length - 1];
    }

    // Slider position for a stored value (inverse of value_at, clamped); 50 when it cannot be read.
    public double position_of (string value) {
        if (curve == "step") {
            for (int i = 0; i < values.length; i++)
                if (values[i] == value)
                    return positions[i];
            return 50;
        }
        double x = double.parse (value);
        double best = 50, best_err = -1;
        for (int i = 1; i < positions.length; i++) {
            double a = double.parse (values[i - 1]);
            double b = double.parse (values[i]);
            double cand;
            if (a == b) {
                if (x != a)
                    continue;
                cand = positions[i - 1];
            } else {
                double t = ((x - a) / (b - a)).clamp (0, 1);
                cand = positions[i - 1] + t * (positions[i] - positions[i - 1]);
            }
            double err = (double.parse (value_at (cand)) - x).abs ();
            if (best_err < 0 || err < best_err) {
                best = cand;
                best_err = err;
            }
        }
        return best;
    }

    public static string format_number (double v) {
        string s = "%.4f".printf (v);
        while (s.has_suffix ("0"))
            s = s.substring (0, s.length - 1);
        if (s.has_suffix ("."))
            s = s.substring (0, s.length - 1);
        return s == "" || s == "-0" ? "0" : s;
    }
}

public class SimpleAxis : Object {
    public string id { get; construct; }
    public string label { get; construct; }
    public string description { get; construct; }
    public string low_word { get; construct; }
    public string high_word { get; construct; }
    public Gee.ArrayList<SimpleAxisOption> options = new Gee.ArrayList<SimpleAxisOption> ();

    public SimpleAxis (string id, string label, string description, string low_word, string high_word) {
        Object (id: id, label: label, description: description, low_word: low_word, high_word: high_word);
    }
}

public delegate void ScreensaverStillCallback (string? path, string? error);

public interface ScreensaverBackend : Object {
    public abstract Gee.List<ScreensaverOption> options_for (string hack_id);
    public abstract string option_value (string hack_id, string name);
    public abstract void set_option (string hack_id, string name, string value);
    // Several options of one hack in a single settings write (a slider moves many).
    public abstract void set_options (string hack_id, Gee.Map<string, string> values);
    // Forget stored values so the hack's own defaults apply again.
    public abstract void unset_options (string hack_id, Gee.Collection<string> names);
    // True when the user stored an explicit value (a pinned option the randomize roll keeps).
    public abstract bool option_is_stored (string hack_id, string name);
    public abstract void reset_options (string hack_id);
    public abstract bool is_available ();
    public abstract Gee.List<ScreensaverHack> hacks ();
    // Style categories in display order (category-list.tsv); empty when the data is missing.
    public abstract Gee.List<ScreensaverCategory> categories ();
    // Category id of a hack, "" when it has none (categories.tsv).
    public abstract string category_of (string id);
    // "ncz" for in-house savers, "ported" for adapted ones, "" when unknown.
    public abstract string origin_of (string id);
    // Comma separated search tags, "" when none.
    public abstract string tags_of (string id);
    // Minimum GPU class (weak, mid, strong) from tiers.tsv, "" when unknown.
    public abstract string tier_of (string id);
    // Class of the GPU that renders the display (weak, mid, strong), "" when unknown.
    public abstract string display_class { owned get; }
    // Options a simple view shows (options/simple.tsv); empty means "use the basic level".
    public abstract Gee.List<string> simple_options_for (string hack_id);
    // Slider axes of a simple mode (options/<hack>-simple.tsv); empty when the hack has none.
    public abstract Gee.List<SimpleAxis> simple_axes_for (string hack_id);
    // Starred hacks and the most recently used ones (per user, newest first).
    public abstract string[] favourites { owned get; set; }
    public abstract string[] recent { owned get; }
    public abstract void note_recent (string id);
    // Show developer-only option groups (Capture).
    public abstract bool developer_mode { get; set; }
    // Remembered engine filter of the chooser.
    public abstract string engine_filter { owned get; set; }
    // True when `ncz-screensaver still` can render a frame of this hack.
    public abstract bool still_supported (string hack_id);
    public abstract void render_still (string hack_id, int width, int height, double at, owned ScreensaverStillCallback cb);
    public abstract string mode { owned get; set; }
    public abstract string hack_id { owned get; set; }
    public abstract string[] enabled_hacks { owned get; set; }
    public abstract bool has_tiers { get; }
    // True when some hack is expected to run poorly on this graphics chip.
    public abstract bool has_flags { get; }
    public abstract bool is_flagged (string id);
    // Short expectation such as "about 12 fps on Intel UHD 630"; empty if unknown.
    public abstract string expectation (string id);
    public abstract bool show_all { get; set; }
    // Human-readable class of the display GPU, e.g. "Weak (46.5 ms on the calibration test)".
    // Kept for the legacy single-display-GPU summary; new code should prefer gpu_verdict_label.
    public abstract string gpu_class_label { owned get; }
    // Per-GPU verdict, e.g. "GPU0 (Weak), GPU1 (Strong)" — UX requirement,
    // operator 2026-09-30. Falls back to gpu_class_label when the launcher
    // does not expose the per-GPU verdict (older than 0.7.11).
    public abstract string gpu_verdict_label { owned get; }
    // Plain-English description of the PRIME render-offload policy, including
    // unsupported layouts. Empty only when an older launcher omits it.
    public abstract string prime_offload_label { owned get; }
    public abstract bool verify_render { get; set; }
    public abstract void recalibrate ();
    // Empty when the hack has no known defect, else a short reason.
    public abstract string known_issue (string id);
    public abstract string pool_class { owned get; set; }
    public abstract string render_quality { owned get; set; }
    public abstract int max_render_height { get; set; }
    // Frame rate and render divisor of the saver shown behind the lock
    // (dev.ncz.screensaver lock-fps / lock-render-divisor, 0 = Auto). False when
    // the installed engine predates those keys; the rows are then hidden.
    public abstract bool lock_motion_supported { get; }
    public abstract int lock_fps { get; set; }
    public abstract int lock_render_divisor { get; set; }
    // What Auto means on this computer when the image lowers it for hardware
    // that runs hot (NCZ_SAVER_AUTO_FPS); 0 = the normal Auto.
    public abstract int lock_auto_fps { get; }
    public abstract int start_delay { get; set; }
    public abstract int rotate_delay { get; set; }
    public abstract bool lock_supported { get; }
    public abstract bool lock_enabled { get; set; }
    public abstract int lock_delay { get; set; }
    public abstract bool lock_on_suspend { get; set; }
    public abstract int display_off_delay { get; set; }
    public abstract string? color_hack { get; }
    public abstract string[] color_ids { owned get; }
    public abstract string[] color_labels { owned get; }
    public abstract string color_mode { owned get; set; }
    public abstract bool gpu_offload_supported { get; }
    public abstract string gpu_offload { owned get; set; }
    public abstract void preview (string id);
    public abstract void stop_preview ();
    public signal void changed ();
    // The set of flagged screensavers changed (GPU offload or pool setting)
    public signal void flags_changed ();
}
