#nullable enable

using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// File &gt; Core Manager: the core packages that are in the cores folder, and
	/// the versions of each.
	///
	/// Chimera ships no cores and downloads none (user-decided, 2026-10-07; see
	/// docs/core-manager.md). A core is a file somebody downloaded from its project
	/// or built, and put in the cores folder; this window says which folder that
	/// is, lets it be opened or changed, lists what is in it, and removes what is
	/// no longer wanted. Nothing here reaches the network - nothing anywhere in
	/// Chimera does.
	///
	/// Thin over <see cref="CoreManagerModel"/>, like the firmware windows are over
	/// their surveys: what somebody is told is decided by the model, which is tested
	/// without a UI, and this arranges it.
	/// </summary>
	public sealed class CoreManagerForm : FormBase
	{
		private readonly Func<IReadOnlyList<DiscoveredCorePackage>> _scan;
		private readonly Func<string> _folder;
		private readonly Action<string>? _useFolder;
		private readonly Func<string?>? _askForFolder;
		private readonly Action<string>? _openFolder;
		private readonly Func<(string Path, int Packages)>? _former;
		private readonly Action? _changed;

		private readonly ListView _cores;
		private readonly CheckBox _selectAll;
		private readonly ComboBox _versions;
		private readonly Label _versionDetail;
		private readonly Label _header;
		private readonly Label _status;
		private readonly Button _removeVersion;
		private readonly Button _removeCore;
		private readonly Button _open;
		private readonly Button _change;
		private readonly Button _useFormer;
		private readonly Button _systems;

		/// <summary>Set while the code is ticking boxes, so its own events do not answer back.</summary>
		private bool _suppressCheckEvents;

		/// <summary>
		/// The cores whose box is ticked, by name.
		///
		/// Kept as a set rather than read back off the ListView, because the event
		/// that reports a tick arrives as a posted Windows message: by the time it
		/// is delivered the collection may be mid-rebuild, and enumerating it from
		/// the handler is what crashed the window on Windows. Nothing outside the
		/// list's own events writes this.
		/// </summary>
		private readonly HashSet<string> _ticked = new(StringComparer.OrdinalIgnoreCase);

		/// <summary>
		/// False until the constructor has built every control.
		///
		/// A ListView raises ItemChecked while its handle is being created, which on
		/// .NET Framework happens inside the constructor - before the buttons the
		/// handler wants to enable exist. Mono does not do this, so the Linux tests
		/// never saw it and it arrived as a NullReferenceException on Windows the
		/// first time somebody opened the window.
		/// </summary>
		private bool _ready;

		private List<CoreManagerRow> _rows = new();
		private readonly CoreKindFilterBox _shows;
		private readonly Action<CoreKindFilter>? _rememberShows;

		protected override string WindowTitleStatic => "Core Manager";

		/// <param name="scan">the packages in the cores folder, read afresh each time</param>
		/// <param name="folder">the cores folder as it is configured now</param>
		/// <param name="useFolder">told the folder somebody chose; absent, the folder cannot be changed from here</param>
		/// <param name="askForFolder">a folder picker; null from it is a cancel</param>
		/// <param name="openFolder">shows a folder in the system's file manager</param>
		/// <param name="former">where earlier versions downloaded cores to, and how many packages are still there</param>
		/// <param name="changed">told after anything here changed what is in the folder, or which folder it is</param>
		public CoreManagerForm(
			Func<IReadOnlyList<DiscoveredCorePackage>> scan,
			Func<string> folder,
			Action<string>? useFolder = null,
			Func<string?>? askForFolder = null,
			Action<string>? openFolder = null,
			Func<(string Path, int Packages)>? former = null,
			Action? changed = null,
			CoreKindFilter shows = CoreKindFilter.All,
			Action<CoreKindFilter>? rememberShows = null)
		{
			_scan = scan;
			_folder = folder;
			_useFolder = useFolder;
			_askForFolder = askForFolder;
			_openFolder = openFolder;
			_former = former;
			_changed = changed;
			_rememberShows = rememberShows;

			SuspendLayout();
			ClientSize = new(UIHelper.ScaleX(1100), UIHelper.ScaleY(500));
			MinimumSize = new(UIHelper.ScaleX(900), UIHelper.ScaleY(420));
			StartPosition = FormStartPosition.CenterParent;
			ShowIcon = false;

			var margin = UIHelper.ScaleX(8);
			var sideWidth = UIHelper.ScaleX(320);
			var footer = UIHelper.ScaleY(76);
			var listTop = UIHelper.ScaleY(76);

			// two lines: which folder, and that it is the only way a core arrives
			_header = new Label
			{
				AutoSize = false,
				Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
				Location = new(margin, UIHelper.ScaleY(9)),
				Size = new(ClientSize.Width - (2 * margin), UIHelper.ScaleY(38)),
				// filled in by Reload: it names the folder and counts what is in it
			};

			// The select-all sits above the list rather than in the header, because a
			// WinForms ListView header is not a place a control can live - and here it
			// also says how many are ticked, which a header box could not.
			_selectAll = new CheckBox
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Left,
				AutoSize = true,
				Location = new(margin + UIHelper.ScaleX(2), UIHelper.ScaleY(54)),
				Text = "Select all",
			};
			_selectAll.CheckedChanged += (_, _) => SelectAllChanged();

			_cores = new ListView
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				FullRowSelect = true,
				HideSelection = false,
				Location = new(margin, listTop),
				Size = new(ClientSize.Width - sideWidth - (3 * margin), ClientSize.Height - listTop - footer),
				MultiSelect = false,
				View = View.Details,
				CheckBoxes = true,
			};
			// these have to add up to less than the list is wide (ClientSize minus the
			// side panel and the margins), or the last one is only reachable by
			// scrolling sideways
			_cores.Columns.Add("Core", UIHelper.ScaleX(150));
			// an emulator or a game (docs/game-cores.md)
			_cores.Columns.Add("Type", UIHelper.ScaleX(70));
			_cores.Columns.Add("Systems", UIHelper.ScaleX(200));
			// wide enough for a date, a commit and "(+2 more)"
			_cores.Columns.Add("Version", UIHelper.ScaleX(250));
			// right-aligned, because a column of sizes is read by comparing them
			_cores.Columns.Add("Size", UIHelper.ScaleX(70), HorizontalAlignment.Right);
			_cores.SelectedIndexChanged += (_, _) => ShowSelectedCore();
			_cores.ItemChecked += (_, e) =>
			{
				if (_suppressCheckEvents) return;
				// e.Item is the one the message is about; the collection it belongs
				// to is not safe to walk from here
				if (e.Item?.Tag is CoreManagerRow row)
				{
					if (e.Item.Checked) _ticked.Add(row.Name);
					else _ticked.Remove(row.Name);
				}
				UpdateButtons();
			};

			// which kinds of core the list shows, right-aligned above it on the select-all's line
			_shows = new CoreKindFilterBox("Show:", offerAll: true)
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Right,
				Value = shows,
			};
			_shows.Location = new(_cores.Right - _shows.PreferredSize.Width, UIHelper.ScaleY(51));
			_shows.Changed += () =>
			{
				_rememberShows?.Invoke(_shows.Value);
				Reload();
			};

			// The right column is a panel of its own so everything in it is placed
			// against ITS left edge. Right-anchoring a dozen loose controls to the form
			// puts them wherever the current DPI and font decide to.
			Panel side = new()
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Right,
				Location = new(ClientSize.Width - sideWidth - margin, listTop),
				Size = new(sideWidth, ClientSize.Height - listTop - footer),
			};

			Label versionLabel = new()
			{
				AutoSize = true,
				Location = new(0, UIHelper.ScaleY(2)),
				Text = "Version",
			};

			_versions = new ComboBox
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
				DropDownStyle = ComboBoxStyle.DropDownList,
				Location = new(0, UIHelper.ScaleY(22)),
				Width = sideWidth,
			};
			_versions.SelectedIndexChanged += (_, _) => ShowSelectedVersion();

			_removeVersion = new Button
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Left,
				Location = new(0, UIHelper.ScaleY(54)),
				Size = new((sideWidth - UIHelper.ScaleX(10)) / 2, UIHelper.ScaleY(26)),
				Text = "Remove version",
			};
			_removeVersion.Click += (_, _) => RemoveSelectedVersion();

			_versionDetail = new Label
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				AutoSize = false,
				Location = new(0, UIHelper.ScaleY(90)),
				Size = new(sideWidth, side.Height - UIHelper.ScaleY(90)),
			};

			side.Controls.AddRange(new Control[] { versionLabel, _versions, _removeVersion, _versionDetail });

			_status = new Label
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				AutoSize = false,
				Location = new(margin, ClientSize.Height - footer + UIHelper.ScaleY(4)),
				Size = new(ClientSize.Width - (2 * margin), UIHelper.ScaleY(32)),
			};

			// about the folder first - it is how a core gets here - then what is in it
			var buttonRow = ClientSize.Height - UIHelper.ScaleY(34);
			var bw = UIHelper.ScaleX(140);
			var gap = UIHelper.ScaleX(8);
			Button Place(int slot, string text) => new()
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left,
				Location = new(margin + (slot * (bw + gap)), buttonRow),
				Size = new(bw, UIHelper.ScaleY(26)),
				Text = text,
			};

			_open = Place(0, "Open cores folder");
			_open.Click += (_, _) => OpenTheFolder();

			_change = Place(1, "Change folder...");
			_change.Click += (_, _) => ChangeTheFolder();

			Button rescan = Place(2, "Look again");
			rescan.Click += (_, _) =>
			{
				Reload();
				_changed?.Invoke();
				Say($"{_rows.Count} core(s) in {_folder()}.");
			};

			_removeCore = Place(3, "Remove");
			_removeCore.Click += (_, _) => RemoveCheckedCores();

			// which machines all of this adds up to, and which core runs each (#172)
			_systems = Place(4, "Systems...");
			_systems.Click += (_, _) =>
			{
				using SupportedSystemsForm form = new(SupportedSystems.From(_rows));
				form.ShowDialog(this);
			};

			// only there when earlier versions left packages where they downloaded them
			_useFormer = Place(5, "Use that folder");
			_useFormer.Click += (_, _) =>
			{
				if (_former?.Invoke() is { Packages: > 0 } left) UseFolder(left.Path);
			};

			Button close = new()
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Right,
				DialogResult = DialogResult.OK,
				Location = new(ClientSize.Width - margin - UIHelper.ScaleX(90), buttonRow),
				Size = new(UIHelper.ScaleX(90), UIHelper.ScaleY(26)),
				Text = "Close",
			};

			Controls.AddRange(new Control[] { _header, _selectAll, _shows, _cores, side, _status, _open, _change, rescan, _removeCore, _systems, _useFormer, close });
			AcceptButton = close;
			ResumeLayout();

			// every control exists now, so the list's events have something to talk to
			_ready = true;
			Reload();
		}

		/// <summary>The two lines above the list, as they read now. For tests.</summary>
		public string HeaderText => _header.Text;

		/// <summary>The line under the list, as it reads now. For tests.</summary>
		public string StatusText => _status.Text;

		/// <summary>Whether the offer to use the folder earlier versions downloaded into is showing. For tests.</summary>
		public bool OffersTheFormerFolder => _useFormer.Visible;

		/// <summary>Rebuilds the list from a fresh scan of the folder, keeping the selection.</summary>
		private void Reload()
		{
			var wasSelected = Selected()?.Name;
			_rows = CoreManagerModel.Build(_scan()).ToList();

			// what was ticked survives a reload: a removal must not silently change
			// what the next button press would act on. A row that is gone leaves with
			// it - and so does one the filter hides: the button acts on what can be
			// seen ticked, never on something out of view.
			_ticked.IntersectWith(Shown().Select(static r => r.Name));

			_suppressCheckEvents = true;
			_cores.BeginUpdate();
			_cores.Items.Clear();
			foreach (var row in Shown())
			{
				ListViewItem item = new(row.Name) { Tag = row };
				item.SubItems.Add(CoreKindFilterExtensions.KindText(row.IsGameCore));
				item.SubItems.Add(row.SystemsSpelled);
				item.SubItems.Add(VersionText(row));
				item.SubItems.Add(SizeText(row));
				item.Checked = _ticked.Contains(row.Name);
				_cores.Items.Add(item);
			}
			_cores.EndUpdate();
			_suppressCheckEvents = false;

			var folder = _folder();
			_header.Text = $"Cores folder: {folder}   ({_rows.Count} core(s)){Environment.NewLine}"
				+ "Chimera downloads nothing. Get a core's package (.chimeraCore) from its project, or build it, and put it in this folder.";

			// somebody who updated from a Chimera that downloaded cores has them
			// somewhere this one does not look; say so where they will see it
			var left = _former?.Invoke() ?? ("", 0);
			_useFormer.Visible = left.Packages > 0 && _useFolder is not null;
			if (left.Packages > 0)
			{
				Say($"{left.Packages} core package(s) are in {left.Path}, where earlier versions of Chimera downloaded them. "
					+ "Move them here, or use that folder as the cores folder.");
			}
			else if (_rows.Count is 0)
			{
				Say("There are no cores in this folder yet.");
			}

			if (wasSelected is not null && ItemFor(wasSelected) is { } keep) keep.Selected = true;
			else if (_cores.Items.Count > 0) _cores.Items[0].Selected = true;
			ShowSelectedCore();
			UpdateButtons();
		}

		/// <summary>The rows the filter lets through, in the model's order: the emulators, then the games.</summary>
		private IEnumerable<CoreManagerRow> Shown() => _rows.Where(r => _shows.Value.Shows(r.IsGameCore));

		/// <summary>The rows whose box is ticked, in list order.</summary>
		private List<CoreManagerRow> Checked()
			=> _rows.FindAll(r => _ticked.Contains(r.Name));

		/// <summary>The list item for one core, or null when it is not listed.</summary>
		private ListViewItem? ItemFor(string name)
		{
			foreach (ListViewItem item in _cores.Items)
			{
				if (item.Tag is CoreManagerRow row && string.Equals(row.Name, name, StringComparison.OrdinalIgnoreCase)) return item;
			}
			return null;
		}

		/// <summary>
		/// Remove acts on what is ticked, so with nothing ticked there is nothing for
		/// it to do and it says so by being unavailable rather than by complaining
		/// afterwards.
		/// </summary>
		private void UpdateButtons()
		{
			if (!_ready) return;
			var any = Checked().Count is not 0;
			_removeCore.Enabled = any;
			_open.Enabled = _openFolder is not null;
			_change.Enabled = _useFolder is not null && _askForFolder is not null;
			_selectAll.Text = any ? $"Select all ({Checked().Count} ticked)" : "Select all";
		}

		private void SelectAllChanged()
		{
			if (_suppressCheckEvents) return;
			_suppressCheckEvents = true;
			_ticked.Clear();
			if (_selectAll.Checked) _ticked.UnionWith(Shown().Select(static r => r.Name));
			foreach (ListViewItem item in _cores.Items) item.Checked = _selectAll.Checked;
			_suppressCheckEvents = false;
			UpdateButtons();
		}

		/// <summary>The newest version here, by its date and commit, and how many more there are.</summary>
		private static string VersionText(CoreManagerRow row)
		{
			var versions = row.Installed.Select(static p => p.DatedVersion).Where(static v => v.Length is not 0).ToList();
			return versions.Count switch
			{
				0 => row.Installed.Count is 1 ? "" : $"{row.Installed.Count} versions",
				1 => versions[0],
				_ => $"{versions[0]}  (+{versions.Count - 1} more)",
			};
		}

		/// <summary>
		/// How big the core is, to one decimal place. Cores run from half a megabyte
		/// to a couple of hundred, so the useful comparison is between them rather
		/// than to the byte.
		/// </summary>
		private static string SizeText(CoreManagerRow row)
		{
			var bytes = row.SizeBytes;
			if (bytes <= 0) return "";
			var mb = bytes / 1024.0 / 1024.0;
			return mb < 1.0 ? $"{bytes / 1024.0:0} KB" : $"{mb:0.0} MB";
		}

		private CoreManagerRow? Selected()
			=> _cores.SelectedItems.Count is 0 ? null : _cores.SelectedItems[0].Tag as CoreManagerRow;

		/// <summary>The versions of the selected core that are here, newest first (issue #67).</summary>
		private void ShowSelectedCore()
		{
			var row = Selected();
			_versions.BeginUpdate();
			_versions.Items.Clear();
			foreach (var package in row?.Installed ?? [ ]) _versions.Items.Add(new VersionChoice(package));
			_versions.EndUpdate();
			if (_versions.Items.Count > 0) _versions.SelectedIndex = 0;
			ShowSelectedVersion();
		}

		private void ShowSelectedVersion()
		{
			var choice = _versions.SelectedItem as VersionChoice;
			_versionDetail.Text = choice is null ? "" : Detail(choice.Package);
			_removeVersion.Enabled = choice is not null;
		}

		/// <summary>Where the file is, how big, and - the one thing only the package can say - its terms.</summary>
		private static string Detail(DiscoveredCorePackage package)
		{
			List<string> lines = new() { package.Path };
			if (CoreVersionDates.Of(package) is { } built) lines.Add($"Built {CoreVersionDates.Format(built)}");
			// The bundle used to carry every core and compute one LICENSES.md from
			// them; it carries none now, so this is where a core says what it demands.
			lines.Add("");
			lines.Add(CoreLicence.Read(package.Path)?.Summary() is { Length: not 0 } terms
				? terms
				: "This package states no licence.");
			return string.Join(Environment.NewLine, lines);
		}

		private void Say(string message) => _status.Text = message;

		/// <summary>Ticks or unticks the core named <paramref name="name"/>. For tests and screenshots.</summary>
		public bool SetChecked(string name, bool ticked)
		{
			if (ItemFor(name) is not { } item) return false;
			item.Checked = ticked;
			if (item.Tag is CoreManagerRow row)
			{
				if (ticked) _ticked.Add(row.Name);
				else _ticked.Remove(row.Name);
			}
			UpdateButtons();
			return true;
		}

		/// <summary>Whether the button that acts on ticked cores is available.</summary>
		public bool BulkActionsEnabled => _removeCore.Enabled;

		/// <summary>Selects the core named <paramref name="name"/>, if it is listed.</summary>
		public bool Select(string name)
		{
			if (ItemFor(name) is not { } item) return false;
			item.Selected = true;
			return true;
		}

		private void OpenTheFolder()
		{
			var folder = _folder();
			try
			{
				// an empty Cores/ comes with the bundle, but a folder somebody named
				// may not be there yet, and "open it" should not be the thing that fails
				Directory.CreateDirectory(folder);
				_openFolder?.Invoke(folder);
			}
			catch (Exception ex)
			{
				Say($"{folder} could not be opened: {ex.Message}");
			}
		}

		private void ChangeTheFolder()
		{
			if (_askForFolder?.Invoke() is { Length: not 0 } chosen) UseFolder(chosen);
		}

		/// <summary>Makes <paramref name="folder"/> the cores folder and lists what is in it. Public so a test can drive it.</summary>
		public void UseFolder(string folder)
		{
			if (_useFolder is null) return;
			_useFolder(folder);
			Reload();
			_changed?.Invoke();
			if (_former?.Invoke() is not { Packages: > 0 })
			{
				Say($"The cores folder is now {_folder()}: {_rows.Count} core(s). A core already loaded stays loaded until Chimera is restarted.");
			}
		}

		/// <summary>
		/// Removes every version of every ticked core: deletes the package files,
		/// behind a confirmation that says what it costs. Only files that are in the
		/// cores folder itself; a package found in a further search directory is
		/// somebody's arrangement and is left where it is.
		/// </summary>
		private void RemoveCheckedCores()
		{
			var wanted = Checked();
			if (wanted.Count is 0) return;
			var versions = wanted.Sum(static r => r.Installed.Count);
			if (MessageBox.Show(
				this,
				$"Delete {versions} package file(s) of {wanted.Count} core(s) from the cores folder?{Environment.NewLine}{Environment.NewLine}"
					+ "A movie recorded on one of these exact builds needs it to replay, and Chimera cannot fetch it again.",
				"Remove cores",
				MessageBoxButtons.OKCancel,
				MessageBoxIcon.Warning) is not DialogResult.OK)
			{
				return;
			}

			var folder = _folder();
			var removed = 0;
			List<string> kept = new();
			foreach (var path in wanted.SelectMany(static r => r.InstalledPaths))
			{
				if (!CoresFolder.Holds(folder, path)) { kept.Add(Path.GetFileName(path)); continue; }
				try
				{
					Delete(path);
					removed++;
				}
				catch (Exception ex)
				{
					kept.Add($"{Path.GetFileName(path)} ({ex.Message})");
				}
			}
			Reload();
			_changed?.Invoke();
			Say(kept.Count is 0
				? $"Removed {removed} package(s)."
				: $"Removed {removed}; left alone {string.Join(", ", kept)} (not in the cores folder).");
		}

		/// <summary>
		/// Deletes one version. Only ever one: an old build is the only way to replay
		/// a movie recorded on it, so nothing removes a version to make room for
		/// another.
		/// </summary>
		private void RemoveSelectedVersion()
		{
			var row = Selected();
			if (row is null || _versions.SelectedItem is not VersionChoice { Package.Path: var path }) return;
			if (!CoresFolder.Holds(_folder(), path))
			{
				Say($"{Path.GetFileName(path)} is not in the cores folder, so it is not the manager's to delete. It is at {path}.");
				return;
			}
			if (MessageBox.Show(
				this,
				$"Delete {Path.GetFileName(path)}?{Environment.NewLine}{Environment.NewLine}"
					+ "A movie recorded on this exact build needs it to replay, and Chimera cannot fetch it again.",
				"Remove core",
				MessageBoxButtons.OKCancel,
				MessageBoxIcon.Warning) is not DialogResult.OK)
			{
				return;
			}
			try
			{
				Delete(path);
				Reload();
				_changed?.Invoke();
				Say($"Removed {Path.GetFileName(path)}.");
			}
			catch (Exception ex)
			{
				Say($"Could not remove it: {ex.Message}");
			}
		}

		/// <summary>
		/// A package file is deleted. An unpacked package - a folder, which is how
		/// somebody keeps their own build - is not: this window does not empty folders.
		/// </summary>
		/// <exception cref="IOException">it is a folder, or the file will not go</exception>
		private static void Delete(string path)
		{
			if (Directory.Exists(path)) throw new IOException("a folder; delete it yourself");
			File.Delete(path);
		}

		/// <summary>One line of the version selector: a package that is here.</summary>
		private sealed class VersionChoice
		{
			public VersionChoice(DiscoveredCorePackage package) => Package = package;

			public DiscoveredCorePackage Package { get; }

			public override string ToString()
				=> Package.DatedVersion.Length is 0 ? Path.GetFileNameWithoutExtension(Package.Path) : Package.DatedVersion;
		}
	}
}
