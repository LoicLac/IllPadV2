
# MPR121 Jitter and False Touch Detection

## INTRODUCTION  
Touch acquisition takes a few different parts of the system in order to detect touch. The baseline filter and touch detection are tightly coupled. The purpose of the touch detection block is to use the baseline value and the 2nd level filter data to determine when a user has touched an electrode. The electrodes are independently configured using the Touch Threshold and Release Threshold registers. The global Debounce register also controls when a touch is detected by adding some minimal delay. The data is then output through a couple of registers: Filtered Data High, Filtered Data Low, Baseline Data and two touch output registers.

<table>
  <thead>
    <tr>
      <th colspan="4">AFE AQUISITION</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><b>RAW DATA</b><br>1 - 32 μs</td>
      <td><b>1st FILTER</b><br>1 - 128 μs</td>
      <td><b>2nd FILTER</b><br>4 - 2048 μs</td>
      <td><b>BASELINE FILTER</b></td>
    </tr>
<tr>
      <td colspan="4" style="text-align:center;">TOUCH</td>
    </tr>
<tr>
      <td colspan="4" style="text-align:center;"><b>STATUS REGISTER</b></td>
    </tr>
<tr>
      <td colspan="4" style="text-align:center;">IRQ</td>
    </tr>
  </tbody>
</table>

**Figure 1. Data Flow in the MPR121**

© Freescale Semiconductor, Inc., 2009, 2010. All rights reserved.


---


First, the MPR121 touch sensor detects touch by the methods in this application note, and the data is output through the first two registers in the map. The two touch status registers both trigger an interrupt on any change of the data. Thus, as a touch happens (bit is set) an interrupt will be triggered, and when a touch is released (bit is cleared) it will also trigger. To clear the interrupt all you must do is initiate a I2C communication, with the intent that you read register 0x00 and 0x01 to determine which electrodes are touched.

**TOUCH STATUS REGISTER 0**

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
      <td colspan="8" style="background-color:#bfbfbf;"></td>
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
      <td colspan="9">= Unimplemented</td>
    </tr>
  </tbody>
</table>

**Figure 2. Touch Status Register 0**

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
        Electrode 7 Status – The Electrode 7 Status bit shows touched or not touched.<br>
        0 – Not Touched<br>
        1 – Touched
      </td>
    </tr>
<tr>
      <td>6<br>E6S</td>
      <td>
        Electrode 6 Status – The Electrode 6 Status bit shows touched or not touched.<br>
        0 – Not Touched<br>
        1 – Touched
      </td>
    </tr>
<tr>
      <td>5<br>E5S</td>
      <td>
        Electrode 5 Status – The Electrode 5 Status bit shows touched or not touched.<br>
        0 – Not Touched<br>
        1 – Touched
      </td>
    </tr>
<tr>
      <td>4<br>E4S</td>
      <td>
        Electrode 4 Status – The Electrode 4 Status bit shows touched or not touched.<br>
        0 – Not Touched<br>
        1 – Touched
      </td>
    </tr>
<tr>
      <td>3<br>E3S</td>
      <td>
        Electrode 3 Status – The Electrode 3 Status bit shows touched or not touched.<br>
        0 – Not Touched<br>
        1 – Touched
      </td>
    </tr>
<tr>
      <td>2<br>E2S</td>
      <td>
        Electrode 2 Status – The Electrode 2 Status bit shows touched or not touched.<br>
        0 – Not Touched<br>
        1 – Touched
      </td>
    </tr>
<tr>
      <td>1<br>E1S</td>
      <td>
        Electrode 1 Status – The Electrode 1 Status bit shows touched or not touched.<br>
        0 – Not Touched<br>
        1 – Touched
      </td>
    </tr>
<tr>
      <td>0<br>E0S</td>
      <td>
        Electrode 0 Status – The Electrode 0 Status bit shows touched or not touched.<br>
        0 – Not Touched<br>
        1 – Touched
      </td>
    </tr>
  </tbody>
</table>



---


# TOUCH STATUS REGISTER 1

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
<td>OVCF</td>
<td>0</td>
<td>0</td>
<td>EPROXS</td>
<td>E11S</td>
<td>E10S</td>
<td>E9S</td>
<td>E8S</td>
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
<td colspan="9">= Unimplemented</td>
</tr>
</tbody>
</table>

> **Figure 3. Touch Status Register1**

<table>
<thead>
<tr>
<th>Field</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr>
<td>7<br>OVCF</td>
<td>
Over Current Flag – The Over Current Flag will be set any time the wrong value of Rext is connected to the MPR121’s Rext pin. This is to protect the part from high current that could result from an incorrect resistor value.<br>
0 – Correct Rext resistor value<br>
1 – Incorrect Rext resistor value
</td>
</tr>
<tr>
<td>4<br>EPROXS</td>
<td>
Electrode PROX Status – The Electrode PROX Status bit shows touched or not touched.<br>
0 – Not Touched<br>
1 – Touched
</td>
</tr>
<tr>
<td>3<br>E11S</td>
<td>
Electrode 11 Status – The Electrode 11 Status bit shows touched or not touched.<br>
0 – Not Touched<br>
1 – Touched
</td>
</tr>
<tr>
<td>2<br>E10S</td>
<td>
Electrode 10 Status – The Electrode 10 Status bit shows touched or not touched.<br>
0 – Not Touched<br>
1 – Touched
</td>
</tr>
<tr>
<td>1<br>E9S</td>
<td>
Electrode 9 Status – The Electrode 9 Status bit shows touched or not touched.<br>
0 – Not Touched<br>
1 – Touched
</td>
</tr>
<tr>
<td>0<br>E8S</td>
<td>
Electrode 8 Status – The Electrode 8 Status bit shows touched or not touched.<br>
0 – Not Touched<br>
1 – Touched
</td>
</tr>
</tbody>
</table>

The next registers shown are used to provide raw data information and baseline information. The values in these registers and how they go together are described in this application note and others.


---


# FILTER DATA HIGH

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
      <td>0</td>
      <td>0</td>
      <td>0</td>
      <td>0</td>
      <td>0</td>
      <td>0</td>
      <td colspan="2">FDHB</td>
    </tr>
<tr>
      <td>W</td>
      <td colspan="8" style="background-color:#bfbfbf"></td>
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
      <td colspan="9" style="background-color:#bfbfbf">= Unimplemented</td>
    </tr>
  </tbody>
</table>

> Figure 4. Filtered Data High Register

<table>
  <thead>
    <tr>
      <th>Field</th>
      <th>Description</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>7:0<br>FDHB</td>
      <td>
        Filtered Data High Bits – The Filtered Data High Bits displays the higher 2 bits of the 10 bit filtered A/D reading.<br>
        00 Encoding 0<br>
        ~<br>
        11 Encoding 3
      </td>
    </tr>
  </tbody>
</table>

# FILTERED DATA LOW

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
      <td colspan="8">FDLB</td>
    </tr>
<tr>
      <td>W</td>
      <td colspan="8" style="background-color:#bfbfbf"></td>
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
      <td colspan="9" style="background-color:#bfbfbf">= Unimplemented</td>
    </tr>
  </tbody>
</table>

> Figure 5. Filtered Data Low Register

<table>
  <thead>
    <tr>
      <th>Field</th>
      <th>Description</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>7:0<br>FDLB</td>
      <td>
        Filtered Data Low Byte – The Filtered Data Low Byte displays the lower 8 bits of the 10 bit filtered A/D reading.<br>
        00000000 Encoding 0<br>
        ~<br>
        11111111 Encoding 255
      </td>
    </tr>
  </tbody>
</table>



---


# BASELINE VALUE

<table>
<thead>
<tr>
<th>7</th><th>6</th><th>5</th><th>4</th><th>3</th><th>2</th><th>1</th><th>0</th>
</tr>
</thead>
<tbody>
<tr>
<td colspan="8" align="center">R</td>
</tr>
<tr>
<td colspan="8" align="center">BV</td>
</tr>
<tr>
<td colspan="8" align="center">W</td>
</tr>
<tr>
<td>Reset:</td><td>0</td><td>0</td><td>0</td><td>0</td><td>0</td><td>0</td><td>0</td><td>0</td>
</tr>
<tr>
<td colspan="8" style="background-color: #c0c0c0;">= Unimplemented</td>
</tr>
</tbody>
</table>

**Figure 6. Filtered Data High Register**

<table>
<thead>
<tr>
<th>Field</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr>
<td>7:0<br>BV</td>
<td>
Baseline Value – The Baseline Value byte displays the higher 8 bits of the 10 bit baseline value.<br>
00000000 Encoding 0 – The 10 bit baseline value is between 0 and 3.<br>
~<br>
11111111 Encoding 255 – The 10 bit baseline value is between 1020 and 1023.
</td>
</tr>
</tbody>
</table>

In this system, a touch is defined as any time the difference between the Filtered Data and the Baseline Value is greater than the threshold. Since this calculation is done totally internal to the part, it is unnecessary for the user to actually do this math in the software. If it were being done, the steps would be to first combine the Filtered Data Low and Filtered Data High values into a single 10-bit number. Thus,

$$
Data = Filtered\ Data\ High \times 256 + Filtered\ Data\ Low
$$

The baseline is then shifted to the left to make it equal scale to the Data.

$$
Baseline = Baseline\ Value \times 4
$$

Internally to the device, the full 10-bit value is stored, but as this level of precision is not necessary as the low two bits are disregarded for output. The Touch Threshold is a user defined value. There is both a touch and an un-touch threshold to provide hysteresis.

## TOUCH THRESHOLD REGISTER

<table>
<thead>
<tr>
<th>7</th><th>6</th><th>5</th><th>4</th><th>3</th><th>2</th><th>1</th><th>0</th>
</tr>
</thead>
<tbody>
<tr>
<td colspan="8" align="center">R</td>
</tr>
<tr>
<td colspan="8" align="center">TTH</td>
</tr>
<tr>
<td colspan="8" align="center">W</td>
</tr>
<tr>
<td>Reset:</td><td>0</td><td>0</td><td>0</td><td>0</td><td>0</td><td>0</td><td>0</td><td>0</td>
</tr>
<tr>
<td colspan="8" style="background-color: #c0c0c0;">= Unimplemented</td>
</tr>
</tbody>
</table>

**Figure 7. Touch Threshold Register**

<table>
<thead>
<tr>
<th>Field</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr>
<td>7:0<br>TTH</td>
<td>
Touch Threshold – The Touch Threshold Byte sets the trip point for detecting a touch.<br>
00000000 Encoding 0<br>
~<br>
11111111 Encoding 255
</td>
</tr>
</tbody>
</table>



---


# RELEASE THRESHOLD REGISTER

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
<td colspan="8" style="text-align:center;">RTH</td>
</tr>
<tr>
<td colspan="8" style="text-align:center;">Reset: 0 0 0 0 0 0 0 0</td>
</tr>
<tr>
<td colspan="8" style="background-color: #c0c0c0; text-align:center;">= Unimplemented</td>
</tr>
</tbody>
</table>

**Figure 8. Release Threshold Register**

<table>
<thead>
<tr>
<th>Field</th>
<th>Description</th>
</tr>
</thead>
<tbody>
<tr>
<td>7:0 RTH</td>
<td>
Release Threshold – The Release Threshold Byte sets the trip point for detecting a touch.<br>
00000000 Encoding 0<br>
~<br>
11111111 Encoding 255
</td>
</tr>
</tbody>
</table>

For the system to recognize a touch the delta must be greater than the Touch Threshold.

$$
\text{Delta} = \text{Baseline} - \text{Data}
$$

$$
\text{Trigger Touch} \rightarrow \Delta > \text{Touch Threshold}
$$

A release is triggered when the Delta falls below the Release Threshold. This can happen for both changes to the Baseline and actual Data changes. To understand how the Baseline can change, refer to AN3891.

$$
\text{Trigger Release} \rightarrow \Delta < \text{Touch Threshold}
$$


---


# DEBOUNCE TOUCH AND RELEASE REGISTER

The last register available in this set is the Debounce register. The Debounce register maintains the accuracy of touch and releases by further improving the performance. The debounce allows two different settings to prevent bounce in the end system. If the value is set to 0x22, the requirement would be three sequential changes in status before the change would be recognized.

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
      <td colspan="3">DR</td>
      <td>0</td>
      <td colspan="2">Dt</td>
    </tr>
<tr>
      <td>W</td>
      <td colspan="7" style="background-color:#bfbfbf;">= Unimplemented</td>
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

**Figure 9. Debounce Touch and Release Register**

<table>
  <thead>
    <tr>
      <th>Field</th>
      <th>Description</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>6:4<br>DR</td>
      <td>
        Debounce Release – The Debounce Release determines the number of sequential release detections before an interrupt is triggered and a release is reported.<br>
        000 Encoding 0 - Consecutive releases detection before Status change is 1<br>
        001 Encoding 1 - Consecutive releases detection before Status change is 2<br>
        ~<br>
        007 Encoding 7 - Consecutive releases detection before Status change is 8
      </td>
    </tr>
<tr>
      <td>2:0<br>DT</td>
      <td>
        Debounce Touch – The Debounce Touch determines the number of sequential touch detections before an interrupt is triggered and a touch is reported.<br>
        000 Encoding 0 - Consecutive touch detection before Status change is 1<br>
        001 Encoding 1 - Consecutive touch detection before Status change is 2<br>
        ~<br>
        007 Encoding 7 - Consecutive touch detection before Status change is 8
      </td>
    </tr>
  </tbody>
</table>

## CONCLUSION

The use of each of the features together can have a great effect on the jitter and false touch rejection. Jitter is prevented by utilizing the two threshold settings. Thus the provided hysteresis prevents jitter on the data from going through to the output. Depending on environmental conditions, the Debounce can be used to eliminate the remainder of dramatic change of the signal that aren’t really touches.

Additional filtering can be done before the data gets to the touch detection system. Refer to Freescale Application Note AN3890.



---
