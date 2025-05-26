document.addEventListener('DOMContentLoaded', function() {
  // Relay controls
  const relaysDiv = document.getElementById('relays');
  if (relaysDiv) {
    fetch('/relaystates')
      .then(res => res.json())
      .then(states => {
        relaysDiv.innerHTML = '';
        for (let i = 0; i < 8; i++) {
          const btn = document.createElement('button');
          btn.className = 'relay-btn' + (states[i] ? ' on' : '');
          btn.textContent = `Relay ${i + 1}`;
          btn.onclick = () => {
            fetch(`/relay?num=${i + 1}&state=${states[i] ? 0 : 1}`)
              .then(() => location.reload());
          };
          relaysDiv.appendChild(btn);
        }
      });
  }

  // Sensor readings
  function updateSensor() {
    fetch('/current')
      .then(res => res.json())
      .then(data => {
        document.getElementById('temp').textContent = data.temperature !== undefined ? data.temperature.toFixed(1) : '--';
        document.getElementById('hum').textContent = data.humidity !== undefined ? data.humidity.toFixed(1) : '--';
      });
  }
  if (document.getElementById('temp')) {
    updateSensor();
    setInterval(updateSensor, 5000);
  }
});