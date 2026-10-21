#nullable enable

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

using Chimera.Client.Common;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Client.Common.CorePackages
{
	/// <summary>
	/// The frontend against real, published core packages.
	///
	/// Chimera used to build every core it shipped, so a change that broke the
	/// generic waterbox adapter could not get past its own build. Now the cores are
	/// published separately (docs/core-manager.md) and nothing in Chimera's CI
	/// compiles one - which would leave a frontend change free to break every core
	/// at once, unseen until somebody downloaded it.
	///
	/// So CI fills build/Cores with what each core last published
	/// (<c>tools/fetch-cores.sh</c>) and these run against it. They need no rom, no
	/// firmware and no emulation: what they check is the CONTRACT - that a package
	/// this frontend will be asked to open can be read, understood, and turned into
	/// a working factory. Whether the emulation is still right is each core
	/// repository's own gate, which runs against Chimera's main.
	/// </summary>
	[TestClass]
	public class InstalledCorePackagesTests
	{
		private static IReadOnlyList<string> Packages()
		{
			var files = InstalledPackages.Files;
			if (files.Count is 0)
			{
				Assert.Inconclusive($"no core packages in {InstalledPackages.Directory} (see tools/fetch-cores.sh)");
			}
			return files;
		}

		[TestMethod]
		public void EveryPackageIsReadable()
		{
			List<string> broken = new();
			foreach (var path in Packages())
			{
				var peeked = CorePackageDiscovery.Peek(path);
				if (peeked is null) broken.Add($"{InstalledPackages.NameOf(path)}: not recognised as a package at all");
				else if (peeked.Error is not null) broken.Add($"{InstalledPackages.NameOf(path)}: {peeked.Error}");
			}
			Assert.AreEqual(0, broken.Count, string.Join("; ", broken));
		}

		[TestMethod]
		public void EveryPackageIsBuiltForAnAbiThisFrontendRuns()
		{
			// the check that exists because cores and frontend are versioned apart:
			// if this fails, either GuestAbi.Current moved without the cores being
			// rebuilt, or MinimumSupported was raised and retired them
			var refused = Packages()
				.Select(CorePackageDiscovery.Peek)
				.Where(static p => p?.IsAbiIncompatible is true)
				.Select(static p => $"{p!.Name}: {p.Error}")
				.ToList();
			Assert.AreEqual(0, refused.Count, string.Join("; ", refused));
		}

		/// <summary>
		/// The declaration turned into the thing that runs it. The factory's
		/// constructor is where a package's machines, settings and controller are
		/// validated against each other, so this is the real test of whether the
		/// frontend still understands what the cores are saying.
		/// </summary>
		[TestMethod]
		public void EveryPackageBecomesAFactory()
		{
			List<string> failed = new();
			foreach (var path in Packages())
			{
				var dir = Extracted(path);
				if (dir is null) { failed.Add($"{InstalledPackages.NameOf(path)}: could not be opened"); continue; }
				try
				{
					if (!WaterboxCoreFactory.IsWaterboxPackage(dir))
					{
						failed.Add($"{InstalledPackages.NameOf(path)}: no core.wbx and waterbox.config");
						continue;
					}
					var factory = new WaterboxCoreFactory(dir);
					if (string.IsNullOrWhiteSpace(factory.CoreName)) failed.Add($"{InstalledPackages.NameOf(path)}: names no core");
					if (factory.SystemIds.Count is 0) failed.Add($"{InstalledPackages.NameOf(path)}: claims no machine");
				}
				catch (Exception ex)
				{
					failed.Add($"{InstalledPackages.NameOf(path)}: {ex.Message}");
				}
				finally
				{
					TryDelete(dir);
				}
			}
			Assert.AreEqual(0, failed.Count, string.Join("; ", failed));
		}

		/// <summary>
		/// A package declares the bindings its controller ships with, and the
		/// frontend has none of its own. A binding for a control the controller does
		/// not declare is a binding nothing can ever use. Judged on each core's
		/// newest package: an older version kept for old projects is what it was.
		/// </summary>
		[TestMethod]
		public void KeybindsOnlyNameControlsTheControllerHas()
		{
			List<string> stray = new();
			Packages();
			foreach (var path in InstalledPackages.Newest)
			{
				var dir = Extracted(path);
				if (dir is null) continue;
				try
				{
					var keybinds = PackageKeybinds.Read(dir);
					if (keybinds is null) continue;
					var config = WaterboxConfig.FromJson(File.ReadAllText(Path.Combine(dir, WaterboxCoreFactory.ConfigFileName)));
					// Compared by the exact name, as InputManager binds them: a control
					// declared "P1 A" is bound only by a binding called "P1 A". This
					// test used to drop the player prefix on both sides, on the belief
					// that the frontend adds it - it does not - and so passed a Dolphin
					// package none of whose 75 bindings ever applied (chimera#284).
					HashSet<string> buttons = new(AllButtons(config), StringComparer.Ordinal);
					HashSet<string> axes = new(AllAxes(config), StringComparer.Ordinal);
					if (buttons.Count is 0 && axes.Count is 0) continue;
					foreach (var (controller, bindings) in keybinds.AllTrollers)
					{
						foreach (var button in bindings.Keys)
						{
							if (!buttons.Contains(button))
							{
								stray.Add($"{InstalledPackages.NameOf(path)}/{controller}: {button}");
							}
						}
					}
					foreach (var (controller, bindings) in keybinds.AllTrollersAnalog ?? new())
					{
						foreach (var axis in bindings.Keys)
						{
							if (!axes.Contains(axis))
							{
								stray.Add($"{InstalledPackages.NameOf(path)}/{controller}: {axis} (an axis)");
							}
						}
					}
				}
				catch (Exception ex)
				{
					stray.Add($"{InstalledPackages.NameOf(path)}: {ex.Message}");
				}
				finally
				{
					TryDelete(dir);
				}
			}
			Assert.AreEqual(0, stray.Count, string.Join("; ", stray));
		}

		/// <summary>
		/// The names a game controller's inputs arrive by (SDL2Gamepad: the buttons, and the axes of GetAxes, which
		/// SDL2InputAdapter sends as "X1 LeftThumbX Axis"). A joystick that is not a game controller ("J1 ...") has
		/// the ones in <see cref="JoystickButton"/> and <see cref="JoystickAxis"/>.
		/// </summary>
		private static readonly HashSet<string> ControllerButtons = new(StringComparer.Ordinal)
		{
			"A", "B", "X", "Y", "Back", "Guide", "Start", "LeftThumb", "RightThumb", "LeftShoulder", "RightShoulder",
			"DpadUp", "DpadDown", "DpadLeft", "DpadRight", "Misc", "Paddle1", "Paddle2", "Paddle3", "Paddle4", "Touchpad",
			"LStickUp", "LStickDown", "LStickLeft", "LStickRight", "RStickUp", "RStickDown", "RStickLeft", "RStickRight",
			"LeftTrigger", "RightTrigger",
		};

		private static readonly HashSet<string> ControllerAxes = new(StringComparer.Ordinal)
		{
			"LeftThumbX", "LeftThumbY", "RightThumbX", "RightThumbY", "LeftTrigger", "RightTrigger", "TouchpadX", "TouchpadY",
		};

		private static readonly Regex JoystickButton = new(@"^([XYZWVSQPN][+-]|Axis\d+[+-]|B\d+|POV\d+[UDLR])$");

		private static readonly Regex JoystickAxis = new(@"^([XYZWVSQPN]|Axis\d+) Axis$");

		/// <summary>
		/// A default binding to a gamepad names an input Chimera sends. The ares package bound its sticks to
		/// "X1 LStickX", which nothing sends ("X1 LeftThumbX Axis" is the stick), so a Nintendo 64's stick did nothing
		/// until bound by hand (chimera#314); RPCS3's did the same, and three cores bound L3 and R3 to "LeftStick" and
		/// "RightStick" (the stick buttons are LeftThumb and RightThumb). The key-name checks looked only at the keyboard.
		/// </summary>
		[TestMethod]
		public void KeybindsNameGamepadInputsChimeraSends()
		{
			List<string> unknown = new();
			Packages();
			foreach (var path in InstalledPackages.Newest)
			{
				var dir = Extracted(path);
				if (dir is null) continue;
				try
				{
					var keybinds = PackageKeybinds.Read(dir);
					if (keybinds is null) continue;
					var name = InstalledPackages.NameOf(path);
					foreach (var (controller, bindings) in keybinds.AllTrollers)
					{
						foreach (var (button, value) in bindings)
						{
							foreach (var part in (value ?? "").Split(',', '+').Select(static p => p.Trim()))
							{
								var m = Regex.Match(part, @"^([XJ])\d+ (.+)$");
								if (m.Success && !(m.Groups[1].Value is "X" ? ControllerButtons.Contains(m.Groups[2].Value) : JoystickButton.IsMatch(m.Groups[2].Value)))
								{
									unknown.Add($"{name}/{controller}/{button}: {part}");
								}
							}
						}
					}
					foreach (var (controller, bindings) in keybinds.AllTrollersAnalog ?? new())
					{
						foreach (var (axis, bind) in bindings)
						{
							foreach (var part in (bind.Value ?? "").Split(',').Select(static p => p.Trim()))
							{
								var m = Regex.Match(part, @"^([XJ])\d+ (.+)$");
								if (!m.Success) continue;
								var known = m.Groups[1].Value is "X"
									? m.Groups[2].Value.EndsWith(" Axis", StringComparison.Ordinal)
										&& ControllerAxes.Contains(m.Groups[2].Value.Substring(0, m.Groups[2].Value.Length - " Axis".Length))
									: JoystickAxis.IsMatch(m.Groups[2].Value);
								if (!known)
								{
									unknown.Add($"{name}/{controller}/{axis}: {part} (an axis)");
								}
							}
						}
					}
				}
				catch (Exception ex)
				{
					unknown.Add($"{InstalledPackages.NameOf(path)}: {ex.Message}");
				}
				finally
				{
					TryDelete(dir);
				}
			}
			Assert.AreEqual(0, unknown.Count, "gamepad inputs Chimera does not send: " + string.Join("; ", unknown));
		}

		/// <summary>
		/// A package that ships a picture of its controller (issue 338) ships it: the picture an
		/// input declaration names is a PNG inside the package, and the package's licence list names
		/// it, so where it came from and on what terms travels with it.
		/// </summary>
		[TestMethod]
		public void APictureOfTheControllerIsAPngThePackageLicenses()
		{
			List<string> wrong = new();
			Packages();
			foreach (var path in InstalledPackages.Newest)
			{
				var dir = Extracted(path);
				if (dir is null) continue;
				try
				{
					var configPath = Path.Combine(dir, "waterbox.config");
					if (!File.Exists(configPath) || WaterboxConfig.FromJson(File.ReadAllText(configPath)) is not { } cfg) continue;
					var inputs = new[] { cfg.Input }.Concat((cfg.Machines ?? new()).Select(static m => m.Input)).Where(static i => i?.Picture is { Length: > 0 });
					var licences = Path.Combine(dir, "licenses", "licenses.json");
					var licenceText = File.Exists(licences) ? File.ReadAllText(licences) : "";
					foreach (var input in inputs)
					{
						var picture = input!.Picture!;
						var name = InstalledPackages.NameOf(path);
						var file = WaterboxCore.PictureIn(dir, picture);
						if (file is null)
						{
							wrong.Add($"{name}: {picture} is not in the package");
							continue;
						}
						var head = File.ReadAllBytes(file).Take(8).ToArray();
						if (!head.SequenceEqual(new byte[] { 0x89, (byte) 'P', (byte) 'N', (byte) 'G', 0x0D, 0x0A, 0x1A, 0x0A }))
						{
							wrong.Add($"{name}: {picture} is not a PNG");
						}
						if (!licenceText.Contains(Path.GetFileName(picture)))
						{
							wrong.Add($"{name}: licenses/licenses.json does not name {Path.GetFileName(picture)}");
						}
					}
				}
				catch (Exception ex)
				{
					wrong.Add($"{InstalledPackages.NameOf(path)}: {ex.Message}");
				}
				finally
				{
					TryDelete(dir);
				}
			}
			Assert.AreEqual(0, wrong.Count, string.Join("; ", wrong));
		}

		/// <summary>
		/// A default binding to a joystick's hat names its first hat. Chimera numbers a joystick's hats from 0
		/// (SDL2Gamepad: "POV0U"), so "POV1U" is a second hat, which a stick with one does not have. 33 cores bound
		/// their directions to POV1 and none to POV0, so a joystick that is not a game controller moved nothing
		/// until bound by hand (found with chimera#336). A package that binds a later hat binds the first too.
		/// </summary>
		[TestMethod]
		public void KeybindsNameAJoysticksFirstHat()
		{
			List<string> wrong = new();
			Packages();
			foreach (var path in InstalledPackages.Newest)
			{
				var dir = Extracted(path);
				if (dir is null) continue;
				try
				{
					var keybinds = PackageKeybinds.Read(dir);
					if (keybinds is null) continue;
					var hats = keybinds.AllTrollers
						.SelectMany(static c => c.Value.Values)
						.SelectMany(static v => (v ?? "").Split(',', '+').Select(static p => p.Trim()))
						.Select(static p => Regex.Match(p, @"^J\d+ POV(\d+)[UDLR]$"))
						.Where(static m => m.Success)
						.Select(static m => int.Parse(m.Groups[1].Value))
						.ToHashSet();
					if (hats.Count is not 0 && !hats.Contains(0))
					{
						wrong.Add($"{InstalledPackages.NameOf(path)} binds POV{string.Join("/POV", hats.OrderBy(static h => h))} and not POV0");
					}
				}
				catch (Exception ex)
				{
					wrong.Add($"{InstalledPackages.NameOf(path)}: {ex.Message}");
				}
				finally
				{
					TryDelete(dir);
				}
			}
			Assert.AreEqual(0, wrong.Count, "joystick hats past the first, with the first unbound: " + string.Join("; ", wrong));
		}

		/// <summary>
		/// The names the keyboard and the mouse arrive by: each key as Input hands it to the bindings
		/// (<see cref="DistinctKeyNameOverrides.GetName"/>), Shift, Ctrl and Alt for either key of each (the
		/// default, Config.MergeLAndRModifierKeys), and the mouse's buttons.
		/// </summary>
		private static HashSet<string> KeyAndMouseButtons()
		{
			HashSet<string> names = new(StringComparer.Ordinal) { "Shift", "Ctrl", "Alt", "WMouse L", "WMouse M", "WMouse R", "WMouse 1", "WMouse 2" };
			foreach (Chimera.Input.DistinctKey k in Enum.GetValues(typeof(Chimera.Input.DistinctKey))) names.Add(DistinctKeyNameOverrides.GetName(k));
			return names;
		}

		/// <summary>The mouse's axes: its place in the window and its movement.</summary>
		private static readonly HashSet<string> MouseAxes = new(StringComparer.Ordinal) { "WMouse X", "WMouse Y", "RMouse X", "RMouse Y" };

		/// <summary>On a default setup a left or right modifier key arrives under the merged name only.</summary>
		private static readonly Dictionary<string, string> MergedModifiers = new(StringComparer.Ordinal)
		{
			["LeftShift"] = "Shift", ["RightShift"] = "Shift", ["LeftCtrl"] = "Ctrl", ["RightCtrl"] = "Ctrl", ["LeftAlt"] = "Alt", ["RightAlt"] = "Alt",
		};

		/// <summary>
		/// A default binding to a key or the mouse names one Chimera sends, and an axis is bound to an axis. Sixteen
		/// cores once shipped keys by names nothing sends ("D1", "Return", "Multiply"), found by a script in four core
		/// repositories; this holds every package to it (chimera#317). A panel that binds LeftShift must bind Shift
		/// too, as a default setup sends only Shift.
		/// </summary>
		[TestMethod]
		public void KeybindsNameKeysAndMouseInputsChimeraSends()
		{
			var keys = KeyAndMouseButtons();
			List<string> unknown = new();
			Packages();
			foreach (var path in InstalledPackages.Newest)
			{
				var dir = Extracted(path);
				if (dir is null) continue;
				try
				{
					var keybinds = PackageKeybinds.Read(dir);
					if (keybinds is null) continue;
					var name = InstalledPackages.NameOf(path);
					foreach (var (controller, bindings) in keybinds.AllTrollers)
					{
						HashSet<string> bound = new(StringComparer.Ordinal);
						foreach (var (button, value) in bindings)
						{
							foreach (var part in (value ?? "").Split(',', '+').Select(static p => p.Trim()).Where(static p => p.Length is not 0))
							{
								bound.Add(part);
								if (Regex.IsMatch(part, @"^[XJ]\d+ ")) continue; // a gamepad's: KeybindsNameGamepadInputsChimeraSends
								if (!keys.Contains(part)) unknown.Add($"{name}/{controller}/{button}: {part}");
							}
						}
						foreach (var (side, merged) in MergedModifiers)
						{
							if (bound.Contains(side) && !bound.Contains(merged)) unknown.Add($"{name}/{controller}: {side} is bound but not {merged}, the only one a default setup sends");
						}
					}
					foreach (var (controller, bindings) in keybinds.AllTrollersAnalog ?? new())
					{
						foreach (var (axis, bind) in bindings)
						{
							foreach (var part in (bind.Value ?? "").Split(',').Select(static p => p.Trim()).Where(static p => p.Length is not 0))
							{
								if (Regex.IsMatch(part, @"^[XJ]\d+ ")) continue;
								if (!MouseAxes.Contains(part)) unknown.Add($"{name}/{controller}/{axis}: {part} (an axis bound to what is not one)");
							}
						}
					}
				}
				catch (Exception ex)
				{
					unknown.Add($"{InstalledPackages.NameOf(path)}: {ex.Message}");
				}
				finally
				{
					TryDelete(dir);
				}
			}
			Assert.AreEqual(0, unknown.Count, "keys and mouse inputs Chimera does not send: " + string.Join("; ", unknown));
		}

		/// <summary>
		/// What the manager will call each package in the store, checked against what
		/// the package says it is. A published package with no version cannot be
		/// filed, cited by a movie, or told apart from the next one.
		/// </summary>
		[TestMethod]
		public void EveryPackageStampsAVersion()
		{
			var unversioned = Packages()
				.Select(CorePackageDiscovery.Peek)
				.Where(static p => p is { Error: null } && p.Version.Length is 0)
				.Select(static p => p!.Name)
				.ToList();
			Assert.AreEqual(0, unversioned.Count, string.Join("; ", unversioned));
		}

		/// <summary>
		/// A package of several machines may declare one setting twice, once for
		/// each kind of machine (a console's boot ROM and an arcade board's). A
		/// machine's defaults come from the declarations that machine has, and of
		/// two with the same name the later one wins. So every machine must be
		/// left with ONE declaration of a name, which means the two say which
		/// machines they belong to in their "when" lists. "exposedWhen" does not
		/// do that: it decides what a person is shown, not what applies.
		/// The PCSX2 package told its two boot ROM settings apart by "exposedWhen"
		/// only (2026-09-20 to 2026-10-10): a PlayStation 2 project that did not
		/// name its boot ROM got the arcade board's default, was asked for no
		/// bios, and could not start.
		/// </summary>
		[TestMethod]
		public void ASettingHasOneDeclarationOnEveryMachine()
		{
			List<string> twice = new();
			foreach (var path in Packages())
			{
				var dir = Extracted(path);
				if (dir is null) continue;
				try
				{
					var config = WaterboxConfig.FromJson(File.ReadAllText(Path.Combine(dir, WaterboxCoreFactory.ConfigFileName)));
					if (config is null) continue;
					twice.AddRange(SettingsDeclaredTwice(config).Select(found => $"{InstalledPackages.NameOf(path)}: {found}"));
				}
				catch (Exception ex)
				{
					twice.Add($"{InstalledPackages.NameOf(path)}: {ex.Message}");
				}
				finally
				{
					TryDelete(dir);
				}
			}
			Assert.AreEqual(0, twice.Count, string.Join("; ", twice));
		}

		/// <summary>
		/// A sound rate a package or one of its machines states is a rate a sound
		/// chip can have. The adapter builds a resampler from it; a typing slip
		/// (4800, 480000) would otherwise be played as it stands (issue #226 is
		/// where a machine came to state its own).
		/// </summary>
		[TestMethod]
		public void EverySoundRateAPackageStatesIsARealOne()
		{
			List<string> wrong = new();
			foreach (var path in Packages())
			{
				var dir = Extracted(path);
				if (dir is null) continue;
				try
				{
					var config = WaterboxConfig.FromJson(File.ReadAllText(Path.Combine(dir, WaterboxCoreFactory.ConfigFileName)));
					if (config is null) continue;
					wrong.AddRange(SoundRatesOutOfRange(config).Select(found => $"{InstalledPackages.NameOf(path)}: {found}"));
				}
				catch (Exception ex)
				{
					wrong.Add($"{InstalledPackages.NameOf(path)}: {ex.Message}");
				}
				finally
				{
					TryDelete(dir);
				}
			}
			Assert.AreEqual(0, wrong.Count, string.Join("; ", wrong));
		}

		/// <summary>The rates a declaration states that no sound chip has: below 8 kHz or above 192 kHz.</summary>
		private static List<string> SoundRatesOutOfRange(WaterboxConfig config)
		{
			static bool Real(int rate) => rate is >= 8000 and <= 192000;
			List<string> wrong = new();
			if (config.Audio is { } audio && !Real(audio.Rate)) wrong.Add($"audio.rate is {audio.Rate}");
			foreach (var machine in config.Machines ?? new())
			{
				if (machine.AudioRate is { } rate && !Real(rate)) wrong.Add($"machine {machine.Id}: audioRate is {rate}");
			}
			return wrong;
		}

		/// <summary>The check above, shown to find what it looks for. Needs no package.</summary>
		[TestMethod]
		public void ASoundRateNoChipHasIsFound()
		{
			var config = WaterboxConfig.FromJson("""
				{
				  "coreName": "rates", "systemId": "SYS",
				  "video": { "width": 320, "height": 240 },
				  "audio": { "samplesPerFrame": 2048, "rate": 48000 },
				  "input": { "buttons": [] },
				  "machines": [
				    { "id": "A", "when": [ "a" ], "audioRate": 64000 },
				    { "id": "B", "when": [ "b" ], "audioRate": 6400 },
				    { "id": "C", "when": [ "c" ] }
				  ]
				}
				""");
			Assert.IsNotNull(config);
			CollectionAssert.AreEqual(new[] { "machine B: audioRate is 6400" }, SoundRatesOutOfRange(config));
		}

		/// <summary>
		/// The check above, shown to find what it looks for. Needs no package.
		/// The first declaration set is the PCSX2 package's mistake in small; the
		/// second is the same with each declaration naming its machines.
		/// </summary>
		[TestMethod]
		public void TwoDeclarationsOfOneSettingAreToldApartByTheirMachines()
		{
			const string Head = """
				{ "machineSetting": "machine",
				  "machines": [ { "id": "HOME", "when": [ "home" ] }, { "id": "ARCADE", "when": [ "arcade" ] } ],
				  "settings": [
				    { "name": "machine", "type": "enum", "options": [ "home", "arcade" ], "default": "home" },
				""";
			var shownOnly = WaterboxConfig.FromJson(Head + """
				    { "name": "bios", "type": "enum", "options": [ "home-rom" ], "default": "home-rom",
				      "exposedWhen": { "setting": "machine", "is": "home" } },
				    { "name": "bios", "type": "enum", "options": [ "arcade-rom" ], "default": "arcade-rom",
				      "exposedWhen": { "setting": "machine", "is": "arcade" } } ] }
				""")!;
			var found = SettingsDeclaredTwice(shownOnly).ToList();
			Assert.AreEqual(2, found.Count, string.Join("; ", found));
			StringAssert.Contains(found[0], "'bios'");
			// and this is what it costs: the home machine starts on the arcade rom
			Assert.AreEqual("arcade-rom", WaterboxCore.EffectiveSettingsFor(shownOnly, null)["bios"]);

			var byMachine = WaterboxConfig.FromJson(Head + """
				    { "name": "bios", "type": "enum", "options": [ "home-rom" ], "default": "home-rom",
				      "when": [ "home" ], "exposedWhen": { "setting": "machine", "is": "home" } },
				    { "name": "bios", "type": "enum", "options": [ "arcade-rom" ], "default": "arcade-rom",
				      "when": [ "arcade" ], "exposedWhen": { "setting": "machine", "is": "arcade" } } ] }
				""")!;
			Assert.AreEqual(0, SettingsDeclaredTwice(byMachine).Count());
			Assert.AreEqual("home-rom", WaterboxCore.EffectiveSettingsFor(byMachine, null)["bios"]);
		}

		/// <summary>
		/// A label names a value the setting has (issue #293). One that names no value is never shown,
		/// which is how a value renamed in "options" and not in "optionLabels" would lose its words
		/// without anyone being told.
		/// </summary>
		[TestMethod]
		public void EveryOptionLabelNamesAValueOfItsSetting()
		{
			List<string> stray = new();
			foreach (var path in Packages())
			{
				var dir = Extracted(path);
				if (dir is null) continue;
				try
				{
					var config = WaterboxConfig.FromJson(File.ReadAllText(Path.Combine(dir, WaterboxCoreFactory.ConfigFileName)));
					if (config is null) continue;
					stray.AddRange(LabelsOfNoValue(config).Select(found => $"{InstalledPackages.NameOf(path)}: {found}"));
				}
				catch (Exception ex)
				{
					stray.Add($"{InstalledPackages.NameOf(path)}: {ex.Message}");
				}
				finally
				{
					TryDelete(dir);
				}
			}
			Assert.AreEqual(0, stray.Count, string.Join("; ", stray));
		}

		[TestMethod]
		public void ALabelOfNoValueIsFound()
		{
			var config = WaterboxConfig.FromJson("""
				{ "settings": [
				    { "name": "bios", "type": "enum", "options": [ "a-rom", "b-rom" ], "default": "a-rom",
				      "optionLabels": { "a-rom": "USA", "c-rom": "Japan" } } ] }
				""")!;
			CollectionAssert.AreEqual(new[] { "'bios' labels 'c-rom', which is not one of its values" }, LabelsOfNoValue(config).ToArray());
			Assert.AreEqual("USA", config.Settings![0].LabelFor("a-rom"));
			Assert.AreEqual("b-rom", config.Settings![0].LabelFor("b-rom"), "a value with no label is shown as it is");
		}

		/// <summary>Each label, in every declaration, that names no value of its setting.</summary>
		private static IEnumerable<string> LabelsOfNoValue(WaterboxConfig config)
		{
			foreach (var decl in config.Settings ?? [ ])
			{
				foreach (var value in (decl.OptionLabels?.Keys ?? Enumerable.Empty<string>()).Where(v => decl.Options?.Contains(v) is not true))
				{
					yield return $"'{decl.Name}' labels '{value}', which is not one of its values";
				}
			}
		}

		/// <summary>Each setting a machine of this package is left with more than one declaration of.</summary>
		private static IEnumerable<string> SettingsDeclaredTwice(WaterboxConfig config)
		{
			// a package with no machines is one machine, and narrows nothing
			var machines = config.Machines is { Count: > 0 } several
				? several.Cast<WaterboxConfig.MachineConfig?>()
				: new WaterboxConfig.MachineConfig?[] { null };
			foreach (var machine in machines)
			{
				foreach (var group in config.SettingsFor(machine).GroupBy(static d => d.Key).Where(static g => g.Count() > 1))
				{
					yield return $"'{group.Key}' has {group.Count()} declarations on {machine?.Id ?? "its one machine"}";
				}
			}
		}

		private static IEnumerable<string> AllButtons(WaterboxConfig? config)
		{
			if (config?.Input?.Buttons is { } buttons) foreach (var b in buttons) yield return b;
			foreach (var machine in config?.Machines ?? new List<WaterboxConfig.MachineConfig>())
			{
				foreach (var b in machine.Input?.Buttons ?? new List<string>()) yield return b;
			}
		}

		private static IEnumerable<string> AllAxes(WaterboxConfig? config)
		{
			if (config?.Input?.Axes is { } axes) foreach (var a in axes) yield return a.Name ?? "";
			foreach (var machine in config?.Machines ?? new List<WaterboxConfig.MachineConfig>())
			{
				foreach (var a in machine.Input?.Axes ?? new List<WaterboxConfig.AxisConfig>()) yield return a.Name ?? "";
			}
		}

		/// <summary>Unpacks a package to a temp directory, or null if it cannot be read.</summary>
		private static string? Extracted(string packagePath)
		{
			var dir = Path.Combine(Path.GetTempPath(), $"chimera-pkg-{Guid.NewGuid():N}");
			try
			{
				System.IO.Compression.ZipFile.ExtractToDirectory(packagePath, dir);
				return dir;
			}
			catch (Exception)
			{
				TryDelete(dir);
				return null;
			}
		}

		private static void TryDelete(string dir)
		{
			try
			{
				if (System.IO.Directory.Exists(dir)) System.IO.Directory.Delete(dir, recursive: true);
			}
			catch (Exception)
			{
				// a leftover temp dir is not a test failure
			}
		}
	}
}
