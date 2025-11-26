"""
FastAPI web server with WebSocket support.

Provides REST API and real-time WebSocket updates for monitoring
and controlling the MODBUS simulator.
"""

import asyncio
import json
import logging
import time
from typing import Optional
from pathlib import Path

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.staticfiles import StaticFiles
from fastapi.responses import HTMLResponse, FileResponse
from pydantic import BaseModel

logger = logging.getLogger(__name__)


class SetTargetRequest(BaseModel):
    """Request to set target pressure."""
    sensor: int
    target: float


class SetRateRequest(BaseModel):
    """Request to set pressure rate."""
    sensor: int
    rate: float


class SetPressureRequest(BaseModel):
    """Request to set current pressure directly."""
    sensor: int
    pressure: float


class TriggerDropRequest(BaseModel):
    """Request to trigger pressure drop."""
    target_pressure: Optional[float] = None
    duration_seconds: Optional[float] = None
    rate: Optional[float] = None
    sensor: Optional[int] = None


class SetDropRateRequest(BaseModel):
    """Request to set drop rate."""
    rate: float


class SetUartParamsRequest(BaseModel):
    """Request to set UART parameters."""
    baudrate: int
    parity: str


class SetDeviceAddressRequest(BaseModel):
    """Request to set device address."""
    address: int


class WebServer:
    """
    FastAPI web server for simulator monitoring and control.

    Provides REST endpoints and WebSocket for real-time updates.
    """

    def __init__(
        self,
        simulators: list,
        sensors: list,
        hub,
        timer,
        modbus_device,
        host: str = "0.0.0.0",
        port: int = 8080,
        update_interval_ms: int = 500
    ):
        """
        Initialize web server.

        Args:
            simulators: List of PressureSimulator instances
            sensors: List of PressureSensor instances
            hub: ModbusHub instance
            timer: ESP32Timer instance
            modbus_device: ModbusDevice instance
            host: Server host
            port: Server port
            update_interval_ms: WebSocket update interval
        """
        self.simulators = simulators
        self.sensors = sensors
        self.hub = hub
        self.timer = timer
        self.modbus_device = modbus_device
        self.host = host
        self.port = port
        self.update_interval_ms = update_interval_ms

        self.app = FastAPI(title="MODBUS RTU Simulator")
        self._connected_clients: list[WebSocket] = []
        self._running = False

        # Setup routes
        self._setup_routes()
        self._setup_static_files()

    def _setup_static_files(self) -> None:
        """Setup static file serving."""
        web_dir = Path(__file__).parent.parent / "web"

        if (web_dir / "static").exists():
            self.app.mount(
                "/static",
                StaticFiles(directory=str(web_dir / "static")),
                name="static"
            )

    def _setup_routes(self) -> None:
        """Setup API routes."""

        @self.app.get("/", response_class=HTMLResponse)
        async def get_index():
            """Serve main page."""
            web_dir = Path(__file__).parent.parent / "web"
            index_path = web_dir / "templates" / "index.html"

            if index_path.exists():
                return FileResponse(str(index_path))
            return HTMLResponse("<h1>MODBUS Simulator</h1><p>Frontend not found</p>")

        @self.app.get("/api/status")
        async def get_status():
            """Get current simulator status."""
            return self._get_full_status()

        @self.app.get("/api/sensors")
        async def get_sensors():
            """Get sensor states."""
            return {
                "sensors": [s.get_state() for s in self.sensors]
            }

        @self.app.get("/api/simulators")
        async def get_simulators():
            """Get simulator states."""
            return {
                "simulators": [s.get_state() for s in self.simulators]
            }

        @self.app.get("/api/timing")
        async def get_timing():
            """Get timing statistics."""
            return self.timer.stats.get_stats()

        @self.app.get("/api/hub")
        async def get_hub():
            """Get hub state."""
            return self.hub.get_state()

        @self.app.post("/api/simulator/{sensor}/start")
        async def start_simulator(sensor: int):
            """Start simulator for sensor."""
            if 0 < sensor <= len(self.simulators):
                self.simulators[sensor - 1].start()
                return {"status": "started", "sensor": sensor}
            return {"error": "Invalid sensor"}

        @self.app.post("/api/simulator/{sensor}/stop")
        async def stop_simulator(sensor: int):
            """Stop simulator for sensor."""
            if 0 < sensor <= len(self.simulators):
                self.simulators[sensor - 1].stop()
                return {"status": "stopped", "sensor": sensor}
            return {"error": "Invalid sensor"}

        @self.app.post("/api/simulator/{sensor}/reset")
        async def reset_simulator(sensor: int):
            """Reset simulator for sensor."""
            if 0 < sensor <= len(self.simulators):
                self.simulators[sensor - 1].reset()
                return {"status": "reset", "sensor": sensor}
            return {"error": "Invalid sensor"}

        @self.app.post("/api/simulator/target")
        async def set_target(request: SetTargetRequest):
            """Set target pressure."""
            if 0 < request.sensor <= len(self.simulators):
                self.simulators[request.sensor - 1].set_target(request.target)
                return {"status": "ok", "sensor": request.sensor, "target": request.target}
            return {"error": "Invalid sensor"}

        @self.app.post("/api/simulator/rate")
        async def set_rate(request: SetRateRequest):
            """Set pressure rate."""
            if 0 < request.sensor <= len(self.simulators):
                self.simulators[request.sensor - 1].set_rate(request.rate)
                return {"status": "ok", "sensor": request.sensor, "rate": request.rate}
            return {"error": "Invalid sensor"}

        @self.app.post("/api/simulator/pressure")
        async def set_pressure(request: SetPressureRequest):
            """Set current pressure directly."""
            if 0 < request.sensor <= len(self.simulators):
                self.simulators[request.sensor - 1].set_pressure(request.pressure)
                return {"status": "ok", "sensor": request.sensor, "pressure": request.pressure}
            return {"error": "Invalid sensor"}

        @self.app.post("/api/hub/trigger")
        async def trigger_hub(request: Optional[TriggerDropRequest] = None):
            """Manually trigger pressure drop."""
            if request:
                self.hub.trigger_pressure_drop(
                    target_pressure=request.target_pressure,
                    duration_seconds=request.duration_seconds,
                    rate=request.rate,
                    sensor=request.sensor
                )
            else:
                self.hub.trigger_pressure_drop()
            return {"status": "triggered"}

        @self.app.post("/api/hub/rate")
        async def set_drop_rate(request: SetDropRateRequest):
            """Set pressure drop rate."""
            self.hub.set_drop_rate(request.rate)
            return {"status": "ok", "rate": request.rate}

        @self.app.get("/api/device/config")
        async def get_device_config():
            """Get MODBUS device configuration."""
            return self.modbus_device.get_config_state()

        @self.app.post("/api/device/uart")
        async def set_uart_params(request: SetUartParamsRequest):
            """Set UART parameters."""
            # Map baudrate to code
            baud_to_code = {v: k for k, v in self.modbus_device.BAUD_RATES.items()}
            parity_to_code = {v: k for k, v in self.modbus_device.PARITY_MODES.items()}

            baud_code = baud_to_code.get(request.baudrate, 0x01)
            parity_code = parity_to_code.get(request.parity.upper(), 0x00)

            self.modbus_device.set_uart_params(parity_code, baud_code)

            # Persist to .env
            from .config import persist_config
            persist_config('MODBUS_BAUDRATE', str(request.baudrate))
            persist_config('MODBUS_PARITY', request.parity.upper())

            return {"status": "ok", "baudrate": request.baudrate, "parity": request.parity}

        @self.app.post("/api/device/address")
        async def set_device_address(request: SetDeviceAddressRequest):
            """Set device address."""
            if 1 <= request.address <= 255:
                self.modbus_device.set_device_address(request.address)

                # Persist to .env
                from .config import persist_config
                persist_config('MODBUS_SLAVE_ID', str(request.address))

                return {"status": "ok", "address": request.address}
            return {"error": "Invalid address (must be 1-255)"}

        @self.app.websocket("/ws")
        async def websocket_endpoint(websocket: WebSocket):
            """WebSocket endpoint for real-time updates."""
            await websocket.accept()
            self._connected_clients.append(websocket)
            logger.info(f"WebSocket client connected ({len(self._connected_clients)} total)")

            try:
                # Send initial status immediately on connect
                await self._send_update(websocket)

                while True:
                    # Receive messages from client
                    try:
                        data = await asyncio.wait_for(
                            websocket.receive_text(),
                            timeout=0.1
                        )
                        await self._handle_ws_message(websocket, data)
                    except asyncio.TimeoutError:
                        pass

                    # Send updates
                    await self._send_update(websocket)
                    await asyncio.sleep(self.update_interval_ms / 1000.0)

            except WebSocketDisconnect:
                logger.debug("WebSocket disconnected normally")
            except RuntimeError as e:
                logger.debug(f"WebSocket runtime error: {e}")
            except Exception as e:
                logger.error(f"WebSocket unexpected error: {e}")
            finally:
                if websocket in self._connected_clients:
                    self._connected_clients.remove(websocket)
                logger.info(f"WebSocket client disconnected ({len(self._connected_clients)} remaining)")

    async def _handle_ws_message(self, websocket: WebSocket, data: str) -> None:
        """Handle incoming WebSocket message."""
        try:
            msg = json.loads(data)
            msg_type = msg.get("type")

            if msg_type == "set_target":
                sensor = msg.get("sensor", 1)
                target = msg.get("target", 1.0)
                if 0 < sensor <= len(self.simulators):
                    self.simulators[sensor - 1].set_target(target)

            elif msg_type == "set_rate":
                sensor = msg.get("sensor", 1)
                rate = msg.get("rate", 0.1)
                if 0 < sensor <= len(self.simulators):
                    self.simulators[sensor - 1].set_rate(rate)

            elif msg_type == "start_simulation":
                sensor = msg.get("sensor", 1)
                if 0 < sensor <= len(self.simulators):
                    self.simulators[sensor - 1].start()

            elif msg_type == "stop_simulation":
                sensor = msg.get("sensor", 1)
                if 0 < sensor <= len(self.simulators):
                    self.simulators[sensor - 1].stop()

            elif msg_type == "reset_simulation":
                sensor = msg.get("sensor", 1)
                if 0 < sensor <= len(self.simulators):
                    self.simulators[sensor - 1].reset()

            elif msg_type == "trigger_hub":
                self.hub.trigger_pressure_drop(
                    target_pressure=msg.get("target_pressure"),
                    duration_seconds=msg.get("duration_seconds"),
                    rate=msg.get("rate"),
                    sensor=msg.get("sensor")
                )

            elif msg_type == "set_drop_rate":
                rate = msg.get("rate", 0.1)
                self.hub.set_drop_rate(rate)

            elif msg_type == "set_uart_params":
                baudrate = msg.get("baudrate", 9600)
                parity = msg.get("parity", "N")
                baud_to_code = {v: k for k, v in self.modbus_device.BAUD_RATES.items()}
                parity_to_code = {v: k for k, v in self.modbus_device.PARITY_MODES.items()}
                baud_code = baud_to_code.get(baudrate, 0x01)
                parity_code = parity_to_code.get(parity.upper(), 0x00)
                self.modbus_device.set_uart_params(parity_code, baud_code)
                from .config import persist_config
                persist_config('MODBUS_BAUDRATE', str(baudrate))
                persist_config('MODBUS_PARITY', parity.upper())

            elif msg_type == "set_device_address":
                address = msg.get("address", 1)
                if 1 <= address <= 255:
                    self.modbus_device.set_device_address(address)
                    from .config import persist_config
                    persist_config('MODBUS_SLAVE_ID', str(address))

            elif msg_type == "set_fault":
                sensor = msg.get("sensor", 1)
                fault_mode = msg.get("fault_mode")  # 'wire_break', 'sensor_defect', or None
                if 0 < sensor <= len(self.sensors):
                    self.sensors[sensor - 1].set_fault(fault_mode)

        except json.JSONDecodeError:
            logger.warning(f"Invalid JSON: {data}")

    async def _send_update(self, websocket: WebSocket) -> None:
        """Send status update to WebSocket client."""
        try:
            status = self._get_full_status()
            status["type"] = "status_update"
            await websocket.send_json(status)
        except Exception as e:
            logger.debug(f"Failed to send update: {e}")

    def _get_full_status(self) -> dict:
        """Get complete simulator status."""
        return {
            "timestamp": time.time(),
            "sensors": [s.get_state() for s in self.sensors],
            "simulators": [s.get_state() for s in self.simulators],
            "timing": self.timer.stats.get_stats(),
            "hub": self.hub.get_state(),
            "device": self.modbus_device.get_config_state()
        }

    async def broadcast(self, message: dict) -> None:
        """Broadcast message to all connected clients."""
        disconnected = []

        for client in self._connected_clients:
            try:
                await client.send_json(message)
            except Exception:
                disconnected.append(client)

        for client in disconnected:
            self._connected_clients.remove(client)


def create_web_server(
    simulators: list,
    sensors: list,
    hub,
    timer,
    modbus_device,
    config
) -> WebServer:
    """
    Create WebServer from configuration.

    Args:
        simulators: List of PressureSimulator instances
        sensors: List of PressureSensor instances
        hub: ModbusHub instance
        timer: ESP32Timer instance
        modbus_device: ModbusDevice instance
        config: Config instance

    Returns:
        Configured WebServer instance
    """
    return WebServer(
        simulators=simulators,
        sensors=sensors,
        hub=hub,
        timer=timer,
        modbus_device=modbus_device,
        host=config.WEB_HOST,
        port=config.WEB_PORT,
        update_interval_ms=config.WEB_UPDATE_INTERVAL_MS
    )
