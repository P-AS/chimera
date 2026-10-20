#nullable enable

using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace Chimera.Common
{
	/// <summary>
	/// The Objective-C runtime (libobjc, part of every macOS), through which C# talks to
	/// AppKit: a class is looked up by name, a method by its selector, and every call is
	/// <c>objc_msgSend</c>. On arm64 objc_msgSend is not variadic - it takes exactly the
	/// called method's arguments - so each shape of call has its own import below.
	/// </summary>
	public static class ObjCImports
	{
		private const string OBJC = "/usr/lib/libobjc.A.dylib";

		[StructLayout(LayoutKind.Sequential)]
		public struct CGRect
		{
			public double X, Y, Width, Height;

			public CGRect(double x, double y, double width, double height)
			{
				X = x;
				Y = y;
				Width = width;
				Height = height;
			}
		}

		[DllImport(OBJC)]
		public static extern IntPtr objc_getClass(string name);

		[DllImport(OBJC)]
		public static extern IntPtr sel_registerName(string name);

		[DllImport(OBJC)]
		public static extern IntPtr objc_allocateClassPair(IntPtr superclass, string name, IntPtr extraBytes);

		[DllImport(OBJC)]
		public static extern void objc_registerClassPair(IntPtr cls);

		[DllImport(OBJC)]
		[return: MarshalAs(UnmanagedType.I1)]
		public static extern bool class_addMethod(IntPtr cls, IntPtr name, IntPtr imp, string types);

		[DllImport(OBJC, EntryPoint = "objc_msgSend")]
		public static extern IntPtr IntPtr_objc_msgSend(IntPtr receiver, IntPtr selector);

		[DllImport(OBJC, EntryPoint = "objc_msgSend")]
		public static extern IntPtr IntPtr_objc_msgSend_IntPtr(IntPtr receiver, IntPtr selector, IntPtr arg1);

		[DllImport(OBJC, EntryPoint = "objc_msgSend")]
		public static extern IntPtr IntPtr_objc_msgSend_IntPtr_IntPtr_IntPtr(IntPtr receiver, IntPtr selector, IntPtr arg1, IntPtr arg2, IntPtr arg3);

		[DllImport(OBJC, EntryPoint = "objc_msgSend")]
		public static extern IntPtr IntPtr_objc_msgSend_IntPtr_bool(IntPtr receiver, IntPtr selector, IntPtr arg1, [MarshalAs(UnmanagedType.I1)] bool arg2);

		[DllImport(OBJC, EntryPoint = "objc_msgSend")]
		public static extern IntPtr IntPtr_objc_msgSend_long(IntPtr receiver, IntPtr selector, long arg1);

		[DllImport(OBJC, EntryPoint = "objc_msgSend")]
		public static extern IntPtr IntPtr_objc_msgSend_CGRect_bool(IntPtr receiver, IntPtr selector, CGRect arg1, [MarshalAs(UnmanagedType.I1)] bool arg2);

		[DllImport(OBJC, EntryPoint = "objc_msgSend")]
		public static extern long long_objc_msgSend(IntPtr receiver, IntPtr selector);

		[DllImport(OBJC, EntryPoint = "objc_msgSend")]
		[return: MarshalAs(UnmanagedType.I1)]
		public static extern bool bool_objc_msgSend(IntPtr receiver, IntPtr selector);

		[DllImport(OBJC, EntryPoint = "objc_msgSend")]
		public static extern void void_objc_msgSend(IntPtr receiver, IntPtr selector);

		[DllImport(OBJC, EntryPoint = "objc_msgSend")]
		public static extern void void_objc_msgSend_IntPtr(IntPtr receiver, IntPtr selector, IntPtr arg1);

		[DllImport(OBJC, EntryPoint = "objc_msgSend")]
		public static extern void void_objc_msgSend_bool(IntPtr receiver, IntPtr selector, [MarshalAs(UnmanagedType.I1)] bool arg1);

		[DllImport(OBJC, EntryPoint = "objc_msgSend")]
		public static extern void void_objc_msgSend_long(IntPtr receiver, IntPtr selector, long arg1);

		/// <summary>A selector by name, e.g. <c>Sel("setTitle:")</c>.</summary>
		public static IntPtr Sel(string name) => sel_registerName(name);

		/// <summary>An NSString of <paramref name="s"/>; autoreleased, so made inside an autorelease pool.</summary>
		public static unsafe IntPtr NSString(string s)
		{
			var utf8 = Encoding.UTF8.GetBytes(s + "\0");
			fixed (byte* p = utf8) return IntPtr_objc_msgSend_IntPtr(objc_getClass("NSString"), Sel("stringWithUTF8String:"), (IntPtr) p);
		}

		/// <summary>The text of an NSString, or null for nil.</summary>
		public static unsafe string? StringFrom(IntPtr nsString)
		{
			if (nsString == IntPtr.Zero) return null;
			var p = (byte*) IntPtr_objc_msgSend(nsString, Sel("UTF8String"));
			if (p == null) return null;
			var length = 0;
			while (p[length] is not 0) length++;
			return Encoding.UTF8.GetString(p, length);
		}

		/// <summary>An NSArray of the given objects; autoreleased.</summary>
		public static IntPtr NSArray(IReadOnlyList<IntPtr> items)
		{
			var array = IntPtr_objc_msgSend(objc_getClass("NSMutableArray"), Sel("array"));
			foreach (var item in items) void_objc_msgSend_IntPtr(array, Sel("addObject:"), item);
			return array;
		}
	}
}
