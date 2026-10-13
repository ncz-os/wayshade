// SPDX-License-Identifier: Apache-2.0
using Gtk;
using Singularity;
using Singularity.Widgets;
using Peas;

[ModuleInit]
public void peas_register_types (TypeModule module) {
    var objmodule = module as Peas.ObjectModule;
    objmodule.register_extension_type (typeof (Singularity.Plugin), typeof (ScreensaverPlugin));
}

public class ScreensaverPlugin : Object, Singularity.Plugin {
    private ScreensaverBackend backend = create_screensaver_backend ();

    public void activate (PluginContext context) {
    }

    public void deactivate () {
    }

    public Gtk.Widget? get_settings_widget () {
        if (!backend.is_available ())
            return new ScreensaverUnavailable ();
        return new ScreensaverSettings (backend);
    }
}

public class OptionBinding : Object {
    private string hack_id;
    private ScreensaverOption option;
    private Gtk.Widget row;

    public OptionBinding (string hack_id, ScreensaverOption option, Gtk.Widget row) {
        this.hack_id = hack_id;
        this.option = option;
        this.row = row;
    }

    public void refresh (ScreensaverBackend backend) {
        string value = backend.option_value (hack_id, option.name);
        if (row is SwitchRow)
            ((SwitchRow) row).switch_btn.active = value == "true";
        else if (row is SelectionRow)
            ((SelectionRow) row).current_value = value;
        else if (row is SpinRow)
            ((SpinRow) row).spin_btn.value = double.parse (value);
        else if (row is EntryRow)
            ((EntryRow) row).text = value;
        else if (row is ExpanderRow)
            ((ExpanderRow) row).subtitle = option.display (value);
    }
}

public class ScreensaverUnavailable : Gtk.Box {
    public ScreensaverUnavailable () {
        Object (orientation: Orientation.VERTICAL, spacing: 0);
        var page = new StatusPage ();
        page.title = "Screensaver is not installed";
        page.description = "Install the screensaver package to configure it here.";
        page.icon_name = "preferences-desktop-screensaver-symbolic";
        append (page);
    }
}

// Small style additions on top of the Singularity theme: tier badges, palette swatches,
// preset pills and the still preview. Loaded once per display.
namespace NczChooserStyle {
    private static bool loaded = false;
    private const string CSS = """
        .ncz-badge { padding: 0 7px; border-radius: 9px; font-size: 0.78em; background: alpha(currentColor, 0.12); }
        .ncz-badge-mid { background: alpha(#e0a040, 0.28); }
        .ncz-badge-strong { background: alpha(#e06060, 0.28); }
        .ncz-swatch { min-width: 30px; min-height: 30px; padding: 2px; border-radius: 16px; }
        .ncz-pill { padding: 2px 10px; border-radius: 14px; font-size: 0.9em; }
        .ncz-still { border-radius: 10px; }
        .ncz-row-box { padding: 8px 12px; }
        .ncz-axis-word { font-size: 0.78em; opacity: 0.65; }
        .ncz-axis-warning { font-size: 0.8em; color: #e0a040; }
        .ncz-options { padding: 0 0 6px 12px; }
        .ncz-open { color: @accent_color; }
        /* The Settings page already draws one card around this widget: inner groups are
           plain sections, not cards in cards. */
        .ncz-flat .preferences-group.preferences-group { background: none; border: none; border-radius: 0; box-shadow: none; }
        .ncz-flat .preferences-group .group-header { padding-left: 0; padding-right: 0; }
    """;

    public static void ensure () {
        if (loaded)
            return;
        loaded = true;
        var display = Gdk.Display.get_default ();
        if (display == null)
            return;
        var css = new Gtk.CssProvider ();
        css.load_from_string (CSS);
        // USER priority, as the Singularity theme itself loads, so these rules can refine it
        Gtk.StyleContext.add_provider_for_display (display, css, Gtk.STYLE_PROVIDER_PRIORITY_USER);
    }
}

// A caption that wraps and never asks for more than the sidebar can give: a label's minimum
// width is its longest word, but its natural width is the whole line unless capped, and a
// row wider than the configured layer surface is a Wayland protocol error under fractional
// scaling (seen on the Mali board at scale 1.75).
public static Gtk.Label ncz_caption (string text, int max_chars = 34) {
    var l = new Gtk.Label (text);
    l.xalign = 0;
    l.wrap = true;
    l.wrap_mode = Pango.WrapMode.WORD_CHAR;
    l.max_width_chars = max_chars;
    l.hexpand = true;
    l.add_css_class ("caption");
    l.add_css_class ("dim-label");
    return l;
}

// One placement of a hack in the chooser (a hack appears again under Favourites and Recently used).
public class HackRow : Object {
    public ScreensaverHack hack;
    public ActionRow row;
    public CheckButton pick;
    public ToggleButton star;
    public Button gear;
    public Button preview;
    public Switch pool;
    public Label badge;
    public string section;
    public Gtk.Revealer options; // the hack's options, directly under its row
    public bool shown = true;
}

public class CategorySection : Object {
    public string id;
    public string label;
    public ExpanderRow expander;
    public Label count;
    public bool pinned; // Favourites and Recently used: expanded by default
    public Gee.ArrayList<HackRow> rows = new Gee.ArrayList<HackRow> ();
}

// A simple-mode slider: title, reset, 0..100 scale with the centre mark, end words, warning.
public class SliderRow : Gtk.ListBoxRow {
    public SimpleAxis axis;
    public Scale scale;
    public Button reset;
    private Label warning_label;
    public signal void moved (double position);
    public signal void reset_requested ();

    public SliderRow (SimpleAxis axis) {
        this.axis = axis;
        activatable = false;
        selectable = false;
        var box = new Box (Orientation.VERTICAL, 2);
        box.add_css_class ("ncz-row-box");
        var head = new Box (Orientation.HORIZONTAL, 6);
        var title = new Label (axis.label);
        title.xalign = 0;
        title.hexpand = true;
        title.ellipsize = Pango.EllipsizeMode.END;
        title.add_css_class ("heading");
        head.append (title);
        reset = new Button.from_icon_name ("edit-undo-symbolic");
        reset.add_css_class ("flat");
        reset.valign = Align.CENTER;
        reset.tooltip_text = "Back to the shipped look";
        reset.update_property (Gtk.AccessibleProperty.LABEL, "Reset %s".printf (axis.label), -1);
        reset.clicked.connect (() => reset_requested ());
        head.append (reset);
        box.append (head);
        box.append (ncz_caption (axis.description));
        scale = new Scale.with_range (Orientation.HORIZONTAL, 0, 100, 1);
        scale.draw_value = false;
        scale.hexpand = true;
        scale.add_mark (50, PositionType.BOTTOM, null);
        scale.update_property (Gtk.AccessibleProperty.LABEL, axis.label, -1);
        scale.update_property (Gtk.AccessibleProperty.DESCRIPTION, "%s: %s to %s, centre is the shipped look".printf (axis.label, axis.low_word, axis.high_word), -1);
        scale.value_changed.connect (() => moved (scale.get_value ()));
        box.append (scale);
        var words = new Box (Orientation.HORIZONTAL, 0);
        var low = new Label (axis.low_word);
        low.xalign = 0;
        low.hexpand = true;
        low.add_css_class ("ncz-axis-word");
        var high = new Label (axis.high_word);
        high.xalign = 1;
        high.add_css_class ("ncz-axis-word");
        words.append (low);
        words.append (high);
        box.append (words);
        warning_label = ncz_caption ("");
        warning_label.remove_css_class ("dim-label");
        warning_label.add_css_class ("ncz-axis-warning");
        warning_label.visible = false;
        box.append (warning_label);
        child = box;
    }

    public void set_warning (string? text) {
        warning_label.label = text ?? "";
        warning_label.visible = text != null && text != "";
    }
}

// Palette swatches: one toggle per palette, drawn as a small gradient disc.
public class SwatchRow : Gtk.ListBoxRow {
    private Gee.HashMap<string, ToggleButton> buttons = new Gee.HashMap<string, ToggleButton> ();
    private Gee.HashMap<string, DrawingArea> areas = new Gee.HashMap<string, DrawingArea> ();
    private Label current_label;
    private bool updating = false;
    public signal void chosen (string id);

    private const double PI = 3.14159265358979;

    // Representative colours per palette (docs/PALETTES.md): swatches are hints, the render decides.
    private static string[] colours_for (string id) {
        switch (id) {
        case "stylized": return { "#ff6a00", "#ffd36b", "#7aa7ff" };
        case "kipthorne": return { "#8a5a10", "#e0a83a", "#fff2d8" };
        case "faithful": return { "#ff8c42", "#ffe8c0", "#9fc3ff" };
        case "singularity": return { "#000000", "#989898", "#ffffff" };
        case "slingshot": return { "#5d2014", "#ffb347", "#cfe3ff" };
        case "whitehole": return { "#10334a", "#4fd0c8", "#eaf8ff" };
        case "eht": return { "#3a0600", "#d94a00", "#ffd060" };
        case "psychedelic": return { "#ff2d95", "#2dff6a", "#2d9bff" };
        case "synthwave": return { "#2b1b6b", "#ff2bd6", "#7df9ff" };
        case "cygnus": return { "#14061f", "#ff2bd6", "#35e0ff" };
        default: return { "#404040", "#808080", "#c0c0c0" };
        }
    }

    public SwatchRow (string title, string subtitle, string[] ids, string[] labels) {
        activatable = false;
        selectable = false;
        var box = new Box (Orientation.VERTICAL, 4);
        box.add_css_class ("ncz-row-box");
        var head = new Box (Orientation.HORIZONTAL, 6);
        var t = new Label (title);
        t.xalign = 0;
        t.add_css_class ("heading");
        head.append (t);
        current_label = new Label ("");
        current_label.add_css_class ("dim-label");
        current_label.xalign = 1;
        current_label.hexpand = true;
        current_label.ellipsize = Pango.EllipsizeMode.END;
        current_label.max_width_chars = 22;
        head.append (current_label);
        box.append (head);
        box.append (ncz_caption (subtitle));
        var flow = new FlowBox ();
        flow.selection_mode = SelectionMode.NONE;
        flow.min_children_per_line = 1; // wraps to the width it is given
        flow.max_children_per_line = 5;
        flow.row_spacing = 6;
        flow.column_spacing = 6;
        flow.halign = Align.START;
        for (int i = 0; i < ids.length; i++) {
            string id = ids[i];
            string label = i < labels.length ? labels[i] : id;
            var btn = new ToggleButton ();
            btn.add_css_class ("flat");
            btn.add_css_class ("ncz-swatch");
            btn.tooltip_text = label;
            btn.update_property (Gtk.AccessibleProperty.LABEL, label, -1);
            var area = new DrawingArea ();
            area.content_width = 26;
            area.content_height = 26;
            string[] cols = colours_for (id);
            area.set_draw_func ((da, cr, w, h) => {
                double r = double.min (w, h) / 2.0 - 1;
                double cx = w / 2.0, cy = h / 2.0;
                var pat = new Cairo.Pattern.linear (cx - r, cy - r, cx + r, cy + r);
                for (int k = 0; k < cols.length; k++) {
                    Gdk.RGBA c = {};
                    if (c.parse (cols[k]))
                        pat.add_color_stop_rgb ((double) k / (cols.length - 1), c.red, c.green, c.blue);
                }
                cr.arc (cx, cy, r, 0, 2 * PI);
                cr.set_source (pat);
                cr.fill_preserve ();
                cr.set_line_width (btn.active ? 2.5 : 1);
                cr.set_source_rgba (1, 1, 1, btn.active ? 0.95 : 0.25);
                cr.stroke ();
                if (btn.active) {
                    cr.arc (cx, cy, r - 4, 0, 2 * PI);
                    cr.set_line_width (1.5);
                    cr.set_source_rgba (0, 0, 0, 0.6);
                    cr.stroke ();
                }
            });
            btn.child = area;
            btn.toggled.connect (() => {
                area.queue_draw ();
                if (updating)
                    return;
                if (btn.active)
                    chosen (id);
                else
                    set_current (id); // cannot un-choose: keep it on
            });
            buttons[id] = btn;
            areas[id] = area;
            flow.insert (btn, -1);
            var fc = btn.parent as FlowBoxChild;
            if (fc != null)
                fc.focusable = false;
        }
        box.append (flow);
        child = box;
    }

    public void set_current (string id) {
        updating = true;
        foreach (var e in buttons.entries) {
            e.value.active = e.key == id;
            areas[e.key].queue_draw ();
            if (e.key == id)
                current_label.label = e.value.tooltip_text ?? id;
        }
        updating = false;
    }
}

// Preset pills: one toggle per scene preset, "None" first.
public class PillRow : Gtk.ListBoxRow {
    private Gee.ArrayList<ToggleButton> pills = new Gee.ArrayList<ToggleButton> ();
    private Gee.ArrayList<string> values = new Gee.ArrayList<string> ();
    private bool updating = false;
    public signal void chosen (string value);

    public PillRow (string title, string subtitle, string[] ids, string[] labels, string[] tooltips) {
        activatable = false;
        selectable = false;
        var box = new Box (Orientation.VERTICAL, 4);
        box.add_css_class ("ncz-row-box");
        var t = new Label (title);
        t.xalign = 0;
        t.add_css_class ("heading");
        box.append (t);
        box.append (ncz_caption (subtitle));
        var flow = new FlowBox ();
        flow.selection_mode = SelectionMode.NONE;
        flow.min_children_per_line = 1;
        flow.max_children_per_line = 3;
        flow.row_spacing = 6;
        flow.column_spacing = 6;
        flow.halign = Align.START;
        for (int i = 0; i < ids.length; i++) {
            string id = ids[i];
            var btn = new ToggleButton ();
            var pl = new Label (i < labels.length ? labels[i] : id);
            pl.ellipsize = Pango.EllipsizeMode.END;
            pl.max_width_chars = 20;
            btn.child = pl;
            btn.add_css_class ("ncz-pill");
            if (i < tooltips.length && tooltips[i] != "")
                btn.tooltip_text = tooltips[i];
            btn.toggled.connect (() => {
                if (updating)
                    return;
                if (btn.active)
                    chosen (id);
                else
                    set_current (id);
            });
            pills.add (btn);
            values.add (id);
            flow.insert (btn, -1);
            var fc = btn.parent as FlowBoxChild;
            if (fc != null)
                fc.focusable = false;
        }
        box.append (flow);
        child = box;
    }

    public void set_current (string value) {
        updating = true;
        for (int i = 0; i < pills.size; i++)
            pills[i].active = values[i] == value;
        updating = false;
    }
}

// A still frame of the hack with the current options, re-rendered after changes.
public class StillRow : Gtk.ListBoxRow {
    public Picture picture;
    private Label status;
    private Label caption;

    public StillRow () {
        activatable = false;
        selectable = false;
        var box = new Box (Orientation.VERTICAL, 4);
        box.add_css_class ("ncz-row-box");
        var overlay = new Overlay ();
        picture = new Picture ();
        picture.content_fit = ContentFit.COVER;
        picture.can_shrink = true;
        picture.hexpand = true;
        picture.add_css_class ("ncz-still");
        picture.update_property (Gtk.AccessibleProperty.LABEL, "Preview frame rendered with the current settings", -1);
        // The texture is 480 px wide; without this the row would ask for 480 px and overflow
        // the sidebar. An external-policy scroller reports no natural width of its own.
        var clamp = new ScrolledWindow ();
        clamp.hscrollbar_policy = PolicyType.EXTERNAL;
        clamp.vscrollbar_policy = PolicyType.NEVER;
        clamp.propagate_natural_width = false;
        clamp.propagate_natural_height = false;
        clamp.height_request = 190;
        clamp.hexpand = true;
        clamp.child = picture;
        overlay.child = clamp;
        status = new Label ("Rendering a preview frame…");
        status.ellipsize = Pango.EllipsizeMode.END;
        status.max_width_chars = 30;
        status.add_css_class ("osd");
        status.add_css_class ("caption");
        status.halign = Align.END;
        status.valign = Align.START;
        status.margin_top = 8;
        status.margin_end = 8;
        overlay.add_overlay (status);
        box.append (overlay);
        caption = ncz_caption ("A single frame rendered with the settings below; the real screensaver moves.");
        box.append (caption);
        child = box;
    }

    public void set_busy (bool busy) {
        status.label = "Rendering a preview frame…";
        status.visible = busy;
    }

    public void set_error (string text) {
        status.label = text;
        status.visible = true;
    }

    public void show_file (string path) {
        try {
            picture.paintable = Gdk.Texture.from_file (File.new_for_path (path));
            status.visible = false;
        } catch (Error e) {
            set_error ("Cannot show the preview: " + e.message);
        }
    }
}

// The settings of one hack: simple mode (sliders, swatches, scene, presets) when the engine ships
// a simple-mode file, else the simple option set, and everything else under Advanced.
public class HackOptionsPanel : Gtk.Box {
    private const string[] SHOWN_FIRST = { "palette", "scene", "preset" };
    private ScreensaverBackend backend;
    private ScreensaverHack hack;
    private bool refreshing = false;
    private Gee.ArrayList<OptionBinding> bindings = new Gee.ArrayList<OptionBinding> ();
    private Gee.ArrayList<SliderRow> sliders = new Gee.ArrayList<SliderRow> ();
    private Gee.HashMap<string, double?> pending = new Gee.HashMap<string, double?> ();
    private uint apply_source = 0;
    private uint still_source = 0;
    private bool still_busy = false;
    private bool still_again = false;
    private StillRow? still = null;
    private SwatchRow? swatches = null;
    private PillRow? pills = null;
    private SelectionRow? scene_row = null;
    private ScreensaverOption? palette_opt = null;
    private ScreensaverOption? scene_opt = null;
    private ScreensaverOption? preset_opt = null;
    public PreferencesGroup group;
    // True when at least one row is shown outside the Advanced expander, so the panel
    // is not just a collapsed Advanced plus the footer in non-developer mode.
    private bool basic_content = false;
    public bool has_basic_content () {
        return basic_content;
    }
    public signal void preview_requested (string id);
    // The header's "Hide" button was pressed.
    public signal void close_requested ();
    // Emitted after the Developer switch repopulated the Advanced expander (informational).
    public signal void developer_toggled ();

    public HackOptionsPanel (ScreensaverBackend backend, ScreensaverHack hack) {
        Object (orientation: Orientation.VERTICAL, spacing: 0);
        this.backend = backend;
        this.hack = hack;
        var options = backend.options_for (hack.id);
        var axes = backend.simple_axes_for (hack.id);
        group = new PreferencesGroup ("%s options".printf (hack.title),
                                      axes.is_empty ? "Changes apply the next time it starts"
                                                    : "The centre of every slider is the shipped look; changes apply to the preview and the next start");
        var hide = new Button.with_label ("Hide");
        hide.add_css_class ("flat");
        hide.valign = Align.CENTER;
        hide.tooltip_text = "Collapse these options";
        hide.update_property (Gtk.AccessibleProperty.LABEL, "Hide the options of %s".printf (hack.title), -1);
        hide.clicked.connect (() => close_requested ());
        group.add_header_suffix (hide);
        append (group);
        if (!axes.is_empty)
            build_simple (options, axes);
        else
            build_generic (options);
        changed_id = backend.changed.connect (refresh);
        refresh ();
        if (still != null)
            schedule_still (50);
    }

    private ulong changed_id = 0;

    public override void dispose () {
        if (changed_id != 0) {
            backend.disconnect (changed_id);
            changed_id = 0;
        }
        if (apply_source != 0) {
            Source.remove (apply_source);
            apply_source = 0;
        }
        if (still_source != 0) {
            Source.remove (still_source);
            still_source = 0;
        }
        base.dispose ();
    }

    private ScreensaverOption? find (Gee.List<ScreensaverOption> options, string name) {
        foreach (var o in options)
            if (o.name == name)
                return o;
        return null;
    }

    private static string pretty (string id) {
        string s = id.replace ("-", " ");
        return s.length > 0 ? s.substring (0, 1).up () + s.substring (1) : s;
    }

    private static string scene_label (string id) {
        switch (id) {
        case "single": return "Single black hole";
        case "binary": return "Binary black holes";
        case "merger": return "Merger";
        case "tidal-disruption": return "Tidal disruption";
        case "galaxy-zoom": return "Galaxy zoom";
        case "three-body": return "Three black holes";
        default: return pretty (id);
        }
    }

    private void build_simple (Gee.List<ScreensaverOption> options, Gee.List<SimpleAxis> axes) {
        if (backend.still_supported (hack.id)) {
            still = new StillRow ();
            group.add_row (still);
        }
        palette_opt = find (options, "palette");
        if (palette_opt != null && palette_opt.kind == "enum") {
            string[] labels = new string[0];
            foreach (unowned string id in palette_opt.choices) {
                int i = index_of (backend.color_ids, id);
                labels += i >= 0 ? backend.color_labels[i] : pretty (id);
            }
            swatches = new SwatchRow ("Look", "Colour palette of the disk, halo and sky", palette_opt.choices, labels);
            swatches.chosen.connect ((id) => {
                if (refreshing)
                    return;
                backend.set_option (hack.id, "palette", id);
                if (backend.color_hack == hack.id && index_of (backend.color_ids, id) >= 0)
                    backend.color_mode = id; // keep the legacy key in step for older launchers
                schedule_still (300);
            });
            group.add_row (swatches);
        }
        scene_opt = find (options, "scene");
        if (scene_opt != null && scene_opt.kind == "enum") {
            string[] labels = new string[0];
            foreach (unowned string id in scene_opt.choices)
                labels += scene_label (id);
            scene_row = new SelectionRow ("Scene", labels, labels[0]);
            scene_row.selected.connect ((label) => {
                if (refreshing)
                    return;
                int i = index_of (labels, label);
                if (i >= 0) {
                    backend.set_option (hack.id, "scene", scene_opt.choices[i]);
                    schedule_still (300);
                }
            });
            group.add_row (scene_row);
        }
        preset_opt = find (options, "preset");
        if (preset_opt != null && preset_opt.kind == "enum" && preset_opt.choices.length > 1) {
            string[] ids = preset_opt.choices;
            string[] labels = new string[0];
            string[] tips = new string[0];
            string prefix = hack.title + ": ";
            for (int i = 0; i < ids.length; i++) {
                if (i == 0) {
                    labels += "None";
                    tips += "Only your own settings";
                    continue;
                }
                string l = ids[i];
                if (l.has_prefix (prefix))
                    l = l.substring (prefix.length);
                int p = l.last_index_of (" (");
                if (p > 0 && l.has_suffix (")"))
                    l = l.substring (0, p);
                labels += l;
                tips += ids[i];
            }
            pills = new PillRow ("Presets", "A named scene applied under your settings; sliders still adjust it", ids, labels, tips);
            pills.chosen.connect ((value) => {
                if (refreshing)
                    return;
                backend.set_option (hack.id, "preset", value);
                schedule_still (300);
            });
            group.add_row (pills);
        }
        // Randomize: only shown when the engine exposes the option (0.8.9+).
        if (find (options, "randomize") != null) {
            var rnd = new SwitchRow ("Randomize everything",
                "Re-roll the look and the scene type at each start, inside the safe ranges; the sliders still adjust it",
                backend.option_value (hack.id, "randomize") == "everything");
            rnd.switch_btn.notify["active"].connect (() => {
                if (refreshing)
                    return;
                backend.set_option (hack.id, "randomize", rnd.switch_btn.active ? "everything" : "off");
                schedule_still (300);
            });
            group.add_row (rnd);
            var roll = new ActionRow ("Roll now", "Draw one random configuration now; keep it by leaving Randomize off afterwards");
            var roll_btn = new Button.with_label ("Roll");
            roll_btn.valign = Align.CENTER;
            roll_btn.clicked.connect (() => {
                // A fresh seed makes the engine roll a new configuration; it is
                // reproducible and can be kept by turning Randomize off.
                var values = new Gee.HashMap<string, string> ();
                values["randomize"] = "everything";
                values["seed"] = GLib.Random.next_int ().to_string ();
                backend.set_options (hack.id, values);
                schedule_still (300);
            });
            roll.add_suffix (roll_btn);
            group.add_row (roll);
            // Pinning is visible and undoable: a pinned value is never rolled.
            if (backend.option_is_stored (hack.id, "preset")) {
                var fixed = new ActionRow ("Scene fixed: %s".printf (backend.option_value (hack.id, "preset")),
                    "Randomize rolls only the look; unpin to include the scene type");
                var unpin = new Button.with_label ("Unpin");
                unpin.valign = Align.CENTER;
                unpin.clicked.connect (() => {
                    var names = new Gee.ArrayList<string> ();
                    names.add ("preset");
                    backend.unset_options (hack.id, names);
                    fixed.set_visible (false);
                    schedule_still (300);
                });
                fixed.add_suffix (unpin);
                group.add_row (fixed);
            }
            bool palette_stored = backend.option_is_stored (hack.id, "palette");
            if (palette_stored || backend.color_mode != "stylized") {
                var popt = find (options, "palette");
                string pname = popt != null ? popt.display (backend.option_value (hack.id, "palette"))
                                            : backend.option_value (hack.id, "palette");
                var fixed = new ActionRow ("Palette fixed: %s".printf (pname),
                    "The palette is pinned (your setting or the system colour mode); unpin to let Randomize roll it");
                var unpin = new Button.with_label ("Unpin");
                unpin.valign = Align.CENTER;
                unpin.clicked.connect (() => {
                    if (backend.option_is_stored (hack.id, "palette")) {
                        var names = new Gee.ArrayList<string> ();
                        names.add ("palette");
                        backend.unset_options (hack.id, names);
                    } else {
                        backend.color_mode = "stylized";
                    }
                    fixed.set_visible (false);
                    schedule_still (300);
                });
                fixed.add_suffix (unpin);
                group.add_row (fixed);
            }
        }
        foreach (var axis in axes) {
            var row = new SliderRow (axis);
            var a = axis;
            row.moved.connect ((pos) => {
                if (refreshing)
                    return;
                pending[a.id] = pos;
                if (apply_source != 0)
                    Source.remove (apply_source);
                apply_source = Timeout.add (250, () => {
                    apply_source = 0;
                    flush_pending ();
                    return false;
                });
                update_warning (row);
            });
            row.reset_requested.connect (() => {
                var names = new Gee.ArrayList<string> ();
                foreach (var o in a.options)
                    names.add (o.name);
                pending.unset (a.id);
                backend.unset_options (hack.id, names);
                schedule_still (300);
            });
            sliders.add (row);
            group.add_row (row);
            basic_content = true; // a slider is a real row, so the basic view is not blank
        }
        var shown = new Gee.HashSet<string> ();
        foreach (unowned string n in SHOWN_FIRST)
            shown.add (n);
        add_advanced (options, shown, "Every option by group; the sliders above move several of them at once");
        add_footer ("Reset all", "Forget every stored option of %s".printf (hack.title));
    }

    private void build_generic (Gee.List<ScreensaverOption> options) {
        var simple = backend.simple_options_for (hack.id);
        var shown = new Gee.HashSet<string> ();
        if (!simple.is_empty) {
            foreach (var name in simple) {
                var o = find (options, name);
                if (o != null) {
                    group.add_row (make_option_row (o));
                    shown.add (name);
                    basic_content = true;
                }
            }
        } else {
            foreach (var o in options) {
                if (!o.advanced) {
                    group.add_row (make_option_row (o));
                    shown.add (o.name);
                    basic_content = true;
                }
            }
        }
        add_advanced (options, shown, "Fine tuning");
        add_footer ("Reset to defaults", "Forget every stored option of %s".printf (hack.title));
    }

    private ExpanderRow? advanced_row = null;
    private Gee.List<ScreensaverOption>? advanced_options = null;
    private Gee.Set<string>? advanced_shown = null;
    private int advanced_binding_start = 0;

    private void add_advanced (Gee.List<ScreensaverOption> options, Gee.Set<string> shown, string subtitle) {
        bool has_capture = false, has_other = false;
        foreach (var o in options) {
            if (shown.contains (o.name))
                continue;
            if (o.group_name == "Capture")
                has_capture = true;
            else
                has_other = true;
        }
        if (!has_other && !has_capture)
            return;
        advanced_options = options;
        advanced_shown = shown;
        advanced_row = new ExpanderRow ("Advanced", subtitle);
        advanced_binding_start = bindings.size;
        populate_advanced ();
        group.add_row (advanced_row);
    }

    // (Re)fill the Advanced expander in place. Flipping the Developer switch calls this
    // instead of rebuilding the whole panel, so the viewport and the open expander stay
    // where the user left them (rebuilding threw the page back to the panel top).
    private void populate_advanced () {
        advanced_row.clear_rows ();
        while (bindings.size > advanced_binding_start)
            bindings.remove_at (bindings.size - 1);
        var groups = new Gee.ArrayList<string> ();
        var by_group = new Gee.HashMap<string, Gee.ArrayList<ScreensaverOption>> ();
        bool developer = backend.developer_mode;
        bool has_capture = false;
        foreach (var o in advanced_options) {
            if (advanced_shown.contains (o.name))
                continue;
            if (o.group_name == "Capture") {
                has_capture = true;
                if (!developer)
                    continue;
            }
            if (!by_group.has_key (o.group_name)) {
                by_group[o.group_name] = new Gee.ArrayList<ScreensaverOption> ();
                groups.add (o.group_name);
            }
            by_group[o.group_name].add (o);
        }
        if (groups.size == 1) {
            foreach (var o in by_group[groups[0]])
                advanced_row.add_row (make_option_row (o));
        } else {
            foreach (var g in groups) {
                var sub = new ExpanderRow (g, "%d options".printf (by_group[g].size));
                foreach (var o in by_group[g])
                    sub.add_row (make_option_row (o));
                advanced_row.add_row (sub);
            }
        }
        if (has_capture) {
            var dev = new SwitchRow ("Developer options", "Show the capture settings used for stills and recordings", developer);
            dev.switch_btn.notify["active"].connect (() => {
                if (refreshing)
                    return;
                backend.developer_mode = dev.switch_btn.active;
                populate_advanced ();
                advanced_row.expanded = true;
                developer_toggled ();
            });
            advanced_row.add_row (dev);
        }
    }

    private void add_footer (string reset_label, string reset_tip) {
        var actions = new ActionRow ("", "");
        actions.visible = true;
        var reset = new Button.with_label (reset_label);
        reset.valign = Align.CENTER;
        reset.tooltip_text = reset_tip;
        reset.clicked.connect (() => {
            backend.reset_options (hack.id);
            if (backend.color_hack == hack.id)
                backend.color_mode = "stylized";
            schedule_still (300);
        });
        var preview = new Button.with_label ("Preview");
        preview.valign = Align.CENTER;
        preview.tooltip_text = "Run it full screen for 30 seconds";
        preview.clicked.connect (() => preview_requested (hack.id));
        actions.add_suffix (reset);
        actions.add_suffix (preview);
        actions.title = "Try it";
        actions.subtitle = "Preview runs the screensaver with these settings";
        group.add_row (actions);
    }

    private void flush_pending () {
        if (pending.is_empty)
            return;
        var values = new Gee.HashMap<string, string> ();
        foreach (var row in sliders) {
            if (!pending.has_key (row.axis.id))
                continue;
            double pos = pending[row.axis.id];
            foreach (var o in row.axis.options)
                values[o.name] = o.value_at (pos);
        }
        pending.clear ();
        backend.set_options (hack.id, values);
        schedule_still (350);
    }

    private void schedule_still (uint delay) {
        if (still == null)
            return;
        if (still_source != 0)
            Source.remove (still_source);
        still_source = Timeout.add (delay, () => {
            still_source = 0;
            render_still ();
            return false;
        });
    }

    private void render_still () {
        if (still == null)
            return;
        if (still_busy) {
            still_again = true;
            return;
        }
        still_busy = true;
        still.set_busy (true);
        // 12 s into the run: the director camera's establishing shot frames the whole disk
        backend.render_still (hack.id, 480, 270, 12.0, (path, error) => {
            still_busy = false;
            if (path != null)
                still.show_file (path);
            else
                still.set_error (error ?? "Preview unavailable");
            if (still_again) {
                still_again = false;
                schedule_still (100);
            }
        });
    }

    private void update_warning (SliderRow row) {
        if (row.axis.id != "detail")
            return;
        bool weak = backend.display_class == "weak";
        row.set_warning (weak && row.scale.get_value () > 50.5
                         ? "Above the centre this may stutter on this graphics chip (calibrated as weak)" : null);
    }

    private static int index_of (string[] ids, string id) {
        for (int i = 0; i < ids.length; i++)
            if (ids[i] == id)
                return i;
        return -1;
    }

    public void refresh () {
        refreshing = true;
        foreach (var b in bindings)
            b.refresh (backend);
        if (swatches != null && palette_opt != null)
            swatches.set_current (backend.option_value (hack.id, "palette"));
        if (scene_row != null && scene_opt != null) {
            string v = backend.option_value (hack.id, "scene");
            scene_row.current_value = scene_label (v);
        }
        if (pills != null && preset_opt != null)
            pills.set_current (backend.option_value (hack.id, "preset"));
        foreach (var row in sliders) {
            if (pending.has_key (row.axis.id))
                continue; // the user is still moving it
            var lead = row.axis.options[0];
            double pos = lead.position_of (backend.option_value (hack.id, lead.name));
            if ((row.scale.get_value () - pos).abs () > 0.5)
                row.scale.set_value (pos);
            update_warning (row);
        }
        refreshing = false;
    }

    private Gtk.Widget make_option_row (ScreensaverOption option) {
        string hack_id = hack.id;
        string current = backend.option_value (hack_id, option.name);
        switch (option.kind) {
        case "bool":
            var row = new SwitchRow (option.label, option.description, current == "true");
            row.switch_btn.notify["active"].connect (() => {
                if (!refreshing)
                    backend.set_option (hack_id, option.name, row.switch_btn.active ? "true" : "false");
            });
            bindings.add (new OptionBinding (hack_id, option, row));
            return row;
        case "enum":
            if (option.group_titles.size > 0 && option.name != "preset") {
                var outer = new ExpanderRow (option.label, option.display (current));
                foreach (var title in option.group_titles) {
                    var inner = new ExpanderRow (title, "%d choices".printf (option.group_choices[title].size));
                    foreach (var choice in option.group_choices[title]) {
                        string id = choice;
                        var item = new ActionRow (option.display (id));
                        var use = new Button.with_label ("Use");
                        use.valign = Align.CENTER;
                        use.clicked.connect (() => {
                            backend.set_option (hack_id, option.name, id);
                            outer.subtitle = option.display (id);
                        });
                        item.add_suffix (use);
                        inner.add_row (item);
                    }
                    outer.add_row (inner);
                }
                bindings.add (new OptionBinding (hack_id, option, outer));
                return outer;
            }
            var row = new SelectionRow (option.label, option.choices, current);
            row.selected.connect ((item) => {
                if (!refreshing)
                    backend.set_option (hack_id, option.name, item);
            });
            bindings.add (new OptionBinding (hack_id, option, row));
            return row;
        case "int":
        case "float":
            bool is_float = option.kind == "float";
            double lo = double.parse (option.min_value);
            double hi = double.parse (option.max_value);
            double step = is_float ? ((hi - lo) <= 10 ? 0.05 : 0.5) : 1;
            var row = new SpinRow (option.label, option.description, lo, hi, step, double.parse (current));
            if (is_float)
                row.spin_btn.digits = 2;
            if (option.name == "seed" && !is_float) {
                var randomize = new Button.from_icon_name ("view-refresh-symbolic");
                randomize.add_css_class ("flat");
                randomize.valign = Align.CENTER;
                randomize.tooltip_text = "Pick a new random seed";
                randomize.update_property (Gtk.AccessibleProperty.LABEL, "Pick a new random seed", -1);
                randomize.clicked.connect (() => {
                    row.spin_btn.value = Random.int_range ((int32) (lo < 1 ? 1 : lo), (int32) hi);
                });
                var copy = new Button.from_icon_name ("edit-copy-symbolic");
                copy.add_css_class ("flat");
                copy.valign = Align.CENTER;
                copy.tooltip_text = "Copy the seed to the clipboard";
                copy.update_property (Gtk.AccessibleProperty.LABEL, "Copy the seed", -1);
                copy.clicked.connect (() => {
                    var display = Gdk.Display.get_default ();
                    if (display != null)
                        display.get_clipboard ().set_content (new Gdk.ContentProvider.for_value ("%d".printf ((int) row.spin_btn.value)));
                });
                row.add_suffix (randomize);
                row.add_suffix (copy);
            }
            row.spin_btn.value_changed.connect (() => {
                if (refreshing)
                    return;
                double v = row.spin_btn.value;
                backend.set_option (hack_id, option.name, is_float ? "%.2f".printf (v) : "%d".printf ((int) v));
            });
            bindings.add (new OptionBinding (hack_id, option, row));
            return row;
        default:
            var row = new EntryRow (option.label);
            row.text = current;
            row.entry_changed.connect (() => {
                if (!refreshing)
                    backend.set_option (hack_id, option.name, row.text);
            });
            bindings.add (new OptionBinding (hack_id, option, row));
            return row;
        }
    }
}

// The options of one hack as shown under its row: the Developer hint, then the panel.
public class HackOptionsBox : Gtk.Box {
    public HackOptionsPanel panel;

    public HackOptionsBox (ScreensaverBackend backend, ScreensaverHack hack) {
        Object (orientation: Orientation.VERTICAL, spacing: 0);
        add_css_class ("ncz-options");
        panel = new HackOptionsPanel (backend, hack);
        var hint = ncz_caption ("No basic options; enable Developer options in Advanced to see them.", 60);
        hint.margin_top = 12;
        hint.margin_bottom = 12;
        hint.margin_start = 12;
        hint.visible = !panel.has_basic_content () && !backend.developer_mode;
        panel.developer_toggled.connect (() => {
            hint.visible = !panel.has_basic_content () && !backend.developer_mode;
        });
        append (hint);
        append (panel);
    }
}

public class ScreensaverSettings : Gtk.Box {
    private const string[] MODE_IDS = { "off", "one", "random" };
    private const string[] MODE_LABELS = { "Off", "One screensaver", "Random rotation" };
    private const string[] GPU_IDS = { "auto", "prime", "off" };
    private const string[] GPU_LABELS = { "Automatic (discrete on AC)", "Always the discrete GPU", "Same GPU as the desktop" };
    private const string[] RENDER_IDS = { "auto", "high", "balanced", "fast", "custom" };
    private const string[] RENDER_LABELS = { "Auto (recommended)", "High (native resolution)", "Balanced (75%)", "Fast (50%, 1080p cap)", "Custom" };
    private const string[] POOL_IDS = { "auto", "weak", "mid", "all" };
    private const string[] POOL_LABELS = {
        "Automatic",
        "Only screensavers that run well on weak graphics chips",
        "Also screensavers that need a mid-range GPU",
        "All screensavers"
    };
    private const string[] ENGINE_IDS = { "all", "ncz", "shader", "hyprsaver", "classics" };
    private const string[] ENGINE_LABELS = { "All", "In-house", "Shader", "hyprsaver", "Classics" };

    private ScreensaverBackend backend;
    private bool refreshing = false;
    private SelectionRow mode_row;
    private ActionRow current_row;
    private Button options_btn;
    private SpinRow start_row;
    private SpinRow rotate_row;
    private SelectionRow pool_row;
    private SwitchRow? show_all_row = null;
    private SwitchRow verify_row;
    private SelectionRow render_row;
    private SpinRow render_height_row;
    private SwitchRow? lock_row = null;
    private SpinRow? lock_delay_row = null;
    private SwitchRow? suspend_row = null;
    private SpinRow display_row;
    private SelectionRow? color_row = null;
    private SelectionRow? gpu_row = null;
    private ActionRow? class_info_row = null;
    private ActionRow? prime_info_row = null;

    // chooser
    private Box chooser_box = new Box (Orientation.VERTICAL, 8);
    private Gtk.SearchEntry search;
    private SelectionRow engine_row;
    private string engine = "all";
    private bool searching = false;
    private PreferencesGroup? list_group = null;
    private Gee.ArrayList<CategorySection> sections = new Gee.ArrayList<CategorySection> ();
    private Gee.ArrayList<HackRow> hack_rows = new Gee.ArrayList<HackRow> ();
    private Gee.HashMap<string, ScreensaverCategory> category_by_id = new Gee.HashMap<string, ScreensaverCategory> ();
    private string built_pinned = "";
    private Gee.HashMap<string, bool> expanded_memory = new Gee.HashMap<string, bool> ();

    // At most one hack's options are open, under that hack's row.
    private HackRow? open_row = null;
    private HackOptionsBox? open_box = null;

    public ScreensaverSettings (ScreensaverBackend backend) {
        Object (orientation: Orientation.VERTICAL, spacing: 12);
        this.backend = backend;
        NczChooserStyle.ensure ();
        add_css_class ("ncz-flat");
        foreach (var c in backend.categories ())
            category_by_id[c.id] = c;
        engine = backend.engine_filter;
        if (index_of (ENGINE_IDS, engine) < 0)
            engine = "all";

        build_screensaver_group ();
        append (chooser_box);
        build_chooser ();
        if (backend.lock_supported)
            build_lock_group ();
        build_display_group ();
        build_render_group ();
        if (backend.color_hack != null && backend.options_for (backend.color_hack).is_empty)
            build_color_group ();
        build_gpu_group ();

        backend.flags_changed.connect (() => {
            rebuild_list ();
            refresh ();
        });
        backend.changed.connect (refresh);
        refresh ();
    }

    private static int minutes (int seconds) {
        return (seconds + 59) / 60;
    }

    private static int index_of (string[] ids, string id) {
        for (int i = 0; i < ids.length; i++)
            if (ids[i] == id)
                return i;
        return -1;
    }

    private ScreensaverHack? hack_by_id (string id) {
        foreach (var h in backend.hacks ())
            if (h.id == id)
                return h;
        return null;
    }

    // ---- top group ----------------------------------------------------------------------

    private void build_screensaver_group () {
        var group = new PreferencesGroup ("Screensaver", "Runs after the desktop has been idle");
        append (group);

        mode_row = new SelectionRow ("Mode", MODE_LABELS, MODE_LABELS[0]);
        mode_row.selected.connect ((label) => {
            if (refreshing)
                return;
            int i = index_of (MODE_LABELS, label);
            if (i >= 0)
                backend.mode = MODE_IDS[i];
        });
        group.add_row (mode_row);

        current_row = new ActionRow ("Screensaver", "Pick one in the list below");
        options_btn = new Button.with_label ("Options");
        options_btn.valign = Align.CENTER;
        options_btn.tooltip_text = "Show the settings of this screensaver";
        options_btn.clicked.connect (() => {
            if (hack_by_id (backend.hack_id) != null)
                configure (backend.hack_id, null, true);
        });
        current_row.add_suffix (options_btn);
        group.add_row (current_row);

        start_row = new SpinRow ("Start after (minutes)", "Idle time before the screensaver starts", 1, 240, 1, 5);
        start_row.spin_btn.value_changed.connect (() => {
            if (!refreshing)
                backend.start_delay = (int) start_row.spin_btn.value * 60;
        });
        group.add_row (start_row);

        rotate_row = new SpinRow ("Change every (minutes)", "How long each screensaver runs in random mode", 1, 240, 1, 10);
        rotate_row.spin_btn.value_changed.connect (() => {
            if (!refreshing)
                backend.rotate_delay = (int) rotate_row.spin_btn.value * 60;
        });
        group.add_row (rotate_row);

        pool_row = new SelectionRow ("Screensavers used in random mode", POOL_LABELS, POOL_LABELS[0]);
        pool_row.selected.connect ((label) => {
            if (refreshing)
                return;
            int i = index_of (POOL_LABELS, label);
            if (i >= 0)
                backend.pool_class = POOL_IDS[i];
        });
        group.add_row (pool_row);

        // Read-only: class of the graphics chips driving the display and any offload
        // targets. The title is "Graphics chips" (plural) so the per-GPU verdict
        // ("GPU0 (Weak), GPU1 (Strong)") reads naturally for hybrid-graphics hosts;
        // a single-GPU host shows "GPU0 (Strong)" etc. (UX requirement,
        // operator 2026-09-30).
        class_info_row = new ActionRow ("Graphics chips", backend.gpu_verdict_label);
        var again = new Button.with_label ("Measure again");
        again.valign = Align.CENTER;
        again.tooltip_text = "Run the GPU speed test again (takes a few seconds)";
        again.clicked.connect (() => backend.recalibrate ());
        class_info_row.add_suffix (again);
        class_info_row.visible = backend.gpu_verdict_label != "";
        group.add_row (class_info_row);

        verify_row = new SwitchRow ("Detect black screensavers", "Skip a screensaver that draws only black");
        verify_row.switch_btn.notify["active"].connect (() => {
            if (!refreshing)
                backend.verify_render = verify_row.switch_btn.active;
        });
        group.add_row (verify_row);
    }

    // ---- chooser ------------------------------------------------------------------------

    private void build_chooser () {
        var header = new PreferencesGroup ("Screensavers", "Grouped by style. Star favourites, pick one, or choose which ones rotate.");
        chooser_box.append (header);

        search = new Gtk.SearchEntry ();
        search.placeholder_text = "Search screensavers (name, style, tag)";
        search.search_delay = 150;
        search.search_changed.connect (() => apply_filters ());
        search.update_property (Gtk.AccessibleProperty.LABEL, "Search screensavers", -1);
        chooser_box.append (search);

        var filter_group = new PreferencesGroup ();
        engine_row = new SelectionRow ("Show", ENGINE_LABELS, ENGINE_LABELS[index_of (ENGINE_IDS, engine)]);
        engine_row.tooltip_text = "Filter by engine: in-house savers, Shadertoy-style shaders, hyprsaver shaders, xscreensaver classics";
        engine_row.selected.connect ((label) => {
            int i = index_of (ENGINE_LABELS, label);
            if (i < 0 || ENGINE_IDS[i] == engine)
                return;
            engine = ENGINE_IDS[i];
            backend.engine_filter = engine;
            apply_filters ();
        });
        filter_group.add_row (engine_row);
        chooser_box.append (filter_group);

        if (backend.has_tiers && backend.has_flags) {
            show_all_row = new SwitchRow ("Show all screensavers",
                                          "Also list screensavers that may run poorly on this graphics chip");
            show_all_row.switch_btn.notify["active"].connect (() => {
                if (refreshing)
                    return;
                backend.show_all = show_all_row.switch_btn.active;
                apply_filters ();
            });
            filter_group.add_row (show_all_row);
        }
        rebuild_list ();
    }

    private string pinned_signature () {
        return string.joinv (",", backend.favourites) + "|" + string.joinv (",", backend.recent);
    }

    private void rebuild_list () {
        foreach (var s in sections)
            expanded_memory[s.id] = s.expander.expanded;
        HackOptionsBox? keep = open_box;
        string? keep_id = open_row != null ? open_row.hack.id : null;
        string? keep_section = open_row != null ? open_row.section : null;
        double keep_y = keep_id != null ? row_y (open_row.row) : double.NAN;
        if (open_row != null)
            open_row.options.set_child (null);
        open_row = null;
        open_box = null;
        if (list_group != null) {
            chooser_box.remove (list_group);
            list_group = null;
        }
        sections.clear ();
        hack_rows.clear ();
        list_group = new PreferencesGroup ();
        chooser_box.append (list_group);
        built_pinned = pinned_signature ();

        var favourites = new Gee.HashSet<string> ();
        foreach (unowned string f in backend.favourites)
            favourites.add (f);
        var fav_hacks = new Gee.ArrayList<ScreensaverHack> ();
        foreach (var h in backend.hacks ())
            if (favourites.contains (h.id))
                fav_hacks.add (h);
        if (!fav_hacks.is_empty)
            add_section ("favourites", "Favourites", "The ones you starred", fav_hacks, true);
        var recent_hacks = new Gee.ArrayList<ScreensaverHack> ();
        foreach (unowned string r in backend.recent) {
            var h = hack_by_id (r);
            if (h != null)
                recent_hacks.add (h);
        }
        if (!recent_hacks.is_empty)
            add_section ("recent", "Recently used", "Previewed or chosen lately", recent_hacks, true);

        var by_category = new Gee.HashMap<string, Gee.ArrayList<ScreensaverHack>> ();
        var other = new Gee.ArrayList<ScreensaverHack> ();
        foreach (var h in backend.hacks ()) {
            string c = backend.category_of (h.id);
            if (c == "" || !category_by_id.has_key (c)) {
                other.add (h);
                continue;
            }
            if (!by_category.has_key (c))
                by_category[c] = new Gee.ArrayList<ScreensaverHack> ();
            by_category[c].add (h);
        }
        foreach (var c in backend.categories ()) {
            if (!by_category.has_key (c.id))
                continue;
            var list = by_category[c.id];
            list.sort ((a, b) => strcmp (a.title.down (), b.title.down ()));
            add_section (c.id, c.label, c.description, list, false);
        }
        if (!other.is_empty) {
            other.sort ((a, b) => strcmp (a.title.down (), b.title.down ()));
            add_section ("other", backend.categories ().is_empty ? "All screensavers" : "Other",
                         backend.categories ().is_empty ? "Alphabetical" : "Not yet placed in a style", other, backend.categories ().is_empty);
        }
        apply_filters ();
        if (keep_id != null) {
            var hr = find_row (keep_id, keep_section);
            if (hr != null)
                show_options (hr, keep, keep_y, false);
        }
        sync_open_state ();
    }

    private void add_section (string id, string label, string description, Gee.List<ScreensaverHack> hacks, bool pinned) {
        var s = new CategorySection ();
        s.id = id;
        s.label = label;
        s.pinned = pinned;
        s.expander = new ExpanderRow (label, description);
        s.count = new Label ("%d".printf (hacks.size));
        s.count.add_css_class ("ncz-badge");
        s.count.valign = Align.CENTER;
        s.count.tooltip_text = "%d screensavers".printf (hacks.size);
        s.expander.add_suffix (s.count);
        // Rotation: the whole group with one click
        var menu = new MenuButton ();
        menu.icon_name = "view-more-symbolic";
        menu.add_css_class ("flat");
        menu.valign = Align.CENTER;
        menu.tooltip_text = "Use this group in the random rotation";
        menu.update_property (Gtk.AccessibleProperty.LABEL, "Rotation choices for %s".printf (label), -1);
        var pop = new Popover ();
        var pbox = new Box (Orientation.VERTICAL, 2);
        var only = new Button.with_label ("Rotate only these");
        var add = new Button.with_label ("Add these to the rotation");
        var remove = new Button.with_label ("Remove these from the rotation");
        foreach (var b in new Button[] { only, add, remove }) {
            b.add_css_class ("flat");
            b.halign = Align.FILL;
            pbox.append (b);
        }
        var ids = new Gee.ArrayList<string> ();
        foreach (var h in hacks)
            ids.add (h.id);
        only.clicked.connect (() => { set_pool_from (ids, "only"); pop.popdown (); });
        add.clicked.connect (() => { set_pool_from (ids, "add"); pop.popdown (); });
        remove.clicked.connect (() => { set_pool_from (ids, "remove"); pop.popdown (); });
        pop.child = pbox;
        menu.popover = pop;
        s.expander.add_suffix (menu);
        foreach (var h in hacks) {
            var hr = make_hack_row (h);
            hr.section = id;
            s.rows.add (hr);
            hack_rows.add (hr);
            s.expander.add_row (hr.row);
            s.expander.add_row (hr.options);
        }
        s.expander.expanded = expanded_memory.has_key (id) ? expanded_memory[id] : pinned;
        sections.add (s);
        list_group.add_row (s.expander);
    }

    private string engine_of (ScreensaverHack hack) {
        switch (hack.group_name) {
        case "xshadertoy": return "shader";
        case "hyprsaver": return "hyprsaver";
        case "Classics": return "classics";
        default: return "ncz";
        }
    }

    private string engine_text (ScreensaverHack hack) {
        string e = engine_of (hack);
        string origin = backend.origin_of (hack.id);
        switch (e) {
        case "shader": return origin == "ncz" ? "Shader, in-house" : "Shader";
        case "hyprsaver": return "hyprsaver";
        case "classics": return "Classic (xscreensaver)";
        default: return "In-house";
        }
    }

    private HackRow make_hack_row (ScreensaverHack hack) {
        var hr = new HackRow ();
        hr.hack = hack;
        string id = hack.id;
        hr.row = new ActionRow (hack.title, engine_text (hack));

        hr.pick = new CheckButton ();
        hr.pick.valign = Align.CENTER;
        hr.pick.tooltip_text = "Use this screensaver";
        hr.pick.update_property (Gtk.AccessibleProperty.LABEL, "Use %s".printf (hack.title), -1);
        hr.pick.toggled.connect (() => {
            if (refreshing)
                return;
            if (hr.pick.active)
                select_hack (id, hr.section);
            else if (backend.hack_id == id)
                hr.pick.active = true;
        });
        hr.row.add_prefix (hr.pick);

        hr.badge = new Label ("");
        hr.badge.valign = Align.CENTER;
        hr.badge.add_css_class ("ncz-badge");
        string tier = backend.tier_of (id);
        switch (tier) {
        case "weak":
            hr.badge.label = "Light";
            hr.badge.tooltip_text = "Runs well on any graphics chip";
            break;
        case "mid":
            hr.badge.label = "Mid";
            hr.badge.add_css_class ("ncz-badge-mid");
            hr.badge.tooltip_text = "Needs a mid-range GPU";
            break;
        case "strong":
            hr.badge.label = "Heavy";
            hr.badge.add_css_class ("ncz-badge-strong");
            hr.badge.tooltip_text = "Needs a strong GPU";
            break;
        default:
            hr.badge.visible = false;
            break;
        }
        hr.badge.update_property (Gtk.AccessibleProperty.LABEL, hr.badge.tooltip_text ?? "", -1);
        hr.row.add_suffix (hr.badge);

        hr.star = new ToggleButton ();
        hr.star.icon_name = "non-starred-symbolic";
        hr.star.add_css_class ("flat");
        hr.star.valign = Align.CENTER;
        hr.star.tooltip_text = "Favourite";
        hr.star.update_property (Gtk.AccessibleProperty.LABEL, "Favourite %s".printf (hack.title), -1);
        hr.star.toggled.connect (() => {
            hr.star.icon_name = hr.star.active ? "starred-symbolic" : "non-starred-symbolic";
            if (!refreshing)
                set_favourite (id, hr.star.active);
        });
        hr.row.add_suffix (hr.star);

        hr.gear = new Button.from_icon_name ("emblem-system-symbolic");
        hr.gear.add_css_class ("flat");
        hr.gear.valign = Align.CENTER;
        hr.gear.tooltip_text = "Options";
        hr.gear.update_property (Gtk.AccessibleProperty.LABEL, "Options of %s".printf (hack.title), -1);
        hr.gear.visible = !backend.options_for (id).is_empty;
        hr.gear.clicked.connect (() => configure (id, hr.section, true));
        hr.row.add_suffix (hr.gear);

        hr.preview = new Button.from_icon_name ("media-playback-start-symbolic");
        hr.preview.add_css_class ("flat");
        hr.preview.valign = Align.CENTER;
        hr.preview.tooltip_text = "Preview for 30 seconds";
        hr.preview.update_property (Gtk.AccessibleProperty.LABEL, "Preview %s".printf (hack.title), -1);
        hr.preview.clicked.connect (() => start_preview (id));
        hr.row.add_suffix (hr.preview);

        hr.pool = new Switch ();
        hr.pool.valign = Align.CENTER;
        hr.pool.tooltip_text = "Use in the random rotation";
        hr.pool.update_property (Gtk.AccessibleProperty.LABEL, "Rotate %s".printf (hack.title), -1);
        hr.pool.notify["active"].connect (() => {
            if (!refreshing)
                set_hack_enabled (id, hr.pool.active);
        });
        hr.row.add_suffix (hr.pool);

        hr.options = new Gtk.Revealer ();
        hr.options.transition_type = RevealerTransitionType.SLIDE_DOWN;
        hr.options.visible = false;
        hr.options.notify["child-revealed"].connect (() => {
            if (hr.options.child_revealed && hr == open_row && hr.options.transition_type != RevealerTransitionType.NONE)
                after_layout (() => place (hr, row_y (hr.row)));
        });

        hr.row.activated.connect (() => {
            if (backend.mode != "random")
                select_hack (id, hr.section);
        });
        return hr;
    }

    private void select_hack (string id, string section) {
        var hr = find_row (id, section);
        double y = hr != null ? row_y (hr.row) : double.NAN;
        if (backend.mode == "off")
            backend.mode = "one";
        if (backend.hack_id != id)
            backend.hack_id = id;
        backend.note_recent (id);
        if (!backend.options_for (id).is_empty && (open_row == null || open_row.hack.id != id))
            configure (id, section, false, y);
    }

    private void set_favourite (string id, bool on) {
        string[] kept = new string[0];
        bool seen = false;
        foreach (unowned string f in backend.favourites) {
            if (f == id) {
                seen = true;
                if (!on)
                    continue;
            }
            kept += f;
        }
        if (on && !seen)
            kept += id;
        backend.favourites = kept;
    }

    private void set_pool_from (Gee.List<string> ids, string how) {
        var all = new Gee.ArrayList<string> ();
        foreach (var hack in backend.hacks ())
            all.add (hack.id);
        var current = new Gee.ArrayList<string> ();
        foreach (var e in backend.enabled_hacks)
            current.add (e);
        if (current.is_empty)
            current.add_all (all);
        var result = new Gee.ArrayList<string> ();
        if (how == "only") {
            result.add_all (ids);
        } else {
            result.add_all (current);
            foreach (var id in ids) {
                if (how == "add" && !result.contains (id))
                    result.add (id);
                if (how == "remove")
                    result.remove (id);
            }
        }
        if (result.size == all.size)
            result.clear ();
        string[] strv = new string[0];
        foreach (var e in result)
            strv += e;
        backend.enabled_hacks = strv;
        if (backend.mode != "random")
            backend.mode = "random";
    }

    private bool matches_engine (ScreensaverHack hack) {
        if (engine == "all")
            return true;
        if (engine == "ncz")
            return backend.origin_of (hack.id) == "ncz" || engine_of (hack) == "ncz";
        return engine_of (hack) == engine;
    }

    private bool matches_search (ScreensaverHack hack, string needle) {
        if (needle == "")
            return true;
        string cat = backend.category_of (hack.id);
        string cat_label = category_by_id.has_key (cat) ? category_by_id[cat].label : "";
        string hay = "%s %s %s %s %s".printf (hack.title, hack.id, cat_label, backend.tags_of (hack.id), engine_text (hack)).down ();
        foreach (unowned string word in needle.down ().split (" ")) {
            if (word != "" && !hay.contains (word))
                return false;
        }
        return true;
    }

    private void apply_filters () {
        string needle = search.text.strip ();
        bool now_searching = needle != "";
        bool show_all = show_all_row == null || backend.show_all;
        foreach (var hr in hack_rows) {
            bool ok = matches_engine (hr.hack) && matches_search (hr.hack, needle)
                && (show_all || !backend.is_flagged (hr.hack.id));
            hr.shown = ok;
            hr.row.visible = ok;
            hr.options.visible = ok && hr == open_row;
        }
        foreach (var s in sections) {
            int visible = 0;
            foreach (var hr in s.rows)
                if (hr.shown)
                    visible++;
            s.expander.visible = visible > 0;
            s.count.label = visible == s.rows.size ? "%d".printf (s.rows.size) : "%d of %d".printf (visible, s.rows.size);
            if (now_searching && visible > 0)
                s.expander.expanded = true;
            else if (searching && !now_searching)
                s.expander.expanded = s.pinned;
        }
        searching = now_searching;
    }

    // ---- options panel ------------------------------------------------------------------

    private void configure (string id, string? section, bool toggle, double y = double.NAN) {
        var hack = hack_by_id (id);
        var hr = find_row (id, section);
        if (hack == null || hr == null || backend.options_for (id).is_empty)
            return;
        if (hr == open_row) {
            if (toggle)
                close_options ();
            else
                after_layout (() => place (hr, double.NAN));
            return;
        }
        if (y.is_nan ())
            y = row_y (hr.row);
        if (backend.mode != "one" || backend.hack_id != id)
            backend.note_recent (id);
        // note_recent may have rebuilt the list
        hr = find_row (id, section);
        if (hr == null)
            return;
        var box = new HackOptionsBox (backend, hack);
        box.panel.preview_requested.connect ((hid) => start_preview (hid));
        box.panel.close_requested.connect (() => close_options ());
        show_options (hr, box, y, true);
    }

    // The row in that section, else one already on screen, else the one in its style group.
    private HackRow? find_row (string id, string? section) {
        HackRow? fallback = null;
        foreach (var s in sections) {
            foreach (var hr in s.rows) {
                if (hr.hack.id != id)
                    continue;
                if (section != null ? s.id == section : (s.expander.expanded && hr.shown))
                    return hr;
                if (fallback == null || !s.pinned)
                    fallback = hr;
            }
        }
        return fallback;
    }

    private void show_options (HackRow hr, HackOptionsBox box, double y, bool animate) {
        close_options ();
        open_row = hr;
        open_box = box;
        bool opened_section = false;
        foreach (var s in sections) {
            if (s.id == hr.section && !s.expander.expanded) {
                s.expander.expanded = true;
                opened_section = true;
            }
        }
        hr.options.transition_type = animate ? RevealerTransitionType.SLIDE_DOWN : RevealerTransitionType.NONE;
        hr.options.set_child (box);
        hr.options.visible = hr.shown;
        hr.options.reveal_child = true;
        sync_open_state ();
        after_layout (() => place (hr, y));
        if (opened_section) {
            // The style group was folded: its row is not mapped until it has unfolded, so
            // bring the options into view once it has (the Options button of the page top).
            Timeout.add (450, () => {
                if (hr == open_row)
                    after_layout (() => place (hr, double.NAN));
                return false;
            });
        }
    }

    // The gear of the open hack and the Options button say it is open, and what they do next.
    private void sync_open_state () {
        foreach (var hr in hack_rows) {
            bool open = hr == open_row;
            hr.gear.tooltip_text = open ? "Hide the options" : "Options";
            hr.gear.update_state (Gtk.AccessibleState.EXPANDED, open, -1);
            if (open)
                hr.gear.add_css_class ("ncz-open");
            else
                hr.gear.remove_css_class ("ncz-open");
        }
        string current = backend.hack_id;
        bool current_open = open_row != null && open_row.hack.id == current;
        options_btn.label = current_open ? "Hide options" : "Options";
        options_btn.tooltip_text = current_open ? "Collapse the settings of this screensaver"
                                                : "Show the settings of this screensaver";
        options_btn.update_state (Gtk.AccessibleState.EXPANDED, current_open, -1);
    }

    private void close_options () {
        if (open_row == null)
            return;
        open_row.options.visible = false;
        open_row.options.reveal_child = false;
        open_row.options.set_child (null);
        open_row = null;
        open_box = null;
        sync_open_state ();
    }

    private Gtk.ScrolledWindow? scroller () {
        return get_ancestor (typeof (Gtk.ScrolledWindow)) as Gtk.ScrolledWindow;
    }

    private double row_y (Gtk.Widget? w) {
        var sw = scroller ();
        var b = Graphene.Rect ();
        if (w == null || sw == null || !w.get_mapped () || !w.compute_bounds (sw, out b))
            return double.NAN;
        return b.origin.y;
    }

    private delegate void Then ();

    private void after_layout (owned Then then) {
        var clock = get_frame_clock ();
        if (clock == null)
            return;
        ulong handler = 0;
        handler = clock.layout.connect_after (() => {
            clock.disconnect (handler);
            then ();
        });
        queue_allocate ();
    }

    // Keep the row at old_y, then scroll the options into view without pushing the row off the top.
    private void place (HackRow hr, double old_y) {
        var sw = scroller ();
        double y = row_y (hr.row);
        if (sw == null || y.is_nan () || hr != open_row)
            return;
        double shift = old_y.is_nan () ? 0 : y - old_y;
        double top = y - shift;
        if (top < 0) {
            shift += top;
            top = 0;
        }
        var b = Graphene.Rect ();
        var child = hr.options.get_child ();
        if (child != null && child.compute_bounds (sw, out b)) {
            double bottom = b.origin.y + b.size.height - shift;
            double vh = sw.get_height ();
            if (bottom > vh)
                shift += double.min (bottom - vh, top);
        }
        if (shift != 0)
            sw.vadjustment.value += shift;
    }

    // ---- the rest of the page -----------------------------------------------------------

    private void build_lock_group () {
        var group = new PreferencesGroup ("Lock screen");
        append (group);

        lock_row = new SwitchRow ("Lock after the screensaver");
        lock_row.switch_btn.notify["active"].connect (() => {
            if (!refreshing)
                backend.lock_enabled = lock_row.switch_btn.active;
            lock_delay_row.sensitive = lock_row.switch_btn.active;
        });
        group.add_row (lock_row);

        lock_delay_row = new SpinRow ("Lock after (minutes)", "Minutes after the screensaver starts; 0 locks at once", 0, 240, 1, 1);
        lock_delay_row.spin_btn.value_changed.connect (() => {
            if (!refreshing)
                backend.lock_delay = (int) lock_delay_row.spin_btn.value * 60;
        });
        group.add_row (lock_delay_row);

        suspend_row = new SwitchRow ("Lock on suspend");
        suspend_row.switch_btn.notify["active"].connect (() => {
            if (!refreshing)
                backend.lock_on_suspend = suspend_row.switch_btn.active;
        });
        group.add_row (suspend_row);
    }

    private void build_display_group () {
        var group = new PreferencesGroup ("Display");
        append (group);

        display_row = new SpinRow ("Turn display off after (minutes)", "0 keeps the display on", 0, 240, 1, 0);
        display_row.spin_btn.value_changed.connect (() => {
            if (!refreshing)
                backend.display_off_delay = (int) display_row.spin_btn.value * 60;
        });
        group.add_row (display_row);
    }

    private void build_render_group () {
        var group = new PreferencesGroup ("Render quality", "Lower quality renders shader screensavers smaller: less GPU load and less stutter on 4K screens");
        append (group);

        render_row = new SelectionRow ("Render quality", RENDER_LABELS, RENDER_LABELS[0]);
        render_row.selected.connect ((label) => {
            if (refreshing)
                return;
            int i = index_of (RENDER_LABELS, label);
            if (i >= 0)
                backend.render_quality = RENDER_IDS[i];
        });
        group.add_row (render_row);

        render_height_row = new SpinRow ("Maximum render height (advanced)", "Pixels; 0 lets Auto decide (choose High for no cap)", 0, 8192, 10, 0);
        render_height_row.spin_btn.value_changed.connect (() => {
            if (!refreshing)
                backend.max_render_height = (int) render_height_row.spin_btn.value;
        });
        group.add_row (render_height_row);
    }

    // Older engines without per-hack options: the Black Hole palette through the legacy key.
    private void build_color_group () {
        var group = new PreferencesGroup ("Color model");
        append (group);

        color_row = new SelectionRow ("Color model", backend.color_labels, backend.color_labels[0]);
        color_row.selected.connect ((label) => {
            if (refreshing)
                return;
            int i = index_of (backend.color_labels, label);
            if (i >= 0)
                backend.color_mode = backend.color_ids[i];
        });
        group.add_row (color_row);

        var preview = new ActionRow ("Preview", "Runs the screensaver for 30 seconds");
        var button = new Button.with_label ("Preview");
        button.valign = Align.CENTER;
        button.clicked.connect (() => start_preview (backend.color_hack));
        preview.add_suffix (button);
        group.add_row (preview);
    }

    private void build_gpu_group () {
        var group = new PreferencesGroup ("Graphics", "Only the screensaver process moves to the discrete GPU");
        append (group);

        gpu_row = new SelectionRow ("Screensaver GPU", GPU_LABELS, GPU_LABELS[0]);
        gpu_row.sensitive = backend.gpu_offload_supported;
        gpu_row.selected.connect ((label) => {
            if (refreshing)
                return;
            int i = index_of (GPU_LABELS, label);
            if (i >= 0)
                backend.gpu_offload = GPU_IDS[i];
        });
        group.add_row (gpu_row);

        // Read-only: plain-English explanation of the PRIME render-offload
        // policy. Always pre-created (hidden by default) so refresh() can
        // flip its visibility and content later, mirroring the GTK4
        // reference app's pattern (see _prime_row in
        // contrib/gtk4-reference-app/ncz-screensaver-settings). The launcher
        // explains unsupported single-GPU layouts too; older launchers may
        // omit the field. (UX requirement, operator 2026-09-30.)
        prime_info_row = new ActionRow ("PRIME render offload", "");
        prime_info_row.valign = Align.START;
        prime_info_row.visible = false;
        group.add_row (prime_info_row);
    }

    private string warning_text (string id) {
        string expect = backend.expectation (id);
        return expect != "" ? "May stutter - " + expect : "May stutter on this graphics chip";
    }

    // Flagged hacks ask first; without a parent window, preview at once. A plain GTK alert
    // (not a libsingularity dialog) so the plugin loads on every desktop build in the field.
    private void start_preview (string id) {
        var window = get_root () as Gtk.Window;
        if (!backend.is_flagged (id) || window == null) {
            if (backend.is_flagged (id))
                warning ("previewing a flagged screensaver without a confirmation window");
            backend.preview (id);
            return;
        }
        var dialog = new Gtk.AlertDialog ("Preview anyway?");
        dialog.detail = warning_text (id).replace ("May stutter", "This screensaver may run poorly here");
        dialog.buttons = { "Cancel", "Preview" };
        dialog.default_button = 1;
        dialog.cancel_button = 0;
        dialog.modal = true;
        dialog.choose.begin (window, null, (obj, res) => {
            try {
                if (dialog.choose.end (res) == 1)
                    backend.preview (id);
            } catch (Error e) {
                // dismissed
            }
        });
    }

    private void set_hack_enabled (string id, bool enabled) {
        var all = new Gee.ArrayList<string> ();
        foreach (var hack in backend.hacks ())
            all.add (hack.id);
        var current = new Gee.ArrayList<string> ();
        foreach (var e in backend.enabled_hacks)
            current.add (e);
        if (current.is_empty)
            current.add_all (all);
        if (enabled && !current.contains (id))
            current.add (id);
        if (!enabled)
            current.remove (id);
        if (current.size == all.size)
            current.clear ();
        // Gee to_array () is not NULL-terminated, which a strv setting needs
        string[] ids = new string[0];
        foreach (var e in current)
            ids += e;
        backend.enabled_hacks = ids;
    }

    private void refresh () {
        refreshing = true;

        if (pinned_signature () != built_pinned)
            rebuild_list ();

        string mode = backend.mode;
        int mi = index_of (MODE_IDS, mode);
        mode_row.current_value = mi >= 0 ? MODE_LABELS[mi] : MODE_LABELS[0];
        rotate_row.visible = mode != "one";
        pool_row.visible = mode != "one" && backend.has_tiers;
        var current = hack_by_id (backend.hack_id);
        current_row.visible = mode != "random";
        options_btn.visible = current != null && !backend.options_for (current.id).is_empty;
        if (current != null) {
            string cat = backend.category_of (current.id);
            string cat_label = category_by_id.has_key (cat) ? category_by_id[cat].label : engine_text (current);
            current_row.subtitle = mode == "off" ? "%s (off: pick one below to turn it on)".printf (current.title)
                                                 : "%s - %s".printf (current.title, cat_label);
        } else {
            current_row.subtitle = "Pick one in the list below";
        }
        int ri = index_of (RENDER_IDS, backend.render_quality);
        render_row.current_value = RENDER_LABELS[ri >= 0 ? ri : 0];
        render_height_row.spin_btn.value = backend.max_render_height;
        int pi = index_of (POOL_IDS, backend.pool_class);
        pool_row.current_value = POOL_LABELS[pi >= 0 ? pi : 0];
        verify_row.switch_btn.active = backend.verify_render;
        start_row.spin_btn.value = minutes (backend.start_delay);
        rotate_row.spin_btn.value = minutes (backend.rotate_delay);
        if (backend.lock_supported) {
            lock_row.switch_btn.active = backend.lock_enabled;
            lock_delay_row.spin_btn.value = minutes (backend.lock_delay);
            lock_delay_row.sensitive = lock_row.switch_btn.active;
            suspend_row.switch_btn.active = backend.lock_on_suspend;
        }
        display_row.spin_btn.value = minutes (backend.display_off_delay);
        if (color_row != null) {
            int ci = index_of (backend.color_ids, backend.color_mode);
            color_row.current_value = backend.color_labels[ci >= 0 ? ci : 0];
        }
        if (gpu_row != null) {
            int gi = index_of (GPU_IDS, backend.gpu_offload);
            gpu_row.current_value = GPU_LABELS[gi >= 0 ? gi : 0];
        }
        // Refresh the read-only GPU-verdict row. After recalibrate() the
        // launcher's verdict changes (a freshly measured ms), and after an
        // AC/battery transition the PRIME explanation would change; without
        // this the rows go stale until the user reopens the settings.
        if (class_info_row != null) {
            string verdict = backend.gpu_verdict_label;
            class_info_row.subtitle = verdict;
            class_info_row.visible = verdict != "";
        }
        if (prime_info_row != null) {
            string prime = backend.prime_offload_label;
            prime_info_row.subtitle = prime;
            prime_info_row.visible = prime != "";
        }

        var enabled = new Gee.HashSet<string> ();
        foreach (var e in backend.enabled_hacks)
            enabled.add (e);
        var favourites = new Gee.HashSet<string> ();
        foreach (unowned string f in backend.favourites)
            favourites.add (f);
        bool random = mode == "random";
        foreach (var hr in hack_rows) {
            string id = hr.hack.id;
            hr.pool.visible = random;
            hr.pool.active = enabled.is_empty || enabled.contains (id);
            hr.pick.visible = !random;
            hr.pick.active = backend.hack_id == id;
            hr.star.active = favourites.contains (id);
            string subtitle = engine_text (hr.hack);
            if (backend.is_flagged (id))
                subtitle += " - " + warning_text (id);
            string issue = backend.known_issue (id);
            if (issue != "")
                subtitle += " - known issue: " + issue;
            hr.row.subtitle = subtitle;
        }
        if (random) {
            foreach (var s in sections) {
                int on = 0;
                foreach (var hr in s.rows)
                    if (enabled.is_empty || enabled.contains (hr.hack.id))
                        on++;
                s.count.tooltip_text = "%d of %d in the rotation".printf (on, s.rows.size);
            }
        }

        if (show_all_row != null)
            show_all_row.switch_btn.active = backend.show_all;

        refreshing = false;
    }
}
