using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;
using Chimera.Emulation.Common;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Drives Config > Firmware over surveyed groups. The verdicts are decided
	/// (and tested) in <c>FirmwareSurvey</c>; what is left here is that the window
	/// shows them, folds a long list of releases to the ones on hand, and that
	/// choosing and clearing a file reach the owner.
	/// </summary>
	[TestClass]
	public class FirmwareSurveyFormTests
	{
		private static FirmwareSurveyRow Row(string core, string id, string? label, CoreFirmwareState state, string? sha1 = null, string? path = null)
			=> new()
			{
				CoreName = core,
				Decl = new() { Id = id, Display = "Console BIOS", Label = label, Size = 4096, Sha1 = sha1 },
				State = state,
				Path = path,
				Where = path is null ? FirmwareWhere.Nowhere : FirmwareWhere.Chosen,
			};

		private static FirmwareSurveyGroup Ps2(int releases, int onHand)
		{
			List<FirmwareSurveyRow> rows = new();
			for (var i = 0; i < releases; i++)
			{
				var found = i < onHand;
				rows.Add(Row("PCSX2", "bios.bin", $"release {i}", found ? CoreFirmwareState.Good : CoreFirmwareState.Missing,
					sha1: new string((char)('A' + i % 6), 40), path: found ? $"/dumps/r{i}.bin" : null));
			}
			return new() { CoreName = "PCSX2", Rows = rows };
		}

		private sealed class Harness : IDisposable
		{
			// a clear remembers a null path, which is the whole point of it
			internal readonly List<(FirmwareSurveyRow Row, string? Path)> Remembered = new();

			/// <summary>what the file picker answers next; null is a cancelled pick</summary>
			internal string? NextPick;
			internal IReadOnlyList<FirmwareSurveyGroup> Groups;
			internal readonly FirmwareSurveyForm Form;

			internal Harness(params FirmwareSurveyGroup[] groups)
			{
				Groups = groups;
				Form = new(() => Groups, (row, path) => Remembered.Add((row, path)), _ => NextPick);
				Form.Show();
			}

			public void Dispose() => Form.Dispose();
		}

		private static ListView ListOf(Form form) => form.Controls.OfType<ListView>().Single();

		private static FirmwareSurveyGroup Xemu(string? mcpxPath = "/fw/mcpx_1.0.bin")
			=> new() { CoreName = "xemu", Rows = [ Row("xemu", "mcpx", null, CoreFirmwareState.Good, path: mcpxPath), Row("xemu", "hdd", null, CoreFirmwareState.Missing) ] };

		/// <summary>Issue 368: a long list narrowed by a Find box, on a found file's name or a core's.</summary>
		[TestMethod]
		public void FindNarrowsTheListToWhatMatches()
		{
			using Harness h = new(Xemu(), new FirmwareSurveyGroup { CoreName = "Stella", Rows = [ ] });

			h.Form.FindText = "mcpx_1.0";
			Assert.AreEqual(1, h.Form.DisplayedRows.Count);
			Assert.AreEqual("mcpx", h.Form.DisplayedRows[0].Decl.Id);
			Assert.AreEqual(1, ListOf(h.Form).Groups.Count, "a core with nothing that matches is not listed");

			h.Form.FindText = "STELLA";
			Assert.AreEqual(1, ListOf(h.Form).Groups.Count, "a core's name, in any case");
			StringAssert.StartsWith(ListOf(h.Form).Groups[0].Header, "Stella");

			h.Form.FindText = "";
			Assert.AreEqual(2, h.Form.DisplayedRows.Count);
			Assert.AreEqual(2, ListOf(h.Form).Groups.Count);
		}

		/// <summary>Issue 368: a core's files fold to one line and open again; all of them at once too.</summary>
		[TestMethod]
		public void ACoreFoldsToOneLineAndOpensAgain()
		{
			using Harness h = new(Xemu(), Ps2(releases: 3, onHand: 1));
			var list = ListOf(h.Form);

			h.Form.Collapse("xemu");
			Assert.IsFalse(h.Form.DisplayedRows.Any(static r => r.CoreName == "xemu"));
			var xemuLines = list.Items.Cast<ListViewItem>().Where(i => i.Group == list.Groups[0]).ToList();
			Assert.AreEqual(1, xemuLines.Count, "one line for the folded core");
			StringAssert.Contains(xemuLines[0].SubItems[1].Text, "xemu: 2 files");
			Assert.AreEqual(3, h.Form.DisplayedRows.Count, "the other core is still open");

			h.Form.Expand("xemu");
			Assert.AreEqual(2, h.Form.DisplayedRows.Count(static r => r.CoreName == "xemu"));

			h.Form.CollapseAll();
			Assert.AreEqual(0, h.Form.DisplayedRows.Count);
			h.Form.ExpandAll();
			Assert.AreEqual(5, h.Form.DisplayedRows.Count);
		}

		/// <summary>
		/// The window photographed in every theme, one core folded (the line a folded
		/// core leaves), so its new Find box and folded line are looked at too.
		/// </summary>
		[TestMethod]
		public void TheWindowInEveryTheme()
		{
			using Harness h = new(Xemu(), Ps2(releases: 3, onHand: 1), new FirmwareSurveyGroup { CoreName = "Stella", Rows = [ ] });
			h.Form.Collapse("PCSX2");
			UiShots.Shoot(h.Form, "firmware-survey");
		}

		/// <summary>Issue 368: Open Folder shows where a found file is, the file selected.</summary>
		[TestMethod]
		public void OpenFolderShowsTheFoundFileInItsFolder()
		{
			var dir = Path.Combine(Path.GetTempPath(), "chimera-fw-open-" + Guid.NewGuid().ToString("N"));
			Directory.CreateDirectory(dir);
			try
			{
				var file = Path.Combine(dir, "mcpx_1.0.bin");
				File.WriteAllText(file, "x");
				List<(string Folder, string? File)> opened = new();
				using FirmwareSurveyForm form = new(() => [ Xemu(file) ], (_, _) => { }, _ => null, openFolder: (folder, f) => opened.Add((folder, f)));
				form.Show();
				var list = ListOf(form);

				list.Items[0].Selected = true;
				form.OpenSelectedFolder();
				Assert.AreEqual(1, opened.Count);
				Assert.AreEqual(Path.GetFullPath(dir), opened[0].Folder);
				Assert.AreEqual(Path.GetFullPath(file), opened[0].File);

				list.Items[1].Selected = true;
				form.OpenSelectedFolder();
				Assert.AreEqual(1, opened.Count, "a row with no file found opens nothing");

				File.Delete(file);
				var gone = FirmwareSurveyForm.OpenFolderTarget(Row("xemu", "mcpx", null, CoreFirmwareState.Unreadable, path: file));
				Assert.AreEqual(Path.GetFullPath(dir), gone?.Folder, "a file gone still has its folder");
				Assert.IsNull(gone?.File);
				Assert.IsNull(FirmwareSurveyForm.OpenFolderTarget(Row("xemu", "mcpx", null, CoreFirmwareState.Unreadable, path: Path.Combine(dir, "gone", "x.bin"))));
			}
			finally
			{
				Directory.Delete(dir, recursive: true);
			}
		}

		[TestMethod]
		public void EveryCoreIsAGroupAndEveryDeclarationARow()
		{
			using Harness h = new(
				new FirmwareSurveyGroup { CoreName = "xemu", Rows = [ Row("xemu", "mcpx", null, CoreFirmwareState.Good, path: "/fw/mcpx_1.0.bin"), Row("xemu", "hdd", null, CoreFirmwareState.Missing) ] },
				new FirmwareSurveyGroup { CoreName = "Stella", Rows = [ ] });
			var list = ListOf(h.Form);
			Assert.AreEqual(2, list.Groups.Count);
			StringAssert.StartsWith(list.Groups[0].Header, "xemu");
			Assert.AreEqual(2, h.Form.DisplayedRows.Count);
			Assert.AreEqual(3, list.Items.Count, "the core that needs nothing still says so");
		}

		[TestMethod]
		public void EveryCoresFilesInOneListThatTheShowChoiceNarrows()
		{
			// docs/game-cores.md: no divider; Show: All / Emulators / Games (user-decided, 2026-09-29)
			using Harness h = new(
				new FirmwareSurveyGroup { CoreName = "xemu", Rows = [ Row("xemu", "mcpx", null, CoreFirmwareState.Good, path: "/fw/mcpx_1.0.bin") ] },
				new FirmwareSurveyGroup { CoreName = "SDLPoP", IsGameCore = true, Rows = [ Row("SDLPoP", "PRINCE.DAT", null, CoreFirmwareState.Missing) ] });
			var list = ListOf(h.Form);
			Assert.AreEqual(2, list.Items.Count, "a row for each file, and no divider");
			CollectionAssert.AreEqual(new[] { "xemu", "SDLPoP" }, list.Groups.Cast<ListViewGroup>().Select(static g => g.Header.Split(' ')[0]).ToArray());

			var shows = h.Form.Controls.OfType<CoreKindFilterBox>().Single();
			shows.ChooseForTest(CoreKindFilter.Games);
			Assert.AreEqual("PRINCE.DAT", h.Form.DisplayedRows.Single().Decl.Id);
			shows.ChooseForTest(CoreKindFilter.Emulators);
			Assert.AreEqual("mcpx", h.Form.DisplayedRows.Single().Decl.Id);
			shows.ChooseForTest(CoreKindFilter.All);
			Assert.AreEqual(2, h.Form.DisplayedRows.Count);
		}

		[TestMethod]
		public void ALongListOfReleasesFoldsToTheOnesOnHand()
		{
			using Harness h = new(Ps2(releases: 73, onHand: 2));
			Assert.AreEqual(2, h.Form.DisplayedRows.Count, "only the dumps on hand are rows");
			var list = ListOf(h.Form);
			Assert.AreEqual(3, list.Items.Count, "plus the line saying how many more there are");
			StringAssert.Contains(list.Items[2].SubItems[1].Text, "71 more");

			var showAll = h.Form.Controls.OfType<CheckBox>().Single();
			showAll.Checked = true;
			Assert.AreEqual(73, h.Form.DisplayedRows.Count);
		}

		[TestMethod]
		public void AShortListIsNeverFolded()
		{
			using Harness h = new(Ps2(releases: 3, onHand: 0));
			Assert.AreEqual(3, h.Form.DisplayedRows.Count);
		}

		[TestMethod]
		public void SetFileAndClearReachTheOwner()
		{
			using Harness h = new(Ps2(releases: 2, onHand: 1));
			var list = ListOf(h.Form);
			list.Items[1].Selected = true;
			Assert.IsNotNull(h.Form.SelectedRow, "clicking a row selected nothing");
			Assert.AreEqual("release 1", h.Form.SelectedRow.Decl.Label);

			h.NextPick = "/somewhere/else.bin";
			h.Form.Controls.OfType<Button>().Single(static b => b.Text == "Set File...").PerformClick();
			Assert.AreEqual(1, h.Remembered.Count);
			Assert.AreEqual("release 1", h.Remembered[0].Row.Decl.Label);
			Assert.AreEqual("/somewhere/else.bin", h.Remembered[0].Path);

			list.Items[0].Selected = true;
			h.Form.Controls.OfType<Button>().Single(static b => b.Text == "Clear").PerformClick();
			Assert.AreEqual(2, h.Remembered.Count);
			Assert.IsNull(h.Remembered[1].Path, "a clear forgets");
			Assert.AreEqual("release 0", h.Remembered[1].Row.Decl.Label);
		}

		/// <summary>
		/// Rescan, Scan Folder and Set File build the list again; the list stays where the reader
		/// had scrolled it, rather than going back to its first row (issue 348).
		/// </summary>
		[TestMethod]
		public void ARescanKeepsTheListWhereItWasScrolled()
		{
			var groups = Enumerable.Range(0, 40)
				.Select(static i => new FirmwareSurveyGroup
				{
					CoreName = $"core{i:D2}",
					Rows = [ Row($"core{i:D2}", "bios", null, CoreFirmwareState.Missing), Row($"core{i:D2}", "disk", null, CoreFirmwareState.Missing) ],
				})
				.ToArray();
			using Harness h = new(groups);
			var list = ListOf(h.Form);
			list.EnsureVisible(list.Items.Count - 1);
			list.EnsureVisible(50);
			var before = h.Form.ScrollAnchor()!.Value;
			Assert.IsTrue(before.Index > 50, $"the test did not scroll the list down (row {before.Index} halfway down)");
			var anchorRow = h.Form.DisplayedRows[before.Index];

			h.Form.Controls.OfType<Button>().Single(static b => b.Text == "Rescan").PerformClick();
			Assert.AreEqual(before, h.Form.ScrollAnchor(), "a rescan moved the list");

			// a row fewer further up: the same row stays where it was drawn
			h.Groups = groups.Select(static g => g.CoreName == "core01"
				? new FirmwareSurveyGroup { CoreName = g.CoreName, Rows = [ g.Rows[0] ] }
				: g).ToArray();
			h.Form.Populate();
			var after = h.Form.ScrollAnchor()!.Value;
			Assert.AreEqual(before.Index - 1, after.Index, "not the same row halfway down");
			Assert.AreSame(anchorRow.Decl, h.Form.DisplayedRows[after.Index].Decl);
			Assert.AreEqual(before.Y, after.Y);
		}

		[TestMethod]
		public void APickThatIsCancelledChangesNothing()
		{
			using Harness h = new(Ps2(releases: 1, onHand: 0));
			ListOf(h.Form).Items[0].Selected = true;
			h.NextPick = null;
			h.Form.Controls.OfType<Button>().Single(static b => b.Text == "Set File...").PerformClick();
			Assert.AreEqual(0, h.Remembered.Count);
		}
	}
}
