using System.Linq;

using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common.Movie
{
	/// <summary>
	/// Issue 360: a branch changes only on Add or Update, so loading another one
	/// over changes the current branch does not hold loses them. Before such a
	/// load TAStudio offers to update the current branch (owner, 2026-10-24); it
	/// asks only when the movie holds what the branch does not.
	/// </summary>
	[TestClass]
	public class TasMovieDiffersFromBranchTests
	{
		private static TasBranch BranchOf(TasMovie movie) => new()
		{
			InputLog = movie.GetLogEntries().Clone(),
			Markers = movie.Markers.DeepClone(),
		};

		[TestMethod]
		public void TheSameMovieIsNotAChange()
		{
			var movie = TasMovieTests.MakeMovie(10);
			movie.Markers.Add(4, "mine");
			var branch = BranchOf(movie);

			Assert.IsFalse(movie.DiffersFrom(branch));
		}

		[TestMethod]
		public void AnInputTheBranchDoesNotHoldIsAChange()
		{
			var movie = TasMovieTests.MakeMovie(10);
			var branch = BranchOf(movie);

			movie.SetBoolState(3, "A", true);
			Assert.IsTrue(movie.DiffersFrom(branch), "an input changed");

			movie.SetBoolState(3, "A", false);
			Assert.IsFalse(movie.DiffersFrom(branch), "changed back");

			movie.InsertEmptyFrame(10, 2);
			Assert.IsTrue(movie.DiffersFrom(branch), "frames added at the end");
		}

		[TestMethod]
		public void AMarkerOfThePersonsIsAChange()
		{
			var movie = TasMovieTests.MakeMovie(10);
			var branch = BranchOf(movie);

			movie.Markers.Add(6, "a marker");
			Assert.IsTrue(movie.DiffersFrom(branch), "a marker added");

			movie.Markers.Remove(movie.Markers.Single(m => !m.IsPermanent));
			Assert.IsFalse(movie.DiffersFrom(branch), "and removed");
		}
	}
}
