/* Hardware: Connect a DIN module to J8/J9. Connect 12 pushbuttons to the first
             12 inputs. They serve as scene/mute selection switches.
             Connect the Kill switch to input 13, the Sync switch to input 14
             and the MuteMode switch to input 15.

             Connect a DOUT to J8/J9 as well. Add LEDs to the first 12 outputs.
             Those will serve as a indication for the selected scene/mute
             and correspond to the buttons with the same number.
             Connect the Kill, Sync and MuteMode LEDs to output 13, 14 & 15.

             For the STM32F1 core:
             Connect 12 potentiometers to the 12 analog inputs on J5A-J5C.
             They will control the performance macros on the Rytm.
             For the STM32F4 core:
             Connect the first two 4051 multiplexers according to the
             schematic of the AINx4 module. They will multiplex 12 inputs to
             the first two analog inputs of J5A.

             This is how the two MIDI ports are used:
                MIDI 1 In:  Data from here will be forwarded to the Rytm.
                MIDI 1 Out: This is a THRU port for MIDI 1 In (if clocked externally)
                            or a THRU port for MIDI 2 In (if clocked from the Rytm)
                MIDI 2 In:  Connect this to the Rytms MIDI Out for feedback over
                            over the track mute states (and for syncing to the
                            Rytms clock output, if that is the selected sync
                            source)
                MIDI 2 Out: Connect this to the Rytms MIDI Input

             The Analog Rytm will have to be configured like this:
                - MIDI Track channels: Channel 1-12 for Track 1-12 respectively
                - MIDI Scene channel: Channel 15
                - MIDI Transport and clock send enabled (for syncing to Rytms clock)
                or: MIDI Transport and clock receive enabled (for syncing to MIDI 1 In)
                - MIDI CC receive enabled.
                - Parameter format: CC (not NRPN!)
                - optional: Encoder destination: Int+Ext for feedback over the seected scene
                - optional: Mute destination: Int+Ext for feedback over track mute states
*/

/////////////////////////////////////////////////////////////////////////////
// Include files
/////////////////////////////////////////////////////////////////////////////

#include <mios32.h>
#include <eeprom.h>
#include "app.h"

typedef uint8_t bool;
enum { false = 0, true };

// performance potentiometers
uint8_t lastValue[12];
const uint8_t potCC[12] = { 35, 36, 37, 39, 40, 41, 42, 43, 44, 45, 46, 47 };
bool performanceKill;
bool queuedPerformanceKillState;

// scene changes
int8_t currentScene;
const uint8_t sceneCCValue[13] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
int8_t queuedScene;

// track mute states
uint16_t currentTrackMutes;
uint16_t queuedTrackMutes;

// settings
typedef enum
{
    syncToMidi1 = 0,
    syncToRytm
} syncSource_t;

typedef enum
{
    _1_16 = 1,
    _1_8  = 2,
    _1_4  = 4,
    _1_2  = 8
} syncDenominator_t;

typedef union
{
    struct __attribute__((packed))
    {
        uint8_t sync:1;     // true == mute, performance kill and scene changes are synced
        uint8_t muteMode:1; // true == buttons change mute states, false == buttons change scene
        uint8_t syncNominator:5;
        syncSource_t syncSource:1;
        syncDenominator_t syncDenominator:8;
        uint16_t killEnable; // bit flags for enabling kill on selected perf. macros
    } readable;
    uint16_t raw[2];
} settings_t;
settings_t settings;

// counters, UI things and other volatile stuff.
int syncCounter;
int runTestSyncCounter;
int syncTimeout;
int blinkCounter;
int syncFlashPulseCounter;
typedef enum
{
    dontShowSettings = 0,
    showKillEnable,
    showSyncOptions
} settingsDisplay_t;
settingsDisplay_t showSettings;
typedef enum
{
    stopped,
    running
} runMode_t;
runMode_t runMode;
bool ignoreNextSyncBttnRelease;
bool ignoreNextMuteBttnRelease;
bool syncBttnState;
bool muteBttnState;

#define BLINK_MAX       500
#define SLOW_BLINK      (blinkCounter > BLINK_MAX/2)
#define FAST_BLINK      ((blinkCounter % BLINK_MAX/2) > BLINK_MAX/4)
#define FLASH_PULSE     250

#define SYNC_TIMEOUT    500 // 500ms with no clock signal => stopped.

#define POT_FIRST       0
#define SWITCH_FIRST    0
#define SWITCH_KILL     12
#define SWITCH_SYNC     13
#define SWITCH_MUTEMODE 14

#define LED_FIRST       0
#define LED_KILL        12
#define LED_SYNC        13
#define LED_MUTEMODE    14

#define SCENE_CC        92
#define MUTE_CC         94

// local prototypes
static s32 NOTIFY_MIDI_Rx(mios32_midi_port_t port, u8 midi_byte);
static void triggerSceneSync();
static void triggerKillSync();
static void triggerMuteSync();
static void updateLEDs();
static void checkEnterSettings();
static void displaySettings();
static void storeSettings();
static void loadSettings();
static void initSettings();

/////////////////////////////////////////////////////////////////////////////
// switches the LEDs on or off to indicate the selected scene
/////////////////////////////////////////////////////////////////////////////
static void triggerSceneSync()
{
    if (queuedScene >= 0)
    {
        currentScene = queuedScene;
        queuedScene = -1;
        MIOS32_MIDI_SendCC(UART1, Chn1, SCENE_CC, sceneCCValue[currentScene]);
    }
}

static void triggerKillSync()
{
    if (queuedPerformanceKillState != performanceKill)
    {
        performanceKill = queuedPerformanceKillState;
        if (performanceKill)
        {
            int i;
            for (i = 0; i < 12; i++)
            {
                if (settings.readable.killEnable & (1 << i))
                    MIOS32_MIDI_SendCC(UART1, Chn1, potCC[i], 0);
            }
        }
        else
        {
            int i;
            for (i = 0; i < 12; i++)
                MIOS32_MIDI_SendCC(UART1, Chn1, potCC[i], lastValue[i]);
        }
    }
}

static void triggerMuteSync()
{
    int i;
    for (i = 0; i < 12; i++)
    {
        bool isMuted = (currentTrackMutes & (1<<i))?1:0;
        bool isQueued = (queuedTrackMutes & (1<<i))?1:0;
        if (isMuted != isQueued)
        {
            MIOS32_MIDI_SendCC(UART1, Chn1 + i, MUTE_CC, isQueued?127:0);
        }
    }
    currentTrackMutes = queuedTrackMutes;
}

static void updateLEDs()
{
    if (settings.readable.muteMode)
    {
        MIOS32_DOUT_PinSet(LED_MUTEMODE, 1);

        // turn on the led for each unmuted track
        int i;
        for (i = 0; i < 12; i++)
        {
            bool isMuted = (currentTrackMutes & (1<<i))?1:0;
            bool isQueued = (queuedTrackMutes & (1<<i))?1:0;
            if (isMuted != isQueued)
            {
                MIOS32_DOUT_PinSet(i, FAST_BLINK?1:0);
            }
            else
            {
                MIOS32_DOUT_PinSet(i, isMuted?0:1);
            }
        }
    }
    else
    {
        MIOS32_DOUT_PinSet(LED_MUTEMODE, 0);

        // turn on the led for the selected scene
        int i;
        for (i = 0; i < 12; i++)
            MIOS32_DOUT_PinSet(i, (i==(currentScene - 1))?1:0);
        // if there's a scene change queued - display that
        if (queuedScene >= 0)
        {
            // soon switching off the scene
            if ((queuedScene == 0) && (currentScene > 0))
            {
                MIOS32_DOUT_PinSet(currentScene - 1, FAST_BLINK?1:0);
            }
            else if (queuedScene > 0)
            {
                MIOS32_DOUT_PinSet(queuedScene - 1, FAST_BLINK?1:0);
            }
        }
    }

    // turn on the led for the kill state
    MIOS32_DOUT_PinSet(LED_KILL, performanceKill?1:0);
    // if there's a kill state change queued - display that
    if (queuedPerformanceKillState != performanceKill)
        MIOS32_DOUT_PinSet(LED_KILL, FAST_BLINK?1:0);

    // set the sync led
    if (settings.readable.sync)
    {
        // when synced: led is on, briefly flashes of on the sync point
        // if no tempo signal is present, flash continuously
        if ((syncCounter == 0) && (syncFlashPulseCounter == 0))
            syncFlashPulseCounter = FLASH_PULSE;
        if (syncFlashPulseCounter)
            syncFlashPulseCounter--;
        MIOS32_DOUT_PinSet(LED_SYNC, (syncFlashPulseCounter > FLASH_PULSE/2)?0:1);
    }
    else
        MIOS32_DOUT_PinSet(LED_SYNC, 0);
}

static void displaySettings()
{
    if (showSettings == showKillEnable)
    {
        MIOS32_DOUT_PinSet(LED_KILL, SLOW_BLINK?1:0);
        MIOS32_DOUT_PinSet(LED_SYNC, 1);
        MIOS32_DOUT_PinSet(LED_MUTEMODE, 0);

        int i;
        for (i = 0; i < 12; i++)
            MIOS32_DOUT_PinSet(i, (settings.readable.killEnable & (1 << i))?1:0);
    }
    else
    {
        MIOS32_DOUT_PinSet(LED_KILL, settings.readable.syncSource == syncToMidi1);
        MIOS32_DOUT_PinSet(LED_SYNC, 1);
        MIOS32_DOUT_PinSet(LED_MUTEMODE, SLOW_BLINK?1:0);

        int i;
        for (i = 0; i < 4; i++)
            MIOS32_DOUT_PinSet(i, (settings.readable.syncDenominator & (1 << i))?1:0);
        for (i = 4; i < 12; i++)
            MIOS32_DOUT_PinSet(i, (settings.readable.syncNominator == i)?1:0);
    }
}

static void storeSettings()
{
    int i;
    for (i = 0; i < sizeof(settings_t)/2; i++)
    {
        int32_t result = EEPROM_Write(i, settings.raw[i]);

        if (result == -1)
            MIOS32_MIDI_SendDebugMessage("Error writing settings at address %d: Page is full.", i);
        else if (result == -2)
            MIOS32_MIDI_SendDebugMessage("Error writing settings at address %d: No valid page was found.", i);
        else if (result == -3)
            MIOS32_MIDI_SendDebugMessage("Error writing settings at address %d: Flash write error.", i);
        else if (result < 0)
            MIOS32_MIDI_SendDebugMessage("Error writing settings at address %d: Unknown error %d.", i, result);
        if (result < 0)
            break;
    }
}

static void loadSettings()
{
    int i;
    for (i = 0; i < sizeof(settings_t)/2; i++)
    {
        int32_t result = EEPROM_Read(i);
        if (result >= 0)
            settings.raw[i] = result;
        else
        {
            if (result == -1)
                MIOS32_MIDI_SendDebugMessage("Error reading settings at address %d: Page not programmed yet.", i);
            else if (result == -2)
                MIOS32_MIDI_SendDebugMessage("Error reading settings at address %d: Page not found.", i);
            else
                MIOS32_MIDI_SendDebugMessage("Error reading settings at address %d: Unknown error %d.", i, result);
            initSettings();
            break;
        }
    }
}

static void initSettings()
{
    settings.readable.sync = 1;
    settings.readable.syncSource = syncToMidi1;
    settings.readable.syncNominator = 8;
    settings.readable.syncDenominator = _1_8;
    settings.readable.muteMode = 1;
    settings.readable.killEnable = 0x0fff;
}

static void checkEnterSettings()
{
    if (!muteBttnState && !syncBttnState)
    {
        ignoreNextSyncBttnRelease = 1;
        ignoreNextMuteBttnRelease = 1;

        switch (showSettings)
        {
            case dontShowSettings:
                showSettings = showKillEnable;
                break;
            case showKillEnable:
                showSettings = showSyncOptions;
                break;
            default:
            case showSyncOptions:
                storeSettings();
                showSettings = dontShowSettings;
                break;
        }
    }
}

/////////////////////////////////////////////////////////////////////////////
// This hook is called after startup to initialize the application
/////////////////////////////////////////////////////////////////////////////
void APP_Init(void)
{
    // init all onboard LEDs
    MIOS32_BOARD_LED_Init(0xffffffff);
    // init the eeprom
    EEPROM_Init(0);

    // init variables
    performanceKill = 0;
    queuedPerformanceKillState = 0;
    currentScene = 0;
    queuedScene = -1;
    currentTrackMutes = 0;
    queuedTrackMutes = 0;

    runMode = stopped;
    syncCounter = 0;
    runTestSyncCounter = 0;
    syncTimeout = 0;

    blinkCounter = 0;
    syncFlashPulseCounter = 0;
    showSettings = dontShowSettings;
    muteBttnState = 1;
    syncBttnState = 1;
    ignoreNextSyncBttnRelease = 0;
    ignoreNextMuteBttnRelease = 0;

    int i;
    for (i = POT_FIRST; i < POT_FIRST + 12; i++)
    {
        APP_AIN_NotifyChange(i, MIOS32_AIN_PinGet(i));
    }

    // init current scene
    MIOS32_MIDI_SendCC(UART1, Chn1, SCENE_CC, sceneCCValue[currentScene]);

    // install MIDI Rx callback function
    MIOS32_MIDI_DirectRxCallback_Init(NOTIFY_MIDI_Rx);

    loadSettings();
}


/////////////////////////////////////////////////////////////////////////////
// This task is running endless in background
/////////////////////////////////////////////////////////////////////////////
void APP_Background(void)
{
}


/////////////////////////////////////////////////////////////////////////////
// This hook is called each mS from the main task which also handles DIN, ENC
// and AIN events. You could add more jobs here, but they shouldn't consume
// more than 300 uS to ensure the responsiveness of buttons, encoders, pots.
// Alternatively you could create a dedicated task for application specific
// jobs as explained in $MIOS32_PATH/apps/tutorials/006_rtos_tasks
/////////////////////////////////////////////////////////////////////////////
void APP_Tick(void)
{
    // check if the sync counter still advances (clock signal is present)
    if ((runMode == running) && (runTestSyncCounter == syncCounter))
    {
        syncTimeout++;
    }
    else
    {
        runTestSyncCounter = syncCounter;
        syncTimeout = 0;
    }
    // on timeout: reset to stopped mode
    if (syncTimeout >= SYNC_TIMEOUT)
    {
        runMode = stopped;
        syncCounter = 0;
        runTestSyncCounter = 0;
        syncTimeout = 0;
    }

    blinkCounter++;
    if (blinkCounter > BLINK_MAX)
        blinkCounter = 0;

    if (showSettings)
        displaySettings();
    else
        updateLEDs();
}


/////////////////////////////////////////////////////////////////////////////
// This hook is called each mS from the MIDI task which checks for incoming
// MIDI events. You could add more MIDI related jobs here, but they shouldn't
// consume more than 300 uS to ensure the responsiveness of incoming MIDI.
/////////////////////////////////////////////////////////////////////////////
void APP_MIDI_Tick(void)
{
}


/////////////////////////////////////////////////////////////////////////////
// This hook is called when a MIDI package has been received
/////////////////////////////////////////////////////////////////////////////
void APP_MIDI_NotifyPackage(mios32_midi_port_t port, mios32_midi_package_t midi_package)
{
    //MIOS32_MIDI_SendDebugMessage("Midi port: %d: %02X %02X %02X", port, midi_package.evnt0, midi_package.evnt1, midi_package.evnt2);
    /*
    MIDI 1 In:  Data from here will be forwarded to the Rytm.
    MIDI 1 Out: If clocked externally, this is a MIDI THRU for MIDI 1 In.
                If clocked from the Rytm, this is the THRU for MIDI 2 In.
    MIDI 2 In:  Connect this to the Rytms MIDI Out for feedback over
                over the track mute states (and for syncing to the
                Rytms clock output, if that is the selected sync
                source)
    MIDI 2 Out: Connect this to the Rytms MIDI Input
    */

    // forward incoming messages.
    switch( port ) {
        case USB0:
            MIOS32_MIDI_SendPackage(UART0, midi_package);
            MIOS32_MIDI_SendPackage(UART1, midi_package);
            break;
        case UART0:
            MIOS32_MIDI_SendPackage(USB0,  midi_package);
            MIOS32_MIDI_SendPackage(UART1, midi_package);

            if (settings.readable.syncSource == syncToMidi1)
                MIOS32_MIDI_SendPackage(UART0, midi_package);
            break;
        case UART1:
            {
                if (settings.readable.syncSource == syncToRytm)
                    MIOS32_MIDI_SendPackage(UART0, midi_package);
                if (midi_package.event == CC)
                {
                    if (midi_package.value1 == MUTE_CC)
                    {
                        if (midi_package.chn <= Chn12)
                        {
                            if (midi_package.value2 > 0)
                            {
                                currentTrackMutes |= (1 << midi_package.chn);
                                queuedTrackMutes |= (1 << midi_package.chn);
                            }
                            else
                            {
                                currentTrackMutes &= ~(1 << midi_package.chn);
                                queuedTrackMutes &= ~(1 << midi_package.chn);
                            }
                        }
                    }
                    else if (midi_package.value1 == SCENE_CC)
                    {
                        int sceneNumber;
                        for (sceneNumber = 0; sceneNumber < 12; sceneNumber++)
                        {
                            if (midi_package.value2 <= sceneCCValue[sceneNumber])
                                break;
                        }
                        currentScene = sceneNumber;
                    }
                }
            } break;
    }
}


/////////////////////////////////////////////////////////////////////////////
// This hook is called before the shift register chain is scanned
/////////////////////////////////////////////////////////////////////////////
void APP_SRIO_ServicePrepare(void)
{
}


/////////////////////////////////////////////////////////////////////////////
// This hook is called after the shift register chain has been scanned
/////////////////////////////////////////////////////////////////////////////
void APP_SRIO_ServiceFinish(void)
{
}


/////////////////////////////////////////////////////////////////////////////
// This hook is called when a button has been toggled
// pin_value is 1 when button released, and 0 when button pressed
/////////////////////////////////////////////////////////////////////////////
void APP_DIN_NotifyToggle(u32 pin, u32 pin_value)
{
    // for testing the wiring
    //MIOS32_MIDI_SendDebugMessage("Digital pin: %d = %d", pin, pin_value);
    //MIOS32_DOUT_PinSet(pin, (pin_value == 0)? 1:0);

    if ((pin >= SWITCH_FIRST) && (pin < SWITCH_FIRST + 12))
    {
        if (pin_value)
            return;

        if (showSettings == showKillEnable)
        {
            int i = pin - SWITCH_FIRST;
            settings.readable.killEnable ^= (1 << i);
        }
        else if (showSettings == showSyncOptions)
        {
            int i = pin - SWITCH_FIRST;
            if (i < 4)
                settings.readable.syncDenominator = 1 << i;
            else
                settings.readable.syncNominator = i;
        }
        else if (settings.readable.muteMode)
        {
            bool isMuted = (queuedTrackMutes & (1 << pin))?1:0;
            if (isMuted)
                queuedTrackMutes &= ~(1 << pin);
            else
                queuedTrackMutes |= (1 << pin);

            if (!settings.readable.sync || runMode == stopped)
                triggerMuteSync();
        }
        else
        {
            int newScene = pin - SWITCH_FIRST + 1;
            if (newScene == queuedScene) // there's something queued - abort
                queuedScene = -1;
            else if (newScene == currentScene) // switch off scene
                queuedScene = 0;
            else
                queuedScene = newScene;

            if (!settings.readable.sync || runMode == stopped)
                triggerSceneSync();
        }
    }
    else if (pin == SWITCH_KILL)
    {
        if (pin_value)
            return;

        if (showSettings == showSyncOptions)
        {
            settings.readable.syncSource = (settings.readable.syncSource == syncToMidi1)?syncToRytm:syncToMidi1;
            triggerKillSync();
            triggerSceneSync();
            triggerMuteSync();
            syncCounter = 0;
            runMode = stopped;
        }
        else if (showSettings == dontShowSettings)
        {
            queuedPerformanceKillState = !queuedPerformanceKillState;

            if (!settings.readable.sync || runMode == stopped)
                triggerKillSync();
        }
    }
    else if (pin == SWITCH_SYNC)
    {
        syncBttnState = pin_value;
        checkEnterSettings();

        if (!pin_value)
            return;

        if (ignoreNextSyncBttnRelease)
        {
            ignoreNextSyncBttnRelease = 0;
            return;
        }

        settings.readable.sync = !settings.readable.sync;
        triggerKillSync();
        triggerSceneSync();
        triggerMuteSync();
    }
    else if (pin == SWITCH_MUTEMODE)
    {
        muteBttnState = pin_value;
        checkEnterSettings();

        if (!pin_value)
            return;

        if (ignoreNextMuteBttnRelease)
        {
            ignoreNextMuteBttnRelease = 0;
            return;
        }

        settings.readable.muteMode = !settings.readable.muteMode;
    }
}


/////////////////////////////////////////////////////////////////////////////
// This hook is called when an encoder has been moved
// incrementer is positive when encoder has been turned clockwise, else
// it is negative
/////////////////////////////////////////////////////////////////////////////
void APP_ENC_NotifyChange(u32 encoder, s32 incrementer)
{
}


/////////////////////////////////////////////////////////////////////////////
// This hook is called when a pot has been moved
/////////////////////////////////////////////////////////////////////////////
void APP_AIN_NotifyChange(u32 pin, u32 pin_value)
{
    // convert 12bit value to 7bit value
    u8 value_7bit = pin_value >> 5;
    //MIOS32_MIDI_SendDebugMessage("Analog pin: %d = %d", pin, pin_value);

    if ((pin >= POT_FIRST) && (pin < POT_FIRST + 12))
    {
        lastValue[pin - POT_FIRST] = value_7bit;
        if (!(performanceKill && (settings.readable.killEnable & (1 << (pin - POT_FIRST)))))
            MIOS32_MIDI_SendCC(UART1, Chn15, potCC[pin - POT_FIRST], value_7bit);
    }
}

/////////////////////////////////////////////////////////////////////////////
// Installed via MIOS32_MIDI_DirectRxCallback_Init
/////////////////////////////////////////////////////////////////////////////
static s32 NOTIFY_MIDI_Rx(mios32_midi_port_t port, u8 midi_byte)
{
    if (   ((port == UART0) && (settings.readable.syncSource == syncToMidi1))
        || ((port == USB0)  && (settings.readable.syncSource == syncToMidi1))
        || ((port == UART1) && (settings.readable.syncSource == syncToRytm )) )
    {
        int syncMax = settings.readable.syncNominator * settings.readable.syncDenominator * 6;
        switch (midi_byte)
        {
            case 0xF8: // clock
                {
                    if (runMode == running)
                    {
                        syncCounter++;
                        if (syncCounter >= syncMax)
                        {
                            triggerKillSync();
                            triggerSceneSync();
                            triggerMuteSync();
                            syncCounter = 0;
                        }
                    }
                } break;
            case 0xFA: // start
                {
                    runMode = running;
                    syncCounter = 0;
                    triggerKillSync();
                    triggerSceneSync();
                    triggerMuteSync();
                } break;
            case 0xFB: // continue
                {
                    runMode = running;
                } break;
            case 0xFC: // stop
                {
                    runMode = stopped;
                    syncCounter = 0;
                    triggerKillSync();
                    triggerSceneSync();
                    triggerMuteSync();
                } break;
            default:
                break;
        }
    }
    return 0; // no error, no filtering
}
