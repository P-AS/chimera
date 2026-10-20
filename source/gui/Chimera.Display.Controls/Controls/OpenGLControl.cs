using System.Windows.Forms;

using Chimera.Common;

namespace Chimera.Display.Controls
{
	internal sealed class OpenGLControl : GraphicsControl
	{
		private readonly Action _initGLState;
		private SDL2OpenGLContext _context;

		/// <summary>keeps the standalone window responsive while nothing is being drawn, and notices it being resized</summary>
		private readonly Timer _pumpTimer;

		public OpenGLControl(Action initGLState)
		{
			_initGLState = initGLState;

			// according to OpenTK, these are the styles we want to set
			SetStyle(ControlStyles.Opaque, true);
			SetStyle(ControlStyles.UserPaint, true);
			SetStyle(ControlStyles.AllPaintingInWmPaint, true);
			SetStyle(ControlStyles.UserMouse, true);
			DoubleBuffered = false;

			if (DrawsInOwnWindow)
			{
				_pumpTimer = new() { Interval = 15 };
				_pumpTimer.Tick += (_, _) =>
				{
					SDL2OpenGLContext.PumpWindowEvents();
					FollowWindowSize();
				};
			}
		}

		public override bool DrawsInOwnWindow => SDL2OpenGLContext.UsesStandaloneWindow;

		protected override void Dispose(bool disposing)
		{
			if (disposing)
			{
				_pumpTimer?.Dispose();
				if (DrawsInOwnWindow)
				{
					// the window is the control's for its whole life, not its handle's
					_context?.Dispose();
					_context = null;
				}
			}
			base.Dispose(disposing);
		}

		protected override CreateParams CreateParams
		{
			get
			{
				const int CS_VREDRAW = 0x1;
				const int CS_HREDRAW = 0x2;
				const int CS_OWNDC = 0x20;

				var cp = base.CreateParams;
				if (!OSTailoredCode.IsUnixHost)
				{
					// According to OpenTK, this is necessary for OpenGL on windows
					cp.ClassStyle |= CS_VREDRAW | CS_HREDRAW | CS_OWNDC;
				}

				return cp;
			}
		}

		protected override void OnHandleCreated(EventArgs e)
		{
			base.OnHandleCreated(e);
			if (DrawsInOwnWindow)
			{
				CreateOwnWindow();
				return;
			}
			_context = new(Handle, 3, 2, true);
			_initGLState();
		}

		/// <summary>
		/// the standalone window doesn't need this control's handle, and the control is never shown,
		/// so the window is made when it's first needed rather than when a handle is
		/// </summary>
		private void CreateOwnWindow()
		{
			if (_context is not null) return;
			_context = new("Chimera", ClientSize.Width, ClientSize.Height, 3, 2, true);
			_pumpTimer.Start();
			_initGLState();
		}

		/// <summary>the person resized the standalone window: what is drawn follows it</summary>
		private void FollowWindowSize()
		{
			if (_context is null) return;
			var (width, height) = _context.GetWindowSize();
			if (width != ClientSize.Width || height != ClientSize.Height) ClientSize = new(width, height);
		}

		protected override void OnClientSizeChanged(EventArgs e)
		{
			base.OnClientSizeChanged(e);
			if (DrawsInOwnWindow) _context?.SetWindowSize(ClientSize.Width, ClientSize.Height);
		}

		protected override void OnHandleDestroyed(EventArgs e)
		{
			base.OnHandleDestroyed(e);
			if (DrawsInOwnWindow) return;
			_context.Dispose();
			_context = null;
		}

		private void MakeContextCurrent()
		{
			if (IsDisposed)
			{
				throw new ObjectDisposedException(nameof(OpenGLControl));
			}

			if (_context is null)
			{
				if (DrawsInOwnWindow) CreateOwnWindow();
				else CreateControl();
			}
			else
			{
				_context.MakeContextCurrent();
			}
		}

		public override void SetVsync(bool state)
		{
			MakeContextCurrent();
			_context.SetVsync(state);
		}

		public override void Begin()
			=> MakeContextCurrent();

		public override void End()
			=> SDL2OpenGLContext.MakeNoneCurrent();

		public override void SwapBuffers()
		{
			MakeContextCurrent();
			_context.SwapBuffers();
			if (DrawsInOwnWindow) SDL2OpenGLContext.PumpWindowEvents();
		}
	}
}
