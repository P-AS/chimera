using System.Collections.Generic;
using System.Linq;

using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// A project's saved piano-roll columns are fitted to the controller the core
	/// declares when the project is opened (issue #291). An Amiga CD32 project
	/// whose saved columns lacked one of the controller's controls did not open:
	/// setting the missing column's width read it as null.
	/// </summary>
	[TestClass]
	public class TAStudioColumnsFitTests
	{
		private static readonly (string Name, string Mnemonic)[] Controls =
		[
			("P1 Up", "U"),
			("P1 Fire", "F"),
			("P1 Play", "P"),
			("Key A", "A"),
			("Mouse X", "MX"),
		];

		private static List<RollColumn> Fit(IEnumerable<RollColumn> saved)
			=> TAStudio.FitColumnsToControls(saved, Controls, static name => name == "Mouse X");

		private static RollColumn Column(string name, string text = "") => new(name: name, widthUnscaled: 10, text: text);

		/// <summary>
		/// The controls the save lacks get the columns a new piano roll would give
		/// them, in the controller's order, and what the user set on a saved column
		/// stays.
		/// </summary>
		[TestMethod]
		public void AControlTheSaveLacksGetsItsColumn()
		{
			var hiddenFire = Column("P1 Fire", "F");
			hiddenFire.Visible = false;
			var saved = new List<RollColumn> { Column("CursorColumn"), Column("FrameColumn", "Frame#"), Column("P1 Up", "U"), hiddenFire };

			var fitted = Fit(saved);

			CollectionAssert.AreEqual(
				new[] { "CursorColumn", "FrameColumn", "P1 Up", "P1 Fire", "P1 Play", "Key A", "Mouse X" },
				fitted.Select(static c => c.Name).ToArray());
			Assert.AreSame(hiddenFire, fitted[3], "the saved column is the one kept");
			Assert.IsFalse(fitted[3].Visible, "hidden by the user, still hidden");
			Assert.IsTrue(fitted[4].Visible, "a new button column is shown");
			Assert.AreEqual("P", fitted[4].Text);
			Assert.IsFalse(fitted[5].Visible, "a keyboard key is hidden, as in a new roll");
			Assert.IsTrue(fitted[6].Rotatable, "an axis column is rotatable");
		}

		/// <summary>A column for a control the controller no longer has is left out.</summary>
		[TestMethod]
		public void AColumnForAControlThatIsGoneIsLeftOut()
		{
			var saved = new List<RollColumn> { Column("CursorColumn"), Column("FrameColumn"), Column("P2 Up"), Column("P1 Up") };

			var names = Fit(saved).Select(static c => c.Name).ToList();

			CollectionAssert.DoesNotContain(names, "P2 Up");
			Assert.AreEqual(2 + Controls.Length, names.Count);
		}

		/// <summary>A save with no columns at all gets the cursor and frame columns too.</summary>
		[TestMethod]
		public void AnEmptySaveGetsEveryColumn()
		{
			var names = Fit([]).Select(static c => c.Name).ToList();

			Assert.AreEqual("CursorColumn", names[0]);
			Assert.AreEqual("FrameColumn", names[1]);
			Assert.AreEqual(2 + Controls.Length, names.Count);
		}
	}
}
