namespace Chimera.Client.Common
{
	[AttributeUsage(AttributeTargets.Parameter)]
	public sealed class LuaColorParamAttribute : Attribute {}

	[AttributeUsage(AttributeTargets.Method | AttributeTargets.Class)]
	public sealed class LuaDeprecatedMethodAttribute : Attribute {}

	/// <summary>
	/// A function that is kept so scripts written for other emulators still load, but that
	/// raises an error in every Chimera core. The Lua Functions list shows it as
	/// "[Not available]", and its description says why (issue 341).
	/// </summary>
	[AttributeUsage(AttributeTargets.Method)]
	public sealed class LuaUnavailableMethodAttribute : Attribute {}

	[AttributeUsage(AttributeTargets.Method)]
	public sealed class LuaMethodAttribute : Attribute
	{
		public LuaMethodAttribute(string name, string description)
		{
			Name = name;
			Description = description;
		}

		public string Name { get; }
		public string Description { get; }
	}

	[AttributeUsage(AttributeTargets.Method)]
	public sealed class LuaMethodExampleAttribute : Attribute
	{
		public LuaMethodExampleAttribute(string example)
			=> Example = example.ReplaceLineEndings();

		public string Example { get; }
	}

	[AttributeUsage(AttributeTargets.Class)]
	public sealed class LuaLibraryAttribute : Attribute
	{
		public LuaLibraryAttribute(bool released)
		{
			Released = released;
		}

		public bool Released { get; }
	}

	/// <summary>
	/// Marks table return values that are indexed starting from zero, for documentation purposes only.
	/// </summary>
	[AttributeUsage(AttributeTargets.ReturnValue | AttributeTargets.Parameter)]
	public sealed class LuaZeroIndexedAttribute : Attribute { }
}
