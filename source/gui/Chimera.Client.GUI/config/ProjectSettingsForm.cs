#nullable enable

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Windows.Forms;

using Chimera.Emulation.Common.Engine;
using Chimera.Emulation.Common.Waterbox;

using Newtonsoft.Json;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// The settings of the open project, changed after the project was made (issue #370,
	/// user-decided 2026-10-25). Trying a renderer or an anti-aliasing mode used to mean a
	/// new project for each.
	///
	/// A change is STRUCTURAL (docs/project.md, Editing): the machine restarts at frame 0 with
	/// the new settings, the greenzone goes with the old machine, and the inputs are kept and
	/// play again - whether they still do what they did is the person's call, and the window
	/// says so before anything happens. This window only collects the values; MainForm asks
	/// and restarts (ApplyProjectSettings).
	///
	/// The settings shown are the ones the New Project wizard shows for the same files and
	/// values (the engine decides which are exposed), with two differences: the renderer is
	/// here, because the wizard asks it on its first page; and the machine is not, because the
	/// machine decides which files the project takes, and those are the project's.
	/// </summary>
	public sealed class ProjectSettingsForm : FormBase
	{
		private const string TITLE = "Project Settings";

		private readonly WaterboxConfig _cfg;
		private readonly string _slotsJson;
		private readonly IReadOnlyList<WaterboxConfig.SettingDecl> _gameSettings;
		private readonly WaterboxCoreSettings _settings;
		private readonly string _before;
		private readonly long _clockBefore;
		private readonly PropertyGrid _grid;
		private readonly TextBox _clockStart;
		private readonly Button _restart;
		private readonly Label _status;

		/// <summary>The values as the window leaves them.</summary>
		public WaterboxCoreSettings Settings => _settings;

		/// <summary>The clock's start as the window leaves it, or as it was when the text cannot be read.</summary>
		public long ClockStart => NewProjectWizard.ParseClockText(_clockStart.Text) ?? _clockBefore;

		/// <summary>Whether anything the machine is made from differs from what it was made from.</summary>
		public bool Changed => MachineText(_cfg, _settings) != _before || ClockStart != _clockBefore;

		protected override string WindowTitleStatic => TITLE;

		/// <param name="cfg">the running core's package declaration</param>
		/// <param name="current">the values the machine runs with now</param>
		/// <param name="gameSettings">the loaded game's own settings, which only its core can name</param>
		/// <param name="slotsJson">the project's files by slot, which decide some settings' existence</param>
		/// <param name="clockStart">the second the machine's clock starts at</param>
		public ProjectSettingsForm(WaterboxConfig cfg, WaterboxCoreSettings current,
			IReadOnlyList<WaterboxConfig.SettingDecl> gameSettings, string slotsJson, long clockStart)
		{
			_cfg = cfg;
			_slotsJson = slotsJson;
			_gameSettings = gameSettings;
			_settings = current.Clone();
			_before = MachineText(cfg, _settings);
			_clockBefore = clockStart;

			SuspendLayout();
			FormBorderStyle = FormBorderStyle.Sizable;
			MaximizeBox = false;
			MinimizeBox = false;
			ShowIcon = false;
			ShowInTaskbar = false;
			StartPosition = FormStartPosition.CenterParent;

			var margin = UIHelper.ScaleX(12);
			var width = UIHelper.ScaleX(600);
			var y = UIHelper.ScaleY(12);

			Controls.Add(new Label
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
				AutoSize = false,
				Location = new(margin, y),
				Size = new(width - (2 * margin), UIHelper.ScaleY(48)),
				Text = "A change restarts the machine at frame 0 with the new settings. The greenzone is cleared;"
					+ " the inputs are kept and play again, but they may not do what they did before."
					+ " The machine itself goes with the project's files and is not changed here.",
			});
			y += UIHelper.ScaleY(54);

			_grid = new PropertyGrid
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				Location = new(margin, y),
				Size = new(width - (2 * margin), UIHelper.ScaleY(330)),
				ToolbarVisible = false,
				PropertySort = PropertySort.Alphabetical,
			};
			_grid.PropertyValueChanged += (_, _) =>
			{
				RefreshExposedSettings();
				UpdateRestart();
			};
			Controls.Add(_grid);
			y += _grid.Height + UIHelper.ScaleY(8);

			// the machine's own, not a core's setting: under the grid, as in the wizard
			var clockLabel = new Label
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left,
				AutoSize = true,
				Location = new(margin, y + UIHelper.ScaleY(4)),
				Text = "The machine's clock starts at:",
			};
			_clockStart = new TextBox
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left,
				Location = new(margin + clockLabel.PreferredSize.Width + UIHelper.ScaleX(6), y),
				Size = new(UIHelper.ScaleX(140), UIHelper.ScaleY(23)),
				Text = NewProjectWizard.ClockText(clockStart),
			};
			_clockStart.TextChanged += (_, _) => UpdateRestart();
			var clockUtc = new Label
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left,
				AutoSize = true,
				Location = new(_clockStart.Right + UIHelper.ScaleX(6), y + UIHelper.ScaleY(4)),
				Text = "UTC",
			};
			Controls.AddRange([ clockLabel, _clockStart, clockUtc ]);
			y += UIHelper.ScaleY(34);

			_status = new Label
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				AutoSize = false,
				Location = new(margin, y + UIHelper.ScaleY(5)),
				Size = new(width - (2 * margin) - (2 * UIHelper.ScaleX(150)) - UIHelper.ScaleX(16), UIHelper.ScaleY(20)),
			};
			_restart = new Button
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Right,
				DialogResult = DialogResult.OK,
				Location = new(width - margin - (2 * UIHelper.ScaleX(150)) - UIHelper.ScaleX(8), y),
				Size = new(UIHelper.ScaleX(150), UIHelper.ScaleY(26)),
				Text = "Restart with these",
			};
			var cancel = new Button
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Right,
				DialogResult = DialogResult.Cancel,
				Location = new(width - margin - UIHelper.ScaleX(150), y),
				Size = new(UIHelper.ScaleX(150), UIHelper.ScaleY(26)),
				Text = "Cancel",
			};
			Controls.AddRange([ _status, _restart, cancel ]);
			AcceptButton = _restart;
			CancelButton = cancel;

			ClientSize = new(width, y + UIHelper.ScaleY(38));
			MinimumSize = Size;
			ResumeLayout();

			// after the grid has a parent: the description box goes in beside it
			_grid.UseScrollableDescription();
			RefreshExposedSettings();
			_grid.SelectedObject = _settings;
			UpdateRestart();
		}

		/// <summary>
		/// The settings the engine exposes for these files and values, as the wizard's
		/// settings page has them (NewProjectWizard.RefreshExposedSettings): a value can
		/// expose further settings, so this runs again after every change.
		/// </summary>
		private void RefreshExposedSettings()
		{
			var effective = WaterboxCore.EffectiveSettingsFor(_cfg, _settings);
			var exposed = EngineSettingsGate.Evaluate(_cfg.RawSettingsJson, _slotsJson, JsonConvert.SerializeObject(effective));
			// indexed against the PACKAGE's list, which is the one the engine counts in
			var all = _cfg.SettingsByDeclarationIndexFor(_cfg.MachineFor(effective));
			var declarations = exposed
				.Where(entry => entry.Index >= 0 && entry.Index < all.Count
					&& all[entry.Index] is { } decl && decl.Name == entry.Name)
				.Select(entry => all[entry.Index])
				.Where(decl => decl.Name != _cfg.MachineSetting)
				.Concat(_gameSettings)
				.ToList();
			var shown = _settings.Declarations;
			if (shown is not null && shown.Count == declarations.Count
				&& shown.Zip(declarations, static (a, b) => ReferenceEquals(a, b)).All(static same => same))
			{
				return;
			}
			_settings.Declarations = declarations;
			_grid.Refresh();
		}

		private void UpdateRestart()
		{
			var clockReadable = NewProjectWizard.ParseClockText(_clockStart.Text) is not null;
			_restart.Enabled = clockReadable && Changed;
			_status.Text = !clockReadable
				? "The clock's start reads as year-month-day hours:minutes:seconds, from 1970 to 2200."
				: Changed ? "" : "Nothing is changed yet.";
		}

		/// <summary>The settings the grid shows, by name.</summary>
		internal IReadOnlyList<string> ShownSettings => _settings.Declarations?.Select(static d => d.Key).ToList() ?? [ ];

		/// <summary>Whether the window offers to restart.</summary>
		internal bool RestartOffered => _restart.Enabled;

		/// <summary>Sets a value as the grid does, and lets the window answer it.</summary>
		internal void SetValueForTest(string name, object value)
		{
			_settings.Values[name] = value;
			RefreshExposedSettings();
			UpdateRestart();
		}

		/// <summary>The clock's start as typed.</summary>
		internal string ClockTextForTest
		{
			get => _clockStart.Text;
			set => _clockStart.Text = value;
		}

		/// <summary>
		/// What the machine is made from, as text that is the same for the same machine:
		/// every declared setting at its effective value, and every value set beyond them
		/// (a game's own settings), in name order.
		/// </summary>
		internal static string MachineText(WaterboxConfig cfg, WaterboxCoreSettings settings)
		{
			SortedDictionary<string, string> all = new(StringComparer.Ordinal);
			foreach (var (key, value) in WaterboxCore.EffectiveSettingsFor(cfg, settings)) all[key] = Convert.ToString(value, CultureInfo.InvariantCulture) ?? "";
			foreach (var (key, value) in settings.Values ?? new()) all[key] = Convert.ToString(value, CultureInfo.InvariantCulture) ?? "";
			return string.Join("\n", all.Select(static pair => pair.Key + "=" + pair.Value));
		}
	}
}
