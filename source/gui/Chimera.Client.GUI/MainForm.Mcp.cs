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
			if (!EngineMcp.Pending) return;
			var session = Emulator is WaterboxCore core ? core.SessionHandle : IntPtr.Zero;
			var movie = MovieSession.Movie;
			var active = movie.IsActive();
			var log = active && movie.GetLogEntries() is EngineStringLog entries ? entries.Engine.Handle : IntPtr.Zero;
			var frame = Emulator.IsNull() ? -1 : Emulator.Frame;
			while (EngineMcp.Service(session, log, frame, McpHostStatus(), out var id, out var tool, out var arguments))
			{
				string text;
				bool isError;
				try
				{
					text = McpHostTools.Run(active ? movie as ITasMovie : null, tool, arguments, out isError);
				}
				catch (Exception ex)
				{
					// the agent is told, and the session goes on
					text = $"Chimera could not run '{tool}': {ex.Message}";
					isError = true;
				}
				EngineMcp.Answer(id, text, isError);
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
