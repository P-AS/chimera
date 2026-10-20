using System.Globalization;

namespace Chimera.Common
{
	/// <summary>
	/// The clock Chimera writes a build's and a core version's date in (issue 331, user-decided
	/// 2026-10-20): Central European time as Zurich keeps it. That is CET (UTC+1), and CEST
	/// (UTC+2) from 01:00 UTC on the last Sunday of March to 01:00 UTC on the last Sunday of
	/// October, the European rule. The zone is written after the time, and the release notes of
	/// Chimera and of every core give dates the same way, so a date seen here can be found there
	/// whatever clock the reader's computer keeps. The rule is worked out here rather than asked
	/// of the system's list of time zones, so every computer writes the same text for one moment.
	/// </summary>
	public static class EuropeanClock
	{
		/// <summary>A moment as <c>2026-10-18 18:35 CEST</c>: the day and minute, and the zone.</summary>
		public static string Format(DateTimeOffset when)
		{
			var utc = when.UtcDateTime;
			var summer = IsSummerTime(utc);
			return utc.AddHours(summer ? 2 : 1).ToString("yyyy-MM-dd HH:mm", CultureInfo.InvariantCulture)
				+ (summer ? " CEST" : " CET");
		}

		/// <summary>Whether a moment (in UTC) falls in Central European summer time.</summary>
		public static bool IsSummerTime(DateTime utc)
			=> utc >= LastSunday(utc.Year, 3).AddHours(1) && utc < LastSunday(utc.Year, 10).AddHours(1);

		private static DateTime LastSunday(int year, int month)
		{
			var last = new DateTime(year, month, DateTime.DaysInMonth(year, month), 0, 0, 0, DateTimeKind.Utc);
			return last.AddDays(-(int) last.DayOfWeek);
		}
	}
}
