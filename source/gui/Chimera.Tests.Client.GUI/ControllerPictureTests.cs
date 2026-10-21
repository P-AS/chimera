using System.Drawing;
using System.Drawing.Imaging;
using System.IO;

using Chimera.Client.GUI;
using Chimera.Emulation.Common;
using Chimera.Emulation.Common.Waterbox;

namespace Chimera.Tests.Client.GUI
{
	/// <summary>
	/// A package may ship a picture of its controller, which Config &gt; Controls shows beside the
	/// bindings (issue 338). The picture is a file inside the package that its input declaration
	/// names; a name that leaves the package, or names nothing, is no picture.
	/// </summary>
	[TestClass]
	public class ControllerPictureTests
	{
		private sealed class Machine : IEmulator, IControllerPicture
		{
			public Machine(string? picture)
			{
				ControllerPicturePath = picture;
				ServiceProvider = new BasicServiceProvider(this);
			}

			public string? ControllerPicturePath { get; }

			public IEmulatorServiceProvider ServiceProvider { get; }

			public ControllerDefinition ControllerDefinition { get; } = new ControllerDefinition("pad") { BoolButtons = { "P1 A" } }.MakeImmutable();

			public bool FrameAdvance(IController controller, bool render, bool renderSound = true) => true;

			public int Frame => 0;

			public string SystemId => "TEST";

			public bool DeterministicEmulation => true;

			public void ResetCounters() {}

			public void Dispose() {}
		}

		private static string Folder()
		{
			var dir = Path.Combine(Path.GetTempPath(), "chimera-picture-" + Path.GetRandomFileName());
			Directory.CreateDirectory(Path.Combine(dir, "pictures"));
			return dir;
		}

		[TestMethod]
		public void ThePictureIsTheFileInsideThePackage()
		{
			var dir = Folder();
			try
			{
				var file = Path.Combine(dir, "pictures", "pad.png");
				using (Bitmap bmp = new(20, 10)) bmp.Save(file, ImageFormat.Png);
				File.WriteAllText(Path.Combine(Path.GetTempPath(), "outside.png"), "x");

				Assert.AreEqual(Path.GetFullPath(file), WaterboxCore.PictureIn(dir, "pictures/pad.png"));
				Assert.IsNull(WaterboxCore.PictureIn(dir, "pictures/none.png"), "a picture that is not there");
				Assert.IsNull(WaterboxCore.PictureIn(dir, "../outside.png"), "a name that leaves the package");
				Assert.IsNull(WaterboxCore.PictureIn(dir, null));
				Assert.IsNull(WaterboxCore.PictureIn(dir, ""));
			}
			finally
			{
				Directory.Delete(dir, recursive: true);
			}
		}

		[TestMethod]
		public void TheControlsWindowReadsThePictureTheCoreOffers()
		{
			var dir = Folder();
			try
			{
				var file = Path.Combine(dir, "pictures", "pad.png");
				using (Bitmap bmp = new(32, 16)) bmp.Save(file, ImageFormat.Png);
				using (var picture = ControllerConfig.ControllerPicture(new Machine(file)))
				{
					Assert.IsNotNull(picture, "the core's picture was not read");
					Assert.AreEqual(new Size(32, 16), picture!.Size);
				}
				Assert.IsNull(ControllerConfig.ControllerPicture(new Machine(null)), "a core without a picture");

				File.WriteAllText(file, "not a picture");
				Assert.IsNull(ControllerConfig.ControllerPicture(new Machine(file)), "a file that is not a picture is no picture");
			}
			finally
			{
				Directory.Delete(dir, recursive: true);
			}
		}
	}
}
