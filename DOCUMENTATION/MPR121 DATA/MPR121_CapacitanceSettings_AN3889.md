
# MPR121 Capacitance Sensing Settings

## INTRODUCTION
Touch acquisition takes a few different parts of the system in order to detect touch. The first stage of this process is to capture the pad capacitance. Freescale’s MPR121 utilizes the principle that a capacitor holds a fixed amount of charge at a specific electric potential. Both the implementation and the configuration will be described in this application note.

<table>
  <thead>
    <tr>
      <th colspan="7" style="text-align:center;">AFE AQUISITION</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td style="text-align:center; border:1px solid black;">RAW<br>DATA<br>1 - 32 μs</td>
      <td style="text-align:center;">→</td>
      <td style="text-align:center; border:1px solid black;">1st<br>FILTER<br>1 - 128 μs</td>
      <td style="text-align:center;">→</td>
      <td style="text-align:center; border:1px solid black;">2nd<br>FILTER<br>4 - 2048 μs</td>
      <td style="text-align:center;">→</td>
      <td style="text-align:center; border:1px solid black;">BASELINE<br>FILTER</td>
    </tr>
<tr>
      <td colspan="7" style="text-align:center;">
        

<table>
          <tbody>
            <tr>
              <td style="text-align:center; border:1px solid black;" colspan="3">TOUCH</td>
            </tr>
<tr>
              <td colspan="3" style="text-align:center; border:1px solid black;">STATUS REGISTER</td>
            </tr>
<tr>
              <td colspan="3" style="text-align:center; border:1px solid black;">IRQ</td>
            </tr>
          </tbody>
        </table>

      </td>
    </tr>
  </tbody>
</table>

**Figure 1. Data Flow in the MPR121**



---


# CAPACITANCE MEASUREMENT

The basic measurement technique used by the MPR121 is to charge up the capacitor C on one electrode input with a DC current I for a time T (the charge time). Before measurement, the electrode input is grounded, so the electrode voltage starts from 0 V and charges up with a slope, Equation 1, where C is the pad capacitance on the electrode (Figure 2). All of the other electrodes are grounded during this measurement. At the end of time T, the electrode voltage is measured with a 10 bit ADC. The voltage is inversely proportional to capacitance according to Equation 2. The electrode is then discharged back to ground at the same rate it was charged.

$$
\frac{dV}{dt} = \frac{I}{C} \quad \textbf{Equation 1}
$$

$$
V = \frac{I \times T}{C} \quad \textbf{Equation 2}
$$

<table>
  <thead>
    <tr>
      <th colspan="7" style="text-align:center;">Figure 2. MPR121 Electrode Measurement Charging Pad Capacitance</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td rowspan="2" style="vertical-align:bottom;">Electrode Voltage <br> V</td>
      <td colspan="3" style="text-align:center; border-bottom: 1px dashed #999;">Electrode Charge Time</td>
      <td colspan="3" style="text-align:center; border-bottom: 1px dashed #999;">Electrode Discharge Time</td>
    </tr>
<tr>
      <td style="text-align:center;">Electrode Charging</td>
      <td style="text-align:center; font-weight:bold;">T</td>
      <td style="text-align:center;">Electrode voltage measured here</td>
      <td style="text-align:center;">Electrode Discharging</td>
      <td style="text-align:center;">2T</td>
      <td></td>
    </tr>
  </tbody>
</table>

When measuring capacitance there are some inherent restrictions due to the methodology used. On the MPR121 the voltage after charging must be in the range that is shown in Figure 3.

<table>
  <thead>
    <tr>
      <th colspan="8" style="text-align:center;">Figure 3. Valid ADC Values vs. V<sub>DD</sub></th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td style="width:40px;">900</td>
      <td colspan="7" rowspan="11" style="position:relative;">
        <div style="position:absolute; left:10px; top:20px; font-size:10px; color:#000080;">ADChigh</div>
        <div style="position:absolute; left:10px; top:70px; font-size:10px; color:#808000;">ADCmid</div>
        <div style="position:absolute; left:10px; top:120px; font-size:10px; color:#c000c0;">ADClow</div>
        <svg width="300" height="150" style="background:#fff; border:1px solid #ccc;">
          <polyline points="0,130 50,120 100,110 150,100 200,90 250,80 300,70" style="fill:none;stroke:#000080;stroke-width:1" />
          <polyline points="0,80 300,80" style="fill:none;stroke:#808000;stroke-width:1" />
          <polyline points="0,70 50,80 100,90 150,100 200,110 250,120 300,130" style="fill:none;stroke:#c000c0;stroke-width:1" />
        </svg>
      </td>
    </tr>
<tr><td>800</td></tr>
<tr><td>700</td></tr>
<tr><td>600</td></tr>
<tr><td>500</td></tr>
<tr><td>400</td></tr>
<tr><td>300</td></tr>
<tr><td>200</td></tr>
<tr><td>100</td></tr>
<tr><td>0</td></tr>
<tr>
      <td colspan="8" style="text-align:center;">
        1.71      1.91      2.11      2.31      2.51      2.71<br>
        V<sub>DD</sub> (V)
      </td>
    </tr>
  </tbody>
</table>



---


The valid operating range of the electrode charging source is 0.7 V to (V<sub>DD</sub> - 0.7 V). This means that for a given V<sub>DD</sub> the valid ADC (voltage visible to the digital interface) range is given by

$$
ADC_{low} = \frac{0.7}{V_{DD}} (1024), \quad \textbf{Equation 3}
$$

and

$$
ADC_{high} = \frac{V_{DD} - 0.7}{V_{DD}} (1024). \quad \textbf{Equation 4}
$$

These equations are represented in the graph. In the nominal case of V<sub>DD</sub> = 1.8 V the ADC range is shown below in Table 1.

<table>
<thead>
<tr>
<th>V<sub>DD</sub></th>
<th>ADC<sub>high</sub></th>
<th>ADC<sub>low</sub></th>
<th>ADC<sub>mid</sub></th>
</tr>
</thead>
<tbody>
<tr>
<td>1.8</td>
<td>625.7778</td>
<td>398.2222</td>
<td>512</td>
</tr>
</tbody>
</table>

Any ADC counts outside of the range shown are invalid and settings must be adjusted to be within this range. If capacitance variation is of importance for an application after the current output, charge time and supply voltage are determined then the following equations can be used. The valid range for capacitance is calculated by using the minimum and maximum ADC values in the capacitance equation. Substituting the low and high ADC equations into the capacitance equation yields the equations for the minimum and maximum capacitance values which are

$$
C_{low} = \frac{I \times T}{V_{DD} - 0.7} \quad \text{and} \quad C_{high} = \frac{I \times T}{0.7}. \quad \textbf{Equation 5}
$$

## SENSITIVITY

The sensitivity of the MPR121 is relative to the capacitance range being measured. Given the ADC value, current and time and settings capacitance can be calculated,

$$
C = \frac{I \times T \times 1024}{V_{DD} \times ADC}. \quad \textbf{Equation 6}
$$

For a given capacitance the sensitivity can be measured by taking the derivative of this equation. The result of this is the following equation, representing the change in capacitance per one ADC count, where the ADC in the equation represents the current value.

$$
\frac{dC}{dADC} = \frac{I \times T \times 1024}{V_{DD} \times ADC^{2}}. \quad \textbf{Equation 7}
$$

This relationship is shown in the following graph by taking the midpoints off all possible ranges by varying the current and time settings. The midpoint is assumed to be 512 for ADC and the nominal supply voltage of 1.8 V is used.

<table>
<thead>
<tr>
<th colspan="6" style="text-align:center;">Sensitivity vs. Midpoint Capacitance for V<sub>DD</sub> = 1.8 V</th>
</tr>
</thead>
<tbody>
<tr>
<td>0</td>
<td>500</td>
<td>1000</td>
<td>1500</td>
<td>2000</td>
<td>2500</td>
</tr>
<tr>
<td colspan="6" style="text-align:center;">(Midpoint Capacitance (pF))</td>
</tr>
<tr>
<td>-5</td>
<td></td>
<td></td>
<td></td>
<td></td>
<td></td>
</tr>
<tr>
<td>-4.5</td>
<td></td>
<td></td>
<td></td>
<td></td>
<td></td>
</tr>
<tr>
<td>-4</td>
<td></td>
<td></td>
<td></td>
<td></td>
<td></td>
</tr>
<tr>
<td>-3.5</td>
<td></td>
<td></td>
<td></td>
<td></td>
<td></td>
</tr>
<tr>
<td>-3</td>
<td></td>
<td></td>
<td></td>
<td></td>
<td></td>
</tr>
<tr>
<td>-2.5</td>
<td></td>
<td></td>
<td></td>
<td></td>
<td></td>
</tr>
<tr>
<td>-2</td>
<td></td>
<td></td>
<td></td>
<td></td>
<td></td>
</tr>
<tr>
<td>-1.5</td>
<td></td>
<td></td>
<td></td>
<td></td>
<td></td>
</tr>
<tr>
<td>-1</td>
<td></td>
<td></td>
<td></td>
<td></td>
<td></td>
</tr>
<tr>
<td>-0.5</td>
<td></td>
<td></td>
<td></td>
<td></td>
<td></td>
</tr>
<tr>
<td>0</td>
<td colspan="5" style="text-align:center;">Sensitivity (pF/ADC Count)</td>
</tr>
</tbody>
</table>

> dC/dADC @cmid (pF/1 ADC Count)

**Figure 4.**


---


Smaller amounts of change indicate increased sensitivity for the capacitance sensor. Some sample values are shown in  
Table 2.

<table>
<thead>
<tr>
<th>pF</th>
<th>Sensitivity (pF/ADC count)</th>
</tr>
</thead>
<tbody>
<tr>
<td>10</td>
<td>-0.01953</td>
</tr>
<tr>
<td>100</td>
<td>-0.19531</td>
</tr>
</tbody>
</table>

In the previous cases, the capacitance is assumed to be in the middle of the range for specific settings. Within the capacitance range the equation is nonlinear, thus the sensitivity is best with the lowest capacitance. This graph shows the sensitivity derivative reading across the valid range of capacitances for a set I, T, and V<sub>DD</sub>. For simple small electrodes (that are approximately 21 pF) and a nominal 1.8 V supply. Figure 5 is representative of this effect.

<table>
<thead>
<tr>
<th colspan="16">Sensitivity vs. Capacitance for V<sub>DD</sub> = 1.8 V and I = 36 μA and T = .5 μS</th>
</tr>
</thead>
<tbody>
<tr>
<td>0.1</td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td>
</tr>
<tr>
<td>0.09</td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td>
</tr>
<tr>
<td>0.08</td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td>
</tr>
<tr>
<td>0.07</td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td>
</tr>
<tr>
<td>0.06</td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td>
</tr>
<tr>
<td>0.05</td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td>
</tr>
<tr>
<td>0.04</td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td>
</tr>
<tr>
<td>0.03</td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td>
</tr>
<tr>
<td>0.02</td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td>
</tr>
<tr>
<td>0.01</td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td><td></td>
</tr>
<tr>
<td>0</td>
<td>10</td><td>12</td><td>14</td><td>16</td><td>18</td><td>20</td><td>22</td><td>24</td><td>26</td><td>28</td><td>30</td><td></td><td></td><td></td><td></td>
</tr>
</tbody>
</table>

- Sensitivity (pF/ADC Count) increases nonlinearly with Capacitance.
- Minimum sensitivity near 16 pF.
- Maximum sensitivity near 26 pF.

**Figure 5.**

## CONFIGURATION

From the implementation above, there are two elements that can be configured to yield a wide range of capacitance readings ranging from 0.455 pF to 2874.39 pF. The two configurable components are the electrode charge current and the electrode charge time. The electrode charge current can be configured to equal a range of values between 1 μA and 63 μA. This value is set in the Charge Discharge Current (CDC) in the Analog Front End AFE Configuration register. The electrode charge time can be configured to equal a range of values between 500 ns and 32 μS. This value is set in the Charge Discharge Time (CDT) in the Filter Configuration Register.

### AFE CONFIGURATION REGISTER

The AFE Configuration Register is used to set both the CDC and the number of samples taken in the lowest level filter. The address of the AFE Configuration Register is 0x5C.

<table>
<thead>
<tr>
<th>7</th>
<th>6</th>
<th>5</th>
<th>4</th>
<th>3</th>
<th>2</th>
<th>1</th>
<th>0</th>
</tr>
</thead>
<tbody>
<tr>
<td>R</td>
<td colspan="7">FFI</td>
<td colspan="8">CDC</td>
</tr>
<tr>
<td>W</td>
<td colspan="7"></td>
<td colspan="8"></td>
</tr>
<tr>
<td>Reset:</td>
<td>0</td>
<td>0</td>
<td>0</td>
<td>0</td>
<td>0</td>
<td>0</td>
<td>0</td>
<td>0</td>
</tr>
</tbody>
</table>

= Unimplemented

**Figure 6. AFE Configuration Register**



---



<table>
<thead>
<tr>
<th colspan="2">Table 3. AFE Configuration Register Field Descriptions</th>
</tr>
<tr>
<th>Field</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr>
<td>7:6<br>FFI</td>
<td>First Filter Iterations – The first filter iterations field selects the number of samples taken as input to the first level of filtering.<br>
00 Encoding 0 – Sets samples taken to 6<br>
01 Encoding 1 – Sets samples taken to 10<br>
10 Encoding 2 – Sets samples taken to 18<br>
11 Encoding 3 – Sets samples taken to 34
</td>
</tr>
<tr>
<td>5:0<br>CDC</td>
<td>Charge Discharge Current – The Charge Discharge Current field selects the supply current to be used when charging and discharging an electrode.<br>
000000 Encoding 0 – Disables Electrode Charging<br>
000001 Encoding 1 – Sets the current to 1 μA<br>
~<br>
111111 Encoding 63 – Sets the current to 63 μA
</td>
</tr>
</tbody>
</table>

<br>

<b>FILTER CONFIGURATION REGISTER</b>

<table>
<thead>
<tr>
<th>7</th>
<th>6</th>
<th>5</th>
<th>4</th>
<th>3</th>
<th>2</th>
<th>1</th>
<th>0</th>
</tr>
</thead>
<tbody>
<tr>
<td>R</td>
<td colspan="3">CDT</td>
<td colspan="2">SFI</td>
<td colspan="3">ESI</td>
</tr>
<tr>
<td>W</td>
<td colspan="7"></td>
</tr>
<tr>
<td>Reset:</td>
<td>0</td>
<td>0</td>
<td>0</td>
<td>0</td>
<td>0</td>
<td>0</td>
<td>0</td>
<td>0</td>
</tr>
<tr>
<td colspan="9" style="background-color:#b0b0b0;">= Unimplemented</td>
</tr>
</tbody>
</table>

<p><b>Figure 7. Filter Configuration Register</b></p>

<table>
<thead>
<tr>
<th colspan="2">Table 4. Filter Configuration Register Field Descriptions</th>
</tr>
<tr>
<th>Field</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr>
<td>7:5<br>CDT</td>
<td>Charge Discharge Time – The Charge Discharge Time field selects the amount of time an electrode charges and discharges.<br>
000 Encoding 0 – Invalid<br>
001 Encoding 1 – Time is set to 0.5 μs<br>
010 Encoding 2 – Time is set to 1 μs<br>
~<br>
111 Encoding 7 – Time is set to 32 μs.
</td>
</tr>
<tr>
<td>4:3<br>SFI</td>
<td>Second Filter Iterations – The Second Filter Iterations field selects the number of samples taken for the second level filter.<br>
00 Encoding 0 – Number of samples is set to 4<br>
01 Encoding 1 – Number of samples is set to 6<br>
10 Encoding 2 – Number of samples is set to 10<br>
11 Encoding 3 – Number of samples is set to 18
</td>
</tr>
<tr>
<td>2:0<br>ESI</td>
<td>Electrode Sample Interval – The Electrode Sample Interval field selects the period between samples used for the second level of filtering.<br>
000 Encoding 0 – Period set to 1 ms<br>
001 Encoding 1 – Period set to 2 ms<br>
~<br>
111 Encoding 7 – Period set to 128 ms
</td>
</tr>
</tbody>
</table>

<p>The SFI, ESI and FFI are described in AN3890. In addition to these global (same for all electrodes) settings, the MPR121 electrodes can also be independently configured.</p>


---


# ELECTRODE CHARGE CURRENT REGISTER

<table>
  <thead>
    <tr>
      <th>7</th>
      <th>6</th>
      <th>5</th>
      <th>4</th>
      <th>3</th>
      <th>2</th>
      <th>1</th>
      <th>0</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>R</td>
      <td>0</td>
      <td>0</td>
      <td colspan="5" align="center">CDC</td>
    </tr>
<tr>
      <td>W</td>
      <td colspan="7" bgcolor="#C0C0C0"></td>
    </tr>
<tr>
      <td>Reset:</td>
      <td>0</td>
      <td>0</td>
      <td>0</td>
      <td>0</td>
      <td>0</td>
      <td>0</td>
      <td>0</td>
      <td>0</td>
    </tr>
<tr>
      <td colspan="9" bgcolor="#C0C0C0">= Unimplemented</td>
    </tr>
  </tbody>
</table>

> **Figure 8. Electrode Charge Current Register**

<table>
  <thead>
    <tr>
      <th>Field</th>
      <th>Description</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>5:0<br>CDC</td>
      <td>
        Electrode # Charge Discharge Current – The Charge Discharge Current field selects the supply current to be used when charging and discharging an electrode.<br>
        000000 Encoding 0 – Disables Electrode Charging<br>
        000001 Encoding 1 – Sets the current to 1 μA<br>
        ~<br>
        111111 Encoding 63 – Sets the current to 63 μA
      </td>
    </tr>
  </tbody>
</table>

# ELECTRODE CHARGE TIME

<table>
  <thead>
    <tr>
      <th>7</th>
      <th>6</th>
      <th>5</th>
      <th>4</th>
      <th>3</th>
      <th>2</th>
      <th>1</th>
      <th>0</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>R</td>
      <td>0</td>
      <td colspan="3" align="center">CDT#</td>
      <td>0</td>
      <td colspan="2" align="center">CDT#</td>
    </tr>
<tr>
      <td>W</td>
      <td colspan="7" bgcolor="#C0C0C0"></td>
    </tr>
<tr>
      <td>Reset:</td>
      <td>0</td>
      <td>0</td>
      <td>0</td>
      <td>0</td>
      <td>0</td>
      <td>0</td>
      <td>0</td>
      <td>0</td>
    </tr>
<tr>
      <td colspan="9" bgcolor="#C0C0C0">= Unimplemented</td>
    </tr>
  </tbody>
</table>

> **Figure 9. Electric Charge Time Register**

<table>
  <thead>
    <tr>
      <th>Field</th>
      <th>Description</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>6:4<br>CDT#</td>
      <td>
        Electrode # Charge Discharge Time – The Charge Discharge Time field selects the amount of time an electrode charges and discharges.<br>
        000 Encoding 0 – Global value is used.<br>
        001 Encoding 1 – Time is set to 0.5 μs<br>
        010 Encoding 2 – Time is set to 1 μs<br>
        ~<br>
        11 Encoding 7 – Time is set to 32 μs.
      </td>
    </tr>
<tr>
      <td>2:0<br>CDT#</td>
      <td>
        Electrode # Charge Discharge Time – The Charge Discharge Time field selects the amount of time an electrode charges and discharges.<br>
        000 Encoding 0 – Global value is used.<br>
        001 Encoding 1 – Time is set to 0.5 μs<br>
        010 Encoding 2 – Time is set to 1 μs<br>
        ~<br>
        11 Encoding 7 – Time is set to 32 μs.
      </td>
    </tr>
  </tbody>
</table>



---


# AUTO-CONFIGURATION

One of the new features added in the MPR121 that was not included in the MPR03X is the ability to automatically configure the Charge Current the Charge Time. This eliminates much of the guess involved with touch sensors and allows the same settings to properly configure the device for a wide range of application and electrodes. As shown earlier in this document, the sensitivity of the sensor is maximized by having the baseline be as high as possible for a specific baseline capacitance. The restriction on the high side is that a system should not charge above V<sub>DD</sub> - 0.7 V due to this being a non-linear region. Thus the target voltage used is approximately V<sub>DD</sub> - 0.7 V.

<table>
<thead>
<tr>
<th colspan="2">Table 7.</th>
</tr>
<tr>
<th>Voltage (V<sub>DD</sub>)</th>
<th>V<sub>DD</sub> - 0.7 V</th>
<th>ADC</th>
<th>Baseline</th>
</tr>
</thead>
<tbody>
<tr>
<td>1.8 V</td>
<td>1.1 V</td>
<td>625</td>
<td>156</td>
</tr>
<tr>
<td>V<sub>DD</sub></td>
<td>2.3 V</td>
<td>785</td>
<td>196</td>
</tr>
</tbody>
</table>

This implies that the automatic configuration system should target approximately 156 when V<sub>DD</sub> is 1.8 V and 196 when V<sub>DD</sub> is 3.0 V. The following three registers should be set based on the V<sub>DD</sub> in the system. If the voltage is unregulated, set the values assuming the lowest voltage necessary for the battery. If the final voltage supply in the system is not known, just use the 1.8 V values as they represent the worst case. This lower setting will not dramatically affect the performance, thus the 1.8 V could be considered default and be used in all cases where fine tuning is not required.

## AUTO-CONFIG USL REGISTER

<table>
<thead>
<tr>
<th>7</th>
<th>6</th>
<th>5</th>
<th>4</th>
<th>3</th>
<th>2</th>
<th>1</th>
<th>0</th>
</tr>
</thead>
<tbody>
<tr>
<td colspan="8" style="text-align:center;">USL</td>
</tr>
<tr>
<td>0</td>
<td>0</td>
<td>0</td>
<td>0</td>
<td>0</td>
<td>0</td>
<td>0</td>
<td>0</td>
</tr>
<tr>
<td colspan="8" style="background-color:#b0b0b0;">= Unimplemented</td>
</tr>
</tbody>
</table>

**Figure 10. AUTO-CONFIG USL Register**

<table>
<thead>
<tr>
<th>Table 8. AUTO-CONFIG USL Register Field Descriptions</th>
</tr>
</thead>
<tbody>
<tr>
<th>Field</th>
<th>Description</th>
</tr>
<tr>
<td>7:0 USL</td>
<td>
Upper Limit – The Upper Limit for the auto-configuration baseline search is set to this value.<br>
00000000 – Upper Limit set to 0<br>
00000001 – Upper Limit set to 1<br>
~<br>
11111111 – Upper Limit set to 255
</td>
</tr>
</tbody>
</table>

As this register represents the upper limit for the auto-configuration the value can be calculated by:

$$
V_{SL} = \frac{V_{DD} - 0.7}{V_{DD}} \cdot 256
$$

**Equation 8**

For the 1.8 V system, this value is 156 or 0x9C.



---


# AUTO-CONFIG TARGET LEVEL REGISTER

<table>
  <thead>
    <tr>
      <th>7</th>
      <th>6</th>
      <th>5</th>
      <th>4</th>
      <th>3</th>
      <th>2</th>
      <th>1</th>
      <th>0</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td colspan="8" style="text-align:center;">R</td>
    </tr>
<tr>
      <td colspan="8" style="text-align:center;">T_L</td>
    </tr>
<tr>
      <td colspan="8" style="text-align:center;">W</td>
    </tr>
<tr>
      <td>0</td>
      <td>0</td>
      <td>0</td>
      <td>0</td>
      <td>0</td>
      <td>0</td>
      <td>0</td>
      <td>0</td>
    </tr>
  </tbody>
</table>

= Unimplemented

**Figure 11. AUTO-CONFIG Target Level Register**

<table>
  <thead>
    <tr>
      <th>Field</th>
      <th>Description</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>7:0<br>TL</td>
      <td>
        Target Level – The Target Level for the auto-configuration baseline search is set to this value.<br>
        00000000 – Target Level set to 0<br>
        00000001 – Target Level set to 1<br>
        ~<br>
        11111111 – Target Level set to 255
      </td>
    </tr>
  </tbody>
</table>

This register represents the target level for the auto-configuration. The value can be calculated by:

$$
Target = \frac{V_{DD} - 0.7}{V_{DD}} \cdot 256 \cdot 0.9
$$

90% of USL

For a 1.8 V system, this value is 140 or 0x8C.

# AUTO-CONFIG LSL REGISTER

<table>
  <thead>
    <tr>
      <th>7</th>
      <th>6</th>
      <th>5</th>
      <th>4</th>
      <th>3</th>
      <th>2</th>
      <th>1</th>
      <th>0</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td colspan="8" style="text-align:center;">R</td>
    </tr>
<tr>
      <td colspan="8" style="text-align:center;">LSL</td>
    </tr>
<tr>
      <td colspan="8" style="text-align:center;">W</td>
    </tr>
<tr>
      <td>0</td>
      <td>0</td>
      <td>0</td>
      <td>0</td>
      <td>0</td>
      <td>0</td>
      <td>0</td>
      <td>0</td>
    </tr>
  </tbody>
</table>

= Unimplemented

**Figure 12. AUTO-CONFIG LSL Register**

<table>
  <thead>
    <tr>
      <th>Field</th>
      <th>Description</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>7:0<br>LSL</td>
      <td>
        Lower Limit – The Lower Limit for the auto-configuration baseline search is set to this value.<br>
        00000000 – Lower Limit set to 0<br>
        00000001 – Lower Limit set to 1<br>
        ~<br>
        11111111 – Lower Limit set to 255
      </td>
    </tr>
  </tbody>
</table>

This register represents the lower limit for the auto-configuration. The value can be calculated by:

$$
Target = \frac{V_{DD} - 0.7}{V_{DD}} \cdot 256 \cdot 0.65
$$

65% of USL

For a 1.8 V system, this value is 101 or 0x65.

The last setting required to set up the auto-configuration system is the AUTO e Register.


---


# AUTO-CONFIG CONTROL REGISTER

<table>
<thead>
<tr>
<th>7</th>
<th>6</th>
<th>5</th>
<th>4</th>
<th>3</th>
<th>2</th>
<th>1</th>
<th>0</th>
</tr>
</thead>
<tbody>
<tr>
<td colspan="2" style="text-align:center;">AFES</td>
<td colspan="2" style="text-align:center;">RETRY</td>
<td colspan="2" style="text-align:center;">BVA</td>
<td style="text-align:center;">ARE</td>
<td style="text-align:center;">ACE</td>
</tr>
<tr>
<td colspan="8" style="text-align:center;">Reset: 0 0 0 0 0 0 0 0</td>
</tr>
<tr>
<td colspan="8" style="text-align:center;">= Unimplemented</td>
</tr>
</tbody>
</table>

**Figure 13. AUTO-CONFIG Control Register**

<table>
<thead>
<tr>
<th>Field</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr>
<td>7:6<br>AFES</td>
<td>
First Filter Iterations – The first filter iterations field selects the number of samples taken as input to the first level of filtering. This value must match the FFI set in the AFE Configuration register for proper AUTO-CONFIG functionality.<br>
- 00 Encoding 0 – Sets samples taken to 6<br>
- 01 Encoding 1 – Sets samples taken to 10<br>
- 10 Encoding 2 – Sets samples taken to 18<br>
- 11 Encoding 3 – Sets samples taken to 34
</td>
</tr>
<tr>
<td>5:4<br>RETRY</td>
<td>
Retry – The Retry value determines under what circumstances the auto-configuration system will retry.<br>
- 00 – Retry disabled<br>
- 01 – Retry enabled<br>
- 10 – Retry enabled<br>
- 11 – Retry enabled
</td>
</tr>
<tr>
<td>3:2<br>BVA</td>
<td>
Baseline Value Adjust – The baseline value adjust determines the initial value of the baseline registers after auto-configuration completes.<br>
- 00 – Baseline is not changed<br>
- 01 – Baseline is cleared<br>
- 10 – Baseline is set to the AUTO-CONFIG baseline with the lower 3 bits cleared<br>
- 11 – Baseline is set to the AUTO-CONFIG baseline
</td>
</tr>
<tr>
<td>1<br>ARE</td>
<td>
Automatic Reconfiguration Enable – The automatic reconfiguration enable, enables or disables automatic reconfiguration.<br>
- 0 – ARE is disabled<br>
- 1 – ARE is enabled
</td>
</tr>
<tr>
<td>0<br>ACE</td>
<td>
Automatic Configuration Enable – The automatic configuration enable, enables or disables automatic configuration.<br>
- 0 – ACE is disabled<br>
- 1 – ACE is enabled
</td>
</tr>
</tbody>
</table>

The normal setup of the system is to set this register to `0x0B` or `0b00001011`. This means that the FFI is 00, but if the FFI in the AFE Configuration Register is different, it must be changed to match. For a description of this register, please refer to AN3890.

The RETRY is disabled because in production systems, this will not be required. The BVA is set to 10 which allows the baseline to be updated. 10 is used instead of 11 because this guarantees that the baseline will be lower than the data. This is preferable as it protects against false touches. If somehow the baseline started higher than the data, a touch would be triggered and the detection system would have to be reset to work correctly. Last, both the automatic configuration and automatic reconfiguration are enable. Reconfiguration will trigger any time the baseline drifts outside the range set by the USL and the LSL.


---


There is also a set of flags which show when the automatic configuration has failed. For normal sized touch electrodes, this cannot occur without the USL, LSL and TSL being incorrectly set. The most likely configuration error is to set the USL (upper limit) at a lower value than the LSL (lower limit). Thus, as the algorithm searches for settings that work, it would always result in a fail throwing the OOR (Out Of Range) status flag.  
The ARFF and ACFF also tell the user which type of configuration cycle caused the error. If it was triggered during an initial calibration, the ACFF will trigger. If the fail occurs during a reconfiguration, the ARFF will trigger.

**ELE0-7 OUT OF RANGE STATUS REGISTER**

<table>
<thead>
<tr>
<th></th>
<th>7</th>
<th>6</th>
<th>5</th>
<th>4</th>
<th>3</th>
<th>2</th>
<th>1</th>
<th>0</th>
</tr>
</thead>
<tbody>
<tr>
<td>R</td>
<td>E7S</td>
<td>E6S</td>
<td>E5S</td>
<td>E4S</td>
<td>E3S</td>
<td>E2S</td>
<td>E1S</td>
<td>E0S</td>
</tr>
<tr>
<td>W</td>
<td colspan="8" style="background-color:#c0c0c0;"></td>
</tr>
<tr>
<td>Reset:</td>
<td>0</td>
<td>0</td>
<td>0</td>
<td>0</td>
<td>0</td>
<td>0</td>
<td>0</td>
<td>0</td>
</tr>
<tr>
<td colspan="9" style="background-color:#c0c0c0;">= Unimplemented</td>
</tr>
</tbody>
</table>

**Figure 14. ELE0-7 Out Of Range Status Register**

**Table 12. ELE0-7 Out Of Range Status Register Field Descriptions**

<table>
<thead>
<tr>
<th>Field</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr>
<td>7<br>E7S</td>
<td>
Electrode 7 OOR Status – The Electrode 7 OOR Status shows if the AUTO-CONFIG has failed.<br>
0 – Auto-configuration Successful<br>
1 – Auto-configuration Failed
</td>
</tr>
<tr>
<td>6<br>E6S</td>
<td>
Electrode 6 OOR Status – The Electrode 7 OOR Status shows if the AUTO-CONFIG has failed.<br>
0 – Auto-configuration Successful<br>
1 – Auto-configuration Failed
</td>
</tr>
<tr>
<td>5<br>E5S</td>
<td>
Electrode 5 OOR Status – The Electrode 7 OOR Status shows if the AUTO-CONFIG has failed.<br>
0 – Auto-configuration Successful<br>
1 – Auto-configuration Failed
</td>
</tr>
<tr>
<td>4<br>E4S</td>
<td>
Electrode 4 OOR Status – The Electrode 7 OOR Status shows if the AUTO-CONFIG has failed.<br>
0 – Auto-configuration Successful<br>
1 – Auto-configuration Failed
</td>
</tr>
<tr>
<td>3<br>E3S</td>
<td>
Electrode 3 OOR Status – The Electrode 7 OOR Status shows if the AUTO-CONFIG has failed.<br>
0 – Auto-configuration Successful<br>
1 – Auto-configuration Failed
</td>
</tr>
<tr>
<td>2<br>E2S</td>
<td>
Electrode 2 OOR Status – The Electrode 7 OOR Status shows if the AUTO-CONFIG has failed.<br>
0 – Auto-configuration Successful<br>
1 – Auto-configuration Failed
</td>
</tr>
<tr>
<td>1<br>E1S</td>
<td>
Electrode 1 OOR Status – The Electrode 7 OOR Status shows if the AUTO-CONFIG has failed.<br>
0 – Auto-configuration Successful<br>
1 – Auto-configuration Failed
</td>
</tr>
<tr>
<td>0<br>E0S</td>
<td>
Electrode 0 OOR Status – The Electrode 7 OOR Status shows if the AUTO-CONFIG has failed.<br>
0 – Auto-configuration Successful<br>
1 – Auto-configuration Failed
</td>
</tr>
</tbody>
</table>



---


# ELE8-11, ELEPROX OUT OF RANGE STATUS REGISTER

<table>
<thead>
<tr>
<th>7</th>
<th>6</th>
<th>5</th>
<th>4</th>
<th>3</th>
<th>2</th>
<th>1</th>
<th>0</th>
</tr>
</thead>
<tbody>
<tr>
<td>ARFF</td>
<td>ACFF</td>
<td>0</td>
<td>ELEPROXS</td>
<td>E11S</td>
<td>E10S</td>
<td>E9S</td>
<td>E8S</td>
</tr>
<tr>
<td colspan="8" style="background-color:#b0b0b0;"> </td>
</tr>
<tr>
<td>Reset:</td>
<td>0</td>
<td>0</td>
<td>0</td>
<td>0</td>
<td>0</td>
<td>0</td>
<td>0</td>
<td>0</td>
</tr>
<tr>
<td colspan="8">= Unimplemented</td>
</tr>
</tbody>
</table>

> **Figure 15. ELE8-11, ELEPROX Out Of Range Status Register**

<table>
<thead>
<tr>
<th>Field</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr>
<td>7<br>ARFF</td>
<td>
Automatic Reconfiguration Fail Flag – The Automatic Reconfiguration Fail Flag shows if the OOR was triggered during a reconfiguration cycle.<br>
0 – Auto-reconfiguration did not cause the OOR flag<br>
1 – Auto-reconfiguration did cause the OOR flag
</td>
</tr>
<tr>
<td>6<br>ACFF</td>
<td>
Automatic Configuration Fail Flag – The Automatic Configuration Fail Flag shows if the OOR was triggered during an initial configuration cycle.<br>
0 – Auto-configuration did not cause the OOR flag<br>
1 – Auto-configuration did cause the OOR flag
</td>
</tr>
<tr>
<td>4<br>ELEPROXS</td>
<td>
Electrode PROX OOR Status – The Electrode PROX OOR Status shows if the AUTO-CONFIG has failed.<br>
0 – Auto-configuration Successful<br>
1 – Auto-configuration Failed
</td>
</tr>
<tr>
<td>3<br>E11S</td>
<td>
Electrode 11 OOR Status – The Electrode 11 OOR Status shows if the AUTO-CONFIG has failed.<br>
0 – Auto-configuration Successful<br>
1 – Auto-configuration Failed
</td>
</tr>
<tr>
<td>2<br>E10S</td>
<td>
Electrode 10 OOR Status – The Electrode 10 OOR Status shows if the AUTO-CONFIG has failed.<br>
0 – Auto-configuration Successful<br>
1 – Auto-configuration Failed
</td>
</tr>
<tr>
<td>1<br>E9S</td>
<td>
Electrode 9 OOR Status – The Electrode 9 OOR Status shows if the AUTO-CONFIG has failed.<br>
0 – Auto-configuration Successful<br>
1 – Auto-configuration Failed
</td>
</tr>
<tr>
<td>0<br>E8S</td>
<td>
Electrode 8 OOR Status – The Electrode 8 OOR Status shows if the AUTO-CONFIG has failed.<br>
0 – Auto-configuration Successful<br>
1 – Auto-configuration Failed
</td>
</tr>
</tbody>
</table>


