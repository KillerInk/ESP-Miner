import { Component, ElementRef, HostListener, OnInit, ViewChild } from '@angular/core';
import { interval, map, min, Observable, shareReplay, startWith, switchMap, tap } from 'rxjs';
import { HashSuffixPipe } from 'src/app/pipes/hash-suffix.pipe';
import { QuicklinkService } from 'src/app/services/quicklink.service';
import { ShareRejectionExplanationService } from 'src/app/services/share-rejection-explanation.service';
import { SystemService } from 'src/app/services/system.service';
import { ThemeService } from 'src/app/services/theme.service';
import { ISystemInfo } from 'src/models/ISystemInfo';
import { ISystemStatistics } from 'src/models/ISystemStatistics';
import { Title } from '@angular/platform-browser';
import { UIChart } from 'primeng/chart';
import { Chart } from 'chart.js';
import { saveAs } from 'file-saver';

@Component({
  selector: 'app-home',
  templateUrl: './home.component.html',
  styleUrls: ['./home.component.scss']
})
export class HomeComponent {

  public info$!: Observable<ISystemInfo>;
  public stats$!: Observable<ISystemStatistics>;

  public chartOptions: any;
  public dataLabel: number[] = [];
  public hashrateData: number[] = [];
  public temperatureData: number[] = [];
  public mhzData: number[] = [];
  public coreVoltageData: number[] = [];
  public coreVoltageCurrentData: number[] = [];
  public powerData: number[] = [];
  public fanspeed: number[] = [];
  public chartData?: any;
  public avghashrateData: number[] = [];
  public espRam: number[] = [];

  public maxPower: number = 0;
  public nominalVoltage: number = 0;
  public maxTemp: number = 75;
  public maxFrequency: number = 800;

  public quickLink$!: Observable<string | undefined>;

  public activePoolURL!: string;
  public activePoolPort!: number;
  public activePoolUser!: string;
  public activePoolLabel!: 'Primary' | 'Fallback';
  @ViewChild('chart')
  private chart?: UIChart
  @ViewChild('chartContainer') chartContainer?: ElementRef;
  private visibleItemCount = 0;
  private itemPosition = 0;
  private mousebuttonpressed = false;
  private mousestartposition = 0;

  private pageDefaultTitle: string = '';
  public datasetVisibility: boolean[] = [];

  public isMouseOverChart = false;

  public diffData: number[] = [];

  constructor(
    private systemService: SystemService,
    private themeService: ThemeService,
    private quickLinkService: QuicklinkService,
    private shareRejectReasonsService: ShareRejectionExplanationService,
    private titleService: Title,

  ) {
    this.initializeChart();

    // Subscribe to theme changes
    this.themeService.getThemeSettings().subscribe(() => {
      this.updateChartColors();
    });
  }

  ngOnInit() {
    this.pageDefaultTitle = this.titleService.getTitle();
  }


  private get zoomPanFactor(): number {
    // Adjust divisor for desired sensitivity
    return Math.max(1, Math.floor(this.visibleItemCount / 40));
  }

  onMouseWheel(event: WheelEvent) {
    if (!this.isMouseOverChart) return;
    const factor = this.zoomPanFactor;
    if (event.deltaY > 0)
      this.visibleItemCount += factor;
    else
      this.visibleItemCount -= factor;
    if (this.visibleItemCount > this.dataLabel.length)
      this.visibleItemCount = this.dataLabel.length;
    if (this.visibleItemCount < 5)
      this.visibleItemCount = 5;
    this.setTimeLimits();
    event.preventDefault();
  }


  onMouseDown(event: MouseEvent) {
    if (!this.isMouseOverChart) return;
    this.mousebuttonpressed = true;
    this.mousestartposition = event.pageX;
    console.log("mousedown");
    return false;
  }


  onMouseUp(event: MouseEvent) {
    if (!this.isMouseOverChart) return;
    this.mousebuttonpressed = false;
    this.mousestartposition = 0;
    console.log("mouseup");
    return true
  }

  private stepcount = 0;

  onMouseMove(event: MouseEvent) {
    if (!this.isMouseOverChart) return;
    const factor = this.zoomPanFactor;
    if (this.mousebuttonpressed && this.stepcount == 1) {
      if (this.mousestartposition > event.pageX) {
        this.itemPosition += factor;
        this.mousestartposition = event.pageX;
      }
      else if (this.mousestartposition < event.pageX) {
        this.itemPosition -= factor;
        this.mousestartposition = event.pageX;
      }
      this.stepcount = 0;
      if (this.itemPosition > 0)
        this.itemPosition = 0;
    }
    else if (this.mousebuttonpressed)
      this.stepcount++;

    this.setTimeLimits();
    return false;
  }

  private setTimeLimits() {
    var min = (this.dataLabel.length - this.visibleItemCount) + this.itemPosition;
    if (min < 0) {
      min = 0
      this.itemPosition++;
      return;
    }
    var max = this.dataLabel.length + this.itemPosition;

    if (min >= this.dataLabel.length)
      min = this.dataLabel.length - 5;
    if (max > this.dataLabel.length)
      max = this.dataLabel.length;
    this.chartOptions.scales.x.min = this.dataLabel[min];
    this.chartOptions.scales.x.max = this.dataLabel[max];
    console.log("max:" + (max));
    console.log("min:" + (min));
    console.log("itempos:" + (this.itemPosition));
    (this.chart?.chart as any)?.update();
  }

  private updateChartColors() {
    const documentStyle = getComputedStyle(document.documentElement);
    const textColor = documentStyle.getPropertyValue('--text-color');
    const textColorSecondary = documentStyle.getPropertyValue('--text-color-secondary');
    const surfaceBorder = documentStyle.getPropertyValue('--surface-border');
    const primaryColor = documentStyle.getPropertyValue('--primary-color');
    const mhzColor = documentStyle.getPropertyValue('--green-800');
    const coreVoltageColor = documentStyle.getPropertyValue('--yellow-500');
    const fanspeedColor = documentStyle.getPropertyValue('--indigo-600');
    const avghashColor = documentStyle.getPropertyValue('--pink-300');
    const coreVoltageCurrentColor = documentStyle.getPropertyValue('--yellow-800');
    const espRamColor = documentStyle.getPropertyValue('--teal-600');


    // Update chart colors
    if (this.chartData && this.chartData.datasets) {
      this.chartData.datasets[0].backgroundColor = textColorSecondary + '30';
      this.chartData.datasets[0].borderColor = textColorSecondary;
      this.chartData.datasets[1].backgroundColor = primaryColor;
      this.chartData.datasets[1].borderColor = primaryColor;
      this.chartData.datasets[2].backgroundColor = mhzColor;
      this.chartData.datasets[2].borderColor = mhzColor;
      this.chartData.datasets[3].backgroundColor = coreVoltageColor;
      this.chartData.datasets[3].borderColor = coreVoltageColor;
      this.chartData.datasets[4].backgroundColor = fanspeedColor;
      this.chartData.datasets[4].borderColor = fanspeedColor;
      this.chartData.datasets[5].borderColor = avghashColor;
      this.chartData.datasets[5].backgroundColor = avghashColor;
      this.chartData.datasets[6].borderColor = coreVoltageCurrentColor;
      this.chartData.datasets[6].backgroundColor = coreVoltageCurrentColor;
      this.chartData.datasets[7].borderColor = espRamColor;
      this.chartData.datasets[7].backgroundColor = espRamColor;
    }

    // Update chart options
    if (this.chartOptions) {
      this.chartOptions.plugins.legend.labels.color = textColor;
      this.chartOptions.scales.x.ticks.color = textColorSecondary;
      this.chartOptions.scales.x.grid.color = surfaceBorder;
      this.chartOptions.scales.y.ticks.color = textColorSecondary;
      this.chartOptions.scales.y.grid.color = surfaceBorder;
      this.chartOptions.scales.y2.ticks.color = primaryColor;
      this.chartOptions.scales.y2.grid.color = surfaceBorder;
      this.chartOptions.scales.y3.ticks.color = mhzColor;
      this.chartOptions.scales.y3.grid.color = surfaceBorder;
      this.chartOptions.scales.y4.ticks.color = coreVoltageColor;
      this.chartOptions.scales.y4.grid.color = surfaceBorder;
      this.chartOptions.scales.y5.ticks.color = fanspeedColor;
      this.chartOptions.scales.y5.grid.color = surfaceBorder;
      this.chartOptions.scales.y6.ticks.color = avghashColor;
      this.chartOptions.scales.y6.grid.color = surfaceBorder;
      this.chartOptions.scales.y7.ticks.color = coreVoltageCurrentColor;
      this.chartOptions.scales.y7.grid.color = surfaceBorder;
      this.chartOptions.scales.y8.ticks.color = espRamColor;
      this.chartOptions.scales.y8.grid.color = surfaceBorder;
    }

    // Force chart update
    this.chartData = { ...this.chartData };
  }



  private initializeChart() {
    const documentStyle = getComputedStyle(document.documentElement);
    const textColor = documentStyle.getPropertyValue('--text-color');
    const textColorSecondary = documentStyle.getPropertyValue('--text-color-secondary');
    const surfaceBorder = documentStyle.getPropertyValue('--surface-border');
    const primaryColor = documentStyle.getPropertyValue('--primary-color');
    const mhzColor = documentStyle.getPropertyValue('--green-800');
    const coreVoltageColor = documentStyle.getPropertyValue('--yellow-500');
    const fanspeedColor = documentStyle.getPropertyValue('--indigo-600');
    const avghashColor = documentStyle.getPropertyValue('--pink-300');
    const coreVoltageCurrentColor = documentStyle.getPropertyValue('--yellow-800');
    const espRamColor = documentStyle.getPropertyValue('--teal-600');
    const diffColor = '#a259f7'; // purple

    this.chartData = {
      labels: this.dataLabel,
      datasets: [
        {
          type: 'line',
          label: 'Hashrate',
          data: this.hashrateData,
          backgroundColor: textColorSecondary + '30',
          borderColor: textColorSecondary,
          tension: 0,
          pointRadius: 0,
          pointHoverRadius: 0,
          borderWidth: 1.5,
          yAxisID: 'y',
          fill: false,
        },
        {
          type: 'line',
          label: 'ASIC Temp',
          data: this.temperatureData,
          fill: false,
          backgroundColor: primaryColor,
          borderColor: primaryColor,
          tension: 0,
          pointRadius: 0,
          pointHoverRadius: 0,
          borderWidth: 1,
          yAxisID: 'y2',
        },
        {
          type: 'line',
          label: 'ASIC Freq',
          data: this.mhzData,
          fill: false,
          backgroundColor: mhzColor,
          borderColor: mhzColor,
          tension: 0,
          pointRadius: 0,
          pointHoverRadius: 0,
          borderWidth: 1,
          yAxisID: 'y3',
        },
        {
          type: 'line',
          label: 'VoltSet',
          data: this.coreVoltageData,
          fill: false,
          backgroundColor: coreVoltageColor,
          borderColor: coreVoltageColor,
          tension: 0,
          pointRadius: 0,
          pointHoverRadius: 0,
          borderWidth: 1,
          yAxisID: 'y4',
        },
        {
          type: 'line',
          label: 'Fan',
          data: this.fanspeed,
          fill: false,
          backgroundColor: fanspeedColor,
          borderColor: fanspeedColor,
          tension: 0,
          pointRadius: 0,
          pointHoverRadius: 0,
          borderWidth: 1,
          yAxisID: 'y5',
        },
        {
          type: 'line',
          label: 'AvgHashrate',
          data: this.avghashrateData,
          backgroundColor: avghashColor + '30',
          borderColor: avghashColor,
          tension: 0,
          pointRadius: 0,
          pointHoverRadius: 0,
          borderWidth: 1,
          yAxisID: 'y6',
          fill: false,
        },
        {
          type: 'line',
          label: 'VoltCurrent',
          data: this.coreVoltageCurrentData,
          fill: false,
          backgroundColor: coreVoltageColor,
          borderColor: coreVoltageColor,
          tension: 0,
          pointRadius: 0,
          pointHoverRadius: 0,
          borderWidth: 1,
          yAxisID: 'y7',
        },
        {
          type: 'line',
          label: 'EspRam',
          data: this.espRam,
          fill: false,
          backgroundColor: espRamColor,
          borderColor: espRamColor,
          tension: 0,
          pointRadius: 0,
          pointHoverRadius: 0,
          borderWidth: 1,
          yAxisID: 'y8',
        },
        {
          type: 'line',
          label: 'Power',
          data: this.powerData,
          fill: false,
          backgroundColor: coreVoltageColor,
          borderColor: coreVoltageColor,
          tension: 0,
          pointRadius: 0,
          pointHoverRadius: 0,
          borderWidth: 1,
          yAxisID: 'y9',
        },
        {
          type: 'line',
          label: 'V/F Ratio',
          data: this.diffData,
          backgroundColor: diffColor,
          borderColor: diffColor,
          tension: 0,
          pointRadius: 0,
          pointHoverRadius: 0,
          borderWidth: 0.5,
          yAxisID: 'y10',
          fill: false,
          spanGaps: false
        },
      ]
    };

    this.datasetVisibility = this.chartData.datasets.map(() => true);
    this.restoreDatasetVisibility();

    // Initialize chart options
    this.chartOptions = {
      animation: false,
      maintainAspectRatio: false,
      interaction: {
        mode: 'nearest',
        axis: 'x',
        intersect: false
      },
      plugins: {
        legend: {
          labels: {
            color: textColor,
          },
          onClick: (e: any, legendItem: any, legend: any) => {
            const ci = legend.chart;
            const datasetIndex = legendItem.datasetIndex;
            // Toggle visibility
            const meta = ci.getDatasetMeta(datasetIndex);
            meta.hidden = meta.hidden === null ? !ci.data.datasets[datasetIndex].hidden : null;
            this.datasetVisibility[datasetIndex] = !meta.hidden;
            this.saveDatasetVisibility();
            ci.update();
          }
        },
        tooltip: {
          callbacks: {
            label: function (tooltipItem: any) {
              let label = tooltipItem.dataset.label || '';
              if (label) {
                label += ': ';
              }
              if (tooltipItem.dataset.label === 'ASIC Temp') {
                label += tooltipItem.raw + '°C';
              }
              else if (tooltipItem.dataset.label === 'ASIC Freq') {
                label += tooltipItem.raw + 'mHz';
              }
              else if (tooltipItem.dataset.label === 'VoltSet') {
                label += tooltipItem.raw + 'mv';
              }
              else if (tooltipItem.dataset.label === 'VoltCurrent') {
                label += tooltipItem.raw + 'mv';
              }
              else if (tooltipItem.dataset.label === 'Fan') {
                label += tooltipItem.raw + '%';
              }
              else if (tooltipItem.dataset.label === 'EspRam') {
                label += tooltipItem.raw + 'byte';
              }
              else if (tooltipItem.dataset.label === 'AvgHashrate') {
                label += HashSuffixPipe.transform(tooltipItem.raw);
              }
              else if (tooltipItem.dataset.label === 'Power') {
                label += tooltipItem.raw + ' W';
              }
              else if (tooltipItem.dataset.label === 'V/F Ratio') {
                label += tooltipItem.raw.toFixed(4); 
              }
              else {
                label += HashSuffixPipe.transform(tooltipItem.raw);
              }
              return label;
            }
          }
        },
      },
      layout: {
        padding: {
          right: 60,
          left: 60 // <-- Add this line to add padding to the right side of the chart
        }
      },
      scales: {
        x: {
          type: 'time',
          time: {
            unit: 'second', // Set the unit to 'minute'
          },
          ticks: {
            color: textColorSecondary
          },
          grid: {
            color: surfaceBorder,
            drawBorder: false,
            display: true
          }
        },
        y: {
          display: false, // <-- Hide hashrate scale display
          ticks: {
            color: textColorSecondary,
            callback: (value: number) => HashSuffixPipe.transform(value)
          },
          grid: {
            color: surfaceBorder,
            drawBorder: false
          },
        },
        y2: {
          drawOnChartArea: false,
          type: 'linear',
          display: false,
          position: 'right',
          ticks: {
            color: primaryColor,
            callback: (value: number) => value + '°C'
          },
          grid: {
            drawOnChartArea: false,
            color: surfaceBorder
          },
        },
        y3: {
          type: 'linear',
          display: false,
          position: 'right',
          ticks: {
            color: mhzColor,
            callback: (value: number) => value + 'mHz'
          },
          grid: {
            drawOnChartArea: false,
            color: surfaceBorder
          },
        }
        ,
        y4: {
          type: 'linear',
          display: false,
          position: 'right',
          ticks: {
            color: coreVoltageColor,
            callback: (value: number) => value + 'mv'
          },
          grid: {
            drawOnChartArea: false,
            color: surfaceBorder
          },
        }
        ,
        y5: {
          type: 'linear',
          display: false,
          position: 'right',
          ticks: {
            color: coreVoltageColor,
            callback: (value: number) => value + '%'
          },
          grid: {
            drawOnChartArea: false,
            color: surfaceBorder
          },
        },
        y6: {
          ticks: {
            color: avghashColor,
            display: false,
            callback: (value: number) => ''
          },
          grid: {
            color: surfaceBorder,
            drawOnChartArea: false,
          },
        }
        ,
        y7: {
          type: 'linear',
          display: false,
          position: 'right',
          ticks: {
            color: coreVoltageCurrentColor,
            callback: (value: number) => value + 'mv'
          },
          grid: {
            drawOnChartArea: false,
            color: surfaceBorder
          },
        }
        ,
        y8: {
          type: 'linear',
          display: false,
          position: 'right',
          ticks: {
            color: espRamColor,
            callback: (value: number) => value / 1024 + '/kb'
          },
          grid: {
            drawOnChartArea: false,
            color: surfaceBorder
          },

        },
        y9: {
          type: 'linear',
          display: false,
          position: 'right',
          ticks: {
            color: coreVoltageCurrentColor,
            callback: (value: number) => value + 'W'
          },
          grid: {
            drawOnChartArea: false,
            color: surfaceBorder
          },
        },
        y10: {
          type: 'linear',
          display: false,
          position: 'right',
          ticks: {
            color: diffColor,
            callback: (value: number) => value.toFixed(2)
          },
          grid: {
            drawOnChartArea: false,
            color: diffColor + '22'
          },
        }
      }
    };

    this.chartData.labels = this.dataLabel;
    this.chartData.datasets[0].data = this.hashrateData;
    this.chartData.datasets[1].data = this.temperatureData;
    this.chartData.datasets[2].data = this.mhzData;
    this.chartData.datasets[3].data = this.coreVoltageData;
    this.chartData.datasets[4].data = this.fanspeed;
    this.chartData.datasets[5].data = this.avghashrateData;
    this.chartData.datasets[6].data = this.coreVoltageCurrentData;
    this.chartData.datasets[7].data = this.espRam;
    this.chartData.datasets[8].data = this.powerData;
    this.chartData.datasets[9].data = this.diffData;

    // load previous data
    this.stats$ = this.systemService.getStatistics().pipe(shareReplay({ refCount: true, bufferSize: 1 }));
    this.stats$.subscribe(stats => {
      stats.statistics.forEach(element => {
        this.addDataPoint(element, true, stats);
      });
      this.visibleItemCount = this.dataLabel.length;
      this.setTimeLimits();
      this.chart?.refresh();
      this.startGetInfo();
    });
  }

  /**
   * Adds a new data point from either live info or statistics.
   * Handles all arrays and diffData, and manages shifting if needed.
   */
  private addDataPoint(info: ISystemInfo | number[], isStats = false, stats?: ISystemStatistics) {
    if (isStats && Array.isArray(info) && stats) {
      // For stats$ subscription
      const [
        hashrate, temperature, power, timestamp, voltage, freq, fanspeed, avghashrate, voltageCur, freeHeap
      ] = info as number[];

      this.hashrateData.push(hashrate * 1e9);
      this.temperatureData.push(temperature);
      this.powerData.push(Number(power.toFixed(2)));
      this.dataLabel.push(new Date().getTime() - stats.currentTimestamp + timestamp);
      this.coreVoltageData.push(voltage);
      this.mhzData.push(freq);
      this.fanspeed.push(fanspeed);
      this.avghashrateData.push(avghashrate * 1e9);
      this.coreVoltageCurrentData.push(voltageCur);
      this.espRam.push(freeHeap);
    } else if (!isStats && !Array.isArray(info)) {
      // For info$ subscription
      this.hashrateData.push(info.hashRate * 1e9);
      this.temperatureData.push(info.temp);
      this.mhzData.push(info.frequency);
      this.coreVoltageData.push(Number(info.coreVoltage.toFixed(2)));
      this.powerData.push(Number(info.power.toFixed(2)));
      this.fanspeed.push(info.fanspeed);
      this.avghashrateData.push(info.avghashRate * 1e9);
      this.dataLabel.push(new Date().getTime());
      this.coreVoltageCurrentData.push(info.coreVoltageActual);
      this.espRam.push(info.freeHeap);
    }

    // Calculate V/F ratio for each new point
    const lastIdx = this.coreVoltageData.length - 1;
    if (lastIdx >= 0 && this.mhzData[lastIdx]) {
      this.diffData[lastIdx] = this.coreVoltageData[lastIdx] / this.mhzData[lastIdx];
    } else {
      this.diffData[lastIdx] = 0;
    }

    if(this.itemPosition == 0)
      this.visibleItemCount++;
    else
      this.visibleItemCount--;
    //this.itemPosition--;

    // Shift arrays if needed
    if (this.hashrateData.length >= 720) {
      this.hashrateData.shift();
      this.temperatureData.shift();
      this.mhzData.shift();
      this.coreVoltageData.shift();
      this.powerData.shift();
      this.dataLabel.shift();
      this.fanspeed.shift();
      this.avghashrateData.shift();
      this.coreVoltageCurrentData.shift();
      this.espRam.shift();
      this.diffData.shift();
      this.visibleItemCount--;
    }
    this.setTimeLimits();
  }

  private startGetInfo() {
    this.info$ = interval(5000).pipe(
      startWith(() => this.systemService.getInfo()),
      switchMap(() => this.systemService.getInfo()),
      tap(info => {
        if (!info.power_fault) {
          this.addDataPoint(info);
          this.chart?.refresh();
        }

        this.maxPower = Math.max(info.maxPower, info.power);
        this.nominalVoltage = info.nominalVoltage;
        this.maxTemp = Math.max(75, info.temp);
        this.maxFrequency = Math.max(800, info.frequency);

        const isFallback = info.isUsingFallbackStratum;

        this.activePoolLabel = isFallback ? 'Fallback' : 'Primary';
        this.activePoolURL = isFallback ? info.fallbackStratumURL : info.stratumURL;
        this.activePoolUser = isFallback ? info.fallbackStratumUser : info.stratumUser;
        this.activePoolPort = isFallback ? info.fallbackStratumPort : info.stratumPort;
      }),
      map(info => {
        info.power = parseFloat(info.power.toFixed(1))
        info.voltage = parseFloat((info.voltage / 1000).toFixed(1));
        info.current = parseFloat((info.current / 1000).toFixed(1));
        info.coreVoltageActual = parseFloat((info.coreVoltageActual / 1000).toFixed(2));
        info.coreVoltage = parseFloat((info.coreVoltage).toFixed(2));
        info.temp = parseFloat(info.temp.toFixed(1));

        return info;
      }),
      shareReplay({ refCount: true, bufferSize: 1 }),

    );
    // live data

    this.quickLink$ = this.info$.pipe(
      map(info => {
        const url = info.isUsingFallbackStratum ? info.fallbackStratumURL : info.stratumURL;
        const user = info.isUsingFallbackStratum ? info.fallbackStratumUser : info.stratumUser;
        return this.quickLinkService.getQuickLink(url, user);
      })
    );

    this.info$.subscribe(info => {
      this.titleService.setTitle(
        [
          this.pageDefaultTitle,
          info.hostname,
          (info.hashRate ? HashSuffixPipe.transform(info.hashRate * 1000000000) : false),
          (info.temp ? `${info.temp}${info.vrTemp ? `/${info.vrTemp}` : ''} °C` : false),
          (!info.power_fault ? `${info.power} W` : false),
          (info.bestDiff ? info.bestDiff : false),
        ].filter(Boolean).join(' • ')
      );
    });

  }

  getRejectionExplanation(reason: string): string | null {
    return this.shareRejectReasonsService.getExplanation(reason);
  }

  getSortedRejectionReasons(info: ISystemInfo): ISystemInfo['sharesRejectedReasons'] {
    return [...(info.sharesRejectedReasons ?? [])].sort((a, b) => b.count - a.count);
  }

  trackByReason(_index: number, item: { message: string, count: number }) {
    return item.message; //Track only by message
  }

  public calculateAverage(data: number[]): number {
    if (data.length === 0) return 0;
    const sum = data.reduce((sum, value) => sum + value, 0);
    return sum / data.length;
  }

  public calculateEfficiencyAverage(hashrateData: number[], powerData: number[]): number {
    if (hashrateData.length === 0 || powerData.length === 0) return 0;

    // Calculate efficiency for each data point and average them
    const efficiencies = hashrateData.map((hashrate, index) => {
      const power = powerData[index] || 0;
      if (hashrate > 0) {
        return power / (hashrate / 1000000000000); // Convert to J/TH
      } else {
        return power; // in this case better than infinity or NaN
      }
    });

    return this.calculateAverage(efficiencies);
  }

  private saveDatasetVisibility() {
    localStorage.setItem('datasetVisibility', JSON.stringify(this.datasetVisibility));
  }

  private loadDatasetVisibility() {
    const saved = localStorage.getItem('datasetVisibility');
    if (saved) {
      try {
        const arr = JSON.parse(saved);
        if (Array.isArray(arr) && arr.length === this.chartData.datasets.length) {
          this.datasetVisibility = arr;
        }
      } catch { }
    }
  }

  private restoreDatasetVisibility() {
    this.loadDatasetVisibility();
    // Wait for chart to be available before applying visibility
    if (!this.chart?.chart) {
      // Try again after a short delay if chart is not ready yet
      setTimeout(() => this.restoreDatasetVisibility(), 100);
      return;
    }
    if (this.datasetVisibility.length) {
      const chartInstance = (this.chart.chart as any);
      this.datasetVisibility.forEach((visible, idx) => {
        const meta = chartInstance.getDatasetMeta(idx);
        meta.hidden = !visible;
      });
      chartInstance.update();
    }
  }

  public saveChartDataAsJson() {
    // Prepare the data to save
    const exportData = {
      date: new Date().toISOString(),
      labels: this.dataLabel,
      hashrateData: this.hashrateData,
      temperatureData: this.temperatureData,
      mhzData: this.mhzData,
      coreVoltageData: this.coreVoltageData,
      coreVoltageCurrentData: this.coreVoltageCurrentData,
      powerData: this.powerData,
      fanspeed: this.fanspeed,
      avghashrateData: this.avghashrateData,
      espRam: this.espRam
    };

    const json = JSON.stringify(exportData, null, 2);
    const blob = new Blob([json], { type: 'application/json' });
    const dateStr = new Date().toISOString().replace(/[:.]/g, '-');
    saveAs(blob, `esp32-miner-data-${dateStr}.json`);
  }
}

Chart.register({
  id: 'customValueLabels',
  afterDatasetsDraw: (chart: any) => {
    const ctx = chart.ctx;

    chart.data.datasets.forEach((dataset: any, i: number) => {
      const meta = chart.getDatasetMeta(i);
      if (!chart.isDatasetVisible(i)) return;

      const data = dataset.data;
      const scale = chart.scales.x;
      const visibleMin = scale.left;
      const visibleMax = scale.right;

      // Find valid and visible indices
      const visibleIndices = data
        .map((v: any, idx: number) => {
          const point = meta.data[idx];
          if (
            v !== undefined &&
            v !== null &&
            v !== '' &&
            v !== 'NaN' &&
            v !== 'NaNundefined' &&
            !(typeof v === 'number' && isNaN(v)) &&
            point &&
            point.x >= visibleMin &&
            point.x <= visibleMax
          ) {
            return idx;
          }
          return null;
        })
        .filter((idx: number | null) => idx !== null) as number[];

      if (visibleIndices.length === 0) return;

      const firstIndex = visibleIndices[0];
      const lastIndex = visibleIndices[visibleIndices.length - 1];

      // Find min and max value indices in the visible range
      let minIndex = firstIndex;
      let maxIndex = firstIndex;
      let minValue = data[firstIndex];
      let maxValue = data[firstIndex];

      visibleIndices.forEach(idx => {
        if (data[idx] < minValue) {
          minValue = data[idx];
          minIndex = idx;
        }
        if (data[idx] > maxValue) {
          maxValue = data[idx];
          maxIndex = idx;
        }
      });

      // Always show label for oldest (firstIndex) and newest (lastIndex) data,
      // even if they are not min/max, plus min/max (no duplicates)
      let labelIndices = Array.from(new Set([firstIndex, lastIndex, minIndex, maxIndex]));

      // --- Avoid overlapping: shift labels horizontally and vertically as needed ---
      // Store label positions to check for overlap
      const labelPositions: { x: number, y: number, w: number, h: number }[] = [];

      labelIndices.forEach(idx => {
        let value = data[idx];
        let suffix = '';
        if (dataset.label === 'Hashrate' || dataset.label === 'AvgHashrate') {
          value = HashSuffixPipe.transform(value);
          suffix = '';
        }
        else if (dataset.label === 'V/F Ratio') {
          value = value.toFixed(4);
        }
        else if (dataset.label === 'ASIC Temp') {
          suffix = '°C';
        }
        else if (dataset.label === 'ASIC Freq') {
          suffix = 'MHz';
        }
        else if (dataset.label === 'VoltSet' || dataset.label === 'VoltCurrent') {
          suffix = 'mV';
        }
        else if (dataset.label === 'Fan') {
          suffix = '%';
        }
        else if (dataset.label === 'EspRam') {
          suffix = 'B';
        }
        else if (dataset.label === 'Power') {
          suffix = 'W';
        }
        const point = meta.data[idx];
        if (!point) return;

        // Label styling
        const paddingX = 4;
        const paddingY = 2;
        ctx.save();
        ctx.font = '10px "Segoe UI", Arial, sans-serif';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';

        const text = String(value) + (suffix ? ' ' + suffix : '');
        const textWidth = ctx.measureText(text).width;
        const rectWidth = textWidth + paddingX * 2;
        const rectHeight = 14;
        


        // Initial position (no horizontal shift, only vertical)
        let x = point.x - rectWidth / 2;
        let y = point.y - rectHeight /2;
        if(idx ==firstIndex)
          x -= rectWidth *0.6;
        if(idx ==lastIndex)
          x += rectWidth*0.6;
        // Try to find a non-overlapping position by shifting vertically (y-direction) only
        let tryCount = 0;
        let found = false;
        const verticalShift = rectHeight +4; // <-- Increased from 4 to 12 for more space
        while (!found && tryCount < 5) {     // <-- Increased max tries for safety
          found = true;
          for (const pos of labelPositions) {
            // Check for rectangle overlap
            if (
              x < pos.x + pos.w &&
              x + rectWidth > pos.x &&
              y < pos.y + pos.h&&
              y + rectHeight > pos.y
            ) {
              // Overlap detected, shift down (y-direction only)
              y += verticalShift;
              found = false;
              break;
            }
          }
          tryCount++;
        }

        // Save this label's position
        labelPositions.push({ x, y, w: rectWidth, h: rectHeight });

        // Draw background with rounded corners
        ctx.beginPath();
        const radius = 5;
        ctx.moveTo(x + radius, y);
        ctx.lineTo(x + rectWidth - radius, y);
        ctx.quadraticCurveTo(x + rectWidth, y, x + rectWidth, y + radius);
        ctx.lineTo(x + rectWidth, y + rectHeight - radius);
        ctx.quadraticCurveTo(x + rectWidth, y + rectHeight, x + rectWidth - radius, y + rectHeight);
        ctx.lineTo(x + radius, y + rectHeight);
        ctx.quadraticCurveTo(x, y + rectHeight, x, y + rectHeight - radius);
        ctx.lineTo(x, y + radius);
        ctx.quadraticCurveTo(x, y, x + radius, y);
        ctx.closePath();

        ctx.fillStyle = "#222c";
        ctx.fill();

        // Draw border
        ctx.lineWidth = 1;
        ctx.strokeStyle = dataset.borderColor || '#fff';
        ctx.stroke();

        // Draw text
        ctx.fillStyle = "#fff";
        ctx.fillText(text, x + rectWidth / 2, y + rectHeight / 2 + paddingY / 2);

        ctx.restore();
      });
    });
  }
});

Chart.register({
  id: 'legendMargin',
  beforeInit(chart) {
    if (!chart.legend) return; // Safeguard for undefined legend
    const originalFit = chart.legend.fit;
    chart.legend.fit = function fit() {
      originalFit.bind(chart.legend)();
      this.height += 20; // <-- Adjust this value for more/less space
    };
  }
});
