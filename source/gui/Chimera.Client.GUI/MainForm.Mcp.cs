#nullable enable

using System;

using Chimera.Client.Common;
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

		/// <summary>A seek an agent asked for that is still on its way: the call to answer when it arrives, and where to.</summary>
		private (ulong Id, int Frame)? _mcpSeek;

		/// <summary>TAStudio as the MCP server's tools see it (<see cref="IMcpWindow"/>).</summary>
		private sealed class McpWindow : IMcpWindow
		{
			private readonly MainForm _main;

			public McpWindow(MainForm main) => _main = main;

			public int Frame => _main.Emulator.Frame;

			public void SeekTo(int frame) => _main.Tools.TAStudio.GoToFrame(frame);

			public int SaveBranch(string note) => _main.Tools.TAStudio.AddBranchWithNote(note);

			public void LoadBranch(int number) => _main.Tools.TAStudio.LoadBranchByIndex(number - 1);
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
				_mcpForm = new McpServerForm();
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
				// --mcp-allow-changes: the tick box, for a run nobody clicks in
				if (_argParser.mcpAllowChanges) EngineMcp.ChangesAllowed = true;
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
			if (_mcpSeek is { } seek)
			{
				// A seek runs over many passes of this loop. Its call is answered when the playback
				// position has stopped, and nothing else of the agent's is run before that.
				if (tastudio && Tools.TAStudio.SeekInProgress) return;
				var arrived = window is null
					? "The project was closed while the playback position was moving."
					: McpHostTools.SeekAnswer(window, seek.Frame, out _);
				EngineMcp.Answer(seek.Id, arrived, window is null || window.Frame != seek.Frame);
				_mcpSeek = null;
			}
			if (!EngineMcp.Pending) return;
			var session = Emulator is WaterboxCore core ? core.SessionHandle : IntPtr.Zero;
			var movie = MovieSession.Movie;
			var active = movie.IsActive();
			var log = active && movie.GetLogEntries() is EngineStringLog entries ? entries.Engine.Handle : IntPtr.Zero;
			var frame = Emulator.IsNull() ? -1 : Emulator.Frame;
			while (EngineMcp.Service(session, log, frame, McpHostStatus(), out var id, out var tool, out var arguments))
			{
				string? text;
				bool isError;
				try
				{
					text = McpHostTools.Run(active ? movie as ITasMovie : null, window, tool, arguments, out isError);
				}
				catch (Exception ex)
				{
					// the agent is told, and the session goes on
					text = $"Chimera could not run '{tool}': {ex.Message}";
					isError = true;
				}
				if (text is null)
				{
					// a seek has begun: answered above, on the pass where it has arrived
					_mcpSeek = (id, (int) (JObject.Parse(arguments)["frame"] ?? 0));
					return;
				}
				EngineMcp.Answer(id, text, isError);
				// what the agent changed is shown at once, as a change made by hand is
				if (tastudio && !isError && !tool.StartsWith("list_", StringComparison.Ordinal)) Tools.TAStudio.RefreshDialog();
				frame = Emulator.IsNull() ? -1 : Emulator.Frame;
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
			}
			return status.ToString(Formatting.None);
		}
	}
}
