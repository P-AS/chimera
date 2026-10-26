using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace Chimera.Emulation.Common.Waterbox
{
	/// <summary>
	/// The built-in factory for waterbox packages (core.wbx + waterbox.config). One
	/// instance per loaded package: it parses that package's waterbox.config and
	/// produces the generic <see cref="WaterboxCore"/> for its declared system.
	/// Constructed by CorePackageLoader when it sees a waterbox package - there is no
	/// per-core managed assembly.
	/// </summary>
	public sealed class WaterboxCoreFactory : ICoreFactory, ICoreIdentity, ICoreFirmwareUser
	{
		public const string WbxFileName = "core.wbx";
		public const string ConfigFileName = "waterbox.config";

		private readonly WaterboxConfig _cfg;
		private readonly string _packageDir;

		public WaterboxCoreFactory(string packageDir)
		{
			_packageDir = packageDir;
			_cfg = WaterboxConfig.FromJson(File.ReadAllText(Path.Combine(packageDir, ConfigFileName)))
				?? throw new InvalidOperationException($"{ConfigFileName} is empty or invalid");
			// before anything else is read out of it: a package from the far side of an
			// ABI change may declare fields this build has no idea how to interpret, so
			// the version check cannot come after the interpreting
			if (GuestAbi.Refuse(_cfg.Abi) is { } refusal) throw new NotSupportedException($"core package \"{_cfg.CoreName}\" is {refusal}");
			if (_cfg.Machines is { Count: > 0 } machines)
			{
				// a package of machines must say which setting picks one, and that
				// setting must be one it actually declares - otherwise every session
				// would silently be the first machine
				if (string.IsNullOrEmpty(_cfg.MachineSetting))
				{
					throw new InvalidOperationException($"{ConfigFileName} declares machines but no machineSetting to pick between them");
				}
				if (_cfg.Settings?.Exists(s => s.Name == _cfg.MachineSetting) is not true)
				{
					throw new InvalidOperationException($"{ConfigFileName}: machineSetting \"{_cfg.MachineSetting}\" is not one of this package's settings");
				}
				foreach (var machine in machines)
				{
					if (string.IsNullOrEmpty(machine.Id)) throw new InvalidOperationException($"{ConfigFileName}: a machine has no id");
					// a machine without its own controller uses the package's -
					// dolphin's GameCube and Wii share one pad - so only a machine
					// with NO controller anywhere is a broken declaration
					if (machine.Input?.Buttons is null && _cfg.Input?.Buttons is null)
					{
						throw new InvalidOperationException($"{ConfigFileName}: machine \"{machine.Id}\" has no controller, and the package declares none to fall back on");
					}
				}
			}
			else if (string.IsNullOrEmpty(_cfg.SystemId))
			{
				throw new InvalidOperationException($"{ConfigFileName} is missing systemId");
			}
		}

		/// <summary>True if the directory is a waterbox package (has both core.wbx and waterbox.config).</summary>
		public static bool IsWaterboxPackage(string packageDir)
			=> File.Exists(Path.Combine(packageDir, WbxFileName))
				&& File.Exists(Path.Combine(packageDir, ConfigFileName));

		public string CoreName => _cfg.CoreName ?? "Waterbox";

		/// <summary>The package's identity, so lists of available cores name the core rather than the adapter.</summary>
		public CoreAttribute CoreIdentity => WaterboxCore.IdentityOf(_cfg);

		public WaterboxConfig Config => _cfg;
		public string PackageDir => _packageDir;


		/// <summary>Rom-extension -&gt; systemId map from waterbox.config, over every machine.</summary>
		public Dictionary<string, string> Extensions => _cfg.AllExtensions;

		/// <summary>Every machine this package can be (one, for most packages).</summary>
		public IReadOnlyList<string> SystemIds => _cfg.SystemIds;

		/// <summary>The files this core expects the user to provide (see waterbox.config "firmware").</summary>
		public IReadOnlyList<CoreFirmwareDecl> Firmware => _cfg.Firmware ?? (IReadOnlyList<CoreFirmwareDecl>) [ ];

		/// <summary>Of those, the ones the decisions actually called for at the last load.</summary>
		public IReadOnlyList<CoreFirmwareDecl> FirmwareInUse { get; private set; } = [ ];

		public Type CoreType => typeof(WaterboxCore);

		public Type SettingsType => typeof(WaterboxCoreSettings);


		public IEmulator Create(CoreCreationContext ctx)
		{
			// a game runs only in a project (user-decided, 2026-10-26)
			var project = ctx.Project
				?? throw new InvalidOperationException($"{CoreName}: a game opens only in a project");
			var settings = MachinePinnedSettings(ctx);
			// one line per boot, greppable: a project open must produce exactly
			// one of these (the witness gate counts them)
			Console.WriteLine($"[waterbox] booting {CoreName}");
			return new WaterboxCore(project, _cfg, _packageDir, settings, ResolveFirmware(ctx));
		}

		/// <summary>
		/// The settings this boot runs with, with the machine settled.
		///
		/// A project pins the machine like any other setting, so it is usually
		/// already here. A project that does not name it (one written by hand)
		/// still names its system (its Platform), and that system IS a machine,
		/// so it decides; without this such a project of a Master System game
		/// would boot the package's first machine, a Mega Drive, and refuse the
		/// cartridge.
		/// </summary>
		private WaterboxCoreSettings? MachinePinnedSettings(CoreCreationContext ctx)
		{
			var settings = ctx.Settings as WaterboxCoreSettings;
			// both together: the constructor above refuses a package that declares
			// machines without naming the setting that picks between them
			if (_cfg.Machines is not { Count: > 0 } machines
				|| _cfg.MachineSetting is not { Length: > 0 } machineSetting)
			{
				return settings;
			}

			// what the CALLER said, not what the package defaults to: every package
			// has a default machine, so asking the effective settings would mean the
			// rom never gets a say
			var pinned = settings?.Values is not null && settings.Values.TryGetValue(machineSetting, out var value)
				? value?.ToString() ?? ""
				: "";
			if (machines.Exists(m => m.Selects(pinned))) return settings;

			var routed = _cfg.MachineForSystem(ctx.Game?.System);
			if (routed is null) return settings;

			settings = settings?.Clone() ?? new WaterboxCoreSettings();
			settings.Values[machineSetting] = routed.When is { Count: > 0 } ? routed.When[0] : routed.Id ?? "";
			return settings;
		}

		/// <summary>
		/// Collects the files the user provided for this core's declarations,
		/// asking only for what the DECISIONS call for: the engine's firmware
		/// decision tree evaluates the declaration against the slot map and the
		/// effective settings (docs/project.md). A missing required file stops
		/// the load here, before the sandbox is built, so what the user sees
		/// names the file rather than "Init failed".
		/// </summary>
		private Dictionary<string, byte[]> ResolveFirmware(CoreCreationContext ctx)
		{
			// the project says which file fills which slot
			var slotsJson = ctx.Project?.SlotsJson ?? "{}";
			var effective = WaterboxCore.EffectiveSettingsFor(
				_cfg, MachinePinnedSettings(ctx));

			var applicable = Engine.EngineFirmware.Evaluate(
				_cfg.RawFirmwareJson, slotsJson, Newtonsoft.Json.JsonConvert.SerializeObject(effective));

			Dictionary<string, byte[]> resolved = new();
			List<CoreFirmwareDecl> used = new();
			FirmwareInUse = used;
			var needed = new List<CoreFirmwareDecl>();
			foreach (var (id, index) in applicable)
			{
				var decl = index >= 0 && index < Firmware.Count ? Firmware[index] : null;
				if (decl is null || decl.Id != id) decl = Firmware.FirstOrDefault(d => d.Id == id);
				if (decl is not null) needed.Add(decl);
			}
			// and what THIS GAME needs, which only the core can say
			needed.AddRange(GameFirmwareFor(ctx, effective, slotsJson));
			foreach (var decl in needed)
			{
				used.Add(decl);
				var bytes = ctx.FirmwareProvider?.Invoke(decl);
				if (bytes is null)
				{
					// A declaration may say the core can start without it
					// (an optional identity file, an expansion rom); absent
					// simply means not mounted, and the core carries on.
					if (!decl.Required) continue;
					throw new MissingFirmwareException(
						$"{CoreName} needs firmware that has not been provided: {decl.DisplayName}."
							+ " Put the file in the Firmware folder, or point at it in Config > Firmware,"
							+ " and open the project again.");
				}
				resolved[decl.Id] = bytes;
			}
			return resolved;
		}

		/// <summary>
		/// The firmware the game in hand needs beyond the package's declarations,
		/// for a core whose package says the game decides
		/// (<see cref="WaterboxConfig.GameFirmware"/>): the core is asked, on the
		/// game and its files, exactly as the wizard asked it when the project was made
		/// (its SuggestSettings answer's "firmware"). Asked again at every load
		/// rather than read from the project, so a project cannot carry a list
		/// the core no longer agrees with. A core that cannot answer needs
		/// nothing more as far as this goes: it then says what it lacks itself,
		/// when it starts.
		/// </summary>
		private List<CoreFirmwareDecl> GameFirmwareFor(CoreCreationContext ctx, Dictionary<string, object> effective, string slotsJson)
		{
			if (!_cfg.GameFirmware || ctx.Project is not { } project) return [ ];
			try
			{
				// the project as the run will have it: its files and its slot map
				var answer = Engine.EngineSession.Suggest(_packageDir, project);
				if (string.IsNullOrWhiteSpace(answer)) return [ ];
				var root = Newtonsoft.Json.Linq.JObject.Parse(answer);
				var decls = WaterboxConfig.ParseGameFirmware(
					root["firmware"]?.ToString(Newtonsoft.Json.Formatting.None), Firmware.Select(static d => d.Id), out var raw);
				// an entry may apply with some settings only (a BIOS set needed
				// for one BIOS choice): the same decision tree decides
				var settingsJson = Newtonsoft.Json.JsonConvert.SerializeObject(effective);
				return Engine.EngineFirmware.Evaluate(raw, slotsJson, settingsJson)
					.Select(entry => entry.Index >= 0 && entry.Index < decls.Count ? decls[entry.Index] : null)
					.Where(static d => d is not null)
					.Select(static d => d!)
					.ToList();
			}
			catch (Exception e) when (e is InvalidOperationException or Newtonsoft.Json.JsonException)
			{
				return [ ];
			}
		}
	}
}
