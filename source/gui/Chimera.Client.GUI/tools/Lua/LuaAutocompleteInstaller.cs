using System.IO;
using Chimera.Client.Common;

namespace Chimera.Client.GUI
{
	public class LuaAutocompleteInstaller
	{
		/// <param name="appDataFolder">where the editors keep their packages; the user's roaming AppData when not given (a test gives a folder of its own)</param>
		public LuaAutocompleteInstaller(string appDataFolder = null)
		{
			_appDataFolder = appDataFolder;
		}

		private readonly string _appDataFolder;

		public enum TextEditors
		{
			Sublime2,
			NotePad,
		}

		public bool IsInstalled(TextEditors editor)
		{
			return editor switch
			{
				TextEditors.Sublime2 => IsSublimeInstalled(),
				TextEditors.NotePad => IsNotepadInstalled(),
				_ => false,
			};
		}

		public bool IsChimeraLuaRegistered(TextEditors editor)
		{
			return editor switch
			{
				TextEditors.Sublime2 => IsChimeraLuaSublimeInstalled(),
				TextEditors.NotePad => IsChimeraLuaNotepadInstalled(),
				_ => false,
			};
		}

		public void InstallChimeraLua(TextEditors editor, LuaDocumentation docs)
		{
			switch (editor)
			{
				case TextEditors.Sublime2:
					InstallChimeraLuaToSublime2(docs);
					break;
				case TextEditors.NotePad:
					InstallChimeraLuaToNotepad(docs);
					break;
			}
		}

		private string AppDataFolder => _appDataFolder ?? Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);

		private bool IsSublimeInstalled()
		{
			// The most likely location of the app, eventually we should consider looking through the registry or installed apps as a more robust way to detect it;
			string exePath = @"C:\Program Files\Sublime Text 2\sublime_text.exe";
			return File.Exists(exePath);
		}

		private bool IsNotepadInstalled()
		{
			// The most likely location of the app, eventually we should consider looking through the registry or installed apps as a more robust way to detect it;
			string exePath = @"C:\Program Files (x86)\Notepad++\notepad++.exe";
			return File.Exists(exePath);
		}

		private static readonly string SublimeLuaPath = Path.Combine("Sublime Text 2", "Packages", "Lua");

		/// <summary>
		/// Chimera's own name. A file of the old name may be BizHawk's, written by
		/// BizHawk for itself, so it is left where it is.
		/// </summary>
		public const string SublimeCompletionsFilename = "chimera.lua.sublime-completions";

		private bool IsChimeraLuaSublimeInstalled()
		{
			return File.Exists(Path.Combine(AppDataFolder, SublimeLuaPath, SublimeCompletionsFilename));
		}

		private readonly string NotepadPath = "TODO";
		private readonly string NotepadAutoCompleteFileName = "TODO";

		private bool IsChimeraLuaNotepadInstalled()
		{
			var completions = Path.Combine(AppDataFolder, NotepadPath, NotepadAutoCompleteFileName);
			return File.Exists(completions);
		}

		private void InstallChimeraLuaToSublime2(LuaDocumentation docs)
		{
			// Sublime Text makes its Lua package folder only when the package is
			// first changed, so a fresh install has none: it is made here
			var folder = Path.Combine(AppDataFolder, SublimeLuaPath);
			Directory.CreateDirectory(folder);
			File.WriteAllText(Path.Combine(folder, SublimeCompletionsFilename), docs.ToSublime2CompletionList());
		}

		private void InstallChimeraLuaToNotepad(LuaDocumentation docs)
		{
			var autocomplete = Path.Combine(AppDataFolder, NotepadPath, NotepadAutoCompleteFileName);

			var text = docs.ToNotepadPlusPlusAutoComplete();

			// TODO
			//File.WriteAllText(completions, text);
		}
	}
}
