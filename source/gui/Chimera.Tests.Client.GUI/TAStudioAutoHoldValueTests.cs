using System.Collections.Generic;

using Chimera.Client.Common;
using Chimera.Client.GUI;
using Chimera.Common;
using Chimera.Emulation.Common;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Auto-hold on an analog axis holds the value the user chose when turning it
	/// on, and the choice is kept in the project (issue #298, user-decided
	/// 2026-10-17). It used to hold the axis's maximum, always.
	/// </summary>
	[TestClass]
	public class TAStudioAutoHoldValueTests
	{
		private static readonly AxisSpec Stick = new((-128).RangeTo(127), 0);

		/// <summary>A chosen value is held; with none chosen, the maximum, as before.</summary>
		[TestMethod]
		public void TheChosenValueIsHeld()
		{
			var chosen = new Dictionary<string, int> { ["P1 X Axis"] = -40 };

			Assert.AreEqual(-40, TAStudio.AxisHoldValue(chosen, "P1 X Axis", Stick));
			Assert.AreEqual(127, TAStudio.AxisHoldValue(chosen, "P1 Y Axis", Stick), "another axis, nothing chosen");
			Assert.AreEqual(127, TAStudio.AxisHoldValue(null, "P1 X Axis", Stick), "no choices at all");
		}

		/// <summary>
		/// A value the axis cannot take (a project made with another controller, or
		/// edited by hand) holds the maximum rather than an impossible value.
		/// </summary>
		[TestMethod]
		public void AValueOutsideTheRangeHoldsTheMaximum()
		{
			var chosen = new Dictionary<string, int> { ["P1 X Axis"] = 32767 };

			Assert.AreEqual(127, TAStudio.AxisHoldValue(chosen, "P1 X Axis", Stick));
		}

		/// <summary>What the user types is taken only when it is a whole number the axis can take.</summary>
		[TestMethod]
		public void OnlyAValueOfTheAxisIsTaken()
		{
			Assert.AreEqual(-128, TAStudio.ParseAxisHoldValue("-128", Stick));
			Assert.AreEqual(12, TAStudio.ParseAxisHoldValue(" 12 ", Stick));
			Assert.IsNull(TAStudio.ParseAxisHoldValue("128", Stick), "above the range");
			Assert.IsNull(TAStudio.ParseAxisHoldValue("-129", Stick), "below the range");
			Assert.IsNull(TAStudio.ParseAxisHoldValue("", Stick), "nothing typed");
			Assert.IsNull(TAStudio.ParseAxisHoldValue("1.5", Stick), "not a whole number");
		}

		/// <summary>The choices travel in the project's TAStudio settings and come back.</summary>
		[TestMethod]
		public void TheChoicesRoundTripThroughTheProject()
		{
			var settings = new TAStudio.MovieClientSettings();
			settings.AxisHoldValues["P1 X Axis"] = -40;
			settings.AxisHoldValues["P1 Y Axis"] = 90;

			var loaded = (TAStudio.MovieClientSettings)ConfigService.LoadWithType(ConfigService.SaveWithType(settings));

			Assert.AreEqual(2, loaded.AxisHoldValues.Count);
			Assert.AreEqual(-40, loaded.AxisHoldValues["P1 X Axis"]);
			Assert.AreEqual(90, loaded.AxisHoldValues["P1 Y Axis"]);
		}

		/// <summary>A project saved before the choice existed opens with none chosen.</summary>
		[TestMethod]
		public void AProjectFromBeforeHasNoChoices()
		{
			var saved = ConfigService.SaveWithType(new TAStudio.MovieClientSettings());
			var older = saved.Replace("\"AxisHoldValues\":{}", "").Replace(",}", "}").Replace("{,", "{").Replace(",,", ",");
			StringAssert.DoesNotMatch(older, new System.Text.RegularExpressions.Regex("AxisHoldValues"));

			var loaded = (TAStudio.MovieClientSettings)ConfigService.LoadWithType(older);

			Assert.IsNotNull(loaded.AxisHoldValues);
			Assert.AreEqual(0, loaded.AxisHoldValues.Count);
			Assert.AreEqual(127, TAStudio.AxisHoldValue(loaded.AxisHoldValues, "P1 X Axis", Stick));
		}
	}
}
