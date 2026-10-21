using System.Linq;
using System.Reflection;

using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common.lua
{
	/// <summary>
	/// The events no Chimera core can honour say so in the Lua Functions list (issue 341). Each of
	/// them would run a Lua function in the middle of a frame, which a core in the sandbox cannot
	/// stop to do, so every core refuses them with an error. The list marks them "[Not available]"
	/// and their descriptions begin by saying why.
	/// </summary>
	[DoNotParallelize] // as LuaEventTests: run beside the other Lua tests, the scripts' output got mixed
	[TestClass]
	public class LuaUnavailableEventsTests
	{
		private static readonly string[] Refused =
		[
			// oninputpoll is not here: a core that reports its reads of the controls has it (issue 344)
			"on_bus_exec", "on_bus_exec_any", "on_bus_read", "on_bus_write",
			"onmemoryexecute", "onmemoryexecuteany", "onmemoryread", "onmemorywrite",
		];

		private static LibraryFunction[] EventFunctions()
			=> typeof(EventsLuaLibrary).GetMethods()
				.Where(m => m.GetCustomAttribute<LuaMethodAttribute>(false) is not null)
				.Select(m => new LibraryFunction("event", "", m))
				.ToArray();

		[TestMethod]
		public void TheRefusedEventsAreMarkedAndNoOthers()
		{
			var marked = EventFunctions().Where(f => f.IsUnavailable).Select(f => f.Name).OrderBy(n => n).ToArray();
			CollectionAssert.AreEqual(Refused.OrderBy(n => n).ToArray(), marked);
		}

		[TestMethod]
		public void TheirDescriptionsSayTheyAreNotAvailable()
		{
			foreach (var f in EventFunctions().Where(f => Refused.Contains(f.Name)))
			{
				StringAssert.StartsWith(f.Description, "Not available in Chimera: every core refuses it with an error.", f.Name);
				StringAssert.Contains(f.Description, "cannot stop part way through a frame", f.Name);
			}
		}

		/// <summary>The guard the examples show: the refusal comes back from pcall and the script goes on.</summary>
		[TestMethod]
		public void AGuardedRegistrationGetsTheReasonAndTheScriptGoesOn()
		{
			LuaTestContext context = new();
			context.AddScript("guarded_bus_read.lua", true);
			context.RunYielding();

			Assert.AreEqual(2, context.loggedMessages.Count, string.Join("\n", context.loggedMessages));
			StringAssert.Contains(context.loggedMessages[0], "cannot stop part way through a frame");
			Assert.AreEqual("still running", context.loggedMessages[1]);
		}

		/// <summary>Unguarded, the console's first line says why, and says it once.</summary>
		[TestMethod]
		public void AnUnguardedRegistrationLogsTheReasonOnce()
		{
			LuaTestContext context = new();
			context.AddScript("unguarded_bus_read.lua", true);
			context.RunYielding();

			Assert.AreEqual(1, context.loggedMessages.Count, string.Join("\n", context.loggedMessages));
			var log = context.loggedMessages[0];
			StringAssert.Contains(log.Split('\n')[0], "cannot stop part way through a frame", log);
			Assert.AreEqual(log.IndexOf("cannot stop part way through a frame"), log.LastIndexOf("cannot stop part way through a frame"), log);
			StringAssert.Contains(log, "EventsLuaLibrary.OnBusRead", log);
		}
	}
}
