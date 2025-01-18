#include "includes.h"

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <script src='https://cdn.jsdelivr.net/npm/chart.js'></script>
</head>
<body>
  <h1>Energy Meter Data</h1>
  <div id='latestValues'></div>
  <div id='calculatedValue'></div>
  <div class='chart-container' style='position: relative; height:800px; width:calc(100% - 40px); margin:20px 20px;'>
    <canvas id='energyChart'></canvas>
  </div>
  <script>
    var lastUpdateTime = 0;
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
      options: { responsive: true, maintainAspectRatio: false }
    });

    function fetchData() {
      console.log('Fetching data... Timestamp:', lastUpdateTime);
      fetch('/data')
        .then(response => response.json())
        .then(data => {
          let latestText = '';
          let gridPower = 0, solarPower = 0, consumerPower = 0;
          
          data.forEach(item => {
            latestText += item.name + ': ' + item.power + 'W, ';
            if (item.name === 'Grid') {
              gridPower = item.power;
            } else if (item.name === 'Solar') {
              solarPower = item.power;
            } else if (item.name === 'Consumer') {
              consumerPower = item.power;
            }
          });

          document.getElementById('latestValues').innerHTML = latestText;
          // Calculate as Grid - Solar
          var calcValue = (gridPower || 0) - (solarPower || 0);
          document.getElementById('calculatedValue').innerHTML = 'Calculated Consumer: ' + calcValue + 'W';

          energyChart.data.labels.push(new Date().toLocaleTimeString());
          energyChart.data.datasets[0].data.push(gridPower);
          energyChart.data.datasets[1].data.push(solarPower);
          energyChart.data.datasets[2].data.push(consumerPower);
          energyChart.update();
        })
        .catch(error => console.log(error));
    }

    setInterval(fetchData, 1000);
    fetchData();
  </script>
  <button onclick="location.href='/config'">Configuration</button>
</body>
</html>
)rawliteral";
