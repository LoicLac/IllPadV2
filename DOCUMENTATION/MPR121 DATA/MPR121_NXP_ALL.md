
# Proximity Capacitive Touch Sensor Controller

The MPR121 is the second generation sensor controller following the initial release of the MPR03x series of devices. The MPR121 features an increased internal intelligence plus Freescale’s second generation capacitance detection engine. Some major enhancements include an increased electrode count, a hardware configurable I²C address, an expanded filtering system with debounce, and completely independent electrodes with built-in autoconfiguration. The device also features a 13th simulated electrode which represents the simultaneous charging of all the electrodes connected together. When used with a touch panel or touch screen array, the 13th simulated electrode allows a greater near proximity detection distance and an increased sensing area.

## Features
* 1.71V to 3.6V operation
* 29 μA typical run current at 16 ms sampling interval
* 3 μA in scan stop mode current
* 12 electrodes/capacitance sensing inputs in which 8 are multifunctional for LED driving and GPIO
* Integrated independent autocalibration for each electrode input
* Autoconfiguration of charge current and charge time for each electrode input
* Separate touch and release trip thresholds for each electrode, providing hysteresis and electrode independence
* I²C interface, with IRQ Interrupt output to advise electrode status changes
* 3 mm x 3 mm x 0.65 mm 20 lead QFN package
* -40°C to +85°C operating temperature range

## Implementations
* General Purpose Capacitance Detection
* Switch Replacements
* Touch Pads, Touch Wheel, Touch Slide Bar, Touch Screen Panel
* Capacitance Near Proximity Detection

## Typical Applications
* PC Peripherals
* MP3 Players
* Remote Controls
* Mobile Phones
* Lighting Controls

## Ordering Information

<table>
<thead>
<tr>
<th>Device Name</th>
<th>Temperature Range</th>
<th>Case Number</th>
<th>Touch Pads</th>
<th>I²C Address</th>
<th>Shipping</th>
</tr>
</thead>
<tbody>
<tr>
<td>MPR121QR2</td>
<td>-40°C to +85°C</td>
<td>2059 (20-Pin QFN)</td>
<td>12-pads</td>
<td>0x5A - 0x5D</td>
<td>Tape & Reel</td>
</tr>
</tbody>
</table>

## MPR121 Package Views and Pin Connections

**Bottom View**  
20-PIN QFN  
CASE 2059-01

**Top View**

<table>
<thead>
<tr>
<th colspan="5">V<sub>DD</sub></th>
<th colspan="5"></th>
<th colspan="5"></th>
</tr>
<tr>
<th>20</th>
<th>19</th>
<th>18</th>
<th>17</th>
<th>16</th>
<th></th>
<th></th>
<th></th>
<th></th>
<th></th>
<th></th>
<th></th>
<th></th>
<th></th>
<th></th>
</tr>
</thead>
<tbody>
<tr>
<td>IRQ (1)</td>
<td>SCL (2)</td>
<td>SDA (3)</td>
<td>ADDR (4)</td>
<td>VREG (5)</td>
<td></td>
<td>6</td>
<td>7</td>
<td>8</td>
<td>9</td>
<td>10</td>
<td>ELE7 (15)</td>
<td>ELE6 (14)</td>
<td>ELE5 (13)</td>
<td>ELE4 (12)</td>
</tr>
<tr>
<td></td>
<td></td>
<td></td>
<td></td>
<td></td>
<td></td>
<td></td>
<td></td>
<td></td>
<td></td>
<td></td>
<td>ELE3 (11)</td>
<td></td>
<td></td>
<td></td>
</tr>
<tr>
<td></td>
<td></td>
<td></td>
<td></td>
<td></td>
<td>V<sub>SS</sub></td>
<td>REXT</td>
<td>ELE0</td>
<td>ELE1</td>
<td>ELE2</td>
<td></td>
<td></td>
<td></td>
<td></td>
<td></td>
</tr>
</tbody>
</table>



---


# 1 Pin Descriptions

Table 1. Pin Descriptions

<table>
<thead>
<tr>
<th>Pin No.</th>
<th>Pin Name</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr>
<td>1</td>
<td>IRQ</td>
<td>Open Collector Interrupt Output Pin, active low</td>
</tr>
<tr>
<td>2</td>
<td>SCL</td>
<td>I<sup>2</sup>C Clock</td>
</tr>
<tr>
<td>3</td>
<td>SDA</td>
<td>I<sup>2</sup>C Data</td>
</tr>
<tr>
<td>4</td>
<td>ADDR</td>
<td>I<sup>2</sup>C Address Select Input Pin. Connect the ADDR pin to the VSS, VDD, SDA or SCL line, the resulting I<sup>2</sup>C addresses are 0x5A, 0x5B, 0x5C and 0x5D respectively</td>
</tr>
<tr>
<td>5</td>
<td>VREG</td>
<td>Internal Regulator Node – Connect a 0.1 μF bypass cap to VSS</td>
</tr>
<tr>
<td>6</td>
<td>VSS</td>
<td>Ground</td>
</tr>
<tr>
<td>7</td>
<td>REXT</td>
<td>External Resistor – Connect a 75 kΩ 1% resistor to VSS to set internal reference current</td>
</tr>
<tr>
<td>8</td>
<td>ELE0</td>
<td>Electrode 0</td>
</tr>
<tr>
<td>9</td>
<td>ELE1</td>
<td>Electrode 1</td>
</tr>
<tr>
<td>10</td>
<td>ELE2</td>
<td>Electrode 2</td>
</tr>
<tr>
<td>11</td>
<td>ELE3</td>
<td>Electrode 3</td>
</tr>
<tr>
<td>12</td>
<td>ELE4</td>
<td>Electrode 4</td>
</tr>
<tr>
<td>13</td>
<td>ELE5</td>
<td>Electrode 5</td>
</tr>
<tr>
<td>14</td>
<td>ELE6</td>
<td>Electrode 6</td>
</tr>
<tr>
<td>15</td>
<td>ELE7</td>
<td>Electrode 7</td>
</tr>
<tr>
<td>16</td>
<td>ELE8</td>
<td>Electrode 8</td>
</tr>
<tr>
<td>17</td>
<td>ELE9</td>
<td>Electrode 9</td>
</tr>
<tr>
<td>18</td>
<td>ELE10</td>
<td>Electrode 10</td>
</tr>
<tr>
<td>19</td>
<td>ELE11</td>
<td>Electrode 11</td>
</tr>
<tr>
<td>20</td>
<td>VDD</td>
<td>Connect a 0.1 μF bypass cap to VSS</td>
</tr>
</tbody>
</table>



---


# Schematic Drawings and Implementation

<table>
<thead>
<tr>
  <th colspan="7">Power Configuration 1: MPR121 runs from a 1.71V to 2.75V supply.</th>
</tr>
</thead>
<tbody>
<tr>
  <td>VDD 1.71V to 2.75V</td>
  <td colspan="2">0.1 μF</td>
  <td>20</td>
  <td>VDD</td>
  <td>ELE11/LED7</td>
  <td>19</td>
</tr>
<tr>
  <td></td>
  <td></td>
  <td></td>
  <td>6</td>
  <td>VSS</td>
  <td>ELE10/LED6</td>
  <td>18</td>
</tr>
<tr>
  <td></td>
  <td></td>
  <td></td>
  <td>5</td>
  <td>VREG</td>
  <td>ELE9/LED5</td>
  <td>17</td>
</tr>
<tr>
  <td></td>
  <td></td>
  <td></td>
  <td>1</td>
  <td>IRQ</td>
  <td>ELE8/LED4</td>
  <td>16</td>
</tr>
<tr>
  <td></td>
  <td></td>
  <td></td>
  <td>2</td>
  <td>SCL</td>
  <td>ELE7/LED3</td>
  <td>15</td>
</tr>
<tr>
  <td></td>
  <td></td>
  <td></td>
  <td>3</td>
  <td>SDA</td>
  <td>ELE6/LED2</td>
  <td>14</td>
</tr>
<tr>
  <td></td>
  <td></td>
  <td></td>
  <td>4</td>
  <td>ADDR</td>
  <td>ELE5/LED1</td>
  <td>13</td>
</tr>
<tr>
  <td></td>
  <td></td>
  <td rowspan="3" style="text-align:center;">75 kΩ 1%</td>
  <td>7</td>
  <td>REXT</td>
  <td>ELE4/LED0</td>
  <td>12</td>
</tr>
<tr>
  <td>GND</td>
  <td></td>
  <td></td>
  <td>ELE3</td>
  <td></td>
  <td>11</td>
</tr>
<tr>
  <td>GND</td>
  <td></td>
  <td></td>
  <td>ELE2</td>
  <td></td>
  <td>10</td>
</tr>
<tr>
  <td></td>
  <td></td>
  <td></td>
  <td>ELE1</td>
  <td></td>
  <td>9</td>
</tr>
<tr>
  <td></td>
  <td></td>
  <td></td>
  <td>ELE0</td>
  <td></td>
  <td>8</td>
</tr>
<tr>
  <td colspan="7" style="text-align:center;">MPR121Q TOUCH SENSOR</td>
</tr>
</tbody>
</table>

<table>
<thead>
<tr>
  <th colspan="7">Power Configuration 2: MPR121 runs from a 2.0V to 3.6V supply.</th>
</tr>
</thead>
<tbody>
<tr>
  <td>VDD 2.0V to 3.6V</td>
  <td colspan="2">0.1 μF</td>
  <td>20</td>
  <td>VDD</td>
  <td>ELE11/LED7</td>
  <td>19</td>
</tr>
<tr>
  <td></td>
  <td></td>
  <td></td>
  <td>6</td>
  <td>VSS</td>
  <td>ELE10/LED6</td>
  <td>18</td>
</tr>
<tr>
  <td></td>
  <td></td>
  <td></td>
  <td>5</td>
  <td>VREG</td>
  <td>ELE9/LED5</td>
  <td>17</td>
</tr>
<tr>
  <td></td>
  <td></td>
  <td></td>
  <td>1</td>
  <td>IRQ</td>
  <td>ELE8/LED4</td>
  <td>16</td>
</tr>
<tr>
  <td></td>
  <td></td>
  <td></td>
  <td>2</td>
  <td>SCL</td>
  <td>ELE7/LED3</td>
  <td>15</td>
</tr>
<tr>
  <td></td>
  <td></td>
  <td></td>
  <td>3</td>
  <td>SDA</td>
  <td>ELE6/LED2</td>
  <td>14</td>
</tr>
<tr>
  <td></td>
  <td></td>
  <td></td>
  <td>4</td>
  <td>ADDR</td>
  <td>ELE5/LED1</td>
  <td>13</td>
</tr>
<tr>
  <td></td>
  <td></td>
  <td rowspan="3" style="text-align:center;">75 kΩ 1%</td>
  <td>7</td>
  <td>REXT</td>
  <td>ELE4/LED0</td>
  <td>12</td>
</tr>
<tr>
  <td></td>
  <td>0.1 μF</td>
  <td></td>
  <td>ELE3</td>
  <td></td>
  <td>11</td>
</tr>
<tr>
  <td>GND</td>
  <td>GND</td>
  <td></td>
  <td>ELE2</td>
  <td></td>
  <td>10</td>
</tr>
<tr>
  <td>GND</td>
  <td></td>
  <td></td>
  <td>ELE1</td>
  <td></td>
  <td>9</td>
</tr>
<tr>
  <td></td>
  <td></td>
  <td></td>
  <td>ELE0</td>
  <td></td>
  <td>8</td>
</tr>
<tr>
  <td colspan="7" style="text-align:center;">MPR121Q TOUCH SENSOR</td>
</tr>
</tbody>
</table>



---


# 3 Device Operation Overview

## Power Supply  
The VDD pin is the main power supply input to the MPR121 and is always decoupled with a 0.1 μF ceramic capacitor to the VSS. Excessive noise on the VDD should be avoided.  
The VDD pin has an operational voltage range specification between 1.71V to 3.6V. The internal voltage regulator, which generates current to internal circuitry, operates with an input range from 2.0V to 3.6V. To work with a power supply below 2.0V and to avoid the unnecessary voltage drop, the internal voltage regulator can be bypassed, refer to Figure 1 and Figure 2.  
When a power supply is in the range of 1.71V to 2.75V, the VDD and VREG pins can be connected together (Figure 1) so that internal voltage regulator is bypassed. In this configuration, the supply voltage cannot be higher than 2.75V as this is the maximum voltage limit for VREG pin.  
When a power supply is higher than 2.75V, it must be connected to the VDD, i.e. configuration as in Figure 2. In this configuration, a separate 0.1 μF decoupling ceramic capacitor on VREG to VSS is applied as a bypass cap for internal circuitry. This configuration can work with a VDD supply voltage down to 2.0V. For a typical two dry cell 1.5V batteries application, this configuration covers the entire expected working voltage range from 2.0V to 3.0V.

## Capacitance Sensing  
The MPR121 uses a constant DC current capacitance sensing scheme. It can measure capacitances ranging from 10 pF to over 2000 pF with a resolution up to 0.01 pF. The device does this by varying the amount of charge current and charge time applied to the sensing inputs.  
The 12 electrodes are controlled independently; this allows for a great deal of flexibility in electrode pattern design. An automatic configuration system is integrated as part of the device, this greatly simplifies the individual register setup. Please refer to the Freescale application note, AN3889, for more details.  
The voltage measured on the input sensing node is inversely proportional to the capacitance. At the end of each charge circle, this voltage is sampled by an internal 10-bit ADC. The sampled data is then processed through several stages of digital filtering. The digital filtering process allows for good noise immunity in different environments. For more information on the filtering system, refer to application note AN3890.

## Touch Sensing  
Once the electrode capacitance data is acquired, the electrode touch/release status is determined comparing it to the capacitance baseline value. The capacitance baseline is tracked by MPR121 automatically based on the background capacitance variation.  
The baseline value is compared with the current immediate electrode data to determine if a touch or release has occurred. A designer has the ability to set the touch/release thresholds, as well as a touch/release debounce time. This is to eliminate jitter and false touches due to noise. Additional information on baseline capacitance system is covered in application notes AN3891 and AN3892.

## Proximity Sensing  
One new feature of the MPR121 is the near proximity sensing system. This means that all of the system’s electrodes can be summed together to create a single large electrode. The major advantage of the large electrode is that is can cover a much larger sensing area. The near proximity sensing system can be used while at the same time having separate electrodes by using touch button sensing.  
Proximity detection is read as an independent channel and has configuration registers similar to the other 12 channels. When proximity detection is enabled, this “13th” measurement channel will be included at the beginning of a normal detection cycle. This system is described in application note AN3893.

## LED Driver  
Among the 12 electrode inputs, 8 inputs are designed as multifunctional pins. When these pins are not configured as electrodes, they may be used to drive LEDs or used for general purpose input or output. For more details on this feature, please refer to application note AN3894.

## Serial Communication  
The MPR121 is an Inter-Integrated Circuit (I²C) compliant device with an interrupt IRQ pin. This pin is triggered any time a touch or release is detected. The device has a configurable I²C address by connecting the ADDR pin to the VSS, VDD, SDA or SCL lines. This results in I²C addresses of 0x5A, 0x5B, 0x5C and 0x5D. The specific details of this system are described in AN3895. For reference, the register map of the MPR121 is included in **Table 2**.



---


# Table 2. Register Map

<table>
    <thead>
    <tr>
        <th>REGISTER</th>
        <th colspan="8">Fields</th>
        <th>Register

Address</th>
        <th>Initial

Value</th>
        <th>Auto-

Increment

Address</th>
    </tr>
    <tr>
        <th>ELE0 - ELE7 Touch Status</th>
        <th>ELE7</th>
        <th>ELE6</th>
        <th>ELE5</th>
        <th>ELE4</th>
        <th>ELE3</th>
        <th>ELE2</th>
        <th>ELE1</th>
        <th>ELE0</th>
        <th>0x00</th>
        <th>0x00</th>
        <th rowspan="49">Register
Address + 1</th>
    </tr>
    <tr>
        <th>ELE8 - ELE11, ELEPROX Touch Status</th>
        <th>OVCF</th>
        <th></th>
        <th></th>
        <th>ELEPROX</th>
        <th>ELE11</th>
        <th>ELE10</th>
        <th>ELE9</th>
        <th>ELE8</th>
        <th>0x01</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE0-7 OOR Status</th>
        <th>E7_OOR</th>
        <th>E6_OOR</th>
        <th>E5_OOR</th>
        <th>E4_OOR</th>
        <th>E3_OOR</th>
        <th>E2_OOR</th>
        <th>E1_OOR</th>
        <th>E0_OOR</th>
        <th>0x02</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE8-11, ELEPROX OOR Status</th>
        <th>ACFF</th>
        <th>ARFF</th>
        <th></th>
        <th>PROX_OOR</th>
        <th>E11_OOR</th>
        <th>E10_OOR</th>
        <th>E9_OOR</th>
        <th>E8_OOR</th>
        <th>0x03</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE0 Electrode Filtered Data LSB</th>
        <th colspan="8">EFD0LB</th>
        <th>0x04</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE0 Electrode Filtered Data MSB</th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th colspan="2">EFD0HB</th>
        <th>0x05</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE1 Electrode Filtered Data LSB</th>
        <th colspan="8">EFD1LB</th>
        <th>0x06</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE1 Electrode Filtered Data MSB</th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th colspan="2">EFD1HB</th>
        <th>0x07</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE2 Electrode Filtered Data LSB</th>
        <th colspan="8">EFD2LB</th>
        <th>0x08</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE2 Electrode Filtered Data MSB</th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th colspan="2">EFD2HB</th>
        <th>0x09</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE3 Electrode Filtered Data LSB</th>
        <th colspan="8">EFD3LB</th>
        <th>0x0A</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE3 Electrode Filtered Data MSB</th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th colspan="2">EFD3HB</th>
        <th>0x0B</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE4 Electrode Filtered Data LSB</th>
        <th colspan="8">EFD4LB</th>
        <th>0x0C</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE4 Electrode Filtered Data MSB</th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th colspan="2">EFD4HB</th>
        <th>0x0D</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE5 Electrode Filtered Data LSB</th>
        <th colspan="8">EFD5LB</th>
        <th>0x0E</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE5 Electrode Filtered Data MSB</th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th colspan="2">EFD5HB</th>
        <th>0x0F</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE6 Electrode Filtered Data LSB</th>
        <th colspan="8">EFD6LB</th>
        <th>0x10</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE6 Electrode Filtered Data MSB</th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th colspan="2">EFD6HB</th>
        <th>0x11</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE7 Electrode Filtered Data LSB</th>
        <th colspan="8">EFD7LB</th>
        <th>0x12</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE7 Electrode Filtered Data MSB</th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th colspan="2">EFD7HB</th>
        <th>0x13</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE8 Electrode Filtered Data LSB</th>
        <th colspan="8">EFD8LB</th>
        <th>0x14</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE8 Electrode Filtered Data MSB</th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th colspan="2">EFD8HB</th>
        <th>0x15</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE9 Electrode Filtered Data LSB</th>
        <th colspan="8">EFD9LB</th>
        <th>0x16</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE9 Electrode Filtered Data MSB</th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th colspan="2">EFD9HB</th>
        <th>0x17</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE10 Electrode Filtered Data LSB</th>
        <th colspan="8">EFD10LB</th>
        <th>0x18</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE10 Electrode Filtered Data MSB</th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th colspan="2">EFD10HB</th>
        <th>0x19</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE11 Electrode Filtered Data LSB</th>
        <th colspan="8">EFD11LB</th>
        <th>0x1A</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE11 Electrode Filtered Data MSB</th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th colspan="2">EFD11HB</th>
        <th>0x1B</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELEPROX Electrode Filtered Data LSB</th>
        <th colspan="8">EFDPROXLB</th>
        <th>0x1C</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELEPROX Electrode Filtered Data MSB</th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th colspan="2">EFDPROXHB</th>
        <th>0x1D</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE0 Baseline Value</th>
        <th colspan="8">E0BV</th>
        <th>0x1E</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE1 Baseline Value</th>
        <th colspan="8">E1BV</th>
        <th>0x1F</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE2 Baseline Value</th>
        <th colspan="8">E2BV</th>
        <th>0x20</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE3 Baseline Value</th>
        <th colspan="8">E3BV</th>
        <th>0x21</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE4 Baseline Value</th>
        <th colspan="8">E4BV</th>
        <th>0x22</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE5 Baseline Value</th>
        <th colspan="8">E5BV</th>
        <th>0x23</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE6 Baseline Value</th>
        <th colspan="8">E6BV</th>
        <th>0x24</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE7 Baseline Value</th>
        <th colspan="8">E7BV</th>
        <th>0x25</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE8 Baseline Value</th>
        <th colspan="8">E8BV</th>
        <th>0x26</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE9 Baseline Value</th>
        <th colspan="8">E9BV</th>
        <th>0x27</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE10 Baseline Value</th>
        <th colspan="8">E10BV</th>
        <th>0x28</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE11 Baseline Value</th>
        <th colspan="8">E11BV</th>
        <th>0x29</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELEPROX Baseline Value</th>
        <th colspan="8">EPROXBV</th>
        <th>0x2A</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>MHD Rising</th>
        <th></th>
        <th></th>
        <th colspan="6">MHDR</th>
        <th>0x2B</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>NHD Amount Rising</th>
        <th></th>
        <th></th>
        <th colspan="6">NHDR</th>
        <th>0x2C</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>NCL Rising</th>
        <th colspan="8">NCLR</th>
        <th>0x2D</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>FDL Rising</th>
        <th colspan="8">FDLR</th>
        <th>0x2E</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>MHD Falling</th>
        <th></th>
        <th></th>
        <th colspan="6">MHDF</th>
        <th>0x2F</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>NHD Amount Falling</th>
        <th></th>
        <th></th>
        <th colspan="6">NHDF</th>
        <th>0x30</th>
        <th>0x00</th>
    </tr></table>

MPR121


---


# Table 2. Register Map

<table>
    <thead>
    <tr>
        <th>REGISTER</th>
        <th colspan="6">Fields</th>
        <th>Register

Address</th>
        <th>Initial

Value</th>
        <th>Auto-

Increment

Address</th>
    </tr>
    <tr>
        <th>NCL Falling</th>
        <th colspan="6">NCLF</th>
        <th>0x31</th>
        <th>0x00</th>
        <th rowspan="49">Register
Address + 1</th>
    </tr>
    <tr>
        <th>FDL Falling</th>
        <th colspan="6">FDLF</th>
        <th>0x32</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>NHD Amount Touched</th>
        <th></th>
        <th></th>
        <th colspan="4">NHDT</th>
        <th>0x33</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>NCL Touched</th>
        <th colspan="6">NCLT</th>
        <th>0x34</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>FDL Touched</th>
        <th colspan="6">FDLT</th>
        <th>0x35</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELEPROX MHD Rising</th>
        <th></th>
        <th></th>
        <th colspan="4">MHDPROXR</th>
        <th>0x36</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELEPROX NHD Amount Rising</th>
        <th></th>
        <th></th>
        <th colspan="4">NHDPROXR</th>
        <th>0x37</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELEPROX NCL Rising</th>
        <th colspan="6">NCLPROXR</th>
        <th>0x38</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELEPROX FDL Rising</th>
        <th colspan="6">FDLPROXR</th>
        <th>0x39</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELEPROX MHD Falling</th>
        <th></th>
        <th></th>
        <th colspan="4">MHDPROXF</th>
        <th>0x3A</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELEPROX NHD Amount Falling</th>
        <th></th>
        <th></th>
        <th colspan="4">NHDPROXF</th>
        <th>0x3B</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELEPROX NCL Falling</th>
        <th colspan="6">NCLPROXF</th>
        <th>0x3C</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELEPROX FDL Falling</th>
        <th colspan="6">FDLPROXF</th>
        <th>0x3D</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELEPROX NHD Amount Touched</th>
        <th></th>
        <th></th>
        <th colspan="4">NHDPROXT</th>
        <th>0x3E</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELEPROX NCL Touched</th>
        <th colspan="6">NCLPROXT</th>
        <th>0x3F</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELEPROX FDL Touched</th>
        <th colspan="6">FDLPROXT</th>
        <th>0x40</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE0 Touch Threshold</th>
        <th colspan="6">E0TTH</th>
        <th>0x41</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE0 Release Threshold</th>
        <th colspan="6">E0RTH</th>
        <th>0x42</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE1 Touch Threshold</th>
        <th colspan="6">E1TTH</th>
        <th>0x43</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE1 Release Threshold</th>
        <th colspan="6">E1RTH</th>
        <th>0x44</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE2 Touch Threshold</th>
        <th colspan="6">E2TTH</th>
        <th>0x45</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE2 Release Threshold</th>
        <th colspan="6">E2RTH</th>
        <th>0x46</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE3 Touch Threshold</th>
        <th colspan="6">E3TTH</th>
        <th>0x47</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE3 Release Threshold</th>
        <th colspan="6">E3RTH</th>
        <th>0x48</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE4 Touch Threshold</th>
        <th colspan="6">E4TTH</th>
        <th>0x49</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE4 Release Threshold</th>
        <th colspan="6">E4RTH</th>
        <th>0x4A</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE5 Touch Threshold</th>
        <th colspan="6">E5TTH</th>
        <th>0x4B</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE5 Release Threshold</th>
        <th colspan="6">E5RTH</th>
        <th>0x4C</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE6 Touch Threshold</th>
        <th colspan="6">E6TTH</th>
        <th>0x4D</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE6 Release Threshold</th>
        <th colspan="6">E6RTH</th>
        <th>0x4E</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE7 Touch Threshold</th>
        <th colspan="6">E7TTH</th>
        <th>0x4F</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE7 Release Threshold</th>
        <th colspan="6">E7RTH</th>
        <th>0x50</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE8 Touch Threshold</th>
        <th colspan="6">E8TTH</th>
        <th>0x51</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE8 Release Threshold</th>
        <th colspan="6">E8RTH</th>
        <th>0x52</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE9 Touch Threshold</th>
        <th colspan="6">E9TTH</th>
        <th>0x53</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE9 Release Threshold</th>
        <th colspan="6">E9RTH</th>
        <th>0x54</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE10 Touch Threshold</th>
        <th colspan="6">E10TTH</th>
        <th>0x55</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE10 Release Threshold</th>
        <th colspan="6">E10RTH</th>
        <th>0x56</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE11 Touch Threshold</th>
        <th colspan="6">E11TTH</th>
        <th>0x57</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE11 Release Threshold</th>
        <th colspan="6">E11RTH</th>
        <th>0x58</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELEPROX Touch Threshold</th>
        <th colspan="6">EPROXTTH</th>
        <th>0x59</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELEPROX Release Threshold</th>
        <th colspan="6">EPROXRTH</th>
        <th>0x5A</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>Debounce Touch & Release</th>
        <th></th>
        <th colspan="3">DR</th>
        <th></th>
        <th>DT</th>
        <th>0x5B</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>Filter/Global CDC Configuration</th>
        <th colspan="2">FFI</th>
        <th colspan="4">CDC</th>
        <th>0x5C</th>
        <th>0x10</th>
    </tr>
    <tr>
        <th>Filter/Global CDT Configuration</th>
        <th colspan="3">CDT</th>
        <th colspan="2">SFI</th>
        <th>ESI</th>
        <th>0x5D</th>
        <th>0x24</th>
    </tr>
    <tr>
        <th>Electrode Configuration</th>
        <th colspan="2">CL</th>
        <th colspan="2">ELEPROX_EN</th>
        <th colspan="2">ELE_EN</th>
        <th>0x5E</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE0 Electrode Current</th>
        <th></th>
        <th></th>
        <th colspan="4">CDC0</th>
        <th>0x5F</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE1 Electrode Current</th>
        <th></th>
        <th></th>
        <th colspan="4">CDC1</th>
        <th>0x60</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE2 Electrode Current</th>
        <th></th>
        <th></th>
        <th colspan="4">CDC2</th>
        <th>0x61</th>
        <th>0x00</th>
    </tr></table>



---


NXP

Table 2. Register Map

<table>
    <thead>
    <tr>
        <th>REGISTER</th>
        <th colspan="8">Fields</th>
        <th>Register

Address</th>
        <th>Initial

Value</th>
        <th>Auto-

Increment

Address</th>
    </tr>
    <tr>
        <th>ELE3 Electrode Current</th>
        <th></th>
        <th></th>
        <th>CDC3</th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th>0x62</th>
        <th>0x00</th>
        <th rowspan="29">Register
Address + 1</th>
    </tr>
    <tr>
        <th>ELE4 Electrode Current</th>
        <th></th>
        <th></th>
        <th colspan="6">CDC4</th>
        <th>0x63</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE5 Electrode Current</th>
        <th></th>
        <th></th>
        <th colspan="6">CDC5</th>
        <th>0x64</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE6 Electrode Current</th>
        <th></th>
        <th></th>
        <th colspan="6">CDC6</th>
        <th>0x65</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE7 Electrode Current</th>
        <th></th>
        <th></th>
        <th colspan="6">CDC7</th>
        <th>0x66</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE8 Electrode Current</th>
        <th></th>
        <th></th>
        <th colspan="6">CDC8</th>
        <th>0x67</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE9 Electrode Current</th>
        <th></th>
        <th></th>
        <th colspan="6">CDC9</th>
        <th>0x68</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE10 Electrode Current</th>
        <th></th>
        <th></th>
        <th colspan="6">CDC10</th>
        <th>0x69</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE11 Electrode Current</th>
        <th></th>
        <th></th>
        <th colspan="6">CDC11</th>
        <th>0x6A</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELEPROX Electrode Current</th>
        <th></th>
        <th></th>
        <th colspan="6">CDCPROX</th>
        <th>0x6B</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE0, ELE1 Charge Time</th>
        <th></th>
        <th colspan="3">CDT1</th>
        <th></th>
        <th colspan="3">CDT0</th>
        <th>0x6C</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE2, ELE3 Charge Time</th>
        <th></th>
        <th colspan="3">CDT3</th>
        <th></th>
        <th colspan="3">CDT2</th>
        <th>0x6D</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE4, ELE5 Charge Time</th>
        <th></th>
        <th colspan="3">CDT5</th>
        <th></th>
        <th colspan="3">CDT4</th>
        <th>0x6E</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE6, ELE7 Charge Time</th>
        <th></th>
        <th colspan="3">CDT7</th>
        <th></th>
        <th colspan="3">CDT6</th>
        <th>0x6F</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE8, ELE9 Charge Time</th>
        <th></th>
        <th colspan="3">CDT9</th>
        <th></th>
        <th colspan="3">CDT8</th>
        <th>0x70</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELE10, ELE11 Charge Time</th>
        <th></th>
        <th colspan="3">CDT11</th>
        <th></th>
        <th colspan="3">CDT10</th>
        <th>0x71</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>ELEPROX Charge Time</th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th colspan="3">CDTPROX</th>
        <th>0x72</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>GPIO Control Register 0</th>
        <th>CTL011</th>
        <th>CTL010</th>
        <th>CTL09</th>
        <th>CTL08</th>
        <th>CTL07</th>
        <th>CTL06</th>
        <th>CTL05</th>
        <th>CTL04</th>
        <th>0x73</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>GPIO Control Register 1</th>
        <th>CTL111</th>
        <th>CTL110</th>
        <th>CTL19</th>
        <th>CTL18</th>
        <th>CTL17</th>
        <th>CTL16</th>
        <th>CTL15</th>
        <th>CTL14</th>
        <th>0x74</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>GPIO Data Register</th>
        <th>DAT11</th>
        <th>DAT10</th>
        <th>DAT9</th>
        <th>DAT8</th>
        <th>DAT7</th>
        <th>DAT6</th>
        <th>DAT5</th>
        <th>DAT4</th>
        <th>30x75</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>GPIO Direction Register</th>
        <th>DIR11</th>
        <th>DIR10</th>
        <th>DIR9</th>
        <th>DIR8</th>
        <th>DIR7</th>
        <th>DIR6</th>
        <th>DIR5</th>
        <th>DIR4</th>
        <th>0x76</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>GPIO Enable Register</th>
        <th>EN11</th>
        <th>EN10</th>
        <th>EN9</th>
        <th>EN8</th>
        <th>EN7</th>
        <th>EN6</th>
        <th>EN5</th>
        <th>EN4</th>
        <th>0x77</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>GPIO Data Set Register</th>
        <th>SET11</th>
        <th>SET10</th>
        <th>SET9</th>
        <th>SET8</th>
        <th>SET7</th>
        <th>SET6</th>
        <th>SET5</th>
        <th>SET4</th>
        <th>0x78</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>GPIO Data Clear Register</th>
        <th>CLR11</th>
        <th>CLR10</th>
        <th>CLR9</th>
        <th>CLR8</th>
        <th>CLR7</th>
        <th>CLR6</th>
        <th>CLR5</th>
        <th>CLR4</th>
        <th>0x79</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>GPIO Data Toggle Register</th>
        <th>TOG11</th>
        <th>TOG10</th>
        <th>TOG9</th>
        <th>TOG8</th>
        <th>TOG7</th>
        <th>TOG6</th>
        <th>TOG5</th>
        <th>TOG4</th>
        <th>0x7A</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>AUTO-CONFIG Control Register 0</th>
        <th colspan="2">FFI</th>
        <th colspan="2">RETRY</th>
        <th colspan="2">BVA</th>
        <th>ARE</th>
        <th>ACE</th>
        <th>0x7B</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>AUTO-CONFIG Control Register 1</th>
        <th>SCTS</th>
        <th></th>
        <th></th>
        <th></th>
        <th></th>
        <th>OORIE</th>
        <th>ARFIE</th>
        <th>ACFIE</th>
        <th>0x7C</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>AUTO-CONFIG USL Register</th>
        <th colspan="8">USL</th>
        <th>0x7D</th>
        <th>0x00</th>
    </tr>
    <tr>
        <th>AUTO-CONFIG LSL Register</th>
        <th colspan="8">LSL</th>
        <th>0x7E</th>
        <th>0x00</th>
    </tr>
    </thead>
    <tr>
        <td>AUTO-CONFIG Target Level Register</td>
        <td colspan="8">TL</td>
        <td>0x7F</td>
        <td>0x00</td>
        <td>0x00</td>
    </tr>
    <tr>
        <td>Soft Reset Register</td>
        <td colspan="8">SRST</td>
        <td>0x80</td>
        <td></td>
        <td></td>
    </tr></table>

MPR121
Sensors
Freescale Semiconductor, Inc.


---


# 4 Electrical Characteristics

## 4.1 Absolute Maximum Ratings
Absolute maximum ratings are stress ratings only and functional operation at the maxima is not guaranteed. Stress beyond the limits specified in Table 3 may affect device reliability or cause permanent damage to the device. For functional operating conditions, refer to the remaining tables in this section. This device contains circuitry protecting against damage due to high-static voltage or electrical fields; however, it is advised that normal precautions be taken to avoid application of any voltages higher than maximum-rated voltages to this high-impedance circuit.

<table>
<thead>
<tr>
<th>Rating</th>
<th>Symbol</th>
<th>Value</th>
<th>Unit</th>
</tr>
</thead>
<tbody>
<tr>
<td>Supply Voltage</td>
<td>V<sub>DD</sub></td>
<td>-0.3 to +3.6</td>
<td>V</td>
</tr>
<tr>
<td>Supply Voltage</td>
<td>V<sub>REG</sub></td>
<td>-0.3 to +2.75</td>
<td>V</td>
</tr>
<tr>
<td>Input Voltage<br>SCL, SDA, IRQ</td>
<td>V<sub>IN</sub></td>
<td>V<sub>SS</sub> - 0.3 to V<sub>DD</sub> + 0.3</td>
<td>V</td>
</tr>
<tr>
<td>Operating Temperature Range</td>
<td>T<sub>O</sub></td>
<td>-40 to +85</td>
<td>°C</td>
</tr>
<tr>
<td>GPIO Source Current per Pin</td>
<td>i<sub>GPIO</sub></td>
<td>12</td>
<td>mA</td>
</tr>
<tr>
<td>GPIO Sink Current per Pin</td>
<td>i<sub>GPIO</sub></td>
<td>1.2</td>
<td>mA</td>
</tr>
<tr>
<td>Storage Temperature Range</td>
<td>T<sub>S</sub></td>
<td>-40 to +125</td>
<td>°C</td>
</tr>
</tbody>
</table>

## 4.2 ESD and Latch-up Protection Characteristics
Normal handling precautions should be used to avoid exposure to static discharge.

Qualification tests are performed to ensure that these devices can withstand exposure to reasonable levels of static without suffering any permanent damage. During the device qualification, ESD stresses were performed for the Human Body Model (HBM), the Machine Model (MM) and the Charge Device Model (CDM).

A device is defined as a failure if after exposure to ESD pulses, the device no longer meets the device specification. Complete DC parametric and functional testing is performed per the applicable device specification at room temperature followed by hot temperature, unless specified otherwise in the device specification.

<table>
<thead>
<tr>
<th>Rating</th>
<th>Symbol</th>
<th>Value</th>
<th>Unit</th>
</tr>
</thead>
<tbody>
<tr>
<td>Human Body Model (HBM)</td>
<td>V<sub>ESD</sub></td>
<td>±2000</td>
<td>V</td>
</tr>
<tr>
<td>Machine Model (MM)</td>
<td>V<sub>ESD</sub></td>
<td>±200</td>
<td>V</td>
</tr>
<tr>
<td>Charge Device Model (CDM)</td>
<td>V<sub>ESD</sub></td>
<td>±500</td>
<td>V</td>
</tr>
<tr>
<td>Latch-up current at T<sub>A</sub> = 85°C</td>
<td>I<sub>LATCH</sub></td>
<td>±100</td>
<td>mA</td>
</tr>
</tbody>
</table>



---


# 4.3 DC Characteristics

This section includes information about power supply requirements and I/O pin characteristics.

**Table 5. DC Characteristics**  
(Typical Operating Circuit, V<sub>DD</sub> and V<sub>REG</sub> = 1.8V, T<sub>A</sub> = 25°C, unless otherwise noted.)

<table>
<thead>
<tr>
<th>Parameter</th>
<th>Symbol</th>
<th>Conditions</th>
<th>Min</th>
<th>Typ</th>
<th>Max</th>
<th>Units</th>
</tr>
</thead>
<tbody>
<tr>
<td colspan="7">[Table data not visible in the image]</td>
</tr>
</tbody>
</table>

1.: ECR set to 0x2C and all 12 channels plus one proximity channel activated. Measurement current CDC is set at maximum of 0x3F.

# 4.4 AC Characteristics

**Table 6. AC Characteristics**  
(Typical Operating Circuit, V<sub>DD</sub> and V<sub>REG</sub> = 1.8V, T<sub>A</sub> = 25°C, unless otherwise noted.)

<table>
<thead>
<tr>
<th>Parameter</th>
<th>Symbol</th>
<th>Conditions</th>
<th>Min</th>
<th>Typ</th>
<th>Max</th>
<th>Units</th>
</tr>
</thead>
<tbody>
<tr>
<td>8 MHz Internal Oscillator</td>
<td>f<sub>H</sub></td>
<td></td>
<td>7.44</td>
<td>8</td>
<td>8.56</td>
<td>MHz</td>
</tr>
<tr>
<td>1 kHz Internal Oscillator</td>
<td>f<sub>L</sub></td>
<td></td>
<td>0.65</td>
<td>1</td>
<td>1.35</td>
<td>kHz</td>
</tr>
</tbody>
</table>

<table>
    <thead>
    <tr>
        <th>Parameter</th>
        <th>Symbol</th>
        <th>Conditions</th>
        <th>Min</th>
        <th>Typ</th>
        <th>Max</th>
        <th>Units</th>
    </tr>
    </thead>
    <tr>
        <td>High Supply Voltage</td>
        <td>VDD</td>
        <td></td>
        <td>2.0</td>
        <td>3.3</td>
        <td>3.6</td>
        <td>V</td>
    </tr>
    <tr>
        <td>Low Supply Voltage</td>
        <td>VREG</td>
        <td></td>
        <td>1.71</td>
        <td>1.8</td>
        <td>2.75</td>
        <td>V</td>
    </tr>
    <tr>
        <td rowspan="8">Average Supply Current(1)</td>
        <td rowspan="8">IDD</td>
        <td>Run Mode @ 1 ms sample period</td>
        <td></td>
        <td>393</td>
        <td></td>
        <td>μA</td>
    </tr>
    <tr>
        <td>Run Mode @ 2 ms sample period</td>
        <td></td>
        <td>199</td>
        <td></td>
        <td>μA</td>
    </tr>
    <tr>
        <td>Run Mode @ 4 ms sample period</td>
        <td></td>
        <td>102</td>
        <td></td>
        <td>μA</td>
    </tr>
    <tr>
        <td>Run Mode @ 8 ms sample period</td>
        <td></td>
        <td>54</td>
        <td></td>
        <td>μA</td>
    </tr>
    <tr>
        <td>Run Mode @ 16 ms sample period</td>
        <td></td>
        <td>29</td>
        <td></td>
        <td>μA</td>
    </tr>
    <tr>
        <td>Run Mode @ 32 ms sample period</td>
        <td></td>
        <td>17</td>
        <td></td>
        <td>μA</td>
    </tr>
    <tr>
        <td>Run Mode @ 64 ms sample period</td>
        <td></td>
        <td>11</td>
        <td></td>
        <td>μA</td>
    </tr>
    <tr>
        <td>Run Mode @ 128 ms sample period</td>
        <td></td>
        <td>8</td>
        <td></td>
        <td>μA</td>
    </tr>
    <tr>
        <td>Measurement Supply Current</td>
        <td>IDD</td>
        <td>Peak of measurement duty cycle</td>
        <td></td>
        <td>1</td>
        <td></td>
        <td>mA</td>
    </tr>
    <tr>
        <td>Idle Supply Current</td>
        <td>IDD</td>
        <td>Stop Mode</td>
        <td></td>
        <td>3</td>
        <td></td>
        <td>μA</td>
    </tr>
    <tr>
        <td>Input Leakage Current ELE_</td>
        <td>IIH, IIL</td>
        <td></td>
        <td></td>
        <td>0.025</td>
        <td></td>
        <td>μA</td>
    </tr>
    <tr>
        <td>Input Self-Capacitance on ELE_</td>
        <td></td>
        <td></td>
        <td></td>
        <td></td>
        <td>15</td>
        <td>pF</td>
    </tr>
    <tr>
        <td>Input High Voltage SDA, SCL</td>
        <td>VIH</td>
        <td></td>
        <td>0.7 x VDD</td>
        <td></td>
        <td></td>
        <td>V</td>
    </tr>
    <tr>
        <td>Input Low Voltage SDA, SCL</td>
        <td>VIL</td>
        <td></td>
        <td></td>
        <td></td>
        <td>0.3 x VDD</td>
        <td>V</td>
    </tr>
    <tr>
        <td>Input Leakage Current

SDA, SCL</td>
        <td>IIH, IIL</td>
        <td></td>
        <td></td>
        <td>0.025</td>
        <td>1</td>
        <td>μA</td>
    </tr>
    <tr>
        <td>Input Capacitance

SDA, SCL</td>
        <td></td>
        <td></td>
        <td></td>
        <td></td>
        <td>7</td>
        <td>pF</td>
    </tr>
    <tr>
        <td>Output Low Voltage

SDA, IRQ</td>
        <td>VOL</td>
        <td>IOL = 6mA</td>
        <td></td>
        <td></td>
        <td>0.5V</td>
        <td>V</td>
    </tr>
    <tr>
        <td>Output High Voltage

ELE4 - ELE11 (GPIO mode)</td>
        <td>VOHGPIO</td>
        <td>VDD = 2.7V to 3.6V: IOHGPIO = -10 mA

VDD = 2.3V to 2.7V: IOHGPIO = -6 mA

VDD = 1.8V to 2.3V: IOHGPIO = -3 mA</td>
        <td>VDD - 0.5</td>
        <td></td>
        <td></td>
        <td>V</td>
    </tr>
    <tr>
        <td>Output Low Voltage

ELE4 - ELE11 (GPIO mode)</td>
        <td>VOLGPIO</td>
        <td>IOLGPIOD = 1 mA</td>
        <td></td>
        <td></td>
        <td>0.5</td>
        <td>V</td>
    </tr>
    <tr>
        <td rowspan="2">Power On Reset</td>
        <td>VTLH</td>
        <td>VDD rising</td>
        <td>1.08</td>
        <td>1.35</td>
        <td>1.62</td>
        <td>V</td>
    </tr>
    <tr>
        <td>VTHL</td>
        <td>VDD falling</td>
        <td>0.88</td>
        <td>1.15</td>
        <td>1.42</td>
        <td>V</td>
    </tr></table>



---


# 4.5 I²C AC Characteristics

## Table 7. I²C AC Characteristics  
(Typical Operating Circuit, V<sub>DD</sub> and V<sub>REG</sub> = 1.8V, T<sub>A</sub> = 25°C, unless otherwise noted.)

<table>
<thead>
<tr>
  <th>Symbol</th>
  <th>Parameter</th>
  <th>Conditions</th>
  <th>Min</th>
  <th>Typ</th>
  <th>Max</th>
  <th>Unit</th>
</tr>
</thead>
<tbody>
<tr>
  <td>f<sub>SCL</sub></td>
  <td>SCL clock frequency</td>
  <td></td>
  <td></td>
  <td>400</td>
  <td></td>
  <td>kHz</td>
</tr>
<tr>
  <td>t<sub>BUF</sub></td>
  <td>Bus free time between STOP and START condition</td>
  <td></td>
  <td>1.3</td>
  <td></td>
  <td></td>
  <td>μs</td>
</tr>
<tr>
  <td>t<sub>HD;STA</sub></td>
  <td>Hold time (repeated) START condition</td>
  <td></td>
  <td>0.6</td>
  <td></td>
  <td></td>
  <td>μs</td>
</tr>
<tr>
  <td>t<sub>SU;STA</sub></td>
  <td>Setup time for a repeated START condition</td>
  <td></td>
  <td>0.6</td>
  <td></td>
  <td></td>
  <td>μs</td>
</tr>
<tr>
  <td>t<sub>SCLL</sub></td>
  <td>SCL low period</td>
  <td></td>
  <td>1.3</td>
  <td></td>
  <td></td>
  <td>μs</td>
</tr>
<tr>
  <td>t<sub>SCLH</sub></td>
  <td>SCL high period</td>
  <td></td>
  <td>0.6</td>
  <td></td>
  <td></td>
  <td>μs</td>
</tr>
<tr>
  <td>t<sub>SU;DAT</sub></td>
  <td>Data setup time</td>
  <td></td>
  <td>100</td>
  <td></td>
  <td></td>
  <td>ns</td>
</tr>
<tr>
  <td>t<sub>HD;DAT</sub></td>
  <td>Data hold time</td>
  <td></td>
  <td>0</td>
  <td></td>
  <td>900</td>
  <td>ns</td>
</tr>
<tr>
  <td>t<sub>R</sub></td>
  <td>Rise time of both SDA and SCL signals</td>
  <td></td>
  <td></td>
  <td>20</td>
  <td>300</td>
  <td>ns</td>
</tr>
<tr>
  <td>t<sub>F</sub></td>
  <td>Fall time of both SDA and SCL signals</td>
  <td></td>
  <td></td>
  <td>20</td>
  <td>300</td>
  <td>ns</td>
</tr>
<tr>
  <td>t<sub>SU;STO</sub></td>
  <td>Setup time for STOP condition</td>
  <td></td>
  <td>0.6</td>
  <td></td>
  <td></td>
  <td>μs</td>
</tr>
<tr>
  <td>t<sub>DH</sub></td>
  <td>Data hold time (repeated START)</td>
  <td></td>
  <td>0</td>
  <td></td>
  <td>900</td>
  <td>ns</td>
</tr>
</tbody>
</table>

<table>
    <thead>
    <tr>
        <th>Parameter</th>
        <th>Symbol</th>
        <th>Conditions</th>
        <th>Min</th>
        <th>Typ</th>
        <th>Max</th>
        <th>Units</th>
    </tr>
    </thead>
    <tr>
        <td>Serial Clock Frequency</td>
        <td>fSCL</td>
        <td></td>
        <td></td>
        <td></td>
        <td>400</td>
        <td>kHz</td>
    </tr>
    <tr>
        <td>Bus Free Time Between a STOP and a START Condition</td>
        <td>tBUF</td>
        <td></td>
        <td>1.3</td>
        <td></td>
        <td></td>
        <td>μs</td>
    </tr>
    <tr>
        <td>Hold Time, (Repeated) START Condition</td>
        <td>tHD, STA</td>
        <td></td>
        <td>0.6</td>
        <td></td>
        <td></td>
        <td>μs</td>
    </tr>
    <tr>
        <td>Repeated START Condition Setup Time</td>
        <td>tSU, STA</td>
        <td></td>
        <td>0.6</td>
        <td></td>
        <td></td>
        <td>μs</td>
    </tr>
    <tr>
        <td>STOP Condition Setup Time</td>
        <td>tSU, STO</td>
        <td></td>
        <td>0.6</td>
        <td></td>
        <td></td>
        <td>μs</td>
    </tr>
    <tr>
        <td>Data Hold Time</td>
        <td>tHD, DAT</td>
        <td></td>
        <td></td>
        <td></td>
        <td>0.9</td>
        <td>μs</td>
    </tr>
    <tr>
        <td>Data Setup Time</td>
        <td>tSU, DAT</td>
        <td></td>
        <td>100</td>
        <td></td>
        <td></td>
        <td>ns</td>
    </tr>
    <tr>
        <td>SCL Clock Low Period</td>
        <td>tLOW</td>
        <td></td>
        <td>1.3</td>
        <td></td>
        <td></td>
        <td>μs</td>
    </tr>
    <tr>
        <td>SCL Clock High Period</td>
        <td>tHIGH</td>
        <td></td>
        <td>0.7</td>
        <td></td>
        <td></td>
        <td>μs</td>
    </tr>
    <tr>
        <td>Rise Time of Both SDA and SCL Signals, Receiving</td>
        <td>tR</td>
        <td></td>
        <td></td>
        <td>20+0.1Cb</td>
        <td>300</td>
        <td>ns</td>
    </tr>
    <tr>
        <td>Fall Time of Both SDA and SCL Signals, Receiving</td>
        <td>tF</td>
        <td></td>
        <td></td>
        <td>20+0.1Cb</td>
        <td>300</td>
        <td>ns</td>
    </tr>
    <tr>
        <td>Fall Time of SDA Transmitting</td>
        <td>tF.TX</td>
        <td></td>
        <td></td>
        <td>20+0.1Cb</td>
        <td>250</td>
        <td>ns</td>
    </tr>
    <tr>
        <td>Pulse Width of Spike Suppressed</td>
        <td>tSP</td>
        <td></td>
        <td></td>
        <td>25</td>
        <td></td>
        <td>ns</td>
    </tr>
    <tr>
        <td>Capacitive Load for Each Bus Line</td>
        <td>Cb</td>
        <td></td>
        <td></td>
        <td></td>
        <td>400</td>
        <td>pF</td>
    </tr></table>



---


# 5 Register Operation Descriptions

## 5.1 Register Read/Write Operations and Measurement Run/Stop Mode

After power on reset (POR) or soft reset by command, all registers are in reset default initial value (see Table 2). All the registers, except registers 0x5C (default 0x10) and 0x5D (default 0x24), are cleared.

Registers 0x2B ~ 0x7F are control and configuration registers which need to be correctly configured before any capacitance measurement and touch detection.

Registers 0x00 ~ 0x2A are output registers updating periodically by the MPR121 in Run Mode. Among these output registers, Baseline Value Registers 0x1D ~ 0x2A are also writable, this is sometimes useful when user specific baseline values are desired.

The MPR121’s Run Mode and Stop Mode are controlled by control bits in Electrode Configuration Register (ECR, 0x5E). When all ELEPROX_EN and ELE_EN bits are zeros, the MPR121 is in Stop Mode. While in Stop Mode, there are no capacitance or touch detection measurement on any of the 13 channels. When any of the ELEPROX_EN and ELE_EN bits are set to ‘1’, the MPR121 is in Run Mode. The MPR121 will continue to run on its own until it is set again to Stop Mode by the user.

The MPR121 registers read operation can be done at any time, either in Run Mode or in Stop Mode. However, the register write operation can only be done in Stop Mode. The ECR (0x5E) and GPIO/LED control registers (0x73~0x7A) can be written at anytime.

## 5.2 Touch Status Registers (0x00~0x01)

### ELE0-ELE7 Touch Status (0x00)

<table>
<thead>
<tr>
<th>Bit</th>
<th>D7</th>
<th>D6</th>
<th>D5</th>
<th>D4</th>
<th>D3</th>
<th>D2</th>
<th>D1</th>
<th>D0</th>
</tr>
</thead>
<tbody>
<tr>
<td>Read</td>
<td>ELE7</td>
<td>ELE6</td>
<td>ELE5</td>
<td>ELE4</td>
<td>ELE3</td>
<td>ELE4</td>
<td>ELE1</td>
<td>ELE0</td>
</tr>
<tr>
<td>Write</td>
<td>—</td>
<td>—</td>
<td>—</td>
<td>—</td>
<td>—</td>
<td>—</td>
<td>—</td>
<td>—</td>
</tr>
</tbody>
</table>

### ELE8-ELE11 ELEPROX Touch Status (0x01)

<table>
<thead>
<tr>
<th>Bit</th>
<th>D7</th>
<th>D6</th>
<th>D5</th>
<th>D4</th>
<th>D3</th>
<th>D2</th>
<th>D1</th>
<th>D0</th>
</tr>
</thead>
<tbody>
<tr>
<td>Read</td>
<td>OVCF</td>
<td>—</td>
<td>—</td>
<td>ELEPROX</td>
<td>ELE11</td>
<td>ELE10</td>
<td>ELE9</td>
<td>ELE8</td>
</tr>
<tr>
<td>Write</td>
<td>—</td>
<td>—</td>
<td>—</td>
<td>—</td>
<td>—</td>
<td>—</td>
<td>—</td>
<td>—</td>
</tr>
</tbody>
</table>

These two registers indicate the detected touch/release status of all of the 13 sensing input channels. ELEPROX is the status for the 13th proximity detection channel. The update rate of these status bits will be {ESI x SFI}.

**ELEx, ELEPROX:** Touch or Release status bit of each respective channel (read only).  
- 1, the respective channel is currently deemed as touched.  
- 0, the respective channel is deemed as released.

> **Note:** When an input is not configured as an electrode and enabled as GPIO input port, the corresponding status bit shows the input level, but these GPIO status changes will not cause any IRQ interrupt. This feature is for ELE4~ELE11 only.

**OVCF:** Over Current Flag (read and write)  
- 1, over current was detected on REXT pin.  
- 0, normal condition.

When over current is detected, the OVCF is set to ‘1’ and the MPR121 goes to Stop Mode. All other bits in status registers 0x00~0x03, output registers 0x04~0x2A, and bits D5~D0 in ECR (0x5E) will also be cleared. When the bit is set at ‘1’, the write to the ECR register to enter Run Mode will be discarded. The write to ’1’ of the OVCF will clear this bit and the MPR121 fault condition will be cleared. The MPR121 can then be configured to return to the Run Mode again.


---


# 5.3 Electrode Filtered Data Register (0x04~0x1D)

## Electrode Filtered Data Low Byte (0x04, 0x06, ..., 0x1C)

<table>
  <thead>
    <tr>
      <th>Bit</th>
      <th>D7</th>
      <th>D6</th>
      <th>D5</th>
      <th>D4</th>
      <th>D3</th>
      <th>D2</th>
      <th>D1</th>
      <th>D0</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>Read</td>
      <td>Bit 7</td>
      <td>Bit 6</td>
      <td>Bit 5</td>
      <td>Bit 4</td>
      <td>Bit 3</td>
      <td>Bit 2</td>
      <td>Bit 1</td>
      <td>Bit 0</td>
    </tr>
<tr>
      <td>Write</td>
      <td colspan="8">—</td>
    </tr>
  </tbody>
</table>

## Electrode Filtered Data High Byte (0x05, 0x07, ..., 0x1D)

<table>
  <thead>
    <tr>
      <th>Bit</th>
      <th>D7</th>
      <th>D6</th>
      <th>D5</th>
      <th>D4</th>
      <th>D3</th>
      <th>D2</th>
      <th>D1</th>
      <th>D0</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>Read</td>
      <td colspan="6">—</td>
      <td>Bit 9</td>
      <td>Bit 8</td>
    </tr>
<tr>
      <td>Write</td>
      <td colspan="8">—</td>
    </tr>
  </tbody>
</table>

The MPR121 provides filtered electrode output data for all 13 channels. The output data is 10-bit and comes from the internal 2nd stage filter output. The data range is 0~1024 or 0x000~0x400 in Hex. Bit 0~7 of the 10-bit data are stored in the low byte and bit 9 and bit 8 are stored in the high byte. The data is the measured voltage on each channel and inversely proportional to the capacitance on that channel.

These registers are read only and are updated every {ESI x SFI}. A multibyte read operation to read both LSB and MSB is recommended to keep the data coherency (i.e, LSB and MSB matching). A multibyte reading of 0x00~0x2A returns results of a single moment without mixing up old and new data.

# 5.4 Baseline Value Register (0x1E~0x2A)

## Electrode Baseline Value (0x1E~0x2A)

<table>
  <thead>
    <tr>
      <th>Bit</th>
      <th>D7</th>
      <th>D6</th>
      <th>D5</th>
      <th>D4</th>
      <th>D3</th>
      <th>D2</th>
      <th>D1</th>
      <th>D0</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>Read</td>
      <td>Bit 9</td>
      <td>Bit 8</td>
      <td>Bit 7</td>
      <td>Bit 6</td>
      <td>Bit 5</td>
      <td>Bit 4</td>
      <td>Bit 3</td>
      <td>Bit 2</td>
    </tr>
<tr>
      <td>Write</td>
      <td colspan="8"></td>
    </tr>
  </tbody>
</table>

Along with the 10-bit electrode filtered data output, each channel also has a 10-bit baseline value. The update rate of these registers is {ESI x SFI} if baseline tracking operation is enabled. These values are the output of the internal baseline filter operation tracking the slow-voltage variation of the background capacitance change. Touch/release detection is made based on the comparison between the 10-bit electrode filtered data and the 10-bit baseline value.

> **Note:** Although internally the baseline value is 10-bit, users can only access the 8 MSB of the 10-bit baseline value through the baseline value registers. The read out from the baseline register must be left shift two bits before comparing it with the 10-bit electrode data.

The Baseline Value register is writable in Stop Mode. Note: when the user writes into the baseline value register, the lower two bits of the 10-bit baseline value are automatically cleared internally upon write operation. The Write to Baseline Value Register by specific values can be sometimes useful if user wants to manipulate the touch/release status. For example, manually setting the target channel from a touch locked state into a touch released state is easily done by setting the baseline value above the signal data.

Refer to the Electrode Configuration Register (ECR, 0x5E) on how to control the on/off operation of baseline tracking and further details on how the initial baseline data is loaded into Run Mode. Refer to Baseline Filtering Control registers (0x2B~0x2A) on how to control the filtering of the baseline value.

# 5.5 Baseline Filtering Control Register (0x2B~0x40)

All 12 of the electrode baseline values are controlled by the same set of filtering control registers, 0x2B ~ 0x35. The 13th channel ELEPROX is controlled by registers 0x36 ~ 0x40. Both sets of registers have the same structure using three different scenarios; rising, falling, and touched.

Rising is defined as when the electrode data is greater than the baseline value. Falling is defined as when the electrode data is less than the baseline value. Touched is when the electrode is in touched status. For each scenario, the filtering characteristic is further defined by four parameters: the maximum half delta (MHD), noise half delta (NHD), noise count limit (NCL) and filter delay count limit (FDL). Note: there is no maximum half delta for the touched scenario.

* **Maximum Half Delta (MHD):** Determines the largest magnitude of variation to pass through the baseline filter. The range of the effective value is 1~63.
* **Noise Half Delta (NHD):** Determines the incremental change when non-noise drift is detected. The range of the effective value is 1~63.

MPR121  
Sensors  
Freescale Semiconductor, Inc.


---


# Noise Count Limit (NCL) and Filter Delay Count Limit (FDL)

**Noise Count Limit (NCL):** Determines the number of samples consecutively greater than the Max Half Delta value. This is necessary to determine that it is not noise. The range of the effective value is 0~255.

**Filter Delay Count Limit (FDL):** Determines the operation rate of the filter. A larger count limit means the filter delay is operating more slowly. The range of the effective value is 0~255.

The setting of the filter is depended on the actual application. For more information on these registers, refer to application note AN3891.

## 5.6 Touch / Release Threshold (0x41~0x5A)

### ELEx, ELEProx Touch Threshold (0x41,0x43,...,0x59)

<table>
<thead>
<tr>
<th>Bit</th>
<th>D7</th>
<th>D6</th>
<th>D5</th>
<th>D4</th>
<th>D3</th>
<th>D2</th>
<th>D1</th>
<th>D0</th>
</tr>
</thead>
<tbody>
<tr>
<td>Read</td>
<td colspan="8" style="text-align:center;">ExTTH</td>
</tr>
<tr>
<td>Write</td>
<td colspan="8"></td>
</tr>
</tbody>
</table>

### ELEx, ELEProx Release Threshold (0x42,0x44,...,0x5A)

<table>
<thead>
<tr>
<th>Bit</th>
<th>D7</th>
<th>D6</th>
<th>D5</th>
<th>D4</th>
<th>D3</th>
<th>D2</th>
<th>D1</th>
<th>D0</th>
</tr>
</thead>
<tbody>
<tr>
<td>Read</td>
<td colspan="8" style="text-align:center;">ExRTH</td>
</tr>
<tr>
<td>Write</td>
<td colspan="8"></td>
</tr>
</tbody>
</table>

**ExTTH:** Electrode touch threshold, in range of 0~0xFF.  
**ExRTH:** Electrode release threshold, in range of 0~0xFF.

Each of the 13 channels can be set with its own set of touch and release thresholds. Touch and release are detected by comparing the electrode filtered data to the baseline value. The amount of deviation from the baseline value represents an immediate capacitance change detected by possible a touch/release action.

- Touch condition: Baseline - Electrode filtered data > Touch threshold  
- Release condition: Baseline - Electrode filtered data < Release threshold

Threshold settings are dependent on the touch/release signal strength, system sensitivity and noise immunity requirements. In a typical touch detection application, threshold is typically in the range 0x04~0x10. The touch threshold is several counts larger than the release threshold. This is to provide hysteresis and to prevent noise and jitter. For more information, refer to the application note AN3892 and the MPR121 design guidelines.

## 5.7 Debounce Register (0x5B)

### Debounce Register (0x5B)

<table>
<thead>
<tr>
<th>Bit</th>
<th>D7</th>
<th>D6</th>
<th>D5</th>
<th>D4</th>
<th>D3</th>
<th>D2</th>
<th>D1</th>
<th>D0</th>
</tr>
</thead>
<tbody>
<tr>
<td>Read</td>
<td>—</td>
<td colspan="3" style="text-align:center;">DR</td>
<td>—</td>
<td colspan="2" style="text-align:center;">DT</td>
</tr>
<tr>
<td>Write</td>
<td colspan="8"></td>
</tr>
</tbody>
</table>

**DT:** Debounce number for touch. The value range is 0~7.  
**DR:** Debounce number for release. The value range is 0~7.

All 13 channels use the same set of touch and release debounce numbers. The status bits in Status Register 0x00 and 0x01 will only take place after the number of consecutive touch or release detection meets the debounce number setting. The debounce setting can be very useful in avoiding possible noise glitches. Using the debounce setting, the status bit change will have a delay of {ESI x SFI x DR (or DT)}.



---


# 5.8 Filter and Global CDC CDT Configuration (0x5C, 0x5D)

## Filter/Global CDC Configuration Register (0x5C)

<table>
  <thead>
    <tr>
      <th>Bit</th>
      <th>D7</th>
      <th>D6</th>
      <th>D5</th>
      <th>D4</th>
      <th>D3</th>
      <th>D2</th>
      <th>D1</th>
      <th>D0</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>Read</td>
      <td colspan="8">FFI</td>
    </tr>
<tr>
      <td>Write</td>
      <td colspan="8">CDC</td>
    </tr>
  </tbody>
</table>

## Filter/Global CDT Configuration Register (0x5D)

<table>
  <thead>
    <tr>
      <th>Bit</th>
      <th>D7</th>
      <th>D6</th>
      <th>D5</th>
      <th>D4</th>
      <th>D3</th>
      <th>D2</th>
      <th>D1</th>
      <th>D0</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>Read</td>
      <td colspan="2">CDT</td>
      <td colspan="3">SFI</td>
      <td colspan="2">ESI</td>
    </tr>
<tr>
      <td>Write</td>
      <td colspan="8"></td>
    </tr>
  </tbody>
</table>

### Table 8. Bit Descriptions

<table>
  <thead>
    <tr>
      <th>Field</th>
      <th>Description</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td rowspan="5"><b>FFI</b></td>
      <td>First Filter Iterations - The first filter iterations field selects the number of samples taken as input to the first level of filtering.</td>
    </tr>
<tr>
      <td>00 Encoding 0 - Sets samples taken to 6 (Default)</td>
    </tr>
<tr>
      <td>01 Encoding 1 - Sets samples taken to 10</td>
    </tr>
<tr>
      <td>10 Encoding 2 - Sets samples taken to 18</td>
    </tr>
<tr>
      <td>11 Encoding 3 - Sets samples taken to 34</td>
    </tr>
<tr>
      <td rowspan="7"><b>CDC</b></td>
      <td>Charge Discharge Current - Selects the global value of charge discharge current applied to electrode. The maximum is 63 μA, 1 μA step.</td>
    </tr>
<tr>
      <td>000000 Encoding 0 - Disable Electrode Charging</td>
    </tr>
<tr>
      <td>000001 Encoding 1 - Sets the current to 1 μA</td>
    </tr>
<tr>
      <td>~</td>
    </tr>
<tr>
      <td>010000 Encoding 16 - Sets the current to 16 μA (Default)</td>
    </tr>
<tr>
      <td>~</td>
    </tr>
<tr>
      <td>111111 Encoding 63 - Sets the current to 63 μA</td>
    </tr>
<tr>
      <td rowspan="7"><b>CDT</b></td>
      <td>Charge Discharge Time - Selects the global value of charge time applied to electrode. The maximum is 32 μs, programmable as 2 ^(n-2) μs.</td>
    </tr>
<tr>
      <td>000 Encoding 0 - Disables Electrode Charging</td>
    </tr>
<tr>
      <td>001 Encoding 1 - Time is set to 0.5 μs (Default)</td>
    </tr>
<tr>
      <td>010 Encoding 2 - Time is set to 1 μs</td>
    </tr>
<tr>
      <td>~</td>
    </tr>
<tr>
      <td>111 Encoding 7 - Time is set to 32 μs</td>
    </tr>
<tr>
      <td rowspan="5"><b>SFI</b></td>
      <td>Second Filter Iterations - Selects the number of samples taken for the second level filter</td>
    </tr>
<tr>
      <td>00 Encoding 0 - Number of samples is set to 4 (Default)</td>
    </tr>
<tr>
      <td>01 Encoding 1 - Number of samples is set to 6</td>
    </tr>
<tr>
      <td>10 Encoding 2 - Number of samples is set to 10</td>
    </tr>
<tr>
      <td>11 Encoding 3 - Number of samples is set to 18</td>
    </tr>
<tr>
      <td rowspan="9"><b>ESI</b></td>
      <td>Electrode Sample Interval - Selects the period between samples used for the second level of filtering. The maximum is 128ms, Programmable to 2^n ms</td>
    </tr>
<tr>
      <td>000 Encoding 0 - Period set to 1 ms</td>
    </tr>
<tr>
      <td>001 Encoding 1 - Period set to 2 ms</td>
    </tr>
<tr>
      <td>~</td>
    </tr>
<tr>
      <td>100 Encoding 4 - Period set to 16 ms (Default)</td>
    </tr>
<tr>
      <td>~</td>
    </tr>
<tr>
      <td>111 Encoding 7 - Period set to 128 ms</td>
    </tr>
  </tbody>
</table>



---


# Electrode Charge Current and Charge Time Registers

These two registers set the global AFE settings. This includes global electrode charge/discharge current CDC, global charge/discharge time CDT, as well as a common filtering setting (FFI, SFI, ESI) for all 13 channels, including the 13th Eleprox channel.

The register `0x5C` holds the global CDC and the first level filter configuration for all 13 channels. For each enabled channel, the global CDC will be used for that channel if the respective charge discharge current CDCx setting in `0x5F~0x6B` for that channel is zero. If it is not zero, the individual CDCx value will be used in place of the global CDC value. If the MPR121’s auto-configuration feature is enabled, CDCx will be automatically set up during system start stage and used for the actual measurement.

The register `0x5D` holds the global CDT and the second level filter configuration for all 13 channels. For each enabled channel, the global CDT will be used for that channel if the respective charge discharge time CDTx setting in `0x6C~0x72` for that channel is zero. If it is not zero, the individual CDTx value will be used in place of the global CDT value. If the SCTS bit (Skip Charge Time Search) in the MPR121’s autoconfiguration is set, then the current global CDT and CDTx will be used for each channel measurements. If not, then the individual CDTx will be automatically set up during the system start stage and used for the actual measurement.

Using only the global CDC and/or global CDT is acceptable where the capacitance values from all 13 channels are similar. If the electrode pattern, size, or even overlay and base material type changes from one channel to another, then using individual CDCx (and CDTx) will have a better result on sensing sensitivity as each electrode is charged up to a point closing to the supply voltage rail so that the highest sensing field is built for each channel.

The settings for the FFI, SFI, and ESI must be selected according to the system design noise filtering requirement. These settings must also balance the need for power consumption and response time.

When the total time required by scanning and charging/discharge all the enabled channels is longer than the ESI setting, then the actual time will override the ESI setting. For example if the ESI = 4 (16 mS), when FFI = 3 (34 samples), CDT = 7 (32 μS), with all 13 channels enabled, the scan time needed is  
$$34 \times (32 \mu S + 32 \mu S) \times 13 = 28 \text{ mS}.$$  
This 28 mS will be the actual sampling interval instead of ESI (16 mS).

## 5.9 Electrode Charge Current Register (0x5F~0x6B)

### Electrode Charge Current (0x5F~0x6B)

<table>
<thead>
<tr>
<th>Bit</th>
<th>D7</th>
<th>D6</th>
<th>D5</th>
<th>D4</th>
<th>D3</th>
<th>D2</th>
<th>D1</th>
<th>D0</th>
</tr>
</thead>
<tbody>
<tr>
<td>Read</td>
<td>—</td>
<td>—</td>
<td colspan="6" rowspan="2" style="text-align:center;">CDCx</td>
</tr>
<tr>
<td>Write</td>
<td>—</td>
<td>—</td>
</tr>
</tbody>
</table>

**CDCx:** Sets the charge current applied to each channel. Similar to global CDC value, the range is 0~63 μA, from `0x00`~`0x3F` in 1 μA step. When the CDCx is zero, the global CDC value will be used for that channel.

The individual CDCx bit can either be set manually or automatically (if autoconfiguration is enabled). When the autoconfiguration is enabled, during the first transition from Stop Mode to Run Mode, the system will automatically run a trial search for the appropriate CDCx (and CDTx if SCTS = 0). The individual CDCx will be automatically updated by the MPR121 into the respective registers once autoconfiguration is finished. CDCx is used in the following capacitance measurement and touch detection.

## 5.10 Electrode Charge Time Register (0x6C~0x72)

### Electrode Charge Time (0x6C~0x72)

<table>
<thead>
<tr>
<th>Bit</th>
<th>D7</th>
<th>D6</th>
<th>D5</th>
<th>D4</th>
<th>D3</th>
<th>D2</th>
<th>D1</th>
<th>D0</th>
</tr>
</thead>
<tbody>
<tr>
<td>Read</td>
<td>—</td>
<td colspan="3" style="text-align:center;">CDTx+1</td>
<td>—</td>
<td colspan="2" style="text-align:center;">CDTx</td>
</tr>
<tr>
<td>Write</td>
<td>—</td>
<td colspan="3"></td>
<td>—</td>
<td colspan="2"></td>
</tr>
</tbody>
</table>

**CDTx:** Sets the charge time applied to each channel. Similar to the global CDT value, the range is 0~32 μS, from `2b000`~`2b111`. When the CDTx is zero, the global CDT value is used for that channel.

The individual CDTx bit can be set manually or automatically (if autoconfiguration is enabled). When autoconfiguration is enabled, during the first transition from Stop Mode to Run Mode, the system will automatically run a trial search for the appropriate CDCx (and CDTx if SCST = 0). This means the autoconfiguration will include a search on the CDTx. The individual CDTx will be automatically updated by the MPR121 into the respective registers once the autoconfiguration is finished. This data is used in the following capacitance measurement and touch detection. If SCTS bit is 1, the search on CDTx will be skipped.

----

**MPR121**  
Sensors  
Freescale Semiconductor, Inc.


---


# 5.11 Electrode Configuration Register (ECR, 0x5E)

## Electrode Configuration Register (0x5E)

<table>
<thead>
<tr>
<th>Bit</th>
<th>D7</th>
<th>D6</th>
<th>D5</th>
<th>D4</th>
<th>D3</th>
<th>D2</th>
<th>D1</th>
<th>D0</th>
</tr>
</thead>
<tbody>
<tr>
<td>Read</td>
<td></td>
<td>CL</td>
<td></td>
<td>ELEPROX_EN</td>
<td></td>
<td></td>
<td>ELE_EN</td>
<td></td>
</tr>
<tr>
<td>Write</td>
<td></td>
<td></td>
<td></td>
<td></td>
<td></td>
<td></td>
<td></td>
<td></td>
</tr>
</tbody>
</table>

### Table 9. Bit Descriptions

<table>
<thead>
<tr>
<th>Field</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr>
<td rowspan="5"><b>CL</b></td>
<td>Calibration Lock - Controls the baseline tracking and how the baseline initial value is loaded</td>
</tr>
<tr>
<td>00 - Baseline tracking enabled, initial baseline value is current value in baseline value register (Default)</td>
</tr>
<tr>
<td>01 - Baseline tracking is disabled</td>
</tr>
<tr>
<td>10 - Baseline tracking enabled, initial baseline value is loaded with the 5 high bits of the first 10-bit electrode data value</td>
</tr>
<tr>
<td>11 - Baseline tracking enabled, initial baseline value is loaded with all 10 bits of the first electrode data value</td>
</tr>
<tr>
<td rowspan="4"><b>ELEPROX_EN</b></td>
<td>Proximity Enable - Controls the operation of 13th Proximity Detection</td>
</tr>
<tr>
<td>00 - Proximity Detection is disabled (Default)</td>
</tr>
<tr>
<td>01 - Run Mode with ELE0~ELE1 combined for proximity detection enabled</td>
</tr>
<tr>
<td>10 - Run Mode with ELE0~ELE3 combined for proximity detection enabled</td>
</tr>
<tr>
<td>11 - Run Mode with ELE0~ELE11 combined for proximity detection enabled</td>
</tr>
<tr>
<td rowspan="15"><b>ELE_EN</b></td>
<td>Electrode Enable - Controls the operation of 12 electrodes detection</td>
</tr>
<tr>
<td>0000 - Electrode detection is disabled (Default)</td>
</tr>
<tr>
<td>0001 - Run Mode with ELE0 for electrode detection enabled</td>
</tr>
<tr>
<td>0010 - Run Mode with ELE0~ ELE1 for electrode detection enabled</td>
</tr>
<tr>
<td>0011 - Run Mode with ELE0~ ELE2 for electrode detection enabled</td>
</tr>
<tr>
<td>0100 - Run Mode with ELE0~ ELE3 for electrode detection enabled</td>
</tr>
<tr>
<td>0101 - Run Mode with ELE0~ ELE4 for electrode detection enabled</td>
</tr>
<tr>
<td>0110 - Run Mode with ELE0~ ELE5 for electrode detection enabled</td>
</tr>
<tr>
<td>0111 - Run Mode with ELE0~ ELE6 for electrode detection enabled</td>
</tr>
<tr>
<td>1000 - Run Mode with ELE0~ ELE7 for electrode detection enabled</td>
</tr>
<tr>
<td>1001 - Run Mode with ELE0~ ELE8 for electrode detection enabled</td>
</tr>
<tr>
<td>1010 - Run Mode with ELE0~ ELE9 for electrode detection enabled</td>
</tr>
<tr>
<td>1011 - Run Mode with ELE0~ ELE10 for electrode detection enabled</td>
</tr>
<tr>
<td>11xx - Run Mode with ELE0~ ELE11 for electrode detection enabled</td>
</tr>
</tbody>
</table>

The Electrode Configuration Register (ECR) determines if the MPR121 is in Run Mode or Stop Mode, controls the baseline tracking operation and specifies the input configurations of the 13 channels.

The ECR reset default value is 0x00, which means MPR121 is in Stop Mode without capacitance measurement on all 13 channels. Setting ELEPROX_EN and/or ELE_EN control bits to non-zero data will put the MPR121 into Run Mode. This will cause the MPR121 to operate immediately on its own. Clearing the ELEPROX_EN and ELE_EN all to zeros will set the MPR121 into Stop Mode (which is its lowest power state). The MPR121 can be switched between Stop Mode and Run Mode at anytime by configuring the ECR.

If all channels including the 13th proximity detection channel are enabled, the proximity sensing channel is scanned first, followed by ELE0, ELE1..., and ELE11 respectively. The scan runs periodically at the sampling rate specified by the ESI in the Filter/CDT Configuration Register (0x5D). Refer to the table above for configuration of the different channels. Enabling specific channels will save the scan time and sensing field power spent on the unused channels.

In a typical touch detection application, baseline tracking is enabled. This is to compensate for the environment and background induced slow capacitance change to the input sensing channels. The CL bits can enable/disable the baseline tracking and specify how to load the baseline initial values. Since the baseline tracking filtering system has a very large time constant and the initial


---


baseline value starts from zero, it will require a very long time for the baseline to ramp up. This results in a short period of no response to touch after the MPR121 is first set to Run Mode. Setting the CL = 2b10 will command the MPR121 to load the initial baseline value at the beginning of the Run Mode. This shortens the initial baseline ramp-up time so that user will not notice any delay on touch detection. The MPR121 uses the five high bits of the first measured 10 bit electrode data.

## Auto-Configuration Registers  (0x7B–0x7F)

For each enabled channel, both the charge time and charge current must be set properly. This is so that a specified amount of charge field can be built on the sensing pad and that the capacitance can be measured using the internal ADC. When all 13 channels are used, there are total 13 CDCx and 13 CDTx values which need to be configured.

The MPR121 provides an auto-configuration function which is able to automatically search and set the charging parameters. When autoconfiguration is run, specific CDCx and CDTx combinations for the enabled channels can be obtained automatically. This eliminates test trials on the prototype device and for further verification on final products. A key task for the design engineer is to verify if the parameter settings generated by the MPR121 are acceptable. This verification ensures that the settings are optimized each time MPR121 powers on and that the equipment can operate in many different environments.

The autoconfiguration finds the optimized CDCx and CDTx combination for each channel so that the charge level (I × T = V) on the each channel is as close as possible to the target setting specified by the designer. An upper and lower setting limit are used to provide the boundaries necessary to verify if the system is setup to operate correctly. If the autoconfiguration can not find the proper CDCx and CDTx value, an Out Of Range (OOR) status will be set for that channel.

Autoconfiguration operates each time the MPR121 transitions from Stop Mode to Run Mode. After autoconfiguration is completed, a set of CDCx and CDTx values for each channel are calculated and automatically loaded into the corresponding register fields.

If autoconfiguration fails, the MPR121 has an auto-reconfiguration function. Autoreconfiguration runs at each sampling interval if a channel has OOR status from a failed autoconfiguration. Autoreconfiguration will run until the OOR status is cleared or until it is disabled.

There are five registers used to control the MPR121 auto-configuration feature. Registers 0x7B and 0x7C are used as the control registers and registers 0x07D to 0x7F are used to hold the configuration target settings. Refer to application note AN3889 for more information.

### Auto-Configure Control Register 0 (0x7B)

<table>
<thead>
<tr>
<th>Bit</th>
<th>D7</th>
<th>D6</th>
<th>D5</th>
<th>D4</th>
<th>D3</th>
<th>D2</th>
<th>D1</th>
<th>D0</th>
</tr>
</thead>
<tbody>
<tr>
<td>Read</td>
<td colspan="2">FFI</td>
<td colspan="2">RETRY</td>
<td>BVA</td>
<td>ARE</td>
<td>ACE</td>
<td></td>
</tr>
<tr>
<td>Write</td>
<td colspan="2">FFI</td>
<td colspan="2">RETRY</td>
<td>BVA</td>
<td>ARE</td>
<td>ACE</td>
<td></td>
</tr>
</tbody>
</table>

### Auto-Configure Control Register 1 (0x7C)

<table>
<thead>
<tr>
<th>Bit</th>
<th>D7</th>
<th>D6</th>
<th>D5</th>
<th>D4</th>
<th>D3</th>
<th>D2</th>
<th>D1</th>
<th>D0</th>
</tr>
</thead>
<tbody>
<tr>
<td>Read</td>
<td>SCTS</td>
<td>—</td>
<td>—</td>
<td>—</td>
<td>—</td>
<td>OORIE</td>
<td>ARFIE</td>
<td>ACFIE</td>
</tr>
<tr>
<td>Write</td>
<td>—</td>
<td>—</td>
<td>—</td>
<td>—</td>
<td>—</td>
<td>OORIE</td>
<td>ARFIE</td>
<td>ACFIE</td>
</tr>
</tbody>
</table>

- **FFI:** The FFI bits are the same as the FFI bits in register 0x5C for correct auto-configuration and reconfiguration operations.  
- **ACE:** Auto-Configuration Enable. 1: Enable, 0: Disable. When Enabled, the autoconfiguration will operate once at the beginning of the transition from Stop Mode to Run Mode. This includes search and update of the CDCx and CDTx for each enabled channel (if SCTS = 0).  
- **ARE:** Auto-Reconfiguration Enable. 1: Enable, 0: Disable. When enabled, if the OOR is set for a channel after autoconfiguration, autoreconfiguration will operate on that channel on each sampling interval until the OOR is cleared.  
- **BVA:** Fill the BVA bits same as the CL bits in ECR (0x5E) register.  
- **RETRY:** Specifies the number of retries for autoconfiguration and autoreconfiguration if the configuration fails before setting OOR.

<table>
<tbody>
<tr><td>00</td><td>No retry</td></tr>
<tr><td>01</td><td>retry 2 times</td></tr>
<tr><td>10</td><td>retry 4 times</td></tr>
<tr><td>11</td><td>retry 8 times</td></tr>
</tbody>
</table>



---


# SCTS: Skip Charge Time Search.

1: Skip CDTx search and update when autoconfiguration or autoreconfiguration, and current global CDT or CDTx are used for respective channels. CDT or CDTx needs to be specified by the designer manually before operation. Setting the SCTS to “1” results in a shorter time to complete autoconfiguration. This is useful for when the designer has obtained the correct CDTx / CDT, and is confident that the current CDT and CDTx settings work in all conditions.  
0: Both CDTx and CDCx will be searched and set by autoconfiguration and/or autoreconfiguration.

**ACFIE:** Auto-configuration fail interrupt enable. 1: Enable, 0: Disable  
**ARFIE:** Auto-reconfiguration fail interrupt enable. 1: Enable, 0: Disable  
**OORIE:** Out-of-range interrupt enable. 1: Enable, 0: Disable  

## Up-Side Limit Register (0x7D)

<table>
<thead>
<tr>
<th>Bit</th>
<th>D7</th>
<th>D6</th>
<th>D5</th>
<th>D4</th>
<th>D3</th>
<th>D2</th>
<th>D1</th>
<th>D0</th>
</tr>
</thead>
<tbody>
<tr>
<td>Read</td>
<td colspan="8" style="text-align:center;">USL</td>
</tr>
<tr>
<td>Write</td>
<td colspan="8"></td>
</tr>
</tbody>
</table>

## Low-Side Limit Register (0x7E)

<table>
<thead>
<tr>
<th>Bit</th>
<th>D7</th>
<th>D6</th>
<th>D5</th>
<th>D4</th>
<th>D3</th>
<th>D2</th>
<th>D1</th>
<th>D0</th>
</tr>
</thead>
<tbody>
<tr>
<td>Read</td>
<td colspan="8" style="text-align:center;">LSL</td>
</tr>
<tr>
<td>Write</td>
<td colspan="8"></td>
</tr>
</tbody>
</table>

## Target Level Register (0x7F)

<table>
<thead>
<tr>
<th>Bit</th>
<th>D7</th>
<th>D6</th>
<th>D5</th>
<th>D4</th>
<th>D3</th>
<th>D2</th>
<th>D1</th>
<th>D0</th>
</tr>
</thead>
<tbody>
<tr>
<td>Read</td>
<td colspan="8" style="text-align:center;">TL</td>
</tr>
<tr>
<td>Write</td>
<td colspan="8"></td>
</tr>
</tbody>
</table>

**USL:** Up-Side Limit. This value sets the electrode data level up limit for the boundary check in autoconfiguration and autoreconfiguration operation.  
**LSL:** Low-Side Limit. This value sets the electrode data level low limit for the boundary check in autoconfiguration and autoreconfiguration operation.  
**TL:** Target Level. This value is the expected target electrode data level for autoconfiguration and autoreconfiguration, that is, after successful autoconfiguration and autoreconfiguration, the measured electrode data level when untouched shall be close to the TL value. TL shall be in between of USL and LSL.

The three parameters, USL, LSL and TL, are in the format similar to the baseline value; only the eight high bits are accessible by user and the two low bits are set to zero automatically. The USL/LSL/TL data needs to be shifted left two bits before comparing with the electrode data or the 10-bit baseline value.

In order to have a valid auto-configuration result, USL/LSL/TL values should follow the relation that  
$$255 > USL > TL > LSL > 0$$  
For example, USL = 200, TL = USL * 0.9 = 180, LSL = USL * 0.5 = 100.

It is possible that in an end user environment, the channel differences may be significant. This is because the same set of USL/LSL/TL data is being used for all channels. It is important that the parameters not be set too close together. This makes it difficult for the autoconfiguration to find a suitable charge setting for a specific channel. In this case, the electrode data might easily go out of USL and LSL setting limits. Since the data is out-of-range, the channel status becomes OOR. If the channel is still OOR after the autoconfiguration has been run, it may indicate that the settings for this channel have not yet been optimized. One solution to this problem is to manually review the USL/LSL/TL settings. Another possible reason why the channel status could be OOR is a problem with the channel itself. This could be caused by a short to ground, short to the power rail, or short to the pad of the other channel.

For the TL setting, a good practice is to try to set it close to the USL. This so the charge field can be set to detect a weak touch. On the other hand, the TL should not be set too close to the USL so that it is constantly exceeding the limit. For example, the electrode data from the end user’s environment might have a much wider variance of readings. Some of the readings might exceed the USL, causing the auto-configuration to fail. For this reason, if the amount of capacitance change in the end user environment is significant, it is suggested that the USL and TL be set low enough to give some headroom for possible capacitance variations.


---


With above mentioned, one possible example setting is given out below using equation 1~3, with the assumption that setting TL at 90% of USL, and LSL at 65% of USL would cover most of the application case. It may need further adjustment in some cases but will be a very good start.

$$
\text{USL} = \frac{(VDD - 0.7)}{VDD} \times 256 \quad \text{Eqn. 1}
$$

$$
\text{TL} = \text{USL} \times 0.9 = \frac{(VDD - 0.7)}{VDD} \times 256 \times 0.9 \quad \text{Eqn. 2}
$$

$$
\text{LSL} = \text{USL} \times 0.65 = \frac{(VDD - 0.7)}{VDD} \times 256 \times 0.65 \quad \text{Eqn. 3}
$$

$$
C_{in} = \frac{I \times T}{V} = \frac{CDC \times CDT}{\text{ADC counts} \times \frac{VDD}{1024}} \quad \text{Eqn. 4}
$$

It may not necessary to set the USL at the level of VDD - 0.7 but it is beneficial to keep the applied constant charge current as accurate as that specified in the data sheet. This so the capacitance value on the input can be calculated with high accuracy using ADC conversion Equation 4. Using VDD-0.7 as USL level allows some headroom for applications where the supply varies over a certain range. For a system where the supply changes over a range, the lowest VDD point is considered for autoconfiguration so that a relative lower charge field can be used to avoid clipping the electrode data to VDD when it drops.

## 5.12 Out-Of-Range Status Registers (0x02, 0x03)

### ELE0~ELE7 OOR Status (0x02)

<table>
<thead>
<tr>
<th>Bit</th><th>D7</th><th>D6</th><th>D5</th><th>D4</th><th>D3</th><th>D2</th><th>D1</th><th>D0</th>
</tr>
</thead>
<tbody>
<tr>
<td>Read</td><td>E7_OOR</td><td>E6_OOR</td><td>E5_OOR</td><td>E4_OOR</td><td>E3_OOR</td><td>E2_OOR</td><td>E1_OOR</td><td>E0_OOR</td>
</tr>
<tr>
<td>Write</td><td>—</td><td>—</td><td>—</td><td>—</td><td>—</td><td>—</td><td>—</td><td>—</td>
</tr>
</tbody>
</table>

### ELE8~ELEPROX OOR Status (0x03)

<table>
<thead>
<tr>
<th>Bit</th><th>D7</th><th>D6</th><th>D5</th><th>D4</th><th>D3</th><th>D2</th><th>D1</th><th>D0</th>
</tr>
</thead>
<tbody>
<tr>
<td>Read</td><td>ACFF</td><td>ARFF</td><td>—</td><td>EPROX_OOR</td><td>E11_OOR</td><td>E10_OOR</td><td>E9_OOR</td><td>E8_OOR</td>
</tr>
<tr>
<td>Write</td><td>—</td><td>—</td><td>—</td><td>—</td><td>—</td><td>—</td><td>—</td><td>—</td>
</tr>
</tbody>
</table>

**Ex_OOR, EPROX_OOR:** Out-Of-Range Status bits for the 13 channels. This bit set indicates that a corresponding channel has failed autoconfiguration and autoreconfiguration for range check. Those bits are cleared when they pass the auto-configuration and auto-reconfiguration range check. These bits are user read only.

**ACFF:** Auto-Configuration Fail Flag. When autoconfiguration fails, this bit is set. This bit is user read only.

**ARFF:** Auto-Reconfiguration Fail Flag. When autoreconfiguration fails, this is bit set. This bit is user read only.

When autoconfiguration and/or autoreconfiguration are enabled, MPR121 checks the electrode data after each auto-configuration, auto-reconfiguration operation to see if it is still in the range set by USL and LSL. When electrode data goes out of the range, corresponding Ex_OORx bit becomes “1” to indicate the failed channels. One example of triggering OOR error is shorting the measurement sensing pad to power rails, or shorting it with other channels.

## 5.13 Soft Rest Register (0x80)

Write `0x80` with `0x63` asserts soft reset. The soft reset does not effect the I²C module, but all others reset the same as POR.

## 5.14 GPIO Registers (0x73~0x7A)

### GPIO Registers (0x73~0x7A)

<table>
<thead>
<tr>
<th>GPIO Registers</th><th>D7</th><th>D6</th><th>D5</th><th>D4</th><th>D3</th><th>D2</th><th>D1</th><th>D0</th>
</tr>
</thead>
<tbody>
<tr>
<td>Control Register 0 (0x73)</td><td>GTL0_E11</td><td>GTL0_E10</td><td>GTL0_E9</td><td>GTL0_E8</td><td>GTL0_E7</td><td>GTL0_E6</td><td>GTL0_E5</td><td>GTL0_E4</td>
</tr>
<tr>
<td>Control Register 1 (0x74)</td><td>GTL1_E11</td><td>GTL1_E10</td><td>GTL1_E9</td><td>GTL1_E8</td><td>GTL1_E7</td><td>GTL1_E6</td><td>GTL1_E5</td><td>GTL1_E4</td>
</tr>
<tr>
<td>Data Register (0x75)</td><td>DAT_E11</td><td>DAT_E10</td><td>DAT_E9</td><td>DAT_E8</td><td>DAT_E7</td><td>DAT_E6</td><td>DAT_E5</td><td>DAT_E4</td>
</tr>
</tbody>
</table>



---


# GPIO Registers (0x73~0x7A)

These registers control GPIO and LED driver functions. D7~D0 bits correspond to GPIO and LED functions on ELE11~ELE4 inputs respectively. When any of these ports are not used for electrode sensing, it can be used for GPIO or LED driver. The GPIO control registers can be write at anytime regardless Stop Mode or Run mode. The configuration of the LED driver and GPIO system is described with more detail in application note AN3894.

Note: The number of touch sensing electrodes, and therefore the number of GPIO ports left available is configured by the ECR (0x5E) and GPIO Enable Register (0x77). ECR has higher priority and overrides the GPIO enabled in 0x77, that is when a pin is enabled as GPIO but is also selected as electrode by ECR, the GPIO function is disabled immediately and it becomes an electrode during Run Mode.

In the Stop Mode just after power-on reset, all electrodes and GPIO ports are in high impedance as all the GPIO ports are default disabled and the electrodes are not enabled.

**EN, DIR, CTL0, CTL1:** GPIO enable and configuration bits, the functions are in description table below.

<table>
<thead>
<tr>
<th>EN</th>
<th>DIR</th>
<th>CTL0:CTL1</th>
<th>Function Description</th>
</tr>
</thead>
<tbody>
<tr>
<td>0</td>
<td>X</td>
<td>XX</td>
<td>GPIO function is disabled. Port is high-z state.</td>
</tr>
<tr>
<td>1</td>
<td>0</td>
<td>00</td>
<td>GPIO port becomes input port.</td>
</tr>
<tr>
<td>1</td>
<td>0</td>
<td>10</td>
<td>GPIO port becomes input port with internal pulldown.</td>
</tr>
<tr>
<td>1</td>
<td>0</td>
<td>11</td>
<td>GPIO port becomes input port with internal pullup.</td>
</tr>
<tr>
<td>1</td>
<td>0</td>
<td>01</td>
<td>Not defined yet (as same as CTL = 00).</td>
</tr>
<tr>
<td>1</td>
<td>1</td>
<td>00</td>
<td>GPIO port becomes CMOS output port.</td>
</tr>
<tr>
<td>1</td>
<td>1</td>
<td>11</td>
<td>GPIO port becomes high side only open drain output port for LED driver.</td>
</tr>
<tr>
<td>1</td>
<td>1</td>
<td>10</td>
<td>GPIO port becomes low side only open drain output port.</td>
</tr>
<tr>
<td>1</td>
<td>1</td>
<td>01</td>
<td>Not defined yet (as same as CTL = 00).</td>
</tr>
</tbody>
</table>

When the EN bit is set, the corresponding GPIO pin is enabled and the GPIO function is configured by CTL0, CTL1 and DIR bits. When the port is used as an input, it can be configured as a normal logic input with high impedance (CTL0CTL1 = 2b00), input with internal pull-down (CTL0CTL1 = 2b10) or pullup (CTL0CTL1 = 2b11). Note: the former may result in an unstable logic input state if opened without fixed logic level input.

The GPIO output configuration can be configured as either push pull (CTL0CTL1 = 2b00) or open drain. When the GPIO is used for LED drivers, the GPIO is set to high side only open drain (CTL0CTL1 = 2b11), which is can source up to 12 mA current into the LED.

**DAT:** GPIO Data Register bits.

When a GPIO is enabled as an output, the GPIO port outputs the corresponding DAT bit level from GPIO Data Register (0x075). The output level toggle remains on during any electrode charging. The level transition will occur after the ADC conversion takes place. It is important to note that reading this register returns the content of the GPIO Data Register, (not a level of the port). When a GPIO is configured as input, reading this register returns the latched input level of the corresponding port (not contents of the GPIO Data Register). Writing to the DAT changes content of the register, but does not effect the input function.

**SET:** Writing a “1” to this bit will set the corresponding bit in the Data Register.

**CLR:** Writing a “1” to this bit will clear the corresponding bit in the Data Register.

**TOG:** Writing a “1” to this bit will toggle the corresponding bit in the Data Register.

Writing “1” into the corresponding bits of GPIO Data Set Register, GPIO Data Clear Register, and GPIO Data Toggle Register will set/clear/toggle contents of the corresponding DAT bit in Data Register. Writing “0” has no meaning. These registers allow any individual port(s) to be set, cleared, or toggled individually without effecting other ports. It is important to note that reading these registers returns the contents of the GPIO Data Register reading.

MPR121  
Sensors  
Freescale Semiconductor, Inc.

<table>
    <tr>
        <td>Direction Register(0x76)</td>
        <td>DIR_E11</td>
        <td>DIR_E10</td>
        <td>DIR_E9</td>
        <td>DIR_E8</td>
        <td>DIR_E7</td>
        <td>DIR_E6</td>
        <td>DIR_E5</td>
        <td>DIR_E4</td>
    </tr>
    <tr>
        <td>Enable Register(0x77)</td>
        <td>EN_E11</td>
        <td>EN_E10</td>
        <td>EN_E9</td>
        <td>EN_E8</td>
        <td>EN_E7</td>
        <td>EN_E6</td>
        <td>EN_E5</td>
        <td>EN_E4</td>
    </tr>
    <tr>
        <td>Data Set Register(0x78)</td>
        <td>SET_E11</td>
        <td>SET_E10</td>
        <td>SET_E9</td>
        <td>SET_E8</td>
        <td>SET_E7</td>
        <td>SET_E6</td>
        <td>SET_E5</td>
        <td>SET_E4</td>
    </tr>
    <tr>
        <td>Data Clear Register(0x79)</td>
        <td>CLR_E11</td>
        <td>CLR_E10</td>
        <td>CLR_E9</td>
        <td>CLR_E8</td>
        <td>CLR_E7</td>
        <td>CLR_E6</td>
        <td>CLR_E5</td>
        <td>CLR_E4</td>
    </tr>
    <tr>
        <td>Data Toggle Register(0x7A)</td>
        <td>TOG_E11</td>
        <td>TOG_E10</td>
        <td>TOG_E11</td>
        <td>TOG_E8</td>
        <td>TOG_E7</td>
        <td>TOG_E6</td>
        <td>TOG_E5</td>
        <td>TOG_E4</td>
    </tr></table>



---


# MPR121 Serial Communication

## 6.1 I²C Serial Communications
The MPR121 uses an I²C Serial Interface. The MPR121 operates as a slave that sends and receives data through an I²C two-wire interface. The interface uses a Serial Data Line (SDA) and a Serial Clock Line (SCL) to achieve bidirectional communication between master(s) and slave(s). A master (typically a microcontroller) initiates all data transfers to and from the MPR121, and it generates the SCL clock that synchronizes the data transfer.

The MPR121 SDA line operates as both an input and an open-drain output. A pullup resistor, typically 4.7 kΩ, is required on SDA.  
The MPR121 SCL line operates only as an input. A pullup resistor, typically 4.7 kΩ, is required on SCL if there are multiple masters on the two-wire interface, or if the master in a single-master system has an open-drain SCL output.

Each transmission consists of a START condition (Figure 3) sent by a master, followed by the MPR121’s 7-bit slave address plus R/W bit, a register address byte, one or more data bytes, and finally a STOP condition.

```
SDA
                                                                                                 tBUF

           tLOW  tSU DAT  tHD DAT tSU STA            tHD STA                    tSU STO
SCL              tHIGH

tHD STA          tR   tF

   START    REPEATED START    STOP         START
 CONDITION     CONDITION     CONDITION    CONDITION
```

**Figure 3. Two-Wire Serial Interface Timing Details**

## 6.2 Slave Address
The MPR121 has selectable slave addresses listed by different ADDR pin connections. This also makes it possible for multiple MPR121 devices to be used together for channel expansions in a single system.

<table>
<thead>
<tr>
<th>ADDR Pin Connection</th>
<th>I²C Address</th>
</tr>
</thead>
<tbody>
<tr>
<td>VSS</td>
<td>0x5A</td>
</tr>
<tr>
<td>VDD</td>
<td>0x5B</td>
</tr>
<tr>
<td>SDA</td>
<td>0x5C</td>
</tr>
<tr>
<td>SCL</td>
<td>0x5D</td>
</tr>
</tbody>
</table>

## 6.3 Operation with Multiple Master
When operating with multiple masters, bus confusion between I²C masters is sometimes a problem. One way to prevent this is to avoid using repeated starts to the MPR121. On an I²C bus, once a master issues a start/repeated start condition, that master owns the bus until a stop condition occurs. If a master that does not own the bus attempts to take control of that bus, then improper addressing may occur. An address may always be rewritten to fix this problem. Follow I²C protocol for multiple master configurations.


---


# 6.4 Read and Write Operation Format

### < Single Byte Read >

<table>
  <thead>
    <tr>
      <th>Master</th>
      <th>ST</th>
      <th>Device Address[6:0]</th>
      <th>W</th>
      <th>Register Address[7:0]</th>
      <th>SR</th>
      <th>Device Address[6:0]</th>
      <th>R</th>
      <th>NAK</th>
      <th>SP</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>Slave</td>
      <td colspan="2"></td>
      <td>AK</td>
      <td colspan="2"></td>
      <td>AK</td>
      <td colspan="2"></td>
      <td>AK</td>
      <td>Data[7:0]</td>
      <td></td>
    </tr>
  </tbody>
</table>

### < Multiple Byte Read >

<table>
  <thead>
    <tr>
      <th>Master</th>
      <th>ST</th>
      <th>Device Address[6:0]</th>
      <th>W</th>
      <th>Register Address[7:0]</th>
      <th>SR</th>
      <th>Device Address[6:0]</th>
      <th>R</th>
      <th>AK</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>Slave</td>
      <td colspan="2"></td>
      <td>AK</td>
      <td colspan="2"></td>
      <td>AK</td>
      <td colspan="2"></td>
      <td>AK</td>
      <td>Data[7:0]</td>
      <td></td>
    </tr>
<tr>
      <td>Master</td>
      <td colspan="2"></td>
      <td>AK</td>
      <td colspan="2"></td>
      <td>AK</td>
      <td>NAK</td>
      <td>SP</td>
    </tr>
<tr>
      <td>Slave</td>
      <td>Data[7:0]</td>
      <td></td>
      <td>Data[7:0]</td>
      <td></td>
      <td>Data[7:0]</td>
      <td></td>
    </tr>
  </tbody>
</table>

### < Single Byte Write >

<table>
  <thead>
    <tr>
      <th>Master</th>
      <th>ST</th>
      <th>Device Address[6:0]</th>
      <th>W</th>
      <th>Register Address[7:0]</th>
      <th>Data[7:0]</th>
      <th>SP</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>Slave</td>
      <td colspan="2"></td>
      <td>AK</td>
      <td colspan="2"></td>
      <td>AK</td>
      <td>AK</td>
    </tr>
  </tbody>
</table>

### Legend

* ST: Start Condition  
* SP: Stop Condition  
* NAK: No Acknowledge  
* W: Write = 0  
* SR: Repeated Start Condition  
* AK: Acknowledge  
* R: Read = 1  



---


NXP

# PACKAGE DIMENSIONS

[The image shows a mechanical outline drawing of a Quad Flat No Lead (QFN) package with dimensions and pin layout.]

- The top view shows a square outline with a hatched area indicating the "PIN 1 INDEX AREA."
- The side view shows the profile of the package with a detail labeled "DETAIL G."
- The bottom view (VIEW M-M) shows the pin layout with 20 terminals arranged on all four sides.
- Pins are numbered 1 to 20, with pin 1 marked by an index arrow.
- Dimensions are given as 3 x 3 mm with a 0.4 mm pitch.
- Terminal width is 0.25 mm ± 0.15 mm.
- Terminal length is 0.60 mm to 0.40 mm.
- The drawing includes geometric tolerances and datum references.

----

<table>
<thead>
<tr>
<th colspan="2">MECHANICAL OUTLINE</th>
<th>PRINT VERSION NOT TO SCALE</th>
</tr>
</thead>
<tbody>
<tr>
<td colspan="2">TITLE:</td>
<td>DOCUMENT NO: 98ASA00021D</td>
</tr>
<tr>
<td colspan="2">QUAD FLAT NO LEAD COL PACKAGE (QFN-COL) 20 TERMINAL, 0.4 PITCH (3 X 3 X 0.6)</td>
<td>REV: 0</td>
</tr>
<tr>
<td colspan="2"></td>
<td>19 FEB 2009</td>
</tr>
<tr>
<td colspan="2">CASE NUMBER: 2059-01</td>
<td>STANDARD: NON JEDEC</td>
</tr>
</tbody>
</table>

----

© FREESCALE SEMICONDUCTOR, INC. ALL RIGHTS RESERVED.

----

**MPR121**

Sensors  
Freescale Semiconductor, Inc.


---


[The image shows a mechanical outline drawing of a Quad Flat No Lead (QFN-COL) package with 20 terminals, 0.4 pitch (3 x 3 x 0.6). The drawing includes detailed dimensions and tolerances for the package, including terminal spacing and seating plane information.]

<table>
  <thead>
    <tr>
      <th colspan="4">© FREESCALE SEMICONDUCTOR, INC. ALL RIGHTS RESERVED.</th>
      <th><b>MECHANICAL OUTLINE</b></th>
      <th colspan="3">PRINT VERSION NOT TO SCALE</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td colspan="4">
        TITLE:<br/>
        QUAD FLAT NO LEAD COL PACKAGE (QFN-COL)<br/>
        20 TERMINAL, 0.4 PITCH (3 X 3 X 0.6)
      </td>
      <td>DOCUMENT NO: 98ASA00021D</td>
      <td>REV: 0</td>
      <td>CASE NUMBER: 2059-01</td>
      <td>19 FEB 2009</td>
    </tr>
<tr>
      <td colspan="4"></td>
      <td colspan="4">STANDARD: NON JEDEC</td>
    </tr>
  </tbody>
</table>



---


NOTES:

1. ALL DIMENSIONS ARE IN MILLIMETERS.

2. INTERPRET DIMENSIONS AND TOLERANCES PER ASME Y14.5M-1994.

3. THIS IS NON JEDEC REGISTERED PACKAGE.

4. COPLANARITY APPLIES TO LEADS AND ALL OTHR BOTTOM SURFACE METALLIZATION.

5. MIN. METAL GAP SHOULD BE 0.2MM.

<table>
  <thead>
    <tr>
      <th colspan="4">© FREESCALE SEMICONDUCTOR, INC. ALL RIGHTS RESERVED.</th>
      <th><b>MECHANICAL OUTLINE</b></th>
      <th>PRINT VERSION NOT TO SCALE</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td colspan="2"><b>TITLE:</b> QUAD FLAT NO LEAD COL PACKAGE (QFN-COL) 20 TERMINAL, 0.4 PITCH (3 X 3 X 0.6)</td>
      <td><b>DOCUMENT NO:</b> 98ASA00021D</td>
      <td><b>REV:</b> 0</td>
      <td><b>CASE NUMBER:</b> 2059-01</td>
      <td><b>19 FEB 2009</b></td>
    </tr>
<tr>
      <td colspan="6"><b>STANDARD:</b> NON JEDEC</td>
    </tr>
  </tbody>
</table>



---



<table>
<thead>
<tr>
<th>Revision number</th>
<th>Revision date</th>
<th>Description of changes</th>
</tr>
</thead>
<tbody>
<tr>
<td>3</td>
<td>12/2011</td>
<td>
* On Page 1, Under Features: Changed 3 mA shutdown current to 3 mA in scan stop mode current, changed 12 electrodes to 12 electrodes/capacitance sensing inputs in which 8 are multifunctional for LED driving and GPIO, added two new bullets: Integrated independent autocalibration for each electrode input and Autoconfiguration of charge current and charge time for each electrode input, Under Implementations: added three bullets  
* Updated Table 1 Pin Descriptions, modified pin descriptions for Pins 4, 5, 7  
* In Section 3, added Power Supply paragraph, modified remaining paragraphs  
* In Table 2, changed ELEPROX to PROX_OOR, changed Register Names from: AFE Configuration and Filter Configuration to: Filter/Global CDC Configuration and Filter/Global CDT Configuration, added new register for Soft Reset Register  
* Removed AN3889, AN3890, AN3891, AN3892, AN3893, AN3894, AN3895, and AN3944 documents  
* Added Sections 5.0 through 6.4
</td>
</tr>
<tr>
<td>4</td>
<td>02/2013</td>
<td>
* Global change to Table 5, renamed all instances Run1 to Run. Added footnote in table
</td>
</tr>
</tbody>
</table>



---


