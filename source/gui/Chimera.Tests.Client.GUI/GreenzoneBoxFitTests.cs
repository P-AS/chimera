using System.Drawing;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The Greenzone box shows all three of its choices whatever font the desktop uses (issue #311): on Linux the
	/// buttons were spaced for a larger font while the box kept its designed height, and "Off" fell below it.
	/// </summary>
	[TestClass]
	public class GreenzoneBoxFitTests
	{
		[TestMethod]
		public void OffIsInsideTheBoxWithALargerFont()
		{
			foreach (var size in new[] { 8.25f, 10f, 12f, 14f })
			{
				using Form form = new() { AutoScaleMode = AutoScaleMode.None, Width = 400, Height = 400 };
				using Font font = new(form.Font.FontFamily, size);
				form.Font = font;
				GreenzoneBox box = new() { Dock = DockStyle.Top, Size = new Size(200, 80) };
				form.Controls.Add(box);
				form.Show();
				Application.DoEvents();

				var off = AllControls(box).Single(static c => c is RadioButton { Text: "Off" });
				var bottom = box.PointToClient(off.PointToScreen(new Point(0, off.Height))).Y;
				form.Close();
				Assert.IsTrue(bottom <= box.ClientSize.Height,
					$"at {size}pt the Off button ends at {bottom}, below the box's {box.ClientSize.Height}");
			}
		}

		private static System.Collections.Generic.IEnumerable<Control> AllControls(Control parent)
			=> parent.Controls.Cast<Control>().SelectMany(static c => new[] { c }.Concat(AllControls(c)));
	}
}
