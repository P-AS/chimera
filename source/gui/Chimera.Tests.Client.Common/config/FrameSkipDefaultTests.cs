using System.IO;

using Chimera.Client.Common;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// Frame skip works now, and the owner chose no skipping as the default
	/// (2026-10-15). A config written before carries the old default of 4,
	/// which nobody chose and which did nothing; it becomes 0 once. A value
	/// chosen after that stands.
	/// </summary>
	[TestClass]
	public class FrameSkipDefaultTests
	{
		private static Config LoadFrom(string json)
		{
			var path = Path.Combine(Path.GetTempPath(), "chimera-frameskip-" + Path.GetRandomFileName() + ".ini");
			try
			{
				File.WriteAllText(path, json);
				var config = ConfigService.Load<Config>(path);
				config.ResolveDefaults();
				return config;
			}
			finally
			{
				File.Delete(path);
			}
		}

		[TestMethod]
		public void AnOldConfigsFrameSkipBecomesNone()
		{
			var config = LoadFrom("{ \"FrameSkip\": 4 }");
			Assert.AreEqual(0, config.FrameSkip);
			Assert.IsTrue(config.FrameSkipSettled);
		}

		[TestMethod]
		public void AChoiceMadeSinceStands()
		{
			var config = LoadFrom("{ \"FrameSkip\": 3, \"FrameSkipSettled\": true }");
			Assert.AreEqual(3, config.FrameSkip);
		}

		[TestMethod]
		public void ANewConfigSkipsNothing()
		{
			var config = new Config();
			config.ResolveDefaults();
			Assert.AreEqual(0, config.FrameSkip);
		}
	}
}
