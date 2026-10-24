using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The New Project wizard's "The machine's clock starts at" row (issue 366):
	/// the sandbox's own second shown as it is typed, and only a time between
	/// 1970 and 2200, written as year-month-day hours:minutes:seconds, taken.
	/// </summary>
	[TestClass]
	public class WizardClockStartTests
	{
		[TestMethod]
		public void TheSandboxsOwnStartIsShownAsItIsTyped()
		{
			Assert.AreEqual("2017-05-27 12:44:28", NewProjectWizard.ClockText(1495889068));
			Assert.AreEqual(1495889068L, NewProjectWizard.ParseClockText("2017-05-27 12:44:28"));
		}

		[TestMethod]
		public void OnlyATimeBetween1970And2200IsTaken()
		{
			Assert.AreEqual(1893553445L, NewProjectWizard.ParseClockText(" 2030-01-02 03:04:05 "));
			Assert.AreEqual(1709251199L, NewProjectWizard.ParseClockText("2024-02-29 23:59:59"), "a leap day");
			Assert.AreEqual(7258118400L, NewProjectWizard.ParseClockText("2200-01-01 00:00:00"), "the last second there is");
			Assert.AreEqual(0L, NewProjectWizard.ParseClockText("1970-01-01 00:00:00"));
			foreach (var bad in new[] { "2023-02-29 00:00:00", "1969-12-31 23:59:59", "2200-01-01 00:00:01",
				"2030-01-02T03:04:05Z", "2030-01-02", "tomorrow", "" })
			{
				Assert.IsNull(NewProjectWizard.ParseClockText(bad), bad);
			}
		}
	}
}
