"""Check mirror of main/battery.c constants.

This file mirrors the OCV table, linear interpolation, +/-3% hysteresis,
the 2%/minute rate limit, the divider calibration and the charge/full
threshold predicates implemented in main/battery.c.

Constants mirrored from main/board_config.h (frozen contract) and the
FULL_EXIT_MV hysteresis local to battery.c: if any value in battery.c or
board_config.h changes, this test must be updated in lock step.

Run: python tests/test_battery_soc.py
"""

# --- Mirror of the OCV table in main/battery.c -----------------------------
# (pack millivolts, percent). Below the first entry clamps to 0, above the
# last clamps to 100; segments are linearly interpolated, half up.
OCV_TABLE_MV = [
    (3300,   0),
    (3450,   8),
    (3550,  15),
    (3650,  28),
    (3720,  40),
    (3780,  50),
    (3850,  62),
    (3920,  73),
    (4000,  84),
    (4080,  93),
    (4200, 100),
]

# --- Mirror of board_config.h divider / detection / charge / full config ---
DIVIDER_RATIO_MILLI = 3000
MV_OFFSET = 0
DETECT_MIN_MV = 2500
DETECT_MAX_MV = 4400
CHARGE_DETECT_MV = 4150
CHARGE_RISE_MV = 40
CHARGE_RISE_WINDOW_MS = 300000
FULL_REST_MV = 4050
FULL_REST_MS = 600000
SOC_REST_WINDOW_MS = 60000
HYSTERESIS_PERCENT = 3
MAX_CHANGE_PER_MIN = 2

# Hysteresis that leaves FULL; local to battery.c (not in the frozen header).
FULL_EXIT_MV = 4000

ONE_MINUTE_US = 60_000_000


def ocv_to_percent(mv):
    """Linear OCV interpolation, integer half-up rounding, clamped 0/100."""
    if mv <= OCV_TABLE_MV[0][0]:
        return 0
    if mv >= OCV_TABLE_MV[-1][0]:
        return 100
    for i in range(1, len(OCV_TABLE_MV)):
        hi_mv, hi_pct = OCV_TABLE_MV[i]
        lo_mv, lo_pct = OCV_TABLE_MV[i - 1]
        if mv <= hi_mv:
            span_mv = hi_mv - lo_mv
            rise_mv = mv - lo_mv
            span_pct = hi_pct - lo_pct
            pct = lo_pct + (span_pct * rise_mv + span_mv // 2) // span_mv
            return min(100, pct)
    return 100


def convert_pack_mv(pin_mv):
    """pack_mv = pin_mv * RATIO_MILLI / 1000 + OFFSET_MV, half-up rounded."""
    pack = (pin_mv * DIVIDER_RATIO_MILLI + 500) // 1000
    pack += MV_OFFSET
    return max(0, pack)


def is_detected(pack_mv):
    return DETECT_MIN_MV <= pack_mv <= DETECT_MAX_MV


def is_charging(pack_mv, rise_mv):
    """Voltage threshold, or a rise of CHARGE_RISE_MV inside the window."""
    return pack_mv >= CHARGE_DETECT_MV or rise_mv >= CHARGE_RISE_MV


def leaves_full(pack_mv):
    return pack_mv < FULL_EXIT_MV


class SocEstimator:
    """Mirror of the hysteresis + per-minute rate limiter in battery.c."""

    def __init__(self):
        self.published = None  # None == percent_valid == false
        self.last_publish_us = 0

    def update(self, now_us, raw):
        if self.published is None:
            self.published = raw
            self.last_publish_us = now_us
            return self.published

        target = self.published
        if raw > self.published + HYSTERESIS_PERCENT:
            target = raw
        elif raw + HYSTERESIS_PERCENT < self.published:
            target = raw
        if target == self.published:
            return self.published

        elapsed_min = (now_us - self.last_publish_us) // ONE_MINUTE_US
        max_delta = MAX_CHANGE_PER_MIN * elapsed_min
        diff = target - self.published
        step = max(-max_delta, min(max_delta, diff))
        if step == 0:
            return self.published
        self.published += step
        self.last_publish_us = now_us
        return self.published


def check(cond, message):
    if not cond:
        raise AssertionError(message)


def main():
    # Endpoints and contract anchor points.
    check(ocv_to_percent(3300) == 0, "3.30 V must map to 0%")
    check(ocv_to_percent(3780) == 50, "3.78 V must map to 50%")
    check(ocv_to_percent(4200) == 100, "4.20 V must map to 100%")
    check(ocv_to_percent(3200) == 0, "below 3.30 V clamps to 0%")
    check(ocv_to_percent(4250) == 100, "above 4.20 V clamps to 100%")

    # Interpolation spot check: 3300->0, 3450->8; 3375 mV -> 4%.
    check(ocv_to_percent(3375) == 4, "mid-segment interpolation mismatch")

    # Monotonic non-decreasing over the whole plausible range.
    previous = -1
    for mv in range(3200, 4301):
        current = ocv_to_percent(mv)
        check(current >= previous, f"OCV not monotonic at {mv} mV")
        previous = current

    # Divider calibration: 1:3 ratio, half-up rounding.
    check(convert_pack_mv(1380) == 4140, "divider ratio mismatch")
    check(convert_pack_mv(1111) == 3333, "divider rounding mismatch")

    # Detection window.
    check(is_detected(2500), "2.50 V edge must be detected")
    check(is_detected(4400), "4.40 V edge must be detected")
    check(not is_detected(2499), "below 2.50 V must not be detected")
    check(not is_detected(4401), "above 4.40 V must not be detected")

    # Charging boundaries.
    check(is_charging(4150, 0), ">=4.15 V must charge")
    check(not is_charging(4149, 0), "4.149 V alone must not charge")
    check(is_charging(3900, 40), ">=40 mV rise must charge")
    check(not is_charging(3900, 39), "39 mV rise must not charge")

    # FULL exit hysteresis.
    check(not leaves_full(4000), "4.00 V must still hold FULL")
    check(leaves_full(3999), "below 4.00 V leaves FULL")

    # Hysteresis + rate limit arithmetic.
    est = SocEstimator()
    check(est.update(0, 40) == 40, "first valid sample publishes directly")
    check(est.update(2_000_000, 37) == 40, "3% drop inside hysteresis must hold")
    check(est.update(4_000_000, 36) == 40, "no elapsed minute: rate limit blocks")
    check(est.update(64_000_000, 36) == 38, "one minute elapsed: max 2% drop")
    check(est.update(124_000_000, 45) == 40, "one minute elapsed: max 2% rise")

    # Evidence table: 3.20-4.25 V every 10 mV.
    print("OCV mapping (pack mV -> percent), 3.20-4.25 V every 10 mV:")
    for mv in range(3200, 4251, 10):
        print(f"  {mv / 1000:.2f} V ({mv:4d} mV) -> {ocv_to_percent(mv):3d}%")

    print("\nAll battery SOC checks passed.")


if __name__ == "__main__":
    main()
