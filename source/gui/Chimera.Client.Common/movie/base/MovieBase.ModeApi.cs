namespace Chimera.Client.Common
{
	public partial class MovieBase
	{
		public MovieMode Mode { get; protected set; } = MovieMode.Inactive;

		public virtual void StartNewRecording()
		{
			Mode = MovieMode.Record;
			Log.Clear();
		}

		public void StartNewPlayback() => Mode = MovieMode.Play;
		public void SwitchToRecord() => Mode = MovieMode.Record;
		public void SwitchToPlay() => Mode = MovieMode.Play;
		public void FinishedMode() => Mode = MovieMode.Finished;
		public void Stop() =>  Mode = MovieMode.Inactive;
	}
}
