#nullable enable

using System;
using System.Collections.Generic;
using System.Linq;

using Chimera.Client.Common;
using Chimera.Common;
using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Engine;
using Chimera.Emulation.Common.Waterbox;

using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace Chimera.Client.GUI
{
	public partial class MainForm
	{
		private McpServerForm? _mcpForm;

		/// <summary>
		/// A seek or an advance_until an agent asked for that is still on its way: the call to answer when
		/// the playback position stops, where it began, where it is to stop at the latest, and whether it
		/// also stops when the engine's condition is met.
		/// </summary>
		private (ulong Id, int Start, int Frame, bool Until)? _mcpSeek;

		/// <summary>What the window was last asked to move to, by <see cref="McpWindow"/>.</summary>
		private (int Start, int Frame, bool Until) _mcpAsked;

		/// <summary>
		/// The frames a capture_series still has to take pictures of, the next first; null when none is
		/// running. Each is reached by a seek of its own (<see cref="_mcpSeek"/>), and the picture is taken
		/// when the seek has stopped there, with the frame drawn.
		/// </summary>
		private Queue<int>? _mcpPictures;

		private IntPtr McpSession => Emulator is WaterboxCore core ? core.SessionHandle : IntPtr.Zero;

		/// <summary>TAStudio as the MCP server's tools see it (<see cref="IMcpWindow"/>).</summary>
		private sealed class McpWindow : IMcpWindow
		{
			private readonly MainForm _main;

			public McpWindow(MainForm main) => _main = main;

			public int Frame => _main.Emulator.Frame;

			public void SeekTo(int frame)
			{
				_main._mcpAsked = (Frame, frame, false);
				_main.Tools.TAStudio.GoToFrame(frame);
			}

			public bool SetCondition(string argumentsJson, out string why) => EngineMcp.SetCondition(_main.McpSession, argumentsJson, out why);

			public bool ConditionMet => EngineMcp.ConditionMet(_main.McpSession);

			public void PlayUntil(int frame)
			{
				_main._mcpAsked = (Frame, frame, true);
				_main.Tools.TAStudio.GoToFrame(frame);
			}

			public void TakePictures(IReadOnlyList<int> frames)
			{
				_main._mcpPictures = new(frames);
				SeekTo(frames[0]);
			}

			public int SaveBranch(string note) => _main.Tools.TAStudio.AddBranchWithNote(note);

			public void LoadBranch(int number) => _main.Tools.TAStudio.LoadBranchByIndex(number - 1);
		}

		/// <summary>Chimera's main window as the MCP server's tools see it (<see cref="IMcpChimera"/>).</summary>
		private sealed class McpChimera : IMcpChimera
		{
			private readonly MainForm _main;

			public McpChimera(MainForm main) => _main = main;

			public int Frame => _main.Emulator.IsNull() ? -1 : _main.Emulator.Frame;

			public bool Paused => _main.EmulatorPaused;

			public void Pause() => _main.PauseEmulator();

			public void Resume() => _main.UnpauseEmulator();

			public JObject ServerInfo() => _main.McpServerInfo();

			public string ProjectsFolder => _main.Config.PathEntries.MovieAbsolutePath();

			public string? SaveProject(string path)
			{
				if (!_main.Tools.Has<TAStudio>()) return "TAStudio is not open";
				var saved = _main.Tools.TAStudio.SaveProjectAs(path);
				return saved.IsError ? $"{saved.UserFriendlyErrorMessage()} {saved.Exception?.Message}".Trim() : null;
			}

			public string? CloseProject()
			{
				_main.CloseProject();
				return _main.Emulator.IsNull() ? null : "something asked to keep it open";
			}

			public McpProjectMade CreateProject(McpProjectRequest request) => _main.McpCreateProject(request);

			public IReadOnlyList<LibraryFunction>? LuaFunctions() => _main.McpLuaConsole()?.Documentation;

			public (object[] Results, string? Error, string Output) RunLua(string code)
			{
				var console = _main.McpLuaConsole()!;
				var (results, error) = console.RunForAgent(code);
				return (results, error, console.TakeOutputForAgent());
			}

			public (bool Running, string Output) LuaScript(string path, bool stop)
			{
				var console = _main.McpLuaConsole()!;
				if (stop) console.RemoveLuaFile(path);
				else console.StartScriptForAgent(path);
				return (console.ScriptRunning(path), console.TakeOutputForAgent());
			}
		}

		/// <summary>
		/// The Lua Console, opened as a person opens it when it is not open: the agent's Lua runs there, where the
		/// person sees it (issue 355). Null when Lua cannot run in this Chimera.
		/// </summary>
		private LuaConsole? McpLuaConsole()
			=> LuaLibraries.IsAvailable ? Tools.Load<LuaConsole>(focus: false) : null;

		/// <summary>
		/// create_project: the New Project wizard, never shown, given the agent's answers
		/// (<see cref="NewProjectWizard.CompleteForAgent"/>), and the project it makes opened as one made by
		/// hand is. The firmware it found is remembered as the wizard's always is.
		/// </summary>
		private McpProjectMade McpCreateProject(McpProjectRequest request)
		{
			ScanForCorePackages();
			List<string> folders = [ Config.PathEntries.FirmwareAbsolutePath() ];
			if (request.FirmwareFolder is { } extra) folders.Add(extra);
			using var wizard = MakeNewProjectWizard(folders);
			var outcome = wizard.CompleteForAgent(request.Core,
				Config.DefaultCoreBuilds.TryGetValue(request.Core, out var preferred) ? preferred : null,
				request.System, request.Files, request.Games, request.Settings, request.Firmware);
			if (!outcome.Made || wizard.CreatedProject is not { } project)
			{
				return new()
				{
					Refusal = outcome.Refusal,
					Missing = outcome.Missing.Count is 0 ? null : new JArray(outcome.Missing.Select(static m => new JObject
					{
						["id"] = m.Id,
						["name"] = m.Name,
						["accepted"] = new JArray(m.Accepted.Select(static d => new JObject
						{
							["file"] = d.Name,
							["size"] = d.Size,
							["sha1"] = d.Sha1,
						})),
					})),
					Searched = outcome.Missing.Count is 0 ? null : new JArray(outcome.Searched),
				};
			}
			// the project open now gives way, and what it had not saved the agent said may go
			if (request.DiscardUnsaved && MovieSession.Movie is ITasMovie open) open.ClearChanges();
			RememberWizardFirmware(wizard);
			if (!LoadNewProject(project) || Emulator.IsNull())
			{
				return new() { Refusal = "The project was made, but its machine did not start." };
			}
			var opened = JObject.Parse(McpHostStatus());
			opened["core"] = Emulator.Attributes().CoreName;
			opened["system"] = Emulator.SystemId;
			opened["frame"] = Emulator.Frame;
			opened["movie_length"] = MovieSession.Movie.FrameCount;
			return new() { Opened = opened };
		}

		/// <summary>server_info: this build, and the folders it reads and writes.</summary>
		private JObject McpServerInfo()
		{
			JObject engine;
			try
			{
				engine = JObject.Parse(ChimeraEngine.BuildInfo);
			}
			catch (JsonException)
			{
				engine = new() { ["text"] = ChimeraEngine.BuildInfo };
			}
			return new JObject
			{
				["chimera"] = VersionInfo.GetBuildName(),
				["commit"] = VersionInfo.GIT_HASH,
				["engine"] = engine,
				["window"] = true,
				["program_folder"] = AppContext.BaseDirectory,
				["config_file"] = _getConfigPath(),
				["folders"] = new JObject
				{
					["projects"] = Config.PathEntries.MovieAbsolutePath(),
					["cores"] = new JArray(CorePackageDiscovery.SearchPaths(Config)),
					["firmware"] = Config.PathEntries.FirmwareAbsolutePath(),
					["lua"] = Config.PathEntries.LuaAbsolutePath(),
				},
				["mcp_name"] = EngineMcp.Endpoint,
			};
		}

		/// <summary>
		/// Tools &gt; MCP Server... (docs/mcp-server.md): the window where the server that lets an
		/// AI agent work on the open project is started, watched and stopped. Chimera never
		/// starts it otherwise, except when it is launched with --mcp-server, and nothing
		/// remembers that it was started: every run begins with it stopped.
		/// </summary>
		private void McpServerMenuItem_Click(object sender, EventArgs e)
		{
			if (_mcpForm is null or { IsDisposed: true })
			{
				_mcpForm = new McpServerForm(Config);
				_mcpForm.ServerToggled += UpdateWindowTitle;
				_mcpForm.Show(this);
			}
			else
			{
				_mcpForm.Activate();
			}
		}

		/// <summary>--mcp-server: the same as pressing Start in Tools &gt; MCP Server..., for a run nobody clicks in.</summary>
		private void StartMcpServerFromCommandLine()
		{
			if (EngineMcp.Start(out var error))
			{
				// the tick box as it was left, or --mcp-allow-changes, which ticks it for a run nobody clicks in
				EngineMcp.ChangesAllowed = Config.McpAllowChanges || _argParser.mcpAllowChanges;
				Console.WriteLine($"[mcp] the server is started at {EngineMcp.Endpoint}");
				UpdateWindowTitle();
			}
			else
			{
				Console.Error.WriteLine($"[mcp] --mcp-server: the server could not be started: {error}");
			}
		}

		/// <summary>
		/// Once per pass of the main loop: when an agent's call waits, the engine runs it here, on the
		/// thread that runs the machine and between two frames. The engine answers what needs only the
		/// machine; a call that needs the movie as this window holds it is handed back and answered
		/// by <see cref="McpHostTools"/>.
		/// </summary>
		private void ServiceMcp()
		{
			var tastudio = !Emulator.IsNull() && Tools.Has<TAStudio>();
			IMcpWindow? window = tastudio ? new McpWindow(this) : null;
			var session = McpSession;
			var movie = MovieSession.Movie;
			var active = movie.IsActive();
			if (_mcpSeek is { } seek)
			{
				// A seek runs over many passes of this loop, one frame a pass. Its call is answered when
				// the playback position has stopped, and nothing else of the agent's is run before that.
				// advance_until stops it early: the engine is asked after every frame whether the
				// condition is met.
				if (tastudio && Tools.TAStudio.SeekInProgress)
				{
					if (!seek.Until || !EngineMcp.ConditionMet(session)) return;
					Tools.TAStudio.StopSeeking();
					PauseEmulator();
				}
				if (window is null || !active || movie is not ITasMovie tasMovie)
				{
					EngineMcp.Answer(session, seek.Id, "The project was closed while the playback position was moving.", isError: true);
				}
				else if (_mcpPictures is { } pictures)
				{
					// on a frame of a capture_series: its picture, then on to the next
					string? failed = null;
					if (window.Frame != pictures.Peek())
					{
						failed = $"The playback position stopped on frame {window.Frame}, not on frame {pictures.Peek()}: the pictures were interrupted.";
					}
					else if (!EngineMcp.Capture(session, seek.Id, window.Frame, out var why))
					{
						failed = $"The picture of frame {window.Frame} was not taken: {why}";
					}
					else
					{
						pictures.Dequeue();
						if (pictures.Count is not 0)
						{
							window.SeekTo(pictures.Peek());
							_mcpSeek = (seek.Id, _mcpAsked.Start, _mcpAsked.Frame, false);
							return;
						}
					}
					EngineMcp.Answer(session, seek.Id, failed ?? McpHostTools.PicturesAnswer(window, tasMovie), isError: failed is not null);
				}
				else if (seek.Until)
				{
					EngineMcp.Answer(session, seek.Id, McpHostTools.UntilAnswer(window, tasMovie, seek.Start, seek.Frame), isError: false);
				}
				else
				{
					var arrived = McpHostTools.SeekAnswer(window, seek.Frame, out var interrupted);
					EngineMcp.Answer(session, seek.Id, arrived, interrupted);
				}
				_mcpSeek = null;
				_mcpPictures = null;
			}
			if (!EngineMcp.Pending) return;
			McpChimera chimera = new(this);
			for (;;)
			{
				// asked again for every call: one that closed the project leaves no machine for the next
				tastudio = !Emulator.IsNull() && Tools.Has<TAStudio>();
				window = tastudio ? new McpWindow(this) : null;
				session = McpSession;
				movie = MovieSession.Movie;
				active = movie.IsActive();
				var log = active && movie.GetLogEntries() is EngineStringLog entries ? entries.Engine.Handle : IntPtr.Zero;
				var frame = Emulator.IsNull() ? -1 : Emulator.Frame;
				EngineMcp.SetCoresFolders(CorePackageDiscovery.SearchPaths(Config));
				if (!EngineMcp.Service(session, log, frame, McpHostStatus(), out var id, out var tool, out var arguments)) break;
				string? text;
				bool isError;
				try
				{
					text = McpHostTools.Run(active ? movie as ITasMovie : null, window, tool, arguments, out isError, chimera);
				}
				catch (Exception ex)
				{
					// the agent is told, and the session goes on
					text = $"Chimera could not run '{tool}': {ex.Message}";
					isError = true;
				}
				if (text is null)
				{
					// a seek or an advance_until has begun: answered above, on the pass where it stops
					_mcpSeek = (id, _mcpAsked.Start, _mcpAsked.Frame, _mcpAsked.Until);
					return;
				}
				// the machine as it is now: a closed project's session is gone
				EngineMcp.Answer(McpSession, id, text, isError);
				// what the agent changed is shown at once, as a change made by hand is
				if (!Emulator.IsNull() && Tools.Has<TAStudio>() && !isError && !tool.StartsWith("list_", StringComparison.Ordinal)) Tools.TAStudio.RefreshDialog();
			}
		}

		/// <summary>What only this window knows, for the status tool: the engine adds the machine's facts.</summary>
		private string McpHostStatus()
		{
			JObject status = new();
			if (!Emulator.IsNull())
			{
				status["project"] = Game.Name;
				var movie = MovieSession.Movie;
				if (movie.IsActive())
				{
					status["project_file"] = movie.Filename ?? "";
					status["unsaved_changes"] = movie.Changes;
				}
				status["paused"] = EmulatorPaused;
				// how fast it is running, measured, beside the machine's own rate the engine adds
				status["measured_frames_per_second"] = Math.Round(_lastFps, 2);
				status["speed_percent"] = Config.SpeedPercent;
				if (_openProject is { } project)
				{
					JArray files = new();
					for (var i = 0; i < project.FileCount; i++)
					{
						files.Add(new JObject { ["slot"] = project.FileSlot(i), ["name"] = project.FileName(i), ["path"] = project.FileSourcePath(i) });
					}
					status["files"] = files;
				}
				// the firmware files the machine was started with: where each was found shows a file from an
				// old folder, or one chosen long ago, for what it is
				if (Emulator is WaterboxCore)
				{
					JArray firmware = new();
					foreach (var entry in CoreFirmwareStore.InUse(Config, CoreRegistry.Instance, Emulator.Attributes().CoreName, CoreRegistry.Instance.PackageSha1Of(Emulator)))
					{
						if (entry.Path is null) continue;
						firmware.Add(new JObject
						{
							["id"] = entry.Decl.Id,
							["name"] = entry.Decl.DisplayName,
							["file"] = entry.Path,
							["sha1"] = entry.Sha1,
							["source"] = entry.SourceText,
							["state"] = entry.StatusText,
						});
					}
					status["firmware"] = firmware;
				}
			}
			return status.ToString(Formatting.None);
		}
	}
}
