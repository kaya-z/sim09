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
*   2 26.11.07 Asynchronous disk port ($E049): the command returns at once and the CPU waits
*   for the result with the interrupts on (the clock
*   and the console go on).  A process does not sleep here: with two or more processes at
*   the disks the Level 1 IOMan/RBF lost
*   a wake-up (tests/os9_test.py), so the Level 1 driver waits itself.  A port without the
*   asynchronous mode is used as before.

Revision equ 2
NumDrvs  set 2 Number of drives

         org Drvbeg
         rmb NumDrvs*DrvMem
RAMSTA   equ .

         mod RAMEND,RAMNAM,Drivr+Objct,Reent+Revision,RAMENT,RAMSTA
         fcb   $FF         mode byte

RAMNAM   fcs /Pdisk/

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
INITXIT  rts

*   SETUPDT: the registers of the port.  In: B = LSN msb, X = LSN lsw, Y = path descriptor,
*   U = static storage.  Out: X = the port
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

*   DOIO: do the command B ($81 read, $55 write) of the port X.  Out: B = the result (0 =
*   good), Z set when good.
*   The port is asynchronous (E049 = 1): the command returns at once, E040 reads $FE until
*   it is finished.  The CPU waits with the interrupts on.
*   A port without the asynchronous mode (or with a host file system, sim09 -v) has the
*   result at once.
DOIO     pshs  x,cc
         lda   #$01           asynchronous, no interrupt of the port
         sta   9,x
         stb   ,x             the command
         andcc #^IntMasks     the interrupts on while the disk works
DOIO3    ldb   ,x             the result, or $FE: not finished
         cmpb  #$FE
         beq   DOIO3
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
         lbsr DOIO         perform io
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
         lbsr DOIO         perform io
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
TERM     clrb
         rts

         emod
RAMEND   equ *

