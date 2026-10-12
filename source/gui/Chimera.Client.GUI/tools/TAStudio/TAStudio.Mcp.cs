#nullable enable

namespace Chimera.Client.GUI
{
	/// <summary>What the MCP server's tools ask of TAStudio (docs/mcp-server.md; see McpHostTools).</summary>
	public partial class TAStudio
	{
		/// <summary>True while the playback position is on its way to a frame.</summary>
		public bool SeekInProgress => SeekingTo != -1;

		/// <summary>Saves a branch as the Branches box's button does and gives it a note. Its number, counted from 1.</summary>
		public int AddBranchWithNote(string note)
		{
			BookMarkControl.AddBranchExternal();
			var index = CurrentTasMovie.Branches.Count - 1;
			if (index >= 0 && note.Length is not 0)
			{
				CurrentTasMovie.Branches[index].UserText = note;
				RefreshDialog();
			}
			return index + 1;
		}
	}
}
