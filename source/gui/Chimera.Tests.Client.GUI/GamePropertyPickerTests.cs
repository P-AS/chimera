using System.Linq;

using Chimera.Client.GUI;
using Chimera.Emulation.Common;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// RAM Watch's Add Game Properties (docs/game-cores.md): the core's properties by
	/// name under their groups, and only the ones not yet watched can be ticked.
	/// </summary>
	[TestClass]
	public class GamePropertyPickerTests
	{
		private static readonly GamePropertyTable Table = GamePropertyTable.Parse(@"{ ""properties"": [
			{ ""name"": ""Kid.X"", ""domain"": ""Game State"", ""offset"": 0, ""type"": ""u8"", ""group"": ""Kid"" },
			{ ""name"": ""Kid.Y"", ""domain"": ""Game State"", ""offset"": 1, ""type"": ""u8"", ""group"": ""Kid"" },
			{ ""name"": ""Guard.HP"", ""domain"": ""Game State"", ""offset"": 2, ""type"": ""s8"", ""group"": ""Guard"" },
			{ ""name"": ""Seed"", ""domain"": ""Game State"", ""offset"": 4, ""type"": ""u32"" }
		] }", static name => name is "Game State" ? 8 : null);

		[TestMethod]
		public void PropertiesSitUnderTheirGroupsAndOnlyNewOnesTick()
		{
			using GamePropertyPicker picker = new(Table, static p => p.Name is "Guard.HP" ? "-1" : "0", static p => p.Name is "Kid.Y");
			picker.Show();
			CollectionAssert.AreEqual(new[] { "Kid", "Kid.X", "Kid.Y", "Guard", "Guard.HP", "Seed" }, picker.Rows.ToArray());

			picker.Tick("Kid");     // a heading
			picker.Tick("Kid.Y");   // already watched
			picker.Tick("Guard.HP");
			picker.Tick("Seed");
			CollectionAssert.AreEqual(new[] { "Guard.HP", "Seed" }, picker.Chosen.Select(static p => p.Name).ToArray());
		}
	}
}
