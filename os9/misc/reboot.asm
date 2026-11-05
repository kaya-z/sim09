* SPDX-License-Identifier: MIT
********************************************************************
* Reboot - start the machine again, as at the power on
*
* Writes $A6 to the power control register (I/O base + $0F: $E00F on
* Level 1, $FF8F on Level 2).  The simulator and the FPGA board then
* clear the memory, reset the devices and start the boot ROM again,
* which reads the OS from the card (so a new card needs no USB replug).
* The files are already written (every write goes through).  Assemble
* with -D L2 for Level 2.
*
* Part of the MC6809/OS-9 simulator (see README).
********************************************************************

         nam   Reboot
         ttl   Restart the machine

         ifp1
         use   defsfile
         endc

tylg     set   Prgrm+Objct
atrv     set   ReEnt+rev
rev      set   $00
edition  set   1

PWR      set   $E00F
         IFNE  L2
PWR      set   $FF8F
         ENDC

         mod   eom,name,tylg,atrv,start,size

         org   0
         rmb   100
size     equ   .

name     fcs   /Reboot/
         fcb   edition

msg      fcc   "Rebooting..."
         fcb   C$CR

start    leax  msg,pcr
         lda   #1
         ldy   #40
         os9   I$WritLn
         ldx   #25                 half a second: the message leaves the transmit ring
         os9   F$Sleep
         orcc  #$50                nothing may run between the write and the restart
         lda   #$A6
         sta   >PWR
loop     bra   loop

         emod
eom      equ   *
         end
