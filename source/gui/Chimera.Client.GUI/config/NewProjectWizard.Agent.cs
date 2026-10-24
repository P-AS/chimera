#nullable enable

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

using Chimera.Client.Common;
using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Client.GUI
{
	public sealed partial class NewProjectWizard
	{
		/// <summary>A firmware file the answers call for that was found nowhere, with the files that would do.</summary>
		public sealed record MissingFirmware(string Id, string Name, IReadOnlyList<CoreFirmwareDecl> Accepted);

		/// <summary>What <see cref="CompleteForAgent"/> made: the project, or why not.</summary>
		public sealed class AgentOutcome
		{
			/// <summary>Set when the project was made: <see cref="CreatedProject"/>.</summary>
			public bool Made { get; init; }

			public string? Refusal { get; init; }

			/// <summary>When firmware stood in the way: each file missing, and where it was looked for.</summary>
			public IReadOnlyList<MissingFirmware> Missing { get; init; } = [ ];

			public IReadOnlyList<string> Searched { get; init; } = [ ];
		}

		/// <summary>
		/// The MCP server's create_project (issue 355): an agent's answers taken through the wizard's pages as a
		/// person would leave them having changed nothing else, and the project made - with the window never
		/// shown, so nothing is asked. The core's suggested settings for the game are applied as on arriving at
		/// the settings page, then the agent's own over them; firmware is looked for where the firmware page
		/// looks, and what is missing is listed rather than asked for. It is this window's own code doing it, so
		/// the project is the one the wizard would make. A core that compiles a game's code before its first
		/// run is the one thing it does not do: that takes the compile page, and is refused when not yet done.
		/// </summary>
		/// <param name="coreName">the core, by the name it gives itself or its package's file name</param>
		/// <param name="preferredSha1">the build to use when several of that core are installed, or null for the newest</param>
		/// <param name="system">the machine, by its id or what it is called, for a package that is several; or null</param>
		/// <param name="files">files by slot</param>
		/// <param name="games">files with no slot named: each goes in the slot that takes its kind of file</param>
		/// <param name="settings">settings by name, over the core's defaults and suggestions</param>
		/// <param name="firmware">firmware files by id, taken as if picked on the firmware page</param>
		public AgentOutcome CompleteForAgent(
			string coreName,
			string? preferredSha1,
			string? system,
			IReadOnlyList<(string Slot, string Path)> files,
			IReadOnlyList<string> games,
			IReadOnlyDictionary<string, object> settings,
			IReadOnlyList<(string Id, string Path)> firmware)
		{
			static AgentOutcome Refused(string why) => new() { Refusal = why };

			// the core, and the machine the agent named
			var named = _cores.Where(c => string.Equals(c.Name, coreName, StringComparison.OrdinalIgnoreCase)
				|| string.Equals(Path.GetFileName(c.Path), coreName, StringComparison.OrdinalIgnoreCase)
				|| string.Equals(Path.GetFileNameWithoutExtension(c.Path), coreName, StringComparison.OrdinalIgnoreCase)).ToList();
			var core = named.FirstOrDefault(c => preferredSha1 is not null && string.Equals(c.Sha1, preferredSha1, StringComparison.OrdinalIgnoreCase))
				?? named.FirstOrDefault();
			if (core is null || !Choose(core)) return Refused($"There is no core called '{coreName}' in the cores folder. list_cores names them.");
			LoadChosenPackage();
			if (_cfg is null) return Refused($"{core.Name} has no usable {WaterboxCoreFactory.ConfigFileName} in its package.");

			Dictionary<string, object> answered = new();
			foreach (var (name, value) in settings) answered[name] = value;
			if (system is { Length: not 0 })
			{
				if (_cfg.Machines is { Count: > 0 } machines && _cfg.MachineSetting is { Length: not 0 } machineSetting)
				{
					var machine = machines.FirstOrDefault(m => Names(m).Any(n => string.Equals(n, system, StringComparison.OrdinalIgnoreCase)));
					if (machine is null)
					{
						return Refused($"{core.Name} has no machine called '{system}'. It has: "
							+ string.Join(", ", machines.Select(static m => m.DisplayName)) + ".");
					}
					answered[machineSetting] = machine.When is { Count: > 0 } ? machine.When[0] : machine.Id ?? "";
				}
				else if (!core.Systems.Any(s => string.Equals(s, system, StringComparison.OrdinalIgnoreCase)
					|| string.Equals(core.SystemNameOf(s), system, StringComparison.OrdinalIgnoreCase)))
				{
					return Refused($"{core.Name} is a {core.SystemsSpelled}, not a '{system}'.");
				}
			}

			// what is asked for must exist before anything is put anywhere
			foreach (var (_, path) in files.Concat(games.Select(static g => ("", g))).Concat(firmware.Select(static f => ("", f.Path))))
			{
				if (!File.Exists(path)) return Refused($"There is no file at '{path}'.");
			}

			// the answers go in as a reopened project's do, which is the wizard's own way in
			SeedFrom(ProjectAnswers.For(core.Name, core.Path, Newtonsoft.Json.JsonConvert.SerializeObject(answered), [ ], firmware));
			if (_declaration is null) return Refused(_status.Text is { Length: not 0 } why ? why : $"{core.Name}'s file slots cannot be read.");
			foreach (var (slot, path) in files.Concat(games.Select(g => (SlotForFile(g), g))))
			{
				if (_declaration.Slots.All(s => s.Id != slot))
				{
					return Refused($"{core.Name} has no slot '{slot}'. Its slots: {string.Join(", ", _declaration.Slots.Select(static s => s.Id))}.");
				}
				_status.Text = "";
				AddFileToSlot(slot, path);
				if (!SlotFileNames(slot).Contains(Path.GetFileName(path)))
				{
					return Refused(_status.Text is { Length: not 0 } why ? why : $"'{path}' was not taken into the slot '{slot}'.");
				}
			}
			RefreshExposedSettings();
			if (CardinalityComplaint() is { } complaint) return Refused(complaint);

			// the settings page, arrived at: the core's suggestions for the game, then the agent's own
			HashGameForPrecompileNow();
			if (!BuildSettingsPage()) return Refused(_status.Text);
			_seededFor = null;
			SuggestSettingsNow();
			var effective = WaterboxCore.EffectiveSettingsFor(_cfg, _settings);
			var declared = _cfg.SettingsFor(_cfg.MachineFor(effective)).Concat(_gameSettings)
				.Where(static d => d.Name is { Length: not 0 })
				.GroupBy(static d => d.Name!, StringComparer.Ordinal)
				.ToDictionary(static g => g.Key, static g => g.First(), StringComparer.Ordinal);
			foreach (var (name, value) in settings)
			{
				if (!declared.TryGetValue(name, out var decl))
				{
					return Refused($"{core.Name} has no setting called '{name}'. describe_core lists its settings.");
				}
				_settings!.Values[name] = decl.Coerce(value);
			}
			RefreshExposedSettings();

			// the firmware page: looked for where it looks, and what is missing said rather than asked for
			if (AnyFirmwareRequired())
			{
				BuildFirmwarePage();
				var missing = _firmwareNeeds.Where(static n => !n.Satisfied).ToList();
				if (missing.Count is not 0)
				{
					var decls = FirmwareDecls();
					return new()
					{
						Refusal = "Firmware the project needs was not found: "
							+ string.Join(", ", missing.Select(static n => n.Decl?.DisplayName ?? n.Id)) + ".",
						Missing = missing.Select(n => new MissingFirmware(n.Id, n.Decl?.DisplayName ?? n.Id,
							decls.Where(d => d.Id == n.Id).ToList())).ToList(),
						Searched = _firmwareSearchDirs.ToList(),
					};
				}
			}
			if (PrecompileApplies())
			{
				BuildPrecompilePage();
				if (!PrecompileSatisfied())
				{
					return Refused($"{core.Name} compiles a game's code before the game's first run, which the New Project "
						+ "window does on its last page, and this game's has not been compiled yet. Make this game's first "
						+ "project there; the compiled code is kept for the next.");
				}
			}
			return MakeProject() is { } refused ? Refused(refused.Why) : new() { Made = true };
		}

		/// <summary>The words a machine may be named by: its id, its label, and its setting's values.</summary>
		private static IEnumerable<string> Names(WaterboxConfig.MachineConfig machine)
			=> new[] { machine.Id, machine.DisplayName }
				.Concat(machine.When ?? [ ])
				.Where(static n => n is { Length: not 0 })!;
	}
}
