#!/usr/bin/env python3
"""
MODBUS RTU Simulator - Main Entry Point

Simulates a Waveshare Modbus RTU Analog Input 8CH module on ESP32
with realistic timing characteristics for fermenter controller development.
"""

import logging
import signal
import sys
import threading
import time

import uvicorn

from src.config import load_config
from src.sensor import create_sensor_from_config
from src.pressure_simulator import PressureSimulator
from src.modbus_device import create_modbus_device
from src.modbus_hub import create_hub_from_config
from src.timing import create_timer_from_config
from src.web_server import create_web_server

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s.%(msecs)03d [%(levelname)s] %(name)s: %(message)s',
    datefmt='%Y-%m-%d %H:%M:%S'
)
logger = logging.getLogger(__name__)


class ModbusSimulator:
    """
    Main simulator orchestrator.

    Manages all threads and components for the MODBUS RTU simulator.
    """

    def __init__(self):
        """Initialize simulator."""
        self.config = load_config()
        self._shutdown_event = threading.Event()
        self._threads: list[threading.Thread] = []

        # Initialize components
        self._init_components()

    def _init_components(self) -> None:
        """Initialize all simulator components."""
        logger.info("Initializing simulator components...")

        # Create timer
        self.timer = create_timer_from_config(self.config)

        # Create sensors
        self.sensors = [
            create_sensor_from_config(1, self.config),
            create_sensor_from_config(2, self.config)
        ]

        # Create pressure simulators
        self.simulators = [
            PressureSimulator(
                self.sensors[0],
                self.config.get_sensor_config(1)
            ),
            PressureSimulator(
                self.sensors[1],
                self.config.get_sensor_config(2)
            )
        ]

        # Create MODBUS device
        self.modbus_device = create_modbus_device(
            self.sensors,
            self.timer,
            self.config
        )

        # Create hub
        self.hub = create_hub_from_config(
            self.simulators,
            self.config
        )

        # Create web server
        self.web_server = create_web_server(
            self.simulators,
            self.sensors,
            self.hub,
            self.timer,
            self.modbus_device,
            self.config
        )

        logger.info("Components initialized successfully")

    def _sensor_update_loop(self) -> None:
        """
        Sensor update thread.

        Periodically updates pressure simulators and MODBUS registers.
        """
        update_interval = self.config.SENSOR_UPDATE_RATE_MS / 1000.0
        logger.info(f"Sensor update loop started (interval: {update_interval*1000}ms)")

        while not self._shutdown_event.is_set():
            try:
                # Update pressure simulators
                for simulator in self.simulators:
                    simulator.update()

                # Update MODBUS registers
                self.modbus_device.update_registers()

                # Sleep until next update
                self._shutdown_event.wait(update_interval)

            except Exception as e:
                logger.error(f"Error in sensor update loop: {e}")
                time.sleep(0.1)

        logger.info("Sensor update loop stopped")

    def _start_web_server(self) -> None:
        """Start web server in a separate thread."""
        logger.info(
            f"Starting web server on {self.config.WEB_HOST}:{self.config.WEB_PORT}"
        )

        config = uvicorn.Config(
            self.web_server.app,
            host=self.config.WEB_HOST,
            port=self.config.WEB_PORT,
            log_level="warning"
        )
        server = uvicorn.Server(config)

        try:
            server.run()
        except Exception as e:
            logger.error(f"Web server error: {e}")

    def start(self) -> None:
        """Start all simulator threads."""
        logger.info("Starting MODBUS RTU Simulator...")

        # Auto-start simulators if configured
        for i, simulator in enumerate(self.simulators):
            sensor_config = self.config.get_sensor_config(i + 1)
            if sensor_config.auto_start:
                simulator.start()
                logger.info(f"Auto-started simulator for sensor {i + 1}")

        # Start MODBUS server
        self.modbus_device.start_server(
            port=self.config.MODBUS_PORT,
            baudrate=self.config.MODBUS_BAUDRATE,
            parity=self.config.MODBUS_PARITY,
            stopbits=self.config.MODBUS_STOPBITS
        )

        # Start hub monitoring
        if self.config.HUB_ENABLED:
            self.hub.start_monitoring()

        # Start sensor update thread
        sensor_thread = threading.Thread(
            target=self._sensor_update_loop,
            name="SensorUpdate",
            daemon=True
        )
        sensor_thread.start()
        self._threads.append(sensor_thread)

        # Start web server thread
        web_thread = threading.Thread(
            target=self._start_web_server,
            name="WebServer",
            daemon=True
        )
        web_thread.start()
        self._threads.append(web_thread)

        logger.info("Simulator started successfully")
        logger.info(f"Web interface: http://{self.config.WEB_HOST}:{self.config.WEB_PORT}")
        logger.info(f"MODBUS port: {self.config.MODBUS_PORT}")

    def stop(self) -> None:
        """Stop all simulator threads."""
        logger.info("Stopping simulator...")

        # Signal shutdown
        self._shutdown_event.set()

        # Stop components
        self.modbus_device.stop_server()
        self.hub.stop_monitoring()

        # Wait for threads
        for thread in self._threads:
            thread.join(timeout=2.0)

        logger.info("Simulator stopped")

    def run(self) -> None:
        """Run simulator until interrupted."""
        self.start()

        try:
            # Keep main thread alive
            while not self._shutdown_event.is_set():
                time.sleep(0.5)
        except KeyboardInterrupt:
            logger.info("Keyboard interrupt received")
        finally:
            self.stop()


def main():
    """Main entry point."""
    simulator = ModbusSimulator()

    # Setup signal handlers
    def signal_handler(signum, frame):
        logger.info(f"Signal {signum} received")
        simulator.stop()
        sys.exit(0)

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    # Run simulator
    simulator.run()


if __name__ == "__main__":
    main()
