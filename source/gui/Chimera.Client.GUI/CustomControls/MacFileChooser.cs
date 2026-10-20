#nullable enable

using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Windows.Forms;

using Chimera.Common;

using static Chimera.Common.ObjCImports;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// macOS's own open and save panels (AppKit's NSOpenPanel and NSSavePanel), the ones every
	/// Mac program shows, instead of Mono's toolkit dialog. It takes the same request and gives
	/// the same answer as <see cref="PortalFileChooser"/>. See docs/folder-picker.md.
	/// </summary>
	/// <remarks>
	/// A Mac panel has no list of filters of its own, so when there is more than one a "Show:"
	/// menu under the panel lists them and changes which files can be picked. The folder
	/// picker's tick box goes in the same place.
	/// </remarks>
	internal static class MacFileChooser
	{
		private const long NSModalResponseOK = 1;
		private const long NSControlStateValueOn = 1;
		private const long NSApplicationActivationPolicyRegular = 0;
		private const long NSApplicationActivationPolicyProhibited = 2;

		/// <summary>Whether this is a Mac, and so whether files and folders go to its panels.</summary>
		public static bool Available => OSTailoredCode.CurrentOS is OSTailoredCode.DistinctOS.macOS;

		/// <summary>the panel and each filter's content types while it is up, for <see cref="FilterChanged"/>; released when it closes</summary>
		private static (IntPtr Panel, List<List<IntPtr>> ContentTypes)? _showing;

		/// <summary>
		/// Shows the panel and waits for it. It runs its own loop, so Chimera's windows are not
		/// painted meanwhile; what was done to them in that time is thrown away afterwards
		/// rather than acted on.
		/// </summary>
		public static PortalFileChooser.Outcome Show(IWin32Window? owner, PortalFileChooser.Request request, out PortalFileChooser.Answer answer)
		{
			answer = new();
			if (!Available) return PortalFileChooser.Outcome.Unavailable;
			// AppKit only answers on the process's first thread, which is the interface's here
			if (!bool_objc_msgSend(objc_getClass("NSThread"), Sel("isMainThread"))) return PortalFileChooser.Outcome.Unavailable;

			var pool = IntPtr_objc_msgSend(IntPtr_objc_msgSend(objc_getClass("NSAutoreleasePool"), Sel("alloc")), Sel("init"));
			try
			{
				var app = IntPtr_objc_msgSend(objc_getClass("NSApplication"), Sel("sharedApplication"));
				if (long_objc_msgSend(app, Sel("activationPolicy")) is NSApplicationActivationPolicyProhibited)
				{
					void_objc_msgSend_long(app, Sel("setActivationPolicy:"), NSApplicationActivationPolicyRegular);
				}
				// the interface's windows are XQuartz's: the panel is brought in front of them, and they are given back after
				var wasFront = IntPtr_objc_msgSend(IntPtr_objc_msgSend(objc_getClass("NSWorkspace"), Sel("sharedWorkspace")), Sel("frontmostApplication"));
				void_objc_msgSend_bool(app, Sel("activateIgnoringOtherApps:"), true);

				var panel = request.Save
					? IntPtr_objc_msgSend(objc_getClass("NSSavePanel"), Sel("savePanel"))
					: IntPtr_objc_msgSend(objc_getClass("NSOpenPanel"), Sel("openPanel"));
				if (panel == IntPtr.Zero) return PortalFileChooser.Outcome.Unavailable;
				Describe(panel, request, out var filters, out var checkBox);

				var response = long_objc_msgSend(panel, Sel("runModal"));

				if (wasFront != IntPtr.Zero) void_objc_msgSend_long(wasFront, Sel("activateWithOptions:"), 0);
				DiscardInputMeanwhile();

				if (response is not NSModalResponseOK) return PortalFileChooser.Outcome.Cancelled;
				answer.Paths = request.Save
					? PathsOf(IntPtr_objc_msgSend_IntPtr(objc_getClass("NSArray"), Sel("arrayWithObject:"), IntPtr_objc_msgSend(panel, Sel("URL"))))
					: PathsOf(IntPtr_objc_msgSend(panel, Sel("URLs")));
				if (filters != IntPtr.Zero) answer.FilterIndex = (int) long_objc_msgSend(filters, Sel("indexOfSelectedItem")) + 1;
				if (checkBox != IntPtr.Zero) answer.CheckBoxChecked = long_objc_msgSend(checkBox, Sel("state")) is NSControlStateValueOn;
				(owner as Form)?.Activate();
				return answer.Paths.Count is 0 ? PortalFileChooser.Outcome.Cancelled : PortalFileChooser.Outcome.Chosen;
			}
			catch (Exception e) when (e is DllNotFoundException or EntryPointNotFoundException or MarshalDirectiveException)
			{
				return PortalFileChooser.Outcome.Unavailable;
			}
			finally
			{
				if (_showing is { } shown)
				{
					foreach (var type in shown.ContentTypes.SelectMany(static t => t)) void_objc_msgSend(type, Sel("release"));
				}
				_showing = null;
				void_objc_msgSend(pool, Sel("drain"));
			}
		}

		private static void Describe(IntPtr panel, PortalFileChooser.Request request, out IntPtr filters, out IntPtr checkBox)
		{
			filters = checkBox = IntPtr.Zero;
			// an open panel shows no title, so what the request says goes in its message too
			if (request.Title.Length is not 0)
			{
				void_objc_msgSend_IntPtr(panel, Sel("setTitle:"), NSString(request.Title));
				void_objc_msgSend_IntPtr(panel, Sel("setMessage:"), NSString(request.Title));
			}
			void_objc_msgSend_bool(panel, Sel("setCanCreateDirectories:"), true);
			if (Directory.Exists(request.Folder))
			{
				var url = IntPtr_objc_msgSend_IntPtr_bool(objc_getClass("NSURL"), Sel("fileURLWithPath:isDirectory:"), NSString(request.Folder), true);
				void_objc_msgSend_IntPtr(panel, Sel("setDirectoryURL:"), url);
			}

			if (request.Save)
			{
				var name = Path.GetFileName(request.FileName);
				if (name.Length is not 0) void_objc_msgSend_IntPtr(panel, Sel("setNameFieldStringValue:"), NSString(name));
				// a name typed without an extension gets the filter's from SaveFilePicker, as from the portal
				void_objc_msgSend_bool(panel, Sel("setAllowsOtherFileTypes:"), true);
			}
			else
			{
				void_objc_msgSend_bool(panel, Sel("setCanChooseFiles:"), !request.Directory);
				void_objc_msgSend_bool(panel, Sel("setCanChooseDirectories:"), request.Directory);
				void_objc_msgSend_bool(panel, Sel("setAllowsMultipleSelection:"), request.Multiple);
			}

			IntPtr accessory;
			if (request.Directory)
			{
				if (request.CheckBoxLabel is null) return;
				checkBox = IntPtr_objc_msgSend_IntPtr_IntPtr_IntPtr(objc_getClass("NSButton"), Sel("checkboxWithTitle:target:action:"),
					NSString(request.CheckBoxLabel), IntPtr.Zero, IntPtr.Zero);
				void_objc_msgSend_long(checkBox, Sel("setState:"), request.CheckBoxChecked ? NSControlStateValueOn : 0);
				accessory = checkBox;
			}
			else
			{
				var choices = PortalFileChooser.FiltersFrom(request.Filter);
				if (choices.Count is 0) return;
				var index = request.FilterIndex >= 1 && request.FilterIndex <= choices.Count ? request.FilterIndex - 1 : 0;
				var contentTypes = choices.Select(static c => ContentTypes(c.Globs)).ToList();
				_showing = (panel, contentTypes);
				AllowContentTypes(panel, contentTypes[index]);
				if (choices.Count is 1) return;

				filters = IntPtr_objc_msgSend_CGRect_bool(IntPtr_objc_msgSend(objc_getClass("NSPopUpButton"), Sel("alloc")), Sel("initWithFrame:pullsDown:"),
					new(0, 0, 320, 26), false);
				void_objc_msgSend(filters, Sel("autorelease"));
				foreach (var (label, globs) in choices)
				{
					void_objc_msgSend_IntPtr(filters, Sel("addItemWithTitle:"), NSString(label.Length is 0 ? string.Join(", ", globs) : label));
				}
				void_objc_msgSend_long(filters, Sel("selectItemAtIndex:"), index);
				void_objc_msgSend_IntPtr(filters, Sel("setTarget:"), FilterTarget.Value);
				void_objc_msgSend_IntPtr(filters, Sel("setAction:"), Sel("filterChanged:"));
				var show = IntPtr_objc_msgSend_IntPtr(objc_getClass("NSTextField"), Sel("labelWithString:"), NSString("Show:"));
				accessory = IntPtr_objc_msgSend_IntPtr(objc_getClass("NSStackView"), Sel("stackViewWithViews:"), NSArray([ show, filters ]));
			}
			void_objc_msgSend_IntPtr(panel, Sel("setAccessoryView:"), accessory);
			if (!request.Save) void_objc_msgSend_bool(panel, Sel("setAccessoryViewDisclosed:"), true);
		}

		/// <summary>
		/// The content types (UTType) a filter's globs name: one for each <c>*.ext</c>. None -
		/// every file - when a glob is <c>*</c> or names no extension the panel could check.
		/// </summary>
		private static List<IntPtr> ContentTypes(List<string> globs)
		{
			var types = new List<IntPtr>();
			foreach (var glob in globs)
			{
				if (!glob.StartsWith("*.", StringComparison.Ordinal)) return [ ];
				var extension = glob.Substring(glob.LastIndexOf('.') + 1).ToLowerInvariant();
				if (extension.Length is 0 || extension.IndexOfAny([ '*', '?', '[' ]) >= 0) return [ ];
				var type = IntPtr_objc_msgSend_IntPtr(UTType.Value, Sel("typeWithFilenameExtension:"), NSString(extension));
				if (type == IntPtr.Zero) return [ ];
				// kept until the panel is gone: the pool would otherwise free it before a change of filter
				types.Add(IntPtr_objc_msgSend(type, Sel("retain")));
			}
			return types;
		}

		private static void AllowContentTypes(IntPtr panel, List<IntPtr> types)
			=> void_objc_msgSend_IntPtr(panel, Sel("setAllowedContentTypes:"), NSArray(types));

		/// <summary>The UTType class; its framework is loaded first, since nothing else in Chimera loads it.</summary>
		private static readonly Lazy<IntPtr> UTType = new(static () =>
		{
			_ = PosixDlfcnImports.dlopen("/System/Library/Frameworks/UniformTypeIdentifiers.framework/UniformTypeIdentifiers", PosixDlfcnImports.RTLD_NOW);
			return objc_getClass("UTType");
		});

		/// <summary>called by the "Show:" menu: the panel now allows what the filter chosen names</summary>
		[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
		private delegate void ActionHandler(IntPtr self, IntPtr selector, IntPtr sender);

		private static readonly ActionHandler FilterChangedHandler = FilterChanged;

		private static void FilterChanged(IntPtr self, IntPtr selector, IntPtr sender)
		{
			if (_showing is not { } showing) return;
			var (panel, contentTypes) = showing;
			var index = (int) long_objc_msgSend(sender, Sel("indexOfSelectedItem"));
			if (index >= 0 && index < contentTypes.Count) AllowContentTypes(panel, contentTypes[index]);
		}

		/// <summary>An object whose <c>filterChanged:</c> is <see cref="FilterChanged"/>: the "Show:" menu's target.</summary>
		private static readonly Lazy<IntPtr> FilterTarget = new(static () =>
		{
			var cls = objc_allocateClassPair(objc_getClass("NSObject"), "ChimeraFilterTarget", IntPtr.Zero);
			_ = class_addMethod(cls, Sel("filterChanged:"), Marshal.GetFunctionPointerForDelegate(FilterChangedHandler), "v@:@");
			objc_registerClassPair(cls);
			return IntPtr_objc_msgSend(IntPtr_objc_msgSend(cls, Sel("alloc")), Sel("init"));
		});

		private static string? PathOf(IntPtr url)
			=> url == IntPtr.Zero ? null : StringFrom(IntPtr_objc_msgSend(url, Sel("path")));

		private static List<string> PathsOf(IntPtr urls)
		{
			var paths = new List<string>();
			var count = long_objc_msgSend(urls, Sel("count"));
			for (var i = 0L; i < count; i++)
			{
				if (PathOf(IntPtr_objc_msgSend_long(urls, Sel("objectAtIndex:"), i)) is { } path) paths.Add(path);
			}
			return paths;
		}

		/// <summary>
		/// The panel's loop is not the interface's: clicks and keys sent to Chimera's windows while
		/// it was up are still queued. They are taken off the queue and thrown away, not acted on.
		/// </summary>
		private static void DiscardInputMeanwhile()
		{
			var blocked = new PortalFileChooser.InputBlocker();
			Application.AddMessageFilter(blocked);
			try
			{
				Application.DoEvents();
			}
			finally
			{
				Application.RemoveMessageFilter(blocked);
			}
		}
	}
}
