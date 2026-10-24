using Chimera.Client.GUI;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// TAStudio's Paste reads lines that Copy wrote with "Include frame numbers
	/// when copying input" on: the frame number in front is dropped, where it was
	/// read as buttons held.
	/// </summary>
	[TestClass]
	public class TAStudioPasteTests
	{
		[TestMethod]
		public void APastedLineLosesItsFrameNumber()
		{
			Assert.AreEqual("|..U.....|", TAStudio.WithoutFrameNumber("00002 |..U.....|"));
			Assert.AreEqual("|P|..U.....|", TAStudio.WithoutFrameNumber("   12 |P|..U.....|"));
		}

		[TestMethod]
		public void OtherLinesAreLeftAsTheyAre()
		{
			Assert.AreEqual("|..U.....|", TAStudio.WithoutFrameNumber("|..U.....|"));
			Assert.AreEqual("12", TAStudio.WithoutFrameNumber("12"));
			Assert.AreEqual("12 abc", TAStudio.WithoutFrameNumber("12 abc"));
			Assert.AreEqual("", TAStudio.WithoutFrameNumber(""));
		}
	}
}
