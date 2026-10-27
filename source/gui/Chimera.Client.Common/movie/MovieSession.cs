using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

using Chimera.Common.PathExtensions;
using Chimera.Common.StringExtensions;
using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Engine;

namespace Chimera.Client.Common
{
	public class MovieSession : IMovieSession
	{
		private readonly IDialogParent _dialogParent;

		private readonly Action _modeChangedCallback;
		private readonly EventHandler _changesChangedCallback;

		private IMovie _queuedMovie;

		public MovieSession(
			IMovieConfig settings,
			string backDirectory,
			IDialogParent dialogParent,
			Action modeChangedCallback,
			EventHandler changesChangedCallback)
		{
			Settings = settings;
			BackupDirectory = backDirectory;
			_dialogParent = dialogParent;
			_modeChangedCallback = modeChangedCallback
				?? throw new ArgumentNullException(paramName: nameof(modeChangedCallback));
			_changesChangedCallback = changesChangedCallback;
		}

		public IMovieConfig Settings { get; }

		public string BackupDirectory { get; set; }

		public IMovie Movie { get; private set; }
		public bool ReadOnly { get; set; } = true;

		/// <summary>
		/// While this is set, playing a frame leaves the movie exactly as it was:
		/// no greenzone state is kept and no lag is written down.
		///
		/// Encoding a video drives the run this way. It reproduces a movie rather
		/// than authoring one, and a reproduction that rewrote the greenzone would
		/// spend a run's worth of memory on states nobody asked for and leave the
		/// project changed by the act of looking at it.
		/// </summary>
		public bool SuppressStateCapture { get; set; }
		public bool NewMovieQueued => _queuedMovie != null;
		public string QueuedSettings => _queuedMovie.SettingsJson;

		public string QueuedCoreName => _queuedMovie?.Core;

		public string/*?*/ QueuedSysID
			=> _queuedMovie?.SystemID;

		public IDictionary<string, object> UserBag { get; set; } = new Dictionary<string, object>();

		public IController MovieIn { get; set; }
		public IInputAdapter MovieOut { get; } = new CopyControllerAdapter();
		public IController StickySource { get; set; }

		public IMovieController MovieController { get; private set; } = new MovieController(NullController.Instance.Definition);

		public IMovieController GenerateMovieController(ControllerDefinition definition = null, string logKey = null)
		{
			// TODO: should this fallback to Movie.LogKey?
			// this function is kinda weird
			return new MovieController(definition ?? MovieController.Definition, logKey);
		}

		public void HandleFrameBefore()
		{
			// the epoch has to be marked before the machine moves, or the frame
			// about to be captured costs a whole state instead of a delta
			if (Movie is ITasMovie beforeFrame && !SuppressStateCapture) beforeFrame.GreenzoneBeforeFrame();

			if (Movie.NotActive())
			{
				LatchInputToUser();
			}
			else if (Movie.IsFinished())
			{
				if (Movie.Emulator.Frame < Movie.FrameCount) // This scenario can happen from rewinding (suddenly we are back in the movie, so hook back up to the movie
				{
					Movie.SwitchToPlay();
					LatchInputToLog();
				}
				else
				{
					LatchInputToUser();
				}
			}
			else if (Movie.IsPlaying())
			{
				LatchInputToLog();
			}
			else if (Movie.IsRecording())
			{
				LatchInputToUser();
				Movie.RecordFrame(Movie.Emulator.Frame, MovieOut.Source);
			}
		}

		public void HandleFrameAfter()
		{
			if (Movie is ITasMovie tasMovie && !SuppressStateCapture)
			{
				tasMovie.GreenzoneCurrentFrame();
			}
		}

		public void HandleSaveState(TextWriter writer)
		{
			if (Movie.IsActive())
			{
				Movie.WriteInputLog(writer);
			}
		}

		/// <exception cref="MoviePlatformMismatchException"><paramref name="movie"/>.<see cref="IBasicMovieInfo.SystemID"/> does not match <paramref name="systemId"/>.<see cref="IEmulator.SystemId"/></exception>
		public void QueueNewMovie(
			IMovie movie,
			string systemId,
			string loadedRomHash,
			PathEntryCollection pathEntries)
		{
			if (movie.SystemID != systemId)
			{
				throw new MoviePlatformMismatchException(
					$"Movie system Id ({movie.SystemID}) does not match the currently loaded platform ({systemId}), unable to load");
			}

			if (!(string.IsNullOrEmpty(movie.Hash) || loadedRomHash.Equals(movie.Hash, StringComparison.Ordinal))
				&& movie is TasMovie tasproj)
			{
				var result = _dialogParent.ModalMessageBox2(
					caption: "Discard GreenZone?",
					text: $"The project {movie.Filename.MakeRelativeTo(pathEntries.MovieAbsolutePath())} appears to be for a different game than the one that's loaded.\n"
						+ "Choose \"No\" to continue anyway, which may lead to an invalid savestate being loaded.\n"
						+ "Choose \"Yes\" to discard the GreenZone (savestate history). This is safer, and at worst you'll only need to watch through the whole movie.");
				//TODO add abort option
				if (result)
				{
					tasproj.TasSession.UpdateValues(frame: 0, currentBranch: tasproj.TasSession.CurrentBranch); // wtf is this API --yoshi
					tasproj.InvalidateEntireGreenzone();
				}
			}

			// the movie's core is the one its machine opens with (QueuedCoreName)
			if (string.IsNullOrWhiteSpace(movie.Core))
			{
				PopupMessage("No core is named in the movie file: the core loaded for its system is used.");
			}

			_queuedMovie = movie;
		}

		public void RunQueuedMovie(bool recordMode, IEmulator emulator)
		{
			// Nothing queued means something ran between the queue and here and took
			// it - a close, or another movie starting - and the caller is about to
			// believe it has a session it does not. Say which, rather than throwing a
			// null reference from the middle of the next line.
			if (_queuedMovie is null)
			{
				throw new InvalidOperationException(
					"no movie is queued: something ended or replaced it between queueing and running."
						+ " A project must be closed before the next one is booted.");
			}

			MovieController = new MovieController(emulator.ControllerDefinition, _queuedMovie.LogKey);

			Movie = _queuedMovie;
			Movie.Attach(emulator);
			_queuedMovie = null;

			Movie.ProcessSavestate(Movie.Emulator);

			if (recordMode)
			{
				Movie.StartNewRecording();
				ReadOnly = false;
				// If we are starting a movie recording while another one is playing, we need to switch back to user input
				LatchInputToUser();
			}
			else
			{
				Movie.StartNewPlayback();
			}
		}

		public void AbortQueuedMovie()
			=> _queuedMovie = null;

		public FileWriteResult StopMovie(bool saveChanges = true)
		{
			FileWriteResult/*?*/ result = null;
			if (Movie.IsActive())
			{
				var message = "Movie ";
				if (Movie.IsRecording())
				{
					message += "recording ";
				}
				else if (Movie.IsPlayingOrFinished())
				{
					message += "playback ";
				}

				message += "stopped.";

				if (saveChanges && Movie.Changes)
				{
					result = Movie.Save();
					if (result.IsError)
					{
						Output($"Failed to write {Path.GetFileName(Movie.Filename)} to disk.");
						Output(result.UserFriendlyErrorMessage());
						return result;
					}
					else
					{
						Output($"{Path.GetFileName(Movie.Filename)} written to disk.");
					}
				}
				Movie.Stop();

				Output(message);
				ReadOnly = true;

				_modeChangedCallback();
			}

			if (Movie is IDisposable d)
			{
				d.Dispose();
			}

			Movie = null;

			return result ?? new();
		}

		public IMovie Get(string path, bool loadMovie, EngineProject project = null)
		{
			// there is exactly one movie-bearing artifact: the project (docs/project.md).
			// Case-insensitively: a filesystem that preserves case still hands
			// back whatever case the file arrived with, and a downloaded
			// project is as likely to be .chimeraproject as .chimeraProject.
			// This was the ONE case-sensitive check on the open-project path,
			// and it made the whole flow end in silence (issue #21).
			if (Path.GetExtension(path)?.EndsWith(TasMovie.Extension, StringComparison.OrdinalIgnoreCase) is not true) return null;
			IMovie movie = new TasMovie(this, path, project);

			if (loadMovie)
			{
				bool loaded = movie.Load();
				if (!loaded) return null;
			}

			movie.ChangesChanged += _changesChangedCallback;

			return movie;
		}

		public void PopupMessage(string message) => _dialogParent.ModalMessageBox(message, "Warning", EMsgBoxIcon.Warning);

		private void Output(string message)
			=> _dialogParent.AddOnScreenMessage(message);

		private void LatchInputToUser()
		{
			MovieOut.Source = MovieIn;
		}

		// Latch input from the input log, if available
		private void LatchInputToLog()
		{
			var input = Movie.GetInputState(Movie.Emulator.Frame);

			MovieController.SetFrom(input ?? StickySource);
			MovieOut.Source = MovieController;
		}
	}
}
