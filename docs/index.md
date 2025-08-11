# Background
While I love my 2022 Model X, I always felt a pang of jealousy when looking at luxury SUVs (X7, GLS, etc.) that featured fancy RGB ambient lighting. While there is a kit online that you can buy and install to add ambient lighting to the X, I decided to take things into my own hands and take the opportunity to learn how to hack into the vehicles CAN bus, reverse engineer Aliexpress RGB ambient lighting strips, and design my first PCB.

# Reverse Engineering
The first step of this project was to reverse engineer the ambient lighting strips I bought off of Aliexpress, along with deciphering the Tesla CAN bus signals to find the data packets that were needed for this project.

## LED Strips
The ambient lighting strips bought off of Aliexpress use WB2815-2020 LEDs that are individually addressable. The 3 pin JST-XH connector wires are GND, DATA, 5V. For some reason, on the strips I bought, the ground uses a **yellow** wire while the data line uses a **black** wire, which is super confusing. I also blew up two strips thinking that they used 12V.

**Bonus:** I also blew up *another* LED strip when I inadvertently turned on my bench top power supply when it was set to 10V and still connected to the breadboard.

**As a quick overview of how the WB2815 LEDs operate:**
- An array of 24-bit packets are sent to the strip by the controller
  - Packet contains 3 bytes, one for each color, in GRB order
  - The individual bit values are distinguished by the time the data line spends HIGH and LOW (refer to the WS2815 datasheet specific timing numbers) 
- Each individual LED reads the first packet in line, and shows the corresponding RGB value
- The remaining packets are passed along to the next LED in line
- Process repeats until packets run out or until the last LED in line

![Data transmission method and 24-bit packet composition from WS2815 datasheet](assets/Tesla%20Ambient%20Lighting-1747267523833.png)

## CAN Bus
This part ended being much more complicated than I had originally managed. The Model X has several different CAN busses so it took some trial and error before I connected my logic analyzer (shout out Saleae) to the right one.
- As a side note, the Model X can bus uses a 500kbps bit rate

![20 pin diagnostic port pinout](assets/Tesla%20Ambient%20Lighting-1747695016590.png)

**Update:** The CAN bus containing all of the information required is the chassis CAN bus on pins 13 and 14 of the diagnostic CAN port

Since there are so many messages being sent over the CAN bus, it is quite difficult to isolate a specific message corresponding to an action. A few different approaches were tested:
- Shoutout to this [Reddit post](https://www.reddit.com/r/HowToHack/s/TRcK2GmOsw) for inspiring most of these methods, albeit my execution being much more primitive
- Also **huge** shoutout to this [online post](https://www.teslaownersonline.com/threads/diagnostic-port-and-data-access.7502/) that contains a bunch of links and information on how to decode the CAN bus, known codes, etc.

### Attempt 1 - Unique IDs
1) Use the logic analyzer (w/ CAN bus decoding) to get "baseline" CAN bus messages, and save messages to a CSV file
2) Use logic analyzer again, but while recording data, perform action (open door, flash headlights, etc.) and save the messages to a CSV file
3) Use a Python script to parse the messages from both CSV files (each row represents one section of a message, so to recreate a message, several rows need to be parsed)
4) Filter out any identifiers from the second batch of messages that are also present in the baseline messages
  - [*] Other filtering methods can be used as well, this one only works if the identifier corresponding with the action is not sent repeatedly (such as status updates)
5) Print the remaining messages, removing any duplicate messages  

### Attempt 2 - Unique Messages
After getting some potential identifier IDs, I then use an Arduino UNO with a MCP2515 CAN transceiver to find the exact message I am looking for. A table of discovered CAN messages (ID and Data in HEX) is below:

| Description                                | ID  | Data             |
| ------------------------------------------ | --- | ---------------- |
| Main screen on (*just kidding maybe not?*) | 551 | 0 31 1 1 0 1 0 1 |
| Right side windows                         | 518 |                  |

### Attempt 3 - Give Up
After much wasted time trying to figure out my own IDs, I stumbled across this absolute gem of a [spreadsheet](https://docs.google.com/spreadsheets/d/1ijvNE4lU9Xoruvcg5AhUNLKr7xYyHcxa8YSkTxAERUw/edit?gid=150828462#gid=150828462) containing a database of known IDs. The ones I wrote down are below:
- `0x3F5` - Light status (ambient lighting, turn signals, headlights, etc.)
  - Second data byte is display brightness
    - The ambient lighting brightness is linked to the display brightness
    - If the ambient lighting is off, then this byte is equal to `0x00`
    - **Note:** The code currently doesn't adjust the brightness of the lights based off of this value, it just checks whether it equals zero
  - **Turn Signals** (didn't end up using)
    - First data byte
    - Left TS Off: `0b0000 0001`
    - Left TS On: `0b0000 0010`
    - Right TS Off: `0b0000 0100`
    - Right TS On: `0b0000 1000`
    - Hazard Off: `0b0001 0101`
    - Hazard On: `0b0001 1010`
    - If no turn signal cycle is ongoing, then byte equals `0x00`
- `0x3B3` - UI Vehicle Status
  - Third data byte contains data regarding the display
  - Bit number 2 (isolated using `0x04` bit mask) is set if the display is in what I call "normal" mode, which is basically the normal UI when the car is on. Otherwise, if the bit is not set, the display could be off, on the sentry mode screen, the charging screen, etc.
    - This bit decides whether to play the start-up animation for the lights

# ESP32 Controller
For this project, I decided on an ESP32-WROOM-32E chip running FreeRTOS in order to get experience using FreeRTOS and so that multiple tasks (CAN bus, LED control, WIFI server, etc.) could run "concurrently."
## Controlling the LED strips
To control the LED strips, the `espressif/led_strip` library was used. As currently configured, this library uses the **Remote Control Transceiver (RMT)** peripheral on the ESP32 to send the signals on the data line. The RMT peripheral was originally intended for infrared transmissions, but it is leveraged by the `led_strip` library to send the timing-critical pulses required to communicate with the WS2815 LEDs. 

**Note:** The documentation for the LED strip library recommends using a chip with DMA (direct memory access) in order to prevent context switches from interfering with the timing of the signals. The ESP-WROOM chip I used does not have DMA functionality, but I did not run into any issues since the LED strips used are pretty short.

![Logic analyzer reading for 0xFF0000 RGB (Transmission order is GRB)](assets/Tesla%20Ambient%20Lighting-1747267092819.png)

**Example Code for Blinking LED strip w/ RGB Gradient**
```c
// required imports go here

// LED strip common configuration
led_strip_config_t strip_config = 
{
    .strip_gpio_num = BLINK_GPIO,  // The GPIO that connected to the LED strip's data line
    .max_leds = 110,                 // The number of LEDs in the strip,
    .led_model = LED_MODEL_WS2812, // LED strip model, it determines the bit timing
    .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB, // The color component format is G-R-B
    .flags = {
        .invert_out = false, // don't invert the output signal
    }
};

// RMT backend specific configuration
led_strip_rmt_config_t rmt_config = 
{
    .clk_src = RMT_CLK_SRC_DEFAULT,    // different clock source can lead to different power consumption
    .resolution_hz = 10 * 1000 * 1000, // RMT counter clock frequency: 10MHz
    .mem_block_symbols = 64,           // the memory size of each RMT channel, in words (4 bytes)
    .flags = {
        .with_dma = false, // DMA feature is available on chips like ESP32-S3/P4
    }
};

void app_main(void)
{

    /// Create the LED strip object
    led_strip_handle_t led_strip;
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip);

    while (1)
    {
        uint8_t red = 255;
        uint8_t green = 0;
        uint8_t blue = 0;

        for (int i = 0 ; i < strip_config.max_leds ; i++) {
            led_strip_set_pixel(led_strip, i, red, green, blue);

            if (red > 0 && blue == 0) {
                red -= 15;
                green += 15;
            } else if (green > 0) {
                green -= 15;
                blue += 15;
            } else if (blue > 0) {
                blue -= 15;
                red += 15;
            }
        }

        led_strip_refresh(led_strip);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        led_strip_clear(led_strip);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
```
## CAN Bus Integration
The ESP32 has a peripheral called the **Two-Wire Automotive Interface (TWAI)** compatible with ISO11898-1 Classical frames. It supports both the Standard Frame Format (11-bit ID) and Extended Frame Format (29-bit ID), both of which are present on the Model X.
- **Note:** The TWAI interface on the ESP32 still requires a CAN bus transceiver chip to convert the differential signal to a logic level signal that can be read by the ESP32 (I used the SN65HVD230)

**Example code used for sniffing CAN bus with ESP32**
```c
// required imports
static const char* TAG = "can_sniffer";

// TWAI configuration
twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(GPIO_NUM_21, GPIO_NUM_22, TWAI_MODE_NORMAL);
twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
twai_filter_config_t f_config;

uint8_t data[16];

void app_main(void)
{
  // Configure TWAI acceptance mask
  // As configured, only accepts messages from 0x7F5.
  // Check ESP32 TWAI driver documentation for more information on how to set these values 
  f_config.acceptance_mask = 0x1FFFFF;
  f_config.acceptance_code = 0x7EA00000;
  f_config.single_filter = true;

  // Install TWAI driver
  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
    ESP_LOGI(TAG, "Driver installed");
  } else {
    ESP_LOGI(TAG, "Failed to install driver");
    return;
  }

  // Start TWAI driver
  if (twai_start() == ESP_OK) {
    ESP_LOGI(TAG, "Driver started");
  } else {
    ESP_LOGI(TAG, "Failed to start driver");
    return;
  }

  while (1)
  {
    // Wait for the message to be received
    twai_message_t message;
    esp_err_t receive_status = twai_receive(&message, pdMS_TO_TICKS(1000));

    if (receive_status == ESP_ERR_TIMEOUT) {
      ESP_LOGI(TAG, "Timed out waiting for message");
      continue;
    } else if (receive_status != ESP_OK) {
      ESP_LOGE(TAG, "Error receiving message");
      return;
    }
  
    // Compare new message data with saved message data
    if (memcmp(data, message.data, message.data_length_code) != 0) {
      // Process received message
      if (message.extd) {
        ESP_LOGI(TAG, "Message is in Extended Format");
      } else {
        ESP_LOGI(TAG, "Message is in Standard Format");
      }

      ESP_LOGI(TAG, "ID is 0x%03X", (unsigned int) message.identifier);
      if (!(message.rtr)) {
        // Copy new message data
        memcpy(data, message.data, message.data_length_code);
        
        // Format data into single string
        char data_string[128];
        for (int i = 0 ; i < message.data_length_code ; i++) {
          char buffer[16];
          sprintf(buffer, "%02X ", data[i]);
          strcat(data_string, buffer);
        }
        ESP_LOGI(TAG, "Data: %s", data_string);

        // Clear data string for next iteration
        memset(data_string, 0, 128);
      }

    }

  }
}
```

## WiFi
The ESP32 is first set up in AP (access point) mode in order to host its own WiFi network that devices can connect to. After initializing in AP mode, an HTTP server with two endpoints (GET and POST) is started. The GET endpoint returns a webpage with an RGB input and submit button, while the POST request takes the RGB data passed in the body and sends it to the light controller task to change the color. `more commands to be added here`

# Code
Each LED strip is controlled using a `ambient_light_t` struct, containing members for the `led_strip` library configuration. When an `ambient_light_t` is initialized using the `init_ambient_light` method, a new task is started dedicated to controlling that light, and a FreeRTOS queue is created for commands to be sent to the LED strip. The process for controlling an `ambient_light_t` is as follows:
1) Add a `command_t*` to the command queue
    - The command contains information on what the command is, along with any other data required for the command (`rgb_t` for `COMMAND_FADE_TO`, etc.)
    - **Note:** It is the responsibility of the one adding the command to the queue to properly allocate memory for the `command_t*`. Once the command has been executed, the light controller will deallocate the memory used by the command pointer and will set the pointer to `NULL` as well. This also means that each command must be allocated memory before being sent to the light controller queue, and commands cannot be reused.
2) When a command is available on the command queue, the task dedicated to controlling the ambient light will call the appropriate `led_strip` functions to control the LED strip specified in the `ambient_light_t` struct.
    - An array of `ambient_light_t` handles is shared in `main_common.h` so that any class is able to send commands to the LED strips, assuming that `ambient_light_t` has been initialized correctly. This is utilized for the startup animation where the dashboard LEDs, after completion of their animation, start the sequential animation for the door LEDs, among other things. For simplicity, a common `rgb_t` is also shared so that the HTTP server only has to update one color to ensure all lights are the same color.

# Schematic and Custom PCB
![Schematic](assets/Tesla%20Ambient%20Lighting-1751386897733.png)
![PCB Layout](assets/Tesla%20Ambient%20Lighting-1751386955666.png)

This was the first time I had ever designed a schematic and custom PCB for one of my projects, so the PCB design and layout is certainly far from perfect. I ordered the components for the PCB off of Digikey, and the PCB + solder mask off of JLCPCB. It was around $40 for the components and maybe another $20 for the PCB/solder mask.

Here's the PCB fresh out of the packaging  
![PCB Fresh](assets/Tesla%20Ambient%20Lighting-1752691864625.jpg)![PCB Fresh 2](assets/Tesla%20Ambient%20Lighting-1752691907320.jpg)

Here's the PCB after putting all the components on  
![PCB Assembled](assets/Tesla%20Ambient%20Lighting-1752691993665.jpg)

Seeing as this was my first time putting together a PCB, there were some issues. First was that, for some reason. the RC circuit that adds a delay to the enable pin on the ESP32 was not going above 700mv, so I just ripped the resistor and capacitor off and connected EN to VCC on the ESP. 
- *I don't know why this was happening, and it is recommended by Espressif to have this circuit to add ~10ms delay to the EN pin, but it works fine without it so* ¯\\\_(ツ)_/¯
![RC Circuit Issue](assets/Tesla%20Ambient%20Lighting-1752692135813.jpg)

Also, when I was soldering the ESP32, I heat up the chip enough, so there ended up being some gaps on the pads which caused one of the lights to not work.  
![Soldering Issue](assets/Tesla%20Ambient%20Lighting-1752692150080.jpg)

After trying again:
![Soldering Fixed](assets/Tesla%20Ambient%20Lighting-1752692164201.jpg)
Still not gorgeous, but functional.

## Hindsight is 20/20
Looking back on it, there were some other things I would have liked to implements.
1) A reset button that pulls the EN pin on the ESP32 low so that the USB cable or power source doesn't have to be disconnected every time
2) Utilizing the DTR and RTS pins on the FT231XS USB-UART chip so that the ESP can be automatically restarted and put into bootloader mode when uploading firmware

I am also looking to add Bluetooth as an alternative to the ESP constantly hosting a WIFI AP.
# Installation
## Wiring
Using the schematics available from Tesla, I decided to use the Autopilot ECU connectors in order to tap into power and the chassis CAN bus. Each connector in the car is individually identifiable using an ID assigned by Tesla, and the two connectors used were X120 (for 12V) and X126 (for chassis CAN bus). 

I decided against using the 12V pins on connector X126 since they're labeled as 5A, and I didn't want to push the current limit of those wires only for a fuse or something to blow up. It is possible that the 12V 20A pin on the X120 connector is used for some higher power applications, meaning that less power is actually available for me to use, but I felt safer with the bigger number. A 12v to 5v converter off of Amazon was then used to get power to the PCB.

![Wiring Diagram](assets/Pasted%20image%2020250603084900.png)
![Connector X120](assets/Pasted%20image%2020250603085106.png)
![Connector X126](assets/Pasted%20image%2020250603085405.png)
![Connector X126 Closeup](assets/Pasted%20image%2020250603085425.png)

Originally, I had hoped to purchase these connectors online to create a harness that could be used to tap into the wires I needed without any modifications to the existing wires. However, after some failed attempts on Alibaba to purchase these connectors, I gave up and decided just to tap into the existing wires.

![Tapped Wires 1](assets/Pasted%20image%2020250603120236.jpg|400)
![Tapped Wires 2](assets/Pasted%20image%2020250603120257.jpg|400)
![Tapped Wires 3](assets/Pasted%20image%2020250603120310.jpg|400)
![Tapped Wires 4](assets/Pasted%20image%2020250603120348.jpg|400)
![Tapped Wires 5](assets/Pasted%20image%2020250603120359.jpg|400)
![Tapped Wires 6](assets/Pasted%20image%2020250603120410.jpg|400)
![Tapped Wires 7](assets/Pasted%20image%2020250603120428.jpg)

One quick side note regarding the LED strips themselves. They come from the manufacturer with the wires coming straight out of the strips, which leads to a lot of wasted room since the wire cannot bend to a right angle without taking up a significant length. Because of this, for the dashboard lights, I unsoldered the wires, and resoldered them to be right angle; I also took this opportunity to solder the black wire to ground and the yellow to data (*as it should have been in the first place*).
![Dashboard LED Strip](assets/Tesla%20Ambient%20Lighting-1750134025975.jpg)
## Dashboard Lights
Starting with the dashboard lights, I had to remove the main display in order to get the dashboard lights as close to the center as possible. It is possible to install them without removing the display, but I couldn't live with myself if I left an inch of unlit space before the main display. The diagnostic panel right above the wireless charger also needs to be removed in order to access the two bolts holding the display on.

After removing the display, it was pretty easy to snake the wires down to the center console where the controller will end up. After installing the lights, the display was reinstalled using the two bolts to the tilt mount.

![Display Removal 1](assets/Tesla%20Ambient%20Lighting-1750134097123.jpg)
![Display Removal 2](assets/Tesla%20Ambient%20Lighting-1750134048209.jpg)
![Display Removal 3](assets/Tesla%20Ambient%20Lighting-1750134205108.jpg)

## Door Lights
This was the *really* frustrating part. Having to take off the door panel and snake wires through impossible spaces just to try and keep everything looking nice is a huge PITA, but in the end, my conscience is able to rest easy knowing I did my best to do things right. Especially since having the windows catch the wires on the way up/down and ripping them apart would be a buzzkill.

To start, I removed the door panel following Tesla's instructions in the Model X service manual.
![Door Panel Removal 1](assets/Tesla%20Ambient%20Lighting-1750134249186.jpg)
![Door Panel Removal 2](assets/Tesla%20Ambient%20Lighting-1750134255542.jpg)

**Note:** There are extra 3-pin connectors that are not plugged into anything in both door panels, and also in the center console and dashboard. I believe these are there from earlier attempts at adding ambient lighting by Tesla, and I spent a couple of hours seeing if I could hijack the wires for communication with my light controller, but in the end I decided it wasn't worth risking damage to the ECUs or other components.

Since the wires for the lights have to run through the rubber snake that connects the door to the body, the wires first have to go inside of the door and make their way to the entry for the rubber snake. This also involves making sure that the new wire is kept close to the existing wire harness so that it does not remain loose inside of the door, since that could lead to the window catching the wire when rolling up or down.

The only way to get any level of useful access to the inside of the door is removing the speaker. The speaker is held in with four screws, but also has these stupid plastic tabs that *will* break when you try to take the speaker off.

![Speaker Removal](assets/Tesla%20Ambient%20Lighting-1750134517914.jpg)
![Inside Door](assets/Tesla%20Ambient%20Lighting-1750134827998.jpg)

From there, the wire for the new light was snaked from the main door panel connector through a small hole in the door to enter the inside of the door. Following that was a **lot** of nasty language as I tried to zip tie the new wire to the existing wire harness inside of the door to keep it from flopping around. This is made extra difficult since there is no way to see what your hands are doing, you just have to feel around to get the zip tie properly sinched down.

**Tip:** The Tesla service manual states to roll the window down when removing the door panel, which I did do. However, I then temporarily plugged the door panel back in to roll the window up so that I had more room inside of the door. Just make sure to leave a window open on the other side, especially if you disconnect LV power since that could possibly lead to you being locked out of the car.

Once the wire is coming out of the door, I used a short vinyl tube to snake through the rubber snake (since it provides enough stiffness to push through the tight gap, while also being flexible to navigate the turns). Once I got it to come out the other end, I tied the wire to one end and pulled the wire through using the vinyl tube.

![Snaking Wire](assets/Tesla%20Ambient%20Lighting-1750134908032.jpg)

After that, the wiring for the door lighting was run under the floor carpeting to the center console area where the light controller will be housed.

![Under Carpet 1](assets/Tesla%20Ambient%20Lighting-1750134945630.jpg)
![Under Carpet 2](assets/Tesla%20Ambient%20Lighting-1750134952253.jpg)

As for the light itself, the wires were first passed through a hole in the door panel where the wooden trim was removed. The wires were then tied to the existing wiring just to keep it tidy. After the door panel was reinstalled and the interior wooden trim was put back on, the light was pushed into the gap.

**Note:** I used 3 pin JST-XH connectors between the door panel and the door so that, in the future, the door panel can be removed fully just by disconnecting the existing connectors along with the 3 pin connector for the ambient lighting.

![Door Light Installed](assets/Tesla%20Ambient%20Lighting-1750135095715.jpg)

Repeat that for the other side and the door lights are finished!

# Finished Product
Overall, I'm pretty satisfied with the final product. I would have liked to add another two LED strips in the center console, but the gap between the wood trim was too tight for me to squeeze the lights in, so I decided just to forgo them for now. In addition, the existing ambient lighting only shines a constant white, and it is possible to replace those lights with RGB ones and link them into the controller. At the end of the day, I don't really notice the color difference and didn't think the extra work was worth it.

![Finished Product 1](assets/Tesla%20Ambient%20Lighting-1752692399398.jpg)
![Finished Product 2](assets/Tesla%20Ambient%20Lighting-1752692404711.jpg)

The RGB values used for the picture was `0x646464` (around half brightness pure white). This ended up being a little too bright for my taste, and I have since moved to `0x503C14` for a dim warm white color that is much easier on the eyes.