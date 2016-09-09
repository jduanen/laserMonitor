/*
 * MicroView Arduino Console for Laser Cutter that shows power and temperature.
 *
 * This was designed to work for a Shapeoko2 with a J-Tech laser.
 * All values here are for use with a 9mm 3.8W 445nm J-Tech laser with G2 lens
 *  and using the J-Tech High Current Driver Board.
 */

#include <MicroView.h>
#include <OneWire.h>
#include <DallasTemperature.h>


// jumpers on driver board all inserted to get 2.5A output
#define MAX_CURRENT 2.5

// Gauge is in units of mA
#define MAX_VAL int(MAX_CURRENT * 1000)

// convert 10bit ADC value to voltage (scaled for 5V operation)
#define VOLTAGE(val) (((val) * (5 / 1023.0)) * 3.61)

// current limiting resistor values
#define R05 5.5     // 5.5ohms for 0.5A
#define R10 2.75    // 2.75ohms for 1.0A
#define R15 1.83    // 1.83ohms for 1.5A
#define R20 1.38    // 1.38ohms for 2.0A
#define R25 1.1     // 1.1ohms for 2.5A

// select current limiting resistor value to be used (set by jumpers 1-5)
#define R_VAL R25

// G2 Laser optical gain (multiply by current to get power at work surface)
#define OPTICAL_GAIN 1.22

// number of samples to filter over
#define N 8

// number of iterations to dwell on a given meter (when alternating)
#define DWELL_COUNT 50

#define MIN_TEMP 1.0
#define MAX_TEMP 99.0

#define REGULATOR_VOLTAGE A0
#define DIODE_VOLTAGE A1
#define TEMP_SENSOR 2
#define SWITCH1 5
#define SWITCH2 6


MicroViewWidget *powerGauge, *tempSlider;
float current[N] = { 0.0 };
uint16_t indx = 0;
int iter = 0;

OneWire oneWire(TEMP_SENSOR);

DallasTemperature sensors(&oneWire);


// Run once at startup to set things up
// Starts the MicroView library, initializes all of the meters, and clears
//  the screen.
void setup() {
    pinMode(SWITCH1, INPUT_PULLUP);
    pinMode(SWITCH2, INPUT_PULLUP);

    uView.begin();
    setupPower(true);
    setupTemperature(true);
    uView.clear(PAGE);

    sensors.begin();
    sensors.setResolution(9);
    sensors.setWaitForConversion(true);
}


//// TODO remove unused
void stopAll() {
    delete powerGauge;
    delete tempSlider;
}


// Read the selector switch bits and return an integer between
//  0 and 3 that indicates the current selector switch setting.
// returns:
//   0: rotate through all meters
//   1: show power Gauge
//   2: show temperature slider
//   3: UNUSED
uint16_t getSelector() {
    uint8_t sel = 0x00;
    if (digitalRead(SWITCH2) == LOW) {
        sel = 0x02;
    }
    if (digitalRead(SWITCH1) == LOW) {
        sel |= 0x01;
    }
    return sel;
}


// Return a value that cycles among values within a given range, dwelling on
//  each value for a given number of iterations.
// param max: max value of range to cycle through
// param dwell: number of iterations to stay on each value
// returns: value in range [1-'max']
uint16_t getMeter(uint16_t num, uint16_t dwell) {
    uint16_t val;

    val = (((iter / dwell) % num) + 1);
    iter++;
    return val;
}


// The main loop
// Update and display one of the meters (based on the selector setting and
//  iteration).
// Updates the desired meter, displays the result, and does a short delay
//  before the next iteration.
void loop() {
    uint16_t sel, meter, delayTime;

    sel = getSelector();
    switch (sel) {
    case 0:
        meter = getMeter(2, DWELL_COUNT);
        break;
    case 1:
    case 2:
        meter = sel;
        break;
    default:
        meter = 1;
        break;
    }

    switch (meter) {
    case 0:
        uView.clear(PAGE);
        break;
    case 1:
        setupPower(false);
        updatePower();
        delayTime = 100;
        break;
    case 2:
        setupTemperature(false);
        updateTemperature();
        delayTime = 100;
        break;
    }

    uView.display();
    delay(delayTime);
}


// Prepare the Power meter
// param init: if true, then create the widget, else redraw it
void setupPower(boolean init) {
    uView.clear(PAGE);
    uView.setFontType(1);
    uView.setColor(WHITE);
    if (init == true) {
        powerGauge = new MicroViewGauge(32, 23, 0, MAX_VAL,
                                        (WIDGETSTYLE1 + WIDGETNOVALUE));
    } else {
        powerGauge->reDraw();
    }
    uView.rectFill(2, 36, (uView.getFontWidth() * 8), uView.getFontHeight(),
                   WHITE, NORM);
    uView.setColor(BLACK);
}


// Get new current sample, process it, and update the power display.
void updatePower() {
    int16_t i, val;
    float avgCurrent;

    // get new raw value and process it (just average samples for now)
    i = (indx++ % N);
    //// TODO consider discarding outliers
    current[i] = getCurrent();

    //// TODO consider a trailing exponential average here
    avgCurrent = 0.0;
    for (i = 0; i < N; i++) {
        avgCurrent += current[i];
    }
    avgCurrent = (avgCurrent / N);

    // update dial
    val = int(avgCurrent * 1000);
    powerGauge->setValue(val);

    // update numeric strip
    uView.setCursor(powerGauge->getX() - 21, powerGauge->getY() + 13);
    uView.print(avgCurrent, 3);
}


// Read voltages, compute current, clip to min/max range, and return current.
float getCurrent() {
    float v1, v2, current;

    v1 = VOLTAGE(analogRead(A0));
    v2 = VOLTAGE(analogRead(A1));
    current = ((v1 - v2) / R_VAL);

    // clip to min/max levels
    if (current > MAX_CURRENT) {
        current = MAX_CURRENT;
    }
    if (current < 0.0) {
        current = 0.0;
    }

    return current;
}


// Prepare the Temperature meter
// param init: if true, then create the widget, else redraw it
void setupTemperature(boolean init) {
    uView.clear(PAGE);
    uView.setFontType(2);
    uView.setColor(WHITE);
    if (init == true) {
        tempSlider = new MicroViewSlider(0, 34, MIN_TEMP, MAX_TEMP,
                                         (WIDGETSTYLE1 + WIDGETNOVALUE));
    } else {
       tempSlider->reDraw();
    }
}


// Read temperature, update bar graph, print new value
void updateTemperature() {
    uint8_t offsetX, offsetY, w, h;
    int16_t val;

    val = (uint16_t)getTemperature();
    tempSlider->setValue(val);
    offsetY = tempSlider->getY() - 25;
    offsetX = tempSlider->getX() + 14;
    uView.setCursor(offsetX, offsetY);
    uView.rectFill(offsetX, offsetY,
                   (uView.getFontWidth() * 5), uView.getFontHeight(),
                   BLACK, NORM);
    offsetX += ((tempSlider->getMaxValLen() - tempSlider->getValLen()) * 4);
    uView.setCursor(offsetX, offsetY);
    uView.print(val);
    uView.setFontType(1);
    uView.print(" C");
    uView.setFontType(2);
}


// Read the temperature sensor, clip the data to a good range, and
//  return the temp in degrees C.
float getTemperature() {
    float val;
    sensors.requestTemperatures();
    val = sensors.getTempCByIndex(0);

    if (val < MIN_TEMP) {
        val = MIN_TEMP;
    }
    if (val > MAX_TEMP) {
        val = MAX_TEMP;
    }
    return val;
}
