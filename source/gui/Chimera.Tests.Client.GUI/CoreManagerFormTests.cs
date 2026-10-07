using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The Core Manager window over the model underneath it (CoreManagerModelTests).
	///
	/// Chimera downloads nothing (user-decided, 2026-10-07): the window lists the
	/// packages in the cores folder, says which folder that is and how a core gets
	/// there, lets the folder be opened or changed, and removes what is ticked.
	/// What is checked here is that wiring - and that there is no button left that
	/// would fetch anything.
	/// </summary>
	[TestClass]
	public class CoreManagerFormTests
	{
		private static DiscoveredCorePackage Package(string name, string version, bool game = false, string? path = null, DateTimeOffset? built = null)
			=> new()
			{
				Name = name,
				Version = version,
				Path = path ?? $"/cores/{name}-{version}.chimeraCore",
				Sha1 = new string('a', 40),
				IsGameCore = game,
				VersionDate = built,
				Systems = [ "SYS" ],
			};

		private static readonly IReadOnlyList<DiscoveredCorePackage> OneCore = [ Package("Genesis Plus GX", "aaaaaaaa") ];

		private static ListView ListOf(Form form) => form.Controls.OfType<ListView>().Single();

		private static CheckBox SelectAllOf(Form form)
			=> form.Controls.OfType<CheckBox>().Single(static box => box.Text.StartsWith("Select all", StringComparison.Ordinal));

		private static CoreKindFilterBox ShowsOf(Form form) => form.Controls.OfType<CoreKindFilterBox>().Single();

		private static List<string> Buttons(Control root)
			=> root.Controls.Cast<Control>().SelectMany(static c => c is Button b ? new[] { b.Text } : Buttons(c).ToArray()).ToList();

		private static Button ButtonOf(Form form, string text) => form.Controls.OfType<Button>().Single(b => b.Text == text);

		/// <summary>A column of every listed row, by the column's header.</summary>
		private static List<string> Column(ListView list, string header)
		{
			var at = list.Columns.Cast<ColumnHeader>().ToList().FindIndex(c => c.Text == header);
			return list.Items.Cast<ListViewItem>().Select(i => i.SubItems[at].Text).ToList();
		}

		private static ComboBox VersionsOf(Form form)
			=> form.Controls.OfType<Panel>().SelectMany(static p => p.Controls.OfType<ComboBox>()).Single();

		private static CoreManagerForm Open(
			IReadOnlyList<DiscoveredCorePackage> packages,
			string folder = "/cores",
			CoreKindFilter shows = CoreKindFilter.All,
			Action<CoreKindFilter>? rememberShows = null)
			=> new(() => packages, () => folder, shows: shows, rememberShows: rememberShows);

		[TestMethod]
		public void NothingInTheWindowFetchesAnything()
		{
			using var form = Open(OneCore);
			form.Show();
			CollectionAssert.AreEquivalent(
				new[] { "Open cores folder", "Change folder...", "Look again", "Remove", "Systems...", "Use that folder", "Remove version", "Close" },
				Buttons(form),
				"no Check for updates, no Download latest, no Install, no Add external core");
			Assert.IsFalse(form.Controls.OfType<CheckBox>().Any(static b => b.Text.Contains("development")), "and no channel to choose a download from");
			StringAssert.Contains(form.HeaderText, "Chimera downloads nothing");
			StringAssert.Contains(form.HeaderText, "/cores", "the window says which folder a core goes in");
		}

		[TestMethod]
		public void TheListIsThePackagesInTheFolder()
		{
			var older = new DateTimeOffset(2026, 9, 1, 5, 0, 0, TimeSpan.Zero);
			var newer = new DateTimeOffset(2026, 9, 5, 5, 0, 0, TimeSpan.Zero);
			using var form = Open([ Package("Genesis Plus GX", "aaaaaaaaaaaa", built: older), Package("Genesis Plus GX", "bbbbbbbbbbbb", built: newer), Package("Ares", "cccccccc") ]);
			form.Show();
			var list = ListOf(form);
			CollectionAssert.AreEqual(new[] { "Ares", "Genesis Plus GX" }, list.Items.Cast<ListViewItem>().Select(static i => i.Text).ToList(), "one core is one row");
			StringAssert.Contains(Column(list, "Version")[1], "(+1 more)");
			StringAssert.Contains(form.HeaderText, "(2 core(s))");

			Assert.IsTrue(form.Select("Genesis Plus GX"));
			var versions = VersionsOf(form).Items.Cast<object>().Select(static o => o.ToString()!).ToList();
			Assert.AreEqual(2, versions.Count, "every version that is here, and none that is not");
			StringAssert.Contains(versions[0], "bbbbbbbb", "newest first (issue #67)");
			StringAssert.Contains(versions[0], CoreVersionDates.Format(newer), "with the date the package states");
			StringAssert.Contains(versions[1], "aaaaaaaa");
		}

		[TestMethod]
		public void AnEmptyFolderSaysSo()
		{
			using var form = Open([ ]);
			form.Show();
			Assert.AreEqual(0, ListOf(form).Items.Count, "there is no list of cores that exist somewhere else");
			StringAssert.Contains(form.StatusText, "no cores in this folder");
			Assert.IsFalse(form.OffersTheFormerFolder);
		}

		[TestMethod]
		public void ConstructingTheWindowSurvivesTheListRaisingEvents()
		{
			// A ListView raises ItemChecked while its handle is created, which on
			// .NET Framework happens INSIDE the constructor, before the buttons the
			// handler enables exist. That crashed on Windows the first time the
			// window opened, with a NullReferenceException nothing on Linux showed.
			// Forcing the handle and then ticking exercises the same order.
			using var form = Open(OneCore);
			_ = form.Handle;
			form.Show();
			Assert.IsTrue(form.SetChecked("Genesis Plus GX", true));
			Assert.IsTrue(form.BulkActionsEnabled);
		}

		[TestMethod]
		public void RemoveWaitsUntilSomethingIsTicked()
		{
			using var form = Open(OneCore);
			form.Show();
			Assert.IsFalse(form.BulkActionsEnabled, "nothing ticked, so there is nothing for it to do");
			Assert.IsTrue(form.SetChecked("Genesis Plus GX", true));
			Assert.IsTrue(form.BulkActionsEnabled);
			Assert.IsTrue(form.SetChecked("Genesis Plus GX", false));
			Assert.IsFalse(form.BulkActionsEnabled);
		}

		[TestMethod]
		public void OneListWithATypeThatTheShowChoiceNarrows()
		{
			// docs/game-cores.md: no dividers; a Type column, and Show: All / Emulators / Games
			// (user-decided, 2026-09-29)
			List<CoreKindFilter> remembered = new();
			using var form = Open(
				[ Package("SDLPoP", "1", game: true), Package("Genesis Plus GX", "1"), Package("Zork", "1", game: true), Package("Aardvark", "1") ],
				rememberShows: remembered.Add);
			form.Show();
			var list = ListOf(form);
			CollectionAssert.AreEqual(new[] { "Aardvark", "Genesis Plus GX", "SDLPoP", "Zork" }, list.Items.Cast<ListViewItem>().Select(static i => i.Text).ToList(),
				"the emulators, then the games, each by name");
			CollectionAssert.AreEqual(new[] { "Emulator", "Emulator", "Game", "Game" }, Column(list, "Type"));

			// everything ticked, then only the games shown: the emulators' ticks go with them,
			// so Remove never acts on a core out of view
			SelectAllOf(form).Checked = true;
			ShowsOf(form).ChooseForTest(CoreKindFilter.Games);
			CollectionAssert.AreEqual(new[] { "SDLPoP", "Zork" }, list.Items.Cast<ListViewItem>().Select(static i => i.Text).ToList());
			CollectionAssert.AreEqual(new[] { CoreKindFilter.Games }, remembered, "the choice is handed to the owner to keep");
			ShowsOf(form).ChooseForTest(CoreKindFilter.All);
			CollectionAssert.AreEqual(new[] { "SDLPoP", "Zork" }, list.Items.Cast<ListViewItem>().Where(static i => i.Checked).Select(static i => i.Text).ToList());

			ShowsOf(form).ChooseForTest(CoreKindFilter.Emulators);
			SelectAllOf(form).Checked = false;
			SelectAllOf(form).Checked = true;
			CollectionAssert.AreEqual(new[] { "Aardvark", "Genesis Plus GX" }, list.Items.Cast<ListViewItem>().Where(static i => i.Checked).Select(static i => i.Text).ToList(), "select all ticks what is shown");
		}

		[TestMethod]
		public void TheWindowOpensOnTheKindItWasLeftOn()
		{
			using var form = Open([ Package("SDLPoP", "1", game: true), Package("Genesis Plus GX", "1") ], shows: CoreKindFilter.Games);
			form.Show();
			Assert.AreEqual(CoreKindFilter.Games, ShowsOf(form).Value);
			CollectionAssert.AreEqual(new[] { "SDLPoP" }, ListOf(form).Items.Cast<ListViewItem>().Select(static i => i.Text).ToList());
		}

		[TestMethod]
		public void TheFolderCanBeOpenedAndChanged()
		{
			var folder = "/cores";
			Dictionary<string, IReadOnlyList<DiscoveredCorePackage>> holds = new()
			{
				["/cores"] = [ ],
				["/elsewhere"] = [ Package("Ares", "1"), Package("Genesis Plus GX", "1") ],
			};
			List<string> opened = new();
			var changes = 0;
			using CoreManagerForm form = new(
				() => holds[folder],
				() => folder,
				useFolder: chosen => folder = chosen,
				askForFolder: () => "/elsewhere",
				openFolder: opened.Add,
				changed: () => changes++);
			form.Show();
			Assert.AreEqual(0, ListOf(form).Items.Count);

			ButtonOf(form, "Change folder...").PerformClick();
			Assert.AreEqual("/elsewhere", folder, "the owner is told the folder somebody chose");
			Assert.AreEqual(2, ListOf(form).Items.Count, "and what is in it is listed at once");
			StringAssert.Contains(form.HeaderText, "/elsewhere");
			Assert.AreEqual(1, changes, "the session is told, so the cores can be used without a restart");

			using CoreManagerForm fixedFolder = new(() => OneCore, () => "/cores");
			fixedFolder.Show();
			Assert.IsFalse(ButtonOf(fixedFolder, "Change folder...").Enabled, "with nobody to tell, the folder cannot be changed from here");
			Assert.IsFalse(ButtonOf(fixedFolder, "Open cores folder").Enabled);
		}

		[TestMethod]
		public void CoresLeftWhereEarlierVersionsDownloadedThemAreOffered()
		{
			// somebody arriving from a Chimera that downloaded cores has them in the data
			// directory, which is no longer searched: the window says so and offers the folder
			var folder = "/cores";
			using CoreManagerForm form = new(
				() => folder == "/data/Cores" ? [ Package("Ares", "1"), Package("Genesis Plus GX", "1") ] : [ ],
				() => folder,
				useFolder: chosen => folder = chosen,
				former: () => ("/data/Cores", folder == "/data/Cores" ? 0 : 2));
			form.Show();
			Assert.IsTrue(form.OffersTheFormerFolder);
			StringAssert.Contains(form.StatusText, "2 core package(s) are in /data/Cores");

			ButtonOf(form, "Use that folder").PerformClick();
			Assert.AreEqual("/data/Cores", folder);
			Assert.AreEqual(2, ListOf(form).Items.Count);
			Assert.IsFalse(form.OffersTheFormerFolder, "once it is the cores folder there is nothing left to offer");
		}

		[TestMethod]
		public void LookingAgainFindsWhatWasPutInTheFolderMeanwhile()
		{
			List<DiscoveredCorePackage> packages = new();
			var changes = 0;
			using CoreManagerForm form = new(() => packages, () => "/cores", changed: () => changes++);
			form.Show();
			Assert.AreEqual(0, ListOf(form).Items.Count);
			packages.Add(Package("Ares", "1"));
			ButtonOf(form, "Look again").PerformClick();
			Assert.AreEqual(1, ListOf(form).Items.Count, "a package copied in while the window is open");
			Assert.AreEqual(1, changes);
		}
	}
}
