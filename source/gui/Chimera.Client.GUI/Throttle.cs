using System.Diagnostics;
using System.Threading;

using Chimera.Client.Common;
using Chimera.Common;

//this throttle is nitsuja's fine-tuned techniques from desmume

namespace Chimera.Client.GUI
{
	public class Throttle
	{
		//if the emulator is paused then we don't need to behave as if unthrottled
		public bool signal_paused;
		public bool signal_frameAdvance;
		public bool signal_unthrottle;
		public bool signal_continuousFrameAdvancing;
		public bool signal_overrideSecondaryThrottle;

		/// <summary>
		/// Whether the frame about to run is drawn. Every frame is, except while
		/// running unthrottled (Turbo held, or Toggle Throttle), which draws one
		/// in Config.UnthrottledDrawOneIn. The frame skip setting this throttle
		/// once had is gone (user-decided, 2026-10-24): the machines Chimera runs
		/// cannot draw less to work less.
		/// </summary>
		public bool DrawNextFrame { get; private set; } = true;

		/// <summary>Frames run unthrottled since the last one drawn.</summary>
		private int _sinceDrawn;

		/// <summary>Says whether the next frame is drawn, and waits until it is due.</summary>
		public void Step(Config config, Sound sound, bool allowSleep)
		{
			//if we're paused, none of this should happen. notably, if we're
			//frame-advancing, we should be paused.
			if (signal_paused && !signal_continuousFrameAdvancing)
			{
				DrawNextFrame = true;
				_sinceDrawn = 0;
				//keep from burning CPU
				Thread.Sleep(15);
				return;
			}

			var oneIn = Math.Max(1, Math.Min(10, config.UnthrottledDrawOneIn));
			if (signal_unthrottle && oneIn > 1)
			{
				DrawNextFrame = _sinceDrawn == 0;
				_sinceDrawn = (_sinceDrawn + 1) % oneIn;
			}
			else
			{
				DrawNextFrame = true;
				_sinceDrawn = 0;
			}

			if ((signal_paused || config.ClockThrottle || signal_overrideSecondaryThrottle) && allowSleep)
			{
				SpeedThrottle(sound, signal_paused);
			}
		}

		private static ulong GetCurTime()
		{
			if (tmethod == 1)
				return (ulong)Stopwatch.GetTimestamp();
			else
				return (ulong)Environment.TickCount;
		}

		private static readonly Func<uint, uint> TimeBeginPeriod = OSTailoredCode.IsUnixHost
			? u => u
			: Win32Imports.timeBeginPeriod;

		private static readonly int tmethod;
		private static readonly ulong afsfreq;
		private static readonly ulong tfreq;

		static Throttle()
		{
			TimeBeginPeriod(1);
			if (Stopwatch.IsHighResolution)
			{
				afsfreq = (ulong)Stopwatch.Frequency;
				tmethod = 1;
			}
			else
			{
				afsfreq = 1000;
				tmethod = 0;
			}
			Util.DebugWriteLine("throttle method: {0}; resolution: {1}", tmethod, afsfreq);
			tfreq = afsfreq * 65536;
		}

		public void SetCoreFps(double desired_fps)
		{
			// Called on every pass of the loop; only a rate that changed resets the speed.
			var fps = (ulong)(65536 * desired_fps);
			if (fps == core_desiredfps) return;
			core_desiredfps = fps;
			int target_pct = pct;
			pct = -1;
			SetSpeedPercent(target_pct);
		}

		private int pct = -1;
		public void SetSpeedPercent(int percent)
		{
			//Console.WriteLine($"throttle set percent {percent}");
			if (pct == percent) return;
			pct = percent;
			float fraction = percent / 100.0f;
			desiredfps = (ulong)(core_desiredfps * fraction);
			//Console.WriteLine($"throttle set desiredfps {desiredfps}");
		}

		private ulong core_desiredfps;
		private ulong desiredfps;

		private ulong ltime;

		private void SpeedThrottle(Sound sound, bool paused)
		{
			ulong timePerFrame = tfreq / desiredfps;

			while (true)
			{
				if (signal_unthrottle)
					return;

				ulong ttime = GetCurTime();
				ulong elapsedTime = ttime - ltime;

				if (elapsedTime >= timePerFrame)
				{
					int maxMissedFrames = (int)Math.Ceiling((sound.SoundMaxBufferDeficitMs / 1000.0) / ((double)timePerFrame / afsfreq));
					if (maxMissedFrames < 3)
						maxMissedFrames = 3;

					if (elapsedTime > timePerFrame * (ulong)(1 + maxMissedFrames))
						ltime = ttime;
					else
						ltime += timePerFrame;

					return;
				}

				int sleepTime = (int)((timePerFrame - elapsedTime) * 1000 / afsfreq);
				if (sleepTime >= 2 || paused)
				{
					switch (OSTailoredCode.CurrentOS)
					{
						case OSTailoredCode.DistinctOS.Linux: //TODO repro
						case OSTailoredCode.DistinctOS.macOS:
							// The actual sleep time on OS X with Mono is generally between the request time
							// and up to 25% over. So we'll scale the sleep time back to account for that.
							sleepTime = sleepTime * 4 / 5;
							break;
						case OSTailoredCode.DistinctOS.Windows:
							// Assuming a timer period of 1 ms (i.e. TimeBeginPeriod(1)): The actual sleep time
							// on Windows XP is generally within a half millisecond either way of the requested
							// time. The actual sleep time on Windows 8 is generally between the requested time
							// and up to a millisecond over. So we'll subtract 1 ms from the time to avoid
							// sleeping longer than desired.
							sleepTime -= 1;
							break;
					}

					Thread.Sleep(Math.Max(sleepTime, 1));
				}
				else if (sleepTime > 0) // spin for <1 millisecond waits
				{
					Thread.Yield(); // limit to other threads on the same CPU core for other short waits
				}
			}
		}
	}
}
