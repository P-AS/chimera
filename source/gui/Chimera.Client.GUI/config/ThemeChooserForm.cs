#nullable enable

using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Common;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Config &gt; Theme: every theme on offer, a drawing of Chimera in the one that
	/// is picked, and the folder they come from (docs/theming.md).
	///
	/// Picking a theme in the list changes only the drawing; OK turns it on. It was a
	/// sub-menu until there were sixty themes to choose from, and a name in a menu
	/// says nothing about what the colours are (user-decided, 2026-10-19).
	/// </summary>
	public sealed class ThemeChooserForm : FormBase
	{
		private readonly ListView _list;
		private readonly ThemePreview _preview;
		private readonly Label _title;
		private readonly Label _about;
		private readonly Label _byline;
		private readonly LinkLabel _status;
		private readonly Action<string> _openFolder;
		private readonly Action<string, string> _tell;

		private List<Theme> _shown = new();

		/// <summary>
		/// Where the picked theme is in <see cref="_shown"/>, or -1. Kept here rather
		/// than read back off the list: on Mono a list that is not on screen yet does
		/// not keep a selection made in code, and clicking from one row to another
		/// passes through "nothing selected" on the way.
		/// </summary>
		private int _picked = -1;

		/// <summary>The theme Chimera is using: the one that was on when the window opened, which this window does not change.</summary>
		private string _inUse;

		/// <summary>False while the list is being filled, so that filling it is not taken for somebody picking.</summary>
		private bool _ready;

		protected override string WindowTitleStatic => "Theme";

		public ThemeChooserForm() : this(openFolder: null) {}

		/// <param name="openFolder">shows a folder to the user; the system's file manager in the application, a stub in tests</param>
		/// <param name="tell">(text, title) - a message the user has to read, for the files that would not load</param>
		public ThemeChooserForm(Action<string>? openFolder, Action<string, string>? tell = null)
		{
			_openFolder = openFolder ?? Util.OpenFolderExternal;
			_tell = tell ?? ((text, title) => MessageBox.Show(this, text, title, MessageBoxButtons.OK, MessageBoxIcon.Error));

			SuspendLayout();
			ClientSize = new(UIHelper.ScaleX(900), UIHelper.ScaleY(566));
			MinimumSize = new(UIHelper.ScaleX(720), UIHelper.ScaleY(480));
			MinimizeBox = false;
			ShowInTaskbar = false;
			StartPosition = FormStartPosition.CenterParent;

			_list = new ListView
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left,
				Location = new(UIHelper.ScaleX(12), UIHelper.ScaleY(12)),
				Size = new(UIHelper.ScaleX(260), UIHelper.ScaleY(480)),
				FullRowSelect = true,
				HideSelection = false,
				MultiSelect = false,
				HeaderStyle = ColumnHeaderStyle.Nonclickable,
				ShowItemToolTips = true,
				View = View.Details,
			};
			_list.Columns.Add("Theme", UIHelper.ScaleX(160));
			_list.Columns.Add("Look", UIHelper.ScaleX(60));
			_list.SelectedIndexChanged += (_, _) =>
			{
				if (!_ready || _list.SelectedIndices.Count is 0) return;
				_picked = _list.SelectedIndices[0];
				ShowPicked();
			};
			_list.DoubleClick += (_, _) => Accept();

			_preview = new ThemePreview
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				Location = new(UIHelper.ScaleX(284), UIHelper.ScaleY(12)),
				Size = new(UIHelper.ScaleX(604), UIHelper.ScaleY(400)),
			};

			_title = new Label
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				Location = new(UIHelper.ScaleX(284), UIHelper.ScaleY(420)),
				Size = new(UIHelper.ScaleX(604), UIHelper.ScaleY(20)),
				AutoEllipsis = true,
			};
			_title.Font = new Font(_title.Font, FontStyle.Bold);
			_about = new Label
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				Location = new(UIHelper.ScaleX(284), UIHelper.ScaleY(442)),
				Size = new(UIHelper.ScaleX(604), UIHelper.ScaleY(30)),
				AutoEllipsis = true,
			};
			_byline = new Label
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				Location = new(UIHelper.ScaleX(284), UIHelper.ScaleY(474)),
				Size = new(UIHelper.ScaleX(604), UIHelper.ScaleY(18)),
				AutoEllipsis = true,
			};
			_byline.SetForeRole(ThemeColorRole.MutedText);

			_status = new LinkLabel
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				Location = new(UIHelper.ScaleX(12), UIHelper.ScaleY(502)),
				Size = new(UIHelper.ScaleX(876), UIHelper.ScaleY(20)),
				AutoEllipsis = true,
			};
			_status.LinkClicked += (_, _) => TellFailures();

			Button Make(string text, int x, int width, AnchorStyles side, Action click)
			{
				Button b = new()
				{
					Anchor = AnchorStyles.Bottom | side,
					Text = text,
					Location = new(UIHelper.ScaleX(x), UIHelper.ScaleY(528)),
					Size = new(UIHelper.ScaleX(width), UIHelper.ScaleY(28)),
				};
				b.Click += (_, _) => click();
				return b;
			}
			var folder = Make("Open Themes Folder", 12, 140, AnchorStyles.Left, OpenFolder);
			var copy = Make("Write a Copy to Edit", 158, 150, AnchorStyles.Left, () => WriteCopy());
			var reload = Make("Reload Themes", 314, 120, AnchorStyles.Left, Reload);
			var ok = Make("OK", 702, 90, AnchorStyles.Right, Accept);
			var cancel = Make("Cancel", 798, 90, AnchorStyles.Right, () => { DialogResult = DialogResult.Cancel; Close(); });
			AcceptButton = ok;
			CancelButton = cancel;

			Controls.AddRange([ _list, _preview, _title, _about, _byline, _status, folder, copy, reload, ok, cancel ]);
			ResumeLayout();

			_inUse = ThemeLibrary.Current.Name;
			Fill(_inUse);
			SayWhatLoaded();
		}

		/// <summary>The theme that was picked when the window closed with OK; null otherwise.</summary>
		public Theme? Chosen { get; private set; }

		/// <summary>The theme picked in the list, which is the one the drawing is in.</summary>
		public Theme? Picked => _picked >= 0 && _picked < _shown.Count ? _shown[_picked] : null;

		/// <summary>The names in the list, in its order, for tests.</summary>
		public IReadOnlyList<string> ListedNames => _shown.Select(static t => t.Name).ToList();

		public ThemePreview Preview => _preview;

		public string TitleText => _title.Text;

		public string AboutText => _about.Text;

		public string BylineText => _byline.Text;

		public string StatusText => _status.Text;

		/// <summary>Picks a theme in the list by name, as a click on it would.</summary>
		public void Pick(string name)
		{
			var at = _shown.FindIndex(t => string.Equals(t.Name, name, StringComparison.OrdinalIgnoreCase));
			if (at < 0) return;
			var was = _ready;
			_ready = false;
			foreach (ListViewItem item in _list.SelectedItems) item.Selected = false;
			_list.Items[at].Selected = true;
			_list.Items[at].Focused = true;
			_list.EnsureVisible(at);
			_ready = was;
			_picked = at;
			ShowPicked();
		}

		protected override void OnShown(EventArgs e)
		{
			base.OnShown(e);
			// a selection made before the list was on screen (see _picked) is made again now that it is
			if (Picked is { } picked) Pick(picked.Name);
		}

		/// <summary>OK: the picked theme is the answer.</summary>
		public void Accept()
		{
			if (Picked is not { } picked) return;
			Chosen = picked;
			DialogResult = DialogResult.OK;
			Close();
		}

		/// <summary>
		/// The two Chimera has built in first, as it has always listed them; then the
		/// files, by name, because sixty themes in the order of their file names would
		/// be in nobody's order.
		/// </summary>
		private void Fill(string pick)
		{
			_ready = false;
			_picked = -1;
			var all = ThemeLibrary.All;
			_shown = all.Where(ThemeLibrary.IsBuiltIn)
				.Concat(all.Where(static t => !ThemeLibrary.IsBuiltIn(t)).OrderBy(static t => t.Name, StringComparer.InvariantCultureIgnoreCase))
				.ToList();
			_list.BeginUpdate();
			_list.Items.Clear();
			foreach (var theme in _shown)
			{
				ListViewItem item = new(theme.Name);
				item.SubItems.Add(theme.IsDark ? "Dark" : "Light");
				if (!string.IsNullOrWhiteSpace(theme.Description)) item.ToolTipText = theme.Description;
				_list.Items.Add(item);
			}
			_list.EndUpdate();
			_ready = true;
			Pick(pick);
			if (Picked is null) Pick(_inUse);
		}

		private void ShowPicked()
		{
			var theme = Picked;
			_preview.Previewed = theme;
			if (theme is null)
			{
				_title.Text = _about.Text = _byline.Text = "";
				return;
			}
			var inUse = string.Equals(theme.Name, _inUse, StringComparison.OrdinalIgnoreCase);
			_title.Text = inUse ? $"{theme.Name} (in use)" : theme.Name;
			// a description may be a poem; the box shows its first lines and the list's tooltip all of it
			_about.Text = theme.Description.Replace("\r\n", "\n").Replace("\n", " / ");
			List<string> by = new();
			if (!string.IsNullOrWhiteSpace(theme.Author)) by.Add($"by {theme.Author}");
			by.Add(ThemeLibrary.IsBuiltIn(theme) ? "built into Chimera" : $"file {theme.Origin}");
			_byline.Text = string.Join(" - ", by);
		}

		private void SayWhatLoaded()
		{
			var failures = ThemeLibrary.Failures;
			var where = $"{ThemeLibrary.All.Count} themes; files are read from {ThemeLibrary.ThemesDirectory}";
			if (failures.Count is 0)
			{
				_status.Text = where;
				_status.LinkArea = new(0, 0);
				_status.SetForeRole(ThemeColorRole.WindowText);
				return;
			}
			const string why = "why?";
			var text = $"{where}. {failures.Count} file{(failures.Count is 1 ? "" : "s")} would not load - {why}";
			_status.Text = text;
			_status.LinkArea = new(text.Length - why.Length, why.Length);
			_status.SetForeRole(ThemeColorRole.AccentError);
		}

		private void TellFailures()
		{
			var failures = ThemeLibrary.Failures;
			if (failures.Count is 0) return;
			_tell(string.Join("\n\n", failures.Select(static f => f.Message)), "These theme files were not loaded");
		}

		/// <summary>
		/// Re-reads the folders. The theme that is on is painted again too, because
		/// its own file may be the one somebody just edited.
		/// </summary>
		public void Reload()
		{
			var pick = Picked?.Name ?? ThemeLibrary.Current.Name;
			ThemeLibrary.Reload();
			ThemeEngine.ApplyToOpenForms(ThemeLibrary.Current);
			// the file of the theme in use may have gone, and then Light is
			_inUse = ThemeLibrary.Current.Name;
			Fill(pick);
			SayWhatLoaded();
		}

		private void OpenFolder()
		{
			var dir = ThemeLibrary.ThemesDirectory;
			try
			{
				Directory.CreateDirectory(dir);
				_openFolder(dir);
			}
			catch (Exception ex)
			{
				_status.Text = $"Could not open {dir}: {ex.Message}";
				_status.LinkArea = new(0, 0);
				_status.SetForeRole(ThemeColorRole.AccentError);
			}
		}

		/// <summary>
		/// Writes the picked theme into the themes folder as a complete file somebody
		/// can edit - which is how a theme gets made without anybody having to type a
		/// hundred colour names from memory. It is given a name of its own, and never
		/// written over a file that is already there: that file may be somebody's work.
		/// Returns where it went, or null.
		/// </summary>
		public string? WriteCopy()
		{
			var source = Picked ?? ThemeLibrary.Current;
			var dir = ThemeLibrary.ThemesDirectory;
			string name;
			string path;
			var n = 1;
			do
			{
				name = n is 1 ? $"{source.Name} copy" : $"{source.Name} copy {n}";
				path = Path.Combine(dir, FileNameFor(name));
				n++;
			}
			while (ThemeLibrary.Find(name) is not null || File.Exists(path));

			// it does NOT inherit "desktop": a theme somebody is about to give colours
			// of its own has to have them painted
			Theme theme = new(
				name: name,
				description: source.Description,
				author: source.Author,
				isDark: source.IsDark,
				followsDesktop: false,
				origin: source.Origin,
				colors: source.ToArray());
			try
			{
				Directory.CreateDirectory(dir);
				File.WriteAllText(path, ThemeFile.Write(theme));
			}
			catch (Exception ex)
			{
				_status.Text = $"Could not write {path}: {ex.Message}";
				_status.LinkArea = new(0, 0);
				_status.SetForeRole(ThemeColorRole.AccentError);
				return null;
			}
			ThemeLibrary.Reload();
			Fill(name);
			_status.Text = $"Written to {path}. Change the colours and the name inside it, then Reload Themes.";
			_status.LinkArea = new(0, 0);
			_status.SetForeRole(ThemeColorRole.WindowText);
			return path;
		}

		private static string FileNameFor(string name)
		{
			var bad = Path.GetInvalidFileNameChars();
			return new string(name.Select(c => bad.Contains(c) ? '_' : c).ToArray()) + ThemeFile.Extension;
		}
	}
}
