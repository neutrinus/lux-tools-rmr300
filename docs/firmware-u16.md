# Original firmware: U16 (GD32F303, boundary-wire board MCU)

U16 digitises the two boundary-wire coils, classifies the signal, reads the two lift sensors and reports to U13 as JSON. It has no link to the display board and no motor, motor-driver or power code. U13 calls it the "border board" (BB, `bb_sv` 50003 on our unit) and can reflash it from a USB stick (`SNK_BB_*.bin`, [usb.md](usb.md)).

Markers: **[F]** read from the firmware, **[I]** inferred, not verified.

## Dump

[`dumps/u16/u16_flash.bin`](../dumps/u16/) (256 KB). It is byte-identical to the U16 dump of the MI 302 ([dumps/mi302](../dumps/mi302/README.md)), so the addresses of [salonnikov/mower-stock-reverse](https://github.com/salonnikov/mower-stock-reverse) (`reverse-v2/chip2/`) apply unchanged.

## Layout [F]

- Bootloader `0x08000000`–`0x0800ffff`, reset `0x080001b5`.
- App vector table at `0x08010000`, SP `0x2000bff8`, reset `0x0801a5f1`; flash used up to `0x08040000`.
- FreeRTOS (tasks `comm_task`, `init_task`, `init_bd`), EasyLogger 2.2.99, cJSON.
- Sources: `process_comm.c`, `process_deal_board.c`, `rw_bdboard_init.c`, `driver_bdsensor.c`, `driver_mboard_port_snk_v2.c`.
- IEC 60730 class B self-test at start-up and at run time (CPU, RAM, clock, PC, ADC, **flash CRC32**). Patching anything means redoing the CRC.

```
rw_bdboard_init()
├── init_all_driver()
│   ├── timer driver          (08019340)
│   ├── board sensor driver   (08019240)  coil ADCs + DMA
│   ├── port driver           (08019300)  UART to U13
│   ├── lift driver           (080192c8)  hall sensors
│   └── factory driver        (08019288)  test hooks [I]
├── bdsensor_init(0x17)
├── comm_task_init(0x14)
└── create_board_task() → create_comm_task()
```

`comm_task` (`08013680`) waits on its queue, runs the coil status cycle (`08014ac8`), the lift state machine (`08013970`) and the message dispatcher (`08019914`), then sleeps 35 ticks.

## Link to U13 [F]

- `mport`: USART2 `0x40004800` ↔ U13 `bdport` (USART1).
- JSON frames of at most 128 bytes (`08019a18`: "send string length=%d too long").
- U16 sends `border_message` (coil readings), `version_message`, `seach_border_message` and `follow_border_message`, each when its flag in the sensor struct is set.
- U13 sends commands (start search, follow border, reset, …) and `{"cmd":32772,"rtc":…}`. None of them changes the detection thresholds.

## Boundary-wire receiver [F]

- Two coils on **ADC0 channel 5** and **ADC1 channel 9**, dual mode with DMA (`08019bf4`), sample time 7. Windows of 800 (fine) or 235 (coarse) samples.
- **Base voltage** (`08012b6c`, check `0801a1f8`): the receiver's DC offset, the average of several conversions. Valid 1906–2191; otherwise "base voltage=%d error, set default 2048".
- Detection (`08015fa8` → `0801baf8`): a sample counts when `|x| > 2500` (5000 in "strong signal" mode, latched after more than 20 saturated windows). Fewer than 5 counted samples in a window = no signal; more than 600 = interference; otherwise a valid wave. Zero crossings (`08015dcc`) and the area/direction integrator (`08015260`) give `area` (0–3) and `str` per coil.
- Coil status (`08014ac8`): 0 none, 1 in range, 2 over range, 3 over limit.

## Lift sensors [F]

Two hall sensors, state machine in `08013970`: 0 none, 1 both lifted, 2 left lifted, 3 right lifted. 10 consecutive samples confirm a change.
