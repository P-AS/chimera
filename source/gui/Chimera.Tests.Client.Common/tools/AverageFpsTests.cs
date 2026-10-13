using System;

using Chimera.Emulation.Common.Engine;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// View &gt; Display Average FPS (chimera issue 265): the count is the engine's
	/// (engine.h, ce_average_fps), and these are the calls the main window makes.
	/// What counts is tested in the engine (test_average_fps.cpp); here, that the
	/// calls reach it with their arguments in the right places.
	/// </summary>
	[TestClass]
	public class AverageFpsTests
	{
		private const long Frame60 = 1_000_000_000 / 60;

		[TestMethod]
		public void FreeRunningCountsAndAPauseDoesNot()
		{
			var e = ChimeraEngine.Instance;
			IntPtr a = e.ce_average_fps_create();
			Assert.AreNotEqual(IntPtr.Zero, a);
			try
			{
				Assert.AreEqual(0.0, e.ce_average_fps_value(a));
				for (int i = 0; i < 120; i++) e.ce_average_fps_pass(a, 1, 2 * Frame60, 0, 0, 0);
				Assert.AreEqual(30.0, e.ce_average_fps_value(a), 0.001);

				// each of the three flags, in its own place, keeps a pass out
				e.ce_average_fps_pass(a, 1, 1000 * Frame60, 1, 0, 0);
				e.ce_average_fps_pass(a, 1, 1000 * Frame60, 0, 1, 0);
				e.ce_average_fps_pass(a, 1, 1000 * Frame60, 0, 0, 1);
				Assert.AreEqual(30.0, e.ce_average_fps_value(a), 0.001);

				e.ce_average_fps_reset(a);
				Assert.AreEqual(0.0, e.ce_average_fps_value(a));
				for (int i = 0; i < 60; i++) e.ce_average_fps_pass(a, 1, Frame60, 0, 0, 0);
				Assert.AreEqual(60.0, e.ce_average_fps_value(a), 0.001);
			}
			finally
			{
				e.ce_average_fps_destroy(a);
			}
		}
	}
}
