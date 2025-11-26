"""
Tests for ESP32 timing simulation.
"""

import time
import pytest
from src.timing import ESP32Timer, TimingStats


class TestTimingStats:
    """Tests for TimingStats class."""

    def test_empty_stats(self):
        """Test stats with no data."""
        stats = TimingStats()
        result = stats.get_stats()

        assert result['avg_response_ms'] == 0.0
        assert result['total_requests'] == 0

    def test_record_single(self):
        """Test recording single value."""
        stats = TimingStats()
        stats.record(10.5)

        result = stats.get_stats()
        assert result['avg_response_ms'] == 10.5
        assert result['total_requests'] == 1

    def test_record_multiple(self):
        """Test recording multiple values."""
        stats = TimingStats()
        values = [10.0, 11.0, 12.0]
        for v in values:
            stats.record(v)

        result = stats.get_stats()
        assert result['avg_response_ms'] == 11.0
        assert result['total_requests'] == 3

    def test_histogram_data(self):
        """Test histogram data retrieval."""
        stats = TimingStats()
        for i in range(5):
            stats.record(float(i))

        data = stats.get_histogram_data()
        assert len(data) == 5
        assert data == [0.0, 1.0, 2.0, 3.0, 4.0]


class TestESP32Timer:
    """Tests for ESP32Timer class."""

    def test_initialization(self):
        """Test timer initialization."""
        timer = ESP32Timer(base_delay_ms=10, jitter_ms=2)
        assert timer.base_delay_ms == 10
        assert timer.jitter_ms == 2

    def test_delay_within_bounds(self):
        """Test that delays are within expected bounds."""
        timer = ESP32Timer(base_delay_ms=10, jitter_ms=2, seed=42)

        delays = []
        for _ in range(100):
            start = time.perf_counter()
            timer.delay()
            elapsed = (time.perf_counter() - start) * 1000
            delays.append(elapsed)

        # All delays should be >= 8ms (10 - 2) and roughly <= 15ms (10 + 2 + overhead)
        min_delay = min(delays)
        max_delay = max(delays)

        assert min_delay >= 7.0, f"Min delay {min_delay} too low"
        assert max_delay <= 20.0, f"Max delay {max_delay} too high"

    def test_reproducible_with_seed(self):
        """Test that same seed produces same results."""
        timer1 = ESP32Timer(base_delay_ms=10, jitter_ms=2, seed=42)
        timer2 = ESP32Timer(base_delay_ms=10, jitter_ms=2, seed=42)

        # Get delay values (not actual delays)
        values1 = [timer1.get_delay_value() for _ in range(10)]
        values2 = [timer2.get_delay_value() for _ in range(10)]

        assert values1 == values2

    def test_stats_recorded(self):
        """Test that delays are recorded in stats."""
        timer = ESP32Timer(base_delay_ms=5, jitter_ms=1, seed=42)

        for _ in range(10):
            timer.delay()

        stats = timer.stats.get_stats()
        assert stats['total_requests'] == 10
        assert 4.0 <= stats['avg_response_ms'] <= 10.0

    def test_zero_jitter(self):
        """Test with zero jitter."""
        timer = ESP32Timer(base_delay_ms=5, jitter_ms=0, seed=42)

        values = [timer.get_delay_value() for _ in range(10)]

        # All values should be exactly 5ms (0.005s)
        for v in values:
            assert v == 0.005

    def test_get_delay_value_no_sleep(self):
        """Test that get_delay_value doesn't actually sleep."""
        timer = ESP32Timer(base_delay_ms=100, jitter_ms=0)

        start = time.perf_counter()
        timer.get_delay_value()
        elapsed = (time.perf_counter() - start) * 1000

        # Should be nearly instant
        assert elapsed < 5.0


class TestTimingAccuracy:
    """Integration tests for timing accuracy."""

    @pytest.mark.parametrize("base_delay,jitter", [
        (10, 2),
        (5, 1),
        (20, 5),
    ])
    def test_timing_accuracy(self, base_delay, jitter):
        """Test timing accuracy across different configurations."""
        timer = ESP32Timer(base_delay_ms=base_delay, jitter_ms=jitter, seed=42)

        actual_delays = []
        for _ in range(20):
            start = time.perf_counter()
            timer.delay()
            elapsed = (time.perf_counter() - start) * 1000
            actual_delays.append(elapsed)

        avg_delay = sum(actual_delays) / len(actual_delays)

        # Average should be close to base_delay
        assert abs(avg_delay - base_delay) < jitter + 2, (
            f"Average delay {avg_delay:.2f}ms not close enough to "
            f"base delay {base_delay}ms"
        )
