// WebSocket client for MODBUS RTU Simulator

let ws = null;
let pressureChart = null;
const chartData = {
    labels: [],
    s1True: [],
    s1Measured: [],
    s2True: [],
    s2Measured: []
};
const MAX_CHART_POINTS = 100;

// Initialize on page load
document.addEventListener('DOMContentLoaded', async () => {
    initChart();

    // Fetch initial status immediately via REST API
    try {
        const response = await fetch('/api/status');
        const data = await response.json();
        console.log('Initial status:', data);
        updateUI(data);
    } catch (error) {
        console.error('Failed to fetch initial status:', error);
    }

    connectWebSocket();
    setupInputListeners();
});

function initChart() {
    const ctx = document.getElementById('pressureChart').getContext('2d');
    pressureChart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: chartData.labels,
            datasets: [
                {
                    label: 'S1 True',
                    data: chartData.s1True,
                    borderColor: '#0d6efd',
                    backgroundColor: 'transparent',
                    borderWidth: 2,
                    pointRadius: 0
                },
                {
                    label: 'S1 Measured',
                    data: chartData.s1Measured,
                    borderColor: '#198754',
                    backgroundColor: 'transparent',
                    borderWidth: 1,
                    pointRadius: 0
                },
                {
                    label: 'S2 True',
                    data: chartData.s2True,
                    borderColor: '#6610f2',
                    backgroundColor: 'transparent',
                    borderWidth: 2,
                    pointRadius: 0
                },
                {
                    label: 'S2 Measured',
                    data: chartData.s2Measured,
                    borderColor: '#20c997',
                    backgroundColor: 'transparent',
                    borderWidth: 1,
                    pointRadius: 0
                }
            ]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            animation: false,
            scales: {
                x: {
                    display: false
                },
                y: {
                    min: 0,
                    max: 1.6,
                    title: {
                        display: true,
                        text: 'Pressure (bar)'
                    }
                }
            },
            plugins: {
                legend: {
                    position: 'top'
                }
            }
        }
    });
}

function connectWebSocket() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws`;

    ws = new WebSocket(wsUrl);

    ws.onopen = () => {
        document.getElementById('connection-status').textContent = 'Connected';
        document.getElementById('connection-status').className = 'badge bg-success';
        console.log('WebSocket connected');
    };

    ws.onclose = () => {
        document.getElementById('connection-status').textContent = 'Disconnected';
        document.getElementById('connection-status').className = 'badge bg-danger';
        console.log('WebSocket disconnected, reconnecting...');
        setTimeout(connectWebSocket, 2000);
    };

    ws.onerror = (error) => {
        console.error('WebSocket error:', error);
    };

    ws.onmessage = (event) => {
        const data = JSON.parse(event.data);
        if (data.type === 'status_update') {
            updateUI(data);
        }
    };
}

function updateUI(data) {
    // Update sensor 1
    if (data.sensors && data.sensors[0]) {
        const s1 = data.sensors[0];
        document.getElementById('s1-true').textContent = s1.true_pressure.toFixed(3) + ' bar';
        document.getElementById('s1-measured').textContent = s1.measured_pressure.toFixed(3) + ' bar';
        document.getElementById('s1-ma').textContent = s1.current_ma.toFixed(2);
        document.getElementById('s1-modbus').textContent = s1.modbus_value;
        // Update fault button states
        updateFaultButtons(1, s1.fault_mode);
    }

    // Update sensor 2
    if (data.sensors && data.sensors[1]) {
        const s2 = data.sensors[1];
        document.getElementById('s2-true').textContent = s2.true_pressure.toFixed(3) + ' bar';
        document.getElementById('s2-measured').textContent = s2.measured_pressure.toFixed(3) + ' bar';
        document.getElementById('s2-ma').textContent = s2.current_ma.toFixed(2);
        document.getElementById('s2-modbus').textContent = s2.modbus_value;
        // Update fault button states
        updateFaultButtons(2, s2.fault_mode);
    }

    // Update simulator status and input fields
    if (data.simulators) {
        if (data.simulators[0]) {
            const sim1 = data.simulators[0];
            const status = sim1.running ? 'Running' : 'Stopped';
            const badge = sim1.running ? 'bg-success' : 'bg-secondary';
            document.getElementById('sim1-status').textContent = status;
            document.getElementById('sim1-status').className = 'badge ' + badge;
            // Update input fields if not focused
            const targetInput = document.getElementById('s1-target');
            const rateInput = document.getElementById('s1-rate');
            if (document.activeElement !== targetInput) {
                targetInput.value = sim1.target_pressure;
            }
            if (document.activeElement !== rateInput) {
                rateInput.value = sim1.rate_bar_per_min;
            }
        }
        if (data.simulators[1]) {
            const sim2 = data.simulators[1];
            const status = sim2.running ? 'Running' : 'Stopped';
            const badge = sim2.running ? 'bg-success' : 'bg-secondary';
            document.getElementById('sim2-status').textContent = status;
            document.getElementById('sim2-status').className = 'badge ' + badge;
            // Update input fields if not focused
            const targetInput = document.getElementById('s2-target');
            const rateInput = document.getElementById('s2-rate');
            if (document.activeElement !== targetInput) {
                targetInput.value = sim2.target_pressure;
            }
            if (document.activeElement !== rateInput) {
                rateInput.value = sim2.rate_bar_per_min;
            }
        }
    }

    // Update timing stats
    if (data.timing) {
        document.getElementById('timing-avg').textContent = data.timing.avg_response_ms.toFixed(2) + ' ms';
        document.getElementById('timing-min').textContent = data.timing.min_response_ms.toFixed(2);
        document.getElementById('timing-max').textContent = data.timing.max_response_ms.toFixed(2);
        document.getElementById('timing-jitter').textContent = data.timing.jitter_ms.toFixed(2) + ' ms';
        document.getElementById('timing-total').textContent = data.timing.total_requests;
    }

    // Update hub status
    if (data.hub) {
        const hubStatus = data.hub.trigger_active ? 'Active' : 'Idle';
        const hubBadge = data.hub.trigger_active ? 'bg-warning' : 'bg-secondary';
        document.getElementById('hub-status').textContent = hubStatus;
        document.getElementById('hub-status').className = 'badge ' + hubBadge;
        document.getElementById('hub-slave').textContent = data.hub.trigger_slave_id;
        document.getElementById('hub-register').textContent = data.hub.trigger_register;
        document.getElementById('hub-affected').textContent = data.hub.affected_sensor;
        document.getElementById('hub-rate').textContent = data.hub.pressure_drop_rate;
    }

    // Update device config
    if (data.device) {
        document.getElementById('dev-slave-id').textContent = data.device.slave_id;
        document.getElementById('dev-version').textContent = data.device.software_version;

        // Update data types
        const dataTypeNames = {
            0: '4-20mA',
            1: '0-20mA',
            2: '0-5V',
            3: '0-10V'
        };
        if (data.device.data_types) {
            document.getElementById('dtype-1').textContent = dataTypeNames[data.device.data_types[0]] || 'Unknown';
            document.getElementById('dtype-2').textContent = dataTypeNames[data.device.data_types[1]] || 'Unknown';
        }
    }

    // Update chart
    updateChart(data);
}

function updateChart(data) {
    const now = new Date().toLocaleTimeString();

    chartData.labels.push(now);

    if (data.sensors && data.sensors[0]) {
        chartData.s1True.push(data.sensors[0].true_pressure);
        chartData.s1Measured.push(data.sensors[0].measured_pressure);
    }

    if (data.sensors && data.sensors[1]) {
        chartData.s2True.push(data.sensors[1].true_pressure);
        chartData.s2Measured.push(data.sensors[1].measured_pressure);
    }

    // Limit data points
    if (chartData.labels.length > MAX_CHART_POINTS) {
        chartData.labels.shift();
        chartData.s1True.shift();
        chartData.s1Measured.shift();
        chartData.s2True.shift();
        chartData.s2Measured.shift();
    }

    pressureChart.update();
}

function setupInputListeners() {
    // Target pressure inputs
    document.getElementById('s1-target').addEventListener('change', (e) => {
        sendMessage({
            type: 'set_target',
            sensor: 1,
            target: parseFloat(e.target.value)
        });
    });

    document.getElementById('s2-target').addEventListener('change', (e) => {
        sendMessage({
            type: 'set_target',
            sensor: 2,
            target: parseFloat(e.target.value)
        });
    });

    // Rate inputs
    document.getElementById('s1-rate').addEventListener('change', (e) => {
        sendMessage({
            type: 'set_rate',
            sensor: 1,
            rate: parseFloat(e.target.value)
        });
    });

    document.getElementById('s2-rate').addEventListener('change', (e) => {
        sendMessage({
            type: 'set_rate',
            sensor: 2,
            rate: parseFloat(e.target.value)
        });
    });
}

function sendMessage(msg) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify(msg));
    }
}

function startSim(sensor) {
    // Send current input values first
    const targetInput = document.getElementById(`s${sensor}-target`);
    const rateInput = document.getElementById(`s${sensor}-rate`);

    sendMessage({
        type: 'set_target',
        sensor: sensor,
        target: parseFloat(targetInput.value)
    });
    sendMessage({
        type: 'set_rate',
        sensor: sensor,
        rate: parseFloat(rateInput.value)
    });
    sendMessage({
        type: 'start_simulation',
        sensor: sensor
    });
}

function stopSim(sensor) {
    sendMessage({
        type: 'stop_simulation',
        sensor: sensor
    });
}

function resetSim(sensor) {
    sendMessage({
        type: 'reset_simulation',
        sensor: sensor
    });
}

function triggerHub() {
    sendMessage({
        type: 'trigger_hub'
    });
}

function setFault(sensor, faultMode) {
    sendMessage({
        type: 'set_fault',
        sensor: sensor,
        fault_mode: faultMode
    });
}

function updateFaultButtons(sensor, faultMode) {
    const wireBtn = document.getElementById(`s${sensor}-fault-wire`);
    const defectBtn = document.getElementById(`s${sensor}-fault-defect`);
    const clearBtn = document.getElementById(`s${sensor}-fault-clear`);

    // Reset all buttons
    wireBtn.className = 'btn btn-outline-danger btn-sm';
    defectBtn.className = 'btn btn-outline-danger btn-sm';
    clearBtn.className = 'btn btn-outline-secondary btn-sm';

    // Highlight active fault
    if (faultMode === 'wire_break') {
        wireBtn.className = 'btn btn-danger btn-sm';
    } else if (faultMode === 'sensor_defect') {
        defectBtn.className = 'btn btn-danger btn-sm';
    } else {
        clearBtn.className = 'btn btn-secondary btn-sm';
    }
}

function setDeviceAddress() {
    const address = parseInt(document.getElementById('dev-address').value);
    if (address >= 1 && address <= 255) {
        sendMessage({
            type: 'set_device_address',
            address: address
        });
    } else {
        alert('Address must be between 1 and 255');
    }
}

function setUartParams() {
    const baudrate = parseInt(document.getElementById('dev-baudrate').value);
    const parity = document.getElementById('dev-parity').value;
    sendMessage({
        type: 'set_uart_params',
        baudrate: baudrate,
        parity: parity
    });
}
