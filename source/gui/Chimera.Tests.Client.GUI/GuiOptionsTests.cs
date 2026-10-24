using System.Collections.Generic;
using System.Linq;
using System.Windows.Forms;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Config &gt; Customize. The owner removed three of its options on 2026-10-24
	/// (Run in background, Pause when menu activated, Skip waterbox integrity
	/// checks): they are gone, and what was below them moved up without landing
	/// on anything.
	/// </summary>
	[TestClass]
	public class GuiOptionsTests
	{
		private static IEnumerable<Control> All(Control parent)
			=> parent.Controls.Cast<Control>().SelectMany(static c => new[] { c }.Concat(All(c)));

		[TestMethod]
		public void TheRemovedOptionsAreGone()
		{
			using GuiOptions form = new(new Config(), static () => { });
			form.Show();
			var texts = All(form).Select(static c => c.Text).ToList();
			foreach (var gone in new[] { "Run in background", "Pause when menu activated", "Skip waterbox integrity checks" })
			{
				Assert.IsFalse(texts.Any(t => t.Contains(gone)), $"\"{gone}\" is still in the window");
			}
		}

		[TestMethod]
		public void NoOptionCoversAnother()
		{
			using GuiOptions form = new(new Config(), static () => { });
			form.Show();
			foreach (var page in All(form).OfType<TabPage>())
			{
				var boxes = page.Controls.OfType<CheckBox>().ToList();
				var others = page.Controls.Cast<Control>().ToList();
				foreach (var box in boxes)
				{
					foreach (var other in others.Where(o => o != box))
					{
						Assert.IsFalse(box.Bounds.IntersectsWith(other.Bounds), $"{page.Text}: \"{box.Text}\" and \"{other.Text}\" overlap");
					}
				}
			}
		}

		[TestMethod]
		public void TheWindowInEveryTheme()
		{
			using GuiOptions form = new(new Config(), static () => { });
			UiShots.Shoot(form, "customize");
		}
	}
}
