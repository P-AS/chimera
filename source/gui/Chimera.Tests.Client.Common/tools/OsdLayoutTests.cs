using System.Collections.Generic;
using System.Drawing;
using System.Linq;

using Chimera.Client.Common;
using Chimera.Emulation.Common;
using Chimera.Tests.Client.Common.Movie;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// The speed, the average speed and the frame counter are drawn where none covers another (issue #312): with
	/// all three on, the average went one line under the speed, which is the frame counter's line.
	/// </summary>
	[TestClass]
	public class OsdLayoutTests
	{
		/// <summary>Records what is drawn where; every character is 7 by 14 pixels.</summary>
		private sealed class RecordingBlitter : IBlitter
		{
			public readonly List<(string Text, Rectangle Where)> Drawn = new();

			public void DrawString(string s, Color color, int x, int y)
				=> Drawn.Add((s, new Rectangle(new Point(x, y), Size.Ceiling(MeasureString(s)))));

			public SizeF MeasureString(string s) => new(s.Length * 7, 14);

			public Rectangle ClipBounds => new(0, 0, 640, 480);

			public float Scale => 1;
		}

		[TestMethod]
		public void TheSpeedItsAverageAndTheFrameCounterDoNotOverlap()
		{
			Config config = new()
			{
				DisplayFps = true, DisplayAverageFps = true, DisplayFrameCounter = true,
				// only these three: the others are drawn from places of their own
				DisplayInput = false, DisplayLagCounter = false, DisplayGameTime = false, DisplayRerecordCount = false, DisplaySubtitles = false,
			};
			FakeEmulator emulator = new();
			FakeMovieSession session = new(emulator);
			TasMovie movie = new(session, "/fake/osd.chimeraProject");
			session.Movie = movie;
			movie.Attach(emulator);
			movie.InsertEmptyFrame(0, 10);
			InputManager input = new() { ClientControls = new Controller(new ControllerDefinition("hotkeys") { BoolButtons = { "Autohold", "Autofire" } }.MakeImmutable()) };
			OSDManager osd = new(config, emulator, input, session)
			{
				Fps = "60 fps",
				AverageFps = "avg 59.8 fps",
			};
			RecordingBlitter g = new();

			osd.DrawScreenInfo(g);

			Assert.IsTrue(g.Drawn.Any(static d => d.Text.Contains("avg 59.8 fps")), "the average is drawn");
			Assert.IsTrue(g.Drawn.Any(static d => d.Text.Contains("60 fps")), "and so is the speed");
			var places = g.Drawn.Select(static d => d.Where).Distinct().ToList();   // the lag overlay redraws the counter in place
			for (var i = 0; i < places.Count; i++)
			{
				for (var j = i + 1; j < places.Count; j++)
				{
					Assert.IsFalse(places[i].IntersectsWith(places[j]),
						$"{string.Join(" / ", g.Drawn.Select(static d => $"'{d.Text}' at {d.Where}"))}");
				}
			}
		}
	}
}
