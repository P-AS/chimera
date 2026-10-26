using System.Collections.Generic;
using System.IO;
using System.IO.Compression;
using System.Linq;

using Chimera.Common;
using Chimera.Common.IOExtensions;
using Chimera.Common.StringExtensions;
using Chimera.Emulation.Common;

namespace Chimera.Client.Common
{
	public class RomLoader
	{
		private class RomAsset : IRomAsset
		{
			public byte[] RomData { get; set; }
			public byte[] FileData { get; set; }
			public string Extension { get; set; }
			public string RomPath { get; set; }
			public GameInfo Game { get; set; }
		}
		private readonly Config _config;

		private readonly IDialogParent _dialogParent;

		public RomLoader(Config config, IDialogParent dialogParent)
		{
			_config = config;
			_dialogParent = dialogParent;
		}

		private bool? Question(string text)
			=> _dialogParent.ModalMessageBox3(icon: EMsgBoxIcon.Question, caption: "ROM loader", text: text);

		public enum LoadErrorType
		{
			Unknown,
			Xml,
		}

		// helper methods for the settings events
		private TSetting GetCoreSettings<TCore, TSetting>()
			where TCore : IEmulator
		{
			return (TSetting)GetCoreSettings(typeof(TCore), typeof(TSetting));
		}

		private object GetCoreSettings(Type t, Type settingsType)
		{
			var e = new SettingsLoadArgs(t, settingsType);
			if (OnLoadSettings == null)
				throw new InvalidOperationException("Frontend failed to provide a settings getter");
			OnLoadSettings(this, e);
			if (e.Settings != null && e.Settings.GetType() != settingsType)
				throw new InvalidOperationException($"Frontend did not provide the requested settings type: Expected {settingsType}, got {e.Settings.GetType()}");
			return e.Settings;
		}

		// TODO: reconsider the need for exposing these;
		public IEmulator LoadedEmulator { get; private set; }
		public GameInfo Game { get; private set; }
		public string CanonicalFullPath { get; private set; }
		public bool Deterministic { get; set; }

		public class RomErrorArgs : EventArgs
		{
			// TODO: think about naming here, what to pass, a lot of potential good information about what went wrong could go here!
			public RomErrorArgs(string message, string systemId, LoadErrorType type)
			{
				Message = message;
				AttemptedCoreLoad = systemId;
				Type = type;
			}

			public RomErrorArgs(string message, string systemId, string path, bool? det, LoadErrorType type)
				: this(message, systemId, type)
			{
				Deterministic = det;
				RomPath = path;
			}

			public string Message { get; }
			public string AttemptedCoreLoad { get; }
			public string RomPath { get; }
			public bool? Deterministic { get; set; }
			public bool Retry { get; set; }
			public LoadErrorType Type { get; }
		}

		public class SettingsLoadArgs : EventArgs
		{
			public object Settings { get; set; }
			public Type Core { get; }
			public Type SettingsType { get; }
			public SettingsLoadArgs(Type t, Type s)
			{
				Core = t;
				SettingsType = s;
				Settings = null;
			}
		}

		public delegate void SettingsLoadEventHandler(object sender, SettingsLoadArgs e);
		public event SettingsLoadEventHandler OnLoadSettings;

		public delegate void LoadErrorEventHandler(object sender, RomErrorArgs e);
		public event LoadErrorEventHandler OnLoadError;

		// May want to phase out this method in favor of the overload with more parameters
		private void DoLoadErrorCallback(string message, string systemId, LoadErrorType type = LoadErrorType.Unknown)
		{
			OnLoadError?.Invoke(this, new RomErrorArgs(message, systemId, type));
		}

		private void DoLoadErrorCallback(string message, string systemId, string path, bool det, LoadErrorType type = LoadErrorType.Unknown)
		{
			OnLoadError?.Invoke(this, new RomErrorArgs(message, systemId, path, det, type));
		}

		/// <summary>
		/// The resolved project when the caller is opening a .chimeraProject: every
		/// file already located and hash-checked (or knowingly overridden) by the
		/// resolution dialog. The loader only builds the mounts from it.
		/// </summary>
		public Emulation.Common.Engine.EngineProject Project { get; set; }

		public bool LoadRom(string path, CoreComm nextComm)
		{
			if (path == null) return false;

			// a game runs only in a project (user-decided, 2026-10-26): there is
			// no other way to a machine
			if (Project is null || !path.EndsWith(".chimeraProject", StringComparison.OrdinalIgnoreCase))
			{
				DoLoadErrorCallback($"{Path.GetFileName(path)}: a game opens only in a project.", null);
				return false;
			}
			try
			{
				LoadProjectGame(path, nextComm, out var projectEmulator, out var projectGame);
				if (projectEmulator is null) return false;
				CanonicalFullPath = Path.GetFullPath(path);
				LoadedEmulator = projectEmulator;
				Game = projectGame;
				return true;
			}
			catch (Exception ex)
			{
				DispatchErrorMessage(ex, system: null, path: path);
				return false;
			}
		}

		/// <summary>
		/// A project's machine: the pinned core and the project's own sync settings
		/// (queued by the movie the project IS). Which files the core sees is the
		/// engine's to decide, from the project itself (engine.h,
		/// ce_session_open_project) - the one place that knows, shared with
		/// chimera-run (docs/project.md).
		/// </summary>
		private void LoadProjectGame(string path, CoreComm nextComm, out IEmulator nextEmulator, out GameInfo game)
		{
			var p = Project;
			var primary = p.GameFile;
			// A project MAY hold no game at all. Two of the ares core's machines
			// are worth starting that way - a PlayStation reaches its own BIOS
			// menu, an MSX its own BASIC - and a core that boots without a
			// medium says so by declaring its rom slot with no minimum. The
			// engine mounts the slot either way; what the core sees is a file of
			// zero length, which is how it tells an empty drive from a full one.
			var haveGame = primary >= 0;
			// A project's files are mounted from WHERE THEY LIE, by the engine.
			// Nothing here reads one: a PS2 disc is over four gigabytes and a
			// byte[] cannot reach two. The game's path is kept for what is named
			// after the game (its compile cache), not to open it.
			var primaryPath = haveGame ? p.FileSourcePath(primary) : "";

			var factory = CoreRegistry.Instance.FactoryFor(p.CoreName, p.CoreSha1)
				?? throw new CoreLoadException($"the project's core '{p.CoreName}' is not loaded");

			game = new GameInfo
			{
				// what to call this: the project's own title if it has one, else
				// the game file's name - which beats the project file's, because
				// an unwritten project's stand-in path says nothing about the game
				Name = p.Title.Length is not 0
					? p.Title
					: (haveGame && Path.GetFileNameWithoutExtension(p.FileName(primary)) is { Length: not 0 } fileName
						? fileName
						: Path.GetFileNameWithoutExtension(path)),
				Hash = haveGame ? p.FileActualSha1(primary) : "",
				System = factory.SystemIds[0],
			};

			var ctx = new CoreCreationContext
			{
				Comm = nextComm,
				Game = game,
				Roms = new List<IRomAsset>
				{
					new RomAsset
					{
						// no bytes: the file is mounted from RomPath, which is
						// where the project resolved it to, not the project's own
						// path (a project is not a game file)
						RomData = null,
						FileData = null,
						Extension = haveGame ? Path.GetExtension(p.FileName(primary)) : "",
						RomPath = primaryPath,
						Game = game,
					},
				},
				DeterministicEmulationRequested = Deterministic,
				// everything sync-relevant lives in the project: its recorded
				// settings drive the boot directly, never a config lookup
				Settings = MovieSettings.Decode(p.SettingsJson)
					?? GetCoreSettings(factory.CoreType, factory.SettingsType),
				FirmwareProvider = CoreFirmwareStore.ProviderFor(_config, factory.CoreName),
				Project = p,
			};
			nextEmulator = factory.Create(ctx);
			CoreRegistry.Instance.NoteCreated(nextEmulator, factory);
			// The game record was made before the core existed, from the package's
			// FIRST system - which for a core with many machines is not the one the
			// project boots: an ares NES project said N64 to anything reading the
			// game's system (scripts, tools, the movie's platform). The core knows
			// which machine it became, so that is what the record says.
			if (!string.IsNullOrEmpty(nextEmulator.SystemId)) game.System = nextEmulator.SystemId;
		}

		private void DispatchErrorMessage(Exception ex, string system, string path)
		{
			if (ex is AggregateException agg)
			{
				// all cores failed to load a game, so tell the user everything that went wrong and maybe they can fix it
				if (agg.InnerExceptions.Count > 1)
				{
					DoLoadErrorCallback("Multiple cores failed to load the rom:", system);
				}
				foreach (Exception e in agg.InnerExceptions)
				{
					DispatchErrorMessage(e, system: system, path: path);
				}

				return;
			}

			// all of the specific exceptions we're trying to catch here aren't expected to have inner exceptions,
			// so drill down in case we got a TargetInvocationException or something like that
			while (ex.InnerException != null)
				ex = ex.InnerException;

			if (ex is CoreLoadException)
			{
				// the user can fix this one, and what to do is in the message
				DoLoadErrorCallback(ex.Message, system);
			}
			else if (ex is NoAvailableCoreException)
			{
				// handle exceptions thrown by the new detected systems that BizHawk does not have cores for
				DoLoadErrorCallback($"{ex.Message}\n\n{ex}", system);
			}
			else
			{
				DoLoadErrorCallback($"A core accepted the rom, but threw an exception while loading it:\n\n{ex}", system);
			}
		}
		/// <remarks>rom extensions (with leading dot, UPPERCASE) declared by loaded core packages; this is what's expected at call-site</remarks>
		public static IReadOnlyCollection<string> KnownRomExtensions
			=> CoreRegistry.Instance.KnownRomExtensions.Select(static e => e.ToUpperInvariant()).ToList();

		/// <remarks>built from the extensions declared by loaded core packages</remarks>
		public static FilesystemFilterSet RomFilter
			=> new(
				combinedEntryDesc: "Everything",
				FilesystemFilter.Archives,
				new FilesystemFilter("ROMs", CoreRegistry.Instance.KnownRomExtensions.Select(static e => e.TrimStart('.')).ToList(), addArchiveExts: true));
	}
}
