#define I2C1_BASE 0x40005800u
#define CR1   (*(volatile unsigned*)(I2C1_BASE + 0x00))
#define CR2   (*(volatile unsigned*)(I2C1_BASE + 0x04))
#define DR    (*(volatile unsigned*)(I2C1_BASE + 0x10))
#define SR1   (*(volatile unsigned*)(I2C1_BASE + 0x14))
#define SR2   (*(volatile unsigned*)(I2C1_BASE + 0x18))

#define EEPROM_ADDR 0xA0

#define BUF ((volatile unsigned char*)0x20001000)

void __attribute__((noreturn, section(".start"))) _start(void)
{
    CR1 = 0;
    CR1 = 0x0401;
    CR1 |= 0x1000;
    while (!(SR1 & 0x01));
    DR = EEPROM_ADDR;
    while (!(SR1 & 0x02));
    if (SR1 & 0x100) goto fail;
    (void)SR2;
    while (!(SR1 & 0x40));
    DR = 0x00;
    while (!(SR1 & 0x44));
    if (SR1 & 0x100) goto fail;

    CR1 |= 0x1000;
    while (!(SR1 & 0x01));
    DR = EEPROM_ADDR | 1;
    while (!(SR1 & 0x02));
    if (SR1 & 0x100) goto fail;
    (void)SR2;

    for (int i = 0; i < 256; i++) {
        while (!(SR1 & 0x20));
        if (i == 255) {
            CR1 = (CR1 & ~0x0400) | 0x0800;
        }
        BUF[i] = DR & 0xFF;
    }

    CR1 = 0x0401;
    while (1) __asm__("wfi");

fail:
    ((volatile unsigned*)BUF)[0] = 0xDEADBEEF;
    while (1) __asm__("wfi");
}
