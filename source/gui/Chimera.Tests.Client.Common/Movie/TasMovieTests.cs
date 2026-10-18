using System.Linq;
using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common.Movie
{
	[TestClass]
	public class TasMovieTests
	{
		internal static TasMovie MakeMovie(int numberOfFrames)
		{
			FakeEmulator emu = new FakeEmulator();
			FakeMovieSession session = new(emu);
			TasMovie movie = new(session, "/fake/path");
			session.Movie = movie;

			movie.Attach(emu);
			movie.InsertEmptyFrame(0, numberOfFrames);

			return movie;
		}

		public static void TestAllOperations(ITasMovie movie, Action PreOperation, Action PostOperation)
		{
			MovieController controllerEmpty = new MovieController(movie.Emulator.ControllerDefinition);
			string entryEmpty = LogEntryGenerator.GenerateLogEntry(controllerEmpty);
			MovieController controllerA = new MovieController(movie.Emulator.ControllerDefinition);
			controllerA.SetBool("A", true);
			string entryA = LogEntryGenerator.GenerateLogEntry(controllerA);

			// Make sure all operations actually do something.
			movie.SetBoolState(3, "A", true);

			void TestForSingleOperation(Action a)
			{
				PreOperation();
				a();
				PostOperation();
				movie.ChangeLog.Undo();
			}

			TestForSingleOperation(() => movie.RecordFrame(1, controllerA));
			TestForSingleOperation(() => movie.Truncate(3));
			TestForSingleOperation(() => movie.PokeFrame(1, controllerA));
			TestForSingleOperation(() => movie.PokeFrame(1, entryA));
			TestForSingleOperation(() => movie.ClearFrame(3));

			TestForSingleOperation(() => movie.InsertInput(2, entryA));
			TestForSingleOperation(() => movie.InsertInput(2, [ entryA, entryEmpty, entryA, entryEmpty ]));
			TestForSingleOperation(() => movie.InsertInput(2, [ controllerA, controllerEmpty, controllerA, controllerEmpty ]));

			TestForSingleOperation(() => movie.RemoveFrame(2));
			TestForSingleOperation(() => movie.RemoveFrames(2, 4));
			TestForSingleOperation(() => movie.RemoveFrames([ 2, 4, 6 ]));

			TestForSingleOperation(() => movie.CopyOverInput(2, [ controllerA, controllerEmpty ]));
			TestForSingleOperation(() => movie.InsertEmptyFrame(2, 2));

			TestForSingleOperation(() => movie.ToggleBoolState(2, "B"));
			TestForSingleOperation(() => movie.SetBoolState(3, "B", true));
			TestForSingleOperation(() => movie.SetBoolStates(3, 2, "B", true));

			TestForSingleOperation(() => movie.SetAxisState(2, "Stick", 10));
			TestForSingleOperation(() => movie.SetAxisStates(3, 2, "Stick", 20));

			// actions that can also extend the movie
			TestForSingleOperation(() => movie.CopyOverInput(9, [ controllerA, controllerEmpty, controllerA ]));
			TestForSingleOperation(() => movie.ToggleBoolState(15, "B"));
			TestForSingleOperation(() => movie.SetBoolState(15, "B", true));
			TestForSingleOperation(() => movie.SetBoolStates(15, 2, "B", true));
			TestForSingleOperation(() => movie.SetAxisState(15, "Stick", 10));
			TestForSingleOperation(() => movie.SetAxisStates(15, 2, "Stick", 20));
		}

#pragma warning disable BHI1600 //TODO disambiguate assert calls
		[TestMethod]
		public void GreenzoneOffStoresNothingAndOnAnchorsTheCurrentFrame()
		{
			TasMovie movie = MakeMovie(100);
			FakeEmulator emu = (FakeEmulator)movie.Emulator;
			Assert.AreEqual(1, movie.GreenzonePeriod, "a project opens storing every frame");

			movie.GreenzonePeriod = 0;
			Assert.AreEqual(0, emu.CapturePeriod);
			emu.Frame = 30;
			movie.States.Capture(30);
			Assert.IsFalse(movie.States.Has(30), "nothing is stored while off");

			emu.Frame = 45;
			movie.GreenzonePeriod = 32;
			Assert.AreEqual(32, emu.CapturePeriod);
			Assert.IsTrue(movie.States.Has(45), "turning it on stores the current frame, whatever the period");
			Assert.IsFalse(movie.States.Has(30));
			movie.States.Capture(50);
			Assert.IsFalse(movie.States.Has(50), "a sparse greenzone skips the frames between");
			movie.States.Capture(64);
			Assert.IsTrue(movie.States.Has(64));
		}

		[TestMethod]
		public void GreenzonePeriodSurvivesAReattach()
		{
			TasMovie movie = MakeMovie(10);
			movie.GreenzonePeriod = 0;
			FakeEmulator other = new FakeEmulator();
			movie.Attach(other);
			Assert.AreEqual(0, other.CapturePeriod, "a machine attached while off stays off");
		}

		[TestMethod]
		public void AllOperationsFlagChanges()
		{
			ITasMovie movie = MakeMovie(10);

			TestAllOperations(movie,
				movie.ClearChanges,
				() => Assert.IsTrue(movie.Changes)
			);
		}

		[TestMethod]
		public void AllOperationsProduceSingleInvalidation()
		{
			ITasMovie movie = MakeMovie(10);
			int invalidations = 0;
			movie.GreenzoneInvalidated = (_) => invalidations++;

			TestAllOperations(movie,
				() => invalidations = 0,
				() => Assert.AreEqual(1, invalidations)
			);
		}
#pragma warning restore BHI1600

		[TestMethod]
		public void CanTruncateLastFrame()
		{
			// arrange
			ITasMovie movie = MakeMovie(2);

			// act
			movie.Truncate(1);

			// assert
			Assert.AreEqual(1, movie.InputLogLength);
		}

		[TestMethod]
		public void MultitrackClearFrame()
		{
			// arrange
			ITasMovie movie = MakeMovie(2);
			IMovieController controller = movie.Session.MovieController;
			controller.SetBool("A", true);
			controller.SetBool("B", true);
			movie.PokeFrame(0, controller);
			MovieBaseTests.SetSingleActiveInput(movie, "A");

			// act
			movie.ClearFrame(0);

			// assert
			Assert.IsFalse(movie.GetInputState(0).IsPressed("A"));
			Assert.IsTrue(movie.GetInputState(0).IsPressed("B"));
		}

		[TestMethod]
		public void MultitrackRemoveFrame()
		{
			// arrange
			ITasMovie movie = MakeMovie(2);
			IMovieController controller = movie.Session.MovieController;
			controller.SetBool("A", true);
			controller.SetBool("B", true);
			movie.PokeFrame(0, controller);
			MovieBaseTests.SetSingleActiveInput(movie, "A");

			// act
			movie.RemoveFrame(0);

			// assert
			Assert.IsFalse(movie.GetInputState(0).IsPressed("A"));
			Assert.IsTrue(movie.GetInputState(0).IsPressed("B"));
			Assert.AreEqual(2, movie.InputLogLength); // with multi-track editing, the movie length is not expected to change
		}

		[TestMethod]
		public void MultitrackRemoveFrames()
		{
			// arrange
			ITasMovie movie = MakeMovie(2);
			IMovieController controller = movie.Session.MovieController;
			controller.SetBool("A", true);
			controller.SetBool("B", true);
			movie.PokeFrame(0, controller);
			MovieBaseTests.SetSingleActiveInput(movie, "A");

			// act
			movie.RemoveFrames(0, 1);

			// assert
			Assert.IsFalse(movie.GetInputState(0).IsPressed("A"));
			Assert.IsTrue(movie.GetInputState(0).IsPressed("B"));
			Assert.AreEqual(2, movie.InputLogLength); // with multi-track editing, the movie length is not expected to change
		}

		[TestMethod]
		public void MultitrackInsertInput()
		{
			// arrange
			ITasMovie movie = MakeMovie(2);
			MovieBaseTests.SetSingleActiveInput(movie, "A");
			IMovieController controller = movie.Session.MovieController;
			controller.SetBool("A", true);
			controller.SetBool("B", true);

			// act
			movie.InsertInput(0, Enumerable.Repeat(controller, 1));

			// assert
			Assert.IsTrue(movie.GetInputState(0).IsPressed("A"));
			Assert.IsFalse(movie.GetInputState(0).IsPressed("B"));
			Assert.AreEqual(3, movie.InputLogLength);
		}

		[TestMethod]
		public void MultitrackInsertInputsByString()
		{
			// arrange
			ITasMovie movie = MakeMovie(2);
			MovieBaseTests.SetSingleActiveInput(movie, "A");
			IMovieController controller = movie.Session.MovieController;
			controller.SetBool("A", true);
			controller.SetBool("B", true);

			// act
			movie.InsertInput(0, Enumerable.Repeat(LogEntryGenerator.GenerateLogEntry(controller), 1));

			// assert
			Assert.IsTrue(movie.GetInputState(0).IsPressed("A"));
			Assert.IsFalse(movie.GetInputState(0).IsPressed("B"));
			Assert.AreEqual(3, movie.InputLogLength);
		}

		[TestMethod]
		public void MultitrackInsertInputByString()
		{
			// arrange
			ITasMovie movie = MakeMovie(2);
			MovieBaseTests.SetSingleActiveInput(movie, "A");
			IMovieController controller = movie.Session.MovieController;
			controller.SetBool("A", true);
			controller.SetBool("B", true);

			// act
			movie.InsertInput(0, LogEntryGenerator.GenerateLogEntry(controller));

			// assert
			Assert.IsTrue(movie.GetInputState(0).IsPressed("A"));
			Assert.IsFalse(movie.GetInputState(0).IsPressed("B"));
			Assert.AreEqual(3, movie.InputLogLength);
		}

		[TestMethod]
		public void MultitrackInsertFrame()
		{
			// arrange
			ITasMovie movie = MakeMovie(2);
			IMovieController controller = movie.Session.MovieController;
			controller.SetBool("A", true);
			controller.SetBool("B", true);
			movie.PokeFrame(0, controller);
			MovieBaseTests.SetSingleActiveInput(movie, "A");

			// act
			movie.InsertEmptyFrame(0);

			// assert
			Assert.IsFalse(movie.GetInputState(0).IsPressed("A"));
			Assert.IsTrue(movie.GetInputState(0).IsPressed("B"));
			Assert.AreEqual(3, movie.InputLogLength);
		}

		[TestMethod]
		public void MultitrackCopyOverInput()
		{
			// arrange
			ITasMovie movie = MakeMovie(2);
			MovieBaseTests.SetSingleActiveInput(movie, "A");
			IMovieController controller = movie.Session.MovieController;
			controller.SetBool("A", true);
			controller.SetBool("B", true);

			// act
			movie.CopyOverInput(0, Enumerable.Repeat(controller, 1));

			// assert
			Assert.IsTrue(movie.GetInputState(0).IsPressed("A"));
			Assert.IsFalse(movie.GetInputState(0).IsPressed("B"));
		}

		/// <summary>
		/// Loading a branch replaced the marker list with a copy, and the movie listened only to the list it was made
		/// with: a marker added after the load did not mark the project unsaved, and closing threw it away unasked.
		/// </summary>
		[TestMethod]
		public void AMarkerAddedAfterABranchLoadMarksTheProjectChanged()
		{
			TasMovie movie = MakeMovie(10);
			movie.BindMarkersToInput = true;
			movie.Markers.Add(3, "three");
			TasBranch branch = new()
			{
				Frame = 5,
				InputLog = movie.GetLogEntries().Clone(),
				ChangeLog = movie.ChangeLog.Clone(),
				Markers = movie.Markers.DeepClone(),
			};
			var listened = movie.Markers;

			movie.LoadBranch(branch);
			movie.ClearChanges();
			movie.Markers.Add(7, "seven");

			Assert.AreSame(listened, movie.Markers, "the branch's markers are loaded into the list the movie listens to");
			Assert.IsTrue(movie.Changes, "a marker added after the load is an unsaved change");
			Assert.IsTrue(movie.Markers.IsMarker(3), "and the branch's own markers are there");
		}

		/// <summary>
		/// Merging two undo steps removed the NEWEST step rather than the second of the two. The piano roll only ever
		/// merges the newest, which hid it; a third step after the two shows it.
		/// </summary>
		[TestMethod]
		public void MergingTwoUndoStepsKeepsTheOnesAfterThem()
		{
			TasMovie movie = MakeMovie(10);
			movie.ChangeLog.Clear();
			int Step(string name, int frame)
			{
				movie.ChangeLog.BeginNewBatch(name);
				movie.SetBoolState(frame, "A", true);
				movie.ChangeLog.EndBatch();
				return movie.ChangeLog.MostRecentId;
			}
			var one = Step("one", 1);
			var two = Step("two", 2);
			Step("three", 3);

			Assert.IsTrue(movie.ChangeLog.MergeActions(one, two));

			Assert.AreEqual("one", movie.ChangeLog.GetActionName(0));
			Assert.AreEqual("three", movie.ChangeLog.GetActionName(1), "the step after the merged two is still there");
		}

		/// <summary>
		/// A cache lump of "null" (a damaged or hand-edited cache) left the lag log null: the next save wrote "null"
		/// back and every later lag mark failed. A bad cache is a clean slate instead.
		/// </summary>
		[TestMethod]
		public void ADamagedLagLogLoadsAsAnEmptyOne()
		{
			foreach (var lump in new[] { "null\nnull\n", "", "{not json\n" })
			{
				TasLagLog log = new();
				log.Load(new System.IO.StringReader(lump));
				log[4] = true;
				Assert.AreEqual(true, log[4], $"a lag mark after loading {lump.Replace("\n", "\\n")}");
			}
		}
	}
}
