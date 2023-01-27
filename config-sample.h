// -------------------------------------------------------------------------
// Config file for One Bus Away transit clock
// Replace these values with real ones
// -------------------------------------------------------------------------


// CONNECTION SETTINGS ---------------------------------------------------
#define WIFI_NAME "placeholder"          // WiFi network name
#define WIFI_PASS "placeholder"          // WiFi network password
#define OBA_ENDPOINT "placeholder"       // OneBusAway root API endpoint
#define OBA_API_KEY "placeholder"        // OneBusAway API Key

// BUS STOPS ---------------------------------------------------------------
#define BUS_STOP_TL "placeholder"
#define BUS_STOP_BL "placeholder"
#define BUS_STOP_TR "placeholder"
#define BUS_STOP_BR "placeholder"

// CONFIGURABLES ---------------------------------------------------------
#define POLL_INTERVAL 2  // Time between searches (minutes)
#define MIN_TIME      -5 // Skip arrivals sooner than this (minutes)
#define READ_TIMEOUT 15  // Cancel query if no data received (seconds)
