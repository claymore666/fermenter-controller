"""
Tests for sensor noise simulation.
"""

import statistics
from src.sensor import PressureSensor


class TestPressureSensor:
    """Tests for PressureSensor class."""

    def test_initialization(self):
        """Test sensor initialization."""
        sensor = PressureSensor(channel=1, noise_percent=2.0)
        assert sensor.channel == 1
        assert sensor.noise_percent == 2.0
        assert sensor.true_pressure == 0.0
        assert sensor.modbus_value == 0

    def test_pressure_to_modbus_conversion(self):
        """Test pressure to MODBUS value conversion."""
        sensor = PressureSensor(channel=1, noise_percent=0)  # No noise

        test_cases = [
            (0.0, 0),       # 0 bar = 4mA = 0
            (0.8, 16383),   # 0.8 bar = 12mA = ~16383
            (1.6, 32767),   # 1.6 bar = 20mA = 32767
        ]

        for pressure, expected in test_cases:
            sensor.update(pressure)
            # Allow small rounding error
            assert abs(sensor.modbus_value - expected) <= 1, (
                f"Pressure {pressure} bar should give ~{expected}, "
                f"got {sensor.modbus_value}"
            )

    def test_current_calculation(self):
        """Test 4-20mA current calculation."""
        sensor = PressureSensor(channel=1, noise_percent=0)

        test_cases = [
            (0.0, 4.0),
            (0.8, 12.0),
            (1.6, 20.0),
        ]

        for pressure, expected_ma in test_cases:
            sensor.update(pressure)
            assert abs(sensor.current_ma - expected_ma) < 0.01

    def test_pressure_clamping(self):
        """Test that pressure is clamped to valid range."""
        sensor = PressureSensor(channel=1, noise_percent=0)

        # Test negative pressure
        sensor.update(-0.5)
        assert sensor.true_pressure == 0.0

        # Test over-range pressure
        sensor.update(2.0)
        assert sensor.true_pressure == 1.6

    def test_gaussian_noise_distribution(self):
        """Test that Gaussian noise has correct distribution."""
        sensor = PressureSensor(
            channel=1,
            noise_percent=2.0,
            noise_type='gaussian',
            seed=42
        )

        # Sample at 1.0 bar (middle of range)
        samples = []
        for _ in range(1000):
            sensor.update(1.0)
            samples.append(sensor.measured_pressure)

        mean = statistics.mean(samples)
        stdev = statistics.stdev(samples)

        # Mean should be close to 1.0
        assert abs(mean - 1.0) < 0.01, f"Mean {mean} not close to 1.0"

        # StdDev should be approximately 2%/3 = 0.67% of 1.0 bar
        expected_stdev = 0.02 / 3 * 1.0
        assert abs(stdev - expected_stdev) < 0.005, (
            f"StdDev {stdev} not close to expected {expected_stdev}"
        )

    def test_uniform_noise_distribution(self):
        """Test that uniform noise has correct distribution."""
        sensor = PressureSensor(
            channel=1,
            noise_percent=2.0,
            noise_type='uniform',
            seed=42
        )

        samples = []
        for _ in range(1000):
            sensor.update(1.0)
            samples.append(sensor.measured_pressure)

        min_val = min(samples)
        max_val = max(samples)

        # Should be within ±2% of 1.0 bar
        assert min_val >= 0.98, f"Min {min_val} below expected range"
        assert max_val <= 1.02, f"Max {max_val} above expected range"

    def test_reproducible_noise_with_seed(self):
        """Test that same seed produces same noise."""
        sensor1 = PressureSensor(channel=1, noise_percent=2.0, seed=42)
        sensor2 = PressureSensor(channel=1, noise_percent=2.0, seed=42)

        values1 = []
        values2 = []

        for _ in range(10):
            sensor1.update(1.0)
            sensor2.update(1.0)
            values1.append(sensor1.measured_pressure)
            values2.append(sensor2.measured_pressure)

        assert values1 == values2

    def test_zero_noise(self):
        """Test sensor with zero noise."""
        sensor = PressureSensor(channel=1, noise_percent=0)

        for _ in range(10):
            sensor.update(1.0)
            assert sensor.measured_pressure == 1.0

    def test_get_state(self):
        """Test state retrieval."""
        sensor = PressureSensor(channel=1, noise_percent=0)
        sensor.update(0.5)

        state = sensor.get_state()

        assert state['channel'] == 1
        assert state['true_pressure'] == 0.5
        assert state['measured_pressure'] == 0.5
        assert 'current_ma' in state
        assert 'modbus_value' in state

    def test_set_noise_config(self):
        """Test dynamic noise configuration."""
        sensor = PressureSensor(channel=1, noise_percent=0)

        sensor.set_noise_config(noise_percent=5.0, noise_type='uniform')

        assert sensor.noise_percent == 5.0
        assert sensor.noise_type == 'uniform'


class TestNoiseStatistics:
    """Statistical tests for noise generation."""

    def test_noise_percent_accuracy(self):
        """Test that noise percentage is accurate."""
        noise_percents = [1.0, 2.0, 5.0]

        for noise_pct in noise_percents:
            sensor = PressureSensor(
                channel=1,
                noise_percent=noise_pct,
                noise_type='uniform',
                seed=42
            )

            samples = []
            for _ in range(1000):
                sensor.update(1.0)
                samples.append(sensor.measured_pressure)

            min_val = min(samples)
            max_val = max(samples)

            expected_min = 1.0 * (1 - noise_pct/100)
            expected_max = 1.0 * (1 + noise_pct/100)

            assert min_val >= expected_min - 0.001
            assert max_val <= expected_max + 0.001
