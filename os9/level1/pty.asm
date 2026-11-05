*******************************************
* PTY
*  Device Driver for emulated terminal
* Emulates the 6850 UART seen in usim
*
* V.port is the control (write) / status (read) port, V.port+1 is the data port:
*  status bit 0: 1=received character ready (RDRF)
*         bit 1: 1=ready for output (TDRE)
*         bit 7: 1=the ACIA requests an interrupt
*  control bit 7: receive interrupt enable (RIE)
*          bits 6-5 = 01: transmit interrupt enable (TIE)
*          bits 1-0 = 11: master reset
*
* Both directions are interrupt driven (the ACIA of sim09 and of the FPGA board raises IRQ):
*  - the interrupt service routine (entered with F$IRQ, called by the Clock module through D.Poll)
*    puts every received character in a ring buffer of 128 bytes, so that input is not lost while no
*    process reads and the keyboard interrupt / quit / pause characters work at once (the signal goes
*    to the last process that used the terminal), even for a program that never reads;
*  - output goes through a ring buffer of 64 bytes, emptied by the transmit interrupt: the process
*    continues while the characters are sent.  When the transmitter is idle a character goes out directly.
*
* Edition History
*
* #   date     Comments
* - -------- -------------------------------
* 1 16.11.01  Driver first written (polled). SMR
* 2 26.11.05  Interrupt driven: receive and transmit rings, F$IRQ, ^C ^E ^W at once, SS.Ready counts

         nam PTY
         ttl Pseudo Terminal

         ifp1
         use defsfile
         endc

Revision equ 2

* 6850 bits
St.RxF   equ   $01              receive data register full
St.TxE   equ   $02              transmit data register empty
St.IRQ   equ   $80              interrupt request
Cr.Rst   equ   $03              master reset
Cr.Base  equ   $15              divide by 16, 8 bits, no parity, 1 stop bit (no interrupts)
Cr.RIE   equ   $80              receive interrupt enable
Cr.TIE   equ   $20              transmit interrupt enable (with bit 6 = 0)

* the static storage must stay within one page (256 bytes): on Level 1 a second page splits the free memory and jwp would not fit
RxSize   equ   128              receive ring (a power of 2)
RxMask   equ   RxSize-1
TxSize   equ   64               transmit ring (a power of 2)
TxMask   equ   TxSize-1

         org V.SCF
RxHead   rmb 1                  receive ring: where the ISR puts the next character
RxTail   rmb 1                  where READ takes the next one
TxHead   rmb 1                  transmit ring: where WRITE puts the next character
TxTail   rmb 1                  where the ISR takes the next one
CtlShd   rmb 1                  the control register (it cannot be read)
RxBuf    rmb RxSize
TxBuf    rmb TxSize
PST      equ .

         mod PEND,PNAM,Drivr+Objct,Reent+Revision,PENT,PST
         fcb READ.+WRITE.
PNAM     fcs /PTY/
         fcb Revision

* what F$IRQ needs: flip, mask and priority.  The interrupt is requested when (status xor flip) and mask is not 0.
IRQPckt  fcb   $00,St.IRQ,$0A

PENT     lbra INIT
         lbra READ
         lbra WRITE
         lbra GETSTA
         lbra PUTSTA
         lbra TERM

**********************
* INIT
* Entry: U = Static storage, Y = Device descriptor
*   Enter the interrupt service routine, reset the ACIA and enable the receive interrupt
*
INIT     pshs cc
         orcc  #IntMasks
         ldx   V.PORT,u
         lda   #Cr.Rst            master reset (this also takes the interrupt request away)
         sta   ,x
         lda   #Cr.Base
         sta   ,x
         sta   CtlShd,u
         clr   RxHead,u
         clr   RxTail,u
         clr   TxHead,u
         clr   TxTail,u
         ldd   V.PORT,u           the status register
         leax  IRQPckt,pcr
         leay  IRQSvc,pcr
         os9   F$IRQ
         bcs   initerr
         lda   #Cr.Base+Cr.RIE
         sta   CtlShd,u
         ldx   V.PORT,u
         sta   ,x
         puls  cc
         clrb
         rts
initerr  puls  cc
         coma                     the carry is set again
         rts

**********************
* READ
* Entry: U = Static Storage
*        Y = Path Descriptor
* Exit:  A = Character read
*
*   Take a byte from the receive ring, sleep when it is empty (the interrupt wakes the process)
*
READ     pshs  cc
readlp   orcc  #IntMasks
         ldb   RxTail,u
         cmpb  RxHead,u
         beq   readslp
         leax  RxBuf,u
         abx
         lda   ,x
         incb
         andb  #RxMask
         stb   RxTail,u
         puls  cc
         clrb
         rts
readslp  lda   V.BUSY,u           the process to wake
         sta   V.WAKE,u
* the interrupts stay masked up to the call: a character cannot slip in between the check and the sleep
         ldx   #0                 until the ISR sends the wake-up signal
         os9   F$Sleep
         ldx   >D.Proc
         ldb   P$Signal,x         a signal for us (^C ^E, or a kill)?
         beq   readst
         cmpb  #S$Intrpt
         bhi   readst
         puls  cc                 the signal code is the error, like with the other SCF drivers
         coma
         rts
readst   ldb   P$State,x
         bitb  #Condem
         beq   readlp
         puls  cc
         comb
         ldb   #E$PrcAbt
         rts

**********************
* WRITE
* Entry: U = Static storage
*        Y = Path Descriptor
*        A = Char to write
*
*   Write the byte directly when the transmitter is idle, else into the transmit ring
*
WRITE    pshs  cc,a
wrlp     orcc  #IntMasks
         ldb   TxHead,u
         cmpb  TxTail,u
         bne   wrring             characters are waiting
         ldx   V.PORT,u
         lda   ,x
         bita  #St.TxE
         beq   wrring
         lda   1,s
         sta   1,x                nothing waits and the transmitter is free: send
         puls  cc,a
         clrb
         rts
wrring   incb
         andb  #TxMask
         cmpb  TxTail,u           full?
         beq   wrslp
         ldb   TxHead,u
         leax  TxBuf,u
         lda   1,s
         sta   b,x
         incb
         andb  #TxMask
         stb   TxHead,u
         lda   CtlShd,u           enable the transmit interrupt
         ora   #Cr.TIE
         sta   CtlShd,u
         ldx   V.PORT,u
         sta   ,x
         puls  cc,a
         clrb
         rts
wrslp    lda   V.BUSY,u           wait for the ISR to empty the ring
         sta   V.WAKE,u
         lda   CtlShd,u
         ora   #Cr.TIE
         sta   CtlShd,u
         ldx   V.PORT,u
         sta   ,x
         ldx   #0
         os9   F$Sleep
         ldx   >D.Proc
         ldb   P$Signal,x
         beq   wrst
         cmpb  #S$Intrpt
         bhi   wrst
         puls  cc,a
         coma
         rts
wrst     ldb   P$State,x
         bitb  #Condem
         beq   wrlp
         puls  cc,a
         comb
         ldb   #E$PrcAbt
         rts

**********************
* GETSTA
*   U = Static Storage
*   Y = Path Descriptor
*
GETSTA   ldx   PD.Rgs,y           X=pointer to registers
         lda   R$B,x              A=contents of B reg.
         cmpa  #SS.Ready          check for ready code
         bne   gsta1              if not, check next
         ldb   RxHead,u           the number of characters in the ring
         subb  RxTail,u
         andb  #RxMask
         beq   notrdy
         stb   R$B,x
ok       clrb
         rts
notrdy   ldb   #E$NotRdy
         coma                     sets the carry flag
         rts
gsta1    cmpa  #SS.EOF            check for eof code
         beq   ok                 which always returns ok
         comb                     otherwise return
         ldb   #E$UnkSVC          unknown code error
         rts

**********************
* PUTSTA
*  Always return error
*
PUTSTA   ldx   PD.Rgs,y           X=register stack
         ldb   R$B,x              B=contents of reg B
         comb                     otherwise return error.
         ldb   #E$UnkSVC
         rts

**********************
* TERM
*   Terminate Driver: let the transmit ring run empty (at most a second), then no more interrupts
*
TERM     ldx   #60                ticks to wait
term1    pshs  x
         lda   TxHead,u
         cmpa  TxTail,u
         puls  x
         beq   term2
         pshs  x
         ldx   #1
         os9   F$Sleep
         puls  x
         leax  -1,x
         bne   term1
term2    orcc  #IntMasks
         ldx   V.PORT,u
         lda   #Cr.Base           no interrupts
         sta   ,x
         clr   CtlShd,u
         ldd   V.PORT,u
         ldx   #0                 remove the entry of the ISR
         leay  IRQSvc,pcr
         os9   F$IRQ
         andcc #^IntMasks
         clrb
         rts

**********************
* IRQSvc - the interrupt service routine
*   Entry: U = Static storage.  Exit: carry clear (the interrupt was ours)
*
*   Loop while the ACIA has something: a received character goes through the special character checks into
*   the ring, a transmit interrupt sends the next character of the transmit ring (the last one turns the
*   transmit interrupt off).  At the end a process that waits for us is woken.
*
IRQSvc   ldx   V.PORT,u
isrlp    ldb   ,x
         bitb  #St.RxF
         beq   isrtx
         lda   1,x                the character (this also clears the interrupt request)
         beq   isrput             a null is a normal character
         cmpa  V.INTR,u
         bne   isrq
         ldb   #S$Intrpt          keyboard interrupt
         bra   isrsig
isrq     cmpa  V.QUIT,u
         bne   isrp
         ldb   #S$Abort           keyboard abort
isrsig   pshs  a
         lda   V.LPRC,u           the last process that used the terminal
         beq   isrsg1
         os9   F$Send
isrsg1   puls  a
         bra   isrput
isrp     cmpa  V.PCHR,u
         bne   isrput
         ldx   V.DEV2,u           pause output of the device that echoes
         beq   isrput
         sta   V.PAUS,x
isrput   ldb   RxHead,u
         leax  RxBuf,u
         abx                      B is unsigned: B,X would sign extend it
         sta   ,x
         incb
         andb  #RxMask
         cmpb  RxTail,u           a full ring loses the character
         beq   isrnxt
         stb   RxHead,u
isrnxt   ldx   V.PORT,u
         bra   isrlp
isrtx    bitb  #St.TxE
         beq   isrend
         lda   CtlShd,u
         bita  #Cr.TIE
         beq   isrend
         ldb   TxTail,u
         cmpb  TxHead,u
         beq   isrtxe
         leay  TxBuf,u
         lda   b,y
         sta   1,x
         incb
         andb  #TxMask
         stb   TxTail,u
         bra   isrlp
isrtxe   anda  #^Cr.TIE           the ring is empty: no more transmit interrupts
         sta   CtlShd,u
         sta   ,x
isrend   lda   V.WAKE,u           a process waits for a character or for room in the ring?
         beq   isrret
         clr   V.WAKE,u
         ldb   #S$Wake
         os9   F$Send
isrret   clrb
         rts

         emod
PEND     equ *
