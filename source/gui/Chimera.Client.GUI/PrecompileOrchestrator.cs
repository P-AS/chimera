using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading;

using Chimera.Client.Common;
using Chimera.Common;
using Chimera.Emulation.Common.Engine;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// Compiles a game's code for a core that recompiles, before the game is
	/// ever run (docs/compile-cache.md). The objects are a pure function of the
	/// game, the package and the target CPU, so this is not a decision anybody
	/// has to make - only work that is better done once, in parallel, with
	/// something on screen, than a minute at a time at every boot.
	///
	/// The work happens in child processes: this same frontend, headless, each
	/// one a precompile session compiling its share. They report every object
	/// they store, and this collects them into the game's manifest.
	/// </summary>
	public static class PrecompileOrchestrator
	{
		/// <summary>"[cache] stored &lt;name&gt; &lt;sha1&gt;", as the engine's cache bridge prints it.</summary>
		private static readonly Regex CacheLine = new(@"^\[cache\] (stored|fetched) (\S+) ([0-9A-Fa-f]{40})\s*$", RegexOptions.Compiled);

		/// <summary>"Precompiled 3/21 modules", as a session prints it.</summary>
		private static readonly Regex ProgressLine = new(@"^Precompiled (\d+)/(\d+) modules", RegexOptions.Compiled);

		/// <summary>
		/// What a session said as it refused the job. A precompile session is a
		/// headless frontend, so the thing that stopped it is a modal it could not
		/// show: "[headless] text: ..." is that modal's words. Without this the
		/// wizard can only say the compile stopped - and on Windows the parent is a
		/// GUI-subsystem process whose Console.Error goes nowhere, so the reason
		/// was invisible in every log.
		/// </summary>
		private static readonly Regex RefusalLine = new(@"^\[headless\] text: (.+)$", RegexOptions.Compiled);

		/// <summary>Why the last Run returned null, in the words of whatever refused it; null when it succeeded.</summary>
		public static string LastFailure { get; private set; }

		/// <summary>
		/// How many sessions run side by side. The engine decides, from what the
		/// machine can really spare (compile_sessions.h): about 8 GB a session
		/// after 4 GB kept back, never more than half the processors, never
		/// more than eight, never fewer than one. Fewer sessions is slower; too
		/// many is a machine that swaps, fills its system drive with page file,
		/// or kills them (chimera issue 259).
		/// </summary>
		public static int Workers => WorkersFor(0);

		/// <summary>The same, with one session counted at <paramref name="sessionBytes"/> (0: the engine's default).</summary>
		public static int WorkersFor(ulong sessionBytes)
			=> ChimeraEngine.Instance.ce_compile_sessions(Environment.ProcessorCount, ChimeraEngine.Instance.ce_compile_memory_to_spare(), sessionBytes);

		/// <summary>
		/// What an exit code says about how a session ended. A session that dies
		/// prints nothing on its way out, so its code is the only evidence there
		/// is - and reporting "failed without saying why" while holding it was
		/// throwing the evidence away. It was worth more than the sentence: the
		/// code said 0xC0000005, which is what turned a supposed memory problem
		/// into the real one (a JIT region that wrapped onto its own code, fixed
		/// in the rpcs3 core, 2026-09-12).
		/// </summary>
		internal static string WhyItDied(int code) => code switch
		{
			137 => "killed by the system (out of memory)",          // 128 + SIGKILL
			139 => "crashed (segmentation fault)",                  // 128 + SIGSEGV
			134 => "aborted",                                       // 128 + SIGABRT
			132 => "crashed (illegal instruction)",                 // 128 + SIGILL: how the sandbox stops a machine
			-1073741819 => "crashed (access violation)",            // 0xC0000005
			-1073741801 => "out of memory",                         // 0xC0000017
			-1073740791 => "crashed (stack buffer overrun)",        // 0xC0000409
			-1073741571 => "crashed (ran out of stack)",            // 0xC00000FD
			-1073741795 => "crashed (illegal instruction)",         // 0xC000001D: the same, on Windows
			HeadlessMode.EXIT_CODE_DIALOG => "stopped at a message it could not show", // what it said is the refusal, when it said one
			_ => $"exit code {code}",
		};

		/// <summary>Whether a code is one the machine gives for running out of memory.</summary>
		internal static bool DiedForWantOfMemory(int code) => code is 137 or -1073741801;

		/// <summary>
		/// What a session says when the sandbox could not get the memory for the
		/// machine: a refusal like any other to the code that reads it, and the
		/// one refusal that fewer sessions at once can cure.
		/// </summary>
		internal static bool RefusedForWantOfMemory(string refusal)
			=> refusal is not null && refusal.IndexOf("memory block", StringComparison.OrdinalIgnoreCase) >= 0;

		/// <summary>
		/// The failure in a person's words when memory was what ran out. It
		/// says where the disk space went, because that is what was seen and
		/// nothing on screen explained it: Windows makes its page file bigger to
		/// give programs the memory they ask for, the file is on the system
		/// drive whatever folder Chimera's data is in, and the drive fills while
		/// a compile runs (chimera issue 259).
		/// </summary>
		internal static string OutOfMemoryMessage(int sessions, ulong sessionBytes = 0)
		{
			var each = (sessionBytes is 0 ? ChimeraEngine.Instance.ce_compile_session_bytes() : sessionBytes) / (1024.0 * 1024 * 1024);
			var what = $"The computer ran out of memory while compiling. A compile session needs about {each:0} GB"
				+ (sessions > 1 ? $", and {sessions} ran at once." : ".");
			if (OSTailoredCode.IsUnixHost) return what + " Close other programs and compile again. Tools > Pre-Compiled Modules sets how much memory a session is counted at.";
			return what
				+ " When Windows runs short of memory it makes its page file bigger, and that file is on the system drive"
				+ " (usually C:) wherever Chimera keeps its data, so free space there falls while a compile runs."
				+ " Close other programs, or free some space on the system drive, and compile again."
				+ " Tools > Pre-Compiled Modules sets how much memory a session is counted at.";
		}

		/// <summary>An object as the wizard shows it: named, hashed, and either there or not.</summary>
		public sealed class Entry
		{
			public string Name { get; set; }
			public string Sha1 { get; set; }
			public bool Present { get; set; }
		}

		/// <summary>What a game needs and what of it is there, from the manifest a previous compile left.</summary>
		public static List<Entry> Survey(string cacheDir)
		{
			var manifest = CoreCacheManifest.Load(cacheDir);
			if (manifest is null) return [ ];
			return manifest.Files
				.Select(f => new Entry
				{
					Name = f.Name,
					Sha1 = f.Sha1,
					Present = string.Equals(CoreCacheManifest.HashOf(cacheDir, f.Name), f.Sha1, StringComparison.OrdinalIgnoreCase),
				})
				.ToList();
		}

		/// <summary>Everything the game needs is on disk and unchanged.</summary>
		public static bool Satisfied(string cacheDir)
		{
			var entries = Survey(cacheDir);
			return entries.Count is not 0 && entries.All(e => e.Present);
		}

		/// <summary>
		/// Runs the sessions and collects what they compile. onEntry is called as
		/// each object lands, so a list can fill while the work happens; onProgress
		/// gets the modules finished across every session and the game's module
		/// count, which they all report the same. Returns the manifest that was
		/// written, or null when nothing usable came of it.
		/// </summary>
		public static CoreCacheManifest Run(
			string packagePath, string configPath, string romPath, string romSha1, string cacheDir,
			string coreName, string coreVersion,
			Action<Entry> onEntry, Action<uint, uint> onProgress, Func<bool> cancelled,
			IReadOnlyDictionary<string, string> firmware = null, string slotsFile = null,
			int sessionGigabytes = 0)
		{
			// the setting (Config.CompileSessionGigabytes); 0 is the engine's default
			var sessionBytes = sessionGigabytes > 0 ? (ulong)sessionGigabytes << 30 : 0;
			LastFailure = null;
			if (string.IsNullOrEmpty(romPath) || !File.Exists(romPath) || cacheDir is null)
			{
				LastFailure = "there is no game file to compile";
				return null;
			}

			var found = new Dictionary<string, string>(StringComparer.Ordinal);
			LastFailure = null;
			var workers = WorkersFor(sessionBytes);
			// kept across attempts: a session's own words are worth more than
			// anything inferred from an exit code, whichever attempt said them
			string refused = null;

			// A session the system killed took its share of the work with it, and a
			// manifest written from what the others managed would be a list of what
			// this game needs with holes in it. The answer is not to give up: it is
			// to want less at once. Halving the sessions and going again turns the
			// one failure a person cannot act on into a slower success they do not
			// have to know about.
			for (;;)
			{
				var done = new uint[workers];
				var total = new uint[workers];
				var processes = new List<Process>();
				string failure = null;

				void Line(int index, string line)
				{
					var refusal = RefusalLine.Match(line);
					if (refusal.Success)
					{
						// first one wins: the others are the same modal in the other sessions
						lock (found)
						{
							failure ??= refusal.Groups[1].Value;
							refused ??= refusal.Groups[1].Value;
						}
						return;
					}
					var m = CacheLine.Match(line);
					if (m.Success)
					{
						lock (found)
						{
							// stored and fetched both count: a worker that found an
							// object another one had just written still needs it
							found[m.Groups[2].Value] = m.Groups[3].Value.ToUpperInvariant();
						}
						onEntry?.Invoke(new Entry { Name = m.Groups[2].Value, Sha1 = m.Groups[3].Value.ToUpperInvariant(), Present = true });
						return;
					}
					m = ProgressLine.Match(line);
					if (!m.Success) return;
					// Every session reports the game's module count, and finishes a
					// share of it: one total, and the dones add up.
					done[index] = uint.Parse(m.Groups[1].Value);
					total[index] = uint.Parse(m.Groups[2].Value);
					onProgress?.Invoke((uint)done.Sum(v => (long)v), total.Max());
				}

				for (var i = 0; i < workers; i++)
				{
					var args = new List<string> { "--headless" };
					if (!string.IsNullOrEmpty(configPath)) args.Add($"--config={configPath}");
					args.Add($"--core={packagePath}");
					args.Add($"--precompile={i}/{workers}");
					// Say the firmware outright. Going through the config means the
					// parent must have written it there first, and the wizard can hold
					// a path that never gets written - a core may call a firmware
					// optional that this GAME cannot boot without. A session refused at
					// boot compiles nothing.
					foreach (var (id, path) in firmware ?? new Dictionary<string, string>())
					{
						args.Add($"--firmware={id}={path}");
					}
					// and the project's other files: a licensed package, an
					// update, an encrypted disc boot on the game file alone
					// nowhere (chimera#140)
					if (!string.IsNullOrEmpty(slotsFile)) args.Add($"--precompile-slots={slotsFile}");
					args.Add(romPath);
					var index = i;
					var p = SelfProcess.Start(args, line => Line(index, line));
					if (p is not null) processes.Add(p);
				}
				if (processes.Count is 0) return null;

				var stopped = false;
				// the least the machine could spare while they ran, looked at
				// every couple of seconds: after they are dead it has it all back
				ulong leastToSpare = 0;
				var looks = 0;
				while (processes.Any(p => !p.HasExited))
				{
					Thread.Sleep(150);
					if (looks++ % 13 == 0)
					{
						var now = ChimeraEngine.Instance.ce_compile_memory_to_spare();
						if (now != 0 && (leastToSpare == 0 || now < leastToSpare)) leastToSpare = now;
					}
					if (stopped || cancelled?.Invoke() != true) continue;
					stopped = true;
					foreach (var p in processes.Where(p => !p.HasExited))
					{
						try { p.Kill(); } catch (Exception) { /* it finished on its own */ }
					}
				}
				foreach (var p in processes) p.WaitForExit();
				if (stopped) return null;

				// A session that died took its share with it, and a manifest written
				// from what the others managed would be a list of what this game
				// needs with holes in it - which is exactly what must not happen.
				var dead = processes.Where(p => p.ExitCode != 0).ToList();
				if (dead.Count is 0) break;

				var why = string.Join(", ",
					dead.Select(p => WhyItDied(p.ExitCode)).Distinct(StringComparer.Ordinal));
				Console.Error.WriteLine(
					$"precompile: {dead.Count} of {processes.Count} sessions failed ({why})");

				// Memory is what ran out when the system says it killed them for
				// it, when a session said the sandbox could not get its memory, or
				// when the machine had next to nothing left while they ran - a
				// session Windows could not give a page to dies of an access
				// violation like any other crash, and only the machine's own
				// figures tell the two apart.
				var ranShort = ChimeraEngine.Instance.ce_compile_memory_ran_short(leastToSpare, sessionBytes) != 0;
				var wantOfMemory = failure is null
					? ranShort || dead.All(p => DiedForWantOfMemory(p.ExitCode))
					: RefusedForWantOfMemory(failure);
				if (wantOfMemory && workers > 1)
				{
					workers = Math.Max(1, workers / 2);
					Console.Error.WriteLine($"precompile: memory ran out; retrying with {workers} session(s)");
					refused = null;
					continue;
				}
				if (wantOfMemory)
				{
					LastFailure = OutOfMemoryMessage(processes.Count, sessionBytes);
					return null;
				}

				// Whatever else happened, it is not "without saying why" any more.
				LastFailure = failure
					?? $"{dead.Count} of {processes.Count} compile sessions failed: {why}."
						+ (workers > 1
							? " Closing other programs, or a machine with more memory, would let it run fewer at once."
							: " This ran one session at a time already, so memory is unlikely to be the reason.");
				return null;
			}

			// What the sessions compiled is what this game needs. Written beside
			// the objects; the project records the same list.
			var manifest = new CoreCacheManifest
			{
				RomName = Path.GetFileName(romPath),
				RomSha1 = romSha1,
				// Who compiled it, which the directory used to say by its name.
				// A game's code is filed under the game alone now, so this is the
				// only record of which build produced these objects - and the only
				// thing standing between a rebuilt core and somebody else's code.
				CoreName = coreName,
				CoreVersion = coreVersion,
				Compiled = DateTime.UtcNow,
				Files = found.OrderBy(kv => kv.Key, StringComparer.Ordinal)
					.Select(kv => new CoreCacheFile { Name = kv.Key, Sha1 = kv.Value })
					.ToList(),
			};
			if (manifest.Files.Count is 0)
			{
				LastFailure = refused ?? "the sessions compiled nothing for this game";
				return null;
			}
			manifest.Save(cacheDir);
			return manifest;
		}
	}

	/// <summary>Launches this same frontend as a child process (headless), streaming its output lines.</summary>
	public static class SelfProcess
	{
		/// <summary>
		/// One argument, quoted the way Windows will parse it back.
		/// 
		/// A backslash is ONLY special before a quote: CommandLineToArgvW reads
		/// 2n of them plus a quote as n backslashes and a delimiter, 2n+1 as n
		/// backslashes and a literal quote, and leaves any other run alone. So
		/// escaping every backslash - which is what this did - doubles every
		/// separator in a path, and "C:\games\x.iso" reaches the child as
		/// "C:\\games\\x.iso", which does not exist. It only bit paths that
		/// needed quoting in the first place, i.e. ones containing a space,
		/// which is why it survived: the precompile sessions all failed at once
		/// on a game whose folder had one.
		/// </summary>
		private static string Quote(string a)
		{
			if (a.Length > 0 && a.IndexOfAny([ ' ', '"', '\t' ]) < 0) return a;
			var sb = new System.Text.StringBuilder(a.Length + 8);
			sb.Append('"');
			for (var i = 0; i < a.Length; i++)
			{
				var slashes = 0;
				while (i < a.Length && a[i] == '\\') { slashes++; i++; }
				if (i == a.Length)
				{
					// run at the end: doubled, so the closing quote stays a delimiter
					sb.Append('\\', slashes * 2);
					break;
				}
				if (a[i] == '"')
				{
					sb.Append('\\', slashes * 2 + 1).Append('"');
				}
				else
				{
					sb.Append('\\', slashes).Append(a[i]);
				}
			}
			sb.Append('"');
			return sb.ToString();
		}

		public static Process Start(IEnumerable<string> args, Action<string> onLine)
		{
			var psi = new ProcessStartInfo
			{
				UseShellExecute = false,
				RedirectStandardOutput = true,
				RedirectStandardError = true,
				CreateNoWindow = true,
			};
			var exe = Process.GetCurrentProcess().MainModule?.FileName ?? "";
			var entry = System.Reflection.Assembly.GetEntryAssembly()?.Location ?? "";
			var underMono = Type.GetType("Mono.Runtime") is not null;
			var all = new List<string>();
			psi.FileName = exe; // under mono: the runtime itself, then the assembly
			if (underMono) all.Add(entry);
			all.AddRange(args);
			psi.Arguments = string.Join(" ", all.Select(Quote));
			try
			{
				var p = Process.Start(psi);
				if (p is null) return null;
				p.OutputDataReceived += (_, e) => { if (e.Data is not null) onLine(e.Data); };
				p.ErrorDataReceived += (_, e) => { if (e.Data is not null) onLine(e.Data); };
				p.BeginOutputReadLine();
				p.BeginErrorReadLine();
				return p;
			}
			catch (Exception ex)
			{
				Console.Error.WriteLine($"precompile: could not start a worker: {ex.Message}");
				return null;
			}
		}
	}
}
