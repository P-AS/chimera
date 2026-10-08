#nullable disable

using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;

namespace Chimera.Emulation.Common
{
	/// <summary>
	/// A generic implementation of IMemoryDomain that can be used by any core
	/// </summary>
	/// <seealso cref="IMemoryDomains" />
	public class MemoryDomainList : ReadOnlyCollection<MemoryDomain>, IMemoryDomains
	{
		private MemoryDomain _mainMemory;
		private MemoryDomain _systemBus;

		// Hidden domains (MemoryDomain.Hidden) are found by name and are not in the
		// collection, so nothing that lists domains - a menu, a drop-down, a script -
		// ever offers one.
		private readonly MemoryDomain[] _hidden;

		public bool Has(string name)
		{
			return this[name] is not null;
		}

		public MemoryDomainList(IList<MemoryDomain> domains, IDebuggable/*?*/ debuggableCore = null)
			: base(debuggableCore is null
				? domains.Where(static d => !d.Hidden).ToArray()
				: domains.Where(static d => !d.Hidden).Append(new RegistersMemoryDomain(debuggableCore)).ToArray())
		{
			_hidden = domains.Where(static d => d.Hidden).ToArray();
		}

		public MemoryDomain this[string name] => this.FirstOrDefault(x => x.Name == name) ?? _hidden.FirstOrDefault(x => x.Name == name);

		public MemoryDomain MainMemory
		{
			get => _mainMemory ?? this[0];
			set => _mainMemory = value;
		}

		public bool HasSystemBus => _systemBus != null || this.Any(x => x.Name == "System Bus");

		public MemoryDomain SystemBus
		{
			get
			{
				if (_systemBus != null)
				{
					return _systemBus;
				}

				return this.FirstOrDefault(x => x.Name == "System Bus") ?? MainMemory;
			}

			set => _systemBus = value;
		}

		/// <summary>
		/// for core use only
		/// </summary>
		public void MergeList(MemoryDomainList other)
		{
			var domains = this.ToDictionary(m => m.Name);
			foreach (var src in other)
			{
				if (domains.TryGetValue(src.Name, out var dst))
				{
					TryMerge<MemoryDomainByteArray>(dst, src, (d, s) => d.Data = s.Data);
					TryMerge<MemoryDomainIntPtr>(dst, src, (d, s) => d.Data = s.Data);
					TryMerge<MemoryDomainIntPtrSwap16>(dst, src, (d, s) => d.Data = s.Data);
					TryMerge<MemoryDomainDelegate>(dst, src, (d, s) => { d.Peek = s.Peek; d.Poke = s.Poke; });
				}
			}
		}

		/// <summary>
		/// big hacks
		/// </summary>
		/// <typeparam name="T">The memory domain type to merge</typeparam>
		private static void TryMerge<T>(MemoryDomain dest, MemoryDomain src, Action<T, T> func)
			where T : MemoryDomain
		{
			if (dest is T d1 && src is T s1)
			{
				func(d1, s1);
			}
		}
	}
}
