"""
Tests for MODBUS protocol implementation.
"""

from src.sensor import PressureSensor
from src.timing import ESP32Timer
from src.modbus_device import ModbusDevice, DelayedDataBlock


class TestDelayedDataBlock:
    """Tests for DelayedDataBlock class."""

    def test_initialization(self):
        """Test data block initialization."""
        block = DelayedDataBlock(0, [0] * 8)
        values = block.getValues(0, 8)
        assert values == [0] * 8

    def test_set_and_get_values(self):
        """Test setting and getting values."""
        block = DelayedDataBlock(0, [0] * 8)

        block.setValues(0, [100])
        block.setValues(1, [200])

        assert block.getValues(0, 1) == [100]
        assert block.getValues(1, 1) == [200]
        assert block.getValues(0, 2) == [100, 200]

    def test_with_timer_delay(self):
        """Test that timer delay is applied."""
        timer = ESP32Timer(base_delay_ms=5, jitter_ms=0, seed=42)
        block = DelayedDataBlock(0, [0] * 8, timer=timer, use_timing=True)

        import time
        start = time.perf_counter()
        block.getValues(0, 1)
        elapsed = (time.perf_counter() - start) * 1000

        # Should have ~5ms delay
        assert elapsed >= 4.0

    def test_without_timing(self):
        """Test that no delay when timing disabled."""
        timer = ESP32Timer(base_delay_ms=100, jitter_ms=0)
        block = DelayedDataBlock(0, [0] * 8, timer=timer, use_timing=False)

        import time
        start = time.perf_counter()
        block.getValues(0, 1)
        elapsed = (time.perf_counter() - start) * 1000

        # Should be nearly instant
        assert elapsed < 10.0


class TestModbusDevice:
    """Tests for ModbusDevice class."""

    def test_initialization(self):
        """Test device initialization."""
        sensors = [
            PressureSensor(1, noise_percent=0),
            PressureSensor(2, noise_percent=0)
        ]
        timer = ESP32Timer(base_delay_ms=10, jitter_ms=2)

        device = ModbusDevice(
            sensors=sensors,
            timer=timer,
            slave_id=1,
            use_timing=False
        )

        assert device.slave_id == 1
        assert len(device.sensors) == 2

    def test_register_update(self):
        """Test register update from sensors."""
        sensors = [
            PressureSensor(1, noise_percent=0),
            PressureSensor(2, noise_percent=0)
        ]
        timer = ESP32Timer(base_delay_ms=10, jitter_ms=2)

        device = ModbusDevice(
            sensors=sensors,
            timer=timer,
            slave_id=1,
            use_timing=False
        )

        # Update sensors
        sensors[0].update(0.8)  # ~16383
        sensors[1].update(1.6)  # 32767

        # Update registers
        device.update_registers()

        # Check register values
        values = device.get_register_values()

        assert abs(values[0] - 16383) <= 1
        assert values[1] == 32767
        # Unused channels should be 0
        assert values[2] == 0
        assert values[7] == 0

    def test_slave_context(self):
        """Test slave context creation."""
        sensors = [
            PressureSensor(1, noise_percent=0),
            PressureSensor(2, noise_percent=0)
        ]
        timer = ESP32Timer(base_delay_ms=10, jitter_ms=2)

        device = ModbusDevice(
            sensors=sensors,
            timer=timer,
            slave_id=5,
            use_timing=False
        )

        # Verify slave context exists for slave ID 5
        assert 5 in device.server_context

    def test_multiple_register_reads(self):
        """Test reading multiple registers at once."""
        sensors = [
            PressureSensor(1, noise_percent=0),
            PressureSensor(2, noise_percent=0)
        ]
        timer = ESP32Timer(base_delay_ms=1, jitter_ms=0)

        device = ModbusDevice(
            sensors=sensors,
            timer=timer,
            slave_id=1,
            use_timing=False
        )

        sensors[0].update(0.4)
        sensors[1].update(0.8)
        device.update_registers()

        # Read all 8 input registers (sensor data uses input registers)
        values = device.input_registers.getValues(0, 8)

        assert len(values) == 8
        assert values[0] > 0  # CH1
        assert values[1] > 0  # CH2
        assert values[2] == 0  # CH3 unused


class TestModbusProtocol:
    """Tests for MODBUS protocol compliance."""

    def test_register_address_range(self):
        """Test that register addresses are within MODBUS range."""
        sensors = [
            PressureSensor(1, noise_percent=0),
            PressureSensor(2, noise_percent=0)
        ]
        timer = ESP32Timer(base_delay_ms=1, jitter_ms=0)

        device = ModbusDevice(
            sensors=sensors,
            timer=timer,
            slave_id=1,
            use_timing=False
        )

        # MODBUS holding registers start at address 0
        # Should be able to read addresses 0-7
        for addr in range(8):
            values = device.holding_registers.getValues(addr, 1)
            assert len(values) == 1

    def test_value_range(self):
        """Test that register values are within 16-bit range."""
        sensors = [
            PressureSensor(1, noise_percent=0),
            PressureSensor(2, noise_percent=0)
        ]
        timer = ESP32Timer(base_delay_ms=1, jitter_ms=0)

        device = ModbusDevice(
            sensors=sensors,
            timer=timer,
            slave_id=1,
            use_timing=False
        )

        # Test extreme values
        sensors[0].update(0.0)
        sensors[1].update(1.6)
        device.update_registers()

        values = device.get_register_values()

        for v in values:
            assert 0 <= v <= 65535  # 16-bit unsigned


class TestWaveshareRegisters:
    """Tests for Waveshare-specific register functionality."""

    def create_device(self, slave_id=1):
        """Helper to create a test device."""
        sensors = [
            PressureSensor(1, noise_percent=0),
            PressureSensor(2, noise_percent=0)
        ]
        timer = ESP32Timer(base_delay_ms=1, jitter_ms=0)
        return ModbusDevice(
            sensors=sensors,
            timer=timer,
            slave_id=slave_id,
            use_timing=False
        )

    def test_input_registers_for_sensor_data(self):
        """Test that sensor data uses input registers (0x04 function)."""
        device = self.create_device()
        device.sensors[0].update(0.8)
        device.sensors[1].update(1.2)
        device.update_registers()

        # Input registers should have sensor values
        values = device.input_registers.getValues(0, 2)
        assert values[0] > 0
        assert values[1] > 0

    def test_data_type_registers(self):
        """Test data type registers at 0x1000-0x1007."""
        device = self.create_device()

        # Get data type register values
        values = device.holding_registers.getValues(device.REG_DATA_TYPE_BASE, 8)

        # All channels default to 4-20mA (0x0003)
        for v in values:
            assert v == device.DATA_TYPE_4_20MA

    def test_uart_params_register(self):
        """Test UART params register at 0x2000."""
        device = self.create_device()

        # Default: 0x0000 (encoded UART settings)
        values = device.holding_registers.getValues(device.REG_UART_PARAMS, 1)
        assert values[0] == 0x0000

    def test_set_uart_params(self):
        """Test setting UART parameters."""
        device = self.create_device()

        # Set even parity (0x01), 19200 baud (0x02)
        device.set_uart_params(0x01, 0x02)

        values = device.holding_registers.getValues(device.REG_UART_PARAMS, 1)
        assert values[0] == 0x0102  # Even parity, 19200

    def test_device_address_register(self):
        """Test device address register at 0x4000."""
        device = self.create_device(slave_id=5)

        values = device.holding_registers.getValues(device.REG_DEVICE_ADDR, 1)
        assert values[0] == 5

    def test_set_device_address(self):
        """Test setting device address."""
        device = self.create_device()

        device.set_device_address(10)

        values = device.holding_registers.getValues(device.REG_DEVICE_ADDR, 1)
        assert values[0] == 10

    def test_software_version_register(self):
        """Test software version register at 0x8000."""
        device = self.create_device()

        values = device.holding_registers.getValues(device.REG_SOFTWARE_VER, 1)
        # Version 1.0 = 0x0064 (100 decimal)
        assert values[0] == 0x0064

    def test_broadcast_address_support(self):
        """Test that broadcast address (0) is supported."""
        device = self.create_device(slave_id=5)

        # Both slave ID and broadcast should be in context
        assert 5 in device.server_context
        assert 0 in device.server_context

    def test_get_config_state(self):
        """Test getting device configuration state."""
        device = self.create_device(slave_id=3)
        device.set_uart_params(0x02, 0x03)  # Odd parity, 38400

        state = device.get_config_state()

        assert state['slave_id'] == 3
        assert state['baudrate'] == 38400
        assert state['parity'] == 'O'
        assert state['software_version'] == 'V1.00'
        assert len(state['data_types']) == 8

    def test_baud_rate_mapping(self):
        """Test baud rate code to value mapping."""
        device = self.create_device()

        assert device.BAUD_RATES[0x00] == 4800
        assert device.BAUD_RATES[0x01] == 9600
        assert device.BAUD_RATES[0x02] == 19200
        assert device.BAUD_RATES[0x03] == 38400
        assert device.BAUD_RATES[0x04] == 57600
        assert device.BAUD_RATES[0x05] == 115200

    def test_parity_mode_mapping(self):
        """Test parity code to mode mapping."""
        device = self.create_device()

        assert device.PARITY_MODES[0x00] == 'N'
        assert device.PARITY_MODES[0x01] == 'E'
        assert device.PARITY_MODES[0x02] == 'O'

    def test_input_register_addresses(self):
        """Test input register address constants."""
        device = self.create_device()

        # Verify addresses match Waveshare documentation
        assert device.REG_INPUT_CH1 == 0x0000
        assert device.REG_INPUT_CH2 == 0x0001
        assert device.REG_INPUT_CH8 == 0x0007

    def test_holding_register_addresses(self):
        """Test holding register address constants."""
        device = self.create_device()

        # Verify addresses match Waveshare documentation
        assert device.REG_DATA_TYPE_BASE == 0x1000
        assert device.REG_UART_PARAMS == 0x2000
        assert device.REG_DEVICE_ADDR == 0x4000
        assert device.REG_SOFTWARE_VER == 0x8000
