import { HttpErrorResponse, HttpEventType } from '@angular/common/http';
import { Component, OnInit, ViewChild } from '@angular/core';
import { FormBuilder, FormGroup, Validators } from '@angular/forms';
import { ToastrService } from 'ngx-toastr';
import { FileUploadHandlerEvent, FileUpload } from 'primeng/fileupload';
import { map, Observable, shareReplay, startWith } from 'rxjs';
import { GithubUpdateService } from 'src/app/services/github-update.service';
import { LoadingService } from 'src/app/services/loading.service';
import { SystemService } from 'src/app/services/system.service';
import { eASICModel } from 'src/models/enum/eASICModel';

@Component({
  selector: 'app-settings',
  templateUrl: './settings.component.html',
  styleUrls: ['./settings.component.scss']
})
export class SettingsComponent implements OnInit {

  public form!: FormGroup;

  public firmwareUpdateProgress: number | null = null;
  public websiteUpdateProgress: number | null = null;


  public eASICModel = eASICModel;
  public ASICModel!: eASICModel;

  public checkLatestRelease: boolean = false;
  public latestRelease$: Observable<any>;

  public info$: Observable<any>;

  @ViewChild('firmwareUpload') firmwareUpload!: FileUpload;
  @ViewChild('websiteUpload') websiteUpload!: FileUpload;

  public autotuneForm!: FormGroup;
  public autotuneInfo: any = {};

  constructor(
    private fb: FormBuilder,
    private systemService: SystemService,
    private toastr: ToastrService,
    private toastrService: ToastrService,
    private loadingService: LoadingService,
    private githubUpdateService: GithubUpdateService
  ) {



    this.latestRelease$ = this.githubUpdateService.getReleases().pipe(map(releases => {
      return releases[0];
    }));

    this.info$ = this.systemService.getInfo().pipe(shareReplay({refCount: true, bufferSize: 1}))


      this.info$.pipe(this.loadingService.lockUIUntilComplete())
      .subscribe(info => {
        this.ASICModel = info.ASICModel;
        this.form = this.fb.group({
          display: [info.display, [Validators.required]],
          flipscreen: [info.flipscreen == 1],
          invertscreen: [info.invertscreen == 1],
          displayTimeout: [info.displayTimeout, [Validators.required]],
          stratumURL: [info.stratumURL, [
            Validators.required,
            Validators.pattern(/^(?!.*stratum\+tcp:\/\/).*$/),
            Validators.pattern(/^[^:]*$/),
          ]],
          stratumPort: [info.stratumPort, [
            Validators.required,
            Validators.pattern(/^[^:]*$/),
            Validators.min(0),
            Validators.max(65535)
          ]],
          stratumUser: [info.stratumUser, [Validators.required]],
          stratumPassword: ['*****', [Validators.required]],
          coreVoltage: [info.coreVoltage, [Validators.required]],
          frequency: [info.frequency, [Validators.required]],
          autofanspeed: [info.autofanspeed == 1, [Validators.required]],
          temptarget: [info.temptarget, [Validators.required]],
          fanspeed: [info.fanspeed, [Validators.required]],
        });

        this.form.controls['autofanspeed'].valueChanges.pipe(
          startWith(this.form.controls['autofanspeed'].value)
        ).subscribe(autofanspeed => {
          if (autofanspeed) {
            this.form.controls['fanspeed'].disable();
            this.form.controls['temptarget'].enable();
          } else {
            this.form.controls['fanspeed'].enable();
            this.form.controls['temptarget'].disable();
          }
        });
      });

  }
  ngOnInit() {
    this.autotuneForm = this.fb.group({
    power_limit: [20, [Validators.required, Validators.min(1)]],
    fan_limit: [75, [Validators.required, Validators.min(1)]],
    max_voltage_asic: [1400, [Validators.required, Validators.min(1)]],
    max_frequency_asic: [1000, [Validators.required, Validators.min(1)]],
    max_asic_temperatur: [65, [Validators.required, Validators.min(1)]],
  });

  // Load autotune settings from API and patch the form if available
  this.systemService.getAutotune().subscribe({
    next: autotune => {
      this.autotuneInfo = autotune;
      this.autotuneForm.patchValue({
        power_limit: autotune.power_limit ?? 20,
        fan_limit: autotune.fan_limit ?? 75,
        max_voltage_asic: autotune.max_voltage_asic ?? 1400,
        max_frequency_asic: autotune.max_frequency_asic ?? 1000,
        max_asic_temperatur: autotune.max_asic_temperatur ?? 65,
      });
    },
    error: err => { this.toastr.error('Failed to load autotune settings'); }
  });
  }

  public updateSystem() {

    const form = this.form.getRawValue();

    form.frequency = parseInt(form.frequency);
    form.coreVoltage = parseInt(form.coreVoltage);

    // bools to ints
    form.flipscreen = form.flipscreen == true ? 1 : 0;
    form.invertscreen = form.invertscreen == true ? 1 : 0;
    form.autofanspeed = form.autofanspeed == true ? 1 : 0;

    if (form.stratumPassword === '*****') {
      delete form.stratumPassword;
    }

    this.systemService.updateSystem(undefined, form)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: () => {
          this.toastr.success('Success!', 'Saved.');
        },
        error: (err: HttpErrorResponse) => {
          this.toastr.error('Error.', `Could not save. ${err.message}`);
        }
      });
  }

  otaUpdate(event: FileUploadHandlerEvent) {
    const file = event.files[0];
    this.firmwareUpload.clear(); // clear the file upload component

    if (file.name != 'esp-miner.bin') {
      this.toastrService.error('Incorrect file, looking for esp-miner.bin.', 'Error');
      return;
    }

    this.systemService.performOTAUpdate(file)
      .pipe(this.loadingService.lockUIUntilComplete())
      .subscribe({
        next: (event) => {
          if (event.type === HttpEventType.UploadProgress) {
            this.firmwareUpdateProgress = Math.round((event.loaded / (event.total as number)) * 100);
          } else if (event.type === HttpEventType.Response) {
            if (event.ok) {
              this.toastrService.success('Firmware updated', 'Success!');

            } else {
              this.toastrService.error(event.statusText, 'Error');
            }
          }
          else if (event instanceof HttpErrorResponse)
          {
            this.toastrService.error(event.error, 'Error');
          }
        },
        error: (err) => {
          this.toastrService.error(err.error, 'Error');
        },
        complete: () => {
          this.firmwareUpdateProgress = null;
        }
      });
  }

  otaWWWUpdate(event: FileUploadHandlerEvent) {
    const file = event.files[0];
    this.websiteUpload.clear(); // clear the file upload component

    if (file.name != 'www.bin') {
      this.toastrService.error('Incorrect file, looking for www.bin.', 'Error');
      return;
    }

    this.systemService.performWWWOTAUpdate(file)
      .pipe(
        this.loadingService.lockUIUntilComplete(),
      ).subscribe({
        next: (event) => {
          if (event.type === HttpEventType.UploadProgress) {
            this.websiteUpdateProgress = Math.round((event.loaded / (event.total as number)) * 100);
          } else if (event.type === HttpEventType.Response) {
            if (event.ok) {
              this.toastrService.success('Website updated', 'Success!');
              setTimeout(() => {
                window.location.reload();
              }, 2000);
            } else {
              this.toastrService.error(event.statusText, 'Error');
            }
          }
          else if (event instanceof HttpErrorResponse)
          {
            const errorMessage = event.error?.message || event.message || 'Unknown error occurred';
            this.toastrService.error(errorMessage, 'Error');
          }
        },
        error: (err) => {
          const errorMessage = err.error?.message || err.message || 'Unknown error occurred';
          this.toastrService.error(errorMessage, 'Error');
        },
        complete: () => {
          this.websiteUpdateProgress = null;
        }
      });
  }

  public restart() {
    this.systemService.restart().subscribe(res => {

    });
    this.toastr.success('Success!', 'Bitaxe restarted');
  }

  public updateAutotune() {
    if (!this.autotuneForm.valid) return;
    this.systemService.updateAutotune(this.autotuneForm.value)
      .subscribe({
        next: () => this.toastr.success('Autotune settings saved!', 'Success'),
        error: (err: HttpErrorResponse) => this.toastr.error('Could not save autotune settings.', err.message)
      });
  }
}
