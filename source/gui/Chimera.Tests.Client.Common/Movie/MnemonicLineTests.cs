using Chimera.Client.Common;
using Chimera.Emulation.Common;

namespace Chimera.Tests.Client.Common.Movie
{
	/// <summary>
	/// A line is read from its first '|': a frame number in front of it (TAStudio's
	/// Copy with "Include frame numbers when copying input" on), or an indent, was
	/// read as buttons held. Every reader of a line goes through SetFromMnemonic:
	/// TAStudio's Paste, Lua's joypad.setfrommnemonicstr, movies and branches.
	/// </summary>
	[TestClass]
	public class MnemonicLineTests
	{
		[TestMethod]
		public void WhatComesBeforeTheFirstBarIsDropped()
		{
			Assert.AreEqual("|..U.....|", MovieController.WithoutFrameNumber("00002 |..U.....|"));
			Assert.AreEqual("|..|", MovieController.WithoutFrameNumber("00002\t|..|"));
			Assert.AreEqual("|..|", MovieController.WithoutFrameNumber("00002|..|"));
			Assert.AreEqual("|..U..|", MovieController.WithoutFrameNumber("   |..U..|"));
			Assert.AreEqual("|P|..U.....|", MovieController.WithoutFrameNumber("   12 |P|..U.....|"));
		}

		[TestMethod]
		public void OtherLinesAreLeftForTheReaderToRefuse()
		{
			Assert.AreEqual("|..U.....|", MovieController.WithoutFrameNumber("|..U.....|"));
			Assert.AreEqual("12", MovieController.WithoutFrameNumber("12"));
			Assert.AreEqual("12 abc", MovieController.WithoutFrameNumber("12 abc"));
			Assert.AreEqual("１２ |..|", MovieController.WithoutFrameNumber("１２ |..|"), "only the digits Copy writes");
			Assert.AreEqual("", MovieController.WithoutFrameNumber(""));
		}

		[TestMethod]
		public void ANumberedLineSetsOnlyItsButtons()
		{
			var machine = new ControllerDefinition("Pad") { BoolButtons = { "P1 A", "P1 B", "P1 C", "P1 D", "P1 E" } }.MakeImmutable();
			machine.BuildMnemonicsCache();
			var movie = new MovieController(machine, null);
			movie.SetFromMnemonic("00002 |A....|");
			Assert.IsTrue(movie.IsPressed("P1 A"));
			foreach (var b in new[] { "P1 B", "P1 C", "P1 D", "P1 E" }) Assert.IsFalse(movie.IsPressed(b), b);
			movie.SetFromMnemonic("  |.B...|");
			Assert.IsFalse(movie.IsPressed("P1 A"));
			Assert.IsTrue(movie.IsPressed("P1 B"));
		}
	}
}
