import { Component, ViewChild } from '@angular/core';
import { Chart } from 'chart.js';
import { ChartModule, UIChart } from 'primeng/chart';
import { interval, map, Observable, shareReplay, startWith, switchMap, tap } from 'rxjs';
import { HashSuffixPipe } from 'src/app/pipes/hash-suffix.pipe';
import { QuicklinkService } from 'src/app/services/quicklink.service';
import { ShareRejectionExplanationService } from 'src/app/services/share-rejection-explanation.service';
import { SystemService } from 'src/app/services/system.service';
import { ThemeService } from 'src/app/services/theme.service';
import { ISystemInfo } from 'src/models/ISystemInfo';


@Component({
  selector: 'app-home',
  templateUrl: './home.component.html',
  styleUrls: ['./home.component.scss']
})
export class HomeComponent {

  public info$!: Observable<ISystemInfo>;
  public expectedHashRate$!: Observable<number | undefined>;

  public chartOptions: any;
  public dataLabel: number[] = [];
  public hashrateData: number[] = [];
  public temperatureData: number[] = [];
  public mhzData: number[] = [];
  public coreVoltageData: number[] = [];
  public powerData: number[] = [];
  public fanspeed: number[] = [];
  public chartData?: any;

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

  constructor(
    private systemService: SystemService,
    private themeService: ThemeService,
    private quickLinkService: QuicklinkService,
    private shareRejectReasonsService: ShareRejectionExplanationService,

  ) {
    this.initializeChart();

    // Subscribe to theme changes
    this.themeService.getThemeSettings().subscribe(() => {
      this.updateChartColors();
    });
  }

  private updateChartColors() {
    const documentStyle = getComputedStyle(document.documentElement);
    const textColor = documentStyle.getPropertyValue('--text-color');
    const textColorSecondary = documentStyle.getPropertyValue('--text-color-secondary');
    const surfaceBorder = documentStyle.getPropertyValue('--surface-border');
    const primaryColor = documentStyle.getPropertyValue('--primary-color');
    const mhzColor = documentStyle.getPropertyValue('--green-800');
    const coreVoltageColor = documentStyle.getPropertyValue('--yellow-700');
    const fanspeedColor = documentStyle.getPropertyValue('--indigo-600');

    // Update chart colors
    if (this.chartData && this.chartData.datasets) {
      this.chartData.datasets[0].backgroundColor = primaryColor + '30';
      this.chartData.datasets[0].borderColor = primaryColor;
      this.chartData.datasets[1].backgroundColor = textColorSecondary;
      this.chartData.datasets[1].borderColor = textColorSecondary;
      this.chartData.datasets[2].backgroundColor = mhzColor;
      this.chartData.datasets[2].borderColor = mhzColor;
      this.chartData.datasets[3].backgroundColor = coreVoltageColor;
      this.chartData.datasets[3].borderColor = coreVoltageColor;
      this.chartData.datasets[4].backgroundColor = fanspeedColor;
      this.chartData.datasets[4].borderColor = fanspeedColor;
    }

    // Update chart options
    if (this.chartOptions) {
      this.chartOptions.plugins.legend.labels.color = textColor;
      this.chartOptions.scales.x.ticks.color = textColorSecondary;
      this.chartOptions.scales.x.grid.color = surfaceBorder;
      this.chartOptions.scales.y.ticks.color = textColorSecondary;
      this.chartOptions.scales.y.grid.color = surfaceBorder;
      this.chartOptions.scales.y2.ticks.color = textColorSecondary;
      this.chartOptions.scales.y2.grid.color = surfaceBorder;
      this.chartOptions.scales.y3.ticks.color = mhzColor;
      this.chartOptions.scales.y3.grid.color = surfaceBorder;
      this.chartOptions.scales.y4.ticks.color = coreVoltageColor;
      this.chartOptions.scales.y4.grid.color = surfaceBorder;
      this.chartOptions.scales.y5.ticks.color = fanspeedColor;
      this.chartOptions.scales.y5.grid.color = surfaceBorder;
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
    const coreVoltageColor = documentStyle.getPropertyValue('--yellow-700');
    const fanspeedColor = documentStyle.getPropertyValue('--indigo-600');

    this.chartData = {
      labels: [],
      datasets: [
        {
          type: 'line',
          label: 'Hashrate',
          data: this.hashrateData,
          backgroundColor: primaryColor + '30',
          borderColor: primaryColor,
          tension: 0,
          pointRadius: 1,
          pointHoverRadius: 1,
          borderWidth: 1,
          yAxisID: 'y',
          fill: false,
        },
        {
          type: 'line',
          label: 'ASIC Temp',
          data: this.temperatureData,
          fill: false,
          backgroundColor: textColorSecondary,
          borderColor: textColorSecondary,
          tension: 0,
          pointRadius: 1,
          pointHoverRadius: 1,
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
          pointRadius: 1,
          pointHoverRadius: 1,
          borderWidth: 1,
          yAxisID: 'y3',
        },
        {
          type: 'line',
          label: 'ASIC Volt',
          data: this.coreVoltageData,
          fill: false,
          backgroundColor: coreVoltageColor,
          borderColor: coreVoltageColor,
          tension: 0,
          pointRadius: 1,
          pointHoverRadius: 1,
          borderWidth: 1,
          yAxisID: 'y4',
        }
        ,
        {
          type: 'line',
          label: 'Fan',
          data: this.fanspeed,
          fill: false,
          backgroundColor: fanspeedColor,
          borderColor: fanspeedColor,
          tension: 0,
          pointRadius: 1,
          pointHoverRadius: 1,
          borderWidth: 1,
          yAxisID: 'y5',
        }
      ]
    };

    this.chartOptions = {
      animation: false,
      maintainAspectRatio: false,
      plugins: {
        legend: {
          labels: {
            color: textColor
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
              else if (tooltipItem.dataset.label === 'ASIC Volt') {
                label += tooltipItem.raw + 'mv';
              }
              else if (tooltipItem.dataset.label === 'Fan') {
                label += tooltipItem.raw + '%';
              }
              else {
                label += HashSuffixPipe.transform(tooltipItem.raw);
              }
              return label;
            }
          }
        },
      },
      scales: {
        x: {
          type: 'time',
          time: {
            unit: 'hour', // Set the unit to 'minute'
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
          ticks: {
            color: textColorSecondary,
            callback: (value: number) => HashSuffixPipe.transform(value)
          },
          grid: {
            color: surfaceBorder,
            drawBorder: false
          }
        },
        y2: {
          drawOnChartArea: false,
          type: 'linear',
          display: true,
          position: 'right',
          ticks: {
            color: textColorSecondary,
            callback: (value: number) => value + '°C'
          },
          grid: {
            drawOnChartArea: false,
            color: surfaceBorder
          },
          suggestedMax: 80
        },
        y3: {
          type: 'linear',
          display: true,
          position: 'right',
          ticks: {
            color: mhzColor,
            callback: (value: number) => value + 'mHz'
          },
          grid: {
            drawOnChartArea: false,
            color: surfaceBorder
          },
          suggestedMax: 600
        }
        ,
        y4: {
          type: 'linear',
          display: true,
          position: 'right',
          ticks: {
            color: coreVoltageColor,
            callback: (value: number) => value + 'mv'
          },
          grid: {
            drawOnChartArea: false,
            color: surfaceBorder
          },
          suggestedMax: 800
        }
        ,
        y5: {
          type: 'linear',
          display: true,
          position: 'right',
          ticks: {
            color: coreVoltageColor,
            callback: (value: number) => value + '%'
          },
          grid: {
            drawOnChartArea: false,
            color: surfaceBorder
          },
          suggestedMax: 100
        }
      }
    };

    this.chartData.labels = this.dataLabel;
    this.chartData.datasets[0].data = this.hashrateData;
    this.chartData.datasets[1].data = this.temperatureData;
    this.chartData.datasets[2].data = this.mhzData;
    this.chartData.datasets[3].data = this.coreVoltageData;
    this.chartData.datasets[4].data = this.fanspeed;

    this.chartData = {
      ...this.chartData
    };


    this.info$ = interval(5000).pipe(
      startWith(() => this.systemService.getInfo()),
      switchMap(() => {
        return this.systemService.getInfo()
      }),
      tap(info => {
        // Only collect and update chart data if there's no power fault
        if (!info.power_fault) {
          this.hashrateData.push(info.hashRate * 1000000000);
          this.temperatureData.push(info.temp);
          this.mhzData.push(info.frequency);
          this.coreVoltageData.push(info.coreVoltage);
          this.powerData.push(info.power);
          this.fanspeed.push(info.fanspeed);

          this.dataLabel.push(new Date().getTime());

          if (this.hashrateData.length >= 720) {
            this.hashrateData.shift();
            this.temperatureData.shift();
            this.mhzData.shift();
            this.coreVoltageData.shift();
            this.powerData.shift();
            this.dataLabel.shift();
            this.fanspeed.shift();
          }

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
        info.coreVoltage = parseFloat((info.coreVoltage / 1000).toFixed(2));
        info.temp = parseFloat(info.temp.toFixed(1));

        return info;
      }),
      shareReplay({ refCount: true, bufferSize: 1 })
    );

    this.expectedHashRate$ = this.info$.pipe(map(info => {
      return Math.floor(info.frequency * ((info.smallCoreCount * info.asicCount) / 1000))
    }))

    this.quickLink$ = this.info$.pipe(
      map(info => {
        const url = info.isUsingFallbackStratum ? info.fallbackStratumURL : info.stratumURL;
        const user = info.isUsingFallbackStratum ? info.fallbackStratumUser : info.stratumUser;
        return this.quickLinkService.getQuickLink(url, user);
      })
    );
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
      return power / (hashrate / 1000000000000); // Convert to J/TH
    });

    return this.calculateAverage(efficiencies);
  }
}
