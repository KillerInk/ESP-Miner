import { ComponentFixture, TestBed } from '@angular/core/testing';
import { provideHttpClient } from '@angular/common/http';
import { provideToastr } from 'ngx-toastr';

import { AutotuneComponent } from './autotune.component';

describe('AutotuneComponent', () => {
  let component: AutotuneComponent;
  let fixture: ComponentFixture<AutotuneComponent>;

  beforeEach(() => {
    TestBed.configureTestingModule({
      imports: [AutotuneComponent],
      providers: [provideHttpClient(), provideToastr()]
    });
    fixture = TestBed.createComponent(AutotuneComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  it('should create', () => {
    expect(component).toBeTruthy();
  });
});
