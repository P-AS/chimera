using System;
using System.Collections;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;

using Newtonsoft.Json;

namespace Chimera.Emulation.Common.Waterbox
{
	/// <summary>
	/// A bag of user settings for a waterbox core, and - the whole point of this
	/// class - a description of itself that a property grid can draw.
	///
	/// The frontend has no per-core settings dialogs: one adapter serves every
	/// package, so there is no class whose properties could be the settings. The
	/// package declares them instead (<see cref="WaterboxConfig.SettingDecl"/>) and
	/// this synthesizes a property per declaration through
	/// <see cref="ICustomTypeDescriptor"/>, which is what WinForms' PropertyGrid
	/// asks for names, types, descriptions and defaults. So the grid shows a
	/// labelled, typed, documented row per setting, and every word in it came from
	/// the core.
	///
	/// Only <see cref="Values"/> is serialized: the config file and movie headers
	/// store a plain name -&gt; value map, unchanged by any of this.
	/// </summary>
	public abstract class WaterboxSettingsBase : ICustomTypeDescriptor
	{
		public Dictionary<string, object> Values { get; set; } = new();

		/// <summary>
		/// The declarations these values belong to. Not serialized and not part of
		/// equality: it is the core talking about the values, not one of them.
		/// Null when the settings were deserialized without a core in hand (from the
		/// config file at startup, say), in which case the grid simply has nothing to
		/// draw - which is correct, since no core is loaded to draw it for.
		/// </summary>
		[JsonIgnore]
		public IReadOnlyList<WaterboxConfig.SettingDecl>? Declarations { get; set; }

		/// <summary>
		/// For a window that knows which files are on hand (the New Project wizard): whether a value of a
		/// setting has the firmware it calls for - (setting, value) to true or false, or null for a value
		/// that calls for no file of its own. Such values are listed first and marked found (issue #293).
		/// Null, as everywhere else, leaves every list as the core declared it.
		/// </summary>
		[JsonIgnore]
		public Func<string, string, bool?>? FirmwareOnHand { get; set; }

		/// <summary>The words after a value whose firmware is on hand.</summary>
		public const string FoundMark = "  (found)";

		/// <summary>True if <paramref name="other"/> holds the same values.</summary>
		public bool ValuesEqual(WaterboxSettingsBase other)
		{
			var a = Values ?? new Dictionary<string, object>();
			var b = other?.Values ?? new Dictionary<string, object>();
			if (a.Count != b.Count) return false;
			foreach (var (k, v) in a)
			{
				if (!b.TryGetValue(k, out var w)) return false;
				// JSON round-trips turn 8 into 8L and back; compare as text so a value
				// that survived a config file still equals the one that did not
				if (!string.Equals(Convert.ToString(v), Convert.ToString(w), StringComparison.Ordinal)) return false;
			}
			return true;
		}

		protected void CopyTo(WaterboxSettingsBase clone)
		{
			clone.Values = Values is null ? new Dictionary<string, object>() : new Dictionary<string, object>(Values);
			clone.Declarations = Declarations;
			clone.FirmwareOnHand = FirmwareOnHand;
		}

		private IReadOnlyList<WaterboxConfig.SettingDecl> Decls
			=> Declarations ?? [ ];

		// ---- ICustomTypeDescriptor: everything but GetProperties is the default ----

		public AttributeCollection GetAttributes() => TypeDescriptor.GetAttributes(GetType(), noCustomTypeDesc: true);

		public string GetClassName() => TypeDescriptor.GetClassName(GetType(), noCustomTypeDesc: true);

		public string GetComponentName() => TypeDescriptor.GetComponentName(GetType(), noCustomTypeDesc: true);

		public TypeConverter GetConverter() => TypeDescriptor.GetConverter(GetType(), noCustomTypeDesc: true);

		public EventDescriptor GetDefaultEvent() => TypeDescriptor.GetDefaultEvent(GetType(), noCustomTypeDesc: true);

		public PropertyDescriptor? GetDefaultProperty() => null;

		public object GetEditor(Type editorBaseType) => TypeDescriptor.GetEditor(GetType(), editorBaseType, noCustomTypeDesc: true);

		public EventDescriptorCollection GetEvents() => TypeDescriptor.GetEvents(GetType(), noCustomTypeDesc: true);

		public EventDescriptorCollection GetEvents(Attribute[] attributes) => TypeDescriptor.GetEvents(GetType(), attributes, noCustomTypeDesc: true);

		public object GetPropertyOwner(PropertyDescriptor pd) => this;

		public PropertyDescriptorCollection GetProperties() => GetProperties(null);

		public PropertyDescriptorCollection GetProperties(Attribute[]? attributes)
			=> new(Decls.Select(static d => (PropertyDescriptor) new SettingPropertyDescriptor(d)).ToArray());

		/// <summary>One declared setting, presented to the grid as if it were a property.</summary>
		private sealed class SettingPropertyDescriptor : PropertyDescriptor
		{
			private readonly WaterboxConfig.SettingDecl _decl;

			public SettingPropertyDescriptor(WaterboxConfig.SettingDecl decl)
				: base(decl.Key, BuildAttributes(decl))
				=> _decl = decl;

			private static Attribute[] BuildAttributes(WaterboxConfig.SettingDecl decl)
			{
				List<Attribute> attrs = [ new DisplayNameAttribute(decl.DisplayName) ];
				if (!string.IsNullOrWhiteSpace(decl.Description)) attrs.Add(new DescriptionAttribute(decl.Description));
				if (decl.EffectiveType is "enum") attrs.Add(new TypeConverterAttribute(typeof(OptionsConverter)));
				if (decl.EffectiveType is "datetime") attrs.Add(new TypeConverterAttribute(typeof(DateTimeTextConverter)));
				return attrs.ToArray();
			}

			public override Type ComponentType => typeof(WaterboxSettingsBase);

			public override bool IsReadOnly => false;

			public override Type PropertyType => _decl.ClrType;

			public override TypeConverter Converter => _decl.EffectiveType switch
			{
				"enum" => new OptionsConverter(_decl),
				"datetime" => new DateTimeTextConverter(),
				_ => base.Converter,
			};

			/// <summary>The value as kept, and as the grid edits it (a "datetime" kept as seconds is edited as a date).</summary>
			private object Kept(object component)
			{
				var values = ((WaterboxSettingsBase) component).Values;
				return _decl.Coerce(values is not null && values.TryGetValue(_decl.Key, out var v) ? v : _decl.Default);
			}

			public override object GetValue(object component) => _decl.Edited(Kept(component));

			public override void SetValue(object component, object value)
			{
				var settings = (WaterboxSettingsBase) component;
				settings.Values ??= new Dictionary<string, object>();
				settings.Values[_decl.Key] = _decl.Coerce(value);
			}

			public override void ResetValue(object component) => SetValue(component, _decl.DefaultValue);

			public override bool CanResetValue(object component) => true;

			// "should serialize" drives the grid's bolding: a value that differs from
			// the core's default is shown bold, which is the only cue that a setting
			// has been changed from what the package ships with
			public override bool ShouldSerializeValue(object component)
				=> !Equals(Kept(component), _decl.DefaultValue);
		}

		/// <summary>
		/// A "datetime" as 2000-01-01 00:00:00 in UTC whatever the computer's language, typed back the same
		/// way (or without the seconds, or as a date alone). The date list the grid opens is the usual one.
		/// </summary>
		private sealed class DateTimeTextConverter : DateTimeConverter
		{
			public override object ConvertTo(ITypeDescriptorContext context, System.Globalization.CultureInfo culture, object value, Type destinationType)
				=> destinationType == typeof(string) && value is DateTime date
					? date.ToString(WaterboxConfig.SettingDecl.DateTimeFormat, System.Globalization.CultureInfo.InvariantCulture)
					: base.ConvertTo(context, culture, value, destinationType);

			public override object ConvertFrom(ITypeDescriptorContext context, System.Globalization.CultureInfo culture, object value)
				=> value is string text
					? WaterboxConfig.SettingDecl.ParseDateTime(text)?.UtcDateTime
						?? throw new FormatException($"\"{text.Trim()}\" is not a date and time written as {WaterboxConfig.SettingDecl.DateTimeFormat}")
					: base.ConvertFrom(context, culture, value);
		}

		/// <summary>
		/// Turns an enum setting's declared options into a dropdown. The list holds the VALUES, which are
		/// what is stored; each is shown by its label, and marked when its firmware is on hand.
		/// </summary>
		private sealed class OptionsConverter : StringConverter
		{
			// null when the grid built the converter from the attribute rather
			// than from a declaration; it then knows no options and says so
			private readonly WaterboxConfig.SettingDecl? _decl;

			public OptionsConverter()
			{
			}

			public OptionsConverter(WaterboxConfig.SettingDecl decl) => _decl = decl;

			public override bool GetStandardValuesSupported(ITypeDescriptorContext context) => _decl?.Options is { Count: > 0 };

			/// <summary>The list is the whole set of legal values, so free text is not.</summary>
			public override bool GetStandardValuesExclusive(ITypeDescriptorContext context) => true;

			/// <summary>As declared - except that values whose firmware is on hand come first, in declared order.</summary>
			public override StandardValuesCollection GetStandardValues(ITypeDescriptorContext context)
			{
				var options = _decl?.Options ?? [ ];
				if (OnHandOf(context) is not { } onHand) return new((ICollection) options);
				return new(options.Where(o => onHand(o) is true).Concat(options.Where(o => onHand(o) is not true)).ToList());
			}

			public override bool CanConvertTo(ITypeDescriptorContext context, Type destinationType)
				=> destinationType == typeof(string) || base.CanConvertTo(context, destinationType);

			public override object ConvertTo(ITypeDescriptorContext context, System.Globalization.CultureInfo culture, object value, Type destinationType)
				=> destinationType == typeof(string) && _decl is not null && value is string text
					? Shown(context, text)
					: base.ConvertTo(context, culture, value, destinationType);

			/// <summary>Back from what was shown to the value: a label, a marked label, or the value itself.</summary>
			public override object ConvertFrom(ITypeDescriptorContext context, System.Globalization.CultureInfo culture, object value)
			{
				if (value is string text && _decl?.Options is { } options)
				{
					foreach (var option in options)
					{
						if (text == option || text == _decl.LabelFor(option) || text == Shown(context, option)) return option;
					}
				}
				return base.ConvertFrom(context, culture, value);
			}

			private string Shown(ITypeDescriptorContext? context, string value)
				=> _decl!.LabelFor(value) + (OnHandOf(context)?.Invoke(value) is true ? FoundMark : "");

			/// <summary>whether this setting's values have their files on hand, if the settings know</summary>
			private Func<string, bool?>? OnHandOf(ITypeDescriptorContext? context)
				=> _decl?.Name is { } name && context?.Instance is WaterboxSettingsBase { FirmwareOnHand: { } onHand }
					? value => onHand(name, value)
					: null;
		}
	}

	/// <summary>
	/// THE settings: every one shapes the machine, so all of them are part of
	/// the project's reproduction contract - merged over the package's declared
	/// defaults, delivered to the guest before Init, and recorded in the
	/// project. (There is no second, "non-sync" kind.)
	/// </summary>
	public sealed class WaterboxCoreSettings : WaterboxSettingsBase
	{
		public WaterboxCoreSettings Clone()
		{
			WaterboxCoreSettings clone = new();
			CopyTo(clone);
			return clone;
		}
	}
}
