*********************************************
* Rdisk
*  A driver for a Ram disk!
* Modified for os9l1 emulator by Soren Roug 2001
*

         nam Pdisk
         ttl A Device Driver for a RAM Disk

         ifp1
         use defsfile
         endc

***********************
* Edition History

*  #   date    Comments
* -- -------- ----------------------------------------------
*   2 26.11.07 Asynchronous disk port: the command returns at once, the process sleeps and the
*   interrupt at the end of the command
*   wakes it (the CPU goes on with the other processes), a board without the asynchronous mode
*   is used as before.

Revision equ 2
NumDrvs  set 2 Number of drives

         org Drvbeg
         rmb NumDrvs*DrvMem
IRQOK    rmb 1            1: the interrupt of the disk port is entered (F$IRQ), the commands are asynchronous
RAMSTA   equ .

         mod RAMEND,RAMNAM,Drivr+Objct,Reent+Revision,RAMENT,RAMSTA
         fcb   $FF         mode byte

RAMNAM   fcs /PDisk/

RAMENT   lbra INIT
         lbra READ
         lbra WRITE
         lbra GETSTA
         lbra PUTSTA
         lbra TERM

*****************************
* INIT
*  Set up the v09 disk

INIT     ldb #NumDrvs Set no drives to 2
* Setup drive tables
         stb   V.NDRV,u     save it
         lda   #$FF
         leax  DRVBEG,u     point to drive table start
L0111    sta   DD.TOT+1,x
         sta   <V.TRAK,x
         leax  <DRVMEM,x
         decb  
         bne   L0111
         clrb
*   the interrupt at the end of an asynchronous command: the status register of the port
*   ($E049) is polled for bit 7 (done)
         clr   IRQOK,u
         pshs  cc
         orcc  #IntMasks
         ldd   V.PORT,u
         addd  #9
         leax  IRQPckt,pcr
         leay  IRQSvc,pcr
         os9   F$IRQ
         bcs   INIT9        no interrupt: the disk stays synchronous
         lda   #1
         sta   IRQOK,u
INIT9    puls  cc           (the carry of F$IRQ is not the result of INIT)
         clrb
INITXIT  rts

*   what F$IRQ needs: flip, mask and priority.  The interrupt is requested when (status xor
*   flip) and mask is not 0.
IRQPckt  fcb   $00,$80,$08

*   SETUPDT: the registers of the port.  In: B = LSN msb, X = LSN lsw, Y = path descriptor, U
*   = static storage.  Out: X = the port
SETUPDT  pshs  x
         lda   <PD.DRV,y      Get the drive number
         ldx   V.PORT,u
         sta   1,x    drive number
         stb   2,x    msb of lsn
         ldd   ,s++
         sta   3,x
         stb   4,x
         ldd   PD.BUF,y
         sta   5,x     buffer address
         stb   6,x
         rts

*   DOIO: do the command B ($81 read, $55 write) of the port X.  U = static storage.  Out: B =
*   the result (0 = good), Z set when good.
*   Asynchronous: the process sleeps until the interrupt at the end of the command wakes it
*   (V.WAKE, like the terminal driver), a
*   port that has no asynchronous mode (or no interrupt) has finished the command when it
*   returns: the result is there at once.
DOIO     pshs  x,cc
         lda   #$01           asynchronous, the interrupt off while we look (the interrupts stay on: the clock)
         sta   9,x
         stb   ,x             the command
         lda   #80            a read takes less than 1 ms: look for it a while before sleeping
DOIOP    ldb   ,x             (the sleep and the wake-up cost more), a write waits for the card for ms
         cmpb  #$FE
         bne   DOIO4
         deca
         bne   DOIOP
DOIO3    orcc  #IntMasks      still busy: sleep until the interrupt at the end of the command
         lda   V.BUSY,u
         sta   V.WAKE,u       the process to wake
         lda   #$81           the interrupt at the end
         sta   9,x
         ldb   ,x             finished meanwhile? (this takes the done flag)
         cmpb  #$FE
         bne   DOIO4
         ldx   #5             the interrupt wakes us at once, if it never comes, look again after 5 ticks
         os9   F$Sleep
         ldx   1,s
         bra   DOIO3
DOIO4    clr   V.WAKE,u
         lda   #$01           the interrupt off
         sta   9,x
         puls  cc,x         the interrupt mask as it was
         tstb
         rts

*****************************
* READ
*  read a sector from disk
*  Entry: U = Static Storage
*         Y = Path Descriptor
*         B = MSB of LSN
*         X = LSB's of LSN
*  Exit: 256 byte sector in PD.BUF buffer
*
READ     pshs u,x,b         S: LSN msb(0) LSN lsw(1) static storage(3)
         lbsr SETUPDT
         ldb  #$81
         bsr  DOIO         perform io
         beq  rd1
         orcc #1            error: carry set, B = code
         bra  rdx
* An RBF driver has to copy the first DD.SIZ bytes of LSN 0 into the drive
* table: RBF takes DD.BIT, DD.MAP, DD.DIR ... from there.  Without this the table
* stays empty and RBF loops forever when it computes allocation sizes (every
* operation that creates or extends a file).
rd1      tst  ,s            LSN 0?
         bne  rdok
         ldx  1,s
         bne  rdok
         ldu  3,s           our static storage
         lda  <PD.DRV,y
         ldb  #DRVMEM
         mul
         leax DRVBEG,u
         leax d,x           drive table of this drive
         ldu  PD.BUF,y      the sector just read
         ldb  #DD.SIZ
rdcp     lda  ,u+
         sta  ,x+
         decb
         bne  rdcp
rdok     clrb
rdx      leas 1,s           drop saved B, keep the result in B
         puls x,u,pc

*****************************
* WRITE
*  Write a sector to disk
* Entry: U = Static Storage
*        Y = Path Descriptor
*        B = MSB of LSN
*        X = LSB's of LSN
*        PD.Buf = Sector to write
*
WRITE    pshs u,x          the caller gets X and U back
         lbsr SETUPDT
         ldb  #$55
         bsr  DOIO         perform io
         beq  WRIT99
         orcc #1            error: carry set, B = code
         puls x,u,pc
WRIT99   clrb
         puls x,u,pc

**************************
* GETSTA
*  get device status
*
GETSTA
Unknown  comb
         ldb  #E$UnkSvc    unknown code: error, carry set
         rts

**************************
* PUTSTA
*  Set device Status
*
PUTSTA   
PUTSTA90 clrb
         rts

*****************************
* TERM
*  terminate Driver
*
TERM     pshs cc
         orcc #IntMasks
         ldd  V.PORT,u
         addd #9
         ldx  #0            remove the entry of the interrupt service routine
         leay IRQSvc,pcr
         os9  F$IRQ
         puls cc
         clrb
         rts

**********************
* IRQSvc - the interrupt service routine (entered with F$IRQ, U = static storage)
*   The command at the port has finished: switch the interrupt off (the result is read by the
*   process, which takes the done flag) and wake the
*   process.  Carry set: not ours (the status says done, but the interrupt is off: the process
*   is on its way to read the result).
IRQSvc   ldx   V.PORT,u
         lda   9,x
         anda  #$C0         done and interrupt enabled?
         cmpa  #$C0
         bne   IRQNO
         lda   #$01         asynchronous, interrupt off
         sta   9,x
         lda   V.WAKE,u
         beq   IRQOUT
         clr   V.WAKE,u
         ldb   #S$Wake
         os9   F$Send
IRQOUT   clrb
         rts
IRQNO    comb
         rts

         emod
RAMEND   equ *

