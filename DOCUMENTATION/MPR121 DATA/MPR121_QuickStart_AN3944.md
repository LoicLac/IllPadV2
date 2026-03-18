
# MPR121 Quick Start Guide

## INTRODUCTION

The MPR121 is Freescale Semiconductor’s top of the line touch sensor and can fit into a wide range of applications. These applications can all be accommodated by having a device with a very large range of flexibility. While all of these added features can allow for a wide range of flexibility, they can also add an unnecessary layer of complication. For advanced users who want to do more than basic touch detection, additional information can be found in other application notes.

To start, the device is configured through an I²C serial interface. The following table lists the registers that are initialized. The order they are written in is not significant except that register 0x05E, the Electrode Configuration Register must be written last.

[Table placeholder]

<table>
    <thead>
    <tr>
        <th>Register Address</th>
        <th>Register Name</th>
        <th>Value</th>
        <th>Application Note</th>
        <th>Section</th>
    </tr>
    </thead>
    <tr>
        <td>0x2B</td>
        <td>MHD Rising</td>
        <td>0x01</td>
        <td>AN3891</td>
        <td>A</td>
    </tr>
    <tr>
        <td>0x2C</td>
        <td>NHD Amount Rising</td>
        <td>0x01</td>
        <td>AN3891</td>
        <td>A</td>
    </tr>
    <tr>
        <td>0x2D</td>
        <td>NCL Rising</td>
        <td>0x00</td>
        <td>AN3891</td>
        <td>A</td>
    </tr>
    <tr>
        <td>0x2E</td>
        <td>FDL Rising</td>
        <td>0x00</td>
        <td>AN3891</td>
        <td>A</td>
    </tr>
    <tr>
        <td>0x2F</td>
        <td>MHD Falling</td>
        <td>0x01</td>
        <td>AN3891</td>
        <td>B</td>
    </tr>
    <tr>
        <td>0x30</td>
        <td>NHD Amount Falling</td>
        <td>0x01</td>
        <td>AN3891</td>
        <td>B</td>
    </tr>
    <tr>
        <td>0x31</td>
        <td>NCL Falling</td>
        <td>0xFF</td>
        <td>AN3891</td>
        <td>B</td>
    </tr>
    <tr>
        <td>0x32</td>
        <td>FDL Falling</td>
        <td>0x02</td>
        <td>AN3891</td>
        <td>B</td>
    </tr>
    <tr>
        <td>0x41</td>
        <td>ELE0 Touch Threshold</td>
        <td>0x0F</td>
        <td>AN3892</td>
        <td>C</td>
    </tr>
    <tr>
        <td>0x42</td>
        <td>ELE0 Release Threshold</td>
        <td>0x0A</td>
        <td>AN3892</td>
        <td>C</td>
    </tr>
    <tr>
        <td>0x43</td>
        <td>ELE1 Touch Threshold</td>
        <td>0x0F</td>
        <td>AN3892</td>
        <td>C</td>
    </tr>
    <tr>
        <td>0x44</td>
        <td>ELE1 Release Threshold</td>
        <td>0x0A</td>
        <td>AN3892</td>
        <td>C</td>
    </tr>
    <tr>
        <td>0x45</td>
        <td>ELE2 Touch Threshold</td>
        <td>0x0F</td>
        <td>AN3892</td>
        <td>C</td>
    </tr>
    <tr>
        <td>0x46</td>
        <td>ELE2 Release Threshold</td>
        <td>0x0A</td>
        <td>AN3892</td>
        <td>C</td>
    </tr>
    <tr>
        <td>0x47</td>
        <td>ELE3 Touch Threshold</td>
        <td>0x0F</td>
        <td>AN3892</td>
        <td>C</td>
    </tr>
    <tr>
        <td>0x48</td>
        <td>ELE3 Release Threshold</td>
        <td>0x0A</td>
        <td>AN3892</td>
        <td>C</td>
    </tr>
    <tr>
        <td>0x49</td>
        <td>ELE4 Touch Threshold</td>
        <td>0x0F</td>
        <td>AN3892</td>
        <td>C</td>
    </tr>
    <tr>
        <td>0x4A</td>
        <td>ELE4 Release Threshold</td>
        <td>0x0A</td>
        <td>AN3892</td>
        <td>C</td>
    </tr>
    <tr>
        <td>0x4B</td>
        <td>ELE5 Touch Threshold</td>
        <td>0x0F</td>
        <td>AN3892</td>
        <td>C</td>
    </tr>
    <tr>
        <td>0x4C</td>
        <td>ELE5 Release Threshold</td>
        <td>0x0A</td>
        <td>AN3892</td>
        <td>C</td>
    </tr>
    <tr>
        <td>0x4D</td>
        <td>ELE6 Touch Threshold</td>
        <td>0x0F</td>
        <td>AN3892</td>
        <td>C</td>
    </tr>
    <tr>
        <td>0x4E</td>
        <td>ELE6 Release Threshold</td>
        <td>0x0A</td>
        <td>AN3892</td>
        <td>C</td>
    </tr>
    <tr>
        <td>0x4F</td>
        <td>ELE7 Touch Threshold</td>
        <td>0x0F</td>
        <td>AN3892</td>
        <td>C</td>
    </tr>
    <tr>
        <td>0x50</td>
        <td>ELE7 Release Threshold</td>
        <td>0x0A</td>
        <td>AN3892</td>
        <td>C</td>
    </tr></table>



---


The following sections describe what each of the defaults do and recommendations for variations.

## Section A

<table>
  <thead>
    <tr>
      <th>Register Address</th>
      <th>Register Name</th>
      <th>Value</th>
      <th>Application Note</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>0x2B</td>
      <td>MHD Rising</td>
      <td>0x01</td>
      <td>AN3891</td>
    </tr>
<tr>
      <td>0x2C</td>
      <td>NHD Amount Rising</td>
      <td>0x01</td>
      <td>AN3891</td>
    </tr>
<tr>
      <td>0x2D</td>
      <td>NCL Rising</td>
      <td>0x00</td>
      <td>AN3891</td>
    </tr>
<tr>
      <td>0x2E</td>
      <td>FDL Rising</td>
      <td>0x00</td>
      <td>AN3891</td>
    </tr>
  </tbody>
</table>

**Description:**  
This group of setting controls the filtering of the system when the data is greater than the baseline. The setting used allow the filter to act quickly and adjust for environmental changes. Additionally, if calibration happens to take place while a touch occurs, the value will self adjust very quickly. This auto-recovery or snap back prevents repeated false negative for a touch detection.

**Variation:**  
As the filter is sensitive to setting changes, it is recommended that users read AN3891 before changing the values. In most cases these default values will work

## Section B

<table>
  <thead>
    <tr>
      <th>Register Address</th>
      <th>Register Name</th>
      <th>Value</th>
      <th>Application Note</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>0x2F</td>
      <td>MHD Falling</td>
      <td>0x01</td>
      <td>AN3891</td>
    </tr>
<tr>
      <td>0x30</td>
      <td>NHD Amount Falling</td>
      <td>0x01</td>
      <td>AN3891</td>
    </tr>
<tr>
      <td>0x31</td>
      <td>NCL Falling</td>
      <td>0xFF</td>
      <td>AN3891</td>
    </tr>
<tr>
      <td>0x32</td>
      <td>FDL Falling</td>
      <td>0x02</td>
      <td>AN3891</td>
    </tr>
  </tbody>
</table>

**Description:**  
This group of setting controls the filtering of the system, when the data is less than the baseline. The settings slow down the filter as the negative charge is in the same direction as a touch. By slowing down the filter, touch signals are “rejected” by the baseline filter. While at the same time long term environmental change that occur slower than at a touch are accepted. This low pass filter both allows for touches to be detected properly while preventing false positive by passing environmental change through the filter.

**Variation:**  
As the filter is sensitive to setting changes, it is recommended that users read AN3891 before changing the values. In most cases these default values will work

<table>
    <thead>
    <tr>
        <th>Register Address</th>
        <th>Register Name</th>
        <th>Value</th>
        <th>Application Note</th>
        <th>Section</th>
    </tr>
    </thead>
    <tr>
        <td>0x51</td>
        <td>ELE8 Touch Threshold</td>
        <td>0x0F</td>
        <td>AN3892</td>
        <td>C</td>
    </tr>
    <tr>
        <td>0x52</td>
        <td>ELE8 Release Threshold</td>
        <td>0x0A</td>
        <td>AN3892</td>
        <td>C</td>
    </tr>
    <tr>
        <td>0x53</td>
        <td>ELE9 Touch Threshold</td>
        <td>0x0F</td>
        <td>AN3892</td>
        <td>C</td>
    </tr>
    <tr>
        <td>0x54</td>
        <td>ELE9 Release Threshold</td>
        <td>0x0A</td>
        <td>AN3892</td>
        <td>C</td>
    </tr>
    <tr>
        <td>0x55</td>
        <td>ELE10 Touch Threshold</td>
        <td>0x0F</td>
        <td>AN3892</td>
        <td>C</td>
    </tr>
    <tr>
        <td>0x56</td>
        <td>ELE10 Release Threshold</td>
        <td>0x0A</td>
        <td>AN3892</td>
        <td>C</td>
    </tr>
    <tr>
        <td>0x57</td>
        <td>ELE11 Touch Threshold</td>
        <td>0x0F</td>
        <td>AN3892</td>
        <td>C</td>
    </tr>
    <tr>
        <td>0x58</td>
        <td>ELE11 Release Threshold</td>
        <td>0x0A</td>
        <td>AN3892</td>
        <td>C</td>
    </tr>
    <tr>
        <td>0x5D</td>
        <td>Filter Configuration</td>
        <td>0x04</td>
        <td>AN3890</td>
        <td>D</td>
    </tr>
    <tr>
        <td>0x5E</td>
        <td>Electrode Configuration</td>
        <td>0x0C</td>
        <td>AN3890</td>
        <td>E</td>
    </tr>
    <tr>
        <td>0x7B</td>
        <td>AUTO-CONFIG Control Register 0</td>
        <td>0x0B</td>
        <td>AN3889</td>
        <td>F</td>
    </tr>
    <tr>
        <td>0x7D</td>
        <td>AUTO-CONFIG USL Register</td>
        <td>0x9C</td>
        <td>AN3889</td>
        <td>F</td>
    </tr>
    <tr>
        <td>0x7E</td>
        <td>AUTO-CONFIG LSL Register</td>
        <td>0x65</td>
        <td>AN3889</td>
        <td>F</td>
    </tr>
    <tr>
        <td>0x7F</td>
        <td>AUTO-CONFIG Target Level Register</td>
        <td>0x8C</td>
        <td>AN3889</td>
        <td>F</td>
    </tr></table>



---


# Section C

<table>
<thead>
<tr>
<th>Register Address</th>
<th>Register Name</th>
<th>Value</th>
<th>Application Note</th>
</tr>
</thead>
<tbody>
<tr>
<td>0x41</td>
<td>ELE0 Touch Threshold</td>
<td>0x0F</td>
<td>AN3892</td>
</tr>
<tr>
<td>0x42</td>
<td>ELE0 Release Threshold</td>
<td>0x0A</td>
<td>AN3892</td>
</tr>
<tr>
<td>0x43</td>
<td>ELE1 Touch Threshold</td>
<td>0x0F</td>
<td>AN3892</td>
</tr><tr>
<td>0x44</td>
<td>ELE1 Release Threshold</td>
<td>0x0A</td>
<td>AN3892</td>
</tr><tr>
<td>0x45</td>
<td>ELE2 Touch Threshold</td>
<td>0x0F</td>
<td>AN3892</td>
</tr><tr>
<td>0x46</td>
<td>ELE2 Release Threshold</td>
<td>0x0A</td>
<td>AN3892</td>
</tr><tr>
<td>0x47</td>
<td>ELE3 Touch Threshold</td>
<td>0x0F</td>
<td>AN3892</td>
</tr><tr>
<td>0x48</td>
<td>ELE3 Release Threshold</td>
<td>0x0A</td>
<td>AN3892</td>
</tr><tr>
<td>0x49</td>
<td>ELE4 Touch Threshold</td>
<td>0x0F</td>
<td>AN3892</td>
</tr><tr>
<td>0x4A</td>
<td>ELE4 Release Threshold</td>
<td>0x0A</td>
<td>AN3892</td>
</tr><tr>
<td>0x4B</td>
<td>ELE5 Touch Threshold</td>
<td>0x0F</td>
<td>AN3892</td>
</tr><tr>
<td>0x4C</td>
<td>ELE5 Release Threshold</td>
<td>0x0A</td>
<td>AN3892</td>
</tr><tr>
<td>0x4D</td>
<td>ELE6 Touch Threshold</td>
<td>0x0F</td>
<td>AN3892</td>
</tr><tr>
<td>0x4E</td>
<td>ELE6 Release Threshold</td>
<td>0x0A</td>
<td>AN3892</td>
</tr><tr>
<td>0x4F</td>
<td>ELE7 Touch Threshold</td>
<td>0x0F</td>
<td>AN3892</td>
</tr><tr>
<td>0x50</td>
<td>ELE7 Release Threshold</td>
<td>0x0A</td>
<td>AN3892</td>
</tr><tr>
<td>0x51</td>
<td>ELE8 Touch Threshold</td>
<td>0x0F</td>
<td>AN3892</td>
</tr><tr>
<td>0x52</td>
<td>ELE8 Release Threshold</td>
<td>0x0A</td>
<td>AN3892</td>
</tr><tr>
<td>0x53</td>
<td>ELE9 Touch Threshold</td>
<td>0x0F</td>
<td>AN3892</td>
</tr><tr>
<td>0x54</td>
<td>ELE9 Release Threshold</td>
<td>0x0A</td>
<td>AN3892</td>
</tr><tr>
<td>0x55</td>
<td>ELE10 Touch Threshold</td>
<td>0x0F</td>
<td>AN3892</td>
</tr>
<tr>
<td>0x56</td>
<td>ELE10 Release Threshold</td>
<td>0x0A</td>
<td>AN3892</td>
</tr>
<tr>
<td>0x57</td>
<td>ELE11 Touch Threshold</td>
<td>0x0F</td>
<td>AN3892</td>
</tr>
<tr>
<td>0x58</td>
<td>ELE11 Release Threshold</td>
<td>0x0A</td>
<td>AN3892</td>
</tr>
</tbody>
</table>

**Description:**  
The touch threshold registers set the minimum delta from the baseline when a touch is detected. 0x0F or 15 in decimal is an estimate of the minimum value for touch. Most electrodes will work with this value even if they vary greatly in size and shape. The value of 0x0A or 10 is the release threshold register allowed for hysteresis in the touch detection.

**Variation:**  
For very small electrodes, smaller values can be used and for very large electrodes the reverse is true. One easy method is to view the deltas actually seen in a system and set the touch at 80% and release at 70% of delta for good performance.



---


# Section D

<table>
<thead>
<tr>
<th>Register Address</th>
<th>Register Name</th>
<th>Value</th>
<th>Application Note</th>
</tr>
</thead>
<tbody>
<tr>
<td>0x5D</td>
<td>Filter Configuration</td>
<td>0x04</td>
<td>AN3890</td>
</tr>
</tbody>
</table>

**Description:**  
There are three settings embedded in this register so it is only necessary to pay attention to one. The ESI controls the sample rate of the device. In the default, the setting used is 0x00 for 1 ms sample rate. Since the SFI is set to 00, resulting in 4 samples averaged, the response time will be 4 ms.

**Variation:**  
To save power, the 1 ms can be increased to 128 ms by increasing the setting to 0x07. The values are base 2 exponential thus 0x01 = 2 ms; 0x02 = 4 ms; and so on to 0x07 = 128 ms. Most of the time, 0x04 results in the best compromise between power consumption and response time.

# Section E

<table>
<thead>
<tr>
<th>Register Address</th>
<th>Register Name</th>
<th>Value</th>
<th>Application Note</th>
</tr>
</thead>
<tbody>
<tr>
<td>0x5E</td>
<td>Electrode Configuration</td>
<td>0x0C</td>
<td>AN3890</td>
</tr>
</tbody>
</table>

**Description:**  
This register controls the number of electrodes being enabled and the mode the device is in. There are only two modes, Standby (when the value is 0x00) and Run (when the value of the lower bit is non-zero). The default value shown enables all 12 electrodes by writing decimal 12 or hex 0x0C to the register. Typically other registers cannot be changed while the part is running so this register should always be written last.

**Variation:**  
During debug of a system, this register will change between the number of electrodes and 0x00 every time a register needs to change. In a production system, this register will only need to be written when the mode is changed from Standby to Run or vise versa.

# Section F

<table>
<thead>
<tr>
<th>Register Address</th>
<th>Register Name</th>
<th>Value</th>
<th>Application Note</th>
</tr>
</thead>
<tbody>
<tr>
<td>0x7B</td>
<td>AUTO-CONFIG Control Register 0</td>
<td>0x0B</td>
<td>AN3889</td>
</tr>
<tr>
<td>0x7D</td>
<td>AUTO-CONFIG USL Register</td>
<td>0x9C</td>
<td>AN3889</td>
</tr>
<tr>
<td>0x7E</td>
<td>AUTO-CONFIG LSL Register</td>
<td>0x65</td>
<td>AN3889</td>
</tr>
<tr>
<td>0x7F</td>
<td>AUTO-CONFIG Target Level Register</td>
<td>0x8C</td>
<td>AN3889</td>
</tr>
</tbody>
</table>

**Description:**  
These are the settings used for the Auto Configuration. They enable AUTO-CONFIG and AUTO_RECONFIG. In addition they set the target range for the baseline. The upper limit is set to 190, the target is set to 180 and the lower limit is set to 140.

**Variation:**  
In most cases these values will never need to be change, but if a case arises, a full description is found in application note AN3889.

----

### CONCLUSION  
In many applications for the MPR121, the default settings presented in this document will be sufficient for both design time activities as well as in the production implementation.


---
