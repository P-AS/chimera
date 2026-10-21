using System.Reflection;
using System.Threading;

using Chimera.Client.Common;
using Chimera.Client.GUI;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Issue 346: after a slow frame the auto frame skip's controller moves the
	/// throttle's timetable back by a frame, so that the frames it skips can
	/// catch up. At Never skip it can skip nothing, and the credit only made the
	/// throttle run ahead of the core's rate. Measured on the timetable itself
	/// (the throttle's private ltime), with the throttle's own sleep left out.
	/// </summary>
	[TestClass]
	public class ThrottleFrameSkipTests
	{
		private static readonly FieldInfo Timetable = typeof(Throttle).GetField("ltime", BindingFlags.NonPublic | BindingFlags.Instance)!;

		/// <summary>The timetable's move over one frame that took 60 ms at a 60 fps rate.</summary>
		private static long MoveAfterASlowFrame(int frameSkip)
		{
			var config = new Config { FrameSkip = frameSkip, AutoMinimizeSkipping = true, ClockThrottle = true };
			var throttle = new Throttle();
			throttle.SetCoreFps(60);
			throttle.SetSpeedPercent(100);
			throttle.Step(config, null!, allowSleep: false, forceFrameSkip: -1);
			var before = (ulong) Timetable.GetValue(throttle)!;
			Thread.Sleep(60);
			throttle.Step(config, null!, allowSleep: false, forceFrameSkip: -1);
			return (long) ((ulong) Timetable.GetValue(throttle)! - before);
		}

		[TestMethod]
		public void AtNeverSkipASlowFrameGivesTheTimetableNoCredit()
			=> Assert.AreEqual(0L, MoveAfterASlowFrame(0), "at Never skip the timetable moved: the throttle would run ahead");

		[TestMethod]
		public void WhereFramesCanBeSkippedTheCreditStays()
			=> Assert.IsTrue(MoveAfterASlowFrame(1) < 0, "at frame skip 1 a slow frame gives the timetable a frame back, so that skipping catches up");
	}
}
