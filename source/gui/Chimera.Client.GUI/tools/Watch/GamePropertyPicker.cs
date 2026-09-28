#nullable enable

using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Emulation.Common;
using Chimera.WinForms.Controls;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// RAM Watch &gt; Watches &gt; Add Game Properties: a game core's properties by name,
	/// grouped as the core groups them, to tick and add as watches (docs/game-cores.md).
	/// An array is listed element by element (<c>Guards.X[2]</c>), each a watch of its
	/// own. One already watched is shown in grey rather than offered twice.
	/// </summary>
	public sealed class GamePropertyPicker : FormBase
	{
		private readonly ListView _list;
		private readonly Button _add;

		protected override string WindowTitleStatic => "Add Game Properties";

		/// <param name="properties">the core's properties</param>
		/// <param name="valueOf">an element's current value as a person reads it</param>
		/// <param name="watched">whether an element is already in the watch list</param>
		public GamePropertyPicker(IGameProperties properties, Func<GamePropertyElement, string> valueOf, Func<GamePropertyElement, bool> watched)
		{
			SuspendLayout();
			ClientSize = new(UIHelper.ScaleX(620), UIHelper.ScaleY(420));
			MinimizeBox = false;
			StartPosition = FormStartPosition.CenterParent;
			var margin = UIHelper.ScaleX(10);

			Label intro = new()
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right,
				Location = new(margin, UIHelper.ScaleY(8)),
				Size = new(ClientSize.Width - (2 * margin), UIHelper.ScaleY(20)),
				Text = "Tick the properties to watch. Each is added under its own name, and a freeze of it keeps that name.",
			};

			_list = new ListView
			{
				Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right,
				CheckBoxes = true,
				FullRowSelect = true,
				HideSelection = false,
				Location = new(margin, UIHelper.ScaleY(32)),
				Size = new(ClientSize.Width - (2 * margin), ClientSize.Height - UIHelper.ScaleY(76)),
				View = View.Details,
			};
			_list.Columns.Add("Property", UIHelper.ScaleX(180));
			_list.Columns.Add("Type", UIHelper.ScaleX(50));
			_list.Columns.Add("Value", UIHelper.ScaleX(90));
			_list.Columns.Add("Description", UIHelper.ScaleX(270));

			// the core's groups in the order it first names them, each under a grey row:
			// Mono's ListView ignores groups in Details view, so a divide is a row here as
			// in every other list
			foreach (var group in properties.Properties.GroupBy(static p => p.Group))
			{
				if (group.Key.Length is not 0)
				{
					ListViewItem heading = new(group.Key) { Tag = null, ForeColor = ThemeEngine.Color(ThemeColorRole.DisabledText) };
					for (var i = 1; i < _list.Columns.Count; i++) heading.SubItems.Add("");
					_list.Items.Add(heading);
				}
				foreach (var element in group.SelectMany(static p => p.Elements))
				{
					var property = element.Property;
					var already = watched(element);
					ListViewItem row = new(element.Name)
					{
						Tag = already ? null : element,
						ToolTipText = property.Description,
						ForeColor = ThemeEngine.Color(already || !property.Writable ? ThemeColorRole.DisabledText : ThemeColorRole.InputText),
					};
					row.SubItems.Add(property.TypeName
						+ (property.Type is GamePropertyType.String or GamePropertyType.Bytes ? $"({property.Size})" : "")
						+ (property.IsBitField ? $":{property.Bits}" : ""));
					row.SubItems.Add(valueOf(element));
					row.SubItems.Add(already ? "(already watched)" : property.Writable ? property.Description : $"{property.Description} (read-only)".TrimStart());
					_list.Items.Add(row);
				}
			}
			// only a property that is offered may be ticked
			_list.ItemCheck += (_, e) =>
			{
				if (e.Index >= 0 && e.Index < _list.Items.Count && _list.Items[e.Index].Tag is null) e.NewValue = CheckState.Unchecked;
			};
			_list.ItemChecked += (_, _) => _add.Enabled = Chosen.Count is not 0;

			_add = new Button
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Right,
				DialogResult = DialogResult.OK,
				Enabled = false,
				Location = new(ClientSize.Width - margin - UIHelper.ScaleX(180) - UIHelper.ScaleX(6), ClientSize.Height - UIHelper.ScaleY(36)),
				Size = new(UIHelper.ScaleX(90), UIHelper.ScaleY(26)),
				Text = "Add",
			};
			Button cancel = new()
			{
				Anchor = AnchorStyles.Bottom | AnchorStyles.Right,
				DialogResult = DialogResult.Cancel,
				Location = new(ClientSize.Width - margin - UIHelper.ScaleX(90), ClientSize.Height - UIHelper.ScaleY(36)),
				Size = new(UIHelper.ScaleX(90), UIHelper.ScaleY(26)),
				Text = "Cancel",
			};

			Controls.AddRange(new Control[] { intro, _list, _add, cancel });
			AcceptButton = _add;
			CancelButton = cancel;
			ResumeLayout();
		}

		/// <summary>What is ticked, in list order.</summary>
		public IReadOnlyList<GamePropertyElement> Chosen
			=> _list.Items.Cast<ListViewItem>().Where(static i => i.Checked && i.Tag is GamePropertyElement).Select(static i => (GamePropertyElement)i.Tag).ToList();

		/// <summary>The rows as listed, a group's heading by its name: for tests.</summary>
		public IReadOnlyList<string> Rows => _list.Items.Cast<ListViewItem>().Select(static i => i.Text).ToList();

		/// <summary>Ticks a property by name, as a person would: for tests.</summary>
		public void Tick(string name)
		{
			foreach (ListViewItem item in _list.Items)
			{
				if (item.Text == name) item.Checked = true;
			}
		}
	}
}
