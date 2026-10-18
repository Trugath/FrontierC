"""Leap-year day numbers for the resident calendar.

Each day of a 366-day year (February has 29 days) becomes
`(month << 5) + day`, with day counting from 1. The month names are the
three-letter labels in months.txt, in the same order.
"""
import csv
from pathlib import Path

MONTHS = ('Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec')


def calendar_days(path):
    with Path(path).open(encoding='ascii', newline='') as stream:
        rows = list(csv.DictReader(stream))
    if [row.get('month') for row in rows] != list(MONTHS):
        raise ValueError('calendar months must be Jan through Dec')
    words = []
    for index, row in enumerate(rows):
        try:
            days = int(row['days'], 10)
        except (TypeError, ValueError):
            raise ValueError(f'{row.get("month")} needs a day count') from None
        if not 1 <= days <= 31:
            raise ValueError(f'{row["month"]} has {days} days')
        words.extend((index << 5) + day for day in range(1, days + 1))
    if len(words) != 366:
        raise ValueError('the calendar must cover 366 days')
    return words
