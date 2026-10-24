using System.Linq;
using System.Reflection;
using System.Threading;

using Chimera.Client.Common;
using Chimera.Client.GUI;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// The throttle has no frame skip any more (user-decided, 2026-10-24): every
	/// frame is drawn, except while running unthrottled (Turbo held, or Toggle
	/// Throttle), which draws one frame in Config.UnthrottledDrawOneIn.
	/// </summary>
	[TestClass]
	public class ThrottleTests
	{
		private static readonly FieldInfo Timetable = typeof(Throttle).GetField("ltime", BindingFlags.NonPublic | BindingFlags.Instance)!;

		/// <summary>Which of `frames` frames in a row are drawn.</summary>
		private static bool[] Drawn(int oneIn, bool unthrottled, int frames)
		{
			var config = new Config { UnthrottledDrawOneIn = oneIn };
			var throttle = new Throttle { signal_unthrottle = unthrottled };
			throttle.SetCoreFps(60);
			throttle.SetSpeedPercent(100);
			return Enumerable.Range(0, frames).Select(_ =>
			{
				throttle.Step(config, null!, allowSleep: false);
				return throttle.DrawNextFrame;
			}).ToArray();
		}

		[TestMethod]
		public void UnthrottledDrawsOneFrameInTheNumberChosen()
		{
			CollectionAssert.AreEqual(new[] { true, false, false, true, false, false }, Drawn(3, unthrottled: true, 6));
			CollectionAssert.AreEqual(new[] { true, false, true, false }, Drawn(2, unthrottled: true, 4));
			Assert.IsTrue(Drawn(1, unthrottled: true, 5).All(static d => d), "Every Frame draws every frame");
		}

		[TestMethod]
		public void ThrottledDrawsEveryFrame()
			=> Assert.IsTrue(Drawn(3, unthrottled: false, 9).All(static d => d), "only unthrottled running leaves frames undrawn");

		/// <summary>
		/// Issue 346: the auto frame skip once moved the throttle's timetable back
		/// a frame after a slow one, so that the throttle ran ahead of the core's
		/// rate. With frame skip gone nothing moves it.
		/// </summary>
		[TestMethod]
		public void ASlowFrameGivesTheTimetableNoCredit()
		{
			var config = new Config { ClockThrottle = true };
			var throttle = new Throttle();
			throttle.SetCoreFps(60);
			throttle.SetSpeedPercent(100);
			throttle.Step(config, null!, allowSleep: false);
			var before = (ulong) Timetable.GetValue(throttle)!;
			Thread.Sleep(60);
			throttle.Step(config, null!, allowSleep: false);
			Assert.AreEqual(before, (ulong) Timetable.GetValue(throttle)!);
		}
	}
}
