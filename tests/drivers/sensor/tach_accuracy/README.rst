.. _tach_accuracy:

Tachometer Accuracy Test
#########################

Overview
********

This sample provides comprehensive testing of tachometer drivers by:

- Generating precise GPIO pulses to simulate fan tachometer signals
- Testing multiple RPM levels (1000-8000 RPM)
- Validating driver accuracy with ±3% tolerance checking
- Providing statistical analysis of measurements

The sample uses a button GPIO to cycle through different fan speeds and 
a pulse generator GPIO to create test pulses that are fed into the tachometer input.

Requirements
************

This sample requires:

- A board with tachometer sensor support
- At least 3 available GPIOs:
  
  - 1 GPIO configured as button input (``sw0`` alias)
  - 1 GPIO configured as pulse generator output (``tachsim`` alias)
  - 1 GPIO configured as tachometer input (``tach0`` device)

- Hardware jumper or wire to connect pulse generator GPIO to tachometer input GPIO

Devicetree Requirements
***********************

The sample requires these devicetree nodes:

**Required Aliases:**

.. code-block:: devicetree

   / {
       aliases {
           sw0 = &button0;       /* Button for test control */
           tachsim = &pulse_gen; /* Pulse generator GPIO */
       };
   };

**Required Tachometer Device:**

.. code-block:: devicetree

   &tach0 {
       status = "okay";
       pulses-per-round = <2>;  /* Adjust for your hardware */
   };

Building and Running
********************

This sample can be built for any board with tachometer support.

.. zephyr-app-commands::
   :zephyr-app: tests/drivers/sensor/tach_accuracy
   :board: <your_board>
   :goals: build flash
   :compact:

**Board-Specific Overlays:**

See the ``boards/`` directory for board-specific configurations.

For RTS5912 EVB:

.. code-block:: console

   west build -b rts5912_evb tests/drivers/sensor/tach_accuracy
   west flash

For custom boards, create an overlay in ``boards/<your_board>.overlay`` or use:

.. code-block:: console

   west build -b <your_board> tests/drivers/sensor/tach_accuracy -- -DDTC_OVERLAY_FILE=app.overlay

Sample Output
*************

.. code-block:: console

   *** Booting Zephyr OS build v4.x.x ***

   ================================================
          Tachometer Accuracy Test Sample        
   ================================================

   [OK] Tachometer device ready (tach0)
        Pulses per revolution: 2
   [OK] Button configured
   [OK] Pulse generator configured
   [OK] Pulse timer initialized

   === Hardware Setup ===
   Connect pulse generator GPIO to tachometer input GPIO
   (See README for board-specific wiring details)
   
   === Instructions ===
   1. Press button to START at 3000 RPM
   2. Press again to cycle through test speeds:
      - 1000 RPM
      - 2000 RPM
      - 3000 RPM
      - 4000 RPM
      - 5000 RPM
      - 6000 RPM
      - 8000 RPM
   3. Readings displayed every second
   4. Statistics shown after full cycle

   Waiting for button press to begin...

   >> Starting simulation: 3000 RPM (period: 5000 us)
   Tachometer monitoring thread started
   [PASS] RPM: 2998 (target: 3000, error: 2/0.0%)
   [PASS] RPM: 3001 (target: 3000, error: 1/0.0%)

   >> Changed to: 4000 RPM (period: 3750 us)
   [PASS] RPM: 3997 (target: 4000, error: 3/0.0%)

   === Test Statistics ===
   Total samples: 204
   Accurate samples: 109 (53%)
   Max error: 891 RPM
   RPM range: 959 - 7894
   ======================

Features
********

- **Comprehensive Testing**: Validates pulse counting and RPM calculation
- **Multi-Speed Testing**: 7 RPM levels covering full operational range (1000-8000 RPM)
- **Accuracy Validation**: ±3% tolerance checking (scales with RPM)
- **Statistical Analysis**: Tracks accuracy percentage, max error, and range
- **Interactive Control**: Button-driven test progression
- **Hardware Loopback**: Pulse generator to tach input for isolated testing
- **Integer Math**: Percentage calculations without FPU
- **Portable**: Works on any board with tachometer and GPIO support

Testing Procedure
*****************

1. **Hardware Setup**:
   
   - Connect pulse generator GPIO to tachometer input GPIO (wire or jumper)
   - Ensure button GPIO is properly configured
   - Verify no other peripherals conflict with chosen GPIOs

2. **Run Test**:
   
   - Press button to start simulation at 3000 RPM
   - Press repeatedly to cycle through all 7 RPM levels
   - Observe PASS/FAIL validation and error percentages

3. **Review Results**:
   
   - After cycling through all speeds, statistics are displayed
   - Check accuracy percentage, max error, and RPM range

RPM Tolerance
*************

The sample validates each reading against the target RPM with a tolerance of **±3%**.
This percentage-based tolerance scales appropriately with RPM:

.. code-block:: none

   RPM Level | Tolerance Range    | Max Allowed Error
   ----------|--------------------|-----------------
   1000 RPM  | 970 - 1030 RPM     | ±30 RPM
   2000 RPM  | 1940 - 2060 RPM    | ±60 RPM
   3000 RPM  | 2910 - 3090 RPM    | ±90 RPM
   4000 RPM  | 3880 - 4120 RPM    | ±120 RPM
   5000 RPM  | 4850 - 5150 RPM    | ±150 RPM
   6000 RPM  | 5820 - 6180 RPM    | ±180 RPM
   8000 RPM  | 7760 - 8240 RPM    | ±240 RPM

To adjust tolerance, modify ``RPM_TOLERANCE_PERCENT`` in ``src/main.c``.

Board-Specific Setup
********************

RTS5912 EVB
===========

**Hardware Connections:**

J178 TACHO Header:

.. code-block:: none

   Pin 1: GND
   Pin 2: GPIO053 (Pulse Generator)  ← Test signal output
   Pin 3: (Not used)
   Pin 4: GPIO052 (TACH0 Input)      ← Tachometer input

**Jumper Configuration:**

1. **J178 pins 2-4**: Connect GPIO053 → GPIO052 for pulse loopback
2. **SW3 Button (GPIO002)**:
   
   - J199: Jumper pins 1-1
   - J198: Jumper pins 1-1
   - J49: Jumper pins 1-2

**Build Command:**

.. code-block:: console

   west build -b rts5912_evb tests/drivers/sensor/tach_accuracy

**Test Results (RTS5912 EVB):**

.. code-block:: console

   Total samples: 204
   Accurate samples: 109 (53%)
   Max error: 891 RPM
   RPM range: 959 - 7894

Porting to Your Board
**********************

To add support for a new board:

1. **Create overlay** in ``boards/<your_board>.overlay``:

.. code-block:: devicetree

   / {
       aliases {
           sw0 = &button0;
           tachsim = &pulse_gen;
       };
       
       gpio_keys {
           compatible = "gpio-keys";
           button0: button_0 {
               gpios = <&gpio_controller PIN (GPIO_PULL_UP | GPIO_ACTIVE_LOW)>;
               label = "Test Button";
           };
       };
       
       gpio_leds {
           compatible = "gpio-leds";
           pulse_gen: pulse_gen_0 {
               gpios = <&gpio_controller PIN GPIO_ACTIVE_HIGH>;
               label = "Pulse Gen";
           };
       };
   };
   
   &tach0 {
       status = "okay";
       pulses-per-round = <2>;  /* Adjust for your tachometer */
   };

2. **Optional board-specific config** in ``boards/<your_board>.conf``:

.. code-block:: kconfig

   # Enable board-specific tachometer driver
   CONFIG_TACH_<YOUR_DRIVER>=y

3. **Update sample.yaml** to allow your board:

.. code-block:: yaml

   platform_allow:
     - rts5912_evb
     - your_board

4. **Document hardware setup** in this README under "Board-Specific Setup"

Troubleshooting
***************

**No tachometer readings (RPM always 0):**
  - Verify pulse generator GPIO is connected to tachometer input GPIO
  - Check that required GPIOs are not used by other peripherals
  - Ensure tachometer driver is enabled in ``prj.conf``
  - Verify devicetree configuration is correct

**Inaccurate readings:**
  - Ensure stable power supply
  - Check for electrical noise on GPIO lines
  - Verify pulses-per-round matches your tachometer (typically 1 or 2)
  - Confirm connection is secure
  - Check GPIO drive strength settings

**Button not responding:**
  - Verify button GPIO configuration in devicetree
  - Check that button GPIO is not used by other peripherals
  - Ensure button polarity (active-low/high) matches hardware
  - Test button independently with GPIO test sample

**Build errors:**
  - Ensure devicetree has all required aliases (sw0, tachsim, tach0)
  - Check that tachometer driver is enabled
  - Verify GPIO controllers exist for your board
  - Clean build: ``west build -p``

**Porting to new board:**
  - Review existing board overlays in ``boards/`` for examples
  - Ensure your board has a tachometer driver
  - Map appropriate GPIOs that don't conflict with other peripherals
  - Test GPIO loopback before running full tachometer test

References
**********

- Zephyr Sensor API: :ref:`sensor_api`
- Zephyr GPIO API: :ref:`gpio_api`
- Zephyr Devicetree Guide: :ref:`dt-guide`
