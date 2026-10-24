using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Issue 360: a person loading another branch over changes the current branch
	/// does not hold is asked whether to update it first (owner, 2026-10-24). Not
	/// when there is no current branch, not when it is the current branch that is
	/// loaded again, and not when nothing changed.
	/// </summary>
	[TestClass]
	public class BranchLoadAsksTests
	{
		[TestMethod]
		public void AnotherBranchOverChangesAsks()
		{
			Assert.IsTrue(BookmarksBranchesBox.AsksBeforeLoading(current: 0, target: 1, branchCount: 2, differs: () => true));
			Assert.IsFalse(BookmarksBranchesBox.AsksBeforeLoading(current: 0, target: 1, branchCount: 2, differs: () => false), "nothing changed");
		}

		[TestMethod]
		public void TheCurrentBranchAgainOrNoneDoesNotAsk()
		{
			var compared = 0;
			bool Differs() { compared++; return true; }

			Assert.IsFalse(BookmarksBranchesBox.AsksBeforeLoading(current: 1, target: 1, branchCount: 2, Differs), "going back to the current branch");
			Assert.IsFalse(BookmarksBranchesBox.AsksBeforeLoading(current: -1, target: 1, branchCount: 2, Differs), "no current branch");
			Assert.IsFalse(BookmarksBranchesBox.AsksBeforeLoading(current: 2, target: 1, branchCount: 2, Differs), "a current branch since removed");
			Assert.AreEqual(0, compared, "the movie is not compared when the answer is already known");
		}
	}
}
