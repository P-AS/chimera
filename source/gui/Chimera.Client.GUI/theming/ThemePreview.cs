#nullable enable

using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

using Chimera.Client.Common;

namespace Chimera.Client.GUI
{
	/// <summary>
	/// A small drawing of Chimera in a theme that is not on: a menu bar with a menu
	/// open, a tool strip, a form with its fields, buttons and coloured notes, a list
	/// with a chosen and a hovered row, a piano roll with every kind of row, and the
	/// status bar. Config &gt; Theme shows it so a theme can be looked at before it is
	/// chosen.
	///
	/// It is a drawing and not real controls on purpose. Real controls take their
	/// colours from the theme that is ON (the walk in <see cref="ThemeEngine"/>, and
	/// half the frontend asking <see cref="ThemeLibrary.Current"/> as it paints), so
	/// a second theme in the same window would be fought over. Here every colour
	/// comes from <see cref="Previewed"/> and nothing else.
	///
	/// Pointing at a part names the colour it is painted with and its value, which
	/// is what somebody editing a theme file wants to know.
	/// </summary>
	public sealed class ThemePreview : Control, IThemedControl
	{
		/// <summary>The size the drawing is laid out at; it is scaled to fit the control, keeping its shape.</summary>
		private static readonly SizeF Design = new(520, 400);

		private readonly List<(RectangleF Area, ThemeColorRole Role)> _parts = new();

		private readonly ToolTip _tip = new() { InitialDelay = 300, ReshowDelay = 100 };

		private Theme? _previewed;

		private Color _surround = SystemColors.Control;

		private Color _frame = SystemColors.ControlDark;

		private string _pointedAt = "";

		public ThemePreview()
		{
			SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw | ControlStyles.UserPaint, true);
		}

		/// <summary>The theme the drawing is in. Null draws nothing but the surround.</summary>
		public Theme? Previewed
		{
			get => _previewed;
			set
			{
				_previewed = value;
				Invalidate();
			}
		}

		/// <summary>What the drawing paints each part with, for the tests: the part's name and the role.</summary>
		public IReadOnlyList<(RectangleF Area, ThemeColorRole Role)> Parts => _parts;

		/// <summary>The theme that is on paints only what is round the drawing: the margin and a one-pixel frame.</summary>
		public void ApplyTheme(Theme theme)
		{
			_surround = theme[ThemeColorRole.WindowBackground];
			_frame = theme[ThemeColorRole.Border];
			BackColor = _surround;
			Invalidate();
		}

		/// <summary>Where the drawing goes in the control: as large as fits, centred, the same shape as <see cref="Design"/>.</summary>
		public RectangleF DrawingBounds
		{
			get
			{
				var scale = Zoom;
				var w = Design.Width * scale;
				var h = Design.Height * scale;
				return new((ClientSize.Width - w) / 2, (ClientSize.Height - h) / 2, w, h);
			}
		}

		private float Zoom => Math.Max(0.1f, Math.Min((ClientSize.Width - 2) / Design.Width, (ClientSize.Height - 2) / Design.Height));

		protected override void OnPaint(PaintEventArgs e)
		{
			var g = e.Graphics;
			g.Clear(_surround);
			_parts.Clear();
			if (_previewed is null) return;

			var bounds = DrawingBounds;
			var state = g.Save();
			g.TranslateTransform(bounds.X, bounds.Y);
			g.ScaleTransform(Zoom, Zoom);
			g.SmoothingMode = SmoothingMode.AntiAlias;
			g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
			using Font font = new(Font.FontFamily, 11f, FontStyle.Regular, GraphicsUnit.Pixel);
			using Font bold = new(Font.FontFamily, 11f, FontStyle.Bold, GraphicsUnit.Pixel);
			using Font underlined = new(Font.FontFamily, 11f, FontStyle.Underline, GraphicsUnit.Pixel);
			Painter p = new(g, _previewed, _parts, font, bold, underlined);
			p.DrawAll();
			g.Restore(state);

			using Pen frame = new(_frame);
			g.DrawRectangle(frame, bounds.X - 1, bounds.Y - 1, bounds.Width + 1, bounds.Height + 1);
		}

		protected override void OnMouseMove(MouseEventArgs e)
		{
			base.OnMouseMove(e);
			var said = Describe(e.Location) ?? "";
			if (said == _pointedAt) return;
			_pointedAt = said;
			_tip.SetToolTip(this, said);
		}

		protected override void OnMouseLeave(EventArgs e)
		{
			base.OnMouseLeave(e);
			_pointedAt = "";
			_tip.SetToolTip(this, "");
		}

		/// <summary>The colour under a point of the control, as the tooltip says it: "MenuSelectedBackground #B4D7ED". Null off the drawing.</summary>
		public string? Describe(Point at)
		{
			if (_previewed is null) return null;
			var role = RoleAt(at);
			return role is null ? null : $"{role} {ThemeFile.FormatColor(_previewed[role.Value])}";
		}

		/// <summary>The role painted at a point of the control: the last part drawn there, which is the one on top.</summary>
		public ThemeColorRole? RoleAt(Point at)
		{
			var bounds = DrawingBounds;
			var x = (at.X - bounds.X) / Zoom;
			var y = (at.Y - bounds.Y) / Zoom;
			for (var i = _parts.Count - 1; i >= 0; i--)
			{
				if (_parts[i].Area.Contains(x, y)) return _parts[i].Role;
			}
			return null;
		}

		/// <summary>Where a point of the drawing is in the control, for the tests: the inverse of what <see cref="RoleAt"/> does.</summary>
		public Point ToControl(PointF design)
		{
			var bounds = DrawingBounds;
			return new((int) Math.Round(bounds.X + (design.X * Zoom)), (int) Math.Round(bounds.Y + (design.Y * Zoom)));
		}

		protected override void Dispose(bool disposing)
		{
			if (disposing) _tip.Dispose();
			base.Dispose(disposing);
		}

		/// <summary>
		/// The drawing itself, in the coordinates of <see cref="Design"/>. Every fill
		/// and every piece of text records the role it used, so that pointing at it
		/// can say which one that was.
		/// </summary>
		private sealed class Painter
		{
			private readonly Graphics _g;
			private readonly Theme _t;
			private readonly List<(RectangleF Area, ThemeColorRole Role)> _parts;
			private readonly Font _font;
			private readonly Font _bold;
			private readonly Font _underlined;

			public Painter(Graphics g, Theme t, List<(RectangleF, ThemeColorRole)> parts, Font font, Font bold, Font underlined)
			{
				_g = g;
				_t = t;
				_parts = parts;
				_font = font;
				_bold = bold;
				_underlined = underlined;
			}

			private void Fill(ThemeColorRole role, float x, float y, float w, float h)
			{
				using SolidBrush brush = new(_t[role]);
				_g.FillRectangle(brush, x, y, w, h);
				_parts.Add((new(x, y, w, h), role));
			}

			private void Outline(ThemeColorRole role, float x, float y, float w, float h)
			{
				using Pen pen = new(_t[role]);
				_g.DrawRectangle(pen, x, y, w, h);
				Edge(role, x, y, x + w, y);
				Edge(role, x, y + h, x + w, y + h);
				Edge(role, x, y, x, y + h);
				Edge(role, x + w, y, x + w, y + h);
			}

			private void Line(ThemeColorRole role, float x1, float y1, float x2, float y2)
			{
				using Pen pen = new(_t[role]);
				_g.DrawLine(pen, x1, y1, x2, y2);
				Edge(role, x1, y1, x2, y2);
			}

			/// <summary>A line is a part too, a unit either side of it, so that pointing at a grid line names the grid lines.</summary>
			private void Edge(ThemeColorRole role, float x1, float y1, float x2, float y2)
				=> _parts.Add((RectangleF.FromLTRB(Math.Min(x1, x2) - 1, Math.Min(y1, y2) - 1, Math.Max(x1, x2) + 1, Math.Max(y1, y2) + 1), role));

			/// <summary>Text with its top left at (x, y); recorded as a part as wide as it is.</summary>
			private float Write(ThemeColorRole role, string text, float x, float y, Font? font = null)
			{
				font ??= _font;
				using SolidBrush brush = new(_t[role]);
				var size = _g.MeasureString(text, font);
				_g.DrawString(text, font, brush, x, y);
				_parts.Add((new(x, y, size.Width, size.Height), role));
				return size.Width;
			}

			public void DrawAll()
			{
				Fill(ThemeColorRole.WindowBackground, 0, 0, 520, 400);
				DrawToolStrip();
				DrawForm();
				DrawList();
				DrawPianoRoll();
				DrawStatusBar();
				// the menu last, because it opens over everything under it
				DrawMenu();
			}

			private void DrawMenu()
			{
				Fill(ThemeColorRole.MenuBackground, 0, 0, 520, 22);
				var x = 8f;
				foreach (var title in new[] { "File", "Emulation", "View", "Config", "Tools", "Help" })
				{
					var w = _g.MeasureString(title, _font).Width + 12;
					if (title is "Config")
					{
						Fill(ThemeColorRole.MenuSelectedBackground, x, 2, w, 18);
						Write(ThemeColorRole.MenuSelectedText, title, x + 6, 5);
						OpenMenu(x, 22);
					}
					else
					{
						Write(ThemeColorRole.MenuText, title, x + 6, 5);
					}
					x += w;
				}
			}

			private void OpenMenu(float left, float top)
			{
				// short, so that it hides as little of the drawing under it as a real one would
				const float width = 120;
				string[] items = [ "Paths...", "", "Theme..." ];
				var height = 6f;
				foreach (var item in items) height += item.Length is 0 ? 7 : 20;
				Fill(ThemeColorRole.MenuBackground, left, top, width, height);
				Outline(ThemeColorRole.MenuBorder, left, top, width - 1, height - 1);
				var y = top + 3;
				foreach (var item in items)
				{
					if (item.Length is 0)
					{
						Line(ThemeColorRole.MenuSeparator, left + 6, y + 3, left + width - 6, y + 3);
						y += 7;
						continue;
					}
					if (item is "Theme...")
					{
						Fill(ThemeColorRole.MenuSelectedBackground, left + 2, y, width - 4, 20);
						Write(ThemeColorRole.MenuSelectedText, item, left + 26, y + 3);
					}
					else
					{
						Write(ThemeColorRole.MenuText, item, left + 26, y + 3);
					}
					y += 20;
				}
			}

			private void DrawToolStrip()
			{
				Fill(ThemeColorRole.ToolStripBackground, 0, 22, 520, 26);
				var x = 8f;
				foreach (var label in new[] { "Play", "Pause", "Record", "Frame Advance" })
				{
					x += Write(ThemeColorRole.ToolStripText, label, x, 29) + 16;
				}
			}

			private void DrawForm()
			{
				const float left = 10;
				const float width = 236;

				Write(ThemeColorRole.WindowText, "Project name", left, 58);
				Fill(ThemeColorRole.InputBackground, left, 74, width, 20);
				Outline(ThemeColorRole.Border, left, 74, width, 20);
				Write(ThemeColorRole.InputText, "any% in 12:34", left + 4, 77);

				// a drop-down: a field with the arrow a control draws itself
				Fill(ThemeColorRole.InputBackground, left, 100, 116, 20);
				Outline(ThemeColorRole.Border, left, 100, 116, 20);
				Write(ThemeColorRole.InputText, "Player 1", left + 4, 103);
				Arrow(ThemeColorRole.GlyphForeground, left + 104, 110);
				// ...and one that cannot be used
				Fill(ThemeColorRole.DisabledBackground, left + 120, 100, 116, 20);
				Outline(ThemeColorRole.Border, left + 120, 100, 116, 20);
				Write(ThemeColorRole.DisabledText, "Player 2", left + 124, 103);
				Arrow(ThemeColorRole.GlyphShadow, left + 224, 110);

				Fill(ThemeColorRole.ReadOnlyBackground, left, 126, width, 20);
				Outline(ThemeColorRole.Border, left, 126, width, 20);
				Write(ThemeColorRole.MutedText, "Movies/any%.tasproj (read-only)", left + 4, 129);

				Write(ThemeColorRole.LinkText, "Open Themes Folder", left, 152, _underlined);
				Write(ThemeColorRole.MutedText, "a note in the margin", left + 128, 152);

				// what a coloured word means
				Write(ThemeColorRole.AccentGood, "Verified", left, 174);
				Write(ThemeColorRole.AccentReady, "Ready", left + 62, 174);
				Write(ThemeColorRole.AccentWarning, "Out of date", left + 112, 174);
				Write(ThemeColorRole.AccentError, "Missing", left + 190, 174);

				Fill(ThemeColorRole.AccentWarningBackground, left, 194, width, 22);
				Write(ThemeColorRole.WindowText, "A firmware file is not the known one.", left + 6, 198);

				// the firmware list's marks
				Mark(ThemeColorRole.GlyphGood, left + 8, 232);
				Write(ThemeColorRole.WindowText, "Good", left + 18, 225);
				Triangle(left + 66, 232);
				Write(ThemeColorRole.WindowText, "Unknown", left + 76, 225);
				Mark(ThemeColorRole.GlyphError, left + 140, 232);
				Write(ThemeColorRole.WindowText, "Bad", left + 150, 225);
				Mark(ThemeColorRole.GlyphNeutral, left + 190, 232);
				Write(ThemeColorRole.WindowText, "None", left + 200, 225);

				Meter(left, 248, width);

				DrawButton("OK", left + 96, 342, 66);
				DrawButton("Cancel", left + 170, 342, 66);
				Fill(ThemeColorRole.ShadedBackground, left, 342, 80, 24);
				Write(ThemeColorRole.MeterText, "-12 dB", left + 6, 347);
			}

			private void Arrow(ThemeColorRole role, float cx, float cy)
			{
				using SolidBrush brush = new(_t[role]);
				_g.FillPolygon(brush, new PointF[] { new(cx - 4, cy - 2), new(cx + 4, cy - 2), new(cx, cy + 3) });
				_parts.Add((new(cx - 5, cy - 4, 10, 8), role));
			}

			private void Mark(ThemeColorRole role, float cx, float cy)
			{
				using SolidBrush brush = new(_t[role]);
				_g.FillEllipse(brush, cx - 6, cy - 6, 12, 12);
				_parts.Add((new(cx - 6, cy - 6, 12, 12), role));
			}

			private void Triangle(float cx, float cy)
			{
				using (SolidBrush brush = new(_t[ThemeColorRole.GlyphWarning]))
				{
					_g.FillPolygon(brush, new PointF[] { new(cx, cy - 7), new(cx + 7, cy + 6), new(cx - 7, cy + 6) });
				}
				_parts.Add((new(cx - 7, cy - 7, 14, 13), ThemeColorRole.GlyphWarning));
				using SolidBrush ink = new(_t[ThemeColorRole.GlyphWarningInk]);
				_g.FillRectangle(ink, cx - 1, cy - 3, 2, 5);
				_g.FillRectangle(ink, cx - 1, cy + 3, 2, 2);
				_parts.Add((new(cx - 1.5f, cy - 3, 3, 8), ThemeColorRole.GlyphWarningInk));
			}

			/// <summary>The recording level meter, and the analog-range box beside it.</summary>
			private void Meter(float left, float top, float width)
			{
				Write(ThemeColorRole.WindowText, "Level", left, top);
				const float barWidth = 140;
				Fill(ThemeColorRole.MeterTrough, left, top + 16, barWidth, 12);
				Fill(ThemeColorRole.MeterLow, left + 2, top + 18, 70, 8);
				Fill(ThemeColorRole.MeterMid, left + 72, top + 18, 30, 8);
				Fill(ThemeColorRole.MeterHigh, left + 102, top + 18, 16, 8);
				Fill(ThemeColorRole.MeterPeak, left + 126, top + 16, 3, 12);

				var box = left + width - 76;
				Fill(ThemeColorRole.AnalogRangeBackground, box, top, 76, 76);
				Fill(ThemeColorRole.AnalogRangeField, box + 6, top + 6, 64, 64);
				Line(ThemeColorRole.AnalogRangeAxis, box + 38, top + 6, box + 38, top + 70);
				Line(ThemeColorRole.AnalogRangeAxis, box + 6, top + 38, box + 70, top + 38);
				using (Pen limit = new(_t[ThemeColorRole.AnalogRangeLimit]))
				{
					_g.DrawEllipse(limit, box + 10, top + 10, 56, 56);
				}
				_parts.Add((new(box + 10, top + 10, 4, 56), ThemeColorRole.AnalogRangeLimit));
				Mark(ThemeColorRole.AnalogRangeDot, box + 52, top + 26);

				Write(ThemeColorRole.WindowText, "Hex", left, top + 36);
				Fill(ThemeColorRole.HexMenuBar, left, top + 52, barWidth, 6);
				Fill(ThemeColorRole.HexBackground, left, top + 58, barWidth, 30);
				// a byte at a time, so that what is behind a byte is behind that byte
				string[][] lines = [ [ "00", "3F", "1A", "C0", "7E", "FF" ], [ "12", "00", "00", "8B", "41", "02" ] ];
				const float pitch = 22;
				for (var row = 0; row < lines.Length; row++)
				{
					var y = top + 60 + (row * 13);
					for (var i = 0; i < lines[row].Length; i++)
					{
						var x = left + 3 + (i * pitch);
						ThemeColorRole? behind = (row, i) switch
						{
							(0, 1) => ThemeColorRole.HexHighlight,
							(0, 3) => ThemeColorRole.HexFreeze,
							(0, 5) => ThemeColorRole.HexHighlightFreeze,
							_ => null,
						};
						if (behind is { } role) Fill(role, x - 1, y, pitch - 2, 13);
						Write(ThemeColorRole.HexText, lines[row][i], x, y);
					}
				}
			}

			private void DrawButton(string text, float x, float y, float w)
			{
				Fill(ThemeColorRole.ButtonBackground, x, y, w, 24);
				Outline(ThemeColorRole.ButtonBorder, x, y, w - 1, 23);
				var size = _g.MeasureString(text, _font);
				Write(ThemeColorRole.ButtonText, text, x + ((w - size.Width) / 2), y + ((24 - size.Height) / 2));
			}

			private void DrawList()
			{
				const float left = 258;
				const float width = 252;
				const float rowHeight = 18;
				const float split = 150;
				var top = 56f;

				Fill(ThemeColorRole.HeaderBackground, left, top, width, 20);
				Write(ThemeColorRole.HeaderText, "Name", left + 6, top + 3, _bold);
				Write(ThemeColorRole.HeaderText, "System", left + split + 6, top + 3, _bold);
				Line(ThemeColorRole.GridLines, left + split, top, left + split, top + 20);
				top += 20;

				(string Name, string System, ThemeColorRole Back, ThemeColorRole Fore)[] rows =
				[
					("Aizome", "NES", ThemeColorRole.InputBackground, ThemeColorRole.InputText),
					("Bubblegum Pop", "SNES", ThemeColorRole.AlternateRowBackground, ThemeColorRole.InputText),
					("Chosen row", "N64", ThemeColorRole.Selection, ThemeColorRole.SelectionText),
					("Row under the pointer", "GBA", ThemeColorRole.HoverBackground, ThemeColorRole.InputText),
					("Chosen, list not focused", "PS2", ThemeColorRole.InactiveSelection, ThemeColorRole.InactiveSelectionText),
				];
				foreach (var row in rows)
				{
					Fill(row.Back, left, top, width, rowHeight);
					Write(row.Fore, row.Name, left + 6, top + 2);
					Write(row.Fore, row.System, left + split + 6, top + 2);
					Line(ThemeColorRole.GridLines, left, top + rowHeight - 1, left + width, top + rowHeight - 1);
					top += rowHeight;
				}
				Outline(ThemeColorRole.Border, left, 56, width, top - 56);
			}

			private void DrawPianoRoll()
			{
				const float left = 258;
				const float width = 252;
				const float top = 178;
				const float rowHeight = 14;
				const float frameColumn = 52;
				float[] columns = [ left + frameColumn, left + frameColumn + 40, left + frameColumn + 80, left + frameColumn + 120, left + frameColumn + 160 ];

				// a header and eleven rows
				const float height = 18 + (11 * rowHeight);
				Fill(ThemeColorRole.RollBackground, left, top, width, height);
				Fill(ThemeColorRole.RollColumnBackground, left, top, width, 18);
				Write(ThemeColorRole.RollText, "Frame", left + 4, top + 2, _bold);
				string[] buttons = [ "A", "B", "Up", "Start", "X" ];
				for (var i = 0; i < buttons.Length; i++)
				{
					Line(ThemeColorRole.RollColumnBorder, columns[i], top, columns[i], top + 18);
					Write(ThemeColorRole.RollText, buttons[i], columns[i] + 4, top + 2, _bold);
				}

				(int Frame, ThemeColorRole Row, string Pressed, ThemeColorRole Fore)[] rows =
				[
					(100, ThemeColorRole.TasGreenZoneInput, "", ThemeColorRole.RollText),
					(101, ThemeColorRole.TasGreenZoneInput, "A", ThemeColorRole.RollText),
					(102, ThemeColorRole.TasGreenZoneInputStated, "A B", ThemeColorRole.RollText),
					(103, ThemeColorRole.TasLagZoneInput, "", ThemeColorRole.RollText),
					(104, ThemeColorRole.TasLagZoneInput, "B", ThemeColorRole.RollText),
					(105, ThemeColorRole.TasMarker, "Up", ThemeColorRole.RollText),
					(106, ThemeColorRole.TasCurrentFrame, "A", ThemeColorRole.RollText),
					(107, ThemeColorRole.RollSelection, "Start", ThemeColorRole.RollSelectionText),
					(108, ThemeColorRole.TasGreenZoneInputInvalidated, "B", ThemeColorRole.RollText),
					(109, ThemeColorRole.TasAnalogEdit, "X", ThemeColorRole.RollText),
				];
				var y = top + 18;
				foreach (var row in rows)
				{
					Fill(row.Row, left, y, width, rowHeight);
					Write(row.Fore, row.Frame.ToString(), left + 18, y + 1);
					foreach (var pressed in row.Pressed.Split(' '))
					{
						var at = Array.IndexOf(buttons, pressed);
						if (at >= 0) Write(row.Fore, pressed, columns[at] + 4, y + 1);
					}
					if (row.Row is ThemeColorRole.TasCurrentFrame) Play(left + 6, y + (rowHeight / 2));
					if (row.Row is ThemeColorRole.TasMarker) Flag(left + 6, y + (rowHeight / 2));
					Line(ThemeColorRole.RollGridLines, left, y + rowHeight - 1, left + width, y + rowHeight - 1);
					y += rowHeight;
				}

				// the column under the cursor, and the one the roll is drawing attention to
				Fill(ThemeColorRole.TasCursorColumn, columns[3], y, 40, rowHeight);
				Fill(ThemeColorRole.RollEmphasisColumn, columns[4], y, 40, rowHeight);
				Write(ThemeColorRole.RollHintText, "end of the movie", left + 18, y + 1);
				Line(ThemeColorRole.RollGridLines, left, y + rowHeight - 1, left + width, y + rowHeight - 1);

				for (var i = 0; i < columns.Length; i++)
				{
					Line(ThemeColorRole.RollGridLines, columns[i], top + 18, columns[i], top + height);
				}

				// the frame numbers as the roll colours them: a frame the greenzone holds
				// in its own colour, the rest washed over the row; and every other
				// player's wash
				var rowTop = top + 18;
				foreach (var row in rows)
				{
					var frameRole = row.Row switch
					{
						ThemeColorRole.TasGreenZoneInput or ThemeColorRole.TasGreenZoneInputStated => ThemeColorRole.TasGreenZone,
						ThemeColorRole.TasLagZoneInput or ThemeColorRole.TasLagZoneInputStated => ThemeColorRole.TasLagZone,
						_ => ThemeColorRole.TasFrameColumnWash,
					};
					// above the row's grid line, which stays
					Fill(frameRole, left, rowTop, frameColumn, frameRole is ThemeColorRole.TasFrameColumnWash ? rowHeight : rowHeight - 1);
					if (frameRole is not ThemeColorRole.TasFrameColumnWash) Write(row.Fore, row.Frame.ToString(), left + 18, rowTop + 1);
					rowTop += rowHeight;
				}
				Fill(ThemeColorRole.TasFrameColumnWash, left, rowTop, frameColumn, top + height - rowTop);
				Fill(ThemeColorRole.TasAlternatePlayer, columns[2], top + 18, 80, height - 18);
				Outline(ThemeColorRole.RollColumnBorder, left, top, width, height);
			}

			private void Play(float x, float cy)
			{
				using SolidBrush brush = new(_t[ThemeColorRole.TasIconPlayback]);
				_g.FillPolygon(brush, new PointF[] { new(x, cy - 4), new(x + 7, cy), new(x, cy + 4) });
				_parts.Add((new(x, cy - 4, 7, 8), ThemeColorRole.TasIconPlayback));
			}

			private void Flag(float x, float cy)
			{
				using SolidBrush brush = new(_t[ThemeColorRole.TasIconMarker]);
				_g.FillRectangle(brush, x, cy - 4, 7, 6);
				_g.FillRectangle(brush, x, cy - 4, 1.5f, 9);
				_parts.Add((new(x, cy - 4, 7, 9), ThemeColorRole.TasIconMarker));
			}

			private void DrawStatusBar()
			{
				Fill(ThemeColorRole.StatusBarBackground, 0, 376, 520, 24);
				Write(ThemeColorRole.StatusBarText, "Frame 106    60 fps    Recording", 8, 381);
				using SolidBrush recording = new(_t[ThemeColorRole.TasIconRecording]);
				_g.FillEllipse(recording, 500, 382, 11, 11);
				_parts.Add((new(500, 382, 11, 11), ThemeColorRole.TasIconRecording));
			}
		}
	}
}
