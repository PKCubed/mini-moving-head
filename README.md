# Mini Moving Head

This project aims to create a miniature moving head light fixture for my miniature stage. This fixture needs to have motorized pan and tilt, and should have a bright LED with a collimating lens to create as tight of a beam as possible. For now, a single color LED is acceptable due to the size constraints, as a multicolor LED with individual separate LED dies in side the package causes individual colored beams preventing color mixing.

This project consists of a custom circuit board with an RP2040 on board to take in unbalanced "DMX" signals from the "DMX" bus and read channel data for it's pan and tilt position and the brightness of it's LED. The circuit board drives two 9 gram servos with PWM to give pan and tilt and a constant current driver with PWM control for the LED. We'll need to make sure that the PWM is fast enough that it's not going to cause artifacts on cameras under any circumstances.

<img width="668" height="761" alt="image" src="https://github.com/user-attachments/assets/61f8f8bc-4c50-4cd1-bae8-50372e11f92a" />
