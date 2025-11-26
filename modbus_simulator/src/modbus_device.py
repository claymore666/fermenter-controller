"""
MODBUS RTU Server with ESP32 timing simulation.

Provides a custom datastore that introduces realistic delays
to simulate ESP32 response characteristics.
"""

import logging
import threading
from typing import Optional

from pymodbus.datastore import (
    ModbusDeviceContext,
    ModbusServerContext,
    ModbusSequentialDataBlock
)
from pymodbus.server import StartSerialServer
from pymodbus.framer import FramerType

from .timing import ESP32Timer
from .sensor import PressureSensor

logger = logging.getLogger(__name__)


class DelayedDataBlock(ModbusSequentialDataBlock):
    """
    MODBUS data block with ESP32-simulated delays.

    Introduces realistic timing delays when reading values
    to simulate ESP32 FreeRTOS behavior.
    """

    def __init__(
        self,
        address: int,
        values: list,
        timer: Optional[ESP32Timer] = None,
        use_timing: bool = True
    ):
        """
        Initialize delayed data block.

        Args:
            address: Starting address
            values: Initial values
            timer: ESP32Timer instance for delays
            use_timing: Whether to apply timing delays
        """
        super().__init__(address, values)
        self.timer = timer
        self.use_timing = use_timing
        self._lock = threading.Lock()

    def getValues(self, address: int, count: int = 1):
        """
        Get values with simulated ESP32 delay.

        Args:
            address: Starting address
            count: Number of values to read

        Returns:
            List of values
        """
        # Apply ESP32 timing delay
        if self.use_timing and self.timer:
            self.timer.delay()

        with self._lock:
            return super().getValues(address, count)

    def setValues(self, address: int, values: list):
        """
        Set values in data block.

        Args:
            address: Starting address
            values: Values to write
        """
        with self._lock:
            super().setValues(address, values)


class ModbusDevice:
    """
    MODBUS RTU device simulator.

    Simulates a Waveshare Modbus RTU Analog Input 8CH module
    with ESP32 timing characteristics.

    Register Map (matching real hardware):
    - Input Registers (0x04): 0x0000-0x0007 - Channel 1-8 sensor data
    - Holding Registers (0x03/0x06/0x10):
      - 0x1000-0x1007 - Data type per channel
      - 0x2000 - UART parameters
      - 0x4000 - Device address
      - 0x8000 - Software version
    """

    # Input Register addresses (sensor data, read with 0x04)
    REG_INPUT_CH1 = 0x0000
    REG_INPUT_CH2 = 0x0001
    REG_INPUT_CH3 = 0x0002
    REG_INPUT_CH4 = 0x0003
    REG_INPUT_CH5 = 0x0004
    REG_INPUT_CH6 = 0x0005
    REG_INPUT_CH7 = 0x0006
    REG_INPUT_CH8 = 0x0007

    # Holding Register addresses (configuration, read/write with 0x03/0x06/0x10)
    REG_DATA_TYPE_BASE = 0x1000  # 0x1000-0x1007 for channels 1-8
    REG_UART_PARAMS = 0x2000
    REG_DEVICE_ADDR = 0x4000
    REG_SOFTWARE_VER = 0x8000

    # Data type modes
    DATA_TYPE_0_5V = 0x0000
    DATA_TYPE_1_5V = 0x0001
    DATA_TYPE_0_20MA = 0x0002
    DATA_TYPE_4_20MA = 0x0003  # Our sensors use this
    DATA_TYPE_RAW = 0x0004

    # Baud rate encoding
    BAUD_RATES = {
        0x00: 4800,
        0x01: 9600,
        0x02: 19200,
        0x03: 38400,
        0x04: 57600,
        0x05: 115200
    }

    # Parity encoding (high byte of UART param)
    PARITY_MODES = {
        0x00: 'N',  # None
        0x01: 'E',  # Even
        0x02: 'O'   # Odd
    }

    def __init__(
        self,
        sensors: list[PressureSensor],
        timer: ESP32Timer,
        slave_id: int = 1,
        use_timing: bool = True
    ):
        """
        Initialize MODBUS device.

        Args:
            sensors: List of PressureSensor instances (channels 1-2)
            timer: ESP32Timer for response delays
            slave_id: MODBUS slave ID
            use_timing: Whether to apply timing delays
        """
        self.sensors = sensors
        self.timer = timer
        self.slave_id = slave_id
        self.use_timing = use_timing
        self._running = False
        self._server_thread: Optional[threading.Thread] = None

        # Initialize Input Registers (sensor data) - addresses 0x0001-0x0008
        # pymodbus uses 1-based addressing internally (Modicon convention)
        self.input_registers = DelayedDataBlock(
            0, [0] * 9, timer, use_timing  # 9 elements, index 0 unused
        )

        # Initialize Holding Registers for configuration
        # We need to handle multiple address ranges:
        # - 0x1000-0x1007: Data types (default 0x0003 = 4-20mA)
        # - 0x2000: UART params
        # - 0x4000: Device address
        # - 0x8000: Software version

        # Create a large block to cover all addresses (simplified approach)
        # In production, use ModbusSparseDataBlock for efficiency
        hr_size = 0x8001  # Up to 0x8000
        hr_values = [0] * hr_size

        # Set default data types to 4-20mA mode
        for i in range(8):
            hr_values[0x1000 + i] = self.DATA_TYPE_4_20MA

        # Set UART params (default: 9600, N, 8, 1)
        hr_values[0x2000] = 0x0000  # Encoded UART settings

        # Set device address
        hr_values[0x4000] = slave_id

        # Set software version (e.g., 1.0 = 0x0064 as shown in docs)
        hr_values[0x8000] = 0x0064

        self.holding_registers = ModbusSequentialDataBlock(0, hr_values)

        # Store current configuration for runtime changes
        self._current_baudrate = 9600
        self._current_parity = 'N'

        # Create device context
        self.slave_context = ModbusDeviceContext(
            di=ModbusSequentialDataBlock(0, [0] * 8),  # Discrete Inputs
            co=ModbusSequentialDataBlock(0, [0] * 8),  # Coils
            hr=self.holding_registers,                 # Holding Registers (config)
            ir=self.input_registers                    # Input Registers (sensor data)
        )

        # Create server context with broadcast support (address 0)
        # Device responds to both its slave_id and broadcast address 0
        self.server_context = ModbusServerContext(
            devices={
                slave_id: self.slave_context,
                0: self.slave_context  # Broadcast address
            },
            single=False
        )

        logger.info(f"MODBUS device initialized with slave ID {slave_id}")

    def update_registers(self) -> None:
        """
        Update input registers with current sensor values.

        Should be called periodically from the sensor update loop.
        Sensor data is in Input Registers (read with function code 0x04).
        """
        for i, sensor in enumerate(self.sensors):
            state = sensor.get_state()
            self.input_registers.setValues(i + 1, [state['modbus_value']])  # 1-based

        # Set unused channels to 0
        for i in range(len(self.sensors), 8):
            self.input_registers.setValues(i + 1, [0])  # 1-based

    def start_server(
        self,
        port: str,
        baudrate: int = 9600,
        parity: str = 'N',
        stopbits: int = 1,
        timeout: float = 1.0
    ) -> None:
        """
        Start MODBUS RTU server in a separate thread.

        Args:
            port: Serial port (e.g., '/dev/ttyUSB0', 'COM3')
            baudrate: Baud rate
            parity: Parity ('N', 'E', 'O')
            stopbits: Stop bits (1 or 2)
            timeout: Response timeout in seconds
        """
        if self._running:
            logger.warning("Server already running")
            return

        self._running = True

        def run_server():
            try:
                logger.info(
                    f"Starting MODBUS RTU server on {port} "
                    f"({baudrate} {parity}{stopbits})"
                )

                # Map parity to pyserial values
                parity_map = {'N': 'N', 'E': 'E', 'O': 'O'}

                StartSerialServer(
                    context=self.server_context,
                    framer=FramerType.RTU,
                    port=port,
                    baudrate=baudrate,
                    parity=parity_map.get(parity, 'N'),
                    stopbits=stopbits,
                    timeout=timeout
                )
            except Exception as e:
                logger.error(f"MODBUS server error: {e}")
                self._running = False

        self._server_thread = threading.Thread(
            target=run_server,
            name="ModbusServer",
            daemon=True
        )
        self._server_thread.start()
        logger.info("MODBUS server thread started")

    def stop_server(self) -> None:
        """Stop MODBUS RTU server."""
        self._running = False
        # Note: pymodbus StartSerialServer doesn't have clean shutdown
        # Server will stop when thread is terminated
        logger.info("MODBUS server stop requested")

    def is_running(self) -> bool:
        """Check if server is running."""
        return self._running

    def get_register_values(self) -> list[int]:
        """
        Get current input register values (sensor data).

        Returns:
            List of 8 register values
        """
        return self.input_registers.getValues(0, 8)

    def get_data_types(self) -> list[int]:
        """
        Get data type configuration for all channels.

        Returns:
            List of 8 data type values
        """
        return self.holding_registers.getValues(0x1000, 8)

    def set_data_type(self, channel: int, data_type: int) -> None:
        """
        Set data type for a channel.

        Args:
            channel: Channel number (1-8)
            data_type: Data type (0x0000-0x0004)
        """
        if 1 <= channel <= 8 and 0 <= data_type <= 4:
            self.holding_registers.setValues(0x1000 + channel - 1, [data_type])
            logger.info(f"Channel {channel} data type set to 0x{data_type:04x}")

    def get_device_address(self) -> int:
        """Get current device address from register."""
        return self.holding_registers.getValues(0x4000, 1)[0]

    def set_device_address(self, address: int) -> None:
        """
        Set device address.

        Args:
            address: New address (1-255)
        """
        if 1 <= address <= 255:
            self.holding_registers.setValues(0x4000, [address])
            logger.info(f"Device address set to {address}")

    def get_uart_params(self) -> tuple[int, str]:
        """
        Get current UART parameters.

        Returns:
            Tuple of (baudrate, parity)
        """
        value = self.holding_registers.getValues(0x2000, 1)[0]
        parity_code = (value >> 8) & 0xFF
        baud_code = value & 0xFF

        baudrate = self.BAUD_RATES.get(baud_code, 9600)
        parity = self.PARITY_MODES.get(parity_code, 'N')

        return baudrate, parity

    def set_uart_params(self, parity_code: int, baud_code: int) -> None:
        """
        Set UART parameters.

        Args:
            parity_code: Parity (0=N, 1=E, 2=O)
            baud_code: Baud rate code (0=4800, 1=9600, 5=115200)
        """
        value = (parity_code << 8) | baud_code
        self.holding_registers.setValues(0x2000, [value])

        baudrate = self.BAUD_RATES.get(baud_code, 9600)
        parity = self.PARITY_MODES.get(parity_code, 'N')

        self._current_baudrate = baudrate
        self._current_parity = parity

        logger.info(f"UART params set: {baudrate} baud, parity {parity}")

    def get_software_version(self) -> str:
        """
        Get software version string.

        Returns:
            Version string (e.g., "V1.00")
        """
        value = self.holding_registers.getValues(0x8000, 1)[0]
        major = value // 100
        minor = value % 100
        return f"V{major}.{minor:02d}"

    def get_config_state(self) -> dict:
        """
        Get complete device configuration state.

        Returns:
            Dictionary with all configuration values
        """
        baudrate, parity = self.get_uart_params()
        return {
            'slave_id': self.slave_id,
            'device_address': self.get_device_address(),
            'baudrate': baudrate,
            'parity': parity,
            'software_version': self.get_software_version(),
            'data_types': self.get_data_types()
        }


def create_modbus_device(
    sensors: list[PressureSensor],
    timer: 'ESP32Timer',
    config
) -> ModbusDevice:
    """
    Create ModbusDevice from configuration.

    Args:
        sensors: List of PressureSensor instances
        timer: ESP32Timer instance for response delays
        config: Config instance

    Returns:
        Configured ModbusDevice instance
    """

    return ModbusDevice(
        sensors=sensors,
        timer=timer,
        slave_id=config.MODBUS_SLAVE_ID,
        use_timing=config.USE_REALISTIC_TIMING
    )
