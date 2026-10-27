#!/usr/bin/env python3
"""Write a moment the way Chimera shows dates (issue 331, user-decided
2026-10-20): Central European time as Zurich keeps it, with its zone, as in
"2026-10-18 18:35 CEST". CET is UTC+1; CEST, UTC+2, runs from 01:00 UTC on
the last Sunday of March to 01:00 UTC on the last Sunday of October. The rule
is worked out here rather than read from the system's time zones, so a runner
without them writes the same text.

usage: tools/european-time.py <UTC time: 2026-10-18T16:35:38Z, or Unix seconds>
"""
import datetime
import sys


def last_sunday(year, month):
    day = datetime.datetime(year + (month == 12), month % 12 + 1, 1, tzinfo=datetime.timezone.utc)
    day -= datetime.timedelta(days=1)
    return day - datetime.timedelta(days=(day.weekday() + 1) % 7)


def european(when):
    when = when.astimezone(datetime.timezone.utc)
    hour = datetime.timedelta(hours=1)
    summer = last_sunday(when.year, 3) + hour <= when < last_sunday(when.year, 10) + hour
    shown = when + (2 if summer else 1) * hour
    return shown.strftime('%Y-%m-%d %H:%M') + (' CEST' if summer else ' CET')


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__.strip().splitlines()[-1])
    text = sys.argv[1].strip()
    if text.isdigit():
        when = datetime.datetime.fromtimestamp(int(text), datetime.timezone.utc)
    else:
        when = datetime.datetime.fromisoformat(text.replace('Z', '+00:00'))
        if when.tzinfo is None:
            when = when.replace(tzinfo=datetime.timezone.utc)
    print(european(when))


if __name__ == '__main__':
    main()
