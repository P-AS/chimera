#nullable enable

using System;
using System.Collections.Generic;
using System.Linq;

using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Engine;
using Chimera.Emulation.Common.Waterbox;

using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace Chimera.Client.Common
{
	/// <summary>
	/// Which values of a setting have their firmware on hand (issue #293). A setting whose values call for
	/// different files - a PlayStation 2's Boot ROM, one BIOS per value - is listed with the values whose
	/// file was found first, each marked; what the setting means does not change. The engine's own decision
	/// tree (ce_firmware_evaluate) is asked, one value at a time, so a core's conditions are read the way a
	/// project's are and no core is known here by name.
	/// </summary>
	public static class FirmwareChoices
	{
		/// <param name="rawFirmwareJson">the package's "firmware" array as written (the engine's input)</param>
		/// <param name="firmware">the same entries, typed and in the same order: an index the engine answers with counts into this</param>
		/// <param name="slotsJson">the files chosen so far, by slot</param>
		/// <param name="effective">every setting as it stands, defaults filled in</param>
		/// <param name="settings">the settings on offer</param>
		/// <param name="index">the files hashed so far: the Firmware folder and every path remembered for the core</param>
		/// <returns>
		/// setting name -&gt; value -&gt; whether that value's own files are all on hand, for the settings whose
		/// values call for different files. A value that calls for no file of its own is absent; so is a
		/// setting that decides no file.
		/// </returns>
		public static Dictionary<string, Dictionary<string, bool>> OnHand(
			string rawFirmwareJson,
			IReadOnlyList<CoreFirmwareDecl> firmware,
			string slotsJson,
			IReadOnlyDictionary<string, object> effective,
			IEnumerable<WaterboxConfig.SettingDecl> settings,
			IReadOnlyList<FirmwareLocator.IndexedFile> index)
		{
			Dictionary<string, Dictionary<string, bool>> table = new(StringComparer.Ordinal);
			var named = SettingsNamedIn(rawFirmwareJson);
			if (named.Count is 0) return table;
			foreach (var decl in settings)
			{
				if (decl.Name is not { } name || !named.Contains(name)) continue;
				if (decl.EffectiveType is not "enum" || decl.Options is not { Count: > 1 } options) continue;
				var asked = options.Distinct(StringComparer.Ordinal).Select(value =>
				{
					Dictionary<string, object> with = new(effective.ToDictionary(static kv => kv.Key, static kv => kv.Value)) { [name] = value };
					var needs = EngineFirmware.Evaluate(rawFirmwareJson, slotsJson, JsonConvert.SerializeObject(with));
					return (Value: value, Needs: new HashSet<(string, int)>(needs));
				}).ToList();
				// what every value calls for is not this setting's doing
				HashSet<(string, int)> common = new(asked[0].Needs);
				foreach (var (_, needs) in asked.Skip(1)) common.IntersectWith(needs);
				Dictionary<string, bool> values = new(StringComparer.Ordinal);
				foreach (var (value, needs) in asked)
				{
					var own = needs.Where(need => !common.Contains(need)).ToList();
					if (own.Count is 0) continue;
					values[value] = own.All(need => DeclFor(firmware, need) is { } d && FirmwareLocator.FindEither(d, index) is not null);
				}
				if (values.Count is not 0) table[name] = values;
			}
			return table;
		}

		/// <summary>the entry the engine meant: by index when that is the same id, else the first of that id</summary>
		private static CoreFirmwareDecl? DeclFor(IReadOnlyList<CoreFirmwareDecl> firmware, (string Id, int Index) need)
			=> need.Index >= 0 && need.Index < firmware.Count && firmware[need.Index].Id == need.Id
				? firmware[need.Index]
				: firmware.FirstOrDefault(d => d.Id == need.Id);

		/// <summary>
		/// The settings a firmware condition names anywhere ("requiredWhen", nested in "all", "any" and
		/// "not"). Only these can decide a file, so only these are asked about.
		/// </summary>
		public static HashSet<string> SettingsNamedIn(string rawFirmwareJson)
		{
			HashSet<string> named = new(StringComparer.Ordinal);
			JArray entries;
			try
			{
				entries = JArray.Parse(string.IsNullOrWhiteSpace(rawFirmwareJson) ? "[]" : rawFirmwareJson);
			}
			catch (JsonException)
			{
				return named;
			}
			foreach (var entry in entries.OfType<JObject>())
			{
				if (entry["requiredWhen"] is { } when) Collect(when, named);
			}
			return named;
		}

		private static void Collect(JToken token, HashSet<string> named)
		{
			switch (token)
			{
				case JObject obj:
					if (obj["setting"] is JValue { Type: JTokenType.String } setting) named.Add((string) setting!);
					foreach (var property in obj.Properties()) Collect(property.Value, named);
					break;
				case JArray array:
					foreach (var item in array) Collect(item, named);
					break;
			}
		}
	}
}
