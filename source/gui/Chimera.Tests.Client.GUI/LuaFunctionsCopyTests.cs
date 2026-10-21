using Chimera.Client.GUI;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Issue 343: a function copied from Help > Lua Functions is pasted into a
	/// script, so every line of it has to be Lua. The declaration is a comment,
	/// a Lua one.
	/// </summary>
	[TestClass]
	public class LuaFunctionsCopyTests
	{
		[TestMethod]
		public void TheDeclarationIsALuaComment()
		{
			var text = LuaFunctionsForm.CopyText("LuaCanvas", "DrawBox", "(int x, int y, int x2, int y2)", "LuaCanvas.DrawBox( 16, 32, 162, 322 );");
			var lines = text.Replace("\r\n", "\n").TrimEnd('\n').Split('\n');
			Assert.AreEqual(2, lines.Length, text);
			StringAssert.StartsWith(lines[0], "--LuaCanvas.DrawBox(", "the declaration is a Lua comment");
			Assert.AreEqual("LuaCanvas.DrawBox( 16, 32, 162, 322 );", lines[1]);
		}

		[TestMethod]
		public void TwoFunctionsWithoutExamplesAreTwoLines()
		{
			var text = LuaFunctionsForm.CopyText("event", "onframeend", "(nluafunc luaf)", null)
				+ LuaFunctionsForm.CopyText("event", "onframestart", "(nluafunc luaf)", null);
			var lines = text.Replace("\r\n", "\n").TrimEnd('\n').Split('\n');
			Assert.AreEqual(2, lines.Length, text);
			foreach (var line in lines) StringAssert.StartsWith(line, "--", line);
		}
	}
}
