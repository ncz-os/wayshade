// Headless regression test for the per-screensaver Options ("gear") button.
// Clicks the gear of every catalog hack against the real engine schemas and
// asserts the options surface is visible and non-empty (a dialog after the fix,
// otherwise the old inline panel, whose off-viewport position is reported).
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

    Gtk.Window? visible_dialog (string title) {
        var model = Gtk.Window.get_toplevels ();
        for (uint i = 0; i < model.get_n_items (); i++) {
            var w = model.get_item (i) as Gtk.Window;
            if (w != null && w.title == "%s options".printf (title) && w.get_mapped ())
                return w;
        }
        return null;
    }

    void pump (uint ms) {
        var loop = new MainLoop ();
        Timeout.add (ms, () => { loop.quit (); return false; });
        loop.run ();
    }

    void fail (string msg) {
        failures++;
        stdout.printf ("FAIL: %s\n", msg);
    }
}

int main (string[] args) {
    Gtk.init ();

    var backend = create_screensaver_backend ();
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
    win.set_default_size (700, 800);
    win.set_child (scroll);
    win.present ();
    GearTest.pump (400);

    int with_options = 0;
    int without = 0;
    foreach (var h in backend.hacks ()) {
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
        gear.clicked ();
        GearTest.pump (300);

        var dlg = GearTest.visible_dialog (h.title);
        if (dlg != null) {
            var panel = GearTest.find_panel (dlg);
            if (panel == null)
                GearTest.fail ("%s: dialog opened without an options panel".printf (h.id));
            else if (!GearTest.has_content (panel))
                GearTest.fail ("%s: dialog panel has no option rows".printf (h.id));
            else if (dlg.get_height () <= 0)
                GearTest.fail ("%s: dialog has zero height".printf (h.id));
            else
                stdout.printf ("PASS %-26s dialog %dx%d rows=%d\n", h.id,
                               dlg.get_width (), dlg.get_height (), GearTest.row_count (panel));
            dlg.close ();
            continue;
        }

        // Before the fix there was no dialog: measure the inline panel in the page.
        var inline = GearTest.find_panel (settings);
        if (inline == null) {
            GearTest.fail ("%s: gear click produced no options surface".printf (h.id));
            continue;
        }
        if (!GearTest.has_content (inline))
            GearTest.fail ("%s: inline panel has no option rows".printf (h.id));
        var bounds = Graphene.Rect ();
        bool ok = inline.compute_bounds (scroll, out bounds);
        int vh = scroll.get_height ();
        bool visible = ok && bounds.origin.y + bounds.size.height > 0 && bounds.origin.y < vh;
        if (!visible)
            GearTest.fail ("%s: inline panel is off-screen (y=%.0f viewport=%d)".printf (
                           h.id, ok ? bounds.origin.y : -1.0, vh));
        else
            stdout.printf ("PASS %-26s inline y=%.0f viewport=%d\n", h.id, bounds.origin.y, vh);
    }

    // A hack whose options are all advanced (or that has none) would show a blank
    // basic view; the dialog must say why instead.
    {
        var empty_hack = new ScreensaverHack ("__no_options__", "No Options", "");
        var dlg = new HackOptionsDialog (win, backend, empty_hack);
        dlg.present ();
        GearTest.pump (200);
        bool hint = false;
        foreach (var w in GearTest.walk (dlg)) {
            var l = w as Gtk.Label;
            if (l != null && l.get_visible () && l.label != null && l.label.contains ("No basic options"))
                hint = true;
        }
        GearTest.checks++;
        if (!hint)
            GearTest.fail ("empty options dialog did not show the Developer hint");
        else
            stdout.printf ("PASS %-26s Developer hint shown\n", "no-basic-options");
        dlg.close ();
    }

    stdout.printf ("\ncatalog=%d with_options=%d without_options=%d checked=%d failures=%d\n",
                   (int) backend.hacks ().size, with_options, without, GearTest.checks, GearTest.failures);
    return GearTest.failures == 0 ? 0 : 1;
}
