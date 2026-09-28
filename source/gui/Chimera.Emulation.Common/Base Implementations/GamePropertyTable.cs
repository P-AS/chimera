#nullable enable

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;

using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace Chimera.Emulation.Common
{
	/// <summary>
	/// A game core's property table (docs/game-cores.md), read from the JSON its
	/// GetGameProperties export gives and checked against the domains the core really
	/// has. A property the frontend cannot use - a domain that is not there, an offset
	/// past its end, a type it does not know, a name already taken - is left out and
	/// said in <see cref="Problems"/>, rather than refusing the core: the other
	/// properties are still worth having, and a bad table is the core's bug to fix.
	/// </summary>
	public sealed class GamePropertyTable : IGameProperties
	{
		private readonly List<GameProperty> _properties = new();
		private readonly Dictionary<string, GameProperty> _byName = new(StringComparer.OrdinalIgnoreCase);
		private readonly Dictionary<(string Domain, long Offset), GameProperty> _byPlace = new();
		private readonly List<string> _problems = new();

		public IReadOnlyList<GameProperty> Properties => _properties;

		public IReadOnlyList<string> Problems => _problems;

		public GameProperty? this[string name] => _byName.TryGetValue(name, out var p) ? p : null;

		public GameProperty? At(string domain, long address)
			=> _byPlace.TryGetValue((domain, address), out var p) ? p : null;

		public GameProperty? Containing(string domain, long address)
		{
			// a property is at most four bytes, so it starts at most three before
			for (var start = address; start >= 0 && start > address - 4; start--)
			{
				if (_byPlace.TryGetValue((domain, start), out var p) && address < start + p.Size) return p;
			}
			return null;
		}

		/// <summary>
		/// Reads a table. <paramref name="domainSize"/> answers a domain's size by its
		/// name, or null when the core has no such domain. Text that is not a table at
		/// all is an empty one with the reason in <see cref="Problems"/>.
		/// </summary>
		public static GamePropertyTable Parse(string? json, Func<string, long?> domainSize)
		{
			GamePropertyTable table = new();
			if (string.IsNullOrWhiteSpace(json)) return table;
			JArray list;
			try
			{
				list = JObject.Parse(json!)["properties"] as JArray
					?? throw new JsonException("it has no \"properties\" list");
			}
			catch (JsonException ex)
			{
				table._problems.Add($"the property table is not readable: {ex.Message}");
				return table;
			}

			foreach (var entry in list)
			{
				if (entry is not JObject o)
				{
					table._problems.Add($"an entry that is not an object: {entry.ToString(Formatting.None)}");
					continue;
				}
				var name = (string?)o["name"] ?? "";
				var said = name.Length is 0 ? o.ToString(Formatting.None) : $"\"{name}\"";
				if (name.Trim().Length is 0)
				{
					table._problems.Add($"a property with no name: {said}");
					continue;
				}
				if (table._byName.ContainsKey(name))
				{
					table._problems.Add($"{said} is named twice; the second is left out");
					continue;
				}
				if (!TryType((string?)o["type"], out var type))
				{
					table._problems.Add($"{said} has a type this build does not read: \"{(string?)o["type"]}\"");
					continue;
				}
				var domain = (string?)o["domain"] ?? "";
				if (domainSize(domain) is not { } size)
				{
					table._problems.Add($"{said} is in \"{domain}\", which the core has no domain called");
					continue;
				}
				var offsetToken = o["offset"];
				if (offsetToken is null || offsetToken.Type is not JTokenType.Integer)
				{
					table._problems.Add($"{said} has no whole-number offset");
					continue;
				}
				var offset = (long)offsetToken;
				GameProperty property = new()
				{
					Name = name,
					Domain = domain,
					Offset = offset,
					Type = type,
					Group = (string?)o["group"] ?? "",
					Values = ReadValues(o["values"] as JObject),
					Writable = o["writable"]?.Type is not JTokenType.Boolean || (bool)o["writable"]!,
					Description = (string?)o["description"] ?? "",
				};
				if (offset < 0 || offset + property.Size > size)
				{
					table._problems.Add($"{said} at {offset} runs past the end of \"{domain}\" ({size} bytes)");
					continue;
				}
				table._properties.Add(property);
				table._byName[name] = property;
				// two properties may share a place (a byte and the word it opens); the
				// first listed names the address
				if (!table._byPlace.ContainsKey((domain, offset))) table._byPlace[(domain, offset)] = property;
			}
			return table;
		}

		private static bool TryType(string? text, out GamePropertyType type)
		{
			type = default;
			if (text is null) return false;
			switch (text.Trim().ToLowerInvariant())
			{
				case "u8": type = GamePropertyType.U8; return true;
				case "s8": type = GamePropertyType.S8; return true;
				case "u16": type = GamePropertyType.U16; return true;
				case "s16": type = GamePropertyType.S16; return true;
				case "u32": type = GamePropertyType.U32; return true;
				case "s32": type = GamePropertyType.S32; return true;
				case "f32": type = GamePropertyType.F32; return true;
				case "bool": type = GamePropertyType.Bool; return true;
				default: return false;
			}
		}

		private static IReadOnlyDictionary<long, string> ReadValues(JObject? values)
		{
			Dictionary<long, string> result = new();
			if (values is null) return result;
			foreach (var pair in values.Properties())
			{
				if (long.TryParse(pair.Name, NumberStyles.Integer, CultureInfo.InvariantCulture, out var key)
					&& pair.Value.Type is JTokenType.String)
				{
					result[key] = (string)pair.Value!;
				}
			}
			return result;
		}

		/// <summary>
		/// The property's value as a number: an integer type exactly, sign-extended where
		/// signed; <c>f32</c> as the float it holds; <c>bool</c> as 0 or 1 (any other byte
		/// counts as 1, as the game would read it).
		/// </summary>
		public static double Read(GameProperty p, MemoryDomain domain)
		{
			switch (p.Type)
			{
				case GamePropertyType.U8: return domain.PeekByte(p.Offset);
				case GamePropertyType.S8: return (sbyte)domain.PeekByte(p.Offset);
				case GamePropertyType.Bool: return domain.PeekByte(p.Offset) is 0 ? 0 : 1;
				case GamePropertyType.U16: return domain.PeekUshort(p.Offset, bigEndian: false);
				case GamePropertyType.S16: return (short)domain.PeekUshort(p.Offset, bigEndian: false);
				case GamePropertyType.U32: return domain.PeekUint(p.Offset, bigEndian: false);
				case GamePropertyType.S32: return (int)domain.PeekUint(p.Offset, bigEndian: false);
				case GamePropertyType.F32: return BitConverter.ToSingle(BitConverter.GetBytes(domain.PeekUint(p.Offset, bigEndian: false)), 0);
				default: throw new ArgumentOutOfRangeException(nameof(p));
			}
		}

		/// <summary>
		/// Writes a value, as the next step of the game will see it. A whole-number type
		/// takes the value truncated toward zero and wrapped to its width, as a poke of
		/// that width would; <c>bool</c> stores 1 for anything but 0.
		/// </summary>
		public static void Write(GameProperty p, MemoryDomain domain, double value)
		{
			var whole = double.IsNaN(value) ? 0 : (long)Math.Truncate(Math.Max(long.MinValue, Math.Min(long.MaxValue, value)));
			switch (p.Type)
			{
				case GamePropertyType.U8 or GamePropertyType.S8: domain.PokeByte(p.Offset, unchecked((byte)whole)); break;
				case GamePropertyType.Bool: domain.PokeByte(p.Offset, value is 0 ? (byte)0 : (byte)1); break;
				case GamePropertyType.U16 or GamePropertyType.S16: domain.PokeUshort(p.Offset, unchecked((ushort)whole), bigEndian: false); break;
				case GamePropertyType.U32 or GamePropertyType.S32: domain.PokeUint(p.Offset, unchecked((uint)whole), bigEndian: false); break;
				case GamePropertyType.F32: domain.PokeUint(p.Offset, BitConverter.ToUInt32(BitConverter.GetBytes((float)value), 0), bigEndian: false); break;
				default: throw new ArgumentOutOfRangeException(nameof(p));
			}
		}

		/// <summary>A value as a person reads it: an enumeration's name where it has one, else the number.</summary>
		public static string Format(GameProperty p, double value)
		{
			if (p.Type is not GamePropertyType.F32 && p.Values.TryGetValue((long)value, out var label)) return label;
			return p.Type switch
			{
				GamePropertyType.F32 => value.ToString("R", CultureInfo.InvariantCulture),
				GamePropertyType.Bool => value is 0 ? "false" : "true",
				_ => ((long)value).ToString(CultureInfo.InvariantCulture),
			};
		}
	}
}
