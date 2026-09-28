using System.Linq;

using Chimera.Client.Common;
using Chimera.Emulation.Common;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// A game core's properties in the watch tools (docs/game-cores.md): a property is
	/// a watch of its own width and sign under its own name, and a freeze made from one
	/// holds its value and keeps its name.
	/// </summary>
	[TestClass]
	public class GamePropertyWatchesTests
	{
		private static readonly GamePropertyTable Table = GamePropertyTable.Parse(@"{ ""properties"": [
			{ ""name"": ""Kid.X"", ""domain"": ""Game State"", ""offset"": 0, ""type"": ""u8"" },
			{ ""name"": ""Guard.HP"", ""domain"": ""Game State"", ""offset"": 2, ""type"": ""s16"" },
			{ ""name"": ""Seed"", ""domain"": ""Game State"", ""offset"": 4, ""type"": ""u32"" },
			{ ""name"": ""Speed"", ""domain"": ""Game State"", ""offset"": 8, ""type"": ""f32"" },
			{ ""name"": ""Alive"", ""domain"": ""Game State"", ""offset"": 12, ""type"": ""bool"" }
		] }", static name => name is "Game State" ? 16 : null);

		private static MemoryDomainByteArray Block() => new("Game State", MemoryDomain.Endian.Little, new byte[16], writable: true, wordSize: 1);

		[TestMethod]
		public void APropertyIsAWatchOfItsOwnWidthAndSignUnderItsName()
		{
			var block = Block();
			var watches = Table.Properties.Select(p => GamePropertyWatches.WatchOf(p, block)).ToList();
			CollectionAssert.AreEqual(new[] { "Kid.X", "Guard.HP", "Seed", "Speed", "Alive" }, watches.Select(static w => w.Notes).ToArray());
			CollectionAssert.AreEqual(
				new[] { WatchSize.Byte, WatchSize.Word, WatchSize.DWord, WatchSize.DWord, WatchSize.Byte },
				watches.Select(static w => w.Size).ToArray());
			CollectionAssert.AreEqual(
				new[] { WatchDisplayType.Unsigned, WatchDisplayType.Signed, WatchDisplayType.Unsigned, WatchDisplayType.Float, WatchDisplayType.Unsigned },
				watches.Select(static w => w.Type).ToArray());
			Assert.IsTrue(watches.TrueForAll(static w => !w.BigEndian));

			GamePropertyTable.Write(Table["Guard.HP"]!, block, -3);
			var hp = watches[1];
			hp.Update(PreviousType.Original);
			Assert.AreEqual("-3", hp.ValueString, "the watch reads what the property holds");
		}

		[TestMethod]
		public void AWatchOnAPropertyTakesItsNameUnlessItHasOne()
		{
			var block = Block();
			var bare = Watch.GenerateWatch(block, 4, WatchSize.DWord, WatchDisplayType.Hex, bigEndian: false);
			Assert.AreEqual("Seed", GamePropertyWatches.Named(bare, Table).Notes);

			var noted = Watch.GenerateWatch(block, 4, WatchSize.DWord, WatchDisplayType.Hex, bigEndian: false, note: "my seed");
			Assert.AreEqual("my seed", GamePropertyWatches.Named(noted, Table).Notes, "a note somebody wrote is kept");

			var between = Watch.GenerateWatch(block, 5, WatchSize.Byte, WatchDisplayType.Hex, bigEndian: false);
			Assert.AreEqual("", GamePropertyWatches.Named(between, Table).Notes, "the middle of a property is not the property");

			var emulator = Watch.GenerateWatch(block, 4, WatchSize.DWord, WatchDisplayType.Hex, bigEndian: false);
			Assert.AreEqual("", GamePropertyWatches.Named(emulator, null).Notes, "a core without properties names nothing");
		}

		[TestMethod]
		public void AFrozenPropertyHoldsItsValueAndKeepsItsName()
		{
			var block = Block();
			GamePropertyTable.Write(Table["Guard.HP"]!, block, -3);
			GamePropertyTable.Write(Table["Speed"]!, block, 2.5);
			var hp = GamePropertyWatches.WatchOf(Table["Guard.HP"]!, block);
			var speed = GamePropertyWatches.WatchOf(Table["Speed"]!, block);
			Cheat hpFreeze = new(hp, hp.Value);
			Cheat speedFreeze = new(speed, speed.Value);
			Assert.AreEqual("Guard.HP", hpFreeze.Name, "the freeze is listed by the property's name");

			// the game moves on and changes both...
			GamePropertyTable.Write(Table["Guard.HP"]!, block, 40);
			GamePropertyTable.Write(Table["Speed"]!, block, -1);
			// ...and the freeze, pulsed before the next step, puts them back
			hpFreeze.Pulse();
			speedFreeze.Pulse();
			Assert.AreEqual(-3.0, GamePropertyTable.Read(Table["Guard.HP"]!, block));
			Assert.AreEqual(2.5, GamePropertyTable.Read(Table["Speed"]!, block));
		}
	}
}
