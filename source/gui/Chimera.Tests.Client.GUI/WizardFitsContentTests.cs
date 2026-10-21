using System.Linq;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The window grows to fit what a core asks for.
	///
	/// Two of the wizard's pages hold content the CORE decides the size of - a
	/// group per declared slot, and a grid row per declared setting - so a
	/// machine with many of either outgrows a window sized for a machine with
	/// few. DOSBox-X is the case Sergio hit: everything was reachable, but only
	/// after dragging the window taller by hand, every time.
	///
	/// What is asserted here is the rule, not a number: a page with a lot in it
	/// makes the window taller than a page with little, growth never runs past
	/// the screen, and stepping back to a short page does not shrink the window
	/// again - a size somebody has chosen is theirs to keep, and a window that
	/// jumps about between steps is worse than one that is too small.
	/// </summary>
	[TestClass]
	public class WizardFitsContentTests
	{
		private static string SlotsDeclaring(int count)
		{
			var slots = string.Join(",\n    ", Enumerable.Range(0, count).Select(i =>
				$$"""{ "id": "slot{{i}}", "title": "Slot {{i}}", "min": 0, "max": -1, "formats": ["bin"] }"""));
			return $$"""
				{
				  "slots": [
				    {{slots}}
				  ]
				}
				""";
		}

		private static NewProjectWizard FormWith(int slotCount)
		{
			NewProjectWizard form = new([ ], static _ => [ ]);
			form.Show();
			form.UseDeclaration(TestPackages.Slots(SlotsDeclaring(slotCount)));
			return form;
		}

		[TestMethod]
		public void ManySlotsMakeATallerWindowThanFew()
		{
			using var few = FormWith(1);
			few.ShowPageForTest(1);
			var shortHeight = few.Height;

			using var many = FormWith(12);
			many.ShowPageForTest(1);
			var tallHeight = many.Height;

			Assert.IsTrue(
				tallHeight > shortHeight,
				$"twelve slots must not be squeezed into the window one slot needs "
					+ $"(one slot: {shortHeight}px, twelve: {tallHeight}px)");
		}

		[TestMethod]
		public void GrowingStopsAtTheScreen()
		{
			using var form = FormWith(200);
			form.ShowPageForTest(1);
			var room = System.Windows.Forms.Screen.FromControl(form).WorkingArea.Height;
			Assert.IsTrue(
				form.Height <= room,
				$"a core with two hundred slots must not open a window taller than the screen "
					+ $"({form.Height}px against {room}px of working area)");
		}

		[TestMethod]
		public void SteppingBackToAShortPageKeepsTheHeight()
		{
			using var form = FormWith(12);
			form.ShowPageForTest(1);
			var grown = form.Height;

			form.ShowPageForTest(0);
			Assert.AreEqual(
				grown,
				form.Height,
				"going back to the core picker must not shrink the window again - "
					+ "a window that resizes itself at every step is worse than one that is too small");
		}

		/// <summary>
		/// The Game step's boxes reach across the window however wide it was when the step was
		/// made, and narrowing the window to its smallest leaves every box and its buttons in
		/// view (issue 333). Made while the window was wide, the boxes kept a wide gap on their
		/// right and shrank to nothing as the window narrowed.
		/// </summary>
		[TestMethod]
		public void TheGameStepFitsAnyWidthTheWindowHad()
		{
			NewProjectWizard form = new([ ], static _ => [ ]);
			using (form)
			{
				form.Show();
				form.UseDeclaration(TestPackages.Slots(SlotsDeclaring(2)));
				// the step is made again while the window is wide, as when a core is chosen after
				// the window was widened (Windows lays out the step while it is hidden, Mono only
				// once it is shown, so it is made here while in view)
				form.Width = UIHelper.ScaleX(1100);
				form.UseDeclaration(TestPackages.Slots(SlotsDeclaring(2)));

				var groups = Descendants(form).OfType<System.Windows.Forms.GroupBox>().Where(static g => g.Text.StartsWith("Slot")).ToList();
				Assert.AreEqual(2, groups.Count);
				var host = groups[0].Parent!;
				foreach (var group in groups)
				{
					Assert.IsTrue(host.ClientSize.Width - group.Right < UIHelper.ScaleX(60),
						$"made in a wide window, the box stops {host.ClientSize.Width - group.Right}px short of the window's right edge");
				}

				form.Width = form.MinimumSize.Width;
				foreach (var group in groups)
				{
					Assert.IsTrue(group.Width >= UIHelper.ScaleX(300), $"narrowed, the box is {group.Width}px wide");
					Assert.IsTrue(group.Right <= host.ClientSize.Width, $"narrowed, the box runs {group.Right - host.ClientSize.Width}px past the window");
					foreach (System.Windows.Forms.Control inside in group.Controls)
					{
						Assert.IsTrue(inside.Left >= 0 && inside.Right <= group.ClientSize.Width && inside.Width > 0,
							$"narrowed, \"{inside.Text}\" is at {inside.Left}..{inside.Right} in a box {group.ClientSize.Width}px wide");
					}
				}
			}
		}

		private static System.Collections.Generic.IEnumerable<System.Windows.Forms.Control> Descendants(System.Windows.Forms.Control parent)
		{
			foreach (System.Windows.Forms.Control child in parent.Controls)
			{
				yield return child;
				foreach (var below in Descendants(child)) yield return below;
			}
		}
	}
}
