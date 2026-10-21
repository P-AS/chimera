namespace Chimera.Emulation.Common
{
	/// <summary>
	/// A picture of the machine's controller, which a core's package may ship (issue 338,
	/// user-decided 2026-10-21). The frontend knows no controller's looks: a package names a
	/// PNG in its input declaration (<c>"picture"</c>), and Config &gt; Controls shows it beside
	/// the bindings.
	/// </summary>
	public interface IControllerPicture : IEmulatorService
	{
		/// <summary>The picture's file, a PNG inside the unpacked package, or null when the package has none.</summary>
		string? ControllerPicturePath { get; }
	}
}
