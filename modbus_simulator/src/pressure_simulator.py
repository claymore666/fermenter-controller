"""
Time-based pressure simulation.

Simulates pressure changes from start to target at a configurable rate.
"""

import time
import threading
from .sensor import PressureSensor
from .config import SensorConfig


class PressureSimulator:
    """
    Simulates time-based pressure change from start to target.

    Supports continuous interpolation with configurable rate,
    direction changes, pause/resume, and reset functionality.
    """

    def __init__(self, sensor: PressureSensor, config: SensorConfig):
        """
        Initialize pressure simulator.

        Args:
            sensor: PressureSensor instance to update
            config: SensorConfig with simulation parameters
        """
        self.sensor = sensor
        self.start_pressure = config.start_pressure
        self.target_pressure = config.target_pressure
        self.rate_bar_per_sec = config.rate_bar_per_min / 60.0

        self.current_pressure = self.start_pressure
        self.running = False
        self.last_update = time.time()

        self._lock = threading.Lock()

        # Initialize sensor with start pressure
        self.sensor.update(self.current_pressure)

    def update(self) -> None:
        """
        Update pressure based on elapsed time.

        Should be called periodically from the sensor update loop.
        """
        with self._lock:
            if not self.running:
                return

            now = time.time()
            delta_t = now - self.last_update
            self.last_update = now

            # Calculate pressure change
            delta_p = self.rate_bar_per_sec * delta_t

            # Move toward target
            if self.current_pressure < self.target_pressure:
                self.current_pressure = min(
                    self.current_pressure + delta_p,
                    self.target_pressure
                )
            elif self.current_pressure > self.target_pressure:
                self.current_pressure = max(
                    self.current_pressure - delta_p,
                    self.target_pressure
                )

            # Update sensor
            self.sensor.update(self.current_pressure)

    def start(self) -> None:
        """Start or resume simulation."""
        with self._lock:
            self.running = True
            self.last_update = time.time()

    def stop(self) -> None:
        """Pause simulation."""
        with self._lock:
            self.running = False

    def reset(self) -> None:
        """Reset pressure to start value."""
        with self._lock:
            self.current_pressure = self.start_pressure
            self.running = False
            self.sensor.update(self.current_pressure)

    def set_target(self, target_pressure: float) -> None:
        """
        Set new target pressure.

        Args:
            target_pressure: New target pressure in bar
        """
        with self._lock:
            self.target_pressure = max(0.0, min(1.6, target_pressure))

    def set_rate(self, rate_bar_per_min: float) -> None:
        """
        Set new rate.

        Args:
            rate_bar_per_min: New rate in bar per minute
        """
        with self._lock:
            self.rate_bar_per_sec = rate_bar_per_min / 60.0

    def set_pressure(self, pressure: float) -> None:
        """
        Directly set current pressure.

        Args:
            pressure: Pressure value in bar
        """
        with self._lock:
            self.current_pressure = max(0.0, min(1.6, pressure))
            self.sensor.update(self.current_pressure)

    def apply_pressure_change(
        self,
        delta_bar: float,
        rate_bar_per_sec: float,
        duration_s: float
    ) -> None:
        """
        Apply temporary pressure change (for hub triggers).

        Args:
            delta_bar: Pressure change in bar (negative for drop)
            rate_bar_per_sec: Rate of change
            duration_s: Duration of change in seconds
        """
        with self._lock:
            # Store original target and rate
            original_target = self.target_pressure
            original_rate = self.rate_bar_per_sec

            # Apply temporary change
            temp_target = max(0.0, min(1.6, self.current_pressure + delta_bar))
            self.target_pressure = temp_target
            self.rate_bar_per_sec = rate_bar_per_sec

            # Schedule restoration (handled by caller via timer)
            self._pending_restore = (original_target, original_rate, duration_s)

    def restore_original(self) -> None:
        """Restore original target and rate after temporary change."""
        with self._lock:
            if hasattr(self, '_pending_restore'):
                original_target, original_rate, _ = self._pending_restore
                self.target_pressure = original_target
                self.rate_bar_per_sec = original_rate
                delattr(self, '_pending_restore')

    def is_running(self) -> bool:
        """Check if simulation is running."""
        with self._lock:
            return self.running

    def is_at_target(self) -> bool:
        """Check if current pressure equals target."""
        with self._lock:
            return abs(self.current_pressure - self.target_pressure) < 0.001

    def get_state(self) -> dict:
        """
        Get current simulation state.

        Returns:
            Dictionary with simulation parameters and state
        """
        with self._lock:
            return {
                'channel': self.sensor.channel,
                'start_pressure': round(self.start_pressure, 3),
                'target_pressure': round(self.target_pressure, 3),
                'current_pressure': round(self.current_pressure, 4),
                'rate_bar_per_min': round(self.rate_bar_per_sec * 60, 3),
                'running': self.running,
                'at_target': abs(self.current_pressure - self.target_pressure) < 0.001
            }
