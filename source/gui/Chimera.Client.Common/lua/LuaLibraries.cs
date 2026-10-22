using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading;

using NLua;
using NLua.Native;

using Chimera.Common;
using Chimera.Common.StringExtensions;
using Chimera.Emulation.Common;

namespace Chimera.Client.Common
{
	public class LuaLibraries : ILuaLibraries
	{
		public static readonly bool IsAvailable = LuaNativeMethodLoader.EnsureNativeMethodsLoaded();

		private void EnumerateLuaFunctions(string name, Type type, LuaLibraryBase instance)
		{
			var libraryDesc = type.GetCustomAttributes(typeof(DescriptionAttribute), false).Cast<DescriptionAttribute>()
				.Select(static descAttr => descAttr.Description)
				.FirstOrDefault() ?? string.Empty;
			if (instance != null) _lua.NewTable(name);
			foreach (var method in type.GetMethods())
			{
				var foundAttrs = method.GetCustomAttributes(typeof(LuaMethodAttribute), false);
				if (foundAttrs.Length == 0) continue;
				if (instance != null) _lua.RegisterFunction($"{name}.{((LuaMethodAttribute)foundAttrs[0]).Name}", instance, method);
				LibraryFunction libFunc = new(
					library: name,
					libraryDescription: libraryDesc,
					method,
					suggestInREPL: instance != null
				);
				Docs.Add(libFunc);
			}
		}

		public LuaLibraries(
			List<LuaFile> scriptList,
			IEmulatorServiceProvider serviceProvider,
			IMainFormForApi mainForm,
			Config config,
			Action<string> printCallback,
			IDialogParent dialogParent,
			ApiContainer apiContainer)
		{
			if (!IsAvailable)
			{
				throw new InvalidOperationException("The Lua dynamic library was not able to be loaded");
			}

			_th = new NLuaTableHelper(_lua, printCallback);
			_mainForm = mainForm;
			_defaultExceptionCallback = printCallback;
			_logToLuaConsoleCallback = (o) =>
			{
				foreach (object obj in o)
					printCallback(obj.ToString());
			};
			Action<string> logOutputCallback = s => Print(s);
			LuaWait = new AutoResetEvent(false);
			PathEntries = config.PathEntries;
			ScriptList = scriptList;
			Docs.Clear();
			_apiContainer = apiContainer;

			// Register lua libraries
			foreach (var lib in typeof(LuaLibraries).Assembly.GetTypesWithoutLoadErrors()
				.Where(static t => typeof(LuaLibraryBase).IsAssignableFrom(t) && t.IsSealed))
			{
				{
					if (!ServiceInjector.IsAvailable(serviceProvider, lib))
					{
						Util.DebugWriteLine($"couldn't instantiate {lib.Name}, adding to docs only");
						EnumerateLuaFunctions(
							lib.Name.RemoveSuffix("LuaLibrary").ToLowerInvariant(), // why tf aren't we doing this for all of them? or grabbing it from an attribute?
							lib,
							instance: null);
						continue;
					}

					var instance = (LuaLibraryBase) Activator.CreateInstance(lib, this, _apiContainer, logOutputCallback);
					if (!ServiceInjector.UpdateServices(serviceProvider, instance, mayCache: true)) throw new Exception("Lua lib has required service(s) that can't be fulfilled");

					if (instance is ClientLuaLibrary clientLib)
					{
						clientLib.MainForm = _mainForm;
						clientLib.AllAPINames = new(() => string.Join("\n", Docs.Select(static lf => lf.Name)) + "\n"); // Docs may not be fully populated now, depending on the order the types are met in, but definitely will be when this is read
					}
					else if (instance is EventsLuaLibrary eventsLib)
					{
						eventsLib.RemoveNamedFunctionMatching = RemoveNamedFunctionMatching;
					}

					AddLibrary(instance);
				}
			}

			_lua.RegisterFunction("print", this, typeof(LuaLibraries).GetMethod(nameof(Print)));

			var packageTable = (LuaTable) _lua["package"];
			var luaPath = PathEntries.LuaAbsolutePath();
			if (OSTailoredCode.IsUnixHost)
			{
				// add %exe%/Lua to library resolution pathset (LUA_PATH)
				// this is done already on windows, but not on linux it seems?
				packageTable["path"] = $"{luaPath}/?.lua;{luaPath}?/init.lua;{packageTable["path"]}";
				// we need to modifiy the cpath so it looks at our lua dir too, and remove the relative pathing
				// we do this on Windows too, but keep in mind Linux uses .so and Windows use .dll
				// TODO: Does the relative pathing issue Windows has also affect Linux? I'd assume so...
				packageTable["cpath"] = $"{luaPath}/?.so;{luaPath}/loadall.so;{packageTable["cpath"]}";
				packageTable["cpath"] = ((string)packageTable["cpath"]).Replace(";./?.so", "");
			}
			else
			{
				packageTable["cpath"] = $"{luaPath}\\?.dll;{luaPath}\\loadall.dll;{packageTable["cpath"]}";
				packageTable["cpath"] = ((string)packageTable["cpath"]).Replace(";.\\?.dll", "");
			}

			EmulationLuaLibrary.FrameAdvanceCallback = FrameAdvance;
			EmulationLuaLibrary.YieldCallback = EmuYield;
		}

		private ApiContainer _apiContainer;

		private GuiApi GuiAPI => (GuiApi)_apiContainer.Gui;

		private readonly IMainFormForApi _mainForm;

		private Lua _lua = new();

		private Thread _currentHostThread;
		private readonly Lock ThreadMutex = new();

		private Stack<LuaFile> _runningFiles = new();

		public LuaFile/*?*/ CurrentFile
			=> _runningFiles.Count is 0 ? null : _runningFiles.Peek();

		private readonly NLuaTableHelper _th;

		private readonly Action<string> _defaultExceptionCallback;

		private static Action<object[]> _logToLuaConsoleCallback;

		private List<IDisposable> _disposables = new();

		public LuaDocumentation Docs { get; } = new LuaDocumentation();

		private EmulationLuaLibrary EmulationLuaLibrary => (EmulationLuaLibrary)Libraries[typeof(EmulationLuaLibrary)];

		public bool IsRebootingCore { get; set; }

		public bool IsUpdateSupressed { get; set; }

		private readonly IDictionary<Type, LuaLibraryBase> Libraries = new Dictionary<Type, LuaLibraryBase>();

		private EventWaitHandle LuaWait;

		public PathEntryCollection PathEntries { get; private set; }

		public ApiGroup ProhibitedApis { get; private set; } = ApiGroup.NONE;

		public List<LuaFile> ScriptList { get; }

		public NLuaTableHelper GetTableHelper() => _th;

		public void Restart(
			IEmulatorServiceProvider newServiceProvider,
			Config config,
			ApiContainer apiContainer)
		{
			_apiContainer = apiContainer;
			PathEntries = config.PathEntries;
			foreach (var lib in Libraries.Values)
			{
				lib.APIs = _apiContainer;
				if (!ServiceInjector.UpdateServices(newServiceProvider, lib, mayCache: true))
				{
					throw new Exception("Lua lib has required service(s) that can't be fulfilled");
				}

				lib.Restarted();
			}
		}

		public void AddLibrary(LuaLibraryBase lib)
		{
			if (lib is IRegisterFunctions rfLib)
			{
				rfLib.CreateAndRegisterNamedFunction = CreateAndRegisterNamedFunction;
			}

			if (lib is IDisposable disposable)
			{
				_disposables.Add(disposable);
			}

			EnumerateLuaFunctions(lib.Name, lib.GetType(), lib);
			Libraries.Add(lib.GetType(), lib);

			if (lib is IPrintingLibrary printLib)
				_logToLuaConsoleCallback = printLib.Log;
		}

		public void AddTypeToDocs(Type type)
		{
			EnumerateLuaFunctions(type.Name, type, null);
		}

		public bool FrameAdvanceRequested { get; private set; }

		public void CallFrameBeforeEvent()
		{
			if (IsUpdateSupressed) return;

			foreach (LuaFile file in ScriptList)
			{
				foreach (var func in file.Functions.Where(static l => l.Event == NamedLuaFunction.EVENT_TYPE_PREFRAME).ToList())
				{
					func.Call();
				}
			}
		}

		public void CallFrameAfterEvent()
		{
			if (IsUpdateSupressed) return;

			foreach (LuaFile file in ScriptList)
			{
				foreach (var func in file.Functions.Where(static l => l.Event == NamedLuaFunction.EVENT_TYPE_POSTFRAME).ToList())
				{
					func.Call();
				}
			}
		}

		public void Close()
		{
			foreach (LuaFile file in ScriptList)
			{
				foreach (var closeCallback in file.Functions
					.Where(static l => l.Event == NamedLuaFunction.EVENT_TYPE_CONSOLECLOSE)
					.ToList())
				{
					closeCallback.Call();
				}

				file.Functions.Clear();
			}

			ScriptList.Clear();
			foreach (IDisposable disposable in _disposables)
			{
				disposable.Dispose();
			}
			_disposables.Clear();
			_lua.Dispose();
			_lua = null;
		}

		private INamedLuaFunction CreateAndRegisterNamedFunction(
			LuaFunction function,
			string theEvent,
			ApiGroup prohibitedApis,
			string name = null)
		{
			var nlf = new NamedLuaFunction(function, theEvent, _defaultExceptionCallback, CurrentFile, this, prohibitedApis, name);
			CurrentFile.Functions.Add(nlf);
			return nlf;
		}

		private bool RemoveNamedFunctionMatching(Func<INamedLuaFunction, bool> predicate)
		{
			if (CurrentFile.Functions.FirstOrDefault(predicate) is not NamedLuaFunction nlf) return false;
			CurrentFile.Functions.Remove(nlf);
			return true;
		}

		public void Sandbox(LuaFile luaFile, Action callback, Action<string> exceptionCallback = null, ApiGroup prohibitedApis = ApiGroup.NONE)
		{
			ApiGroup previousApiGroup = ProhibitedApis;
			ProhibitedApis |= prohibitedApis;

			bool setThread = SetCurrentThread(luaFile);
			_runningFiles.Push(luaFile);
			LuaSandbox.GetSandbox(luaFile.Thread).Sandbox(callback, exceptionCallback ?? _defaultExceptionCallback);
			_runningFiles.Pop();
			if (setThread) ClearCurrentThread();

			ProhibitedApis = previousApiGroup;
		}

		public LuaThread SpawnCoroutineAndSandbox(string file)
			=> SpawnCoroutineAndSandbox(File.ReadAllText(file), "main", Path.GetDirectoryName(file));

		/// <summary>A coroutine running this Lua text, as a script's runs its file.</summary>
		private LuaThread SpawnCoroutineAndSandbox(string text, string chunkName, string directory)
		{
			var main = _lua.LoadString(text, chunkName);
			var thread = _lua.NewThread(main);
			LuaSandbox.CreateSandbox(thread, directory);
			return thread;
		}

		/// <summary>The global the values <see cref="RunText"/>'s code returned are left in for a moment.</summary>
		private const string RunTextResults = "__chimera_run_text_results";

		/// <summary>
		/// Lua text run to its end in a coroutine of its own, started in <paramref name="file"/>, as a script runs
		/// (the MCP server's run_lua, issue 355). An expression gives its value, as on the Lua Console's input
		/// line; statements give what they return. The functions that wait for a frame are refused, since nothing
		/// is waiting to resume it.
		/// </summary>
		/// <remarks>
		/// Whether the text is an expression is asked of Lua's own <c>load</c>, inside the coroutine, which
		/// answers a failure rather than raising one. Asked through NLua's LoadString first, as ExecuteString
		/// does, a text that did not compile was followed by a crash of Mono on the next call, every time, with
		/// the bundled Lua library; why was not found, and nothing of the agent's is compiled through NLua now.
		/// </remarks>
		/// <returns>what it returned; empty when it raised an error, which <paramref name="onError"/> is told</returns>
		public object[] RunText(LuaFile file, string code, Action<string> onError)
		{
			// the code as a Lua long string, whose brackets it does not contain
			var level = new string('=', Enumerable.Range(0, 64).First(n => !code.Contains("]" + new string('=', n) + "]")));
			var text = $"[{level}[\n{code}]{level}]";
			var chunk = $"local code = {text}\n"
				+ "local run, failed = load('return ' .. code, '=input')\n"
				+ "if not run then run, failed = load(code, '=input') end\n"
				+ "if not run then error(failed, 0) end\n"
				+ $"{RunTextResults} = table.pack(run())\n";
			file.Start(SpawnCoroutineAndSandbox(chunk, "run_lua", Path.GetDirectoryName(file.Path)));
			LuaStatus? status = null;
			var failed = false;
			Sandbox(file, () => status = file.Thread.Resume(), message =>
			{
				failed = true;
				onError(message);
			}, ApiGroup.YIELDING);
			if (failed) return [ ];
			if (status != LuaStatus.OK)
			{
				onError($"The code did not run to its end ({status}).");
				return [ ];
			}
			using var packed = _lua[RunTextResults] as LuaTable;
			_lua[RunTextResults] = null;
			if (packed is null) return [ ];
			var count = packed["n"] is double n ? (int) n : packed["n"] is long l ? (int) l : 0;
			var results = new object[count];
			for (var i = 0; i < count; i++) results[i] = packed[(long) (i + 1)];
			return results;
		}

		public LuaThread SpawnBlankCoroutineAndSandbox(string directory)
		{
			var main = _lua.LoadString("", "main");
			var thread = _lua.NewThread(main);
			LuaSandbox.CreateSandbox(thread, Path.GetDirectoryName(directory));
			return thread;
		}

		/// <summary>
		/// resumes suspended scripts
		/// </summary>
		/// <param name="includeFrameWaiters">should frame waiters be waken up? only use this immediately before a frame of emulation</param>
		/// <returns>true if any script stopped</returns>
		public bool ResumeScripts(bool includeFrameWaiters)
		{
			if (ScriptList.Count == 0 || IsUpdateSupressed)
			{
				return false;
			}

			bool anyStopped = false;
			foreach (var lf in ScriptList.Where(static lf => lf.State is LuaFile.RunState.Running))
			{
				var prohibit = lf.FrameWaiting && !includeFrameWaiters;
				if (!prohibit)
				{
					bool shouldStop = true;
					bool waitForFrame = false;
					bool hadException = false;
					if (!lf.RunningEventsOnly)
					{
						(waitForFrame, shouldStop) = ResumeScript(lf, (s) =>
						{
							_defaultExceptionCallback(s);
							hadException = true;
						});
					}
					// An exception in a main loop should stop everything. The code meant to run each frame cannot be run anymore.
					if (hadException)
						shouldStop = true;
					else
						shouldStop = shouldStop && !lf.ShouldKeepRunning();

					if (shouldStop)
					{
						anyStopped = true;
						lf.Stop();
					}

					lf.FrameWaiting = waitForFrame;
				}
			}

			return anyStopped;
		}

		public object[] ExecuteString(string command)
		{
			const string ChunkName = "input"; // shows up in error messages

			// Use LoadString to separate parsing and execution, to tell syntax errors and runtime errors apart
			LuaFunction func;
			try
			{
				// Adding a return is necessary to get out return values of functions and turn expressions ("1+1" etc.) into valid statements
				func = _lua.LoadString($"return {command}", ChunkName);
			}
			catch (Exception)
			{
				// command may be a valid statement without the added "return"
				// if previous attempt couldn't be parsed, run the raw command
				return _lua.DoString(command, ChunkName);
			}

			using (func)
			{
				return func.Call();
			}
		}

		private void ClearCurrentThread()
		{
			lock (ThreadMutex)
			{
				_currentHostThread = null;
			}
		}

		/// <exception cref="InvalidOperationException">attempted to have Lua running in two host threads at once</exception>
		private bool SetCurrentThread(LuaFile luaFile)
		{
			lock (ThreadMutex)
			{
				bool wasNull = _currentHostThread is null;
				if (!wasNull && _currentHostThread != Thread.CurrentThread)
				{
					throw new InvalidOperationException("Can't run lua from two host threads at the same time!");
				}
				_currentHostThread = Thread.CurrentThread;
				return wasNull;
			}
		}

		public (bool WaitForFrame, bool Terminated) ResumeScript(LuaFile lf, Action<string> exceptionCallback)
		{
			var result = (WaitForFrame: false, Terminated: true);
			LuaStatus? execResult = null;
			Sandbox(lf, () => execResult = lf.Thread.Resume(), exceptionCallback);
			if (execResult == null) return result;

			if (execResult == LuaStatus.Yield)
				result = (WaitForFrame: FrameAdvanceRequested, Terminated: false);
			else if (execResult != LuaStatus.OK)
				exceptionCallback($"{nameof(lf.Thread.Resume)}() returned {execResult}?");

			FrameAdvanceRequested = false;
			lf.RunningEventsOnly = result.Terminated;
			return result;
		}

		public static void Print(params object[] outputs)
		{
			_logToLuaConsoleCallback(outputs);
		}

		private void FrameAdvance()
		{
			if (ProhibitedApis.HasFlag(ApiGroup.YIELDING))
			{
				throw new InvalidOperationException("emu.frameadvance() is not allowed during any callbacks");
			}

			FrameAdvanceRequested = true;
			CurrentFile.Thread.Yield();
		}

		private void EmuYield()
		{
			if (ProhibitedApis.HasFlag(ApiGroup.YIELDING))
			{
				throw new InvalidOperationException("emu.yield() is not allowed during any callbacks");
			}

			CurrentFile.Thread.Yield();
		}
	}
}
