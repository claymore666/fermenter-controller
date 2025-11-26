"""
Pressure sensor simulation with noise.

Simulates a 0-1.6 bar pressure sensor with 4-20mA output converted to MODBUS values.
"""

import random
import threading
from typing import Optional


class PressureSensor:
    """
    Simulates a 0-1.6 bar pressure sensor with 4-20mA output.

    The sensor converts pressure to a 4-20mA signal which is then
    mapped to a MODBUS register value (0-32767).
    """

    # Sensor specifications
    MIN_PRESSURE_BAR: float = 0.0
    MAX_PRESSURE_BAR: float = 1.6
    MIN_CURRENT_MA: float = 4.0
    MAX_CURRENT_MA: float = 20.0
    MAX_MODBUS_VALUE: int = 32767

    def __init__(
        self,
        channel: int,
        noise_percent: float = 2.0,
        noise_type: str = 'gaussian',
        seed: Optional[int] = None
    ):
        """
        Initialize pressure sensor.

        Args:
            channel: Sensor channel number (1-8)
            noise_percent: Noise magnitude as percentage of value
            noise_type: Type of noise ('gaussian' or 'uniform')
            seed: Random seed for reproducible noise
        """
        self.channel = channel
        self.noise_percent = noise_percent
        self.noise_type = noise_type

        # State variables
        self.true_pressure: float = 0.0
        self.measured_pressure: float = 0.0
        self.current_ma: float = 4.0
        self.modbus_value: int = 0
        self.fault_mode: Optional[str] = None  # None, 'wire_break', 'sensor_defect'

        self._lock = threading.Lock()

        if seed is not None:
            self._rng = random.Random(seed + channel)
        else:
            self._rng = random.Random()

    def update(self, true_pressure_bar: float) -> None:
        """
        Update sensor with new true pressure value.

        Applies noise and converts to mA and MODBUS values.

        Args:
            true_pressure_bar: True pressure in bar (before noise)
        """
        with self._lock:
            # Clamp to sensor range
            self.true_pressure = max(
                self.MIN_PRESSURE_BAR,
                min(self.MAX_PRESSURE_BAR, true_pressure_bar)
            )

            # Check for fault modes
            if self.fault_mode == 'wire_break':
                self.measured_pressure = 0.0
                self.current_ma = 0.0
                self.modbus_value = 0
                return
            elif self.fault_mode == 'sensor_defect':
                self.measured_pressure = self.true_pressure
                self.current_ma = 25.0
                # 25mA is over-range, calculate modbus value
                self.modbus_value = int((25.0 - 4.0) / 16.0 * 32767)
                return

            # Normal operation: Add sensor noise
            self.measured_pressure = self._add_noise(self.true_pressure)

            # Convert to 4-20mA
            self.current_ma = self._pressure_to_current(self.measured_pressure)

            # Convert to MODBUS value
            self.modbus_value = self._current_to_modbus(self.current_ma)

    def _add_noise(self, value: float) -> float:
        """
        Add sensor noise to value.

        Args:
            value: True value without noise

        Returns:
            Value with noise applied
        """
        if self.noise_percent <= 0:
            return value

        noise_factor = self.noise_percent / 100.0

        if self.noise_type == 'gaussian':
            # 99.7% of values within ±noise_percent
            sigma = noise_factor / 3
            noise = self._rng.gauss(0, sigma)
        else:  # uniform
            noise = self._rng.uniform(-noise_factor, noise_factor)

        noisy_value = value * (1 + noise)

        # Clamp to sensor range
        return max(
            self.MIN_PRESSURE_BAR,
            min(self.MAX_PRESSURE_BAR, noisy_value)
        )

    def _pressure_to_current(self, pressure_bar: float) -> float:
        """
        Convert pressure to 4-20mA current.

        Args:
            pressure_bar: Pressure in bar

        Returns:
            Current in mA
        """
        # Linear interpolation: 0 bar = 4mA, 1.6 bar = 20mA
        ratio = pressure_bar / self.MAX_PRESSURE_BAR
        current = self.MIN_CURRENT_MA + ratio * (
            self.MAX_CURRENT_MA - self.MIN_CURRENT_MA
        )
        return current

    def _current_to_modbus(self, current_ma: float) -> int:
        """
        Convert 4-20mA current to MODBUS register value.

        Args:
            current_ma: Current in mA

        Returns:
            MODBUS register value (0-32767)
        """
        # 4-20mA → 0-32767
        ratio = (current_ma - self.MIN_CURRENT_MA) / (
            self.MAX_CURRENT_MA - self.MIN_CURRENT_MA
        )
        modbus_value = int(ratio * self.MAX_MODBUS_VALUE)

        # Clamp to valid range
        return max(0, min(self.MAX_MODBUS_VALUE, modbus_value))

    def get_state(self) -> dict:
        """
        Get current sensor state.

        Returns:
            Dictionary with all sensor values
        """
        with self._lock:
            return {
                'channel': self.channel,
                'true_pressure': round(self.true_pressure, 4),
                'measured_pressure': round(self.measured_pressure, 4),
                'current_ma': round(self.current_ma, 2),
                'modbus_value': self.modbus_value,
                'fault_mode': self.fault_mode
            }

    def set_fault(self, fault_mode: Optional[str]) -> None:
        """
        Set sensor fault mode.

        Args:
            fault_mode: 'wire_break' (0mA), 'sensor_defect' (25mA), or None (normal)
        """
        with self._lock:
            self.fault_mode = fault_mode

    def set_noise_config(
        self,
        noise_percent: Optional[float] = None,
        noise_type: Optional[str] = None
    ) -> None:
        """
        Update noise configuration.

        Args:
            noise_percent: New noise percentage
            noise_type: New noise type
        """
        with self._lock:
            if noise_percent is not None:
                self.noise_percent = noise_percent
            if noise_type is not None:
                self.noise_type = noise_type


def create_sensor_from_config(channel: int, config) -> PressureSensor:
    """
    Create PressureSensor from configuration.

    Args:
        channel: Sensor channel number
        config: Config instance

    Returns:
        Configured PressureSensor instance
    """
    return PressureSensor(
        channel=channel,
        noise_percent=config.SENSOR_NOISE_PERCENT,
        noise_type=config.NOISE_TYPE,
        seed=config.NOISE_SEED
    )
