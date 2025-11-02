#include "includes.h"

// Minimal fallback HTML content stored in program memory (emergency fallback only)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Energy Monitor - Emergency Mode</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; background: #f0f0f0; }
    .container { max-width: 600px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; }
    h1 { color: #333; text-align: center; }
    .status { padding: 10px; margin: 10px 0; border-radius: 5px; }
    .warning { background: #fff3cd; border: 1px solid #ffeaa7; color: #856404; }
    .info { background: #d1ecf1; border: 1px solid #bee5eb; color: #0c5460; }
    button { padding: 10px 20px; margin: 5px; background: #007bff; color: white; border: none; border-radius: 5px; cursor: pointer; }
    button:hover { background: #0056b3; }
    .metrics { display: flex; gap: 20px; margin: 20px 0; }
    .metric { flex: 1; text-align: center; padding: 10px; background: #f8f9fa; border-radius: 5px; }
  </style>
  </head>
  <body>
    <div class="container">
    <h1>Energy Monitor - Emergency Mode</h1>
      
    <div class="status warning">
      <strong>Notice:</strong> Running in emergency mode. Main dashboard failed to load from filesystem.
        </div>
    
    <div class="status info">
      This is a minimal interface to ensure basic functionality while the main dashboard is rebuilt.
      </div>
      
    <div class="metrics">
      <div class="metric">
        <h3>Grid</h3>
        <div id="gridValue">Loading...</div>
      </div>
      <div class="metric">
        <h3>Solar</h3>
        <div id="solarValue">Loading...</div>
      </div>
      <div class="metric">
        <h3>Consumer</h3>
        <div id="consumerValue">Loading...</div>
      </div>
      </div>

    <div style="text-align: center;">
      <button onclick="location.href='/config'">Settings</button>
      <button onclick="location.href='/test'">Test Connection</button>
      <button onclick="location.reload()">Refresh</button>
    </div>
    
    <div id="uptime" style="text-align: center; margin-top: 20px; color: #666;">
      Uptime: Loading...
    </div>
    </div>

  <script>
    function updateData() {
      fetch('/data')
        .then(response => response.json())
        .then(data => {
            let gridPower = 0, solarPower = 0, consumerPower = 0;
           data.meters.forEach(item => {
                if (item.name === 'Grid') gridPower = item.power;
                else if (item.name === 'Solar') solarPower = item.power;
                else if (item.name === 'Consumer') consumerPower = item.power;
           });

          document.getElementById('gridValue').innerText = Math.abs(gridPower) + 'W';
          document.getElementById('solarValue').innerText = solarPower + 'W';
          document.getElementById('consumerValue').innerText = consumerPower + 'W';
          
           if (data.uptime) {
            document.getElementById('uptime').innerText = 'Uptime: ' + data.uptime;
           }
        })
        .catch(error => {
          console.error('Data fetch error:', error);
        });
    }
    
    // Update every 3 seconds
    setInterval(updateData, 3000);
    updateData(); // Initial load
  </script>
</body>
</html>
)rawliteral";
