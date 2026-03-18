
# MPR121 Serial Communication

## INTRODUCTION

The MPR121 uses an I²C Serial Interface. The I²C protocol implementation and the specifics of communicating with the Touch Sensor Controller are detailed in this application note.

### SERIAL-ADDRESSING

The MPR121 operates as a slave that sends and receives data through an I²C two-wire interface. The interface uses a Serial Data Line (SDA) and a Serial Clock Line (SCL) to achieve bidirectional communication between master(s) and slave(s). A master (typically a microcontroller) initiates all data transfers to and from the MPR121, and it generates the SCL clock that synchronizes the data transfer.

The MPR121 SDA line operates as both an input and an open-drain output. A pullup resistor, typically 4.7 kΩ, is required on SDA.  
The MPR121 SCL line operates only as an input. A pullup resistor, typically 4.7 kΩ, is required on SCL if there are multiple masters on the two-wire interface, or if the master in a single-master system has an open-drain SCL output.

Each transmission consists of a START condition (Figure 1) sent by a master, followed by the MPR121’s 7-bit slave address plus R/W bit, a register address byte, one or more data bytes, and finally a STOP condition.

<table>
  <thead>
    <tr>
      <th colspan="2">SDA</th>
      <th colspan="8"></th>
      <th>t<sub>BUF</sub></th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td colspan="2"></td>
      <td>t<sub>LOW</sub></td>
      <td>t<sub>SU DAT</sub></td>
      <td>t<sub>HD DAT</sub></td>
      <td>t<sub>SU STA</sub></td>
      <td>t<sub>HD STA</sub></td>
      <td></td>
      <td>t<sub>SU STO</sub></td>
      <td colspan="2"></td>
    </tr>
<tr>
      <th colspan="2">SCL</th>
      <td>t<sub>HIGH</sub></td>
      <td></td>
      <td></td>
      <td></td>
      <td></td>
      <td></td>
      <td></td>
      <td></td>
      <td></td>
    </tr>
<tr>
      <td>t<sub>HD STA</sub></td>
      <td>t<sub>R</sub></td>
      <td>t<sub>F</sub></td>
      <td colspan="8"></td>
    </tr>
<tr>
      <td>START CONDITION</td>
      <td></td>
      <td colspan="3">REPEATED START CONDITION</td>
      <td colspan="3"></td>
      <td>STOP CONDITION</td>
      <td colspan="2">START CONDITION</td>
    </tr>
  </tbody>
</table>

**Figure 1. Wire Serial Interface Timing Details**


---


# START AND STOP CONDITIONS

Both SCL and SDA remain high when the interface is not busy. A master signals the beginning of a transmission with a START (S) condition by transitioning SDA from high to low while SCL is high. When the master has finished communicating with the slave, it issues a STOP (P) condition by transitioning SDA from low to high while SCL is high. The bus is then free for another transmission.

<table>
  <tr>
    <td rowspan="2">SDA</td>
    <td>─────────────────────────────╳─────────────────────────────</td>
  </tr>
<tr>
    <td>SCL  ──┐       DATA LINE STABLE<br>         │       DATA VALID<br>         └──────── CHANGE OF DATA ALLOWED ────────</td>
  </tr>
<tr>
    <td colspan="2" align="center"><b>Figure 2. Start and Stop Conditions</b></td>
  </tr>
</table>

## BIT TRANSFER

One data bit is transferred during each clock pulse (Figure 3). The data on SDA must remain stable while SCL is high.

<table>
  <tr>
    <td>SDA</td>
    <td>─────┐       ────────────────       ────────────────       ──────</td>
  </tr>
<tr>
    <td>SCL</td>
    <td>─────┐  ▄ ▄   ▄ ▄   ▄ ▄   ▄ ▄   ▄ ▄   ──────</td>
  </tr>
<tr>
    <td colspan="2" align="center">
      <b>S</b>

---


# SLAVE ADDRESS

The MPR121 has selectable slave addresses listed by different ADDR pin connections. This also makes it possible for multiple MPR121 devices to be used together for channel expansions in a single system.

<table>
<thead>
<tr>
<th>ADDR Pin Connection</th>
<th>I<sup>2</sup>C Address</th>
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

## MESSAGE FORMAT FOR WRITING THE MPR121

A write to the MPR121 comprises the transmission of the MPR121’s keyscan slave address with the R/W bit set to 0, followed by at least one byte of information. The first byte of information is the command byte. The command byte determines which register of the MPR121 is to be written by the next byte, if received. If a STOP condition is detected after the command byte is received, the MPR121 takes no further action (Figure 5) beyond storing the command byte. Any bytes received after the command byte are data bytes.

<table>
<thead>
<tr>
<td colspan="16">Command byte is stored on receipt of STOP condition</td>
</tr>
<tr>
<td>D15</td>
<td>D14</td>
<td>D13</td>
<td>D12</td>
<td>D11</td>
<td>D10</td>
<td>D9</td>
<td>D8</td>
<td colspan="8"></td>
</tr>
<tr>
<td colspan="8">Acknowledge from MPR121</td>
<td colspan="8"></td>
</tr>
<tr>
<td>S</td>
<td colspan="6">SLAVE ADDRESS</td>
<td>0</td>
<td>A</td>
<td colspan="8">COMMAND BYTE</td>
<td>A</td>
<td>P</td>
</tr>
<tr>
<td colspan="4"></td>
<td>R/W</td>
<td colspan="12">Acknowledge from MPR121</td>
</tr>
</thead>
</table>

**Figure 5. Command Byte Received**

Any bytes received after the command byte are data bytes. The first data byte goes into the internal register of the MPR121 selected by the command byte (Figure 6).

<table>
<thead>
<tr>
<td colspan="8">Acknowledge from MPR121</td>
<td colspan="8">Acknowledge from MPR121</td>
</tr>
<tr>
<td>D15</td>
<td>D14</td>
<td>D13</td>
<td>D12</td>
<td>D11</td>
<td>D10</td>
<td>D9</td>
<td>D8</td>
<td>D7</td>
<td>D6</td>
<td>D5</td>
<td>D4</td>
<td>D3</td>
<td>D2</td>
<td>D1</td>
<td>D0</td>
</tr>
<tr>
<td colspan="16">How command byte and data byte map into MPR121's registers</td>
</tr>
<tr>
<td colspan="8">Acknowledge from MPR121</td>
<td colspan="8"></td>
</tr>
<tr>
<td>S</td>
<td colspan="6">SLAVE ADDRESS</td>
<td>0</td>
<td>A</td>
<td colspan="7">COMMAND BYTE</td>
<td>A</td>
<td colspan="2">DATA BYTE</td>
<td>A</td>
<td>P</td>
</tr>
<tr>
<td colspan="4"></td>
<td>R/W</td>
<td colspan="11"></td>
<td>1 byte</td>
</tr>
<tr>
<td colspan="16">Auto-increment memory word address</td>
</tr>
</thead>
</table>

**Figure 6. Command and Single Data Byte Received**

If multiple data bytes are transmitted before a STOP condition is detected, these bytes are generally stored in subsequent MPR121 internal registers because the command byte address generally auto-increments.


---


# MESSAGE FORMAT FOR READING THE MPR121

MPR121 is read using MPR121's internally stored register address as address pointer, the same way the stored register address is used as address pointer for a write. The pointer generally auto-increments after each data byte is read using the same rules as for a write. Thus, a read is initiated by first configuring MPR121's register address by performing a write (Figure 5) followed by a repeated start. The master can now read 'n' consecutive bytes from MPR121, with first data byte being read from the register addressed by the initialized register address.

<table>
  <thead>
    <tr>
      <th colspan="24">Figure 7. Reading MPR121</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td colspan="3">S</td>
      <td colspan="8">SLAVE ADDRESS</td>
      <td>0</td>
      <td>A</td>
      <td colspan="8">COMMAND BYTE</td>
      <td>A</td>
      <td>S</td>
      <td colspan="8">SLAVE ADDRESS</td>
      <td>1</td>
      <td>A</td>
      <td colspan="8">DATA BYTE</td>
      <td>A</td>
      <td>P</td>
    </tr>
<tr>
      <td colspan="3"></td>
      <td colspan="8">D15 D14 D13 D12 D11 D10 D9 D8</td>
      <td></td>
      <td></td>
      <td colspan="8"></td>
      <td></td>
      <td></td>
      <td colspan="8">D7 D6 D5 D4 D3 D2 D1 D0</td>
      <td></td>
      <td></td>
    </tr>
<tr>
      <td colspan="3"></td>
      <td colspan="8">Command byte is stored on receipt of STOP condition</td>
      <td></td>
      <td></td>
      <td colspan="8">Repeated Start</td>
      <td></td>
      <td></td>
      <td colspan="8"></td>
      <td></td>
      <td></td>
    </tr>
<tr>
      <td colspan="3"></td>
      <td colspan="8">Acknowledge from MPR121</td>
      <td></td>
      <td></td>
      <td colspan="8">Acknowledge from MPR121</td>
      <td></td>
      <td></td>
      <td colspan="8">Acknowledge from master</td>
      <td></td>
      <td></td>
    </tr>
<tr>
      <td colspan="3"></td>
      <td colspan="8">R/W</td>
      <td></td>
      <td></td>
      <td colspan="8">Acknowledge from MPR121</td>
      <td></td>
      <td></td>
      <td colspan="8">R/W</td>
      <td></td>
      <td></td>
    </tr>
<tr>
      <td colspan="23"></td>
      <td>n bytes</td>
      <td></td>
    </tr>
<tr>
      <td colspan="23"></td>
      <td>auto-increment memory word address</td>
      <td></td>
    </tr>
  </tbody>
</table>

## OPERATION WITH MULTIPLE MASTER

The application should use repeated starts to address the MPR121 to avoid bus confusion between I²C masters. On a I²C bus, once a master issues a start/repeated start condition, that master owns the bus until a stop condition occurs. If a master that does not own the bus attempts to take control of that bus, then improper addressing may occur. An address may always be rewritten to fix this problem. Follow I²C protocol for multiple master configurations.


