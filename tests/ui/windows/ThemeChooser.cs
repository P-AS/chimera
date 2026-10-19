// Config > Theme on WINDOWS: does the window open on the real toolkit with every
// theme Chimera ships in its list, and is its drawing of Chimera in the colours
// of the theme PICKED - not of the theme that is on, which paints the window
// round it?
//
// tests/ui/run-ui-tests.sh asks the same of Mono under Xvfb
// (ThemeChooserFormTests). This asks the toolkit Chimera's users run. It loads
// the real frontend assembly, opens the window under Dark, picks four of the
// shipped themes in turn and reads, off a picture of the window, a pixel of
// every surface the drawing is made of. It photographs with PrintWindow, which
// renders the window into a bitmap of ours: no screen pixels are read, so no
// other window can be captured even by accident, and it works with the session
// locked.
//
// Run it with theme-chooser.sh. Windows only. Written for the csc.exe that comes
// with .NET Framework, which is C# 5: no string interpolation, no "is" patterns.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;

internal static class ThemeChooserCheck
{
	[DllImport("user32.dll")]
	private static extern int PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);

	private const uint PW_RENDERFULLCONTENT = 2;

	private static void Pump()
	{
		for (var i = 0; i < 12; i++) { Application.DoEvents(); Thread.Sleep(40); }
	}

	// the client area only, as LiveThemeSwitch.cs takes it
	private static Bitmap Shoot(Form f)
	{
		Pump();
		var whole = new Bitmap(f.Width, f.Height, PixelFormat.Format32bppArgb);
		using (var g = Graphics.FromImage(whole))
		{
			var hdc = g.GetHdc();
			try { PrintWindow(f.Handle, hdc, PW_RENDERFULLCONTENT); }
			finally { g.ReleaseHdc(hdc); }
		}
		var origin = f.PointToScreen(Point.Empty);
		var inset = new Rectangle(origin.X - f.Left, origin.Y - f.Top, f.ClientSize.Width, f.ClientSize.Height);
		inset.Intersect(new Rectangle(0, 0, whole.Width, whole.Height));
		var client = whole.Clone(inset, whole.PixelFormat);
		whole.Dispose();
		return client;
	}

	private static readonly ThemeColorRole[] Surfaces =
	{
		ThemeColorRole.WindowBackground, ThemeColorRole.MenuBackground, ThemeColorRole.MenuSelectedBackground,
		ThemeColorRole.ToolStripBackground, ThemeColorRole.StatusBarBackground, ThemeColorRole.InputBackground,
		ThemeColorRole.DisabledBackground, ThemeColorRole.ButtonBackground, ThemeColorRole.HeaderBackground,
		ThemeColorRole.Selection, ThemeColorRole.HoverBackground, ThemeColorRole.TasCurrentFrame,
		ThemeColorRole.TasGreenZone, ThemeColorRole.TasLagZone, ThemeColorRole.HexBackground,
	};

	// A pixel of the drawing painted with this role and nothing over it, found in
	// the drawing's own record of its parts, as ThemeChooserFormTests finds it.
	private static Point? PaintedWith(ThemePreview preview, ThemeColorRole role)
	{
		foreach (var part in preview.Parts)
		{
			var area = part.Item1;
			if (part.Item2 != role || area.Width < 8 || area.Height < 8) continue;
			for (var y = area.Top + 2; y <= area.Bottom - 2; y++)
			{
				for (var x = area.Left + 2; x <= area.Right - 2; x += 2)
				{
					var at = preview.ToControl(new PointF(x, y));
					// two pixels clear all round: drawn at a scale that is not whole, a
					// pixel next to an edge is a blend of the two colours either side
					var clear = true;
					for (var dy = -2; dy <= 2 && clear; dy++)
					{
						for (var dx = -2; dx <= 2 && clear; dx++)
						{
							if (preview.RoleAt(new Point(at.X + dx, at.Y + dy)) != role) clear = false;
						}
					}
					if (clear) return at;
				}
			}
		}
		return null;
	}

	private static int Main(string[] args)
	{
		Application.EnableVisualStyles();
		Application.SetCompatibleTextRenderingDefault(false);
		var themes = args[0];
		var picture = args.Length > 1 ? args[1] : "theme-chooser-windows.png";

		ThemeLibrary.UseDirectory(themes);
		ThemeLibrary.Reload();
		if (ThemeLibrary.Failures.Count != 0)
		{
			// get_Message(): the failure is a record, whose init-only properties C# 5 cannot read by name
			foreach (var f in ThemeLibrary.Failures) Console.WriteLine("FAIL  would not load: " + f.get_Message());
			return 1;
		}
		ThemeLibrary.Select("Dark");

		var bad = 0;
		var form = new ThemeChooserForm(delegate(string folder) { }, null);
		form.StartPosition = FormStartPosition.Manual;
		form.Location = new Point(40, 40);
		form.Show();
		Pump();

		Console.WriteLine(string.Format("listed: {0} of {1} themes", form.ListedNames.Count, ThemeLibrary.All.Count));
		if (form.ListedNames.Count != ThemeLibrary.All.Count || form.ListedNames.Count < 62) bad++;

		foreach (var name in new[] { "Aizome", "Hackerman", "Win98", "Bubblegum Pop" })
		{
			form.Pick(name);
			var theme = ThemeLibrary.Find(name);
			using (var shot = Shoot(form))
			{
				var offset = form.PointToClient(form.Preview.PointToScreen(Point.Empty));
				var wrong = 0;
				foreach (var role in Surfaces)
				{
					var at = PaintedWith(form.Preview, role);
					if (!at.HasValue)
					{
						Console.WriteLine("FAIL  " + name + ": nowhere in the drawing is " + role + " uncovered");
						wrong++;
						continue;
					}
					var seen = shot.GetPixel(offset.X + at.Value.X, offset.Y + at.Value.Y).ToArgb() & 0xFFFFFF;
					var want = theme[role].ToArgb() & 0xFFFFFF;
					if (seen != want)
					{
						Console.WriteLine(string.Format("FAIL  {0}: {1} is #{2:X6} in the picture, the theme says #{3:X6}", name, role, seen, want));
						wrong++;
					}
				}
				Console.WriteLine(string.Format("{0,-14} {1} of {2} surfaces in the theme's colours", name, Surfaces.Length - wrong, Surfaces.Length));
				bad += wrong;
				if (name == "Aizome") shot.Save(picture, ImageFormat.Png);
			}
		}

		if (ThemeLibrary.Current.Name != "Dark")
		{
			Console.WriteLine("FAIL  picking a theme in the list turned it on");
			bad++;
		}
		form.Accept();
		if (form.Chosen == null || form.Chosen.Name != "Bubblegum Pop")
		{
			Console.WriteLine("FAIL  OK did not answer with the theme picked");
			bad++;
		}
		Console.WriteLine(bad == 0 ? "PASS" : "FAIL");
		return bad == 0 ? 0 : 1;
	}
}
