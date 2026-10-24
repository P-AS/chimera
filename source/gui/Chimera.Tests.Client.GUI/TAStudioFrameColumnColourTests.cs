using System.Drawing;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Set Colors' "Frame# Column" and "Frame# Column (Lag)" colour the piano
	/// roll's frame numbers on frames the greenzone holds, as they already
	/// coloured the Markers list's (owner, 2026-10-24). The piano roll washed
	/// every frame number over its row instead, so the two colours seemed to do
	/// nothing.
	/// </summary>
	[TestClass]
	public class TAStudioFrameColumnColourTests
	{
		private static readonly Color Wash = Color.FromArgb(0x60, 0xFF, 0xFF, 0xFF);

		[TestMethod]
		public void AFrameTheGreenzoneHoldsTakesTheFrameColumnColours()
		{
			var palette = TAStudioPalette.FromTheme(ThemeLibrary.Current);

			Assert.AreEqual(palette.GreenZone_FrameCol, TAStudio.FrameColumnColor(current: false, lagged: false, palette, Wash));
			Assert.AreEqual(palette.LagZone_FrameCol, TAStudio.FrameColumnColor(current: false, lagged: true, palette, Wash));
		}

		[TestMethod]
		public void TheCurrentFrameAndAFrameNotRunKeepTheirRow()
		{
			var palette = TAStudioPalette.FromTheme(ThemeLibrary.Current);

			Assert.AreEqual(Wash, TAStudio.FrameColumnColor(current: true, lagged: false, palette, Wash), "the current frame shows the current-frame colour of its row");
			Assert.AreEqual(Wash, TAStudio.FrameColumnColor(current: false, lagged: null, palette, Wash), "a frame not run as it stands");
		}
	}
}
