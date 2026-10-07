#nullable enable

using System;
using System.Drawing;
using System.Windows.Forms;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// What a fresh install is told, once: Chimera ships no cores and downloads
	/// none, so until somebody puts one in the cores folder nothing it can open
	/// exists.
	///
	/// A sentence and a choice, rather than the Core Manager opening by itself.
	/// Somebody who has just started the program for the first time should meet
	/// the program, not a window they did not ask for over an application they
	/// have not seen yet.
	/// </summary>
	public sealed class CoreManagerPrompt : FormBase
	{
		protected override string WindowTitleStatic => "No cores yet";

		/// <param name="coresFolder">where core packages are looked for</param>
		public CoreManagerPrompt(string coresFolder)
		{
			SuspendLayout();
			FormBorderStyle = FormBorderStyle.FixedDialog;
			StartPosition = FormStartPosition.CenterParent;
			MaximizeBox = false;
			MinimizeBox = false;
			ShowIcon = false;
			ShowInTaskbar = false;

			var margin = UIHelper.ScaleX(12);
			Label message = new()
			{
				AutoSize = true,
				Location = new(margin, UIHelper.ScaleY(16)),
				Text = "There are no cores yet, and Chimera downloads none. Get a core's package from its project, or build it,"
					+ Environment.NewLine + $"and put it in {coresFolder}",
			};

			// The sentence decides how wide the window is, rather than a width
			// decided here deciding where the sentence breaks. A guessed width holds
			// only for the font and DPI it was guessed at; measuring holds for both.
			var buttonWidth = UIHelper.ScaleX(150);
			var textSize = TextRenderer.MeasureText(message.Text, Font, Size.Empty, TextFormatFlags.NoPadding);
			var textWidth = textSize.Width;
			var buttonsWidth = (2 * buttonWidth) + UIHelper.ScaleX(8);
			ClientSize = new(
				Math.Max(textWidth, buttonsWidth) + (2 * margin) + UIHelper.ScaleX(4),
				UIHelper.ScaleY(74) + textSize.Height);

			var buttonRow = ClientSize.Height - UIHelper.ScaleY(38);

			Button open = new()
			{
				DialogResult = DialogResult.OK,
				Location = new(ClientSize.Width - margin - buttonWidth, buttonRow),
				Size = new(buttonWidth, UIHelper.ScaleY(26)),
				Text = "Open Core Manager",
			};

			Button cancel = new()
			{
				DialogResult = DialogResult.Cancel,
				Location = new(ClientSize.Width - margin - (2 * buttonWidth) - UIHelper.ScaleX(8), buttonRow),
				Size = new(buttonWidth, UIHelper.ScaleY(26)),
				Text = "Cancel",
			};

			Controls.AddRange(new Control[] { message, cancel, open });
			AcceptButton = open;
			CancelButton = cancel;
			ResumeLayout();
		}
	}
}
