To compile and run the program:

1. Place the 'source-code' folder into contiki-NG/examples

2. For the receiver sensor tag, compile the code by executing following command in the : sudo make TARGET=cc26x0-cc13x0 BOARD=sensortag/cc2650 PORT=/dev/ttyACM0 nbr_receiver

3. Flash the respective .bin file to the sensortags using UniFlash.

4. For the sender sensor tag, compile the code by executing following command in the : sudo make TARGET=cc26x0-cc13x0 BOARD=sensortag/cc2650 PORT=/dev/ttyACM0 nbr_sender 

5. Flash the respective .bin file to the sensortags using UniFlash.

6. Observe output of program.

7. As an enhancement to the program, we added a functionality whereby a receiver will request for light readings at an interval of 15seconds so long as they are in proximity with a sender.

