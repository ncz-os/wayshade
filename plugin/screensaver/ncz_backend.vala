// SPDX-License-Identifier: Apache-2.0
public ScreensaverBackend create_screensaver_backend () {
    return new NczScreensaverBackend ();
}

public class NczScreensaverBackend : Object, ScreensaverBackend {
    private const string SCHEMA = "dev.ncz.screensaver";
    private const string DATA_DIR = "/usr/share/ncz-screensavers";
    private const string CATALOG = DATA_DIR + "/hacks.tsv";
    private const string[] REQUIRED_KEYS = {
        "mode", "hack-id", "random-hacks", "hack-idle-delay", "cycle-delay", "lock-enabled",
        "lock-delay", "lock-on-suspend", "display-off-delay", "gpu-offload", "pool-gpu-class",
        "show-all-hacks", "render-scale-mode", "render-scale", "max-render-height",
        "hack-options", "blackhole-color-mode", "verify-render"
    };
    private const string TIERS = DATA_DIR + "/tiers.tsv";
    private const string LAUNCHER = "ncz-screensaver";
    private const string IDLED = "/usr/libexec/ncz-screensaver-idled";
    private const int RECENT_MAX = 6;

    private Settings? settings;
    private Gee.ArrayList<ScreensaverHack> catalog = new Gee.ArrayList<ScreensaverHack> ();
    private Gee.HashSet<string> flagged_ids = new Gee.HashSet<string> ();
    private Gee.HashMap<string, string> expects = new Gee.HashMap<string, string> ();
    private string gpu_class_text = "";
    private string gpu_verdict_text = "";
    private string prime_offload_text = "";
    private string display_class_id = "";
    private bool tiers_loaded = false;
    private int gpu_count = 0;
    private Gee.HashMap<string, string> issues = new Gee.HashMap<string, string> ();
    private Gee.HashMap<string, string> tiers = new Gee.HashMap<string, string> ();
    private Gee.ArrayList<ScreensaverCategory> category_list = new Gee.ArrayList<ScreensaverCategory> ();
    private Gee.HashMap<string, string> category_by_id = new Gee.HashMap<string, string> ();
    private Gee.HashMap<string, string> origin_by_id = new Gee.HashMap<string, string> ();
    private Gee.HashMap<string, string> tags_by_id = new Gee.HashMap<string, string> ();
    private Gee.HashMap<string, Gee.ArrayList<string>> simple_sets = new Gee.HashMap<string, Gee.ArrayList<string>> ();
    private Gee.HashMap<string, Gee.ArrayList<SimpleAxis>> axes_cache = new Gee.HashMap<string, Gee.ArrayList<SimpleAxis>> ();
    // Per-user chooser state (favourites, recent, developer mode, engine filter): a key file,
    // not a settings key, because no daemon reads it.
    private KeyFile store = new KeyFile ();
    private string store_path;

    public NczScreensaverBackend () {
        store_path = Path.build_filename (Environment.get_user_config_dir (), "ncz-screensavers", "chooser.conf");
        settings = open (SCHEMA);
        if (settings != null && !has_all_keys (settings)) {
            warning ("screensaver schema %s lacks required keys; page disabled", SCHEMA);
            settings = null;
        }
        if (settings != null) {
            settings.changed.connect ((key) => {
                if (key == "gpu-offload" || key == "pool-gpu-class") {
                    load_launcher_state ();
                    flags_changed ();
                }
                changed ();
            });
            load_catalog ();
            load_tiers ();
            load_categories ();
            load_simple_sets ();
            load_store ();
            load_launcher_state ();
            load_issues ();
        }
    }

    private static Settings? open (string schema) {
        var source = SettingsSchemaSource.get_default ();
        if (source == null || source.lookup (schema, true) == null)
            return null;
        return new Settings (schema);
    }

    // A stale schema missing a key would abort the shell on first access
    private static bool has_all_keys (Settings s) {
        foreach (unowned string key in REQUIRED_KEYS)
            if (!s.settings_schema.has_key (key))
                return false;
        return true;
    }

    // A data file of the engine: the user's data dir first (a user may override or, before an
    // engine update, supply the chooser files), then the engine's share directory.
    private static string? data_file (string relative) {
        string[] candidates = {
            Path.build_filename (Environment.get_user_data_dir (), "ncz-screensavers", relative),
            Path.build_filename (DATA_DIR, relative),
        };
        foreach (unowned string c in candidates)
            if (FileUtils.test (c, FileTest.IS_REGULAR))
                return c;
        return null;
    }

    private static string[] tsv_lines (string? path) {
        string data;
        if (path == null)
            return new string[0];
        try {
            FileUtils.get_contents (path, out data);
        } catch (FileError e) {
            return new string[0];
        }
        string[] lines = new string[0];
        foreach (unowned string line in data.split ("\n")) {
            if (line.strip () == "" || line.has_prefix ("#"))
                continue;
            lines += line;
        }
        return lines;
    }

    private void load_catalog () {
        string data;
        try {
            FileUtils.get_contents (CATALOG, out data);
        } catch (FileError e) {
            return;
        }
        foreach (unowned string line in data.split ("\n")) {
            if (line == "" || line.has_prefix ("#"))
                continue;
            string[] f = line.split ("\t");
            if (f.length >= 3)
                catalog.add (new ScreensaverHack (f[0], f[1], f[2]));
        }
    }

    private void load_issues () {
        string data;
        foreach (unowned string path in new string[] { "/usr/share/ncz-screensavers/broken.tsv", "/usr/share/ncz-screensaver-chooser/broken.tsv" }) {
            try {
                FileUtils.get_contents (path, out data);
            } catch (FileError e) {
                continue;
            }
            foreach (unowned string line in data.split ("\n")) {
                string[] f = line.split ("\t");
                if (line.has_prefix ("#") || f.length < 3)
                    continue;
                if (f[1] == "broken" || f[1] == "suspect")
                    issues[f[0]] = f[2];
            }
            return;
        }
    }

    public string known_issue (string id) {
        return issues.has_key (id) ? issues[id] : "";
    }

    private void load_tiers () {
        foreach (unowned string line in tsv_lines (TIERS)) {
            string[] f = line.split ("\t");
            if (f.length >= 2 && (f[1] == "weak" || f[1] == "mid" || f[1] == "strong" || f[1] == "igpu" || f[1] == "discrete")) {
                tiers_loaded = true;
                if (!f[0].contains ("--"))
                    tiers[f[0]] = f[1] == "igpu" ? "weak" : (f[1] == "discrete" ? "strong" : f[1]);
            }
        }
    }

    private void load_categories () {
        foreach (unowned string line in tsv_lines (data_file ("category-list.tsv"))) {
            string[] f = line.split ("\t");
            if (f.length >= 3)
                category_list.add (new ScreensaverCategory (f[0], f[1], f[2]));
        }
        foreach (unowned string line in tsv_lines (data_file ("categories.tsv"))) {
            string[] f = line.split ("\t");
            if (f.length < 2)
                continue;
            category_by_id[f[0]] = f[1];
            if (f.length >= 3)
                origin_by_id[f[0]] = f[2];
            if (f.length >= 4)
                tags_by_id[f[0]] = f[3];
        }
    }

    private void load_simple_sets () {
        foreach (unowned string line in tsv_lines (data_file ("options/simple.tsv"))) {
            string[] f = line.split ("\t");
            if (f.length < 2)
                continue;
            var names = new Gee.ArrayList<string> ();
            foreach (unowned string n in f[1].split (","))
                if (n.strip () != "")
                    names.add (n.strip ());
            string id = f[0].has_suffix ("_gles3") ? f[0] : f[0] + "_gles3";
            simple_sets[id] = names;
        }
    }

    public Gee.List<ScreensaverCategory> categories () {
        return category_list.read_only_view;
    }

    public string category_of (string id) {
        return category_by_id.has_key (id) ? category_by_id[id] : "";
    }

    public string origin_of (string id) {
        return origin_by_id.has_key (id) ? origin_by_id[id] : "";
    }

    public string tags_of (string id) {
        return tags_by_id.has_key (id) ? tags_by_id[id] : "";
    }

    public string tier_of (string id) {
        return tiers.has_key (id) ? tiers[id] : "";
    }

    public string display_class {
        owned get { return display_class_id; }
    }

    public Gee.List<string> simple_options_for (string hack_id) {
        if (simple_sets.has_key (hack_id))
            return simple_sets[hack_id].read_only_view;
        return new Gee.ArrayList<string> ();
    }

    // options/<stem>-simple.tsv: axis rows (option "-") carry label, description and the end
    // words; option rows carry curve and stops. Only options present in the hack's schema are kept,
    // so a newer data file degrades gracefully on an older engine.
    public Gee.List<SimpleAxis> simple_axes_for (string hack_id) {
        if (axes_cache.has_key (hack_id))
            return axes_cache[hack_id];
        var axes = new Gee.ArrayList<SimpleAxis> ();
        axes_cache[hack_id] = axes;
        string stem = hack_id.has_suffix ("_gles3") ? hack_id.substring (0, hack_id.length - 6) : hack_id;
        var known = new Gee.HashSet<string> ();
        foreach (var o in options_for (hack_id))
            known.add (o.name);
        if (known.is_empty)
            return axes;
        SimpleAxis? current = null;
        foreach (unowned string line in tsv_lines (data_file ("options/" + stem + "-simple.tsv"))) {
            string[] f = line.split ("\t");
            if (f.length < 4)
                continue;
            if (f[1] == "-") {
                if (f.length < 8)
                    continue;
                current = new SimpleAxis (f[0], f[4], f[5], f[6], f[7]);
                axes.add (current);
                continue;
            }
            if (current == null || current.id != f[0] || !known.contains (f[1]))
                continue;
            var opt = new SimpleAxisOption (f[1], f[2] == "step" ? "step" : "linear");
            double[] positions = new double[0];
            string[] values = new string[0];
            bool ok = true;
            foreach (unowned string stop in f[3].split (",")) {
                string[] kv = stop.split ("=", 2);
                if (kv.length != 2) {
                    ok = false;
                    break;
                }
                positions += double.parse (kv[0]);
                values += kv[1].strip ();
            }
            if (!ok || positions.length < 2)
                continue;
            // keep stops sorted by position (insertion sort; the lists are tiny)
            for (int i = 1; i < positions.length; i++) {
                for (int j = i; j > 0 && positions[j - 1] > positions[j]; j--) {
                    double tp = positions[j]; positions[j] = positions[j - 1]; positions[j - 1] = tp;
                    string tv = values[j]; values[j] = values[j - 1]; values[j - 1] = tv;
                }
            }
            opt.positions = positions;
            opt.values = values;
            current.options.add (opt);
        }
        // an axis none of whose options the engine knows is useless
        var kept = new Gee.ArrayList<SimpleAxis> ();
        foreach (var a in axes)
            if (!a.options.is_empty)
                kept.add (a);
        axes.clear ();
        axes.add_all (kept);
        return axes;
    }

    // ---- per-user chooser state ------------------------------------------------------------

    private void load_store () {
        try {
            store.load_from_file (store_path, KeyFileFlags.NONE);
        } catch (Error e) {
            store = new KeyFile ();
        }
    }

    private void save_store () {
        try {
            DirUtils.create_with_parents (Path.get_dirname (store_path), 0700);
            store.save_to_file (store_path);
        } catch (Error e) {
            warning ("cannot save %s: %s", store_path, e.message);
        }
    }

    private string[] store_list (string key) {
        try {
            return store.get_string_list ("chooser", key);
        } catch (KeyFileError e) {
            return new string[0];
        }
    }

    public string[] favourites {
        owned get { return store_list ("favourites"); }
        set {
            store.set_string_list ("chooser", "favourites", value);
            save_store ();
            changed ();
        }
    }

    public string[] recent {
        owned get { return store_list ("recent"); }
    }

    public void note_recent (string id) {
        string[] kept = { id };
        foreach (unowned string r in store_list ("recent"))
            if (r != id && kept.length < RECENT_MAX)
                kept += r;
        store.set_string_list ("chooser", "recent", kept);
        save_store ();
        changed ();
    }

    public bool developer_mode {
        get {
            try {
                return store.get_boolean ("chooser", "developer");
            } catch (KeyFileError e) {
                return false;
            }
        }
        set {
            store.set_boolean ("chooser", "developer", value);
            save_store ();
        }
    }

    public string engine_filter {
        owned get {
            try {
                return store.get_string ("chooser", "engine");
            } catch (KeyFileError e) {
                return "all";
            }
        }
        set {
            store.set_string ("chooser", "engine", value);
            save_store ();
        }
    }

    // Run the launcher and parse its JSON output; null when it cannot run.
    private static Json.Node? launcher_json (string sub) {
        return launcher_json_cmd ({ LAUNCHER, sub, "--json" });
    }

    private static Json.Node? launcher_json_cmd (string[] argv) {
        try {
            var proc = new Subprocess.newv (argv, SubprocessFlags.STDOUT_PIPE | SubprocessFlags.STDERR_SILENCE);
            string? out_text = null;
            var cancel = new Cancellable ();
            uint watchdog = Timeout.add_seconds (5, () => {
                cancel.cancel ();
                return false;
            });
            try {
                proc.communicate_utf8 (null, cancel, out out_text, null);
            } finally {
                Source.remove (watchdog);
            }
            // `status` exits 3 when no screensaver is running; the JSON is valid anyway
            if (out_text == null)
                return null;
            var parser = new Json.Parser ();
            parser.load_from_data (out_text);
            return parser.get_root ();
        } catch (Error e) {
            return null;
        }
    }

    // Read flagged hacks and the display GPU class from the launcher.
    private void load_launcher_state () {
        flagged_ids.clear ();
        expects.clear ();
        string cls = "weak";
        double ms = 0;
        var status = launcher_json ("status");
        if (status != null && status.get_node_type () == Json.NodeType.OBJECT) {
            var o = status.get_object ();
            string gc = member_str (o, "gpu_class");
            if (gc != "")
                cls = gc;
            if (o.has_member ("gpu_class_score_ms")) {
                var n = o.get_member ("gpu_class_score_ms");
                if (n.get_node_type () == Json.NodeType.VALUE && !n.is_null () &&
                    (n.get_value_type () == typeof (double) || n.get_value_type () == typeof (int64)))
                    ms = n.get_double ();
            }
            // Per-GPU verdict + PRIME explanation land here on launcher >= 0.7.11.
            // On older launchers the keys are missing and the fields stay "",
            // so the getters below fall back to gpu_class_text.
            string verdict = member_str (o, "gpu_verdict");
            gpu_verdict_text = verdict;
            string prime = member_str (o, "prime_offload");
            prime_offload_text = prime;
        }
        display_class_id = cls;
        string name = cls == "" ? "Weak" : cls.substring (0, 1).up () + cls.substring (1);
        gpu_class_text = ms > 0 ? "%s (%.1f ms on the calibration test)".printf (name, ms) : name;

        var gpus = launcher_json ("gpus");
        gpu_count = (gpus != null && gpus.get_node_type () == Json.NodeType.ARRAY) ? (int) gpus.get_array ().get_length () : 0;

        var list = launcher_json ("list");
        if (list == null)
            return;
        Json.Array? items = null;
        if (list.get_node_type () == Json.NodeType.ARRAY)
            items = list.get_array ();
        else if (list.get_node_type () == Json.NodeType.OBJECT && list.get_object ().has_member ("hacks"))
            items = list.get_object ().get_array_member ("hacks");
        if (items == null)
            return;
        items.foreach_element ((arr, i, node) => {
            if (node.get_node_type () != Json.NodeType.OBJECT)
                return;
            var h = node.get_object ();
            if (!h.has_member ("id"))
                return;
            string id = member_str (h, "id");
            if (id == "")
                return;
            if (h.has_member ("flagged") && h.get_member ("flagged").get_node_type () == Json.NodeType.VALUE
                && h.get_member ("flagged").get_value_type () == typeof (bool) && h.get_boolean_member ("flagged"))
                flagged_ids.add (id);
            if (h.has_member ("expect") && !h.get_null_member ("expect"))
                expects[id] = h.get_string_member ("expect");
            string tier = member_str (h, "min_class");
            if (tier != "" && tier != "unknown" && !tiers.has_key (id))
                tiers[id] = tier;
        });
    }

    public bool is_available () {
        return settings != null
            && has_all_keys (settings)
            && !catalog.is_empty
            && Environment.find_program_in_path (LAUNCHER) != null
            && FileUtils.test (IDLED, FileTest.IS_EXECUTABLE);
    }

    public Gee.List<ScreensaverHack> hacks () {
        return catalog.read_only_view;
    }

    public string mode {
        owned get { return settings.get_string ("mode"); }
        set { settings.set_string ("mode", value); }
    }

    public string hack_id {
        owned get { return settings.get_string ("hack-id"); }
        set { settings.set_string ("hack-id", value); }
    }

    public string[] enabled_hacks {
        owned get { return settings.get_strv ("random-hacks"); }
        set { settings.set_strv ("random-hacks", value); }
    }

    // Option schema, values and scenes of one hack from the launcher, cached per hack
    private Gee.HashMap<string, Gee.List<ScreensaverOption>> option_cache = new Gee.HashMap<string, Gee.List<ScreensaverOption>> ();
    private Gee.HashMap<string, Gee.HashMap<string, string>> scene_title_to_id = new Gee.HashMap<string, Gee.HashMap<string, string>> ();
    private Gee.HashMap<string, Gee.HashMap<string, string>> scene_id_to_title = new Gee.HashMap<string, Gee.HashMap<string, string>> ();
    private const string NO_SCENE = "No scene (my own settings)";

    private static string member_str (Json.Object o, string name) {
        if (!o.has_member (name))
            return "";
        var n = o.get_member (name);
        if (n.is_null () || n.get_node_type () != Json.NodeType.VALUE || n.get_value_type () != typeof (string))
            return "";
        return n.get_string () ?? "";
    }

    private bool options_loaded = false;

    public Gee.List<ScreensaverOption> options_for (string hack_id) {
        if (!options_loaded) {
            options_loaded = true;
            var all = launcher_json_cmd ({ LAUNCHER, "options", "--all", "--json" });
            if (all != null && all.get_node_type () == Json.NodeType.OBJECT && all.get_object ().has_member ("hacks")) {
                var hacks_obj = all.get_object ().get_object_member ("hacks");
                if (hacks_obj != null) {
                    foreach (unowned string id in hacks_obj.get_members ()) {
                        var doc = hacks_obj.get_object_member (id);
                        if (doc != null)
                            parse_options (id, doc);
                    }
                }
            }
        }
        if (!option_cache.has_key (hack_id))
            option_cache[hack_id] = new Gee.ArrayList<ScreensaverOption> ();
        return option_cache[hack_id];
    }

    private void parse_options (string hack_id, Json.Object doc) {
        var list = new Gee.ArrayList<ScreensaverOption> ();
        option_cache[hack_id] = list;

        var by_title = new Gee.HashMap<string, string> ();
        var by_id = new Gee.HashMap<string, string> ();
        var titles = new Gee.ArrayList<string> ();
        titles.add (NO_SCENE);
        by_title[NO_SCENE] = "";
        by_id[""] = NO_SCENE;
        if (doc.has_member ("presets") && doc.get_member ("presets").get_node_type () == Json.NodeType.ARRAY) {
            doc.get_array_member ("presets").foreach_element ((arr, i, node) => {
                if (node.get_node_type () != Json.NodeType.OBJECT)
                    return;
                var po = node.get_object ();
                string id = member_str (po, "id");
                string title = member_str (po, "title");
                string acc = member_str (po, "accuracy");
                if (id == "")
                    return;
                if (title == "")
                    title = id;
                if (acc != "")
                    title = "%s (%s)".printf (title, acc);
                titles.add (title);
                by_title[title] = id;
                by_id[id] = title;
            });
        }
        scene_title_to_id[hack_id] = by_title;
        scene_id_to_title[hack_id] = by_id;

        if (!doc.has_member ("options") || doc.get_member ("options").get_node_type () != Json.NodeType.ARRAY)
            return;
        doc.get_array_member ("options").foreach_element ((arr, i, node) => {
            if (node.get_node_type () != Json.NodeType.OBJECT)
                return;
            var o = node.get_object ();
            string name = member_str (o, "name");
            string kind = member_str (o, "type");
            string label = member_str (o, "label");
            string dflt = member_str (o, "default");
            string[] choices = new string[0];
            if (o.has_member ("choices") && o.get_member ("choices").get_node_type () == Json.NodeType.ARRAY) {
                o.get_array_member ("choices").foreach_element ((a2, j, cn) => {
                    if (cn.get_node_type () == Json.NodeType.VALUE)
                        choices += cn.get_string ();
                });
            }
            if (name == "" || (o.has_member ("common") && o.get_member ("common").get_node_type () == Json.NodeType.VALUE && o.get_boolean_member ("common")))
                return;
            if (name == "preset") {
                if (titles.size < 2)
                    return;
                kind = "enum";
                choices = new string[0];
                foreach (var t in titles)
                    choices += t;
                dflt = NO_SCENE;
                label = "Scene";
            }
            var opt = new ScreensaverOption (name, kind, dflt, member_str (o, "min"), member_str (o, "max"), choices,
                                             label == "" ? name : label, member_str (o, "description"),
                                             member_str (o, "group") == "" ? "General" : member_str (o, "group"),
                                             member_str (o, "level") == "advanced");
            if (o.has_member ("choice_labels") && o.get_member ("choice_labels").get_node_type () == Json.NodeType.OBJECT) {
                var cl = o.get_object_member ("choice_labels");
                foreach (unowned string k in cl.get_members ())
                    opt.choice_labels[k] = member_str (cl, k);
            }
            if (o.has_member ("choice_groups") && o.get_member ("choice_groups").get_node_type () == Json.NodeType.ARRAY) {
                o.get_array_member ("choice_groups").foreach_element ((a3, j3, gn) => {
                    if (gn.get_node_type () != Json.NodeType.OBJECT)
                        return;
                    var go = gn.get_object ();
                    string gl = member_str (go, "label");
                    if (gl == "" || !go.has_member ("choices") || go.get_member ("choices").get_node_type () != Json.NodeType.ARRAY)
                        return;
                    var members = new Gee.ArrayList<string> ();
                    go.get_array_member ("choices").foreach_element ((a4, j4, cn) => {
                        if (cn.get_node_type () == Json.NodeType.VALUE && !cn.is_null ())
                            members.add (cn.get_string ());
                    });
                    if (!members.is_empty) {
                        opt.group_titles.add (gl);
                        opt.group_choices[gl] = members;
                    }
                });
            }
            list.add (opt);
        });
        return;
    }

    private Variant read_options () {
        return settings.get_value ("hack-options");
    }

    public string option_value (string hack_id, string name) {
        string v = raw_option_value (hack_id, name);
        if (name == "preset" && scene_id_to_title.has_key (hack_id))
            return scene_id_to_title[hack_id].has_key (v) ? scene_id_to_title[hack_id][v] : NO_SCENE;
        return v;
    }

    private string raw_option_value (string hack_id, string name) {
        Variant? inner = read_options ().lookup_value (hack_id, new VariantType ("a{ss}"));
        if (inner != null) {
            string? value = null;
            if (inner.lookup (name, "s", out value) && value != null)
                return value;
        }
        foreach (var option in options_for (hack_id))
            if (option.name == name)
                return option.default_value;
        return "";
    }

    public bool option_is_stored (string hack_id, string name) {
        Variant? inner = read_options ().lookup_value (hack_id, new VariantType ("a{ss}"));
        if (inner == null)
            return false;
        string? value = null;
        return inner.lookup (name, "s", out value) && value != null;
    }

    public void set_option (string hack_id, string name, string value_in) {
        var values = new Gee.HashMap<string, string> ();
        values[name] = value_in;
        set_options (hack_id, values);
    }

    public void set_options (string hack_id, Gee.Map<string, string> values_in) {
        var values = new Gee.HashMap<string, string> ();
        foreach (var e in values_in.entries) {
            string value = e.value;
            if (e.key == "preset" && scene_title_to_id.has_key (hack_id)) {
                if (!scene_title_to_id[hack_id].has_key (value))
                    continue;
                value = scene_title_to_id[hack_id][value];
            }
            values[e.key] = value;
        }
        if (values.is_empty)
            return;
        write_options (hack_id, values, new Gee.HashSet<string> ());
    }

    public void unset_options (string hack_id, Gee.Collection<string> names) {
        var drop = new Gee.HashSet<string> ();
        drop.add_all (names);
        write_options (hack_id, new Gee.HashMap<string, string> (), drop);
    }

    // Rewrite hack-options (a{sa{ss}}) with the given values set and names removed for one hack,
    // every other hack untouched, in a single settings write.
    private void write_options (string hack_id, Gee.Map<string, string> values, Gee.Set<string> drop) {
        var outer = new VariantBuilder (new VariantType ("a{sa{ss}}"));
        Variant all = read_options ();
        bool wrote = false;
        for (size_t i = 0; i < all.n_children (); i++) {
            Variant entry = all.get_child_value (i);
            string id = entry.get_child_value (0).get_string ();
            Variant inner = entry.get_child_value (1);
            var b = new VariantBuilder (new VariantType ("a{ss}"));
            var seen = new Gee.HashSet<string> ();
            int kept = 0;
            for (size_t j = 0; j < inner.n_children (); j++) {
                Variant kv = inner.get_child_value (j);
                string k = kv.get_child_value (0).get_string ();
                if (id == hack_id && drop.contains (k))
                    continue;
                if (id == hack_id && values.has_key (k)) {
                    b.add ("{ss}", k, values[k]);
                    seen.add (k);
                } else {
                    b.add ("{ss}", k, kv.get_child_value (1).get_string ());
                }
                kept++;
            }
            if (id == hack_id) {
                foreach (var e in values.entries) {
                    if (!seen.contains (e.key)) {
                        b.add ("{ss}", e.key, e.value);
                        kept++;
                    }
                }
                wrote = true;
            }
            if (kept > 0)
                outer.add ("{s@a{ss}}", id, b.end ());
            else
                b.end ();
        }
        if (!wrote && !values.is_empty) {
            var b = new VariantBuilder (new VariantType ("a{ss}"));
            foreach (var e in values.entries)
                b.add ("{ss}", e.key, e.value);
            outer.add ("{s@a{ss}}", hack_id, b.end ());
        }
        settings.set_value ("hack-options", outer.end ());
    }

    public void reset_options (string hack_id) {
        var outer = new VariantBuilder (new VariantType ("a{sa{ss}}"));
        Variant all = read_options ();
        for (size_t i = 0; i < all.n_children (); i++) {
            Variant entry = all.get_child_value (i);
            if (entry.get_child_value (0).get_string () != hack_id)
                outer.add ("{s@a{ss}}", entry.get_child_value (0).get_string (), entry.get_child_value (1));
        }
        settings.set_value ("hack-options", outer.end ());
    }

    public bool has_tiers {
        get { return tiers_loaded; }
    }

    public bool has_flags {
        get { return !flagged_ids.is_empty; }
    }

    public bool is_flagged (string id) {
        return flagged_ids.contains (id);
    }

    public string expectation (string id) {
        return expects.has_key (id) ? expects[id] : "";
    }

    public bool show_all {
        get { return settings.get_boolean ("show-all-hacks"); }
        set { settings.set_boolean ("show-all-hacks", value); }
    }

    public bool verify_render {
        get { return settings.get_boolean ("verify-render"); }
        set { settings.set_boolean ("verify-render", value); }
    }

    public void recalibrate () {
        try {
            var proc = new Subprocess (SubprocessFlags.STDOUT_SILENCE | SubprocessFlags.STDERR_SILENCE,
                                       LAUNCHER, "calibrate", "--force");
            proc.wait_async.begin (null, (obj, res) => {
                try {
                    proc.wait_async.end (res);
                } catch (Error e) {
                    warning ("screensaver calibrate failed: %s", e.message);
                }
                load_launcher_state ();
                flags_changed ();
                changed ();
            });
        } catch (Error e) {
            warning ("screensaver calibrate failed: %s", e.message);
        }
    }

    public string gpu_class_label {
        owned get { return gpu_class_text; }
    }

    public string gpu_verdict_label {
        // Per-GPU verdict ("GPU0 (Weak), GPU1 (Strong)"). Falls back to the
        // legacy single-display summary on launchers older than 0.7.11 that
        // do not expose the per-GPU verdict in `status --json`.
        owned get {
            return gpu_verdict_text != "" ? gpu_verdict_text : gpu_class_text;
        }
    }

    public string prime_offload_label {
        // Plain-English explanation of the PRIME render-offload policy.
        // Current launchers explain unsupported single-GPU layouts. Older
        // launchers may omit the field.
        owned get { return prime_offload_text; }
    }

    public string render_quality {
        owned get {
            string mode = settings.get_string ("render-scale-mode");
            double scale = settings.get_double ("render-scale");
            int height = settings.get_int ("max-render-height");
            if (mode == "auto" && scale == 1.0 && height == 0)
                return "auto";
            if (mode == "fixed" && scale == 1.0 && height == 0)
                return "high";
            if (mode == "fixed" && scale == 0.75 && height == 0)
                return "balanced";
            if (mode == "fixed" && scale == 0.5 && height == 1080)
                return "fast";
            return "custom";
        }
        set {
            switch (value) {
            case "auto": apply_render ("auto", 1.0, 0); break;
            case "high": apply_render ("fixed", 1.0, 0); break;
            case "balanced": apply_render ("fixed", 0.75, 0); break;
            case "fast": apply_render ("fixed", 0.5, 1080); break;
            default: break;
            }
        }
    }

    private void apply_render (string mode, double scale, int height) {
        settings.set_string ("render-scale-mode", mode);
        settings.set_double ("render-scale", scale);
        settings.set_int ("max-render-height", height);
    }

    public int max_render_height {
        get { return settings.get_int ("max-render-height"); }
        set { settings.set_int ("max-render-height", value); }
    }

    public int lock_auto_fps {
        get {
            string? env = Environment.get_variable ("NCZ_SAVER_AUTO_FPS");
            if (env != null && int.parse (env) > 0)
                return int.parse (env);
            string text;
            try {
                if (!FileUtils.get_contents ("/etc/ncz/lock-renderer.env", out text))
                    return 0;
            } catch (Error e) {
                return 0;
            }
            foreach (unowned string line in text.split ("\n")) {
                string l = line.strip ();
                if (l.has_prefix ("NCZ_SAVER_AUTO_FPS=")) {
                    int v = int.parse (l.substring ("NCZ_SAVER_AUTO_FPS=".length).strip ());
                    return v > 0 ? v : 0;
                }
            }
            return 0;
        }
    }

    // Guarded: reading a key the installed schema lacks aborts the shell.
    public bool lock_motion_supported {
        get {
            return settings.settings_schema.has_key ("lock-fps")
                && settings.settings_schema.has_key ("lock-render-divisor");
        }
    }

    public int lock_fps {
        get { return lock_motion_supported ? settings.get_int ("lock-fps") : 0; }
        set { if (lock_motion_supported) settings.set_int ("lock-fps", value); }
    }

    public int lock_render_divisor {
        get { return lock_motion_supported ? settings.get_int ("lock-render-divisor") : 0; }
        set { if (lock_motion_supported) settings.set_int ("lock-render-divisor", value); }
    }

    public string pool_class {
        owned get {
            string v = settings.get_string ("pool-gpu-class");
            return v == "igpu-only" ? "weak" : v;
        }
        set { settings.set_string ("pool-gpu-class", value); }
    }

    public int start_delay {
        get { return settings.get_int ("hack-idle-delay"); }
        set { settings.set_int ("hack-idle-delay", value); }
    }

    public int rotate_delay {
        get { return settings.get_int ("cycle-delay"); }
        set { settings.set_int ("cycle-delay", value); }
    }

    public bool lock_supported {
        get { return settings != null; }
    }

    public bool lock_enabled {
        get { return settings.get_boolean ("lock-enabled"); }
        set { settings.set_boolean ("lock-enabled", value); }
    }

    public int lock_delay {
        get { return settings.get_int ("lock-delay"); }
        set { settings.set_int ("lock-delay", value); }
    }

    public bool lock_on_suspend {
        get { return settings.get_boolean ("lock-on-suspend"); }
        set { settings.set_boolean ("lock-on-suspend", value); }
    }

    public int display_off_delay {
        get { return settings.get_int ("display-off-delay"); }
        set { settings.set_int ("display-off-delay", value); }
    }

    public string? color_hack {
        get { return "blackhole_gles3"; }
    }

    public string[] color_ids {
        owned get { return { "stylized", "kipthorne", "faithful", "singularity", "slingshot", "whitehole", "eht", "psychedelic", "synthwave", "cygnus" }; }
    }

    public string[] color_labels {
        owned get { return { "Stylized", "Kip Thorne (blackbody)", "Faithful (blackbody with Doppler shift)", "Singularity", "Slingshot", "White hole", "Event Horizon Telescope (orange ring)", "Psychedelic (flowing rainbow)", "Synthwave (magenta and pink)", "Cygnus (NCZ-OS magenta and cyan)" }; }
    }

    public string color_mode {
        owned get { return settings.get_string ("blackhole-color-mode"); }
        set { settings.set_string ("blackhole-color-mode", value); }
    }

    public bool gpu_offload_supported {
        get { return gpu_count >= 2; }
    }

    public string gpu_offload {
        owned get { return settings.get_string ("gpu-offload"); }
        set { settings.set_string ("gpu-offload", value); }
    }

    public void preview (string id) {
        note_recent (id);
        try {
            Process.spawn_async (null, { LAUNCHER, "preview", id, "--seconds", "30" }, null,
                                 SpawnFlags.SEARCH_PATH | SpawnFlags.STDOUT_TO_DEV_NULL | SpawnFlags.STDERR_TO_DEV_NULL,
                                 null, null);
        } catch (SpawnError e) {
            warning ("screensaver preview failed: %s", e.message);
        }
    }

    public void stop_preview () {
        try {
            Process.spawn_async (null, { LAUNCHER, "stop" }, null,
                                 SpawnFlags.SEARCH_PATH | SpawnFlags.STDOUT_TO_DEV_NULL | SpawnFlags.STDERR_TO_DEV_NULL,
                                 null, null);
        } catch (SpawnError e) {
            warning ("screensaver stop failed: %s", e.message);
        }
    }

    // `ncz-screensaver still` exists on launchers >= 0.8.3 and needs the hack's capture options.
    public bool still_supported (string hack_id) {
        bool has_capture = false, has_size = false;
        foreach (var o in options_for (hack_id)) {
            if (o.name == "capture")
                has_capture = true;
            if (o.name == "capture-size")
                has_size = true;
        }
        return has_capture && has_size && launcher_has_still ();
    }

    private int still_probe = -1;

    private bool launcher_has_still () {
        if (still_probe < 0) {
            still_probe = 0;
            try {
                var proc = new Subprocess (SubprocessFlags.STDOUT_PIPE | SubprocessFlags.STDERR_SILENCE, LAUNCHER, "--help");
                string? text = null;
                proc.communicate_utf8 (null, null, out text, null);
                if (text != null && text.contains ("still"))
                    still_probe = 1;
            } catch (Error e) {
                still_probe = 0;
            }
        }
        return still_probe == 1;
    }

    public void render_still (string hack_id, int width, int height, double at, owned ScreensaverStillCallback cb) {
        string path = Path.build_filename (Environment.get_user_runtime_dir (), "ncz-screensaver",
                                           "still-%s-%dx%d.png".printf (hack_id, width, height));
        try {
            var proc = new Subprocess (SubprocessFlags.STDOUT_SILENCE | SubprocessFlags.STDERR_PIPE,
                                       LAUNCHER, "still", hack_id, "--out", path,
                                       "--size", "%dx%d".printf (width, height), "--at", "%.1f".printf (at));
            proc.communicate_utf8_async.begin (null, null, (obj, res) => {
                string? err = null;
                string? out_text = null;
                try {
                    proc.communicate_utf8_async.end (res, out out_text, out err);
                } catch (Error e) {
                    err = e.message;
                }
                if (proc.get_if_exited () && proc.get_exit_status () == 0 && FileUtils.test (path, FileTest.IS_REGULAR))
                    cb (path, null);
                else
                    cb (null, err != null && err.strip () != "" ? err.strip () : "still render failed");
            });
        } catch (Error e) {
            cb (null, e.message);
        }
    }
}
