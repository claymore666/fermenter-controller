"""
Configuration management using Pydantic Settings.

Loads configuration from environment variables and .env file with type validation.
"""

from typing import Literal, Optional
from pydantic import Field, field_validator
from pydantic_settings import BaseSettings, SettingsConfigDict


class SensorConfig(BaseSettings):
    """Configuration for a single pressure sensor channel."""

    enabled: bool = True
    start_pressure: float = 0.0
    target_pressure: float = 1.2
    rate_bar_per_min: float = 0.1
    auto_start: bool = False

    @field_validator('start_pressure', 'target_pressure')
    @classmethod
    def validate_pressure_range(cls, v: float) -> float:
        """Validate pressure is within sensor range (0-1.6 bar)."""
        if not 0 <= v <= 1.6:
            raise ValueError(f"Pressure must be between 0 and 1.6 bar, got {v}")
        return v

    @field_validator('rate_bar_per_min')
    @classmethod
    def validate_rate_positive(cls, v: float) -> float:
        """Validate rate is positive."""
        if v <= 0:
            raise ValueError(f"Rate must be positive, got {v}")
        return v


class Config(BaseSettings):
    """
    Main configuration for MODBUS RTU Simulator.

    All settings can be configured via environment variables or .env file.
    """

    model_config = SettingsConfigDict(
        env_file='.env',
        env_file_encoding='utf-8',
        extra='ignore'
    )

    # Serial Port Configuration
    MODBUS_PORT: str = "/dev/ttyUSB0"
    MODBUS_BAUDRATE: int = 9600
    MODBUS_PARITY: Literal['N', 'E', 'O'] = 'N'
    MODBUS_STOPBITS: int = Field(default=1, ge=1, le=2)
    MODBUS_SLAVE_ID: int = Field(default=1, ge=1, le=247)

    # ESP32 Timing Simulation
    RESPONSE_DELAY_MS: int = Field(default=10, ge=0)
    RESPONSE_JITTER_MS: int = Field(default=2, ge=0)
    SENSOR_UPDATE_RATE_MS: int = Field(default=100, ge=10)
    USE_REALISTIC_TIMING: bool = True

    # Sensor Noise Configuration
    SENSOR_NOISE_PERCENT: float = Field(default=2.0, ge=0, le=50)
    NOISE_TYPE: Literal['gaussian', 'uniform'] = 'gaussian'
    NOISE_SEED: Optional[int] = 42

    # Pressure Sensor 1
    CH1_ENABLED: bool = True
    CH1_START_PRESSURE: float = Field(default=0.5, ge=0, le=1.6)
    CH1_TARGET_PRESSURE: float = Field(default=0.5, ge=0, le=1.6)
    CH1_RATE_BAR_PER_MIN: float = Field(default=100.0, gt=0)
    CH1_AUTO_START: bool = True

    # Pressure Sensor 2
    CH2_ENABLED: bool = True
    CH2_START_PRESSURE: float = Field(default=1.5, ge=0, le=1.6)
    CH2_TARGET_PRESSURE: float = Field(default=1.5, ge=0, le=1.6)
    CH2_RATE_BAR_PER_MIN: float = Field(default=10.0, gt=0)
    CH2_AUTO_START: bool = True

    # MODBUS Hub (Promiscuous Mode)
    HUB_ENABLED: bool = True
    HUB_PROMISCUOUS_MODE: bool = True
    TRIGGER_SLAVE_ID: int = Field(default=2, ge=1, le=247)
    TRIGGER_REGISTER: int = Field(default=0, ge=0)
    TRIGGER_VALUE: int = Field(default=1, ge=0, le=1)
    PRESSURE_DROP_RATE: float = Field(default=0.5, gt=0)
    PRESSURE_DROP_DURATION_S: float = Field(default=5.0, gt=0)
    AFFECTED_SENSOR: int = Field(default=1, ge=1, le=2)

    # Web Server
    WEB_HOST: str = "0.0.0.0"
    WEB_PORT: int = Field(default=8080, ge=1, le=65535)
    WEB_UPDATE_INTERVAL_MS: int = Field(default=500, ge=100)
    ENABLE_WEBSOCKET: bool = True
    ENABLE_DEBUG_MODE: bool = False
    DEBUG_LOG_EVERY_REQUEST: bool = False
    DEBUG_LOG_TIMING_DETAILS: bool = True

    def get_sensor_config(self, channel: int) -> SensorConfig:
        """
        Get configuration for a specific sensor channel.

        Args:
            channel: Sensor channel number (1 or 2)

        Returns:
            SensorConfig for the specified channel
        """
        if channel == 1:
            return SensorConfig(
                enabled=self.CH1_ENABLED,
                start_pressure=self.CH1_START_PRESSURE,
                target_pressure=self.CH1_TARGET_PRESSURE,
                rate_bar_per_min=self.CH1_RATE_BAR_PER_MIN,
                auto_start=self.CH1_AUTO_START
            )
        elif channel == 2:
            return SensorConfig(
                enabled=self.CH2_ENABLED,
                start_pressure=self.CH2_START_PRESSURE,
                target_pressure=self.CH2_TARGET_PRESSURE,
                rate_bar_per_min=self.CH2_RATE_BAR_PER_MIN,
                auto_start=self.CH2_AUTO_START
            )
        else:
            raise ValueError(f"Invalid channel: {channel}. Must be 1 or 2.")


def load_config() -> Config:
    """
    Load and validate configuration from environment.

    Returns:
        Validated Config instance
    """
    return Config()


def persist_config(key: str, value: str) -> None:
    """
    Persist a configuration value to the .env file.

    Args:
        key: Configuration key (e.g., 'MODBUS_BAUDRATE')
        value: New value as string
    """
    from pathlib import Path

    env_path = Path('.env')

    if not env_path.exists():
        # Create .env if it doesn't exist
        env_path.write_text(f"{key}={value}\n")
        return

    # Read existing content
    lines = env_path.read_text().splitlines()

    # Find and update the key
    found = False
    new_lines = []
    for line in lines:
        if line.strip().startswith(f"{key}="):
            new_lines.append(f"{key}={value}")
            found = True
        else:
            new_lines.append(line)

    # Add key if not found
    if not found:
        new_lines.append(f"{key}={value}")

    # Write back
    env_path.write_text('\n'.join(new_lines) + '\n')
