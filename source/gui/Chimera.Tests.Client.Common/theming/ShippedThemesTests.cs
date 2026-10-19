using System.Collections.Generic;
using System.IO;
using System.Linq;

using Chimera.Client.Common;
using Chimera.Common.PathExtensions;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// The themes Chimera ships, in the repository's Themes folder, and where the
	/// frontend looks for themes (docs/theming.md).
	///
	/// A shipped theme is taken as its author made it: the readability floor in
	/// <see cref="ThemeContrastTests"/> holds for Light and Dark, which are
	/// Chimera's own, and not for these (user-decided, 2026-10-19). What IS held
	/// here is that every one of them loads - a file the frontend refused would be
	/// a theme in the folder that the window never offers, which nobody would
	/// report because nobody would know it was missing.
	/// </summary>
	[TestClass]
	public class ShippedThemesTests
	{
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
		public void EveryShippedThemeLoads()
		{
			var folder = ShippedFolder();
			if (folder is null || !Directory.Exists(folder))
			{
				Assert.Inconclusive("the repository is not beside this assembly, so its Themes folder cannot be read");
				return;
			}
			var files = Directory.GetFiles(folder);
			Assert.IsTrue(files.Length >= 60, $"only {files.Length} files in {folder}");

			var (themes, failures) = ThemeLibrary.Read(folder);
			Assert.AreEqual(0, failures.Count, string.Join("\n", failures.Select(static f => f.Message)));
			// two built in, and one theme for every file: none taken for another's name
			Assert.AreEqual(files.Length + 2, themes.Count);

			List<string> wrong = new();
			foreach (var file in files)
			{
				var name = Path.GetFileName(file);
				// a zip carries a name that is not ASCII in a way not every unzipper reads back
				if (name.Any(static c => c > 127)) wrong.Add($"{name}: the file name is not plain ASCII");
				if (!name.EndsWith(ThemeFile.Extension, StringComparison.Ordinal)) wrong.Add($"{name}: a theme file ends in {ThemeFile.Extension}");
			}
			foreach (var theme in themes.Where(static t => !ThemeLibrary.IsBuiltIn(t)))
			{
				if (string.IsNullOrWhiteSpace(theme.Author)) wrong.Add($"{theme.Name}: a theme Chimera ships says who made it");
			}
			Assert.AreEqual(0, wrong.Count, string.Join("\n", wrong));
		}

		/// <summary>
		/// The Themes path in Config &gt; Paths is, by default, the folder the bundle
		/// puts the themes in - beside Chimera.exe, like Cores, Lua and Tools - and
		/// the library's own default is the same folder, so a program that never
		/// read a config (a tool, a test) looks where the frontend does.
		/// </summary>
		[TestMethod]
		public void ThePathsDefaultIsTheFolderBesideTheProgram()
		{
			Config config = new();
			config.ResolveDefaults();
			var expected = Path.GetFullPath(Path.Combine(PathUtils.ExeDirectoryPath, "Themes"));
			Assert.AreEqual(expected, config.PathEntries.ThemesAbsolutePath());
			Assert.AreEqual(expected, Path.GetFullPath(ThemeLibrary.DefaultThemesDirectory));
		}

		private static void WriteTheme(string folder, string file, string name)
		{
			Directory.CreateDirectory(folder);
			var theme = ThemeLibrary.Find("Dark")!;
			File.WriteAllText(Path.Combine(folder, file), ThemeFile.Write(new(name, "", "a test", true, false, file, theme.ToArray())));
		}

		/// <summary>
		/// The folder in Config &gt; Paths is read, and so is the one themes were kept
		/// in before it existed, so that a theme somebody wrote there is not lost.
		/// The same name in both is the configured folder's, and the other file is
		/// reported rather than silently dropped.
		/// </summary>
		[TestMethod]
		public void TheChosenFolderIsReadAndTheFormerOneAfterIt()
		{
			var root = Path.Combine(Path.GetTempPath(), $"chimera-themes-{Guid.NewGuid():N}");
			var chosen = Path.Combine(root, "chosen");
			var dataHome = Path.Combine(root, "data");
			// the data directory is pointed at the test's own through the variable, which
			// wins over everything else and is read on every ask (ProjectCache.DataHome)
			var oldDataHome = Environment.GetEnvironmentVariable("CHIMERA_DATA_HOME");
			try
			{
				WriteTheme(chosen, "Here.chimeraTheme.json", "Here");
				WriteTheme(chosen, "Both.chimeraTheme.json", "Both");
				WriteTheme(Path.Combine(dataHome, "Themes"), "Before.chimeraTheme.json", "Kept From Before");
				WriteTheme(Path.Combine(dataHome, "Themes"), "Both.chimeraTheme.json", "Both");
				Environment.SetEnvironmentVariable("CHIMERA_DATA_HOME", dataHome);

				ThemeLibrary.UseDirectory(chosen);
				ThemeLibrary.Reload();

				Assert.AreEqual(Path.GetFullPath(chosen), Path.GetFullPath(ThemeLibrary.ThemesDirectory));
				var names = ThemeLibrary.All.Select(static t => t.Name).ToList();
				CollectionAssert.Contains(names, "Here");
				CollectionAssert.Contains(names, "Kept From Before");
				Assert.AreEqual(1, names.Count(static n => n == "Both"));
				var failure = ThemeLibrary.Failures.Single();
				StringAssert.StartsWith(Path.GetFullPath(failure.Path), Path.GetFullPath(dataHome));
				StringAssert.Contains(failure.Message, "already a theme called \"Both\"");

				// the same folder again is not a reason to read anything; another one is
				Assert.IsFalse(ThemeLibrary.UseDirectory(chosen));
				Assert.IsTrue(ThemeLibrary.UseDirectory(Path.Combine(root, "elsewhere")));
				CollectionAssert.DoesNotContain(ThemeLibrary.All.Select(static t => t.Name).ToList(), "Here");
			}
			finally
			{
				Environment.SetEnvironmentVariable("CHIMERA_DATA_HOME", oldDataHome);
				ThemeLibrary.UseDirectory("");
				ThemeLibrary.Reload();
				ThemeLibrary.Select(ThemeLibrary.FallbackThemeName);
				try { Directory.Delete(root, recursive: true); } catch (IOException) { }
			}
		}
	}
}
