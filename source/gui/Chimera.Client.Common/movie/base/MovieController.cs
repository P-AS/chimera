using System.Collections.Generic;
using System.Globalization;
using System.Linq;

using Chimera.Emulation.Common;

namespace Chimera.Client.Common
{
	internal class MovieController : IMovieController
	{
		private readonly Dictionary<string, int> _myAxisControls = new();

		private readonly Dictionary<string, bool> _myBoolButtons = new();

		public MovieController(ControllerDefinition definition, string logKey) : this(definition)
		{
			if (!string.IsNullOrEmpty(logKey))
				Definition = new MovieControllerDefinition(definition, logKey);
		}

		public MovieController(ControllerDefinition definition)
		{
			Definition = definition;
			foreach ((string axisName, AxisSpec range) in definition.Axes)
			{
				_myAxisControls[axisName] = range.Neutral;
			}
		}

		public ControllerDefinition Definition { get; }

		public int AxisValue(string name)
			=> _myAxisControls.GetValueOrDefault(name);

		public bool IsPressed(string button)
			=> _myBoolButtons.GetValueOrDefault(button);

		public IReadOnlyCollection<(string Name, int Strength)> GetHapticsSnapshot() => Array.Empty<(string, int)>();

		public void SetHapticChannelStrength(string name, int strength) {}

		public void SetFrom(IController source)
		{
			for (int index = 0; index < Definition.BoolButtons.Count; index++)
			{
				string button = Definition.BoolButtons[index];
				_myBoolButtons[button] = source.IsPressed(button);
			}

			foreach (var name in Definition.Axes.Keys)
			{
				_myAxisControls[name] = source.AxisValue(name);
			}
		}

		public void SetFromMnemonic(string mnemonic)
		{
			if (string.IsNullOrWhiteSpace(mnemonic)) return;
			var iterator = 0;

			foreach (var playerControls in Definition.ControlsOrdered)
			foreach ((string buttonName, AxisSpec? axisSpec) in playerControls)
			{
				while (mnemonic[iterator] == '|') iterator++;

				if (axisSpec.HasValue)
				{
					var commaIndex = mnemonic.IndexOf(',', iterator);
					// in no culture, as the line was written (LogEntryGenerator) and as
					// the engine reads it: an ASCII '-' whatever the Region setting says
#if NETSTANDARD2_1_OR_GREATER || NETCOREAPP2_1_OR_GREATER
					var val = int.Parse(mnemonic.AsSpan(start: iterator, length: commaIndex - iterator), NumberStyles.Integer, CultureInfo.InvariantCulture);
#else
					var axisValueString = mnemonic.Substring(startIndex: iterator, length: commaIndex - iterator);
					var val = int.Parse(axisValueString, NumberStyles.Integer, CultureInfo.InvariantCulture);
#endif
					_myAxisControls[buttonName] = val;

					iterator = commaIndex + 1;
				}
				else
				{
					_myBoolButtons[buttonName] = mnemonic[iterator] != '.';
					iterator++;
				}
			}
		}

		public void SetBool(string buttonName, bool value)
		{
			_myBoolButtons[buttonName] = value;
		}

		public void SetAxis(string buttonName, int value)
		{
			_myAxisControls[buttonName] = value;
		}

		private class MovieControllerDefinition : ControllerDefinition
		{
			private readonly IReadOnlyList<IReadOnlyList<(string, AxisSpec?)>> _controlsFromLogKey;

			public MovieControllerDefinition(ControllerDefinition sourceDefinition, string logKey)
				: base(sourceDefinition)
			{
				var known = sourceDefinition.BoolButtons.Concat(sourceDefinition.Axes.Keys);
				_controlsFromLogKey = ReadLogKey(logKey, known)
					.Select(group => group
						.Select(buttonname => (buttonname, sourceDefinition.Axes.TryGetValue(buttonname, out var axisSpec) ? axisSpec : (AxisSpec?)null))
						.ToArray())
					.ToArray();
			}

			/// <summary>
			/// A key's groups of names. '#' starts a group and '|' ends a name, but a control's own name may hold
			/// either (Virtual Jaguar's "P2 #", MAME's "| @" and "# >"): splitting at every '#' made "P2 #" two
			/// names and a group too many, and every column after it was read as another. So at each place the
			/// machine's own names come first, the longest of them, when one is there whole and a '|' or the key's
			/// end follows it; otherwise a '#' starts a group and a name runs to the next '|'. Everything before
			/// the first '#' is ignored. The engine reads a key the same way (EntryLayout::buildFromKey).
			/// </summary>
			internal static List<List<string>> ReadLogKey(string logKey, IEnumerable<string> known)
			{
				var names = known.Where(static n => n.Length is not 0).Distinct().OrderByDescending(static n => n.Length).ToArray();
				string NameAt(int at)
				{
					foreach (var n in names)
					{
						if (string.CompareOrdinal(logKey, at, n, 0, n.Length) is not 0 || at + n.Length > logKey.Length) continue;
						var end = at + n.Length;
						if (end == logKey.Length || logKey[end] == '|') return n;
					}
					return null;
				}
				List<List<string>> groups = new();
				var pos = logKey.IndexOf('#');
				if (pos < 0) return groups;
				while (pos < logKey.Length)
				{
					var whole = groups.Count is 0 ? null : NameAt(pos);
					if (whole is null && logKey[pos] == '#')
					{
						groups.Add(new());
						pos++;
						continue;
					}
					string name;
					if (whole is not null)
					{
						name = whole;
						pos += whole.Length;
					}
					else
					{
						var bar = logKey.IndexOf('|', pos);
						var end = bar < 0 ? logKey.Length : bar;
						name = logKey.Substring(pos, end - pos);
						pos = end;
					}
					if (pos < logKey.Length && logKey[pos] == '|') pos++;
					if (name.Length is not 0) groups[groups.Count - 1].Add(name);
				}
				return groups;
			}

			protected override IReadOnlyList<IReadOnlyList<(string Name, AxisSpec? AxisSpec)>> GenOrderedControls() => _controlsFromLogKey;
		}
	}
}
