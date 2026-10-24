#nullable enable

using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Common;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Config > Firmware: every installed core asked what it needs, and what the
	/// person has for each - before any project, before any core is loaded.
	///
	/// Just in time, and nothing kept. The rows are built from the packages in the
	/// Cores folder when the window opens (<see cref="FirmwareSurvey"/>) and go away
	/// with it; the frontend has no list of firmware of its own and cannot, since it
	/// does not know what cores exist. Delete a core package and its rows are gone
	/// on the next open; what was chosen for it stays in the config for the day it
	/// is put back. What the person points at is remembered where it lives, never
	/// copied: firmware can be a
	/// PlayStation 3 update or an Xbox disk image, and a second copy is a second
	/// thing to drift.
	///
	/// Like <see cref="CoreFirmwareForm"/>, which shows what the RUNNING machine
	/// is using, this window is thin: the rows and their verdicts come from the
	/// model, which is tested without a UI, and choosing a file is handed to the
	/// owner.
	/// </summary>
	public sealed class FirmwareSurveyForm : FormBase
	{
		/// <summary>0 on hand and right, 1 on hand but not a dump the core knows, 2 gone or unusable, 3 nothing.</summary>
		private static readonly ImageList _marks = BuildMarks();

		/// <summary>An id declared more often than this shows only the dumps on hand until asked for all.</summary>
		public const int CollapseAbove = 8;

		private readonly ListView _list;
		private readonly TextBox _find;
		private readonly Label _detail;
		private readonly Label _header;
		private readonly CheckBox _showAll;
		private readonly CoreKindFilterBox _shows;
		private readonly Button _setButton;
		private readonly Button _clearButton;
		private readonly Func<IReadOnlyList<FirmwareSurveyGroup>> _survey;
		private readonly Action<FirmwareSurveyRow, string?> _remember;
		private readonly Func<string, string?> _pickFile;
		private readonly PickScanFolder? _pickFolder;
		private readonly Action<string, bool>? _scanFolder;

		/// <summary>
		/// Whether Scan Folder walks below the folder it is given. Ticked, which is what this window always
		/// did; the folder picker offers the choice where it can carry one, and this remembers the answer
		/// between scans. Where it cannot (Mono's dialog, and the tree fallback) the scan runs on this value,
		/// which is the behaviour the window had before the option existed.
		/// </summary>
		private bool _scanSubfolders = true;

		private readonly Action<string, string?> _openFolder;

		/// <summary>The cores whose files are folded to one line (issue 368), for as long as the window is open.</summary>
		private readonly HashSet<string> _collapsed = new(StringComparer.Ordinal);

		private List<FirmwareSurveyGroup> _groups = new();
		private List<FirmwareSurveyRow?> _rowOf = new(); // list index -> row, null for a "more" or folded line
		private List<string> _coreOf = new(); // list index -> the core the line belongs to

		protected override string WindowTitleStatic => "Firmware";

		/// <param name="survey">builds the rows from the packages present now (called on open and after every change)</param>
		/// <param name="remember">remembers (or, for null, forgets) the file chosen for one declaration</param>
		/// <param name="pickFile">shows a file picker titled for one row; the dialog belongs to the owner</param>
		/// <param name="pickFolder">shows a folder picker for Scan Folder, carrying the sub-folders choice; null hides the button</param>
		/// <param name="scanFolder">hashes a folder (below it too, when told to) and remembers what it matched</param>
		/// <param name="openFolder">shows a folder on the desktop, the file named selected when there is one; a test gives its own</param>
		public FirmwareSurveyForm(
			Func<IReadOnlyList<FirmwareSurveyGroup>> survey,
			Action<FirmwareSurveyRow, string?> remember,
			Func<string, string?> pickFile,
			PickScanFolder? pickFolder = null,
			Action<string, bool>? scanFolder = null,
			CoreKindFilter shows = CoreKindFilter.All,
			Action<CoreKindFilter>? rememberShows = null,
			Action<string, string?>? openFolder = null)
		{
			_openFolder = openFolder ?? OpenOnDesktop;
			_survey = survey;
			_remember = remember;
			_pickFile = pickFile;
			_pickFolder = pickFolder;
			_scanFolder = scanFolder;

			SuspendLayout();
			ClientSize = new(UIHelper.ScaleX(860), UIHelper.ScaleY(440));
			MinimumSize = new(UIHelper.ScaleX(640), UIHelper.ScaleY(300));
			StartPosition = FormStartPosition.CenterParent;
			ShowIcon = false;

			_header = new Label
			{
				AutoSize = false,
				Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
				Location = new(UIHelper.ScaleX(8), UIHelper.ScaleY(8)),
				Size = new(UIHelper.ScaleX(844), UIHelper.ScaleY(32)),
				Text = "Files your installed cores expect you to provide. A green row was found in the Firmware folder or where you last chose it; a project that needs it opens without asking.",
			};

			_list = new ListView
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				FullRowSelect = true,
				HideSelection = false,
				Location = new(UIHelper.ScaleX(8), UIHelper.ScaleY(72)),
				MultiSelect = false,
				ShowGroups = true,
				Size = new(UIHelper.ScaleX(844), UIHelper.ScaleY(272)),
				SmallImageList = _marks,
				View = View.Details,
			};
			_list.Columns.Add("", UIHelper.ScaleX(26));
			_list.Columns.Add("Firmware", UIHelper.ScaleX(200));
			_list.Columns.Add("Release", UIHelper.ScaleX(150));
			_list.Columns.Add("Expected SHA1", UIHelper.ScaleX(95));
			_list.Columns.Add("Found", UIHelper.ScaleX(150));
			_list.Columns.Add("Where", UIHelper.ScaleX(110));
			_list.Columns.Add("Status", UIHelper.ScaleX(105));
			_list.SelectedIndexChanged += (_, _) => UpdateDetail();

			// which kinds of core's files are listed, right-aligned above the list
			_shows = new CoreKindFilterBox("Show:", offerAll: true)
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Right,
				Value = shows,
			};
			_shows.Location = new(_list.Right - _shows.PreferredSize.Width, UIHelper.ScaleY(44));
			_shows.Changed += () =>
			{
				rememberShows?.Invoke(_shows.Value);
				Render();
			};
			_list.DoubleClick += (_, _) =>
			{
				// a folded core's line opens it; any other line chooses a file, as before
				if (SelectedRow is null && SelectedCore is { } core && _collapsed.Contains(core)) Expand(core);
				else Browse();
			};
			_list.KeyDown += (_, e) =>
			{
				if (SelectedCore is not { } core) return;
				if (e.KeyCode is Keys.Add or Keys.Oemplus) Expand(core);
				else if (e.KeyCode is Keys.Subtract or Keys.OemMinus) Collapse(core);
				else return;
				e.Handled = true;
			};
			_list.ContextMenuStrip = BuildMenu();

			// the list narrowed to what matches, left above the list (issue 368)
			Label findLabel = new()
			{
				AutoSize = true,
				Location = new(UIHelper.ScaleX(8), UIHelper.ScaleY(47)),
				Text = "Find:",
			};
			_find = new TextBox
			{
				// beside the label's text: an AutoSize label has no width of its own yet
				Location = new(findLabel.Left + findLabel.PreferredSize.Width + UIHelper.ScaleX(4), UIHelper.ScaleY(44)),
				Size = new(UIHelper.ScaleX(220), UIHelper.ScaleY(20)),
			};
			_find.TextChanged += (_, _) => Render();

			_detail = new Label
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				AutoEllipsis = true,
				Location = new(UIHelper.ScaleX(8), UIHelper.ScaleY(352)),
				Size = new(UIHelper.ScaleX(844), UIHelper.ScaleY(46)),
			};

			_setButton = MakeButton("Set File...", 8, Browse);
			_clearButton = MakeButton("Clear", 110, Clear);
			Button scanButton = MakeButton("Scan Folder...", 190, ScanFolder);
			scanButton.Visible = _pickFolder is not null && _scanFolder is not null;
			Button rescanButton = MakeButton("Rescan", 300, Populate);
			_showAll = new CheckBox
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left,
				AutoSize = true,
				Location = new(UIHelper.ScaleX(390), UIHelper.ScaleY(412)),
				Text = "Show every release",
			};
			_showAll.CheckedChanged += (_, _) => Render();
			Button closeButton = MakeButton("Close", 780, Close);
			closeButton.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
			CancelButton = closeButton;

			Controls.AddRange([ _header, findLabel, _find, _shows, _list, _detail, _setButton, _clearButton, scanButton, rescanButton, _showAll, closeButton ]);
			ResumeLayout();
			Populate();
		}

		private static ImageList BuildMarks()
		{
			ImageList list = new() { ImageSize = new(16, 16), ColorDepth = ColorDepth.Depth32Bit };
			Bitmap Draw(Action<Graphics> paint)
			{
				Bitmap bmp = new(16, 16);
				using Graphics g = Graphics.FromImage(bmp);
				g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
				paint(g);
				return bmp;
			}
			list.Images.Add(Draw(g =>
			{
				using Pen pen = new(ThemeEngine.Color(ThemeColorRole.GlyphGood), 2.4f);
				g.DrawLines(pen, new[] { new Point(3, 8), new Point(6, 12), new Point(13, 4) });
			}));
			list.Images.Add(Draw(g =>
			{
				using SolidBrush fill = new(ThemeEngine.Color(ThemeColorRole.GlyphWarning));
				g.FillPolygon(fill, new[] { new Point(8, 1), new Point(15, 14), new Point(1, 14) });
				using SolidBrush ink = new(ThemeEngine.Color(ThemeColorRole.GlyphWarningInk));
				g.FillRectangle(ink, 7, 5, 2, 5);
				g.FillRectangle(ink, 7, 11, 2, 2);
			}));
			list.Images.Add(Draw(g =>
			{
				using Pen pen = new(ThemeEngine.Color(ThemeColorRole.GlyphError), 2.4f);
				g.DrawLine(pen, 4, 4, 12, 12);
				g.DrawLine(pen, 12, 4, 4, 12);
			}));
			list.Images.Add(Draw(g =>
			{
				using Pen pen = new(ThemeEngine.Color(ThemeColorRole.GlyphNeutral), 1.4f);
				g.DrawEllipse(pen, 3, 3, 10, 10);
			}));
			return list;
		}

		private static int MarkFor(CoreFirmwareState state) => state switch
		{
			CoreFirmwareState.Good => 0,
			CoreFirmwareState.Unrecognised or CoreFirmwareState.Custom => 1,
			CoreFirmwareState.Unreadable => 2,
			_ => 3,
		};

		private Button MakeButton(string text, int x, Action onClick)
		{
			Button b = new()
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left,
				AutoSize = true,
				Location = new(UIHelper.ScaleX(x), UIHelper.ScaleY(408)),
				Text = text,
			};
			b.Click += (_, _) => onClick();
			return b;
		}

		/// <summary>Surveys the packages again and redraws. Public so a test can drive it.</summary>
		public void Populate()
		{
			_groups = _survey().ToList();
			Render();
		}

		/// <summary>The groups as surveyed, for tests.</summary>
		public IReadOnlyList<FirmwareSurveyGroup> Groups => _groups;

		private void Render()
		{
			var selected = SelectedRow;
			// a row in view and where it is drawn, so a rescan keeps the reader's place (issue 348)
			var anchor = ScrollAnchor();
			var anchorRow = anchor is { } a && a.Index < _rowOf.Count ? _rowOf[a.Index] : null;
			var selectedCore = SelectedCore;
			var find = _find.Text.Trim();
			_list.BeginUpdate();
			_list.Items.Clear();
			_list.Groups.Clear();
			_rowOf.Clear();
			_coreOf.Clear();
			// the emulators' files, then the game cores' (docs/game-cores.md), as the survey orders
			// them, through the Show filter and the Find box
			foreach (var group in _groups.Where(g => _shows.Value.Shows(g.IsGameCore)))
			{
				var shown = group.Rows.Where(r => Matches(group, r, find)).ToList();
				if (find.Length is not 0 && shown.Count is 0 && !Contains(group.CoreName, find)) continue;
				ListViewGroup lvg = new($"{group.CoreName}  ·  {group.Summary}");
				_list.Groups.Add(lvg);
				if (group.Rows.Count is 0)
				{
					ListViewItem none = new("", 3) { Group = lvg, ForeColor = ThemeEngine.Color(ThemeColorRole.DisabledText) };
					none.SubItems.Add("(needs no firmware)");
					_list.Items.Add(none);
					_rowOf.Add(null);
					_coreOf.Add(group.CoreName);
					continue;
				}
				if (_collapsed.Contains(group.CoreName))
				{
					ListViewItem folded = new("", 3) { Group = lvg, ForeColor = ThemeEngine.Color(ThemeColorRole.DisabledText) };
					// the core named here too: not every toolkit draws the group's heading
					folded.SubItems.Add($"+ {group.CoreName}: {shown.Count} {(shown.Count is 1 ? "file" : "files")}");
					folded.SubItems.Add("double-click or + to show");
					folded.Selected = selectedCore == group.CoreName;
					_list.Items.Add(folded);
					_rowOf.Add(null);
					_coreOf.Add(group.CoreName);
					continue;
				}
				foreach (var byId in shown.GroupBy(static r => r.Decl.Id))
				{
					var rows = byId.ToList();
					var collapse = !_showAll.Checked && rows.Count > CollapseAbove;
					var hidden = 0;
					foreach (var row in rows)
					{
						if (collapse && !row.OnHand && row.State is not CoreFirmwareState.Unreadable)
						{
							hidden++;
							continue;
						}
						ListViewItem item = new("", MarkFor(row.State)) { Group = lvg };
						item.SubItems.Add(row.Decl.DisplayName);
						item.SubItems.Add(row.Decl.Label ?? (row.Condition.Length is 0 ? "" : $"when {row.Condition}"));
						item.SubItems.Add(string.IsNullOrEmpty(row.Decl.Sha1) ? "(any)" : CoreFirmwareEntry.Short(row.Decl.Sha1));
						item.SubItems.Add(row.Path is null ? "" : Path.GetFileName(row.Path));
						item.SubItems.Add(row.Where is FirmwareWhere.Chosen ? "chosen" : row.WhereText);
						item.SubItems.Add(row.StatusText);
						item.ForeColor = row.State switch
						{
							CoreFirmwareState.Good => ThemeEngine.Color(ThemeColorRole.AccentGood),
							CoreFirmwareState.Unrecognised or CoreFirmwareState.Custom => ThemeEngine.Color(ThemeColorRole.AccentWarning),
							CoreFirmwareState.Unreadable => ThemeEngine.Color(ThemeColorRole.AccentError),
							_ => ThemeEngine.Color(ThemeColorRole.DisabledText),
						};
						if (ReferenceEquals(selected, row) || (selected is not null && SameRow(selected, row))) item.Selected = true;
						_list.Items.Add(item);
						_rowOf.Add(row);
						_coreOf.Add(group.CoreName);
					}
					if (hidden > 0)
					{
						ListViewItem more = new("", 3) { Group = lvg, ForeColor = ThemeEngine.Color(ThemeColorRole.DisabledText) };
						more.SubItems.Add($"… {hidden} more releases of {rows[0].Decl.DisplayName} not on hand; tick \"Show every release\" to see them");
						_list.Items.Add(more);
						_rowOf.Add(null);
						_coreOf.Add(group.CoreName);
					}
				}
			}
			_list.EndUpdate();
			if (anchor is { } was) ScrollBack(anchorRow, was.Index, was.Y);
			UpdateDetail();
		}

		/// <summary>
		/// The row drawn halfway down the list (or the nearest one above it), and how far down it is
		/// drawn, or null for an empty list. Halfway rather than at the top edge: a row scrolled
		/// under the column headings still answers to a point there. Public so a test can read
		/// where the list is scrolled to.
		/// </summary>
		public (int Index, int Y)? ScrollAnchor()
		{
			if (_list.Items.Count is 0 || !_list.IsHandleCreated) return null;
			var x = _list.ClientSize.Width / 3;
			for (var y = _list.ClientSize.Height / 2; y >= 0; y -= 2)
			{
				var item = _list.GetItemAt(x, y);
				if (item is not null) return (item.Index, item.Bounds.Top);
			}
			return null;
		}

		/// <summary>
		/// After the list was built again, draws the anchor row where it was: the same row when it
		/// is still listed, or else the row at the same place. The list scrolls only to bring a
		/// row into view, and a row brought in from below stops at the top, so the row put at the
		/// top is corrected until the anchor is back within a row of where it was.
		/// </summary>
		private void ScrollBack(FirmwareSurveyRow? row, int index, int y)
		{
			var at = row is null ? -1 : _rowOf.FindIndex(r => r is not null && SameRow(r, row));
			if (at < 0) at = Math.Min(index, _list.Items.Count - 1);
			if (at < 0) return;
			var rowHeight = Math.Max(1, _list.Items[at].Bounds.Height);
			if (Math.Abs(y - _list.Items[at].Bounds.Top) < rowHeight / 2) return; // the list was not scrolled
			var top = at;
			for (var tries = 0; tries < 4; tries++)
			{
				_list.EnsureVisible(_list.Items.Count - 1);
				_list.EnsureVisible(top);
				var off = y - _list.Items[at].Bounds.Top;
				if (Math.Abs(off) < rowHeight / 2) return;
				var next = Math.Max(0, top - (int) Math.Round((double) off / rowHeight));
				if (next == top) return;
				top = next;
			}
		}

		private static bool SameRow(FirmwareSurveyRow a, FirmwareSurveyRow b)
			=> a.CoreName == b.CoreName && a.Decl.Id == b.Decl.Id && string.Equals(a.Decl.Sha1, b.Decl.Sha1, StringComparison.OrdinalIgnoreCase)
				&& a.Decl.Label == b.Decl.Label;

		public FirmwareSurveyRow? SelectedRow
			=> _list.SelectedIndices.Count is 0 || _list.SelectedIndices[0] >= _rowOf.Count ? null : _rowOf[_list.SelectedIndices[0]];

		/// <summary>The core of the selected line, a folded one's included.</summary>
		public string? SelectedCore
			=> _list.SelectedIndices.Count is 0 || _list.SelectedIndices[0] >= _coreOf.Count ? null : _coreOf[_list.SelectedIndices[0]];

		/// <summary>What the Find box holds. Public so a test can drive it.</summary>
		public string FindText
		{
			get => _find.Text;
			set => _find.Text = value;
		}

		private static bool Contains(string? text, string find)
			=> text is not null && text.IndexOf(find, StringComparison.OrdinalIgnoreCase) >= 0;

		/// <summary>
		/// Whether a row answers the Find box: its core, the firmware's name, id or
		/// release, the name a core asks for, or the file found for it (issue 368).
		/// </summary>
		public static bool Matches(FirmwareSurveyGroup group, FirmwareSurveyRow row, string find)
			=> find.Length is 0
				|| Contains(group.CoreName, find)
				|| Contains(row.Decl.DisplayName, find)
				|| Contains(row.Decl.Id, find)
				|| Contains(row.Decl.Label, find)
				|| Contains(row.Decl.Name, find)
				|| (row.Path is { Length: > 0 } path && Contains(Path.GetFileName(path), find));

		/// <summary>Folds a core's files to one line, until it is opened again or the window closes.</summary>
		public void Collapse(string core)
		{
			if (_groups.Any(g => g.CoreName == core && g.Rows.Count is not 0) && _collapsed.Add(core)) Render();
		}

		public void Expand(string core)
		{
			if (_collapsed.Remove(core)) Render();
		}

		public void CollapseAll()
		{
			foreach (var g in _groups.Where(static g => g.Rows.Count is not 0)) _collapsed.Add(g.CoreName);
			Render();
		}

		public void ExpandAll()
		{
			_collapsed.Clear();
			Render();
		}

		/// <summary>
		/// Where Open Folder goes for a row: the folder of the file found for it, with the
		/// file to select when it is still there; null when the row names no file or its
		/// folder is gone.
		/// </summary>
		public static (string Folder, string? File)? OpenFolderTarget(FirmwareSurveyRow row)
		{
			if (row.Path is not { Length: > 0 } path) return null;
			string full;
			try
			{
				full = Path.GetFullPath(path);
			}
			catch (Exception e) when (e is ArgumentException or NotSupportedException or PathTooLongException)
			{
				return null;
			}
			var folder = Path.GetDirectoryName(full);
			if (folder is null || !Directory.Exists(folder)) return null;
			return (folder, File.Exists(full) ? full : null);
		}

		/// <summary>Open Folder on the selected row. Public so a test can drive it.</summary>
		public void OpenSelectedFolder()
		{
			if (SelectedRow is not { } row || OpenFolderTarget(row) is not { } target) return;
			try
			{
				_openFolder(target.Folder, target.File);
			}
			catch (Exception e)
			{
				_detail.Text = $"{target.Folder} could not be opened: {e.Message}";
			}
		}

		private static void OpenOnDesktop(string folder, string? file)
		{
			if (!OSTailoredCode.IsUnixHost && file is not null) Process.Start("explorer.exe", $"/select,\"{file}\"");
			else Util.OpenFolderExternal(folder);
		}

		private ContextMenuStrip BuildMenu()
		{
			ContextMenuStrip menu = new();
			ToolStripMenuItem openFolder = new("Open Folder", null, (_, _) => OpenSelectedFolder());
			ToolStripMenuItem fold = new("", null, (_, _) =>
			{
				if (SelectedCore is not { } core) return;
				if (_collapsed.Contains(core)) Expand(core);
				else Collapse(core);
			});
			ToolStripMenuItem collapseAll = new("Collapse All", null, (_, _) => CollapseAll());
			ToolStripMenuItem expandAll = new("Expand All", null, (_, _) => ExpandAll());
			menu.Items.AddRange([ openFolder, new ToolStripSeparator(), fold, collapseAll, expandAll ]);
			menu.Opening += (_, _) =>
			{
				openFolder.Enabled = SelectedRow is { } row && OpenFolderTarget(row) is not null;
				var core = SelectedCore;
				fold.Visible = core is not null && _groups.Any(g => g.CoreName == core && g.Rows.Count is not 0);
				fold.Text = core is null ? "" : _collapsed.Contains(core) ? $"Expand {core}" : $"Collapse {core}";
				collapseAll.Enabled = _groups.Any(g => g.Rows.Count is not 0 && !_collapsed.Contains(g.CoreName));
				expandAll.Enabled = _collapsed.Count is not 0;
			};
			return menu;
		}

		/// <summary>The rows as displayed, top to bottom, for tests ("more" lines excluded).</summary>
		public IReadOnlyList<FirmwareSurveyRow> DisplayedRows => _rowOf.Where(static r => r is not null).Select(static r => r!).ToList();

		private void UpdateDetail()
		{
			var row = SelectedRow;
			_setButton.Enabled = row is not null;
			_clearButton.Enabled = row is not null && row.Where is FirmwareWhere.Chosen;
			if (row is null)
			{
				_detail.Text = _groups.Count is 0 ? "No core package is in the cores folder." : "";
				return;
			}
			var what = row.Decl.Description ?? $"{row.Decl.DisplayName}, {row.Decl.Size} bytes";
			var expected = string.IsNullOrEmpty(row.Decl.Sha1)
				? (string.IsNullOrEmpty(row.Decl.Name) ? "any file" : $"a file called {row.Decl.Name}, any bytes")
				: $"expects {row.Decl.Sha1!.ToUpperInvariant()}";
			var when = row.Condition.Length is 0 ? "" : $" · needed when {row.Condition}";
			var actual = row.Path is null ? "nothing yet"
				: row.Sha1 is null ? $"{row.Path} could not be read"
				: $"{row.Path} is {row.Sha1}";
			_detail.Text = $"{what}\n{expected}{when}\n{actual}";
		}

		private void Browse()
		{
			var row = SelectedRow;
			if (row is null) return;
			var path = _pickFile($"{row.CoreName}: {row.Decl.DisplayName}{(row.Decl.Label is null ? "" : $" - {row.Decl.Label}")}");
			if (path is null) return;
			_remember(row, path);
			Populate();
		}

		private void Clear()
		{
			var row = SelectedRow;
			if (row is null) return;
			_remember(row, null);
			Populate();
		}

		private void ScanFolder()
		{
			if (_pickFolder is null || _scanFolder is null) return;
			var recurse = _scanSubfolders;
			var folder = _pickFolder(ref recurse);
			if (folder is null) return;
			_scanSubfolders = recurse;
			UseWaitCursor = true;
			try
			{
				_scanFolder(folder, recurse);
			}
			finally
			{
				UseWaitCursor = false;
			}
			Populate();
		}
	}
}
