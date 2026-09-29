using System.Collections.Generic;
using System.Linq;

using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// A game core's properties without an engine, for the tools' own tests: the table is
	/// the engine's description of one (what <see cref="EngineGameProperties.Describe"/>
	/// reads), and values are text kept per element name. What the bytes mean is the
	/// engine's, tested there (source/engine/tests/test_game_properties.cpp); these tests
	/// are about what the tools do with a property, not what it holds.
	/// </summary>
	/// <remarks>Linked into Chimera.Tests.Client.GUI as well.</remarks>
	internal sealed class FakeGameProperties : IGameProperties
	{
		public IReadOnlyList<GameProperty> Properties { get; }

		public IReadOnlyList<string> Problems { get; }

		/// <summary>Each element's value as text, by name; an element not here reads as "0".</summary>
		public Dictionary<string, string> Values { get; } = new();

		/// <summary>Every text set, in order: element name and text.</summary>
		public List<(string Name, string Text)> Sets { get; } = new();

		/// <summary>Elements that refuse to be set, and why.</summary>
		public Dictionary<string, string> Refusals { get; } = new();

		public FakeGameProperties(string engineTableJson)
			=> (Properties, Problems) = EngineGameProperties.Describe(engineTableJson);

		public GamePropertyElement? Find(string name)
			=> Properties.SelectMany(static p => p.Elements).FirstOrDefault(e => string.Equals(e.Name, name, System.StringComparison.OrdinalIgnoreCase))
				?? Properties.FirstOrDefault(p => string.Equals(p.Name, name, System.StringComparison.OrdinalIgnoreCase))?.Element(0);

		public GamePropertyElement? At(string domain, long address, out bool starts)
		{
			foreach (var element in Properties.Where(p => p.Domain == domain).SelectMany(static p => p.Elements))
			{
				if (address >= element.Offset && address < element.Offset + element.Property.Size)
				{
					starts = address == element.Offset;
					return element;
				}
			}
			starts = false;
			return null;
		}

		public string Text(GamePropertyElement element, bool named = true)
			=> Values.TryGetValue(element.Name, out var text) ? text : "0";

		public string? SetText(GamePropertyElement element, string text)
		{
			if (Refusals.TryGetValue(element.Name, out var why)) return why;
			Sets.Add((element.Name, text));
			Values[element.Name] = text;
			return null;
		}

		public object? Get(GamePropertyElement element)
			=> long.TryParse(Text(element), out var n) ? n : Text(element);

		public string? Set(GamePropertyElement element, object value) => SetText(element, value.ToString() ?? "");

		/// <summary>What the game's timer says; null for a core without one.</summary>
		public long? GameTimeMs { get; set; }
	}
}
