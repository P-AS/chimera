#nullable enable

using System.Collections.Generic;

namespace Chimera.Emulation.Common
{
	/// <summary>How a game property's bytes read (docs/game-cores.md). Little-endian throughout.</summary>
	public enum GamePropertyType
	{
		U8,
		S8,
		U16,
		S16,
		U32,
		S32,
		F32,

		/// <summary>One byte, 0 or 1.</summary>
		Bool,
	}

	/// <summary>
	/// One of a game core's properties: a named place in one of its memory domains -
	/// the Kid's position, his hit points, the level - which every tool that watches,
	/// pokes and freezes an address works on by that name (docs/game-cores.md).
	/// </summary>
	public sealed class GameProperty
	{
		/// <summary>Unique within the core, and what watches, freezes and scripts keep - never the offset.</summary>
		public string Name { get; init; } = "";

		/// <summary>The memory domain it lives in.</summary>
		public string Domain { get; init; } = "";

		/// <summary>Where in the domain, in bytes.</summary>
		public long Offset { get; init; }

		public GamePropertyType Type { get; init; }

		/// <summary>For listing ("Kid", "Guard"); "" when the core gave none.</summary>
		public string Group { get; init; } = "";

		/// <summary>Names for an enumeration's values, shown instead of the number. Empty for a plain number.</summary>
		public IReadOnlyDictionary<long, string> Values { get; init; } = new Dictionary<long, string>();

		/// <summary>False for what the game works out afresh every step, which a poke could not change.</summary>
		public bool Writable { get; init; } = true;

		/// <summary>One line for a tooltip; "" when the core gave none.</summary>
		public string Description { get; init; } = "";

		/// <summary>How many bytes it takes.</summary>
		public int Size => Type switch
		{
			GamePropertyType.U8 or GamePropertyType.S8 or GamePropertyType.Bool => 1,
			GamePropertyType.U16 or GamePropertyType.S16 => 2,
			_ => 4,
		};

		public bool Signed => Type is GamePropertyType.S8 or GamePropertyType.S16 or GamePropertyType.S32;

		/// <summary>The type as the table spells it (<c>u16</c>).</summary>
		public string TypeName => Type.ToString().ToLowerInvariant();

		public override string ToString() => Name;
	}

	/// <summary>
	/// A game core's properties (docs/game-cores.md): what the core's property table
	/// says, checked against the domains it actually has. Offered only by a core whose
	/// table names at least one; the tools that name addresses ask for it optionally.
	/// </summary>
	public interface IGameProperties : ISpecializedEmulatorService
	{
		/// <summary>Every property, in the order the core listed them.</summary>
		IReadOnlyList<GameProperty> Properties { get; }

		/// <summary>The property by its name (not case), or null.</summary>
		GameProperty? this[string name] { get; }

		/// <summary>The property that starts at <paramref name="address"/> in <paramref name="domain"/>, or null.</summary>
		GameProperty? At(string domain, long address);

		/// <summary>The property whose bytes include <paramref name="address"/> in <paramref name="domain"/>, or null.</summary>
		GameProperty? Containing(string domain, long address);

		/// <summary>What in the core's table could not be used, one line each; empty for a sound table.</summary>
		IReadOnlyList<string> Problems { get; }
	}
}
