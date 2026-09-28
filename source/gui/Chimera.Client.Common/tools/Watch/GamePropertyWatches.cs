#nullable enable

using Chimera.Emulation.Common;

namespace Chimera.Client.Common
{
	/// <summary>
	/// A game core's properties as the watch tools see them (docs/game-cores.md): a
	/// property is a watch of its own width and sign, named after it. The name is the
	/// watch's note, which is also what a freeze made from it is called - so a property
	/// frozen from RAM Watch or RAM Search is listed in the cheats by its name.
	/// </summary>
	public static class GamePropertyWatches
	{
		/// <summary>The watch that reads <paramref name="property"/> in <paramref name="domain"/>, named after it.</summary>
		public static Watch WatchOf(GameProperty property, MemoryDomain domain)
			=> Watch.GenerateWatch(
				domain,
				property.Offset,
				property.Size switch { 1 => WatchSize.Byte, 2 => WatchSize.Word, _ => WatchSize.DWord },
				property.Type is GamePropertyType.F32 ? WatchDisplayType.Float
					: property.Signed ? WatchDisplayType.Signed
					: WatchDisplayType.Unsigned,
				bigEndian: false,
				note: property.Name);

		/// <summary>
		/// Names a watch after the property that starts where it points, when it has no
		/// note of its own; a note somebody wrote is never replaced. Returns the watch.
		/// </summary>
		public static Watch Named(Watch watch, IGameProperties? properties)
		{
			if (properties is null || watch.IsSeparator || watch.Domain is null || !string.IsNullOrEmpty(watch.Notes)) return watch;
			if (properties.At(watch.Domain.Name, watch.Address) is { } property) watch.Notes = property.Name;
			return watch;
		}
	}
}
