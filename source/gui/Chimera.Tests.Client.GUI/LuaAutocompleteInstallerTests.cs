using System;
using System.IO;
using Chimera.Client.Common;
using Chimera.Client.GUI;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Lua Console > Register To Text Editors > Sublime Text 2. Sublime makes its
	/// Lua package folder only when the package is first changed, and the
	/// registration wrote into it without making it: on a fresh install the click
	/// threw. The file is Chimera's own name now, and a file of BizHawk's name is
	/// BizHawk's to keep.
	/// </summary>
	[TestClass]
	public class LuaAutocompleteInstallerTests
	{
		private string _appData;

		[TestInitialize]
		public void Setup()
		{
			_appData = Path.Combine(Path.GetTempPath(), "chimera-sublime-" + Guid.NewGuid().ToString("N"));
			Directory.CreateDirectory(_appData);
		}

		[TestCleanup]
		public void Teardown()
		{
			try { Directory.Delete(_appData, recursive: true); } catch (IOException) { }
		}

		[TestMethod]
		public void RegisteringMakesTheLuaPackageFolder()
		{
			var installer = new LuaAutocompleteInstaller(_appData);
			Assert.IsFalse(installer.IsChimeraLuaRegistered(LuaAutocompleteInstaller.TextEditors.Sublime2));

			installer.InstallChimeraLua(LuaAutocompleteInstaller.TextEditors.Sublime2, new LuaDocumentation());

			var file = Path.Combine(_appData, "Sublime Text 2", "Packages", "Lua", "chimera.lua.sublime-completions");
			Assert.IsTrue(File.Exists(file), "the completions file is written, the folder made for it");
			StringAssert.Contains(File.ReadAllText(file), "\"completions\"");
			Assert.IsTrue(installer.IsChimeraLuaRegistered(LuaAutocompleteInstaller.TextEditors.Sublime2));
		}

		[TestMethod]
		public void BizHawksOwnFileIsLeftAlone()
		{
			var folder = Path.Combine(_appData, "Sublime Text 2", "Packages", "Lua");
			Directory.CreateDirectory(folder);
			var theirs = Path.Combine(folder, "bizhawk.lua.sublime-completions");
			File.WriteAllText(theirs, "{}");
			var installer = new LuaAutocompleteInstaller(_appData);
			Assert.IsFalse(installer.IsChimeraLuaRegistered(LuaAutocompleteInstaller.TextEditors.Sublime2), "another program's file is not Chimera's registration");

			installer.InstallChimeraLua(LuaAutocompleteInstaller.TextEditors.Sublime2, new LuaDocumentation());

			Assert.AreEqual("{}", File.ReadAllText(theirs));
			Assert.IsTrue(File.Exists(Path.Combine(folder, LuaAutocompleteInstaller.SublimeCompletionsFilename)));
		}
	}
}
