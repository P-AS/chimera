using System;
using System.ComponentModel;
using System.Numerics;

using Chimera.Emulation.Common.Waterbox;

using Newtonsoft.Json;

namespace Chimera.Tests.Emulation.Common
{
	/// <summary>
	/// The whole-number types past an int's, and a date and time (issue 358): what a declaration of each
	/// keeps, what the settings grid edits, and that the numbers reach the core with every digit.
	/// </summary>
	[TestClass]
	public class WaterboxSettingTypesTests
	{
		private static WaterboxConfig.SettingDecl Of(string type, object? dflt = null, decimal? min = null, decimal? max = null)
			=> new() { Name = "n", Type = type, Default = dflt, Min = min, Max = max };

		[TestMethod]
		public void EachWholeNumberTypeHoldsWhatItsCTypeHolds()
		{
			Assert.AreEqual(4294967295u, Of("uint").Coerce(4294967295L));
			Assert.AreEqual(0u, Of("uint").Coerce(-1L), "no sign");
			Assert.AreEqual(4294967295u, Of("uint").Coerce("5000000000"), "past the top is the top");
			Assert.AreEqual(long.MinValue, Of("long").Coerce("-9223372036854775808"));
			Assert.AreEqual(long.MaxValue, Of("long").Coerce(long.MaxValue));
			// Newtonsoft reads a number past a long's top as a BigInteger
			Assert.AreEqual(ulong.MaxValue, Of("ulong").Coerce(BigInteger.Parse("18446744073709551615")));
			Assert.AreEqual(0ul, Of("ulong").Coerce(-5L));
			Assert.AreEqual(int.MaxValue, Of("int").Coerce(5000000000L), "an int is clamped to an int's range too");
		}

		[TestMethod]
		public void DeclaredBoundsAreKeptExactlyAtSixtyFourBits()
		{
			// a decimal bound holds a ulong's whole range
			var decl = JsonConvert.DeserializeObject<WaterboxConfig.SettingDecl>(
				"""{ "name": "seed", "type": "ulong", "default": 1, "min": 1, "max": 18446744073709551614 }""")!;
			Assert.AreEqual(18446744073709551614ul, decl.Coerce(ulong.MaxValue));
			Assert.AreEqual(1ul, decl.Coerce(0));
			Assert.AreEqual(1ul, decl.DefaultValue);
		}

		[TestMethod]
		public void ADefaultPastAnIntsRangeIsInferredAsAWiderType()
		{
			var big = JsonConvert.DeserializeObject<WaterboxConfig.SettingDecl>("""{ "name": "a", "default": 5000000000 }""")!;
			Assert.AreEqual("long", big.EffectiveType);
			Assert.AreEqual(5000000000L, big.DefaultValue);
			var huge = JsonConvert.DeserializeObject<WaterboxConfig.SettingDecl>("""{ "name": "b", "default": 18446744073709551615 }""")!;
			Assert.AreEqual("ulong", huge.EffectiveType);
			Assert.AreEqual("int", new WaterboxConfig.SettingDecl { Default = 8L }.EffectiveType, "a small one is still an int");
		}

		[TestMethod]
		public void ADateTimeIsKeptAsItsSecondsSince1970()
		{
			var clock = Of("datetime", 946684800L);
			Assert.AreEqual(946684800L, clock.DefaultValue);
			Assert.AreEqual(946684800L, clock.Coerce("2000-01-01 00:00:00"), "typed as it is shown");
			Assert.AreEqual(946684800L, clock.Coerce("2000-01-01"), "a date alone is its midnight");
			Assert.AreEqual(946684800L, clock.Coerce(new DateTime(2000, 1, 1, 0, 0, 0, DateTimeKind.Utc)));
			Assert.AreEqual(4102444800L, clock.Coerce("2100-01-01 00:00"), "past 2038, which an int's seconds stop at");
			Assert.AreEqual(-86400L, clock.Coerce("1969-12-31 00:00:00"));
			Assert.AreEqual(0L, clock.Coerce("not a date"), "what is not a date is 1970, as a number that is not one is 0");
			Assert.AreEqual(253402300799L, clock.Coerce(long.MaxValue), "no later than a date can show");
			Assert.AreEqual(946684800L, Of("datetime", 0L, min: 946684800m).Coerce("1990-01-01"), "the declared bounds are seconds");
		}

		[TestMethod]
		public void TheGridEditsADateTimeAsADateInUtcAndKeepsItsSeconds()
		{
			WaterboxCoreSettings settings = new() { Declarations = [ new() { Name = "clock", Type = "datetime", Default = 946684800L } ] };
			var row = TypeDescriptor.GetProperties(settings)["clock"];
			Assert.AreEqual(typeof(DateTime), row.PropertyType);
			Assert.AreEqual(new DateTime(2000, 1, 1, 0, 0, 0, DateTimeKind.Utc), row.GetValue(settings));
			Assert.AreEqual("2000-01-01 00:00:00", row.Converter.ConvertToInvariantString(row.GetValue(settings)));
			Assert.IsFalse(row.ShouldSerializeValue(settings), "the default is not shown as changed");

			row.SetValue(settings, row.Converter.ConvertFromInvariantString("2010-06-15 12:30:00"));
			Assert.AreEqual(1276605000L, settings.Values["clock"], "the project and the core get the seconds");
			Assert.IsTrue(row.ShouldSerializeValue(settings));
			Assert.ThrowsExactly<FormatException>(() => row.Converter.ConvertFromInvariantString("15/06/2010"));
		}

		[TestMethod]
		public void TheGridEditsEachWholeNumberTypeAsItself()
		{
			WaterboxCoreSettings settings = new()
			{
				Declarations = [ Named(Of("uint"), "u"), Named(Of("long"), "l"), Named(Of("ulong"), "ul") ],
			};
			var rows = TypeDescriptor.GetProperties(settings);
			Assert.AreEqual(typeof(uint), rows["u"].PropertyType);
			Assert.AreEqual(typeof(long), rows["l"].PropertyType);
			Assert.AreEqual(typeof(ulong), rows["ul"].PropertyType);
			rows["ul"].SetValue(settings, rows["ul"].Converter.ConvertFromInvariantString("18446744073709551615"));
			Assert.AreEqual(ulong.MaxValue, settings.Values["ul"]);
		}

		[TestMethod]
		public void TheValuesReachTheCoreWithEveryDigit()
		{
			// what WaterboxCore hands the engine, and a project keeps
			WaterboxCoreSettings settings = new();
			settings.Values["ul"] = Of("ulong").Coerce("18446744073709551615");
			settings.Values["l"] = Of("long").Coerce("-9223372036854775808");
			settings.Values["clock"] = Of("datetime").Coerce("2100-01-01 00:00:00");
			var json = JsonConvert.SerializeObject(settings.Values);
			Assert.AreEqual("""{"ul":18446744073709551615,"l":-9223372036854775808,"clock":4102444800}""", json);
		}

		private static WaterboxConfig.SettingDecl Named(WaterboxConfig.SettingDecl decl, string name)
		{
			decl.Name = name;
			return decl;
		}
	}
}
