#nullable disable

using System.Collections.Generic;
using System.Reflection;

using Chimera.Client.Common;
using Chimera.Common;

using static Chimera.Common.RawInputImports;

namespace Chimera.Tests.Client.Common
{
	/// <summary>
	/// Two ways key names went wrong. With "Handle alternate keyboard layouts"
	/// on, a key is named by what the layout makes it, through a table from
	/// Windows' virtual keys to Chimera's keys: it sent both Alt keys to the
	/// Windows keys, so Alt was bound as LeftWin. And turning "Merge L+R
	/// modifier keys" on or off offers to rewrite the bindings, which missed
	/// every key after the first in a list, as "Shift, LeftShift" has a space
	/// before LeftShift.
	/// </summary>
	[TestClass]
	public class KeyNameFixTests
	{
		[TestMethod]
		public void EachModifierVirtualKeyIsItsOwnKey()
		{
			// the table is private to the Windows raw input class
			var type = System.Type.GetType("Chimera.Input.RawKeyMouseInput, Chimera.Input", throwOnError: true);
			var map = (IReadOnlyDictionary<VirtualKey, RawKey>) type
				.GetField("VKeyToRawKeyMap", BindingFlags.NonPublic | BindingFlags.Static)
				.GetValue(null);
			foreach (var (virtualKey, key) in new[]
				{
					(VirtualKey.VK_LSHIFT, RawKey.LEFTSHIFT),
					(VirtualKey.VK_RSHIFT, RawKey.RIGHTSHIFT),
					(VirtualKey.VK_LCONTROL, RawKey.LEFTCONTROL),
					(VirtualKey.VK_RCONTROL, RawKey.RIGHTCONTROL),
					(VirtualKey.VK_LMENU, RawKey.LEFTALT),
					(VirtualKey.VK_RMENU, RawKey.RIGHTALT),
					(VirtualKey.VK_LWIN, RawKey.LEFTGUI),
					(VirtualKey.VK_RWIN, RawKey.RIGHTGUI),
				})
			{
				Assert.AreEqual(key, map[virtualKey], $"{virtualKey} is named as another key");
			}
		}

		/// <summary>
		/// The whole way on Windows: each modifier key's place, through the
		/// keyboard layout as Windows maps it, back to the same key.
		/// </summary>
		[TestMethod]
		public void EachModifierKeyIsItselfThroughTheLayout()
		{
			if (OSTailoredCode.IsUnixHost) Assert.Inconclusive("the layout is Windows' own (MapVirtualKeyW)");
			var mapping = System.Type.GetType("Chimera.Input.RawKeyMouseInput, Chimera.Input", throwOnError: true)
				.GetMethod("MapToRealKeyViaScanCode", BindingFlags.NonPublic | BindingFlags.Static);
			foreach (var key in new[]
				{
					RawKey.LEFTSHIFT, RawKey.RIGHTSHIFT, RawKey.LEFTCONTROL, RawKey.RIGHTCONTROL,
					RawKey.LEFTALT, RawKey.RIGHTALT, RawKey.LEFTGUI, RawKey.RIGHTGUI,
				})
			{
				Assert.AreEqual(key, (RawKey) mapping.Invoke(null, new object[] { key }), $"{key} became another key");
			}
		}

		[TestMethod]
		public void EveryKeyOfAListIsRewritten()
		{
			var merge = new Dictionary<string, string>
			{
				["LeftShift"] = "Shift",
				["RightShift"] = "Shift",
				["RightCtrl"] = "Ctrl",
			};
			var config = new Config();
			config.HotkeyBindings["Pause"] = "LeftShift, RightShift+P, Z";
			config.AllTrollers["Synth"] = new() { ["P1 A"] = "X, RightCtrl+X" };
			config.AllTrollersAutoFire["Synth"] = new() { ["P1 B"] = "C, LeftShift" };
			config.ReplaceKeysInBindings(merge);
			Assert.AreEqual("Shift, Shift+P, Z", config.HotkeyBindings["Pause"]);
			Assert.AreEqual("X, Ctrl+X", config.AllTrollers["Synth"]["P1 A"]);
			Assert.AreEqual("C, Shift", config.AllTrollersAutoFire["Synth"]["P1 B"]);
		}
	}
}
