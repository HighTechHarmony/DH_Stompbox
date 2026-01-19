#include "test.h"
#include "input.h"
#include "audio.h"
#include "display.h"

void hardwareTestMode()
{
    // Start continuous 1kHz tone at 0.5 amplitude
    myEffect.frequency(1000);
    myEffect.amplitude(0.5);
    myEffect2.amplitude(0);
    myEffect3.amplitude(0);

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("HARDWARE TEST MODE");
    display.display();
    delay(1000);

    while (true) // infinite loop - only exit is reset
    {
        // Read inputs
        bool encButton = !digitalRead(ENC_BTN);
        bool fs1 = !digitalRead(FOOT1);
        bool fs2 = !digitalRead(FOOT2);
        int potRaw = analogRead(POT_PIN);

        // Read peak input level for bargraph
        float peakL = 0.0f;
        if (peak1.available())
        {
            peakL = peak1.read(); // Returns 0.0-1.0 range
        }

        // Display diagnostics
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);

        int y = 0;

        // Input level bargraph
        display.setCursor(0, y);
        display.print("Input:");
        int barWidth = (int)(peakL * 80.0f);
        display.drawRect(40, y, 82, 8, SSD1306_WHITE);
        if (barWidth > 0)
        {
            display.fillRect(41, y + 1, barWidth, 6, SSD1306_WHITE);
        }
        y += 10;

        // Audio shield status
        display.setCursor(0, y);
        display.print("AudioShield: ");
        display.println(audioShieldEnabled ? "ENABLED" : "DISABLED");
        y += 10;

        // Encoder and button
        display.setCursor(0, y);
        display.print("Enc: ");
        display.print(encoderPosition);
        display.print(" Btn: ");
        display.println(encButton ? "ON" : "off");
        y += 10;

        // Footswitches
        display.setCursor(0, y);
        display.print("FS1: ");
        display.print(fs1 ? "ON" : "off");
        display.print("  FS2: ");
        display.println(fs2 ? "ON" : "off");
        y += 10;

        // Pot value
        display.setCursor(0, y);
        display.print("Pot raw: ");
        display.println(potRaw);

        display.display();

        // Serial output for debugging
        Serial.print(" Enc:");
        Serial.print(encoderPosition);
        Serial.print(" Btn:");
        Serial.print(encButton ? "ON" : "off");
        Serial.print(" FS1:");
        Serial.print(fs1 ? "ON" : "off");
        Serial.print(" FS2:");
        Serial.print(fs2 ? "ON" : "off");
        Serial.print(" Pot:");
        Serial.println(potRaw);

        delay(50);
    }
}
