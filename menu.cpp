// Menu - Angelo Z. (2025)

/*
  A menu on a oled display 128x64.

  Menu[cursor] item[cursor]
    0
      ____________                ____________
    1|  rectY 0   |     Up      5|  rectY 0   |
    2|            |     Down    6|            |
    3|            |             7|            |
    4|__rectY 4___|             8|__rectY 4___|

      cursor > row 4 then
      scroll++ = Menu[scroll, items]

      Fixed. In setValue() replaced printNumI with print function
    
      I use ClickEncoder library only for the rotary encoder and
      disabled the button due the timing response in menu
      
      For the button
      
            buttonPressed 
            buttonaReleased
            buttonHold
            buttonClicked

    Vedere gestione dei labels
                
*/
#define FirstRun 0  // Prepare eeprom
#include <LiquidCrystal_I2C.h>
#include <ClickEncoder.h>
#include <TimerOne.h>
#include <OLED_I2C.h>

#include "eeprom.h"

#define LIST(x) (sizeof(x) / sizeof(x[0]))


// ----------------------------------------------------------------------------
// Connected bus channels
#define __SCREEN__I2C  0 // 0x3C
#define __EEPROM__I2C  1 // 0x50
#define __LCD__I2C     2 // 0x3C

// ----------------------------------------------------------------------------
// Graphics
//
#define MENU_H 16
#define FONT_H 8
#define FONT_W 6
#define CENTER_TEXT (MENU_H - FONT_H) / 2
#define SCR_WIDTH 128
#define SCR_HEIGHT 64
#define MARGIN_L 5
#define MARGIN_R SCR_WIDTH
#define BUTTON_HOLDTIME 2000
#define SCROLL_DELAY 10

// ----------------------------------------------------------------------------
// Encoder 
#define PushButtonPin   PINB0
#define StepsPerNotch   4
ClickEncoder *encoder;
int buttonPressed_i = 0;
boolean rotary_accel = false;
boolean rotary_off = false;
boolean stopPressEvent = false;

// ----------------------------------------------------------------------------
// Menu Cursor
//
int screenEnd = 4;
int startY = 0;
int scroll = 0;
int cursor = 0;
int rectY = 0;
int _cr = 0, _ry = 0, _sl = 0;

// ----------------------------------------------------------------------------
// Item Scroll
//
#define BUFSIZE (int) (SCR_WIDTH / FONT_W) - 1
char rect_buf[BUFSIZE+1];
int head = 0, tail = 0;
int direction = 0;
int offset = BUFSIZE;

// ----------------------------------------------------------------------------
#define MAX_ITEMS 5
#define PiezoPin 12
boolean redraw = true;
boolean loopMenu = false;
boolean speakerOn = false;
boolean inMenu = true;

LiquidCrystal_I2C lcd = LiquidCrystal_I2C(0x27, 16, 2);

OLED display(SDA, SCL);
extern uint8_t SmallFont[];

// ----------------------------------------------------------------------------
// Items flags
//
typedef enum {
    Item         =  1, 
    Button       =  2,
    Label        =  4,
    OnOff        =  8,
    YesNo        = 16,
    Hide         = 32,
    Protected    = 64,
    Scrolling    = 128
} ItemAttributes;

typedef struct {
    const char *text;
    long val,
         min,
         max;
    void (*action)(void); // callback function
    ItemAttributes properties;
} MenuItem;

typedef struct {
    const char *text;
    MenuItem *item;
    size_t items;
} Menu;

// ----------------------------------------------------------------------------
// List of items of an opened menu
MenuItem *itemsList[MAX_ITEMS];
int indexMenu = 0;
int arrayLen = 0;

// Items identifier 
#define LOOP  0
#define SOUND 1

//----------------------------------------------------------------------------
MenuItem hardware_configuration[] = {
    { "CLOCK", 8000, 8000, 160000000, _lcd, Item | Scrolling },
    { "<-", 0, 0, 0, leaveMenu, Button }
};
/*
MenuItem software_configuration[] = {
    { "MENU LOOP", 0, 0, 1, option, Item | YesNo },
    { "KEY TONE", 0, 0, 1, option, Item | YesNo },
    { "<-", 0, 0, 0, leaveMenu, Button }
};
*/
MenuItem upload_to_eeprom = {
    "", 0, 0, 0, storeData, Button
};

MenuItem noMenu = {
    "", 0, 0, 0, exitMain, Button
};

Menu menus[] = {
    { "SETTINGS", hardware_configuration, 2 }, // Items
    //{ "DISPLAY", software_configuration, 3 }, // Items
    { "SAVE", &upload_to_eeprom, 1 }, // Button
    { "EXIT", &noMenu, 1 } // Button
};
// ----------------------------------------------------------------------------

void (* Reset_AVR)(void) = 0;

void checkMe() {
    //loopMenu = software_configuration[LOOP].val;
    //speakerOn = software_configuration[SOUND].val;
}

#define N_DIGITS 9
#define OK 0x7e
#define LEAVE 0x7f

/*
=====================
 void push
=====================
*/
// Decomposizione di un numero in cifre
inline void push(long number, int8_t frame[]) {
    for (int i = N_DIGITS - 1; i >= 0 && number > 0; i--) {
        frame[i] = number % 10;
        number /= 10;
    }
}


/*
=====================
 long pop
=====================
*/
// Composizione di cifre in un numero intero
inline long pop(int8_t frame[]) {
    long n = 0;

    for (int i = 0; i < N_DIGITS; i++)
        n = n * 10 + frame[i];
    return n;
}


/*
=====================
 void overwriteItem
=====================
*/
void overwriteItem() {
    drawRect();
    display.invertText(true);
    display.print("SET YOUR DESTINY 23", MARGIN_L, MENU_H * rectY + CENTER_TEXT);
    display.update();
}


/*
=====================
 void menuIdle
=====================
*/
void menuIdle(boolean yesno) {
    if (yesno) {
        overwriteItem();
        chipSelect(__LCD__I2C);
        lcd.noAutoscroll();
        lcd.backlight();
        lcd.blink();
    } else {
        lcd.clear();
        lcd.autoscroll();
        lcd.noBacklight();
        chipSelect(__SCREEN__I2C);
        closeItem();
    }

    rotary_off = stopPressEvent = yesno;
}


/*
=====================
 void underscoreCursor
=====================
*/
void underscoreCursor(boolean yesno) {
    if (yesno) {
        lcd.noBlink();
        lcd.cursor();
    } else {
        lcd.blink();
        lcd.noCursor();
    }
}


/*
=====================
 void lcd
=====================
*/
// Su display lcd
void _lcd() {
    MenuItem *mi = itemsList[ cursor ];
    // Mappa di elementi per lo scorrimento cursore
    int8_t digits[11] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, OK, LEAVE };
    // Mappa grafica per la localizzazione del cursore
    uint8_t coord[11][2] = {
        // Cifre
        {0,0},{1,0},{2,0},{4,0},{5,0},{6,0},{8,0},{9,0},{10,0},
        // Conferma
        {0,1},
        // Esci
        {15,1}
    };
    static boolean confirm = false;
    static boolean createLayout = true;
    static boolean updown = false;
    static int8_t x = 0;
  
    // Blocco encoder e button in window. Item rimane invariato.
    menuIdle(true);
    // Spezzo il numero in cifre 
    push( mi->val, digits );   
    
    // Creo layout solo una volta
    if (createLayout) {
        byte bell[] = { 
            B00100, 
            B01110, 
            B01110, 
            B01110,
            B11111,
            B00000,
            B00100,
            B00000
        };
        lcd.createChar(0, bell);
        lcd.home();
  
        for ( int i = 0; i < N_DIGITS; i++ ) {
            lcd.print( digits[i] );
            if ( (i + 1 ) % 3 == 0 && i != N_DIGITS - 1)
                lcd.print(" ");
        }
        
        lcd.setCursor(0, 1);
        lcd.write(0);
        lcd.setCursor(15, 1);
        lcd.print(">");

        createLayout = false;
    }

    // Encoder
    if (!confirm) {   
        int16_t delta = encoder->getValue();
    
        if ( delta != 0 && !buttonReleased() ) {
            // Per cifre 
            if (updown) {
                digits[x] += delta;
                if (digits[x] < 0) digits[x] = 9;
                else if (digits[x] > 9)
                    digits[x] = 0;
                lcd.print( digits[x] );
                // Salvo valore nel item selezionato
                mi->val = pop( digits );
            } else {
                // Per spostamento cursore nella mappa
                int8_t mapsize = LIST( digits );
                x += delta;
                if ( x < 0 )
                    x = mapsize;
                else if ( x > mapsize ) 
                    x = 0;                
            }
        }

        lcd.setCursor(   coord[x][0], coord[x][1]   );
        underscoreCursor(updown);    
    }

    // Pulsante
    if ( buttonClicked() ) {
        if ( digits[x] == OK ) {
            confirm = !confirm;
        } else if ( digits[x] == LEAVE ) {
            createLayout = true;
            confirm = false;
            x = 0;

            menuIdle(false);
        } else {
            updown = !updown;
        }
    }

    if (confirm) {  } 

    // updateButton in window
}


/*
=====================
 void powerOn
=====================
*/
void powerOn() {
    int duration = 150;
    // Play intro
    tone(PiezoPin, 1046, duration);
    delay(duration + 50);
    tone(PiezoPin, 784, duration);
}


/*
=====================
 void keyTone
=====================
*/
void keyPressTone() {
    
    if (speakerOn) { tone(PiezoPin, 3218, 10); }

    direction = 0;
    offset = BUFSIZE;
    head = 0;
    tail = 0;
}


/*
=====================
 void chipSelect
=====================
*/
void chipSelect(byte bus) {
    if (bus > 7) return;
    Wire.beginTransmission(0x70);
    Wire.write(1 << bus);
    Wire.endTransmission();
    delay(10);
}


/*
=====================
 void storeData
=====================
*/
void storeData() {
    int addr = 0;

    chipSelect(__EEPROM__I2C);

    for (int i = 0; i < LIST(menus); i++) {
        for (int k = 0; k < menus[i].items; k++) {
            // Mi interressa solo il valore di val. Min/Max sono
            // constanti.
            addr += eeWrite(addr, menus[i].item[k].val);
            // Importante!!!
            delay(7);
        }
    }

    chipSelect(__SCREEN__I2C);

   lampOfGod();
  //Reset_AVR();
}


/*
=====================
 void loadData
=====================
*/
void loadData() {
    int addr = 0;

    chipSelect(__EEPROM__I2C);

    for (int i = 0; i < LIST(menus); i++) {
        for (int k = 0; k < menus[i].items; k++)
            addr += eeRead(addr, menus[i].item[k].val);
    }

    chipSelect(__SCREEN__I2C);
}


/*
=====================
 void lampOfGod
=====================
*/
void lampOfGod() {

    display.clrScr();
    display.invert(true);
    display.update();
    delay(100);

    display.invert(false);
}


/*
=====================
 void timerIsr
=====================
*/
void timerIsr() {
        encoder->service();
}


/*
=====================
 void setup
=====================
*/
void setup() {
    Serial.begin(9600);

    pinMode(PiezoPin, OUTPUT);
    pinMode(PushButtonPin, INPUT);

    encoder = new ClickEncoder(2, 3, -1, StepsPerNotch);
    encoder->setAccelerationEnabled(false);


    Wire.begin();
    Wire.setClock(400000);

//    #if defined(FirstRun)
//        storeData();
//    #else     
//        loadData();
//    #endif

    chipSelect(__LCD__I2C);
    lcd.init();
    lcd.noBacklight();
    
    chipSelect(__SCREEN__I2C);
    display.begin(SSD1306_128X64);
    display.setFont(SmallFont);

    Timer1.initialize(1000);
    Timer1.attachInterrupt(timerIsr);

    //powerOn();
}


/*
=====================
 boolean hasMask
=====================
*/
boolean hasMask(MenuItem *it, ItemAttributes attr) {
    return ((it->properties & attr) != 0);
}


/*
=====================
 boolean IsItem
=====================
*/
boolean IsItem(MenuItem *it) {
    return ((it->properties & Item) == Item);
}


/*
=====================
 boolean IsLocked
=====================
*/
boolean IsLocked(MenuItem *it) {
    return hasMask(it, Protected);
}


/*
=====================
 void unlockItem
=====================
*/
void unlockItem(MenuItem *it) {
    it->properties &= ~Protected;
}


/*
=====================
 void showItem
=====================
*/
void showItem(MenuItem *it, bool visible) {
    if (visible) it->properties &= ~Hide;
    else it->properties |= Hide;
}


/*
=====================
 void drawBox
=====================
*/
void drawBox(int x, int y, int w, int h) {
    for (int i = 0; i < w; i++) display.drawRect(x, y, x + i, y + h);
}


/*
=====================
 void drawScrollbar
=====================
*/
void drawScrollbar() {
    // Draw a vertical line of dots.
    for (int i = 0; i < SCR_HEIGHT - 2; i += 2) {
        display.setPixel(SCR_WIDTH - 3, i);
    }
    // Draw the handle.
    drawBox(124, 62 / arrayLen * cursor, 3, 62 / arrayLen);
}


/*
=====================
 void cusorDown
=====================
*/
void cursorDown() {
    if (rotary_off)
        return;
        
    if (cursor == arrayLen - 1) {
        if (loopMenu)
        {
            cursor = 0;
            rectY = 0;
            scroll = 0;

            keyPressTone();
        }

        return;
    }

    cursor++;
       
    // Begin scrolling
    if (cursor >= screenEnd) {
        rectY = screenEnd - 1;
        scroll++;
    } else {
        rectY++;
    }

    keyPressTone();
}


/*
=====================
 void cursorUp
=====================
*/
// Button up
void cursorUp()  {
    if (rotary_off)
        return;        
        
    if (cursor == 0) {
        if (loopMenu) {
            cursor = arrayLen - 1;

            if (arrayLen < screenEnd) {
                rectY = cursor;
                scroll = 0;
            } else {
                rectY = screenEnd - 1;
                scroll = arrayLen - screenEnd;
            }

            keyPressTone();
        }

        return;
    }

    if (cursor >= screenEnd) {
        rectY = screenEnd - 1;
        scroll--;
    } else {
        rectY--;
    }

    cursor--;
    
    keyPressTone();
}


/*
=====================
 void playLoad
=====================
*/
void playLoad() {
    // Back to menu
    // if (buttonClicked()) 
    // {
    //     buttonPressed_i = 0;
    //     rotary_off = false;
    // }
    
    display.clrScr();
}

/*
=====================
 void exitMain
=====================
*/
void exitMain() {

    cursor          =  0;
    rectY           =  0;
    scroll          =  0;
    indexMenu       =  0;
    buttonPressed_i = -1;
    rotary_off = true;
}


/*
=====================
 void leaveMenu
=====================
*/
void leaveMenu() {

    // Back to menu
    cursor          = _cr;
    rectY           = _ry;
    scroll          = _sl;
    redraw = true;
    rotary_accel = false;
    inMenu = true;
    buttonPressed_i = 0;
}


/*
=====================
 void countDownFast
=====================
*/
void countDownFast() {
    countDown();
}


/*
=====================
 void countUpFast
=====================
*/
void countUpFast() {
    countUp();
}


/*
=====================
 void countUp
=====================
*/
void countUp() {
    MenuItem *it = itemsList[cursor];
    if (rotary_off) return;
    if (it->val < it->max) it->val++;
    // Refresh
    option();
}


/*
=====================
 void countDown
=====================
*/
void countDown() {
    MenuItem *it = itemsList[cursor];
    if (rotary_off) return;
    if (it->val > it->min) it->val--;
    // Refresh
    option();
}


/*
=====================
 void leaveItem
=====================
*/
void closeItem() {
    // Re-drawing is in loop()
    redraw = true;
    encoder->setAccelerationEnabled(false);
    rotary_accel = false;
    buttonPressed_i = 1;
    inMenu = false;
}


/*
=====================
 void setItem 
=====================
*/
// Visualizza il nuovo valore con countUp e countDown
void option() {
    MenuItem *it = itemsList[cursor];
    const char *prompt[][4] = {{ " ON    <OFF>" }, { "<ON>    OFF " },
        { " YES    <NO>" },{ "<YES>    NO " }
    };
       
    display.invertText(true);
    drawRect();
    
    if (hasMask(it, OnOff))
                            display.print(*prompt[it->val], 
                            MARGIN_L * 2, 
                            MENU_H * rectY + CENTER_TEXT);
    else if (hasMask(it, YesNo))
                            display.print(*prompt[it->val+2], 
                            MARGIN_L * 2, 
                            MENU_H * rectY + CENTER_TEXT);
    else {
        // DON'T USE printNumI. Strange pixel appear at the bottom
        // in the right corner of the screen.
        char num[10] = { 0 };   
        sprintf(num, "%ld", it->val);
        display.print(num, CENTER, MENU_H * rectY + CENTER_TEXT);
    }


    display.update();
}


/*
=====================
 void openItem
=====================
*/
void openItem(MenuItem *mi) {
    if (IsLocked(mi))
        return;
        
    encoder->setAccelerationEnabled(true);

    rotary_accel = true;
    redraw = false;

    mi->action();
}


/*
=====================
 void drawRect
=====================
*/
void drawRect() {
    // There is no fillRect() like in Adafruit_gfx library.
    // Draw a rectangle increasing gradually the width
    // so that it appears filled.
    for (int i = 0; i < SCR_WIDTH; i++) {
        //display.drawRect(0, (MENU_H * rectY),
        //  i, (MENU_H * rectY) + startY + MENU_H - 1);
        display.drawLine(i, (MENU_H * rectY), i, (MENU_H * rectY) + startY + MENU_H);
    }
}

typedef struct {
    boolean isPressed,
            isReleased,
            isClicked,
            isHeld;
    unsigned long pressedTime;
} EncoderButton;

EncoderButton button;


void updateButton() {
    static int stateA = LOW;
    int stateB = (PINB & (1 << PINB0) ? LOW : HIGH);
    
    if (stateB == HIGH && stateA == LOW) {
        button.isPressed = true;
        button.pressedTime = millis();
    } else {
        button.isPressed = false;
    }

    if (stateB == LOW && stateA == HIGH) {
        button.isReleased = true;
        if (millis() - button.pressedTime < BUTTON_HOLDTIME) {
            button.isClicked = true;
        }
    } else {
        button.isReleased = false;
        button.isClicked = false;
    }

    button.isHeld = (stateB == HIGH && millis() - button.pressedTime >= BUTTON_HOLDTIME);

    stateA = stateB;
}

boolean buttonClicked() {
    if (button.isClicked) {
        button.isClicked = false;
        return true;
    }
    return false;
}

boolean buttonHold() {
    return button.isHeld;
}

boolean buttonPressed() {
    return button.isPressed;
}

boolean buttonReleased() {
   return button.isReleased;
}

void drawImage(const char *icon) {
    redraw = false;
    drawRect();

    display.invertText(true);
    display.print(icon, CENTER, MENU_H * rectY + CENTER_TEXT);
    display.update();
}

/*
=====================
 void window
=====================
*/
void window() {
    MenuItem *mi;
    
    if (!inMenu) mi = itemsList[cursor];
    if (!inMenu && IsLocked(mi)) {
        stopPressEvent = true;

        if (buttonPressed()) drawImage("X");

        // Sblocca con tasto premuto a lungo
        if (buttonHold()) 
        {
            unlockItem(mi); 
            buttonPressed_i = 2;
        }
    }

    if (!stopPressEvent) 
    {
        if (buttonClicked()) {
            buttonPressed_i++;
        }
    } 

    int16_t delta = rotary_off ? 0 : encoder->getValue();
    if (delta != 0 && !button.isReleased) {
        if (rotary_accel)
          (delta < 0) ? countUp() : countDown();
        else
          (delta < 0) ? cursorUp() : cursorDown();
        // Resettare dopo drawImage 
        redraw = true; 
    } else { stopPressEvent = false; }

    // Browse menu
    switch (buttonPressed_i) {
        case 0: 
            drawMenus();
            break;
        case 1:
            openMenu();
            break;
        case 2:
            openItem(mi); 
            break;
        case 3:
            closeItem();
            break;
            
        default:
            playLoad();
            break;
    }
  
  
    updateButton();

    // Get rid of delay
    if (buttonPressed_i != 1) delay(100);

    // Manage items stuff here
    checkMe();
}


/*
=====================
 uint8_t createList
=====================
*/
uint8_t createList(MenuItem *lst[]) {
    int cnt = 0;
    
    // Clear list
    for (int i = 0; i < MAX_ITEMS; i++) lst[i] = NULL;
    arrayLen = 0;
       
    // Create new list      
    for (int i = 0; i < menus[indexMenu].items; i++) {
        MenuItem *it = &menus[indexMenu].item[i];
        // Skip them
        if (hasMask(it, Hide) || hasMask(it, Label))
            continue;
        lst[cnt] = it;
        cnt++;
    }

    return cnt;
}


/*
=====================
 void drawMenus
=====================
*/
void drawMenus() {   
    int y = startY;
    arrayLen = LIST(menus);

    display.clrScr();
    display.invertText(false);
    display.print(">", 0, MENU_H * rectY + CENTER_TEXT);
    drawScrollbar();

    for (int i = scroll; i < arrayLen; i++) {
        display.print(menus[i].text, MARGIN_L*2, y + CENTER_TEXT);
        y = y + MENU_H;
    }

    _cr = cursor;
    _ry = rectY;
    _sl = scroll;

    display.update();
}


/*
=====================
 void clearRect
=====================
*/
void clearRect() {
    memset(rect_buf, ' ', BUFSIZE);
    rect_buf[BUFSIZE - 1] = '\0';
}


/*
=====================
 void drawItems
=====================
*/
void drawItems() { 
  MenuItem *it;
  boolean preview = true;
  int y = startY;
  
  // Link items list
  arrayLen = createList(itemsList); 
  if (redraw == false) return;
  display.clrScr();
  drawRect();

  for (int i = scroll; i < arrayLen; i++) {
    it = itemsList[i];
    
    display.invertText(cursor == i);
    preview = true;
    //
    // Scroll content
    //
    if (cursor == i &&   hasMask(it, Item)
                    &&   hasMask(it, Scrolling)
                    && ! hasMask(it, Button)
                    && ! hasMask(it, Label)) {
      preview = false;
      
      clearRect();
      
      // [<-               ]
      if (direction == 0) {
        head++;
        tail = strlen(it->text) - head;
        if (tail <= 0) { direction = 1; head = 0; }
        memcpy(rect_buf, it->text + head, max(0, tail));
      } 
      // [               <-]
      if (direction == 1) {
        tail = min(strlen(it->text), BUFSIZE - offset);
        memcpy(rect_buf + offset, it->text, tail);
        if (--offset < 0) { direction = 0; offset = BUFSIZE - 1; }
      }
      
      display.print(rect_buf, MARGIN_L, y + CENTER_TEXT);
      delay(10);
    } else {
      //
      // Print setting
      //
      display.print(it->text, MARGIN_L, y + CENTER_TEXT);
      
      if (preview   &&  hasMask(it, Item)
                    && !hasMask(it, Button)
                    && !hasMask(it, Label)) {
        char option[10] = { 0 };
        
        if (hasMask(it, OnOff))
          sprintf(option, "%s", it->val == 1 ? "ON" : "OFF");
        else if (hasMask(it, YesNo))
          sprintf(option, "%s", it->val == 1 ? "YES" : "NO");
        else
          sprintf(option, "%ld", it->val);
          
        // Print on the right screen
        int fw = strlen(option) * FONT_W;
        display.print(option, MARGIN_R - fw - MARGIN_L, y + CENTER_TEXT);
      }
    }

    y = y + MENU_H;
  }
  
  display.update();
}


/*
=====================
 void openMenu
=====================
*/
void openMenu() {
    MenuItem *it = menus[cursor].item;

    // Prima di aprire il menu.
    if (inMenu) {
        if (hasMask(it, Button)) {
            it->action();
            buttonPressed_i = 0;
            return;
        }

        // Salva cursori
        indexMenu = cursor;
        cursor = 0;
        rectY = 0;

        inMenu = false;
    }

    // Disegna il menu aperto
    drawItems();
}

/*
=====================
 void loop
=====================
*/
void loop() {
    window();
}

// eof
   
