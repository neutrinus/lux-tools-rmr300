/* I2C 16-byte read via repeated single-byte random reads (proven pattern).
 * Buffer at 0x2000FF00 (high SRAM, away from firmware string buffers).
 * Status at 0x2000FFF0. */
#define I2C 0x40005800u
#define CTL0  (*(volatile unsigned*)(I2C+0x00))
#define CTL1  (*(volatile unsigned*)(I2C+0x04))
#define DR    (*(volatile unsigned*)(I2C+0x10))
#define STAT0 (*(volatile unsigned*)(I2C+0x14))
#define STAT1 (*(volatile unsigned*)(I2C+0x18))
#define CKCFG (*(volatile unsigned*)(I2C+0x1C))
#define RT    (*(volatile unsigned*)(I2C+0x20))

#define BUF  ((volatile unsigned char*)0x2000FF00u)
#define STAT (*(volatile unsigned*)0x2000FFF0u)
#define PROG (*(volatile unsigned*)0x2000FFF4u)
#define ARGS ((volatile unsigned*)0x2000FFF8u)   /* [0]=start addr, [1]=count */
#define DEV 0xD0u

void __attribute__((noreturn, section(".start"))) _start(void)
{
    *(volatile unsigned*)0xE000ED88u = 0;
    *(volatile unsigned*)0xE000EDF0u = 0;
    *(volatile unsigned*)0xE000ED94u = 0;
    __asm__ volatile("isb");

    STAT = 0; PROG = 0;
    unsigned start = ARGS[0];
    unsigned count = ARGS[1];
    for (unsigned k = 0; k < count; k++) BUF[k] = 0;

    /* SWRST recover + restore clock regs */
    { volatile int d; unsigned c1=CTL1, ck=CKCFG, rt=RT;
      CTL0=0x8000u; for(d=0;d<2000;d++); CTL0=0; for(d=0;d<2000;d++);
      CTL1=c1; CTL0=0x0001u; CKCFG=ck; RT=rt; CTL0|=0x0400u; for(d=0;d<2000;d++); }

    { unsigned long t=20000UL; while((STAT1&0x0002u)&&--t); }

    for (unsigned i = 0; i < count; i++) {
        unsigned long tt;
        unsigned wa = (start + i) & 0xFFu;
        /* write word address */
        CTL0 |= 0x0100u;                      /* START */
        { tt=20000UL; while(!(STAT0&0x0001u)&&--tt); if(!tt){STAT=0x100u|i;goto stop;} }
        DR = DEV;
        { tt=20000UL; while(!(STAT0&0x0002u)){ if(STAT0&0x400u){STAT=0x200u|i;goto stop;} if(!--tt){STAT=0x210u|i;goto stop;} } }
        (void)STAT0; (void)STAT1;
        { tt=20000UL; while(!(STAT0&0x0080u)&&--tt); if(!tt){STAT=0x300u|i;goto stop;} }
        DR = wa;                              /* word address */
        { tt=20000UL; while(!(STAT0&0x0004u)&&--tt); if(!tt){STAT=0x400u|i;goto stop;} }

        /* repeated START + read 1 byte with NACK+STOP */
        CTL0 |= 0x0100u;                      /* START */
        { tt=20000UL; while(!(STAT0&0x0001u)&&--tt); if(!tt){STAT=0x500u|i;goto stop;} }
        DR = DEV | 1u;
        { tt=20000UL; while(!(STAT0&0x0002u)){ if(STAT0&0x400u){STAT=0x600u|i;goto stop;} if(!--tt){STAT=0x610u|i;goto stop;} } }
        CTL0 &= ~0x0400u;                     /* NACK after ADDSEND */
        (void)STAT0; (void)STAT1;             /* clear ADDSEND */
        CTL0 |= 0x0200u;                      /* STOP */
        { tt=20000UL; while(!(STAT0&0x0040u)&&--tt); if(!tt){STAT=0x700u|i;goto stop;} }
        BUF[i] = (unsigned char)(DR & 0xFFu);

        { tt=20000UL; while((STAT1&0x0002u)&&--tt); }
        CTL0 |= 0x0400u;
        PROG = i + 1u;
    }
    STAT = 0x600DDA7Au;
stop:
    CTL0 |= 0x0200u;
    CTL0 |= 0x0400u;
    while (1) __asm__("wfi");
}
