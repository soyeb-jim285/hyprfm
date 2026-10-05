"""When will the thesis be done? A model built from my own data."""
from datetime import date, timedelta
from itertools import accumulate, count

PAGES_LEFT = 60
PAGES_PER_HOUR = 0.5      # optimistic, measured on a good day
WRITING_SHARE = 0.05      # the other 95% goes to the rice


def weekly_hours(start=40):
    """Every week, half of last week's willpower survives."""
    return accumulate(count(), lambda hours, _: hours / 2, initial=start)


def thesis_eta():
    left = PAGES_LEFT / PAGES_PER_HOUR
    for week, hours in zip(count(1), weekly_hours()):
        left -= hours * WRITING_SHARE
        if left <= 0:
            return date.today() + timedelta(weeks=week)
        if hours < 1e-9:
            # 40 * (1 + 1/2 + 1/4 + ...) * 5% = 4 hours, total, ever.
            # The thesis needs 120. A geometric series converges;
            # the thesis does not.
            return None


if __name__ == "__main__":
    print(thesis_eta() or "never (see line 22)")
