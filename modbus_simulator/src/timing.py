"""
ESP32 timing simulation utilities.

Provides functions to simulate realistic ESP32 response timing with jitter.
"""

import random
import time
from dataclasses import dataclass, field
from typing import Optional
from collections import deque
import threading


@dataclass
class TimingStats:
    """Statistics for timing measurements."""

    response_times: deque = field(default_factory=lambda: deque(maxlen=100))
    total_requests: int = 0
    _lock: threading.Lock = field(default_factory=threading.Lock)

    def record(self, response_time_ms: float) -> None:
        """Record a response time measurement."""
        with self._lock:
            self.response_times.append(response_time_ms)
            self.total_requests += 1

    def get_stats(self) -> dict:
        """Get timing statistics."""
        with self._lock:
            if not self.response_times:
                return {
                    'avg_response_ms': 0.0,
                    'min_response_ms': 0.0,
                    'max_response_ms': 0.0,
                    'jitter_ms': 0.0,
                    'total_requests': self.total_requests
                }

            times = list(self.response_times)
            avg = sum(times) / len(times)
            min_time = min(times)
            max_time = max(times)

            # Calculate jitter as standard deviation
            if len(times) > 1:
                variance = sum((t - avg) ** 2 for t in times) / len(times)
                jitter = variance ** 0.5
            else:
                jitter = 0.0

            return {
                'avg_response_ms': round(avg, 2),
                'min_response_ms': round(min_time, 2),
                'max_response_ms': round(max_time, 2),
                'jitter_ms': round(jitter, 2),
                'total_requests': self.total_requests
            }

    def get_histogram_data(self) -> list[float]:
        """Get response times for histogram visualization."""
        with self._lock:
            return list(self.response_times)


class ESP32Timer:
    """
    Simulates ESP32 timing characteristics.

    Provides realistic delays with configurable jitter to simulate
    FreeRTOS task scheduling behavior.
    """

    def __init__(
        self,
        base_delay_ms: int = 10,
        jitter_ms: int = 2,
        seed: Optional[int] = None
    ):
        """
        Initialize ESP32 timer.

        Args:
            base_delay_ms: Base response delay in milliseconds
            jitter_ms: Maximum jitter (+/-) in milliseconds
            seed: Random seed for reproducible timing
        """
        self.base_delay_ms = base_delay_ms
        self.jitter_ms = jitter_ms
        self.stats = TimingStats()

        if seed is not None:
            self._rng = random.Random(seed)
        else:
            self._rng = random.Random()

    def delay(self) -> float:
        """
        Apply simulated ESP32 response delay.

        Returns:
            Actual delay time in milliseconds
        """
        base_delay = self.base_delay_ms / 1000.0
        jitter = self._rng.uniform(-self.jitter_ms, self.jitter_ms) / 1000.0

        actual_delay = max(0, base_delay + jitter)

        start = time.perf_counter()
        time.sleep(actual_delay)
        elapsed_ms = (time.perf_counter() - start) * 1000

        self.stats.record(elapsed_ms)

        return elapsed_ms

    def get_delay_value(self) -> float:
        """
        Calculate delay value without actually sleeping.

        Returns:
            Calculated delay in seconds
        """
        base_delay = self.base_delay_ms / 1000.0
        jitter = self._rng.uniform(-self.jitter_ms, self.jitter_ms) / 1000.0
        return max(0, base_delay + jitter)


def create_timer_from_config(config) -> ESP32Timer:
    """
    Create ESP32Timer from configuration.

    Args:
        config: Config instance with timing settings

    Returns:
        Configured ESP32Timer instance
    """
    return ESP32Timer(
        base_delay_ms=config.RESPONSE_DELAY_MS,
        jitter_ms=config.RESPONSE_JITTER_MS,
        seed=config.NOISE_SEED
    )
