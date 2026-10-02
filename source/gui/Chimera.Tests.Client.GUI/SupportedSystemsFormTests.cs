using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>Core Manager &gt; Systems...: the list narrows as the filter is typed (#172).</summary>
	[TestClass]
	public class SupportedSystemsFormTests
	{
		[TestMethod]
		public void TypingNarrowsTheList()
		{
			var systems = SupportedSystems.From(new (string, System.Collections.Generic.IReadOnlyList<string>, bool)[]
			{
				("ares", [ "PS1", "WS", "WSC" ], true),
				("PCSX2", [ "PS2" ], true),
			});
			using SupportedSystemsForm form = new(systems);
			form.Show();
			Assert.AreEqual(4, form.ShownRows.Count);
			Assert.AreEqual("4 systems", form.CountText);

			form.SetFilter("wonder");
			CollectionAssert.AreEqual(new[] { "WonderSwan | WS | ares", "WonderSwan Color | WSC | ares" }, (System.Collections.ICollection)form.ShownRows);
			Assert.AreEqual("2 of 4", form.CountText);
		}
	}
}
