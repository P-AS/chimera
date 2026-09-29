using System.Linq;
using System.Windows.Forms;

using Chimera.Client.GUI;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// A core that takes no file at all - a game core whose game is all firmware,
	/// SDLPoP2's - declares an empty slot list. The file page says so, and it
	/// leaves without asking for anything.
	/// </summary>
	[TestClass]
	public class WizardNoFilesTests
	{
		[TestMethod]
		public void AnEmptyDeclarationAdvancesAndSaysSo()
		{
			NewProjectWizard form = new([ ], static _ => [ ]);
			form.Show();
			using (form)
			{
				form.UseSettingsFrom(new WaterboxConfig { Settings = [ ] });
				form.UseDeclaration(TestPackages.Slots("""{ "slots": [] }"""));
				var said = AllText(form).Any(static t => t.Contains("takes no files: the game is its firmware"));
				Assert.IsTrue(said, "the file page says the core takes no files");

				var next = form.Controls.OfType<Button>().Single(static b => b.Text.StartsWith("Next"));
				next.PerformClick();
				Assert.AreEqual("", form.StatusText, "nothing is asked of a form with no slots");
			}
		}

		private static System.Collections.Generic.IEnumerable<string> AllText(Control root)
		{
			foreach (Control child in root.Controls)
			{
				yield return child.Text;
				foreach (var t in AllText(child)) yield return t;
			}
		}
	}
}
