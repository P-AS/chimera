using System.Collections.Generic;

using Chimera.Client.Common;
using Chimera.Client.GUI;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// Files arriving by drag rather than through a dialog.
	///
	/// Two questions have to be answered without asking: which core a dropped
	/// file is for, and which of that core's slots it belongs in. Both are
	/// answered out of what the PACKAGES declare - the extensions they handle and
	/// the formats each slot takes - so a core installed tomorrow is understood
	/// without a table here changing.
	/// </summary>
	[TestClass]
	public class WizardDropTests
	{
		private const string Declaration = """
			{
			  "slots": [
			    { "id": "disc", "title": "Disc", "min": 1, "max": 1, "formats": ["cue", "chd", "iso"] },
			    { "id": "cards", "title": "Memory cards", "min": 0, "max": 2, "formats": ["mcd"] }
			  ]
			}
			""";

		private static DiscoveredCorePackage Package(string name, params string[] extensions)
		{
			Dictionary<string, string> map = new();
			foreach (var extension in extensions) map[extension] = name;
			return new DiscoveredCorePackage { Name = name, Path = $"/cores/{name}.chimeraCore", Extensions = map };
		}

		private static NewProjectWizard MakeForm(IReadOnlyList<DiscoveredCorePackage>? cores = null)
		{
			NewProjectWizard form = new(cores ?? [ ], static _ => [ ]);
			form.Show();
			form.UseDeclaration(TestPackages.Slots(Declaration));
			return form;
		}

		[TestMethod]
		public void EveryFileListTakesADrop()
		{
			using var form = MakeForm();
			Assert.IsTrue(form.SlotAcceptsDrops("disc"), "the list is the target, so it accepts what lands on it");
			Assert.IsTrue(form.SlotAcceptsDrops("cards"));
		}

		[TestMethod]
		public void AFileGoesToTheSlotThatDeclaresItsFormat()
		{
			using var form = MakeForm();
			Assert.AreEqual("disc", form.SlotForFile("/games/revolt.chd"));
			Assert.AreEqual("cards", form.SlotForFile("/saves/slot1.mcd"),
				"a memory card is not a disc, and the formats say so");
		}

		[TestMethod]
		public void AFileNobodyDeclaresGoesToTheFirstSlot()
		{
			using var form = MakeForm();
			Assert.AreEqual("disc", form.SlotForFile("/games/mystery.bin"),
				"a guess has to land somewhere, and the first slot is the one a person expects");
		}

		[TestMethod]
		public void TheCoreIsGuessedFromWhatThePackagesClaim()
		{
			using var form = MakeForm([ Package("Genesis Plus GX", ".gg", ".md"), Package("Flycast", ".chd", ".gdi") ]);
			Assert.AreEqual(0, form.GuessCoreIndexFor("/games/sonic.gg"));
			Assert.AreEqual(1, form.GuessCoreIndexFor("/games/revolt.chd"));
			Assert.AreEqual(1, form.GuessCoreIndexFor("/GAMES/REVOLT.CHD"), "an extension is not case");
		}

		[TestMethod]
		public void AnExtensionNobodyClaimsIsNotGuessedAt()
		{
			using var form = MakeForm([ Package("Genesis Plus GX", ".gg") ]);
			Assert.AreEqual(-1, form.GuessCoreIndexFor("/games/thing.xyz"));
			Assert.AreEqual(-1, form.GuessCoreIndexFor("/games/no-extension"));
		}

		[TestMethod]
		public void AFileWithNoExtensionIsAFormatASlotCanDeclare()
		{
			// some consoles keep a game as one file with a long name and nothing
			// after it; a slot says so with the empty format (chimera issue 250)
			var declaration = TestPackages.Slots("""
				{
				  "slots": [
				    { "id": "game", "title": "Game", "min": 1, "max": 1, "formats": ["iso", ""] }
				  ]
				}
				""");
			using NewProjectWizard form = new([ ], static _ => [ ]);
			form.Show();
			form.UseDeclaration(declaration);

			form.AddFileToSlot("game", "/games/834312072F4985F9D33D0B9549FFAA32C505FDAD58");
			Assert.AreEqual("", form.StatusText, "a file with no extension is one of the slot's formats, and nothing is said");

			var slot = declaration.Slots[0];
			Assert.AreEqual(".iso, no extension", ProjectSlotDeclaration.FormatsText(slot), "the empty format is said in words");
			Assert.AreEqual("*.iso;*.", ProjectSlotDeclaration.FilterFor(slot), "and a file picker is asked for it as it spells it");

			form.RemoveFileFromSlot("game", "834312072F4985F9D33D0B9549FFAA32C505FDAD58");
			form.AddFileToSlot("game", "/games/thing.bin");
			StringAssert.Contains(form.StatusText, ".iso, no extension", "a format the slot does not take is still said, with what it takes");
		}

		[TestMethod]
		public void AnUnexpectedFormatIsSaidRatherThanRefused()
		{
			using var form = MakeForm();
			form.AddFileToSlot("disc", "/games/revolt.chd");
			Assert.AreEqual("", form.StatusText, "a format the slot declares passes without comment");

			form.AddFileToSlot("cards", "/games/revolt.iso");
			CollectionAssert.AreEqual(new[] { "revolt.iso" }, form.SlotFileNames("cards"),
				"the file is still added: the picker offers All files too, and a renamed file is the person's business");
			StringAssert.Contains(form.StatusText, ".mcd", "but the slot says what it expected");
		}
	}
}
