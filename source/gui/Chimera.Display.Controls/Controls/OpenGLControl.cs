using System.Drawing;
using System.Windows.Forms;

using Chimera.Common;

namespace Chimera.Display.Controls
{
	internal sealed class OpenGLControl : GraphicsControl
	{
		private readonly Action _initGLState;
		private SDL2OpenGLContext _context;

		/// <summary>keeps the standalone window responsive while nothing is being drawn</summary>
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

			if (SDL2OpenGLContext.UsesStandaloneWindow)
			{
				_pumpTimer = new() { Interval = 15 };
				_pumpTimer.Tick += (_, _) => SDL2OpenGLContext.PumpWindowEvents();
			}
		}

		protected override void Dispose(bool disposing)
		{
			if (disposing) _pumpTimer?.Dispose();
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
			if (SDL2OpenGLContext.UsesStandaloneWindow)
			{
				_context = new("Chimera", ClientSize.Width, ClientSize.Height, 3, 2, true);
				_context.SetWindowVisible(Visible);
				_pumpTimer.Start();
			}
			else
			{
				_context = new(Handle, 3, 2, true);
			}
			_initGLState();
		}

		protected override void OnPaint(PaintEventArgs e)
		{
			if (!SDL2OpenGLContext.UsesStandaloneWindow)
			{
				base.OnPaint(e);
				return;
			}

			e.Graphics.Clear(Color.Black);
			TextRenderer.DrawText(e.Graphics, "The display is in its own window", Font, ClientRectangle, Color.Gray,
				TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
		}

		protected override void OnClientSizeChanged(EventArgs e)
		{
			base.OnClientSizeChanged(e);
			_context?.SetWindowSize(ClientSize.Width, ClientSize.Height);
		}

		protected override void OnVisibleChanged(EventArgs e)
		{
			base.OnVisibleChanged(e);
			_context?.SetWindowVisible(Visible);
		}

		protected override void OnHandleDestroyed(EventArgs e)
		{
			base.OnHandleDestroyed(e);
			_pumpTimer?.Stop();
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
				CreateControl();
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
			if (SDL2OpenGLContext.UsesStandaloneWindow) SDL2OpenGLContext.PumpWindowEvents();
		}
	}
}
