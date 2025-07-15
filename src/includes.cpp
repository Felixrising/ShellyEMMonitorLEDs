#include "includes.h"

// Define the HTML content for the root page stored in program memory
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Energy Meter Data</title>
  <!-- Load Moment.js from CDN -->
  <script src="https://cdn.jsdelivr.net/npm/moment@2.29.1/moment.min.js"></script>
  <!-- Load Chart.js from CDN -->
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
  <!-- Load Chartjs-Adapter-Moment from CDN -->
  <script src="https://cdn.jsdelivr.net/npm/chartjs-adapter-moment@1.0.0"></script>
  <!-- Load Chartjs-Plugin-Zoom from CDN -->
  <script src="https://cdn.jsdelivr.net/npm/chartjs-plugin-zoom@1.2.1/dist/chartjs-plugin-zoom.min.js"></script>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body { 
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
      color: #333;
      padding: 20px;
    }
    
    .container { max-width: 1200px; margin: 0 auto; }
    
    h1 { 
      text-align: center; 
      color: white; 
      margin-bottom: 30px; 
      font-size: 2.5em; 
      font-weight: 300;
      text-shadow: 0 2px 4px rgba(0,0,0,0.3);
    }
    
    .control-panel {
      background: rgba(255,255,255,0.95);
      border-radius: 15px;
      padding: 20px;
      margin-bottom: 30px;
      display: flex;
      justify-content: space-between;
      align-items: center;
      box-shadow: 0 8px 32px rgba(0,0,0,0.1);
      backdrop-filter: blur(10px);
    }
    
    .control-item label {
      font-weight: 600;
      color: #555;
      margin-right: 10px;
    }
    
    #frequencySlider {
      width: 200px;
      height: 6px;
      border-radius: 3px;
      background: #ddd;
      outline: none;
      margin-left: 10px;
    }
    
    #frequencySlider::-webkit-slider-thumb {
      appearance: none;
      width: 20px;
      height: 20px;
      border-radius: 50%;
      background: #667eea;
      cursor: pointer;
      box-shadow: 0 2px 6px rgba(0,0,0,0.2);
    }
    
    .btn {
      padding: 12px 24px;
      border: none;
      border-radius: 8px;
      font-size: 14px;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.3s ease;
      text-decoration: none;
      display: inline-block;
    }
    
    .btn-primary {
      background: linear-gradient(45deg, #667eea, #764ba2);
      color: white;
      box-shadow: 0 4px 15px rgba(102, 126, 234, 0.4);
    }
    
    .btn-primary:hover {
      transform: translateY(-2px);
      box-shadow: 0 6px 20px rgba(102, 126, 234, 0.6);
    }
    
    .btn-secondary {
      background: #6c757d;
      color: white;
    }
    
    .btn-secondary:hover {
      background: #5a6268;
      transform: translateY(-2px);
    }
    
    .metrics-grid {
       display: grid;
       grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
       gap: 20px;
       margin-bottom: 30px;
       max-width: 900px;
       margin-left: auto;
       margin-right: auto;
     }
    
    .metric-card {
      background: rgba(255,255,255,0.95);
      border-radius: 15px;
      padding: 25px;
      text-align: center;
      box-shadow: 0 8px 32px rgba(0,0,0,0.1);
      backdrop-filter: blur(10px);
      transition: transform 0.3s ease;
    }
    
    .metric-card:hover {
      transform: translateY(-5px);
    }
    
    .metric-title {
      font-size: 14px;
      color: #666;
      text-transform: uppercase;
      letter-spacing: 1px;
      margin-bottom: 10px;
      font-weight: 600;
    }
    
    .metric-value {
      font-size: 2.5em;
      font-weight: 700;
      margin-bottom: 5px;
    }
    
    .metric-unit {
      font-size: 16px;
      color: #888;
      font-weight: 500;
    }
    
    .metric-subtitle {
      font-size: 12px;
      color: #666;
      margin-top: 5px;
      font-weight: 600;
      text-transform: uppercase;
      letter-spacing: 0.5px;
    }
    
    .metric-grid { color: #e74c3c; }
    .metric-solar { color: #27ae60; }
    .metric-consumer { color: #3498db; }
    .metric-calculated { color: #9b59b6; }
    
    .chart-container { 
      background: rgba(255,255,255,0.95);
      border-radius: 15px;
      padding: 25px;
      box-shadow: 0 8px 32px rgba(0,0,0,0.1);
      backdrop-filter: blur(10px);
      position: relative; 
      height: 500px; 
      margin-bottom: 20px;
    }
    
    .chart-title {
      font-size: 1.5em;
      font-weight: 600;
      color: #333;
      margin-bottom: 20px;
      text-align: center;
    }
    
    @media (max-width: 768px) {
      .control-panel {
        flex-direction: column;
        gap: 20px;
      }
      
      .metrics-grid {
        grid-template-columns: 1fr;
      }
      
      h1 { font-size: 2em; }
      
      .chart-container { height: 400px; }
    }
  </style>
  </head>
  <body>
    <div class="container">
      <h1>Energy Monitor Dashboard</h1>
      
      <!-- Control Panel -->
      <div class="control-panel">
        <div class="control-item">
          <label for="frequencySlider">Update Frequency: <span id="frequencyValue">1</span>s</label>
          <input type="range" id="frequencySlider" min="1" max="30" value="1" step="1">
        </div>
        <div class="control-item">
          <button onclick="location.href='/config'" class="btn btn-primary">Settings</button>
        </div>
      </div>
      
      <!-- Metrics Grid -->
      <div class="metrics-grid">
        <div class="metric-card">
          <div class="metric-title">Grid Power</div>
          <div class="metric-value metric-grid" id="gridValue">0</div>
          <div class="metric-unit">Watts</div>
          <div class="metric-subtitle" id="gridStatus">Importing</div>
        </div>
        <div class="metric-card">
          <div class="metric-title">Solar Generation</div>
          <div class="metric-value metric-solar" id="solarValue">0</div>
          <div class="metric-unit">Watts</div>
        </div>
        <div class="metric-card">
          <div class="metric-title">Home Consumption</div>
          <div class="metric-value metric-consumer" id="consumerValue">0</div>
          <div class="metric-unit">Watts</div>
        </div>
      </div>
      
      <!-- Chart -->
      <div class="chart-container">
        <div class="chart-title">Power Consumption Over Time</div>
        <canvas id="energyChart"></canvas>
      </div>
    </div>

  <script>
    console.log("Initializing Energy Chart...");

    var ctx = document.getElementById('energyChart').getContext('2d');
    var energyChart = new Chart(ctx, {
      type: 'line',
      data: {
        labels: [],
        datasets: [
          { label: 'Grid Power', data: [], borderColor: '#e74c3c', backgroundColor: 'rgba(231, 76, 60, 0.1)', fill: false },
          { label: 'Solar Generation', data: [], borderColor: '#27ae60', backgroundColor: 'rgba(39, 174, 96, 0.1)', fill: false },
          { label: 'Home Consumption', data: [], borderColor: '#3498db', backgroundColor: 'rgba(52, 152, 219, 0.1)', fill: false }
        ]
      },
      options: {
        responsive: true,
        maintainAspectRatio: false,
        scales: {
          x: {
            type: 'time',
            time: {
              parser: 'YYYYMMDDTHHmmssSSS',
              unit: 'minute',
              displayFormats: { minute: 'HH:mm:ss' },
              tooltipFormat: 'YYYY-MM-DD HH:mm:ss'
            },
            title: { display: true, text: 'Time' }
          },
          y: {
            title: { display: true, text: 'Power (W)' },
            beginAtZero: true
          }
        },
        plugins: {
          zoom: {
            zoom: { wheel: { enabled: true }, pinch: { enabled: true }, mode: 'x' },
            pan: { enabled: true, mode: 'x' }
          }
        }
      }
    });

    console.log("Chart initialized.");

    let updateInterval = 1000;  // default 1 second in milliseconds
    let fetchIntervalID;
    const frequencySlider = document.getElementById('frequencySlider');
    const frequencyValueDisplay = document.getElementById('frequencyValue');

    // Retrieve saved frequency from localStorage if available
    let savedFrequency = localStorage.getItem('updateFrequency');
    if (savedFrequency) {
      frequencySlider.value = savedFrequency;
      frequencyValueDisplay.innerText = savedFrequency;
      updateInterval = parseInt(savedFrequency) * 1000;
    } else {
      frequencyValueDisplay.innerText = frequencySlider.value;
    }

    frequencySlider.addEventListener('input', function() {
      let seconds = parseInt(this.value);
      frequencyValueDisplay.innerText = seconds;
      localStorage.setItem('updateFrequency', seconds);
      updateInterval = seconds * 1000;
      clearInterval(fetchIntervalID);
      fetchIntervalID = setInterval(fetchData, updateInterval);
      loadHistory();  // Reload history data with new downsampling rate
    });

    function downsampleHistory(data, intervalSeconds) {
      let downsampled = [];
      if(data.length === 0) return downsampled;

      let bucketStart = moment(data[0].timestamp, "YYYYMMDDTHHmmssSSS");
      let bucketEnd = bucketStart.clone().add(intervalSeconds, 'seconds');
      let bucket = { timestamp: bucketStart.format("YYYYMMDDTHHmmssSSS"), Grid: 0, Solar: 0, Consumer: 0, count: 0 };

      data.forEach(point => {
        let pointTime = moment(point.timestamp, "YYYYMMDDTHHmmssSSS");
        if(pointTime.isBefore(bucketEnd)) {
          bucket.Grid += point.Grid;
          bucket.Solar += point.Solar;
          bucket.Consumer += point.Consumer;
          bucket.count++;
        } else {
          if(bucket.count > 0) {
            bucket.Grid /= bucket.count;
            bucket.Solar /= bucket.count;
            bucket.Consumer /= bucket.count;
          }
          downsampled.push({ ...bucket });
          bucketStart = bucketEnd.clone();
          bucketEnd = bucketStart.clone().add(intervalSeconds, 'seconds');
          bucket = { timestamp: point.timestamp, Grid: point.Grid, Solar: point.Solar, Consumer: point.Consumer, count: 1 };
        }
      });

      if(bucket.count > 0) {
        bucket.Grid /= bucket.count;
        bucket.Solar /= bucket.count;
        bucket.Consumer /= bucket.count;
        downsampled.push({ ...bucket });
      }
      return downsampled;
    }

    function loadHistory() {
      console.log("Loading history data from /history...");
      fetch('/history')
        .then(response => {
          if (!response.ok) throw new Error("Network response was not ok");
          return response.json();
        })
        .then(data => {
          console.log("Parsed history data:", data);
          
          let intervalSeconds = parseInt(frequencySlider.value);
          let downsampledData = downsampleHistory(data, intervalSeconds);
          
          // Clear existing data
          energyChart.data.labels = [];
          energyChart.data.datasets.forEach(ds => ds.data = []);
          
          // Sort history data by timestamp to ensure chronological order
          downsampledData.sort((a, b) => {
            const timeA = moment(a.timestamp, "YYYYMMDDTHHmmssSSS");
            const timeB = moment(b.timestamp, "YYYYMMDDTHHmmssSSS");
            return timeA.valueOf() - timeB.valueOf();
          });
          
          console.log("Loading history points in chronological order:");
          downsampledData.forEach((point, index) => {
            let timeLabel = moment(point.timestamp, "YYYYMMDDTHHmmssSSS").toDate();
            console.log(`History ${index}: ${timeLabel.toISOString()} - Grid:${point.Grid}, Solar:${point.Solar}, Consumer:${point.Consumer}`);
            
            energyChart.data.labels.push(timeLabel);
            energyChart.data.datasets[0].data.push(point.Grid);
            energyChart.data.datasets[1].data.push(point.Solar);
            energyChart.data.datasets[2].data.push(point.Consumer);
          });

          // Store the last history timestamp for comparison with real-time data
          if (downsampledData.length > 0) {
            window.lastHistoryTimestamp = moment(downsampledData[downsampledData.length - 1].timestamp, "YYYYMMDDTHHmmssSSS").toDate();
            console.log("Last history timestamp:", window.lastHistoryTimestamp.toISOString());
          }

          energyChart.update();
          console.log("History loaded, sorted, and chart updated.");
        })
        .catch(error => console.error("Error loading history:", error));
    }

    function fetchData() {
      console.log('Fetching current data from /data...');
      const fetchStartTime = Date.now();
      
      fetch('/data')
        .then(response => {
           if (!response.ok) throw new Error("Network response was not ok");
           return response.json();
        })
        .then(data => {
           const fetchEndTime = Date.now();
           const fetchDuration = fetchEndTime - fetchStartTime;
           
           console.log(`Data fetch took ${fetchDuration}ms:`, data);
           
           // No redirects - always stay on current URL
           
           let gridPower = 0, solarPower = 0, consumerPower = 0;

           data.meters.forEach(item => {
                if (item.name === 'Grid') gridPower = item.power;
                else if (item.name === 'Solar') solarPower = item.power;
                else if (item.name === 'Consumer') consumerPower = item.power;
           });

           // Update metric cards with correct values
           document.getElementById('gridValue').innerText = Math.abs(gridPower);
           document.getElementById('solarValue').innerText = solarPower;
           document.getElementById('consumerValue').innerText = consumerPower;
           
           // Update grid status indicator
           const gridStatus = document.getElementById('gridStatus');
           if (gridPower < 0) {
               gridStatus.innerText = 'Exporting';
               gridStatus.style.color = '#27ae60'; // Green for export
           } else {
               gridStatus.innerText = 'Importing';
               gridStatus.style.color = '#e74c3c'; // Red for import
           }

           // Use consistent timestamp for chart data
           let timeLabel = new Date();
           let lastTime = energyChart.data.labels.length > 0 ? 
                         energyChart.data.labels[energyChart.data.labels.length - 1] : null;
           
           // Check if this data point is older than the last history point
           if (window.lastHistoryTimestamp && timeLabel <= window.lastHistoryTimestamp) {
               console.warn(`Skipping real-time data point that's older than history. Current: ${timeLabel.toISOString()}, Last history: ${window.lastHistoryTimestamp.toISOString()}`);
               return;
           }
           
           // Debug logging for time issues
           if (lastTime && timeLabel < lastTime) {
               console.warn(`Time going backwards! Last: ${lastTime.toISOString()}, Current: ${timeLabel.toISOString()}`);
               return; // Don't add data that goes backwards
           }
           
           console.log(`Adding chart point at ${timeLabel.toISOString()}: Grid=${gridPower}W, Solar=${solarPower}W, Consumer=${consumerPower}W`);
           
           energyChart.data.labels.push(timeLabel);
           energyChart.data.datasets[0].data.push(gridPower);
           energyChart.data.datasets[1].data.push(solarPower);
           energyChart.data.datasets[2].data.push(consumerPower);

           if (energyChart.data.labels.length > 300) {
             energyChart.data.labels.shift();
             energyChart.data.datasets.forEach(dataset => dataset.data.shift());
           }

           energyChart.update();
           console.log(`Chart updated. Total points: ${energyChart.data.labels.length}`);
        })
        .catch(error => {
           console.error("Error fetching data:", error);
           console.error("Fetch duration before error:", Date.now() - fetchStartTime, "ms");
        });
    }

    function checkDebugData() {
      console.log("=== DEBUG DATA CHECK ===");
      
      // Get both debug data and time sync data
      Promise.all([
        fetch('/debug').then(r => r.json()),
        fetch('/time').then(r => r.json())
      ]).then(([debugData, timeData]) => {
        console.log("Debug data:", debugData);
        console.log("Time sync data:", timeData);
        
        const browserTime = Date.now();
        const deviceTime = timeData.currentDeviceTime;
        const timeDiff = browserTime - deviceTime;
        
        console.log(`Browser time: ${new Date(browserTime).toISOString()}`);
        console.log(`Device time: ${new Date(deviceTime).toISOString()}`);
        console.log(`Time difference: ${timeDiff}ms`);
        
        // Show timing info
        console.log(`Device uptime: ${debugData.deviceTime}ms`);
        console.log(`Last data update: ${debugData.lastDataUpdate}ms ago`);
        console.log(`Update interval: ${debugData.dataUpdateInterval}ms`);
        console.log(`WebSocket connected: ${debugData.wsConnected}`);
        console.log(`RPC in progress: ${debugData.rpcInProgress}`);
        console.log(`New data available: ${debugData.newDataAvailable}`);
        
        // Show meter data
        console.log("Current meter readings:");
        debugData.meters.forEach(meter => {
          console.log(`  ${meter.name}: ${meter.power}W (last update: ${meter.lastUpdate}ms)`);
        });
        
        // Show recent history
        console.log("Recent history points:");
        debugData.recentHistory.forEach((point, index) => {
          const date = new Date(point.timestamp);
          console.log(`  ${index}: ${date.toISOString()} - Grid:${point.grid}W, Solar:${point.solar}W, Consumer:${point.consumer}W`);
        });
        
        // Show chart data
        console.log("Current chart data points:", energyChart.data.labels.length);
        if (energyChart.data.labels.length > 0) {
          const lastIndex = energyChart.data.labels.length - 1;
          const lastTime = energyChart.data.labels[lastIndex];
          console.log(`Last chart point: ${lastTime.toISOString()}`);
          console.log(`Last values: Grid=${energyChart.data.datasets[0].data[lastIndex]}, Solar=${energyChart.data.datasets[1].data[lastIndex]}, Consumer=${energyChart.data.datasets[2].data[lastIndex]}`);
          
          // Check for time ordering issues
          console.log("Last 5 chart timestamps:");
          const startIdx = Math.max(0, energyChart.data.labels.length - 5);
          for (let i = startIdx; i < energyChart.data.labels.length; i++) {
            console.log(`  ${i}: ${energyChart.data.labels[i].toISOString()}`);
          }
        }
        
        if (window.lastHistoryTimestamp) {
          console.log(`Last history timestamp: ${window.lastHistoryTimestamp.toISOString()}`);
        }
        
      }).catch(error => console.error("Debug data fetch error:", error));
    }

    window.onload = function() {
      console.log("Page loaded. Starting data retrieval...");
      loadHistory();
      fetchData();
      fetchIntervalID = setInterval(fetchData, updateInterval);
    };
  </script>
</body>
</html>
)rawliteral";
