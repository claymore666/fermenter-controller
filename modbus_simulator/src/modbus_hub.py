"""
MODBUS Hub with promiscuous mode.

Monitors MODBUS bus for trigger events and applies pressure changes
to simulate external device interactions (e.g., solenoid valves).
"""

import logging
import threading
from typing import Optional, Callable

from .pressure_simulator import PressureSimulator

logger = logging.getLogger(__name__)


class ModbusHub:
    """
    MODBUS bus monitor for trigger-based pressure simulation.

    Monitors the MODBUS bus for specific commands (e.g., solenoid activation)
    and triggers corresponding pressure changes in the simulator.
    """

    def __init__(
        self,
        simulators: list[PressureSimulator],
        trigger_slave_id: int = 2,
        trigger_register: int = 0,
        trigger_value: int = 1,
        pressure_drop_rate: float = 0.1,
        pressure_drop_duration: float = 5.0,
        affected_sensor: int = 1
    ):
        """
        Initialize MODBUS hub.

        Args:
            simulators: List of PressureSimulator instances
            trigger_slave_id: Slave ID to monitor for triggers
            trigger_register: Register address to monitor
            trigger_value: Value that triggers pressure change
            pressure_drop_rate: Default rate of pressure change (bar/sec)
            pressure_drop_duration: Default duration of pressure change (seconds)
            affected_sensor: Which sensor to affect (1 or 2)
        """
        self.simulators = simulators
        self.trigger_slave_id = trigger_slave_id
        self.trigger_register = trigger_register
        self.trigger_value = trigger_value
        self.pressure_drop_rate = pressure_drop_rate
        self.pressure_drop_duration = pressure_drop_duration
        self.affected_sensor = affected_sensor

        self._running = False
        self._trigger_active = False
        self._monitor_thread: Optional[threading.Thread] = None
        self._restore_timer: Optional[threading.Timer] = None
        self._lock = threading.Lock()
        self._trigger_callback: Optional[Callable] = None
        self._stored_state: Optional[dict] = None  # Store original target/rate during drop

        logger.info(
            f"MODBUS hub initialized - monitoring slave {trigger_slave_id}, "
            f"register {trigger_register} for value {trigger_value}"
        )

    def set_trigger_callback(self, callback: Callable) -> None:
        """
        Set callback for trigger events (for web notifications).

        Args:
            callback: Function to call when trigger fires
        """
        self._trigger_callback = callback

    def set_drop_rate(self, rate_bar_per_sec: float) -> None:
        """
        Set the pressure drop rate.

        Args:
            rate_bar_per_sec: New drop rate in bar/second
        """
        with self._lock:
            self.pressure_drop_rate = max(0.01, rate_bar_per_sec)
            logger.info(f"Drop rate set to {self.pressure_drop_rate} bar/s")

    def trigger_pressure_drop(
        self,
        target_pressure: Optional[float] = None,
        duration_seconds: Optional[float] = None,
        rate: Optional[float] = None,
        sensor: Optional[int] = None
    ) -> None:
        """
        Trigger a pressure drop event.

        Can drop to a specific pressure value OR drop for a duration.
        If both are specified, target_pressure takes precedence.

        Args:
            target_pressure: Target pressure to drop to (bar)
            duration_seconds: Duration to drop (calculates target from rate)
            rate: Override drop rate for this trigger (bar/sec)
            sensor: Override affected sensor for this trigger
        """
        with self._lock:
            if self._trigger_active:
                # Stop the active drop
                logger.info("Stopping active pressure drop")
                if self._restore_timer:
                    self._restore_timer.cancel()
                    self._restore_timer = None
                self._trigger_active = False

                # Restore original state for affected simulator
                affected = sensor if sensor is not None else self.affected_sensor
                if 0 < affected <= len(self.simulators):
                    simulator = self.simulators[affected - 1]
                    # Stop and restore user's original values
                    simulator.stop()
                    if self._stored_state:
                        simulator.set_target(self._stored_state['target'])
                        simulator.set_rate(self._stored_state['rate'] * 60)
                        self._stored_state = None

                    if self._trigger_callback:
                        self._trigger_callback({
                            'type': 'pressure_drop_stopped',
                            'sensor': affected
                        })
                return

            self._trigger_active = True

        # Determine which sensor
        affected = sensor if sensor is not None else self.affected_sensor

        # Get affected simulator
        if 0 < affected <= len(self.simulators):
            simulator = self.simulators[affected - 1]
        else:
            logger.error(f"Invalid affected sensor: {affected}")
            with self._lock:
                self._trigger_active = False
            return

        # Store original state (user's custom values)
        original_target = simulator.target_pressure
        original_rate = simulator.rate_bar_per_sec
        self._stored_state = {'target': original_target, 'rate': original_rate}

        # Determine drop rate
        drop_rate = rate if rate is not None else self.pressure_drop_rate

        # Get current pressure
        current = simulator.current_pressure

        # Calculate drop target
        if target_pressure is not None:
            # Drop to specific pressure
            drop_target = max(0.0, min(1.6, target_pressure))
            # Calculate how long this will take
            pressure_delta = abs(current - drop_target)
            calc_duration = pressure_delta / drop_rate if drop_rate > 0 else 5.0
        elif duration_seconds is not None:
            # Drop for specified duration
            pressure_delta = drop_rate * duration_seconds
            drop_target = max(0.0, current - pressure_delta)
            calc_duration = duration_seconds
        else:
            # Default: use configured duration
            pressure_delta = drop_rate * self.pressure_drop_duration
            drop_target = max(0.0, current - pressure_delta)
            calc_duration = self.pressure_drop_duration

        logger.info(
            f"Triggering pressure drop on sensor {affected}: "
            f"{current:.3f} -> {drop_target:.3f} bar @ {drop_rate} bar/s"
        )

        # Apply pressure drop
        simulator.set_target(drop_target)
        simulator.set_rate(drop_rate * 60)  # Convert to bar/min
        simulator.start()  # Ensure simulation is running

        # Notify callback
        if self._trigger_callback:
            self._trigger_callback({
                'type': 'pressure_drop',
                'sensor': affected,
                'from_pressure': current,
                'to_pressure': drop_target,
                'rate': drop_rate,
                'duration': calc_duration
            })

        # Schedule restoration
        def restore():
            with self._lock:
                self._trigger_active = False
                self._stored_state = None

            logger.info(
                f"Restoring original target: {original_target:.3f} bar"
            )
            simulator.set_target(original_target)
            simulator.set_rate(original_rate * 60)
            simulator.stop()  # Stop so user can restart with Start button

            if self._trigger_callback:
                self._trigger_callback({
                    'type': 'pressure_restore',
                    'sensor': affected,
                    'target': original_target
                })

        # Use calculated duration plus small buffer
        restore_delay = calc_duration + 0.5
        self._restore_timer = threading.Timer(restore_delay, restore)
        self._restore_timer.start()

    def trigger_pressure_rise(self, target_bar: float = 1.2) -> None:
        """
        Trigger a pressure rise event.

        Args:
            target_bar: Target pressure to rise to
        """
        if 0 < self.affected_sensor <= len(self.simulators):
            simulator = self.simulators[self.affected_sensor - 1]
            current = simulator.current_pressure

            logger.info(
                f"Triggering pressure rise on sensor {self.affected_sensor}: "
                f"{current:.3f} -> {target_bar:.3f} bar"
            )

            simulator.set_target(target_bar)
            simulator.start()

    def start_monitoring(self) -> None:
        """
        Start promiscuous mode monitoring.

        Note: Full promiscuous mode requires raw serial access.
        This implementation provides a mock/manual trigger interface.
        """
        if self._running:
            return

        self._running = True
        logger.info("MODBUS hub monitoring started (manual trigger mode)")

    def stop_monitoring(self) -> None:
        """Stop monitoring."""
        self._running = False

        if self._restore_timer:
            self._restore_timer.cancel()
            self._restore_timer = None

        logger.info("MODBUS hub monitoring stopped")

    def is_trigger_active(self) -> bool:
        """Check if a trigger is currently active."""
        with self._lock:
            return self._trigger_active

    def get_state(self) -> dict:
        """
        Get hub state.

        Returns:
            Dictionary with hub configuration and state
        """
        with self._lock:
            return {
                'running': self._running,
                'trigger_active': self._trigger_active,
                'trigger_slave_id': self.trigger_slave_id,
                'trigger_register': self.trigger_register,
                'trigger_value': self.trigger_value,
                'pressure_drop_rate': self.pressure_drop_rate,
                'pressure_drop_duration': self.pressure_drop_duration,
                'affected_sensor': self.affected_sensor
            }


def create_hub_from_config(
    simulators: list[PressureSimulator],
    config
) -> ModbusHub:
    """
    Create ModbusHub from configuration.

    Args:
        simulators: List of PressureSimulator instances
        config: Config instance

    Returns:
        Configured ModbusHub instance
    """
    return ModbusHub(
        simulators=simulators,
        trigger_slave_id=config.TRIGGER_SLAVE_ID,
        trigger_register=config.TRIGGER_REGISTER,
        trigger_value=config.TRIGGER_VALUE,
        pressure_drop_rate=config.PRESSURE_DROP_RATE,
        pressure_drop_duration=config.PRESSURE_DROP_DURATION_S,
        affected_sensor=config.AFFECTED_SENSOR
    )
