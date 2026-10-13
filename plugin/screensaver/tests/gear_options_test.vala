// Headless regression test for the per-screensaver Options ("gear") button.
// Clicks the gear of every catalog hack against the real engine schemas and
// asserts the options open in place under that hack's row, in view, with no new
// window, one hack at a time, and collapse on a second click.
// Run under a display (tools/test-plugin-gear.sh wraps it in xvfb-run); exit
// status 0 = all passed.

using Gtk;
using Singularity;
using Singularity.Widgets;

namespace GearTest {
    public int failures = 0;
    public int checks = 0;

    void collect (Gtk.Widget w, Gee.ArrayList<Gtk.Widget> acc) {
        acc.add (w);
        for (var c = w.get_first_child (); c != null; c = c.get_next_sibling ())
            collect (c, acc);
    }

    Gee.ArrayList<Gtk.Widget> walk (Gtk.Widget root) {
        var all = new Gee.ArrayList<Gtk.Widget> ();
        collect (root, all);
        return all;
    }

    // The subtitle the row sets for a hack (mirrors ScreensaverSettings.engine_text).
    // Two catalog hacks share the title "Voronoi", so the row's engine subtitle is
    // needed to find the right gear.
    string engine_text (ScreensaverBackend backend, ScreensaverHack h) {
        string e;
        switch (h.group_name) {
        case "xshadertoy": e = "shader"; break;
        case "hyprsaver": e = "hyprsaver"; break;
        case "Classics": e = "classics"; break;
        default: e = "ncz"; break;
        }
        switch (e) {
        case "shader": return backend.origin_of (h.id) == "ncz" ? "Shader, in-house" : "Shader";
        case "hyprsaver": return "hyprsaver";
        case "classics": return "Classic (xscreensaver)";
        default: return "In-house";
        }
    }

    // The gear for a hack: the emblem-system button inside its row (the same path as a
    // real click on the row's gear). Only a visible gear counts; the button exists but
    // is hidden on hacks that offer no options.
    Gtk.Button? gear_for (Gtk.Widget root, ScreensaverBackend backend, ScreensaverHack h) {
        string expect = engine_text (backend, h);
        foreach (var w in walk (root)) {
            var ar = w as ActionRow;
            if (ar == null || ar.title != h.title || ar.subtitle == null || !ar.subtitle.has_prefix (expect))
                continue;
            foreach (var c in walk (ar)) {
                var b = c as Gtk.Button;
                if (b != null && b.icon_name == "emblem-system-symbolic" && b.get_visible ())
                    return b;
            }
        }
        return null;
    }

    HackOptionsPanel? find_panel (Gtk.Widget root) {
        foreach (var w in walk (root)) {
            var p = w as HackOptionsPanel;
            if (p != null)
                return p;
        }
        return null;
    }

    int row_count (HackOptionsPanel p) {
        return (int) p.group.get_rows ().size;
    }

    // Rows other than the "Try it" footer: an options panel must have at least one.
    bool has_content (HackOptionsPanel p) {
        int count = 0;
        foreach (var r in p.group.get_rows ()) {
            var a = r as ActionRow;
            if (a != null && a.title == "Try it")
                continue;
            count++;
        }
        return count > 0;
    }

    int mapped_panels (Gtk.Widget root) {
        int n = 0;
        foreach (var w in walk (root))
            if (w is HackOptionsPanel && w.get_mapped ())
                n++;
        return n;
    }

    double y_in (Gtk.Widget w, Gtk.Widget target, out double h) {
        var b = Graphene.Rect ();
        h = 0;
        if (!w.compute_bounds (target, out b))
            return double.NAN;
        h = b.size.height;
        return b.origin.y;
    }

    void pump (uint ms) {
        var loop = new MainLoop ();
        Timeout.add (ms, () => { loop.quit (); return false; });
        loop.run ();
    }


    // The page the Settings plugin details view builds: a scroller that never scrolls
    // sideways and clips (as the sidebar layer surface does), the content box, one
    // "Settings" group, one row holding the plugin widget with 12 px margins.
    class Host : Object {
        public Gtk.Window win;
        public Gtk.ScrolledWindow scroll;
        public Gtk.Box content;
        public ScreensaverSettings settings;
        public int width;
    }

    Host make_host (ScreensaverBackend backend, int width) {
        var h = new Host ();
        h.width = width;
        h.settings = new ScreensaverSettings (backend);
        h.settings.margin_start = 12;
        h.settings.margin_end = 12;
        h.settings.margin_top = 12;
        h.settings.margin_bottom = 12;
        var group = new PreferencesGroup ("Settings");
        var row = new PreferencesRow ();
        row.set_child (h.settings);
        group.add_row (row);
        h.content = new Gtk.Box (Orientation.VERTICAL, 0);
        h.content.append (group);
        h.scroll = new Gtk.ScrolledWindow ();
        h.scroll.set_policy (PolicyType.EXTERNAL, PolicyType.AUTOMATIC);
        h.scroll.vexpand = true;
        h.scroll.set_child (h.content);
        h.win = new Gtk.Window ();
        h.win.title = "narrow gear test";
        h.win.add_css_class ("singularity");
        h.win.set_default_size (width, 800);
        h.win.set_child (h.scroll);
        h.win.present ();
        pump (400);
        return h;
    }

    // Every mapped descendant must lie inside the viewport width: nothing is wider than
    // the page, so nothing is clipped on the right and no sideways scroll is needed.
    // Widgets inside a scroller that clips on purpose (the preview clamp) are judged by
    // the scroller itself. Returns the number of offenders (the first few are printed).
    int overflow_offenders (Host h, string what) {
        int vw = h.scroll.get_width ();
        int bad = 0;
        int minw, natw;
        h.content.measure (Orientation.HORIZONTAL, -1, out minw, out natw, null, null);
        if (minw > h.width) {
            bad++;
            fail ("%s: page minimum width %d exceeds the %d px viewport".printf (what, minw, h.width));
            // Name the widgets that force it: too wide to fit, with no child that is.
            int named = 0;
            foreach (var w in walk (h.content)) {
                if (!w.get_mapped () || named >= 4)
                    continue;
                int wmin, wnat;
                w.measure (Orientation.HORIZONTAL, -1, out wmin, out wnat, null, null);
                if (wmin <= h.width - 24)
                    continue;
                bool child_wide = false;
                for (var c = w.get_first_child (); c != null; c = c.get_next_sibling ()) {
                    int cmin, cnat;
                    c.measure (Orientation.HORIZONTAL, -1, out cmin, out cnat, null, null);
                    if (c.get_mapped () && cmin > h.width - 24)
                        child_wide = true;
                }
                if (!child_wide) {
                    named++;
                    var lbl = w as Gtk.Label;
                    var arow = w as ActionRow;
                    stdout.printf ("  wide: %s min=%d %s\n", w.get_type ().name (), wmin,
                                   lbl != null ? "\"" + lbl.label.substring (0, int.min (40, lbl.label.length)) + "\""
                                   : (arow != null ? "\"" + arow.title + "\"" : ""));
                    var kids = new StringBuilder ();
                    for (var c = w.get_first_child (); c != null; c = c.get_next_sibling ()) {
                        int cmin, cnat;
                        c.measure (Orientation.HORIZONTAL, -1, out cmin, out cnat, null, null);
                        kids.append ("%s:%d%s ".printf (c.get_type ().name (), cmin, c.get_mapped () ? "" : "(unmapped)"));
                    }
                    stdout.printf ("    kids: %s\n", kids.str);
                }
            }
        }
        foreach (var w in walk (h.content)) {
            if (!w.get_mapped () || w.get_width () <= 0)
                continue;
            // A short value label ("Stylized", "Single black hole") is never cut short.
            var vl = w as Gtk.Label;
            if (vl != null && h.width >= 360 && vl.label.length <= 24 && vl.get_layout ().is_ellipsized ()) {
                bad++;
                fail ("%s: label \"%s\" is truncated".printf (what, vl.label));
            }
            var sw = w.get_ancestor (typeof (Gtk.ScrolledWindow)) as Gtk.ScrolledWindow;
            if (sw != null && sw != h.scroll)
                continue;
            var b = Graphene.Rect ();
            if (!w.compute_bounds (h.scroll, out b))
                continue;
            if (b.origin.x < -1.0 || b.origin.x + b.size.width > vw + 1.0) {
                bad++;
                if (bad <= 3)
                    fail ("%s: %s x=%.0f w=%.0f exceeds the %d px viewport".printf (
                          what, w.get_type ().name (), b.origin.x, b.size.width, vw));
            }
        }
        return bad;
    }

    // Writes the host window to a PNG (GEAR_SHOTS=<dir>), the picture of what a viewer sees.
    void save_shot (Host h, string path) {
        var paintable = new Gtk.WidgetPaintable (h.win);
        var snap = new Gtk.Snapshot ();
        paintable.snapshot (snap, h.win.get_width (), h.win.get_height ());
        var node = snap.free_to_node ();
        var renderer = ((Gtk.Native) h.win).get_renderer ();
        if (node == null || renderer == null)
            return;
        var texture = renderer.render_texture (node, null);
        texture.save_to_png (path);
    }

    void fail (string msg) {
        failures++;
        stdout.printf ("FAIL: %s\n", msg);
    }
}

int main (string[] args) {
    Gtk.init ();
    // The real theme, as the shell has it: paddings and minimum sizes change the widths.
    Singularity.Style.StyleManager.get_default ().load_theme ();

    var backend = create_screensaver_backend ();
    // List hacks flagged for the software renderer too, so every gear can be clicked.
    backend.show_all = true;
    var settings = new ScreensaverSettings (backend);

    // Emulate the host page: the plugin widget inside a PreferencesRow/Group in a
    // scroller whose viewport is the height of a typical settings page.
    var page = new Gtk.Box (Orientation.VERTICAL, 0);
    var group = new PreferencesGroup ();
    var row = new PreferencesRow ();
    row.set_child (settings);
    group.add_row (row);
    page.append (group);
    var scroll = new Gtk.ScrolledWindow ();
    scroll.set_policy (PolicyType.EXTERNAL, PolicyType.AUTOMATIC);
    scroll.set_child (page);
    var win = new Gtk.Window ();
    win.title = "gear test";
    win.add_css_class ("singularity");
    win.set_default_size (700, 800);
    win.set_child (scroll);
    win.present ();
    GearTest.pump (400);

    uint toplevels = Gtk.Window.get_toplevels ().get_n_items ();
    int with_options = 0;
    int without = 0;
    Gtk.Button? last_gear = null;
    Gtk.Button? open_gear = null;
    // GEAR_NARROW_ONLY=1 skips the in-place checks (a quick loop while fixing the layout).
    bool narrow_only = Environment.get_variable ("GEAR_NARROW_ONLY") != null;
    foreach (var h in backend.hacks ()) {
        if (narrow_only)
            break;
        var options = backend.options_for (h.id);
        var gear = GearTest.gear_for (settings, backend, h);
        if (options.is_empty) {
            without++;
            if (gear != null)
                GearTest.fail ("%s has no options but its gear is visible".printf (h.id));
            continue;
        }
        with_options++;
        GearTest.checks++;
        if (gear == null) {
            GearTest.fail ("%s has options but no gear button".printf (h.id));
            continue;
        }
        // As a user would: open the row's style group and scroll the row to the lower
        // part of the viewport, so the options have to be brought into view.
        var ar = gear.get_ancestor (typeof (ActionRow)) as ActionRow;
        var section = ar.get_parent ().get_ancestor (typeof (ExpanderRow)) as ExpanderRow;
        if (section != null)
            section.expanded = true;
        GearTest.pump (400);
        double rh;
        double y0 = GearTest.y_in (ar, scroll, out rh);
        int vh = scroll.get_height ();
        scroll.vadjustment.value += y0 - vh * 0.7;
        GearTest.pump (200);
        y0 = GearTest.y_in (ar, scroll, out rh);

        gear.clicked ();
        GearTest.pump (700);

        if (Gtk.Window.get_toplevels ().get_n_items () != toplevels)
            GearTest.fail ("%s: the gear opened a window".printf (h.id));
        open_gear = gear;
        // Recently used may have been rebuilt, so look the row up again from the options.
        HackOptionsBox? box = null;
        foreach (var w in GearTest.walk (settings))
            if (w is HackOptionsBox && w.get_mapped ())
                box = (HackOptionsBox) w;
        var above = box != null ? box.get_parent ().get_prev_sibling () as ActionRow : null;
        if (box == null || !(box.get_parent () is Gtk.Revealer) || above == null || above.title != h.title) {
            GearTest.fail ("%s: no options directly under the row (box=%s above=%s)".printf (
                           h.id, box != null ? "mapped" : "none", above != null ? above.title : "none"));
            continue;
        }
        ar = above;
        if (GearTest.mapped_panels (settings) != 1)
            GearTest.fail ("%s: %d option panels open".printf (h.id, GearTest.mapped_panels (settings)));
        if (!GearTest.has_content (box.panel))
            GearTest.fail ("%s: options panel has no option rows".printf (h.id));
        double bh;
        double y1 = GearTest.y_in (ar, scroll, out rh);
        double by = GearTest.y_in (box, scroll, out bh);
        // The row stays put unless the options need room, and never leaves the top.
        bool fits = by + bh <= vh + 1;
        bool row_ok = y1 >= -1 && y1 <= y0 + 1 && ((y0 - y1).abs () <= 1 || (by + bh - vh).abs () <= 1 || y1.abs () <= 1);
        bool in_view = by >= 0 && (fits || y1.abs () <= 1);
        if (!row_ok || !in_view)
            GearTest.fail ("%s: row y %.0f->%.0f, options y=%.0f h=%.0f viewport=%d".printf (
                           h.id, y0, y1, by, bh, vh));
        else
            stdout.printf ("PASS %-26s in place row y %.0f->%.0f options y=%.0f h=%.0f rows=%d\n",
                           h.id, y0, y1, by, bh, GearTest.row_count (box.panel));

        // One at a time: reopening the previous hack closes this one.
        if (last_gear != null && last_gear != gear) {
            last_gear.clicked ();
            GearTest.pump (700);
            open_gear = last_gear;
            if (GearTest.mapped_panels (settings) != 1 || box.get_mapped ())
                GearTest.fail ("%s: previous options stayed open".printf (h.id));
        }
        last_gear = gear;
    }

    // A second click on the same gear collapses it.
    if (open_gear != null) {
        GearTest.checks++;
        open_gear.clicked ();
        GearTest.pump (700);
        if (GearTest.mapped_panels (settings) != 0)
            GearTest.fail ("second gear click did not collapse the options");
        else
            stdout.printf ("PASS %-26s second click collapses\n", "toggle");
    }


    // The same options at the narrow widths of the multi-level Settings page.
    int[] widths = { 360, 420, 480 };
    foreach (int width in widths) {
        var host = GearTest.make_host (backend, width);
        foreach (var h in backend.hacks ()) {
            if (backend.options_for (h.id).is_empty)
                continue;
            var gear = GearTest.gear_for (host.settings, backend, h);
            if (gear == null)
                continue;
            var ar = gear.get_ancestor (typeof (ActionRow)) as ActionRow;
            var section = ar.get_parent ().get_ancestor (typeof (ExpanderRow)) as ExpanderRow;
            if (section != null)
                section.expanded = true;
            GearTest.pump (300);
            gear.clicked ();
            GearTest.pump (700);
            GearTest.checks++;
            string what = "%dpx %s".printf (width, h.id);
            string? shots = Environment.get_variable ("GEAR_SHOTS");
            if (shots != null && h.id == "blackhole_gles3") {
                // Bring the options to the top of the page and photograph them.
                double ry;
                double rh2;
                ry = GearTest.y_in (ar, host.scroll, out rh2);
                host.scroll.vadjustment.value += ry - 4;
                GearTest.pump (500);
                GearTest.save_shot (host, Path.build_filename (shots, "options-%dpx.png".printf (width)));
                host.scroll.vadjustment.value = 0;
                GearTest.pump (300);
                GearTest.save_shot (host, Path.build_filename (shots, "page-%dpx.png".printf (width)));
            }
            if (GearTest.overflow_offenders (host, what) == 0)
                stdout.printf ("PASS %-26s narrow %d px: no overflow\n", h.id, width);
            gear.clicked ();
            GearTest.pump (500);
        }
        host.win.close ();
    }

    // The Options button at the top of the page opens the selected screensaver's options
    // even when its style group is folded: the options end up on screen, the button says
    // Hide options, and a second press collapses them.
    {
        var host = GearTest.make_host (backend, 420);
        Gtk.Button? opt = null;
        foreach (var w in GearTest.walk (host.settings)) {
            var b = w as Gtk.Button;
            if (b != null && b.label == "Options" && b.get_mapped ())
                opt = b;
        }
        GearTest.checks++;
        if (opt == null) {
            GearTest.fail ("no Options button on the page");
        } else {
            opt.clicked ();
            GearTest.pump (1200);
            HackOptionsBox? box = null;
            foreach (var w in GearTest.walk (host.settings))
                if (w is HackOptionsBox && w.get_mapped ())
                    box = (HackOptionsBox) w;
            double bh = 0;
            double by = box != null ? GearTest.y_in (box, host.scroll, out bh) : double.NAN;
            if (box == null || by.is_nan () || by >= host.scroll.get_height () || by + bh <= 0)
                GearTest.fail ("Options button: the options are not on screen (y=%.0f, viewport %d)".printf (
                               by, host.scroll.get_height ()));
            else if (opt.label != "Hide options")
                GearTest.fail ("Options button reads \"%s\" while the options are open".printf (opt.label));
            else {
                opt.clicked ();
                GearTest.pump (800);
                if (GearTest.mapped_panels (host.settings) != 0 || opt.label != "Options")
                    GearTest.fail ("Options button: a second press did not collapse the options");
                else
                    stdout.printf ("PASS %-26s Options button opens in view and collapses\n", "options-button");
            }
        }
        host.win.close ();
    }

    // A hack whose options are all advanced (or that has none) would show a blank
    // basic view; the options must say why instead.
    {
        var empty_hack = new ScreensaverHack ("__no_options__", "No Options", "");
        var opts = new HackOptionsBox (backend, empty_hack);
        bool hint = false;
        foreach (var w in GearTest.walk (opts)) {
            var l = w as Gtk.Label;
            if (l != null && l.get_visible () && l.label != null && l.label.contains ("No basic options"))
                hint = true;
        }
        GearTest.checks++;
        if (!hint)
            GearTest.fail ("empty options did not show the Developer hint");
        else
            stdout.printf ("PASS %-26s Developer hint shown\n", "no-basic-options");
    }

    stdout.printf ("\ncatalog=%d with_options=%d without_options=%d checked=%d failures=%d\n",
                   (int) backend.hacks ().size, with_options, without, GearTest.checks, GearTest.failures);
    return GearTest.failures == 0 ? 0 : 1;
}
