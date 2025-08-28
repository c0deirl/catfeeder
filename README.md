# Cat Feeder
This project is for an ESP32 Controlled Cat Feeder.  

## Description
After browsing the web for hours looking for an affordible WIFI schedulable cat feeder, I was running out of ideas. I did not want to spend $100+ on a simple cat food dispenser, that may or may not fit my needs, or the needs of my cats.  

  In the end, i decided to come up with a custom solution for my cats. I had a "schedulable" cat feeder from before. This one was plagued with issues, did not have wifi, and would not let me know if the food hopper was empty (the feeder is in my basemet).  

  The solution, was to develop a custom control using an ESP32. This implementation modifies the existing cat feeder with my own controller. but retaining the existing motor and housing.  

  The ESP is wired to an L298N motor controller, that sends 5v DC to the existing rotation motor, that dispenses the food. The physical design of this feeder is simple, it has a hopper that holds food, and has a slot that allows food to fall into a segmented wheel, that once spun, allows the food to drop into the eating tray.  
  The food itself is metered by how long the wheel spins, if it only makes a small rotation, there is only a small amount of food dispensed. 

  I also wanted a way to let me know if the food hopper is running low. I added an IR LED, and an adjustible photocell. The LED shines across the food hopper, and is not recieved by the photocell if there is food present. Once the food drops below a certain amount, the LED is picked up by the photocell, and an email notification is sent to myself and my wife. 
