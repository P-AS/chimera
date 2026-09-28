using System.ComponentModel;
using System.Linq;

using Chimera.Emulation.Common;

using NLua;

// ReSharper disable UnusedMember.Global
// ReSharper disable UnusedAutoPropertyAccessor.Local
namespace Chimera.Client.Common
{
	[Description("A game core's properties by name (docs/game-cores.md): the Kid's position, the level, the random seed - what the core's property table names. The same bytes memory.* reads in the core's domains, found by name instead of address. An emulator core has none, and list() is empty.")]
	public sealed class GameLuaLibrary : LuaLibraryBase
	{
		[OptionalService]
		private IGameProperties Properties { get; set; }

		[OptionalService]
		private IMemoryDomains Domains { get; set; }

		public GameLuaLibrary(ILuaLibraries luaLibsImpl, ApiContainer apiContainer, Action<string> logOutputCallback)
			: base(luaLibsImpl, apiContainer, logOutputCallback) {}

		public override string Name => "game";

		[LuaMethodExample("for _, name in ipairs(game.list()) do console.log(name .. \" = \" .. tostring(game.get(name))); end;")]
		[LuaMethod("list", "Returns the names of the loaded core's game properties, in the order the core lists them; empty for a core without any")]
		public LuaTable List()
			=> _th.ListToTable((Properties?.Properties ?? [ ]).Select(static p => p.Name).ToList());

		[LuaMethodExample("local x = game.get(\"Kid.X\");")]
		[LuaMethod("get", "Returns a game property's value: a whole number, a float for an f32, or a boolean for a bool. A name the core does not have returns nil and says so in the console")]
		public object Get(string name)
		{
			if (Find("get", name) is not var (property, domain)) return null;
			var value = GamePropertyTable.Read(property, domain);
			return property.Type switch
			{
				GamePropertyType.Bool => value is not 0,
				GamePropertyType.F32 => value,
				_ => (object)(long)value,
			};
		}

		[LuaMethodExample("game.set(\"Kid.HP\", 3);")]
		[LuaMethod("set", "Sets a game property; the game's next step sees it. Takes a number, or a boolean for a bool. Returns whether it was set: not for a name the core does not have, nor for a property the game works out afresh every step, and the console says why")]
		public bool Set(string name, object value)
		{
			if (Find("set", name) is not var (property, domain)) return false;
			if (!property.Writable)
			{
				Log($"game.set: \"{property.Name}\" is worked out by the game every step, so setting it would change nothing");
				return false;
			}
			double? number = value switch
			{
				bool b => b ? 1 : 0,
				long l => l,
				double d => d,
				int i => i,
				_ => null,
			};
			if (number is null)
			{
				Log($"game.set: \"{property.Name}\" takes a number, not {value?.GetType().Name ?? "nil"}");
				return false;
			}
			GamePropertyTable.Write(property, domain, number.Value);
			return true;
		}

		[LuaMethodExample("local info = game.describe(\"Kid.Direction\"); console.log(info.type .. \" at \" .. info.domain .. \":\" .. info.offset);")]
		[LuaMethod("describe", "Returns a table describing a game property: name, domain, offset, type, size, group, writable, description, and label (its current value as the core names it, when it has names for its values); nil for a name the core does not have")]
		public LuaTable Describe(string name)
		{
			if (Find("describe", name) is not var (property, domain)) return null;
			var table = _th.CreateTable();
			table["name"] = property.Name;
			table["domain"] = property.Domain;
			table["offset"] = property.Offset;
			table["type"] = property.TypeName;
			table["size"] = (long)property.Size;
			table["group"] = property.Group;
			table["writable"] = property.Writable;
			table["description"] = property.Description;
			table["label"] = GamePropertyTable.Format(property, GamePropertyTable.Read(property, domain));
			return table;
		}

		/// <summary>
		/// The property and its domain, or null with the reason in the console. Not an
		/// exception: one thrown back through Lua after the script has yielded a frame
		/// takes the whole process down under Mono, even inside a pcall - the memory
		/// library's way (say it, and carry on) is the one that is safe.
		/// </summary>
		private (GameProperty Property, MemoryDomain Domain)? Find(string function, string name)
		{
			if (Properties?[name] is not { } property)
			{
				Log(Properties is null
					? $"game.{function}: the loaded core has no game properties (no \"{name}\")"
					: $"game.{function}: the loaded core has no game property \"{name}\"; game.list() says which it has");
				return null;
			}
			if (Domains?[property.Domain] is not { } domain)
			{
				Log($"game.{function}: \"{property.Name}\" is in \"{property.Domain}\", which the core does not have");
				return null;
			}
			return (property, domain);
		}
	}
}
