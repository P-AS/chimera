using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Config &gt; Theme: the list, the drawing of Chimera in the theme picked, and
	/// the folder the files come from (docs/theming.md).
	/// </summary>
	[TestClass]
	public class ThemeChooserFormTests
	{
		[TestCleanup]
		public void BackToTheDefaults()
		{
			ThemeLibrary.UseDirectory("");
			ThemeLibrary.Reload();
			ThemeLibrary.Select(ThemeLibrary.FallbackThemeName);
		}

		private static string TempFolder()
		{
			var dir = Path.Combine(Path.GetTempPath(), $"chimera-theme-chooser-{Guid.NewGuid():N}");
			Directory.CreateDirectory(dir);
			return dir;
		}

		private static void WriteTheme(string folder, string file, string name, string basedOn = "Dark")
		{
			var theme = ThemeLibrary.Find(basedOn)!;
			File.WriteAllText(Path.Combine(folder, file), ThemeFile.Write(new(name, "", "a test", theme.IsDark, false, file, theme.ToArray())));
		}

		/// <summary>The repository's Themes folder, found by walking up for the file only the repository root has.</summary>
		private static string? ShippedFolder()
		{
			var dir = new DirectoryInfo(AppDomain.CurrentDomain.BaseDirectory);
			while (dir is not null && !File.Exists(Path.Combine(dir.FullName, "official-cores.json")))
			{
				dir = dir.Parent;
			}
			return dir is null ? null : Path.Combine(dir.FullName, "Themes");
		}

		[TestMethod]
		public void ItOpensOnTheThemeThatIsOnWithTheBuiltInsFirst()
		{
			var folder = TempFolder();
			try
			{
				WriteTheme(folder, "b.chimeraTheme.json", "Zebra");
				WriteTheme(folder, "a.chimeraTheme.json", "Aardvark");
				ThemeLibrary.UseDirectory(folder);
				ThemeLibrary.Reload();
				ThemeLibrary.Select("Dark");

				using ThemeChooserForm form = new(openFolder: static _ => { });
				CollectionAssert.AreEqual(new[] { "Light", "Dark", "Aardvark", "Zebra" }, form.ListedNames.ToArray(), "built in first, then the files by name - not by file name");
				Assert.IsNotNull(form.Picked);
				Assert.AreEqual("Dark", form.Picked.Name);
				Assert.AreEqual("Dark (in use)", form.TitleText);
				StringAssert.Contains(form.BylineText, "built into Chimera");
				Assert.AreSame(form.Picked, form.Preview.Previewed);
				StringAssert.Contains(form.StatusText, folder);
			}
			finally
			{
				Directory.Delete(folder, recursive: true);
			}
		}

		/// <summary>Picking is looking: the drawing changes, the theme that is on does not, and OK is what answers.</summary>
		[TestMethod]
		public void PickingDrawsAThemeAndOnlyOkChoosesIt()
		{
			ThemeLibrary.Select("Light");
			using ThemeChooserForm form = new(openFolder: static _ => { });
			form.Pick("Dark");
			Assert.IsNotNull(form.Preview.Previewed);
			Assert.AreEqual("Dark", form.Preview.Previewed.Name);
			Assert.AreEqual("Dark", form.TitleText, "not the one in use");
			Assert.AreEqual("Light", ThemeLibrary.Current.Name, "picking in the list turns nothing on");
			Assert.IsNull(form.Chosen);

			form.Accept();
			Assert.IsNotNull(form.Chosen);
			Assert.AreEqual("Dark", form.Chosen.Name);
			Assert.AreEqual(DialogResult.OK, form.DialogResult);
			Assert.AreEqual("Light", ThemeLibrary.Current.Name, "turning it on is the caller's, with the config");
		}

		/// <summary>The parts whose colour is checked on screen: every surface a theme is mostly made of.</summary>
		private static readonly ThemeColorRole[] Surfaces =
		[
			ThemeColorRole.WindowBackground, ThemeColorRole.MenuBackground, ThemeColorRole.MenuSelectedBackground,
			ThemeColorRole.ToolStripBackground, ThemeColorRole.StatusBarBackground, ThemeColorRole.InputBackground,
			ThemeColorRole.DisabledBackground, ThemeColorRole.ReadOnlyBackground, ThemeColorRole.AccentWarningBackground,
			ThemeColorRole.ButtonBackground, ThemeColorRole.HeaderBackground, ThemeColorRole.AlternateRowBackground,
			ThemeColorRole.Selection, ThemeColorRole.HoverBackground, ThemeColorRole.InactiveSelection,
			ThemeColorRole.RollColumnBackground, ThemeColorRole.TasCurrentFrame, ThemeColorRole.TasGreenZone,
			ThemeColorRole.TasLagZone, ThemeColorRole.TasMarker, ThemeColorRole.RollSelection,
			ThemeColorRole.MeterTrough, ThemeColorRole.HexBackground, ThemeColorRole.ShadedBackground,
		];

		/// <summary>
		/// A pixel of the control that the drawing says is painted with this role and
		/// nothing over it - found in the drawing rather than written down, so that a
		/// font a little wider somewhere does not move the test's points onto text.
		/// </summary>
		private static Point? PaintedWith(ThemePreview preview, ThemeColorRole role)
		{
			foreach (var part in preview.Parts.Where(p => p.Role == role && p.Area.Width >= 8 && p.Area.Height >= 8))
			{
				for (var y = part.Area.Top + 2; y <= part.Area.Bottom - 2; y++)
				{
					for (var x = part.Area.Left + 2; x <= part.Area.Right - 2; x += 2)
					{
						var at = preview.ToControl(new(x, y));
						// two pixels clear all round: drawn at a scale that is not whole (as on
						// Windows), a pixel next to an edge is a blend of the colours either side
						var clear = true;
						for (var dy = -2; dy <= 2 && clear; dy++)
						{
							for (var dx = -2; dx <= 2 && clear; dx++)
							{
								if (preview.RoleAt(at + new Size(dx, dy)) != role) clear = false;
							}
						}
						if (clear) return at;
					}
				}
			}
			return null;
		}

		/// <summary>
		/// What the drawing is FOR: every part is the picked theme's colour, for
		/// every theme Chimera ships. Read off the screen, as the user sees it; and
		/// the tooltip names the colour that is there.
		/// </summary>
		[TestMethod]
		public void TheDrawingIsInThePickedThemesColours()
		{
			var folder = ShippedFolder();
			var themes = ThemeLibrary.Read(folder is not null && Directory.Exists(folder) ? folder : null).Themes;
			Assert.IsTrue(themes.Count >= 2);

			using Form form = new()
			{
				FormBorderStyle = FormBorderStyle.None,
				StartPosition = FormStartPosition.Manual,
				Location = new(0, 0),
				// twice the drawing's size and a pixel round it, so every part lands on whole pixels
				ClientSize = new(1042, 802),
			};
			ThemePreview preview = new() { Dock = DockStyle.Fill };
			form.Controls.Add(preview);
			form.Show();

			List<string> wrong = new();
			var checkedPixels = 0;
			using Bitmap pixel = new(1, 1);
			using var g = Graphics.FromImage(pixel);
			foreach (var theme in themes)
			{
				preview.Previewed = theme;
				preview.Refresh();
				Application.DoEvents();
				foreach (var role in Surfaces)
				{
					if (PaintedWith(preview, role) is not { } where)
					{
						wrong.Add($"{theme.Name}: nowhere in the drawing is {role} uncovered");
						continue;
					}
					g.CopyFromScreen(preview.PointToScreen(where), Point.Empty, new(1, 1));
					var seen = pixel.GetPixel(0, 0).ToArgb() & 0xFFFFFF;
					var want = theme[role].ToArgb() & 0xFFFFFF;
					if (seen != want) wrong.Add($"{theme.Name}: {role} at {where} is #{seen:X6} on screen, the theme says #{want:X6}");
					checkedPixels++;
					Assert.AreEqual($"{role} {ThemeFile.FormatColor(theme[role])}", preview.Describe(where));
				}
			}
			Assert.AreEqual(0, wrong.Count, string.Join("\n", wrong.Take(30)));
			Assert.AreEqual(themes.Count * Surfaces.Length, checkedPixels);
			Assert.IsNull(preview.RoleAt(new(0, 0)), "the margin round the drawing is not a part of it");
		}

		/// <summary>A copy to edit is a theme of its own, and is never written over a file that is already there - that file may be somebody's work.</summary>
		[TestMethod]
		public void ACopyToEditNeverReplacesAFile()
		{
			var folder = TempFolder();
			try
			{
				WriteTheme(folder, "Dark copy.chimeraTheme.json", "Dark copy", basedOn: "Light");
				var before = File.ReadAllText(Path.Combine(folder, "Dark copy.chimeraTheme.json"));
				ThemeLibrary.UseDirectory(folder);
				ThemeLibrary.Reload();

				using ThemeChooserForm form = new(openFolder: static _ => { });
				form.Pick("Dark");
				var path = form.WriteCopy();

				Assert.AreEqual(Path.Combine(folder, "Dark copy 2.chimeraTheme.json"), path);
				Assert.AreEqual(before, File.ReadAllText(Path.Combine(folder, "Dark copy.chimeraTheme.json")), "the file that was there is untouched");
				Assert.IsNotNull(form.Picked);
				Assert.AreEqual("Dark copy 2", form.Picked.Name, "and the copy is what the list now shows");
				var copy = ThemeLibrary.Find("Dark copy 2")!;
				CollectionAssert.AreEqual(ThemeLibrary.Find("Dark")!.ToArray(), copy.ToArray());
				Assert.IsFalse(copy.FollowsDesktop);
				StringAssert.Contains(form.StatusText, path);
			}
			finally
			{
				Directory.Delete(folder, recursive: true);
			}
		}

		[TestMethod]
		public void ReloadFindsANewFileAndSaysWhichWouldNotLoad()
		{
			var folder = TempFolder();
			try
			{
				ThemeLibrary.UseDirectory(folder);
				ThemeLibrary.Reload();
				List<(string Text, string Title)> told = new();
				using ThemeChooserForm form = new(openFolder: static _ => { }, tell: (text, title) => told.Add((text, title)));
				Assert.IsFalse(form.StatusText.Contains("would not load"));

				WriteTheme(folder, "New.chimeraTheme.json", "Brand New");
				File.WriteAllText(Path.Combine(folder, "Broken.chimeraTheme.json"), "{ \"name\": \"Broken\" ");
				form.Reload();

				CollectionAssert.Contains(form.ListedNames.ToArray(), "Brand New");
				CollectionAssert.DoesNotContain(form.ListedNames.ToArray(), "Broken");
				StringAssert.Contains(form.StatusText, "1 file would not load");
			}
			finally
			{
				Directory.Delete(folder, recursive: true);
			}
		}

		[TestMethod]
		public void OpenThemesFolderOpensTheFolderThemesAreReadFrom()
		{
			var folder = Path.Combine(TempFolder(), "not yet there");
			try
			{
				ThemeLibrary.UseDirectory(folder);
				ThemeLibrary.Reload();
				List<string> opened = new();
				using ThemeChooserForm form = new(openFolder: opened.Add);
				// a button only answers a click while its window is up
				form.Show();
				form.Controls.OfType<Button>().Single(static b => b.Text == "Open Themes Folder").PerformClick();
				CollectionAssert.AreEqual(new[] { folder }, opened);
				Assert.IsTrue(Directory.Exists(folder), "a folder that is not there yet is made, so there is somewhere to put a theme");
			}
			finally
			{
				Directory.Delete(Path.GetDirectoryName(folder)!, recursive: true);
			}
		}
	}
}
