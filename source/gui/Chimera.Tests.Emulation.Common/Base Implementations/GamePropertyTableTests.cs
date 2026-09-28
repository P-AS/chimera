using System.Linq;

using Chimera.Emulation.Common;

namespace Chimera.Tests.Emulation.Common
{
	/// <summary>
	/// A game core's property table (docs/game-cores.md): what the frontend takes from
	/// it, what it leaves out and says, and that a property reads and writes the bytes
	/// its type says - which is all a watch, a poke or a freeze of one ever does.
	/// </summary>
	[TestClass]
	public class GamePropertyTableTests
	{
		private const string Table = @"{ ""properties"": [
			{ ""name"": ""Kid.X"", ""domain"": ""Game State"", ""offset"": 0, ""type"": ""u8"", ""group"": ""Kid"", ""description"": ""Across the room"" },
			{ ""name"": ""Kid.Direction"", ""domain"": ""Game State"", ""offset"": 1, ""type"": ""s8"", ""group"": ""Kid"", ""values"": { ""-1"": ""Left"", ""0"": ""Right"" } },
			{ ""name"": ""Kid.Frame"", ""domain"": ""Game State"", ""offset"": 2, ""type"": ""u16"", ""group"": ""Kid"", ""writable"": false },
			{ ""name"": ""Guard.HP"", ""domain"": ""Game State"", ""offset"": 4, ""type"": ""s16"", ""group"": ""Guard"" },
			{ ""name"": ""Seed"", ""domain"": ""Game State"", ""offset"": 8, ""type"": ""u32"" },
			{ ""name"": ""Delta"", ""domain"": ""Game State"", ""offset"": 12, ""type"": ""s32"" },
			{ ""name"": ""Speed"", ""domain"": ""Game State"", ""offset"": 16, ""type"": ""f32"" },
			{ ""name"": ""Alive"", ""domain"": ""Game State"", ""offset"": 20, ""type"": ""bool"" },
			{ ""name"": ""Tile"", ""domain"": ""Level"", ""offset"": 3, ""type"": ""u8"" }
		] }";

		private static long? Sizes(string domain) => domain switch { "Game State" => 24, "Level" => 16, _ => null };

		private static MemoryDomainByteArray Block() => new("Game State", MemoryDomain.Endian.Little, new byte[24], writable: true, wordSize: 1);

		[TestMethod]
		public void EveryPropertyIsTakenInTheCoresOrder()
		{
			var table = GamePropertyTable.Parse(Table, Sizes);
			Assert.AreEqual(0, table.Problems.Count, string.Join("; ", table.Problems));
			CollectionAssert.AreEqual(
				new[] { "Kid.X", "Kid.Direction", "Kid.Frame", "Guard.HP", "Seed", "Delta", "Speed", "Alive", "Tile" },
				table.Properties.Select(static p => p.Name).ToArray());
			var direction = table["kid.direction"];
			Assert.IsNotNull(direction, "a name is found whatever its case");
			Assert.AreEqual("Kid", direction.Group);
			Assert.AreEqual("Left", direction.Values[-1]);
			Assert.IsFalse(table["Kid.Frame"]!.Writable);
			Assert.IsTrue(table["Kid.X"]!.Writable, "writable unless the core says otherwise");
			Assert.AreEqual("Across the room", table["Kid.X"]!.Description);
			CollectionAssert.AreEqual(new[] { 1, 1, 2, 2, 4, 4, 4, 1, 1 }, table.Properties.Select(static p => p.Size).ToArray());
		}

		[TestMethod]
		public void AnAddressIsNamedByThePropertyItStartsAndTheOneItIsIn()
		{
			var table = GamePropertyTable.Parse(Table, Sizes);
			Assert.AreEqual("Seed", table.At("Game State", 8)?.Name);
			Assert.IsNull(table.At("Game State", 9), "the second byte of a property does not start one");
			Assert.AreEqual("Seed", table.Containing("Game State", 11)?.Name);
			Assert.IsNull(table.Containing("Game State", 22), "a byte no property covers");
			Assert.AreEqual("Tile", table.At("Level", 3)?.Name, "a property may be in any of the core's domains");
			Assert.IsNull(table.At("Level", 8));
		}

		[TestMethod]
		public void WhatCannotBeUsedIsLeftOutAndSaid()
		{
			var table = GamePropertyTable.Parse(@"{ ""properties"": [
				{ ""name"": ""Fine"", ""domain"": ""Game State"", ""offset"": 0, ""type"": ""u8"" },
				{ ""name"": ""Nowhere"", ""domain"": ""Missing"", ""offset"": 0, ""type"": ""u8"" },
				{ ""name"": ""Past the end"", ""domain"": ""Game State"", ""offset"": 22, ""type"": ""u32"" },
				{ ""name"": ""Wide"", ""domain"": ""Game State"", ""offset"": 0, ""type"": ""u64"" },
				{ ""name"": ""Fine"", ""domain"": ""Game State"", ""offset"": 1, ""type"": ""u8"" },
				{ ""name"": ""No offset"", ""domain"": ""Game State"", ""type"": ""u8"" },
				{ ""name"": """", ""domain"": ""Game State"", ""offset"": 2, ""type"": ""u8"" },
				7
			] }", Sizes);
			CollectionAssert.AreEqual(new[] { "Fine" }, table.Properties.Select(static p => p.Name).ToArray());
			Assert.AreEqual(0L, table["Fine"]!.Offset, "the first of two with one name is the one kept");
			Assert.AreEqual(7, table.Problems.Count, string.Join("\n", table.Problems));
			StringAssert.Contains(table.Problems[0], "Missing");
			StringAssert.Contains(table.Problems[1], "past the end");
			StringAssert.Contains(table.Problems[2], "u64");
			StringAssert.Contains(table.Problems[3], "named twice");
		}

		[TestMethod]
		public void TextThatIsNoTableIsAnEmptyOneWithTheReason()
		{
			Assert.AreEqual(0, GamePropertyTable.Parse("", Sizes).Properties.Count);
			Assert.AreEqual(0, GamePropertyTable.Parse("", Sizes).Problems.Count, "no table is not a problem: every emulator has none");
			var broken = GamePropertyTable.Parse("{ not json", Sizes);
			Assert.AreEqual(0, broken.Properties.Count);
			Assert.AreEqual(1, broken.Problems.Count);
			Assert.AreEqual(1, GamePropertyTable.Parse(@"{ ""nothing"": [] }", Sizes).Problems.Count);
		}

		[TestMethod]
		public void EachTypeReadsAndWritesItsOwnBytes()
		{
			var table = GamePropertyTable.Parse(Table, Sizes);
			var block = Block();

			GamePropertyTable.Write(table["Kid.Direction"]!, block, -1);
			Assert.AreEqual(0xFF, block.Data[1]);
			Assert.AreEqual(-1.0, GamePropertyTable.Read(table["Kid.Direction"]!, block), "an s8 is sign-extended");

			GamePropertyTable.Write(table["Guard.HP"]!, block, -2);
			CollectionAssert.AreEqual(new byte[] { 0xFE, 0xFF }, block.Data.Skip(4).Take(2).ToArray(), "little-endian");
			Assert.AreEqual(-2.0, GamePropertyTable.Read(table["Guard.HP"]!, block));

			GamePropertyTable.Write(table["Seed"]!, block, 0xDEADBEEF);
			CollectionAssert.AreEqual(new byte[] { 0xEF, 0xBE, 0xAD, 0xDE }, block.Data.Skip(8).Take(4).ToArray());
			Assert.AreEqual((double)0xDEADBEEF, GamePropertyTable.Read(table["Seed"]!, block), "a u32 is not read as negative");

			GamePropertyTable.Write(table["Delta"]!, block, -100000);
			Assert.AreEqual(-100000.0, GamePropertyTable.Read(table["Delta"]!, block));

			GamePropertyTable.Write(table["Speed"]!, block, 1.5);
			Assert.AreEqual(1.5, GamePropertyTable.Read(table["Speed"]!, block));

			GamePropertyTable.Write(table["Alive"]!, block, 7);
			Assert.AreEqual(1, block.Data[20], "a bool stores 1 for anything true");
			block.Data[20] = 2;
			Assert.AreEqual(1.0, GamePropertyTable.Read(table["Alive"]!, block), "and reads any other byte as true, as the game would");

			GamePropertyTable.Write(table["Kid.X"]!, block, 257.9);
			Assert.AreEqual(1, block.Data[0], "a whole-number type takes the value truncated and wrapped to its width");
		}

		[TestMethod]
		public void AValueReadsByItsNameWhereTheCoreGaveOne()
		{
			var table = GamePropertyTable.Parse(Table, Sizes);
			Assert.AreEqual("Left", GamePropertyTable.Format(table["Kid.Direction"]!, -1));
			Assert.AreEqual("5", GamePropertyTable.Format(table["Kid.Direction"]!, 5), "a value with no name is its number");
			Assert.AreEqual("true", GamePropertyTable.Format(table["Alive"]!, 1));
			Assert.AreEqual("1.5", GamePropertyTable.Format(table["Speed"]!, 1.5));
		}
	}
}
