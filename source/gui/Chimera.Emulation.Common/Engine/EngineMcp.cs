#nullable enable

using System;

namespace Chimera.Emulation.Common.Engine
{
	/// <summary>
	/// The MCP server (docs/mcp-server.md): the way an AI agent's program uses the
	/// machine Chimera has open. All of it is the engine's (engine.h, ce_mcp_*);
	/// this only gives the calls C# names. The frontend's part is small: start and
	/// stop it when a person asks, and once per pass of the main loop let the
	/// engine run the calls that wait (<see cref="Pending"/>, <see cref="Service"/>).
	/// </summary>
	public static class EngineMcp
	{
		public enum ServerState
		{
			Stopped = 0,
			/// <summary>started; no agent's program is connected</summary>
			Waiting = 1,
			Connected = 2,
		}

		/// <summary>Starts the server. False with the engine's reason when it cannot be started.</summary>
		public static bool Start(out string error)
		{
			var reason = IntPtr.Zero;
			var ok = ChimeraEngine.Instance.ce_mcp_start(ref reason) is not 0;
			error = ok ? "" : ChimeraEngine.PtrToStringUtf8(reason) ?? "the server could not be started";
			return ok;
		}

		public static void Stop() => ChimeraEngine.Instance.ce_mcp_stop();

		/// <summary>A name for this program's pipe (ce_mcp_name); null for none. Takes effect at the next start.</summary>
		public static void SetName(string? name) => ChimeraEngine.Instance.ce_mcp_name(name);

		public static ServerState State => (ServerState) ChimeraEngine.Instance.ce_mcp_state();

		/// <summary>The name and version the agent's program gave; "" when none is connected.</summary>
		public static string Client => ChimeraEngine.PtrToStringUtf8(ChimeraEngine.Instance.ce_mcp_client()) ?? "";

		/// <summary>The pipe's name (Windows) or folder (Linux).</summary>
		public static string Endpoint => ChimeraEngine.PtrToStringUtf8(ChimeraEngine.Instance.ce_mcp_endpoint()) ?? "";

		/// <summary>Whether the agent may change the movie or the machine. Off at every start.</summary>
		public static bool ChangesAllowed
		{
			get => ChimeraEngine.Instance.ce_mcp_changes_allowed() is not 0;
			set => ChimeraEngine.Instance.ce_mcp_allow_changes(value ? 1 : 0);
		}

		/// <summary>True when a call waits to be run. Cheap enough to ask on every pass of the loop.</summary>
		public static bool Pending => ChimeraEngine.Instance.ce_mcp_pending() is not 0;

		/// <summary>
		/// Has the engine run the calls that wait, on the thread that runs the machine. Returns false
		/// when nothing waits any more, or true with a call only the frontend can run: answer it with
		/// <see cref="Answer"/> and call this again.
		/// </summary>
		/// <param name="session">the open machine's handle, or zero</param>
		/// <param name="log">the movie's input log handle, or zero</param>
		/// <param name="frame">the frame the machine is on, or -1 to let the engine say</param>
		/// <param name="hostStatusJson">a JSON object of what only the frontend knows, for the status tool</param>
		public static bool Service(IntPtr session, IntPtr log, long frame, string? hostStatusJson,
			out ulong id, out string tool, out string arguments)
		{
			id = 0;
			IntPtr toolPtr = IntPtr.Zero, argumentsPtr = IntPtr.Zero;
			var handed = ChimeraEngine.Instance.ce_mcp_service(session, log, frame, hostStatusJson, ref id, ref toolPtr, ref argumentsPtr) is not 0;
			tool = handed ? ChimeraEngine.PtrToStringUtf8(toolPtr) ?? "" : "";
			arguments = handed ? ChimeraEngine.PtrToStringUtf8(argumentsPtr) ?? "{}" : "{}";
			return handed;
		}

		/// <summary>
		/// Answers a call <see cref="Service"/> handed over. <paramref name="session"/> is the open machine as it
		/// is now, or zero: the engine reads the values the agent asked to watch from it.
		/// </summary>
		public static void Answer(IntPtr session, ulong id, string text, bool isError)
			=> ChimeraEngine.Instance.ce_mcp_answer(session, id, text, isError ? 1 : 0);

		/// <summary>Where Chimera finds core packages, for list_cores and describe_core.</summary>
		public static void SetCoresFolders(System.Collections.Generic.IEnumerable<string> folders)
			=> ChimeraEngine.Instance.ce_mcp_cores_folders(Newtonsoft.Json.JsonConvert.SerializeObject(folders));

		/// <summary>
		/// capture_series: the machine is on a frame of the call's plan, and the engine takes its picture, which
		/// goes with the call's answer. False with the engine's reason when it could not.
		/// </summary>
		public static bool Capture(IntPtr session, ulong id, long frame, out string error)
		{
			var reason = IntPtr.Zero;
			var ok = ChimeraEngine.Instance.ce_mcp_capture(session, id, frame, ref reason) is not 0;
			error = ok ? "" : ChimeraEngine.PtrToStringUtf8(reason) ?? "the picture could not be taken";
			return ok;
		}

		/// <summary>
		/// advance_until: gives the engine the tool's arguments, which say what to read and what to compare it
		/// with. False with the engine's reason when it cannot be understood or read.
		/// </summary>
		public static bool SetCondition(IntPtr session, string argumentsJson, out string error)
		{
			var reason = IntPtr.Zero;
			var ok = ChimeraEngine.Instance.ce_mcp_condition_set(session, argumentsJson, ref reason) is not 0;
			error = ok ? "" : ChimeraEngine.PtrToStringUtf8(reason) ?? "the condition was not understood";
			return ok;
		}

		/// <summary>Whether the condition is met on the frame the machine is on now. Asked after each frame played.</summary>
		public static bool ConditionMet(IntPtr session) => ChimeraEngine.Instance.ce_mcp_condition_met(session) is 1;

		/// <summary>How many log lines were ever written; the engine keeps the last 5000.</summary>
		public static ulong LogTotal => ChimeraEngine.Instance.ce_mcp_log_total();

		/// <summary>Log line <paramref name="index"/> (0 is the first ever written); "" for one no longer kept.</summary>
		public static string LogLine(ulong index) => ChimeraEngine.PtrToStringUtf8(ChimeraEngine.Instance.ce_mcp_log_line(index)) ?? "";
	}
}
