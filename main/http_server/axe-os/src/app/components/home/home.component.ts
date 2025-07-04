import { Component, ElementRef, HostListener, OnInit, ViewChild } from '@angular/core';
import { interval, map, Observable, shareReplay, startWith, switchMap, tap } from 'rxjs';
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
export class HomeComponent implements OnInit {
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
  public hashrate_no_error: number[] = [];
  public hashrate_error: number[] = [];
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
  private chart?: UIChart;

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
    private titleService: Title
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
    return Math.max(1, Math.floor(this.visibleItemCount / 40));
  }

  onMouseWheel(event: WheelEvent) {
    if (!this.isMouseOverChart) return;
    const factor = this.zoomPanFactor;
    if (event.deltaY > 0)
      this.visibleItemCount += factor;
    else
      this.visibleItemCount -= factor;

    this.enforceVisibleItemBounds();
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
    } else if (this.mousebuttonpressed)
      this.stepcount++;

    this.setTimeLimits();
    return false;
  }

  private enforceVisibleItemBounds() {
    if (this.visibleItemCount > this.dataLabel.length) this.visibleItemCount = this.dataLabel.length;
    if (this.visibleItemCount < 5) this.visibleItemCount = 5;
  }

  private setTimeLimits() {
    let minIndex = (this.dataLabel.length - this.visibleItemCount) + this.itemPosition;

    if (minIndex < 0) {
      minIndex = 0;
      this.itemPosition++;
      return;
    }
    let maxIndex = this.dataLabel.length + this.itemPosition;

    if (maxIndex > this.dataLabel.length)
      maxIndex = this.dataLabel.length - 5;

    const minLabel = this.dataLabel[minIndex];
    const maxLabel = this.dataLabel[maxIndex];

    this.chartOptions.scales.x.min = minLabel;
    this.chartOptions.scales.x.max = maxLabel;

    (this.chart?.chart as any)?.update();
  }


  private updateChartColors() {
    const documentStyle = getComputedStyle(document.documentElement);
    const colors = this.getThemeColors(documentStyle);

    this.applyColorsToDatasets(colors);
    this.applyColorsToOptions(colors, documentStyle);
    this.chartData = { ...this.chartData };
  }

  private initializeChart() {
    const documentStyle = getComputedStyle(document.documentElement);
    const colors = this.getThemeColors(documentStyle);

    this.initializeChartData(colors);
    this.initializeChartOptions(colors, documentStyle);

    // Load previous data
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

  private getThemeColors(documentStyle: any) {
    return {
      textColor: documentStyle.getPropertyValue('--text-color'),
      textColorSecondary: documentStyle.getPropertyValue('--text-color-secondary'),
      surfaceBorder: documentStyle.getPropertyValue('--surface-border'),
      primaryColor: documentStyle.getPropertyValue('--primary-color'),
      mhzColor: documentStyle.getPropertyValue('--green-800'),
      coreVoltageColor: documentStyle.getPropertyValue('--orange-800'),
      fanspeedColor: documentStyle.getPropertyValue('--indigo-600'),
      avghashColor: documentStyle.getPropertyValue('--pink-300'),
      coreVoltageCurrentColor: documentStyle.getPropertyValue('--orange-900'),
      espRamColor: documentStyle.getPropertyValue('--teal-600'),
      diffColor: '#a259f7',
      hahsratenoerrorcolor: '#3f51b5',
      hahsrateerrorcolor: '#36459a'
    };
  }

  private initializeChartData(colors: any) {
    this.chartData = {
      labels: this.dataLabel,
      datasets: [
        { type: 'line', label: 'Hashrate', data: this.hashrateData, backgroundColor: colors.textColorSecondary + '30', borderColor: colors.textColorSecondary, tension: 0, pointRadius: 0, pointHoverRadius: 0, borderWidth: 0.8, yAxisID: 'y', fill: false },
        { type: 'line', label: 'ASIC Temp', data: this.temperatureData, backgroundColor: colors.primaryColor, borderColor: colors.primaryColor, tension: 0, pointRadius: 0, pointHoverRadius: 0, borderWidth: 0.8, yAxisID: 'y2' },
        { type: 'line', label: 'ASIC Freq', data: this.mhzData, backgroundColor: colors.mhzColor, borderColor: colors.mhzColor, tension: 0, pointRadius: 0, pointHoverRadius: 0, borderWidth: 0.8, yAxisID: 'y3' },
        { type: 'line', label: 'VoltSet', data: this.coreVoltageData, backgroundColor: colors.coreVoltageColor, borderColor: colors.coreVoltageColor, tension: 0, pointRadius: 0, pointHoverRadius: 0, borderWidth: 0.8, yAxisID: 'y4' },
        { type: 'line', label: 'Fan', data: this.fanspeed, backgroundColor: colors.fanspeedColor, borderColor: colors.fanspeedColor, tension: 0, pointRadius: 0, pointHoverRadius: 0, borderWidth: 0.8, yAxisID: 'y5' },
        { type: 'line', label: 'AvgHashrate', data: this.avghashrateData, backgroundColor: colors.avghashColor + '30', borderColor: colors.avghashColor, tension: 0, pointRadius: 0, pointHoverRadius: 0, borderWidth: 0.8, yAxisID: 'y6' },
        { type: 'line', label: 'VoltCurrent', data: this.coreVoltageCurrentData, backgroundColor: colors.coreVoltageColor, borderColor: colors.coreVoltageColor, tension: 0, pointRadius: 0, pointHoverRadius: 0, borderWidth: 0.8, yAxisID: 'y7' },
        { type: 'line', label: 'EspRam', data: this.espRam, backgroundColor: colors.espRamColor, borderColor: colors.espRamColor, tension: 0, pointRadius: 0, pointHoverRadius: 0, borderWidth: 0.8, yAxisID: 'y8' },
        { type: 'line', label: 'Power', data: this.powerData, backgroundColor: colors.coreVoltageColor, borderColor: colors.coreVoltageColor, tension: 0, pointRadius: 0, pointHoverRadius: 0, borderWidth: 0.8, yAxisID: 'y9' },
        { type: 'line', label: 'V/F Ratio', data: this.diffData, backgroundColor: colors.diffColor, borderColor: colors.diffColor, tension: 0, pointRadius: 0, pointHoverRadius: 0, borderWidth: 0.8, yAxisID: 'y10' },
        { type: 'line', label: 'Hashrate no error', data: this.hashrate_no_error, backgroundColor: colors.hahsratenoerrorcolor, borderColor: colors.hahsratenoerrorcolor, tension: 0, pointRadius: 0, pointHoverRadius: 0, borderWidth: 0.8, yAxisID: 'y11' },
        { type: 'line', label: 'Hashrate error', data: this.hashrate_error, backgroundColor: colors.hahsrateerrorcolor, borderColor: colors.hahsrateerrorcolor, tension: 0, pointRadius: 0, pointHoverRadius: 0, borderWidth: 0.8, yAxisID: 'y12' },
      ]
    };
    this.datasetVisibility = this.chartData.datasets.map(() => true);
    this.restoreDatasetVisibility();
  }

  private initializeChartOptions(colors: any, documentStyle: any) {
    this.chartOptions = {
      animation: false,
      maintainAspectRatio: false,
      interaction: { mode: 'nearest', axis: 'x', intersect: false },
      plugins: {
        legend: { labels: { color: colors.textColor }, onClick: (e: any, legendItem: any, legend: any) => this.onLegendClick(e, legendItem, legend) },
        tooltip: { callbacks: { label: this.tooltipLabelFormatter } }
      },
      layout: { padding: { right: 60, left: 60 } },
      scales: {
        x: { type: 'time', time: { unit: 'second' }, ticks: { color: colors.textColorSecondary }, grid: { color: colors.surfaceBorder, drawBorder: false, display: true } },
        y: { display: false, ticks: { color: colors.textColorSecondary, callback: (value: number) => HashSuffixPipe.transform(value) } },
        y2: { display: false, ticks: { color: colors.primaryColor, callback: (value: number) => value + '°C' }, suggestedMax: 80 },
        y3: { display: false, ticks: { color: colors.mhzColor, callback: (value: number) => value + 'mHz' }, suggestedMax: 1200 },
        y4: { display: false, ticks: { color: colors.coreVoltageColor, callback: (value: number) => value + 'mv' }, suggestedMax: 1200 },
        y5: { display: false, ticks: { color: colors.fanspeedColor, callback: (value: number) => value + '%' }, suggestedMax: 100 },
        y6: { ticks: { color: colors.avghashColor, display: false, callback: (value: number) => '' } },
        y7: { display: false, ticks: { color: colors.coreVoltageCurrentColor, callback: (value: number) => value + 'mv' }, suggestedMax: 1200 },
        y8: { display: false, ticks: { color: colors.espRamColor, callback: (value: number) => value / 1024 + '/kb' } },
        y9: { display: false, ticks: { color: colors.coreVoltageCurrentColor, callback: (value: number) => value + 'W' }, suggestedMax: 40 },
        y10: { display: false, ticks: { color: colors.diffColor, callback: (value: number) => value.toFixed(2) }, suggestedMax: 2.5 },
        y11: { display: false, ticks: { color: colors.hahsratenoerrorcolor, callback: (value: number) => HashSuffixPipe.transform(value) } },
        y12: { display: false, ticks: { color: colors.hahsrateerrorcolor, callback: (value: number) => HashSuffixPipe.transform(value) } }
      },
    };
  }

  private onLegendClick(e: any, legendItem: any, legend: any) {
    const ci = legend.chart;
    const datasetIndex = legendItem.datasetIndex;
    // Toggle visibility
    const meta = ci.getDatasetMeta(datasetIndex);
    if (meta.hidden === null) {
      meta.hidden = !ci.data.datasets[datasetIndex].hidden;
    } else {
      meta.hidden = !meta.hidden;
    }
    this.datasetVisibility[datasetIndex] = !meta.hidden;
    this.saveDatasetVisibility();
    ci.update();
    return false;
  }

  private tooltipLabelFormatter(tooltipItem: any) {
    let label = tooltipItem.dataset.label || '';
    if (label) { label += ': '; }
    switch (tooltipItem.dataset.label) {
      case 'ASIC Temp': return `${tooltipItem.raw}°C`;
      case 'ASIC Freq': return `${tooltipItem.raw}mHz`;
      case 'VoltSet': return `${tooltipItem.raw}mv`;
      case 'VoltCurrent': return `${tooltipItem.raw}mv`;
      case 'Fan': return `${tooltipItem.raw}%`;
      case 'EspRam': return `${tooltipItem.raw}byte`;
      case 'Power': return `${tooltipItem.raw} W`;
      case 'V/F Ratio': return tooltipItem.raw.toFixed(4);
      default: return HashSuffixPipe.transform(tooltipItem.raw);
    }
  }


  /**
   * Adds a new data point from either live info or statistics.
   * Handles all arrays and diffData, and manages shifting if needed.
   */
  private addDataPoint(info: ISystemInfo | number[], isStats = false, stats?: ISystemStatistics) {
    if (isStats && Array.isArray(info) && stats) {
      // For stats$ subscription
      const [
        hashrate, temperature, power, timestamp, voltage, freq, fanspeed, avghashrate, voltageCur, freeHeap, hashrate_no_error, hashrate_error
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
      this.hashrate_no_error.push(hashrate_no_error * 1e9);
      this.hashrate_error.push(hashrate_error * 1e9);
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
      this.hashrate_no_error.push(info.hashRate_no_error * 1e9);
      this.hashrate_error.push(info.hashRate_error * 1e9);
    }

    // Calculate V/F ratio for each new point
    const lastIdx = this.coreVoltageData.length - 1;
    if (lastIdx >= 0 && this.mhzData[lastIdx]) {
      this.diffData.push(this.coreVoltageData[lastIdx] / this.mhzData[lastIdx]);
    } else {
      this.diffData.push(0);
    }

    // Shift arrays if needed
    if (this.hashrateData.length >= 720) {
      this.shiftArrayData();
    }
    if (this.itemPosition == 0)
      this.visibleItemCount++;
    else
      this.visibleItemCount--;
    this.calculateMinMax();
    this.setTimeLimits();
  }

  private shiftArrayData() {
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
    this.hashrate_no_error.shift();
    this.hashrate_error.shift();
    this.visibleItemCount--;
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
      setTimeout(() => this.restoreDatasetVisibility(), 200);
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

  private applyColorsToDatasets(colors: any) {
    if (this.chartData && this.chartData.datasets) {
      // Update dataset colors
      this.chartData.datasets[0].backgroundColor = colors.textColorSecondary + '30';
      this.chartData.datasets[0].borderColor = colors.textColorSecondary;

      this.chartData.datasets[1].backgroundColor = colors.primaryColor;
      this.chartData.datasets[1].borderColor = colors.primaryColor;

      this.chartData.datasets[2].backgroundColor = colors.mhzColor;
      this.chartData.datasets[2].borderColor = colors.mhzColor;

      this.chartData.datasets[3].backgroundColor = colors.coreVoltageColor;
      this.chartData.datasets[3].borderColor = colors.coreVoltageColor;

      this.chartData.datasets[4].backgroundColor = colors.fanspeedColor;
      this.chartData.datasets[4].borderColor = colors.fanspeedColor;

      this.chartData.datasets[5].borderColor = colors.avghashColor;
      this.chartData.datasets[5].backgroundColor = colors.avghashColor + '30';

      this.chartData.datasets[6].borderColor = colors.coreVoltageCurrentColor;
      this.chartData.datasets[6].backgroundColor = colors.coreVoltageCurrentColor;

      this.chartData.datasets[7].borderColor = colors.espRamColor;
      this.chartData.datasets[7].backgroundColor = colors.espRamColor;

      this.chartData.datasets[8].borderColor = colors.coreVoltageColor;
      this.chartData.datasets[8].backgroundColor = colors.coreVoltageColor;

      this.chartData.datasets[9].backgroundColor = colors.diffColor;
      this.chartData.datasets[9].borderColor = colors.diffColor;

      this.chartData.datasets[10].backgroundColor = colors.textColorSecondary;
      this.chartData.datasets[10].borderColor = colors.textColorSecondary;

      this.chartData.datasets[11].backgroundColor = colors.textColorSecondary;
      this.chartData.datasets[11].borderColor = colors.textColorSecondary;
    }
  }

  private applyColorsToOptions(colors: any, documentStyle: any) {
    if (this.chartOptions) {
      // Update options with new colors
      this.chartOptions.plugins.legend.labels.color = colors.textColor;

      this.chartOptions.scales.x.ticks.color = colors.textColorSecondary;
      this.chartOptions.scales.x.grid.color = colors.surfaceBorder;

      this.chartOptions.scales.y.ticks.color = colors.textColorSecondary;
      this.chartOptions.scales.y.grid.color = colors.surfaceBorder;

      this.chartOptions.scales.y2.ticks.color = colors.primaryColor;
      this.chartOptions.scales.y2.grid.color = colors.surfaceBorder;

      this.chartOptions.scales.y3.ticks.color = colors.mhzColor;
      this.chartOptions.scales.y3.grid.color = colors.surfaceBorder;

      this.chartOptions.scales.y4.ticks.color = colors.coreVoltageColor;
      this.chartOptions.scales.y4.grid.color = colors.surfaceBorder;

      this.chartOptions.scales.y5.ticks.color = colors.fanspeedColor;
      this.chartOptions.scales.y5.grid.color = colors.surfaceBorder;

      this.chartOptions.scales.y6.ticks.color = colors.avghashColor;
      this.chartOptions.scales.y6.grid.color = colors.surfaceBorder;

      this.chartOptions.scales.y7.ticks.color = colors.coreVoltageCurrentColor;
      this.chartOptions.scales.y7.grid.color = colors.surfaceBorder;

      this.chartOptions.scales.y8.ticks.color = colors.espRamColor;
      this.chartOptions.scales.y8.grid.color = colors.surfaceBorder;

      this.chartOptions.scales.y9.ticks.color = colors.coreVoltageColor;
      this.chartOptions.scales.y9.grid.color = colors.surfaceBorder;

      this.chartOptions.scales.y10.ticks.color = colors.diffColor;
      this.chartOptions.scales.y10.grid.color = colors.surfaceBorder;

      this.chartOptions.scales.y11.ticks.color = colors.hahsratenoerrorcolor;
      this.chartOptions.scales.y11.grid.color = colors.surfaceBorder;

      this.chartOptions.scales.y12.ticks.color = colors.hahsrateerrorcolor;
      this.chartOptions.scales.y12.grid.color = colors.surfaceBorder;
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
      espRam: this.espRam,
      hashrate_no_error: this.hashrate_no_error,
      hashrate_error: this.hashrate_error,
    };

    const json = JSON.stringify(exportData, null, 2);
    const blob = new Blob([json], { type: 'application/json' });
    const dateStr = new Date().toISOString().replace(/[:.]/g, '-');
    saveAs(blob, `esp32-miner-data-${dateStr}.json`);
  }

  calculateMinMax() {
    if (this.hashrateData.length > 0) {
      const minHashrate = Math.min(...this.hashrateData);
      const maxHashrate = Math.max(...this.hashrateData);
      this.chartOptions.scales.y.min = minHashrate;
      this.chartOptions.scales.y.max = maxHashrate;
      this.chartOptions.scales.y6.min = minHashrate;
      this.chartOptions.scales.y6.max = maxHashrate;
      //this.chartOptions.scales.y11.min = minHashrate;
      //this.chartOptions.scales.y11.max = maxHashrate;
      //this.chartOptions.scales.y12.min = minHashrate;
      //this.chartOptions.scales.y12.max = maxHashrate;
    }

    /*if (this.coreVoltageData.length > 0) {
      const minVoltage = Math.min(...this.coreVoltageData);
      const maxVoltage = Math.max(...this.coreVoltageData);
  
      // Set frequency scale's minimum and maximum values
      this.chartOptions.scales.y2.min = minVoltage/2;
      this.chartOptions.scales.y2.max = maxVoltage/2;
      this.chartOptions.scales.y3.min = minVoltage;
      this.chartOptions.scales.y3.max = maxVoltage;
    }*/
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

      // Collect unique indices to label
      const labelIndices = Array.from(new Set([firstIndex, lastIndex, minIndex, maxIndex]));

      const getSuffix = (value: number): string => {
        if (!value || isNaN(value)) value = 0;
        switch (dataset.label) {
          case 'Hashrate':
          case 'AvgHashrate':
          case 'Hashrate no error':
          case 'Hashrate error':
            return HashSuffixPipe.transform(value);
          case 'V/F Ratio':
            return value.toFixed(4);
          case 'ASIC Temp':
            return '°C';
          case 'ASIC Freq':
            return 'MHz';
          case 'VoltSet':
          case 'VoltCurrent':
            return 'mV';
          case 'Fan':
            return '%';
          case 'EspRam':
            return 'B';
          case 'Power':
            return 'W';
          default:
            return '';
        }
      };
      labelIndices.forEach(idx => {
        let value = data[idx];
        let tt = "";
        const suffix = getSuffix(value as number);
        switch (dataset.label) {
          case 'Hashrate':
          case 'AvgHashrate':
          case 'V/F Ratio':
          case 'Hashrate no error':
          case 'Hashrate error':
            tt = suffix;
            break;
          default:
            tt = value.toString() + suffix;
            break;
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

        const text = String(tt);
        const textWidth = ctx.measureText(text).width;
        const rectWidth = textWidth + paddingX * 2;
        const rectHeight = 14;

        // Draw background with rounded corners
        ctx.beginPath();
        const radius = 5;
        const yOffset = 15; 
        let x, y;
        if (point.x < visibleMax / 2) {
          x = point.x - rectWidth  - paddingX; // Move label to the left
          y = point.y + yOffset - rectHeight / 2; // Add y-offset here
        } else {
          x = point.x  + paddingX; // Move label to the right
          y = point.y - yOffset - rectHeight / 2; // Add y-offset here
        }
        
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

        ctx.textBaseline = 'middle';

        // Draw text
        ctx.fillStyle = "#fff";
        if (point.x < visibleMax / 2)
          ctx.fillText(text, point.x - (rectWidth / 2 + paddingX), y + rectHeight / 2 + paddingY / 2);
        else
          ctx.fillText(text, point.x + (rectWidth / 2 + paddingX), y + rectHeight / 2 + paddingY / 2);

        ctx.restore();
      });
    });
  }
});

Chart.register({
  id: 'legendMargin',
  beforeInit(chart: any) {
    if (!chart.legend) return; // Safeguard for undefined legend
    const originalFit = chart.legend.fit;
    chart.legend.fit = function fit() {
      originalFit.bind(chart.legend)();
      this.height += 20; // <-- Adjust this value for more/less space
    };
  }
});
