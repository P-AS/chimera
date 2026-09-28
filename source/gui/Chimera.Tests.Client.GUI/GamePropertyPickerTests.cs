using System.Linq;

using Chimera.Client.GUI;
using Chimera.Tests.Client.Common;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// RAM Watch's Add Game Properties (docs/game-cores.md): the core's properties by
	/// name under their groups, an array element by element, and only the ones not yet
	/// watched can be ticked.
	/// </summary>
	[TestClass]
	public class GamePropertyPickerTests
	{
		private const string Table = @"{ ""properties"": [
			{ ""name"": ""Kid.X"", ""domain"": ""Game State"", ""offset"": 0, ""type"": ""u8"", ""size"": 1, ""count"": 1, ""stride"": 1, ""group"": ""Kid"" },
			{ ""name"": ""Kid.Y"", ""domain"": ""Game State"", ""offset"": 1, ""type"": ""u8"", ""size"": 1, ""count"": 1, ""stride"": 1, ""group"": ""Kid"" },
			{ ""name"": ""Guards.HP"", ""domain"": ""Game State"", ""offset"": 2, ""type"": ""s8"", ""size"": 1, ""count"": 2, ""stride"": 4, ""group"": ""Guards"" },
			{ ""name"": ""Level Name"", ""domain"": ""Game State"", ""offset"": 16, ""type"": ""string"", ""size"": 8, ""count"": 1, ""stride"": 8 }
		], ""problems"": [] }";

		[TestMethod]
		public void PropertiesSitUnderTheirGroupsElementByElementAndOnlyNewOnesTick()
		{
			FakeGameProperties properties = new(Table);
			using GamePropertyPicker picker = new(properties, static e => e.Name, static e => e.Name is "Kid.Y");
			picker.Show();
			CollectionAssert.AreEqual(
				new[] { "Kid", "Kid.X", "Kid.Y", "Guards", "Guards.HP[0]", "Guards.HP[1]", "Level Name" },
				picker.Rows.ToArray());

			picker.Tick("Kid");     // a heading
			picker.Tick("Kid.Y");   // already watched
			picker.Tick("Guards.HP[1]");
			picker.Tick("Level Name");
			CollectionAssert.AreEqual(new[] { "Guards.HP[1]", "Level Name" }, picker.Chosen.Select(static e => e.Name).ToArray());
		}
	}
}
