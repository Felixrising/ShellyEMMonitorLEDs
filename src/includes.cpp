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
    body { font-family: Arial, sans-serif; margin: 20px; }
    .chart-container { position: relative; height:600px; width:calc(100% - 40px); margin:0 20px; }
    #latestValues, #calculatedValue { font-size: 1.2em; margin-bottom: 10px; }
    button { margin-top: 20px; padding: 10px 20px; font-size: 1em; }
    /* Styling for slider */
    #frequencySlider { width: 300px; }
  </style>
</head>
<body>
  <h1>Energy Meter Data</h1>
  
  <!-- Frequency Slider -->
  <label for="frequencySlider">Update Frequency (seconds): <span id="frequencyValue">1</span>s</label><br>
  <input type="range" id="frequencySlider" min="1" max="30" value="1" step="1">
  
  <div id="latestValues">Loading latest values...</div>
  <div id="calculatedValue">Calculating Consumer...</div>
  <div class="chart-container">
    <canvas id="energyChart"></canvas>
  </div>
  <button onclick="location.href='/config'">Configuration</button>

  <script>
    console.log("Initializing Energy Chart...");

    var ctx = document.getElementById('energyChart').getContext('2d');
    var energyChart = new Chart(ctx, {
      type: 'line',
      data: {
        labels: [],
        datasets: [
          { label: 'Grid', data: [], borderColor: 'red', fill: false },
          { label: 'Solar', data: [], borderColor: 'green', fill: false },
          { label: 'Consumer', data: [], borderColor: 'blue', fill: false }
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
          
          energyChart.data.labels = [];
          energyChart.data.datasets.forEach(ds => ds.data = []);
          
          downsampledData.forEach(point => {
            let timeLabel = moment(point.timestamp, "YYYYMMDDTHHmmssSSS").toDate();
            energyChart.data.labels.push(timeLabel);
            energyChart.data.datasets[0].data.push(point.Grid);
            energyChart.data.datasets[1].data.push(point.Solar);
            energyChart.data.datasets[2].data.push(point.Consumer);
          });

          energyChart.update();
          console.log("History loaded, downsampled, and chart updated.");
        })
        .catch(error => console.error("Error loading history:", error));
    }

    function fetchData() {
      console.log('Fetching current data from /data...');
      fetch('/data')
        .then(response => {
           if (!response.ok) throw new Error("Network response was not ok");
           console.log("Response from /data:", response);
           return response.json();
        })
        .then(data => {
           console.log("Parsed current data:", data);
           
           let currentHost = window.location.hostname.toLowerCase();
           let expectedHost = (data.SheMeterName + ".local").toLowerCase();
           if(data.SheMeterName && expectedHost !== currentHost) {
                console.log('Detected new SheMeterName: ' + data.SheMeterName + ', redirecting...');
                let newUrl = 'http://' + expectedHost + '/';
                window.location.href = newUrl;
                return;
           }

           let latestText = '';
           let gridPower = 0, solarPower = 0, consumerPower = 0;

           data.meters.forEach(item => {
                latestText += `${item.name}: ${item.power}W, `;
                if (item.name === 'Grid') gridPower = item.power;
                else if (item.name === 'Solar') solarPower = item.power;
                else if (item.name === 'Consumer') consumerPower = item.power;
           });

           document.getElementById('latestValues').innerText = latestText.slice(0, -2);
           let calcValue = (gridPower || 0) - (solarPower || 0);
           document.getElementById('calculatedValue').innerText = `Calculated Consumer: ${calcValue}W`;

           let timeLabel = new Date();
           energyChart.data.labels.push(timeLabel);
           energyChart.data.datasets[0].data.push(gridPower);
           energyChart.data.datasets[1].data.push(solarPower);
           energyChart.data.datasets[2].data.push(calcValue);

           if (energyChart.data.labels.length > 300) {
             energyChart.data.labels.shift();
             energyChart.data.datasets.forEach(dataset => dataset.data.shift());
           }

           energyChart.update();
           console.log("Chart and display updated with current data.");
        })
        .catch(error => console.error("Error fetching data:", error));
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
