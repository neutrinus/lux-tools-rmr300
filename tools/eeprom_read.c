/*
 * EEPROM read stub for GD32F305 (U13), I2C1 @ 0x40005800.
 *
 * Mirrors the firmware's own I2C read routine (FUN_0x08053930):
 *   - device address byte = 0xD0 (what firmware actually uses)
 *   - DO NOT reset/reconfigure the peripheral. The running firmware has
 *     already set CTL1(FREQ), CKCFG, RT and CTL0.PE. We only drive
 *     START/ADDR/STOP on the live, configured peripheral.
 *   - wait BUSY(STAT1 bit1) == 0 before START
 *   - GD32 status bits == STM32F1 layout:
 *       STAT0 (0x14): bit0 SBSEND, bit1 ADDSEND, bit2 BTC, bit6 RBNE, bit10 AERR(NACK)
 *       STAT1 (0x18): bit1 I2CBSY
 *   - clearing ADDSEND = read STAT0 then STAT1
 *
 * Result buffer at 0x20001000:
 *   byte 0..N-1 = EEPROM data
 * Status word at 0x20001F00:
 *   0x600DDA7A = success
 *   0xDEAD00xx = failure at stage xx
 */
#define I2C1_BASE 0x40005800u
#define CTL0  (*(volatile unsigned*)(I2C1_BASE + 0x00))
#define CTL1  (*(volatile unsigned*)(I2C1_BASE + 0x04))
#define DR    (*(volatile unsigned*)(I2C1_BASE + 0x10))
#define STAT0 (*(volatile unsigned*)(I2C1_BASE + 0x14))
#define STAT1 (*(volatile unsigned*)(I2C1_BASE + 0x18))
#define CKCFG (*(volatile unsigned*)(I2C1_BASE + 0x1C))
#define RT    (*(volatile unsigned*)(I2C1_BASE + 0x20))

#define DEV_ADDR 0xD0u          /* device control byte used by firmware */
#define N        256            /* bytes to read */

#define BUF  ((volatile unsigned char*)0x20001000u)
#define STAT (*(volatile unsigned*)0x20001F00u)

/* spin with a bounded timeout so we never hang the watchdog window */
#define WAIT(cond, stage) do {                          \
        unsigned long _to = 2000000UL;                  \
        while (!(cond)) {                               \
            if (STAT0 & 0x0400u) { STAT = 0xDEAD0000u | 0x80u | (stage); goto stop; } \
            if (--_to == 0)      { STAT = 0xDEAD0000u | (stage); goto stop; }         \
        }                                               \
    } while (0)

void __attribute__((noreturn, section(".start"))) _start(void)
{
    /* Hard-disable FPU + lazy stacking from within our own context so no
     * exception entry ever touches FPDSCR (MPU would fault on it). */
    *(volatile unsigned*)0xE000ED88u = 0;        /* CPACR: deny CP10/CP11 */
    *(volatile unsigned*)0xE000EDF0u = 0;        /* FPCCR: ASPEN/LSPEN = 0 */
    *(volatile unsigned*)0xE000ED94u = 0;        /* MPU_CTRL: disable MPU */
    __asm__ volatile("isb");
    /* clear CONTROL.FPCA so no FP context is considered active */
    __asm__ volatile("mrs r0, control\n\t"
                     "bic r0, r0, #4\n\t"
                     "msr control, r0\n\t"
                     "isb" ::: "r0");

    STAT = 0;

    /* The firmware may have been halted mid-transaction, leaving the
     * peripheral BUSY with stale error flags (AERR/BERR). Software-reset
     * the I2C peripheral to recover a clean state, then re-enable.
     * IMPORTANT: SWRST clears CTL1(FREQ), CKCFG and RT — we must save and
     * restore all of them, otherwise the I2C clock will not generate.
     * SWRST = CTL0 bit15 (0x8000); PE = bit0 (0x01); ACKEN = bit10 (0x400). */
    {
        volatile int d;
        unsigned cfg_ctl1 = CTL1;        /* FREQ */
        unsigned cfg_ckc  = CKCFG;       /* clock config (fast/std, CCR) */
        unsigned cfg_rt   = RT;          /* rise time */
        CTL0 = 0x8000u;                  /* assert SWRST (also clears PE) */
        for (d = 0; d < 1000; d++) ;
        CTL0 = 0x0000u;                  /* deassert SWRST */
        for (d = 0; d < 1000; d++) ;
        CTL1  = cfg_ctl1;                /* restore FREQ first */
        CTL0  = 0x0001u;                 /* PE = 1 (enable) */
        CKCFG = cfg_ckc;                 /* restore clock config (PE must be 1) */
        RT    = cfg_rt;                  /* restore rise time */
        CTL0 |= 0x0400u;                 /* ACKEN */
        for (d = 0; d < 1000; d++) ;
    }

    /* wait bus free */
    {
        unsigned long _to = 2000000UL;
        while ((STAT1 & 0x0002u)) { if (--_to == 0) { STAT = 0xDEAD0001u; goto stop; } }
    }

    /* --- write phase: set word address pointer --- */
    CTL0 |= 0x0100u;                 /* START */
    WAIT(STAT0 & 0x0001u, 2);        /* SBSEND */
    DR = DEV_ADDR;                   /* write addr */
    WAIT(STAT0 & 0x0002u, 3);        /* ADDSEND */
    (void)STAT0; (void)STAT1;        /* clear ADDSEND */
    WAIT(STAT0 & 0x0080u, 4);        /* TBE */
    DR = *(volatile unsigned*)0x20001F04u; /* word address */
    WAIT(STAT0 & 0x0084u, 5);        /* BTC */

    /* --- repeated START into receive mode --- */
    CTL0 |= 0x0400u;                 /* ACKEN */
    CTL0 |= 0x0100u;                 /* START */
    WAIT(STAT0 & 0x0001u, 6);        /* SBSEND */
    DR = DEV_ADDR | 1u;              /* read addr */
    WAIT(STAT0 & 0x0002u, 7);        /* ADDSEND */
    (void)STAT0; (void)STAT1;        /* clear ADDSEND */

    /* Canonical N-byte master receive (N >= 3):
     * - keep ACK on; read bytes until 3 remain
     * - when N-3..: standard EV7 reads
     * - last 3 bytes: special BTC handling to avoid extra clocking */
    {
        int n = N;
        int i = 0;
        while (n > 3) {
            WAIT(STAT0 & 0x0040u, 8);        /* RBNE (EV7) */
            BUF[i++] = (unsigned char)(DR & 0xFFu);
            n--;
            *(volatile unsigned*)0x20001F08u = (unsigned)i;
        }
        /* now exactly 3 bytes remain: data N-3, N-2, N-1 */
        WAIT(STAT0 & 0x0004u, 9);            /* wait BTC (N-3 & N-2 in regs) */
        CTL0 &= ~0x0400u;                    /* clear ACK */
        BUF[i++] = (unsigned char)(DR & 0xFFu);  /* read N-3 */
        CTL0 |= 0x0200u;                     /* STOP */
        BUF[i++] = (unsigned char)(DR & 0xFFu);  /* read N-2 */
        WAIT(STAT0 & 0x0040u, 10);           /* RBNE for last */
        BUF[i++] = (unsigned char)(DR & 0xFFu);  /* read N-1 */
        *(volatile unsigned*)0x20001F08u = (unsigned)i;
    }

    STAT = 0x600DDA7Au;
    CTL0 |= 0x0400u;
    while (1) __asm__("wfi");

stop:
    CTL0 |= 0x0200u;                 /* STOP to release bus */
    CTL0 |= 0x0400u;                 /* re-enable ACK */
    while (1) __asm__("wfi");
}
