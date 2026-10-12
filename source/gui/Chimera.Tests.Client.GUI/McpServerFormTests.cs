using System.Diagnostics;
using System.IO;
using System.Threading.Tasks;

using Chimera.Client.Common;
using Chimera.Client.GUI;
using Chimera.Emulation.Common.Engine;

using Newtonsoft.Json.Linq;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Tools &gt; MCP Server... (docs/mcp-server.md): the window starts and stops the engine's
	/// server, says what it is doing, lets changes be allowed only while it is started, and
	/// shows the log. The server is the real one; its pipe is given a name of this test's own,
	/// so a Chimera the same user has open with its server started is not in the way.
	/// </summary>
	[TestClass]
	public class McpServerFormTests
	{
		private static readonly string PipeName = "uitest" + Process.GetCurrentProcess().Id;

		[TestInitialize]
		public void ANameOfOurOwn()
		{
			EngineMcp.Stop();
			EngineMcp.SetName(PipeName);
		}

		[TestCleanup]
		public void StoppedAgain()
		{
			EngineMcp.Stop();
			EngineMcp.SetName(null);
		}

		[TestMethod]
		public void TheWindowOpensWithTheServerStoppedAndStartsAndStopsIt()
		{
			using McpServerForm form = new();
			form.Show();
			Assert.AreEqual("Stopped.", form.StateText);
			Assert.AreEqual("Start", form.StartStopText);
			Assert.AreEqual(EngineMcp.ServerState.Stopped, EngineMcp.State);

			var toggled = 0;
			form.ServerToggled += () => toggled++;
			form.Toggle();
			Assert.AreEqual(EngineMcp.ServerState.Waiting, EngineMcp.State);
			Assert.AreEqual("Started. No agent is connected.", form.StateText);
			Assert.AreEqual("Stop", form.StartStopText);
			Assert.AreEqual(1, toggled);

			form.Toggle();
			Assert.AreEqual(EngineMcp.ServerState.Stopped, EngineMcp.State);
			Assert.AreEqual("Stopped.", form.StateText);
			Assert.AreEqual("Start", form.StartStopText);
			Assert.AreEqual(2, toggled);
			StringAssert.Contains(form.LogText, "the server was started");
			StringAssert.Contains(form.LogText, "the server was stopped");
		}

		[TestMethod]
		public void ChangesCanBeAllowedOnlyWhileStartedAndAreOffAtEveryStart()
		{
			using McpServerForm form = new();
			form.Show();
			// stopped: there is nothing to allow
			Assert.IsFalse(form.AllowChangesEnabled);
			Assert.IsFalse(form.AllowChanges);

			form.Toggle();
			Assert.IsTrue(form.AllowChangesEnabled);
			Assert.IsFalse(form.AllowChanges, "a start must begin with changes off");
			Assert.IsFalse(EngineMcp.ChangesAllowed);

			form.AllowChanges = true;
			Assert.IsTrue(EngineMcp.ChangesAllowed, "ticking the box must reach the engine");
			form.AllowChanges = false;
			Assert.IsFalse(EngineMcp.ChangesAllowed);
			form.AllowChanges = true;

			// stopped and started again: the tick is gone, in the engine and in the window
			form.Toggle();
			Assert.IsFalse(form.AllowChangesEnabled);
			Assert.IsFalse(form.AllowChanges);
			form.Toggle();
			Assert.IsFalse(form.AllowChanges, "a new start must begin with changes off");
			Assert.IsFalse(EngineMcp.ChangesAllowed);
			StringAssert.Contains(form.LogText, "changes by the agent were allowed");
		}

		[TestMethod]
		public void TheSetUpCommandNamesTheProgramBesideTheEngine()
		{
			using McpServerForm form = new();
			StringAssert.StartsWith(McpServerForm.SetupCommand, "claude mcp add chimera -- \"");
			StringAssert.Contains(McpServerForm.SetupCommand, Path.Combine("dll", "chimera-mcp"));
		}

		[TestMethod]
		public void TheLogCanBeSavedAsItIsShown()
		{
			var path = Path.Combine(Path.GetTempPath(), "chimera-mcp-log-" + Path.GetRandomFileName() + ".txt");
			try
			{
				using McpServerForm form = new();
				form.Show();
				form.Toggle();
				form.SaveLog(path);
				Assert.AreEqual(form.LogText, File.ReadAllText(path));
				StringAssert.Contains(File.ReadAllText(path), "the server was started");
				StringAssert.StartsWith(form.NoteText, "Saved ");
			}
			finally
			{
				File.Delete(path);
			}
		}

		/// <summary>The program an agent starts, built beside the engine: build/dll/chimera-mcp. Null when it is not built.</summary>
		private static string? BuiltProgram()
		{
			var name = Chimera.Common.OSTailoredCode.IsUnixHost ? "chimera-mcp" : "chimera-mcp.exe";
			for (var dir = new DirectoryInfo(System.AppContext.BaseDirectory); dir is not null; dir = dir.Parent)
			{
				var candidate = Path.Combine(dir.FullName, "dll", name);
				if (File.Exists(candidate)) return candidate;
			}
			return null;
		}

		/// <summary>
		/// The whole way: the real chimera-mcp is started as an agent's program would start it,
		/// introduces itself and calls tools; this test is the main loop that lets the engine
		/// run them. The window must name the agent, show each call in the log, and - once
		/// stopped - the agent must be told the server is not started.
		/// </summary>
		[TestMethod]
		public void AnAgentConnectsIsNamedAndItsCallsAreLogged()
		{
			var program = BuiltProgram();
			if (program is null) Assert.Inconclusive("chimera-mcp is not built (meson compile -C build/meson-linux && meson install -C build/meson-linux)");

			using McpServerForm form = new();
			form.Show();
			form.Toggle();

			ProcessStartInfo start = new(program!)
			{
				RedirectStandardInput = true,
				RedirectStandardOutput = true,
				UseShellExecute = false,
			};
			start.Environment["CHIMERA_MCP_NAME"] = PipeName;
			using var agent = Process.Start(start)!;
			try
			{
				JObject Ask(string json)
				{
					agent.StandardInput.WriteLine(json);
					agent.StandardInput.Flush();
					Task<string?> reply = agent.StandardOutput.ReadLineAsync();
					var watch = Stopwatch.StartNew();
					while (!reply.IsCompleted && watch.ElapsedMilliseconds < 20000)
					{
						// what MainForm does once per pass of its loop
						while (EngineMcp.Pending && EngineMcp.Service(System.IntPtr.Zero, System.IntPtr.Zero, -1, "{\"project\":\"the ui test\"}",
							out var id, out var tool, out var arguments))
						{
							var text = McpHostTools.Run(null, null, tool, arguments, out var isError);
							EngineMcp.Answer(System.IntPtr.Zero, id, text ?? "", isError);
						}
						System.Threading.Thread.Sleep(2);
					}
					Assert.IsTrue(reply.IsCompleted, "chimera-mcp did not answer: " + json);
					return JObject.Parse(reply.Result!);
				}
				string TextOf(JObject reply) => (string) reply["result"]!["content"]![0]!["text"]!;

				Ask("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\","
					+ "\"capabilities\":{},\"clientInfo\":{\"name\":\"ui test agent\",\"version\":\"7\"}}}");

				// a tool the engine answers itself; it carries what the window told it
				var status = Ask("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"status\",\"arguments\":{}}}");
				Assert.IsFalse((bool) status["result"]!["isError"]!);
				StringAssert.Contains(TextOf(status), "\"project\":\"the ui test\"");
				StringAssert.Contains(TextOf(status), "\"machine_open\":false");

				// a tool the window answers: with no project open, it says so
				var markers = Ask("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"list_markers\",\"arguments\":{}}}");
				Assert.IsTrue((bool) markers["result"]!["isError"]!);
				Assert.AreEqual(McpHostTools.NoMovie, TextOf(markers));

				form.RefreshNow();
				Assert.AreEqual(EngineMcp.ServerState.Connected, EngineMcp.State);
				Assert.AreEqual("Started. Connected: ui test agent 7.", form.StateText);
				StringAssert.Contains(form.LogText, "the agent's program is ui test agent 7");
				StringAssert.Contains(form.LogText, "status {}");
				StringAssert.Contains(form.LogText, "list_markers {}  ->  REFUSED: " + McpHostTools.NoMovie);

				// stopped from the window: the agent's next call is told how to start it
				form.Toggle();
				Assert.AreEqual("Stopped.", form.StateText);
				var after = Ask("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{\"name\":\"status\",\"arguments\":{}}}");
				Assert.IsTrue((bool) after["result"]!["isError"]!);
				StringAssert.Contains(TextOf(after), "Tools > MCP Server...");
			}
			finally
			{
				if (!agent.HasExited) agent.Kill();
			}
		}
	}
}
